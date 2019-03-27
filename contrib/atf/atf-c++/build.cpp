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

extern "C" {
#include "atf-c/build.h"
#include "atf-c/error.h"
#include "atf-c/utils.h"
}

#include "atf-c++/detail/exceptions.hpp"
#include "atf-c++/detail/process.hpp"

namespace impl = atf::build;
#define IMPL_NAME "atf::build"

// ------------------------------------------------------------------------
// Auxiliary functions.
// ------------------------------------------------------------------------

inline
atf::process::argv_array
cargv_to_argv(const atf_list_t* l)
{
    std::vector< const char* > aux;

    atf_list_citer_t iter;
    atf_list_for_each_c(iter, l)
        aux.push_back(static_cast< const char* >(atf_list_citer_data(iter)));

    return atf::process::argv_array(aux);
}

inline
atf::process::argv_array
cargv_to_argv_and_free(char** l)
{
    try {
        atf::process::argv_array argv((const char* const*)l);
        atf_utils_free_charpp(l);
        return argv;
    } catch (...) {
        atf_utils_free_charpp(l);
        throw;
    }
}

// ------------------------------------------------------------------------
// Free functions.
// ------------------------------------------------------------------------

atf::process::argv_array
impl::c_o(const std::string& sfile, const std::string& ofile,
          const atf::process::argv_array& optargs)
{
    char** l;

    atf_error_t err = atf_build_c_o(sfile.c_str(), ofile.c_str(),
                                    optargs.exec_argv(), &l);
    if (atf_is_error(err))
        throw_atf_error(err);

    return cargv_to_argv_and_free(l);
}

atf::process::argv_array
impl::cpp(const std::string& sfile, const std::string& ofile,
          const atf::process::argv_array& optargs)
{
    char** l;

    atf_error_t err = atf_build_cpp(sfile.c_str(), ofile.c_str(),
                                    optargs.exec_argv(), &l);
    if (atf_is_error(err))
        throw_atf_error(err);

    return cargv_to_argv_and_free(l);
}

atf::process::argv_array
impl::cxx_o(const std::string& sfile, const std::string& ofile,
            const atf::process::argv_array& optargs)
{
    char** l;

    atf_error_t err = atf_build_cxx_o(sfile.c_str(), ofile.c_str(),
                                      optargs.exec_argv(), &l);
    if (atf_is_error(err))
        throw_atf_error(err);

    return cargv_to_argv_and_free(l);
}
