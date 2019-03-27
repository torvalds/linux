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

#include "atf-c++/detail/exceptions.hpp"

extern "C" {
#include "atf-c/error.h"
}

#include <cstdio>
#include <new>

#include <atf-c++.hpp>

#include "atf-c++/detail/sanity.hpp"

// ------------------------------------------------------------------------
// The "test" error.
// ------------------------------------------------------------------------

extern "C" {

struct test_error_data {
    const char* m_msg;
};
typedef struct test_error_data test_error_data_t;

static
void
test_format(const atf_error_t err, char *buf, size_t buflen)
{
    const test_error_data_t* data;

    PRE(atf_error_is(err, "test"));

    data = static_cast< const test_error_data_t * >(atf_error_data(err));
    snprintf(buf, buflen, "Message: %s", data->m_msg);
}

static
atf_error_t
test_error(const char* msg)
{
    atf_error_t err;
    test_error_data_t data;

    data.m_msg = msg;

    err = atf_error_new("test", &data, sizeof(data), test_format);

    return err;
}

} // extern

// ------------------------------------------------------------------------
// Tests cases for the free functions.
// ------------------------------------------------------------------------

ATF_TEST_CASE(throw_atf_error_libc);
ATF_TEST_CASE_HEAD(throw_atf_error_libc)
{
    set_md_var("descr", "Tests the throw_atf_error function when raising "
               "a libc error");
}
ATF_TEST_CASE_BODY(throw_atf_error_libc)
{
    try {
        atf::throw_atf_error(atf_libc_error(1, "System error 1"));
    } catch (const atf::system_error& e) {
        ATF_REQUIRE(e.code() == 1);
        ATF_REQUIRE(std::string(e.what()).find("System error 1") !=
                  std::string::npos);
    } catch (const std::exception& e) {
        ATF_FAIL(std::string("Got unexpected exception: ") + e.what());
    }
}

ATF_TEST_CASE(throw_atf_error_no_memory);
ATF_TEST_CASE_HEAD(throw_atf_error_no_memory)
{
    set_md_var("descr", "Tests the throw_atf_error function when raising "
               "a no_memory error");
}
ATF_TEST_CASE_BODY(throw_atf_error_no_memory)
{
    try {
        atf::throw_atf_error(atf_no_memory_error());
    } catch (const std::bad_alloc&) {
    } catch (const std::exception& e) {
        ATF_FAIL(std::string("Got unexpected exception: ") + e.what());
    }
}

ATF_TEST_CASE(throw_atf_error_unknown);
ATF_TEST_CASE_HEAD(throw_atf_error_unknown)
{
    set_md_var("descr", "Tests the throw_atf_error function when raising "
               "an unknown error");
}
ATF_TEST_CASE_BODY(throw_atf_error_unknown)
{
    try {
        atf::throw_atf_error(test_error("The message"));
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        ATF_REQUIRE(msg.find("The message") != std::string::npos);
    } catch (const std::exception& e) {
        ATF_FAIL(std::string("Got unexpected exception: ") + e.what());
    }
}

// ------------------------------------------------------------------------
// Main.
// ------------------------------------------------------------------------

ATF_INIT_TEST_CASES(tcs)
{
    // Add the test cases for the free functions.
    ATF_ADD_TEST_CASE(tcs, throw_atf_error_libc);
    ATF_ADD_TEST_CASE(tcs, throw_atf_error_no_memory);
    ATF_ADD_TEST_CASE(tcs, throw_atf_error_unknown);
}
