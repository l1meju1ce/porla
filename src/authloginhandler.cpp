#include "authloginhandler.hpp"

#include <boost/log/trivial.hpp>
#include <jwt-cpp/jwt.h>
#include <sodium.h>

#include "data/models/users.hpp"

using porla::AuthLoginHandler;

AuthLoginHandler::AuthLoginHandler(boost::asio::io_context& io, sqlite3* db)
    : m_io(io)
    , m_db(db)
{
}

void AuthLoginHandler::operator()(const std::shared_ptr<HttpContext>& ctx)
{
    const auto req = nlohmann::json::parse(ctx->Request().body());

    if (!req.contains("username") || !req.contains("password"))
    {
        return ctx->Write("Invalid request");
    }

    auto const username = req["username"].get<std::string>();
    auto const password = req["password"].get<std::string>();

    auto const user = porla::Data::Models::Users::GetByUsername(m_db, username);

    if (!user)
    {
        return ctx->Write("No");
    }

    std::thread t(
        [ctx, &io = m_io, password, user]()
        {
            int result = crypto_pwhash_str_verify(
                user->password_hashed.c_str(),
                password.c_str(),
                password.size());

            boost::asio::dispatch(
                io,
                [ctx, result, username = user->username]()
                {
                    if (result != 0)
                    {
                        return ctx->WriteJson({
                            {"error", "invalid_auth"}
                        });
                    }

                    auto token = jwt::create()
                        .set_issuer("porla")
                        .set_type("JWS")
                        .set_subject(username)
                        .sign(jwt::algorithm::hs256{"secret"});

                    ctx->WriteJson({
                        {"token", token}
                    });
                });
        });

    t.detach();
}
