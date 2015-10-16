/*
 *  Copyright (C) 2014-2015 Savoir-Faire Linux Inc.
 *
 *  Author: Adrien Béraud <adrien.beraud@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *  Additional permission under GNU GPL version 3 section 7:
 *
 *  If you modify this program, or any covered work, by linking or
 *  combining it with the OpenSSL project's OpenSSL library (or a
 *  modified version of that library), containing parts covered by the
 *  terms of the OpenSSL or SSLeay licenses, Savoir-Faire Linux Inc.
 *  grants you additional permission to convey the resulting work.
 *  Corresponding Source for a non-source form of such a combination
 *  shall include the source code for the parts of OpenSSL used as well
 *  as that of the covered work.
 */

#include "tools_common.h"
#include <opendht/rng.h>

extern "C" {
#include <gnutls/gnutls.h>
}
#include <ctime>

using namespace dht;

static std::mt19937_64 rd {dht::crypto::random_device{}()};
static std::uniform_int_distribution<dht::Value::Id> rand_id;

const std::string printTime(const std::time_t& now) {
    struct tm tstruct = *localtime(&now);
    char buf[80];
    strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
    return buf;
}

int
main(int argc, char **argv)
{
    auto params = parseArgs(argc, argv);

    // TODO: remove with GnuTLS >= 3.3
    if (int rc = gnutls_global_init())
        throw std::runtime_error(std::string("Error initializing GnuTLS: ")+gnutls_strerror(rc));

    DhtRunner dht;
    dht.run(params.port, dht::crypto::generateIdentity("DHT Chat Node"), true);

    if (not params.bootstrap.first.empty())
        dht.bootstrap(params.bootstrap.first.c_str(), params.bootstrap.second.c_str());

    std::cout << "OpenDht node " << dht.getNodeId() << " running on port " <<  params.port << std::endl;
    std::cout << "Public key ID " << dht.getId() << std::endl;
    std::cout << "  type 'c {hash}' to join a channel" << std::endl << std::endl;

    bool connected {false};
    InfoHash room;
    const InfoHash myid = dht.getId();

    while (true)
    {
        std::cout << (connected ? ">> " : "> ");
        std::string line;
        std::getline(std::cin, line);
        static constexpr dht::InfoHash INVALID_ID {};

        if (std::cin.eof())
            break;
        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::string op, idstr;
        iss >> op;
        if (not connected) {
            if (op  == "x" || op == "q" || op == "exit" || op == "quit")
                break;
            else if (op == "c") {
                iss >> idstr;
                room = InfoHash(idstr);
                if (room == INVALID_ID) {
                    room = InfoHash::get(idstr);
                    std::cout << "Joining h(" << idstr << ") = " << room << std::endl;
                }

                dht.listen<dht::ImMessage>(room, [&](dht::ImMessage&& msg) {
                    if (msg.from != myid)
                        std::cout << msg.from.toString() << " at " << printTime(msg.date)
                                  << " (took " << print_dt(std::chrono::system_clock::now() - std::chrono::system_clock::from_time_t(msg.date))
                                  << "s) " << (msg.to == myid ? "ENCRYPTED ":"") << ": " << msg.id << " - " << msg.msg << std::endl;
                    return true;
                });
                connected = true;
            } else {
                std::cout << "Unknown command. Type 'c {hash}' to join a channel" << std::endl << std::endl;
            }
        } else {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            if (op == "d") {
                connected = false;
                continue;
            } else if (op == "e") {
                iss >> idstr;
                std::getline(iss, line);
                dht.putEncrypted(room, InfoHash(idstr), dht::ImMessage(rand_id(rd), std::move(line), now), [](bool ok) {
                    //dht.cancelPut(room, id);
                    if (not ok)
                        std::cout << "Message publishing failed !" << std::endl;
                });
            } else {
                std::getline(iss, line);
                dht.putSigned(room, dht::ImMessage(rand_id(rd), std::move(line), now), [](bool ok) {
                    //dht.cancelPut(room, id);
                    if (not ok)
                        std::cout << "Message publishing failed !" << std::endl;
                });
            }
        }
    }

    std::cout << std::endl <<  "Stopping node..." << std::endl;
    dht.join();
    gnutls_global_deinit();
    return 0;
}
