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

#if defined(ATF_CXX_DETAIL_TEST_HELPERS_H)
#   error "Cannot include test_helpers.hpp more than once."
#else
#   define ATF_CXX_DETAIL_TEST_HELPERS_H
#endif

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <utility>

#include <atf-c++.hpp>

#include <atf-c++/detail/process.hpp>

#define HEADER_TC(name, hdrname) \
    ATF_TEST_CASE(name); \
    ATF_TEST_CASE_HEAD(name) \
    { \
        set_md_var("descr", "Tests that the " hdrname " file can be " \
            "included on its own, without any prerequisites"); \
    } \
    ATF_TEST_CASE_BODY(name) \
    { \
        header_check(hdrname); \
    }

#define BUILD_TC(name, sfile, descr, failmsg) \
    ATF_TEST_CASE(name); \
    ATF_TEST_CASE_HEAD(name) \
    { \
        set_md_var("descr", descr); \
    } \
    ATF_TEST_CASE_BODY(name) \
    { \
        if (!build_check_cxx_o_srcdir(*this, sfile)) \
            ATF_FAIL(failmsg); \
    }

namespace atf {
namespace tests {
class tc;
}
}

void header_check(const char*);
bool build_check_cxx_o(const char*);
bool build_check_cxx_o_srcdir(const atf::tests::tc&, const char*);
atf::fs::path get_process_helpers_path(const atf::tests::tc&, bool);

struct run_h_tc_data {
    const atf::tests::vars_map& m_config;

    run_h_tc_data(const atf::tests::vars_map& config) :
        m_config(config) {}
};

template< class TestCase >
void
run_h_tc_child(void* v)
{
    run_h_tc_data* data = static_cast< run_h_tc_data* >(v);

    TestCase tc;
    tc.init(data->m_config);
    tc.run("result");
    std::exit(EXIT_SUCCESS);
}

template< class TestCase >
void
run_h_tc(atf::tests::vars_map config = atf::tests::vars_map())
{
    run_h_tc_data data(config);
    atf::process::child c = atf::process::fork(
        run_h_tc_child< TestCase >,
        atf::process::stream_redirect_path(atf::fs::path("stdout")),
        atf::process::stream_redirect_path(atf::fs::path("stderr")),
        &data);
    const atf::process::status s = c.wait();
    ATF_REQUIRE(s.exited());
}
