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

#if !defined(ATF_C_TP_H)
#define ATF_C_TP_H

#include <stdbool.h>

#include <atf-c/error_fwd.h>

struct atf_tc;

/* ---------------------------------------------------------------------
 * The "atf_tp" type.
 * --------------------------------------------------------------------- */

struct atf_tp_impl;
struct atf_tp {
    struct atf_tp_impl *pimpl;
};
typedef struct atf_tp atf_tp_t;

/* Constructors/destructors. */
atf_error_t atf_tp_init(atf_tp_t *, const char *const *);
void atf_tp_fini(atf_tp_t *);

/* Getters. */
char **atf_tp_get_config(const atf_tp_t *);
bool atf_tp_has_tc(const atf_tp_t *, const char *);
const struct atf_tc *atf_tp_get_tc(const atf_tp_t *, const char *);
const struct atf_tc *const *atf_tp_get_tcs(const atf_tp_t *);

/* Modifiers. */
atf_error_t atf_tp_add_tc(atf_tp_t *, struct atf_tc *);

/* ---------------------------------------------------------------------
 * Free functions.
 * --------------------------------------------------------------------- */

atf_error_t atf_tp_run(const atf_tp_t *, const char *, const char *);
atf_error_t atf_tp_cleanup(const atf_tp_t *, const char *);

#endif /* !defined(ATF_C_TP_H) */
