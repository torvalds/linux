/* Copyright (c) 2008 The NetBSD Foundation, Inc.
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

#if !defined(ATF_C_ERROR_H)
#define ATF_C_ERROR_H

#include <atf-c/error_fwd.h>

#include <stdbool.h>
#include <stddef.h>

/* ---------------------------------------------------------------------
 * The "atf_error" type.
 * --------------------------------------------------------------------- */

struct atf_error {
    bool m_free;
    const char *m_type;
    void *m_data;

    void (*m_format)(struct atf_error *, char *, size_t);
};

atf_error_t atf_error_new(const char *, void *, size_t,
                          void (*)(const atf_error_t, char *, size_t));
void atf_error_free(atf_error_t);

atf_error_t atf_no_error(void);
bool atf_is_error(const atf_error_t);

bool atf_error_is(const atf_error_t, const char *);
const void *atf_error_data(const atf_error_t);
void atf_error_format(const atf_error_t, char *, size_t);

/* ---------------------------------------------------------------------
 * Common error types.
 * --------------------------------------------------------------------- */

atf_error_t atf_libc_error(int, const char *, ...);
int atf_libc_error_code(const atf_error_t);
const char *atf_libc_error_msg(const atf_error_t);

atf_error_t atf_no_memory_error(void);

#endif /* !defined(ATF_C_ERROR_H) */
