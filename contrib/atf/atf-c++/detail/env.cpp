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

#include "atf-c++/detail/env.hpp"

extern "C" {
#include "atf-c/detail/env.h"
#include "atf-c/error.h"
}

#include "atf-c++/detail/exceptions.hpp"
#include "atf-c++/detail/sanity.hpp"

namespace impl = atf::env;
#define IMPL_NAME "atf::env"

// ------------------------------------------------------------------------
// Free functions.
// ------------------------------------------------------------------------

std::string
impl::get(const std::string& name)
{
    return atf_env_get(name.c_str());
}

std::string
impl::get(const std::string& name, const std::string& default_value)
{
    return atf_env_get_with_default(name.c_str(), default_value.c_str());
}

bool
impl::has(const std::string& name)
{
    return atf_env_has(name.c_str());
}

void
impl::set(const std::string& name, const std::string& val)
{
    atf_error_t err = atf_env_set(name.c_str(), val.c_str());
    if (atf_is_error(err))
        throw_atf_error(err);
}

void
impl::unset(const std::string& name)
{
    atf_error_t err = atf_env_unset(name.c_str());
    if (atf_is_error(err))
        throw_atf_error(err);
}
