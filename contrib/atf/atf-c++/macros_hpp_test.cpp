// Copyright (c) 2008 The NetBSD Foundation, Inc.
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

#include <atf-c++/macros.hpp>

#include <stdexcept>

void
atf_check_errno_semicolons(void)
{
    // Check that ATF_CHECK_ERRNO does not contain a semicolon that would
    // cause an empty-statement that confuses some compilers.
    ATF_CHECK_ERRNO(1, 1 == 1);
    ATF_CHECK_ERRNO(2, 2 == 2);
}

void
atf_require_inside_if(void)
{
    // Make sure that ATF_REQUIRE can be used inside an if statement that
    // does not have braces.  Earlier versions of it generated an error
    // if there was an else clause because they confused the compiler
    // by defining an unprotected nested if.
    if (true)
        ATF_REQUIRE(true);
    else
        ATF_REQUIRE(true);
}

void
atf_require_eq_inside_if(void)
{
    // Make sure that ATF_REQUIRE_EQ can be used inside an if statement
    // that does not have braces.  Earlier versions of it generated an
    // error if there was an else clause because they confused the
    // compiler by defining an unprotected nested if.
    if (true)
        ATF_REQUIRE_EQ(true, true);
    else
        ATF_REQUIRE_EQ(true, true);
}

void
atf_require_throw_runtime_error(void)
{
    // Check that we can pass std::runtime_error to ATF_REQUIRE_THROW.
    // Earlier versions generated a warning because the macro's code also
    // attempted to capture this exception, and thus we had a duplicate
    // catch clause.
    ATF_REQUIRE_THROW(std::runtime_error, (void)0);
}

void
atf_require_throw_inside_if(void)
{
    // Make sure that ATF_REQUIRE_THROW can be used inside an if statement
    // that does not have braces.  Earlier versions of it generated an
    // error because a trailing ; after a catch block was not allowed.
    if (true)
        ATF_REQUIRE_THROW(std::runtime_error, (void)0);
    else
        ATF_REQUIRE_THROW(std::runtime_error, (void)1);
}

void
atf_require_errno_semicolons(void)
{
    // Check that ATF_REQUIRE_ERRNO does not contain a semicolon that would
    // cause an empty-statement that confuses some compilers.
    ATF_REQUIRE_ERRNO(1, 1 == 1);
    ATF_REQUIRE_ERRNO(2, 2 == 2);
}

// Test case names should not be expanded during instatiation so that they
// can have the exact same name as macros.
#define TEST_MACRO_1 invalid + name
#define TEST_MACRO_2 invalid + name
#define TEST_MACRO_3 invalid + name
ATF_TEST_CASE(TEST_MACRO_1);
ATF_TEST_CASE_HEAD(TEST_MACRO_1) { }
ATF_TEST_CASE_BODY(TEST_MACRO_1) { }
void instantiate_1(void) {
    ATF_TEST_CASE_USE(TEST_MACRO_1);
    atf::tests::tc* the_test = new ATF_TEST_CASE_NAME(TEST_MACRO_1)();
    delete the_test;
}
ATF_TEST_CASE_WITH_CLEANUP(TEST_MACRO_2);
ATF_TEST_CASE_HEAD(TEST_MACRO_2) { }
ATF_TEST_CASE_BODY(TEST_MACRO_2) { }
ATF_TEST_CASE_CLEANUP(TEST_MACRO_2) { }
void instatiate_2(void) {
    ATF_TEST_CASE_USE(TEST_MACRO_2);
    atf::tests::tc* the_test = new ATF_TEST_CASE_NAME(TEST_MACRO_2)();
    delete the_test;
}
ATF_TEST_CASE_WITH_CLEANUP(TEST_MACRO_3);
ATF_TEST_CASE_HEAD(TEST_MACRO_3) { }
ATF_TEST_CASE_BODY(TEST_MACRO_3) { }
ATF_TEST_CASE_CLEANUP(TEST_MACRO_3) { }
void instatiate_3(void) {
    ATF_TEST_CASE_USE(TEST_MACRO_3);
    atf::tests::tc* the_test = new ATF_TEST_CASE_NAME(TEST_MACRO_3)();
    delete the_test;
}
