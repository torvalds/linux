// Copyright (c) 2009 The NetBSD Foundation, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
// CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
// GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
// IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
// IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "atf-c++/detail/application.hpp"

extern "C" {
#include <unistd.h>
}

#include <atf-c++.hpp>

class getopt_app : public atf::application::app {
public:
    getopt_app(void) : app("description", "manpage") {}

    int main(void)
    {
        // Provide an option that is unknown to the application driver and
        // one that is, together with an argument that would be swallowed by
        // the test program option if it were recognized.
        int argc = 4;
        char arg1[] = "progname";
        char arg2[] = "-Z";
        char arg3[] = "-s";
        char arg4[] = "foo";
        char *const argv[] = { arg1, arg2, arg3, arg4, NULL };

        int ch;
        bool zflag;

        // Given that this obviously is an application, and that we used the
        // same driver to start, we can test getopt(3) right here without doing
        // any fancy stuff.
        zflag = false;
        while ((ch = ::getopt(argc, argv, ":Z")) != -1) {
            switch (ch) {
            case 'Z':
                zflag = true;
                break;

            case '?':
            default:
                if (optopt != 's')
                    ATF_FAIL("Unexpected unknown option found");
            }
        }

        ATF_REQUIRE(zflag);
        ATF_REQUIRE_EQ(1, argc - optind);
        ATF_REQUIRE_EQ(std::string("foo"), argv[optind]);

        return 0;
    }
};

ATF_TEST_CASE_WITHOUT_HEAD(getopt);
ATF_TEST_CASE_BODY(getopt)
{
    int argc = 1;
    char arg1[] = "progname";
    char *const argv[] = { arg1, NULL };
    ATF_REQUIRE_EQ(0, getopt_app().run(argc, argv));
}

ATF_INIT_TEST_CASES(tcs)
{
    ATF_ADD_TEST_CASE(tcs, getopt);
}
