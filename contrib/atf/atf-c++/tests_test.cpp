// Copyright (c) 2007 The NetBSD Foundation, Inc.
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

#include "atf-c++/tests.hpp"

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>
}

#include <fstream>
#include <iostream>
#include <sstream>

#include <atf-c++.hpp>

#include "atf-c++/detail/text.hpp"

// ------------------------------------------------------------------------
// Tests for the "atf_tp_writer" class.
// ------------------------------------------------------------------------

static
void
print_indented(const std::string& str)
{
    std::vector< std::string > ws = atf::text::split(str, "\n");
    for (std::vector< std::string >::const_iterator iter = ws.begin();
         iter != ws.end(); iter++)
        std::cout << ">>" << *iter << "<<\n";
}

// XXX Should this string handling and verbosity level be part of the
// ATF_REQUIRE_EQ macro?  It may be hard to predict sometimes that a
// string can have newlines in it, and so the error message generated
// at the moment will be bogus if there are some.
static
void
check_equal(const atf::tests::tc& tc, const std::string& str,
            const std::string& exp)
{
    if (str != exp) {
        std::cout << "String equality check failed.\n"
            "Adding >> and << to delimit the string boundaries below.\n";
        std::cout << "GOT:\n";
        print_indented(str);
        std::cout << "EXPECTED:\n";
        print_indented(exp);
        tc.fail("Constructed string differs from the expected one");
    }
}

ATF_TEST_CASE(atf_tp_writer);
ATF_TEST_CASE_HEAD(atf_tp_writer)
{
    set_md_var("descr", "Verifies the application/X-atf-tp writer");
}
ATF_TEST_CASE_BODY(atf_tp_writer)
{
    std::ostringstream expss;
    std::ostringstream ss;

#define RESET \
    expss.str(""); \
    ss.str("")

#define CHECK \
    check_equal(*this, ss.str(), expss.str())

    {
        RESET;

        atf::tests::detail::atf_tp_writer w(ss);
        expss << "Content-Type: application/X-atf-tp; version=\"1\"\n\n";
        CHECK;
    }

    {
        RESET;

        atf::tests::detail::atf_tp_writer w(ss);
        expss << "Content-Type: application/X-atf-tp; version=\"1\"\n\n";
        CHECK;

        w.start_tc("test1");
        expss << "ident: test1\n";
        CHECK;

        w.end_tc();
        CHECK;
    }

    {
        RESET;

        atf::tests::detail::atf_tp_writer w(ss);
        expss << "Content-Type: application/X-atf-tp; version=\"1\"\n\n";
        CHECK;

        w.start_tc("test1");
        expss << "ident: test1\n";
        CHECK;

        w.end_tc();
        CHECK;

        w.start_tc("test2");
        expss << "\nident: test2\n";
        CHECK;

        w.end_tc();
        CHECK;
    }

    {
        RESET;

        atf::tests::detail::atf_tp_writer w(ss);
        expss << "Content-Type: application/X-atf-tp; version=\"1\"\n\n";
        CHECK;

        w.start_tc("test1");
        expss << "ident: test1\n";
        CHECK;

        w.tc_meta_data("descr", "the description");
        expss << "descr: the description\n";
        CHECK;

        w.end_tc();
        CHECK;

        w.start_tc("test2");
        expss << "\nident: test2\n";
        CHECK;

        w.tc_meta_data("descr", "second test case");
        expss << "descr: second test case\n";
        CHECK;

        w.tc_meta_data("require.progs", "/bin/cp");
        expss << "require.progs: /bin/cp\n";
        CHECK;

        w.tc_meta_data("X-custom", "foo bar baz");
        expss << "X-custom: foo bar baz\n";
        CHECK;

        w.end_tc();
        CHECK;
    }

#undef CHECK
#undef RESET
}

// ------------------------------------------------------------------------
// Main.
// ------------------------------------------------------------------------

ATF_INIT_TEST_CASES(tcs)
{
    // Add tests for the "atf_tp_writer" class.
    ATF_ADD_TEST_CASE(tcs, atf_tp_writer);
}
