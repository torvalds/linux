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

#include "atf-c++/detail/test_helpers.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <atf-c++.hpp>

#include "atf-c++/check.hpp"
#include "atf-c++/detail/env.hpp"
#include "atf-c++/detail/fs.hpp"
#include "atf-c++/detail/process.hpp"

// Path to the directory containing the libatf-c tests, used to locate the
// process_helpers program.  If NULL (the default), the code will use a
// relative path.  Otherwise, the provided path will be used; this is so
// that we can locate the helpers binary if the installation uses a
// different layout than the one we provide (as is the case in FreeBSD).
#if defined(ATF_C_TESTS_BASE)
static const char* atf_c_tests_base = ATF_C_TESTS_BASE;
#else
static const char* atf_c_tests_base = NULL;
#endif
#undef ATF_C_TESTS_BASE

bool
build_check_cxx_o(const char* sfile)
{
    std::vector< std::string > optargs;
    optargs.push_back("-I" + atf::env::get("ATF_INCLUDEDIR", ATF_INCLUDEDIR));
    optargs.push_back("-Wall");
    optargs.push_back("-Werror");

    return atf::check::build_cxx_o(sfile, "test.o",
                                   atf::process::argv_array(optargs));
}

bool
build_check_cxx_o_srcdir(const atf::tests::tc& tc, const char* sfile)
{
    const atf::fs::path sfilepath =
        atf::fs::path(tc.get_config_var("srcdir")) / sfile;
    return build_check_cxx_o(sfilepath.c_str());
}

void
header_check(const char *hdrname)
{
    std::ofstream srcfile("test.cpp");
    ATF_REQUIRE(srcfile);
    srcfile << "#include <" << hdrname << ">\n";
    srcfile.close();

    const std::string failmsg = std::string("Header check failed; ") +
        hdrname + " is not self-contained";
    if (!build_check_cxx_o("test.cpp"))
        ATF_FAIL(failmsg);
}

atf::fs::path
get_process_helpers_path(const atf::tests::tc& tc, bool is_detail)
{
    const char* helper = "detail/process_helpers";
    if (atf_c_tests_base == NULL) {
        if (is_detail)
            return atf::fs::path(tc.get_config_var("srcdir")) /
                   ".." / ".." / "atf-c" / helper;
        else
            return atf::fs::path(tc.get_config_var("srcdir")) /
                   ".." / "atf-c" / helper;
    } else {
        return atf::fs::path(atf_c_tests_base) / helper;
    }
}
