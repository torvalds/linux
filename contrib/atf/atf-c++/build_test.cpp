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

#include "atf-c++/build.hpp"

#include <cstring>
#include <iostream>

#include <atf-c++.hpp>

extern "C" {
#include "atf-c/h_build.h"
}

#include "atf-c++/detail/env.hpp"
#include "atf-c++/detail/process.hpp"

// ------------------------------------------------------------------------
// Auxiliary functions.
// ------------------------------------------------------------------------

template< class C >
void
print_col(const char* prefix, const C& c)
{
    std::cout << prefix << ":";
    for (typename C::const_iterator iter = c.begin(); iter != c.end();
         iter++)
        std::cout << " '" << *iter << "'";
    std::cout << "\n";
}

static
void
print_array(const char* prefix, const char* const* a)
{
    std::cout << prefix << ":";
    for (; *a != NULL; a++)
        std::cout << " '" << *a << "'";
    std::cout << "\n";
}

static
void
verbose_set_env(const char *var, const char *val)
{
    std::cout << "Setting " << var << " to '" << val << "'\n";
    atf::env::set(var, val);
}

static
bool
equal_argvs(const atf::process::argv_array& aa, const char* const* array)
{
    bool equal = true;

    atf::process::argv_array::size_type i = 0;
    while (equal && (i < aa.size() && array[i] != NULL)) {
        if (std::strcmp(aa[i], array[i]) != 0)
            equal = false;
        else
            i++;
    }

    if (equal && (i < aa.size() || array[i] != NULL))
        equal = false;

    return equal;
}

static
void
check_equal_argvs(const atf::process::argv_array& aa, const char* const* array)
{
    print_array("Expected arguments", array);
    print_col("Arguments returned", aa);

    if (!equal_argvs(aa, array))
        ATF_FAIL("The constructed argv differs from the expected values");
}

// ------------------------------------------------------------------------
// Internal test cases.
// ------------------------------------------------------------------------

ATF_TEST_CASE(equal_argvs);
ATF_TEST_CASE_HEAD(equal_argvs)
{
    set_md_var("descr", "Tests the test case internal equal_argvs function");
}
ATF_TEST_CASE_BODY(equal_argvs)
{
    {
        const char* const array[] = { NULL };
        const char* const argv[] = { NULL };

        ATF_REQUIRE(equal_argvs(atf::process::argv_array(argv), array));
    }

    {
        const char* const array[] = { NULL };
        const char* const argv[] = { "foo", NULL };

        ATF_REQUIRE(!equal_argvs(atf::process::argv_array(argv), array));
    }

    {
        const char* const array[] = { "foo", NULL };
        const char* const argv[] = { NULL };

        ATF_REQUIRE(!equal_argvs(atf::process::argv_array(argv), array));
    }

    {
        const char* const array[] = { "foo", NULL };
        const char* const argv[] = { "foo", NULL };

        ATF_REQUIRE(equal_argvs(atf::process::argv_array(argv), array));
    }
}

// ------------------------------------------------------------------------
// Test cases for the free functions.
// ------------------------------------------------------------------------

ATF_TEST_CASE(c_o);
ATF_TEST_CASE_HEAD(c_o)
{
    set_md_var("descr", "Tests the c_o function");
}
ATF_TEST_CASE_BODY(c_o)
{
    for (struct c_o_test* test = c_o_tests; test->expargv[0] != NULL;
         test++) {
        std::cout << "> Test: " << test->msg << "\n";

        verbose_set_env("ATF_BUILD_CC", test->cc);
        verbose_set_env("ATF_BUILD_CFLAGS", test->cflags);
        verbose_set_env("ATF_BUILD_CPPFLAGS", test->cppflags);

        atf::process::argv_array argv =
            atf::build::c_o(test->sfile, test->ofile,
                            atf::process::argv_array(test->optargs));
        check_equal_argvs(argv, test->expargv);
    }
}

ATF_TEST_CASE(cpp);
ATF_TEST_CASE_HEAD(cpp)
{
    set_md_var("descr", "Tests the cpp function");
}
ATF_TEST_CASE_BODY(cpp)
{
    for (struct cpp_test* test = cpp_tests; test->expargv[0] != NULL;
         test++) {
        std::cout << "> Test: " << test->msg << "\n";

        verbose_set_env("ATF_BUILD_CPP", test->cpp);
        verbose_set_env("ATF_BUILD_CPPFLAGS", test->cppflags);

        atf::process::argv_array argv =
            atf::build::cpp(test->sfile, test->ofile,
                            atf::process::argv_array(test->optargs));
        check_equal_argvs(argv, test->expargv);
    }
}

ATF_TEST_CASE(cxx_o);
ATF_TEST_CASE_HEAD(cxx_o)
{
    set_md_var("descr", "Tests the cxx_o function");
}
ATF_TEST_CASE_BODY(cxx_o)
{
    for (struct cxx_o_test* test = cxx_o_tests; test->expargv[0] != NULL;
         test++) {
        std::cout << "> Test: " << test->msg << "\n";

        verbose_set_env("ATF_BUILD_CXX", test->cxx);
        verbose_set_env("ATF_BUILD_CXXFLAGS", test->cxxflags);
        verbose_set_env("ATF_BUILD_CPPFLAGS", test->cppflags);

        atf::process::argv_array argv =
            atf::build::cxx_o(test->sfile, test->ofile,
                              atf::process::argv_array(test->optargs));
        check_equal_argvs(argv, test->expargv);
    }
}

// ------------------------------------------------------------------------
// Main.
// ------------------------------------------------------------------------

ATF_INIT_TEST_CASES(tcs)
{
    // Add the internal test cases.
    ATF_ADD_TEST_CASE(tcs, equal_argvs);

    // Add the test cases for the free functions.
    ATF_ADD_TEST_CASE(tcs, c_o);
    ATF_ADD_TEST_CASE(tcs, cpp);
    ATF_ADD_TEST_CASE(tcs, cxx_o);
}
