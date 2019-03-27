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

#if !defined(ATF_C_DETAIL_DYNSTR_H)
#define ATF_C_DETAIL_DYNSTR_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include <atf-c/error_fwd.h>

/* ---------------------------------------------------------------------
 * The "atf_dynstr" type.
 * --------------------------------------------------------------------- */

struct atf_dynstr {
    char *m_data;
    size_t m_datasize;
    size_t m_length;
};
typedef struct atf_dynstr atf_dynstr_t;

/* Constants */
extern const size_t atf_dynstr_npos;

/* Constructors and destructors */
atf_error_t atf_dynstr_init(atf_dynstr_t *);
atf_error_t atf_dynstr_init_ap(atf_dynstr_t *, const char *, va_list);
atf_error_t atf_dynstr_init_fmt(atf_dynstr_t *, const char *, ...);
atf_error_t atf_dynstr_init_raw(atf_dynstr_t *, const void *, size_t);
atf_error_t atf_dynstr_init_rep(atf_dynstr_t *, size_t, char);
atf_error_t atf_dynstr_init_substr(atf_dynstr_t *, const atf_dynstr_t *,
                                   size_t, size_t);
atf_error_t atf_dynstr_copy(atf_dynstr_t *, const atf_dynstr_t *);
void atf_dynstr_fini(atf_dynstr_t *);
char *atf_dynstr_fini_disown(atf_dynstr_t *);

/* Getters */
const char *atf_dynstr_cstring(const atf_dynstr_t *);
size_t atf_dynstr_length(const atf_dynstr_t *);
size_t atf_dynstr_rfind_ch(const atf_dynstr_t *, char);

/* Modifiers */
atf_error_t atf_dynstr_append_ap(atf_dynstr_t *, const char *, va_list);
atf_error_t atf_dynstr_append_fmt(atf_dynstr_t *, const char *, ...);
void atf_dynstr_clear(atf_dynstr_t *);
atf_error_t atf_dynstr_prepend_ap(atf_dynstr_t *, const char *, va_list);
atf_error_t atf_dynstr_prepend_fmt(atf_dynstr_t *, const char *, ...);

/* Operators */
bool atf_equal_dynstr_cstring(const atf_dynstr_t *, const char *);
bool atf_equal_dynstr_dynstr(const atf_dynstr_t *, const atf_dynstr_t *);

#endif /* !defined(ATF_C_DETAIL_DYNSTR_H) */
