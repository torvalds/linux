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

#include "atf-c++/detail/exceptions.hpp"

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

extern "C" {
#include "atf-c/error.h"
}

#include "atf-c++/detail/sanity.hpp"

// ------------------------------------------------------------------------
// The "system_error" type.
// ------------------------------------------------------------------------

atf::system_error::system_error(const std::string& who,
                                const std::string& message,
                                int sys_err) :
    std::runtime_error(who + ": " + message),
    m_sys_err(sys_err)
{
}

atf::system_error::~system_error(void)
    throw()
{
}

int
atf::system_error::code(void)
    const
    throw()
{
    return m_sys_err;
}

const char*
atf::system_error::what(void)
    const
    throw()
{
    try {
        if (m_message.length() == 0) {
            m_message = std::string(std::runtime_error::what()) + ": ";
            m_message += ::strerror(m_sys_err);
        }

        return m_message.c_str();
    } catch (...) {
        return "Unable to format system_error message";
    }
}

// ------------------------------------------------------------------------
// Free functions.
// ------------------------------------------------------------------------

static
void
throw_libc_error(atf_error_t err)
{
    PRE(atf_error_is(err, "libc"));

    const int ecode = atf_libc_error_code(err);
    const std::string msg = atf_libc_error_msg(err);
    atf_error_free(err);
    throw atf::system_error("XXX", msg, ecode);
}

static
void
throw_no_memory_error(atf_error_t err)
{
    PRE(atf_error_is(err, "no_memory"));

    atf_error_free(err);
    throw std::bad_alloc();
}

static
void
throw_unknown_error(atf_error_t err)
{
    PRE(atf_is_error(err));

    static char buf[4096];
    atf_error_format(err, buf, sizeof(buf));
    atf_error_free(err);
    throw std::runtime_error(buf);
}

void
atf::throw_atf_error(atf_error_t err)
{
    static struct handler {
        const char* m_name;
        void (*m_func)(atf_error_t);
    } handlers[] = {
        { "libc", throw_libc_error },
        { "no_memory", throw_no_memory_error },
        { NULL, throw_unknown_error },
    };

    PRE(atf_is_error(err));

    handler* h = handlers;
    while (h->m_name != NULL) {
        if (atf_error_is(err, h->m_name)) {
            h->m_func(err);
            UNREACHABLE;
        } else
            h++;
    }
    // XXX: I'm not sure that raising an "unknown" error is a wise thing
    // to do here.  The C++ binding is supposed to have feature parity
    // with the C one, so all possible errors raised by the C library
    // should have their counterpart in the C++ library.  Still, removing
    // this will require some code auditing that I can't afford at the
    // moment.
    INV(h->m_name == NULL && h->m_func != NULL);
    h->m_func(err);
    UNREACHABLE;
}
