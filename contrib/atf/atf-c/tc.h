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

#if !defined(ATF_C_TC_H)
#define ATF_C_TC_H

#include <stdbool.h>
#include <stddef.h>

#include <atf-c/defs.h>
#include <atf-c/error_fwd.h>

struct atf_tc;

typedef void (*atf_tc_head_t)(struct atf_tc *);
typedef void (*atf_tc_body_t)(const struct atf_tc *);
typedef void (*atf_tc_cleanup_t)(const struct atf_tc *);

/* ---------------------------------------------------------------------
 * The "atf_tc_pack" type.
 * --------------------------------------------------------------------- */

/* For static initialization only. */
struct atf_tc_pack {
    const char *m_ident;

    const char *const *m_config;

    atf_tc_head_t m_head;
    atf_tc_body_t m_body;
    atf_tc_cleanup_t m_cleanup;
};
typedef const struct atf_tc_pack atf_tc_pack_t;

/* ---------------------------------------------------------------------
 * The "atf_tc" type.
 * --------------------------------------------------------------------- */

struct atf_tc_impl;
struct atf_tc {
    struct atf_tc_impl *pimpl;
};
typedef struct atf_tc atf_tc_t;

/* Constructors/destructors. */
atf_error_t atf_tc_init(atf_tc_t *, const char *, atf_tc_head_t,
                        atf_tc_body_t, atf_tc_cleanup_t,
                        const char *const *);
atf_error_t atf_tc_init_pack(atf_tc_t *, atf_tc_pack_t *,
                             const char *const *);
void atf_tc_fini(atf_tc_t *);

/* Getters. */
const char *atf_tc_get_ident(const atf_tc_t *);
const char *atf_tc_get_config_var(const atf_tc_t *, const char *);
const char *atf_tc_get_config_var_wd(const atf_tc_t *, const char *,
                                     const char *);
bool atf_tc_get_config_var_as_bool(const atf_tc_t *, const char *);
bool atf_tc_get_config_var_as_bool_wd(const atf_tc_t *, const char *,
                                      const bool);
long atf_tc_get_config_var_as_long(const atf_tc_t *, const char *);
long atf_tc_get_config_var_as_long_wd(const atf_tc_t *, const char *,
                                      const long);
const char *atf_tc_get_md_var(const atf_tc_t *, const char *);
char **atf_tc_get_md_vars(const atf_tc_t *);
bool atf_tc_has_config_var(const atf_tc_t *, const char *);
bool atf_tc_has_md_var(const atf_tc_t *, const char *);

/* Modifiers. */
atf_error_t atf_tc_set_md_var(atf_tc_t *, const char *, const char *, ...);

/* ---------------------------------------------------------------------
 * Free functions.
 * --------------------------------------------------------------------- */

atf_error_t atf_tc_run(const atf_tc_t *, const char *);
atf_error_t atf_tc_cleanup(const atf_tc_t *);

/* To be run from test case bodies only. */
void atf_tc_fail(const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(1, 2)
    ATF_DEFS_ATTRIBUTE_NORETURN;
void atf_tc_fail_nonfatal(const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(1, 2);
void atf_tc_pass(void)
    ATF_DEFS_ATTRIBUTE_NORETURN;
void atf_tc_require_prog(const char *);
void atf_tc_skip(const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(1, 2)
    ATF_DEFS_ATTRIBUTE_NORETURN;
void atf_tc_expect_pass(void);
void atf_tc_expect_fail(const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(1, 2);
void atf_tc_expect_exit(const int, const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(2, 3);
void atf_tc_expect_signal(const int, const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(2, 3);
void atf_tc_expect_death(const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(1, 2);
void atf_tc_expect_timeout(const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(1, 2);

/* To be run from test case bodies only; internal to macros.h. */
void atf_tc_fail_check(const char *, const size_t, const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(3, 4);
void atf_tc_fail_requirement(const char *, const size_t, const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(3, 4)
    ATF_DEFS_ATTRIBUTE_NORETURN;
void atf_tc_check_errno(const char *, const size_t, const int,
                        const char *, const bool);
void atf_tc_require_errno(const char *, const size_t, const int,
                          const char *, const bool);

#endif /* !defined(ATF_C_TC_H) */
