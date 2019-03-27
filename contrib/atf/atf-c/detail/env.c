/* Copyright (c) 2007 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  */

#include "atf-c/detail/env.h"

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>

#include "atf-c/detail/sanity.h"
#include "atf-c/detail/text.h"
#include "atf-c/error.h"

const char *
atf_env_get(const char *name)
{
    const char* val = getenv(name);
    PRE(val != NULL);
    return val;
}

const char *
atf_env_get_with_default(const char *name, const char *default_value)
{
    const char* val = getenv(name);
    if (val == NULL)
        return default_value;
    else
        return val;
}

bool
atf_env_has(const char *name)
{
    return getenv(name) != NULL;
}

atf_error_t
atf_env_set(const char *name, const char *val)
{
    atf_error_t err;

#if defined(HAVE_SETENV)
    if (setenv(name, val, 1) == -1)
        err = atf_libc_error(errno, "Cannot set environment variable "
                             "'%s' to '%s'", name, val);
    else
        err = atf_no_error();
#elif defined(HAVE_PUTENV)
    char *buf;

    err = atf_text_format(&buf, "%s=%s", name, val);
    if (!atf_is_error(err)) {
        if (putenv(buf) == -1)
            err = atf_libc_error(errno, "Cannot set environment variable "
                                 "'%s' to '%s'", name, val);
        free(buf);
    }
#else
#   error "Don't know how to set an environment variable."
#endif

    return err;
}

atf_error_t
atf_env_unset(const char *name)
{
    atf_error_t err;

#if defined(HAVE_UNSETENV)
    unsetenv(name);
    err = atf_no_error();
#elif defined(HAVE_PUTENV)
    char *buf;

    err = atf_text_format(&buf, "%s=", name);
    if (!atf_is_error(err)) {
        if (putenv(buf) == -1)
            err = atf_libc_error(errno, "Cannot unset environment variable"
                                 " '%s'", name);
        free(buf);
    }
#else
#   error "Don't know how to unset an environment variable."
#endif

    return err;
}
