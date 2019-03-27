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

#include "atf-c++/check.hpp"

#include <cstring>

extern "C" {
#include "atf-c/build.h"
#include "atf-c/error.h"
}

#include "atf-c++/detail/exceptions.hpp"
#include "atf-c++/detail/process.hpp"
#include "atf-c++/detail/sanity.hpp"

namespace impl = atf::check;
#define IMPL_NAME "atf::check"

// ------------------------------------------------------------------------
// The "check_result" class.
// ------------------------------------------------------------------------

impl::check_result::check_result(const atf_check_result_t* result)
{
    std::memcpy(&m_result, result, sizeof(m_result));
}

impl::check_result::~check_result(void)
{
    atf_check_result_fini(&m_result);
}

bool
impl::check_result::exited(void)
    const
{
    return atf_check_result_exited(&m_result);
}

int
impl::check_result::exitcode(void)
    const
{
    PRE(exited());
    return atf_check_result_exitcode(&m_result);
}

bool
impl::check_result::signaled(void)
    const
{
    return atf_check_result_signaled(&m_result);
}

int
impl::check_result::termsig(void)
    const
{
    PRE(signaled());
    return atf_check_result_termsig(&m_result);
}

const std::string
impl::check_result::stdout_path(void) const
{
    return atf_check_result_stdout(&m_result);
}

const std::string
impl::check_result::stderr_path(void) const
{
    return atf_check_result_stderr(&m_result);
}

// ------------------------------------------------------------------------
// Free functions.
// ------------------------------------------------------------------------

bool
impl::build_c_o(const std::string& sfile, const std::string& ofile,
                const atf::process::argv_array& optargs)
{
    bool success;

    atf_error_t err = atf_check_build_c_o(sfile.c_str(), ofile.c_str(),
                                          optargs.exec_argv(), &success);
    if (atf_is_error(err))
        throw_atf_error(err);

    return success;
}

bool
impl::build_cpp(const std::string& sfile, const std::string& ofile,
                const atf::process::argv_array& optargs)
{
    bool success;

    atf_error_t err = atf_check_build_cpp(sfile.c_str(), ofile.c_str(),
                                          optargs.exec_argv(), &success);
    if (atf_is_error(err))
        throw_atf_error(err);

    return success;
}

bool
impl::build_cxx_o(const std::string& sfile, const std::string& ofile,
                  const atf::process::argv_array& optargs)
{
    bool success;

    atf_error_t err = atf_check_build_cxx_o(sfile.c_str(), ofile.c_str(),
                                            optargs.exec_argv(), &success);
    if (atf_is_error(err))
        throw_atf_error(err);

    return success;
}

std::auto_ptr< impl::check_result >
impl::exec(const atf::process::argv_array& argva)
{
    atf_check_result_t result;

    atf_error_t err = atf_check_exec_array(argva.exec_argv(), &result);
    if (atf_is_error(err))
        throw_atf_error(err);

    return std::auto_ptr< impl::check_result >(new impl::check_result(&result));
}
