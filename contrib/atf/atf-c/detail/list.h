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

#if !defined(ATF_C_DETAIL_LIST_H)
#define ATF_C_DETAIL_LIST_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include <atf-c/error_fwd.h>

/* ---------------------------------------------------------------------
 * The "atf_list_citer" type.
 * --------------------------------------------------------------------- */

struct atf_list_citer {
    const struct atf_list *m_list;
    const void *m_entry;
};
typedef struct atf_list_citer atf_list_citer_t;

/* Getters. */
const void *atf_list_citer_data(const atf_list_citer_t);
atf_list_citer_t atf_list_citer_next(const atf_list_citer_t);

/* Operators. */
bool atf_equal_list_citer_list_citer(const atf_list_citer_t,
                                     const atf_list_citer_t);

/* ---------------------------------------------------------------------
 * The "atf_list_iter" type.
 * --------------------------------------------------------------------- */

struct atf_list_iter {
    struct atf_list *m_list;
    void *m_entry;
};
typedef struct atf_list_iter atf_list_iter_t;

/* Getters. */
void *atf_list_iter_data(const atf_list_iter_t);
atf_list_iter_t atf_list_iter_next(const atf_list_iter_t);

/* Operators. */
bool atf_equal_list_iter_list_iter(const atf_list_iter_t,
                                   const atf_list_iter_t);

/* ---------------------------------------------------------------------
 * The "atf_list" type.
 * --------------------------------------------------------------------- */

struct atf_list {
    void *m_begin;
    void *m_end;

    size_t m_size;
};
typedef struct atf_list atf_list_t;

/* Constructors and destructors */
atf_error_t atf_list_init(atf_list_t *);
void atf_list_fini(atf_list_t *);

/* Getters. */
atf_list_iter_t atf_list_begin(atf_list_t *);
atf_list_citer_t atf_list_begin_c(const atf_list_t *);
atf_list_iter_t atf_list_end(atf_list_t *);
atf_list_citer_t atf_list_end_c(const atf_list_t *);
void *atf_list_index(atf_list_t *, const size_t);
const void *atf_list_index_c(const atf_list_t *, const size_t);
size_t atf_list_size(const atf_list_t *);
char **atf_list_to_charpp(const atf_list_t *);

/* Modifiers. */
atf_error_t atf_list_append(atf_list_t *, void *, bool);
void atf_list_append_list(atf_list_t *, atf_list_t *);

/* Macros. */
#define atf_list_for_each(iter, list) \
    for (iter = atf_list_begin(list); \
         !atf_equal_list_iter_list_iter((iter), atf_list_end(list)); \
         iter = atf_list_iter_next(iter))
#define atf_list_for_each_c(iter, list) \
    for (iter = atf_list_begin_c(list); \
         !atf_equal_list_citer_list_citer((iter), atf_list_end_c(list)); \
         iter = atf_list_citer_next(iter))

#endif /* !defined(ATF_C_DETAIL_LIST_H) */
