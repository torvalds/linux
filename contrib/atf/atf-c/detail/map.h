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

#if !defined(ATF_C_DETAIL_MAP_H)
#define ATF_C_DETAIL_MAP_H

#include <stdarg.h>
#include <stdbool.h>

#include <atf-c/detail/list.h>
#include <atf-c/error_fwd.h>

/* ---------------------------------------------------------------------
 * The "atf_map_citer" type.
 * --------------------------------------------------------------------- */

struct atf_map_citer {
    const struct atf_map *m_map;
    const void *m_entry;
    atf_list_citer_t m_listiter;
};
typedef struct atf_map_citer atf_map_citer_t;

/* Getters. */
const char *atf_map_citer_key(const atf_map_citer_t);
const void *atf_map_citer_data(const atf_map_citer_t);
atf_map_citer_t atf_map_citer_next(const atf_map_citer_t);

/* Operators. */
bool atf_equal_map_citer_map_citer(const atf_map_citer_t,
                                   const atf_map_citer_t);

/* ---------------------------------------------------------------------
 * The "atf_map_iter" type.
 * --------------------------------------------------------------------- */

struct atf_map_iter {
    struct atf_map *m_map;
    void *m_entry;
    atf_list_iter_t m_listiter;
};
typedef struct atf_map_iter atf_map_iter_t;

/* Getters. */
const char *atf_map_iter_key(const atf_map_iter_t);
void *atf_map_iter_data(const atf_map_iter_t);
atf_map_iter_t atf_map_iter_next(const atf_map_iter_t);

/* Operators. */
bool atf_equal_map_iter_map_iter(const atf_map_iter_t,
                                 const atf_map_iter_t);

/* ---------------------------------------------------------------------
 * The "atf_map" type.
 * --------------------------------------------------------------------- */

/* A list-based map.  Typically very inefficient, but our maps are small
 * enough. */
struct atf_map {
    atf_list_t m_list;
};
typedef struct atf_map atf_map_t;

/* Constructors and destructors */
atf_error_t atf_map_init(atf_map_t *);
atf_error_t atf_map_init_charpp(atf_map_t *, const char *const *);
void atf_map_fini(atf_map_t *);

/* Getters. */
atf_map_iter_t atf_map_begin(atf_map_t *);
atf_map_citer_t atf_map_begin_c(const atf_map_t *);
atf_map_iter_t atf_map_end(atf_map_t *);
atf_map_citer_t atf_map_end_c(const atf_map_t *);
atf_map_iter_t atf_map_find(atf_map_t *, const char *);
atf_map_citer_t atf_map_find_c(const atf_map_t *, const char *);
size_t atf_map_size(const atf_map_t *);
char **atf_map_to_charpp(const atf_map_t *);

/* Modifiers. */
atf_error_t atf_map_insert(atf_map_t *, const char *, void *, bool);

/* Macros. */
#define atf_map_for_each(iter, map) \
    for (iter = atf_map_begin(map); \
         !atf_equal_map_iter_map_iter((iter), atf_map_end(map)); \
         iter = atf_map_iter_next(iter))
#define atf_map_for_each_c(iter, map) \
    for (iter = atf_map_begin_c(map); \
         !atf_equal_map_citer_map_citer((iter), atf_map_end_c(map)); \
         iter = atf_map_citer_next(iter))

#endif /* !defined(ATF_C_DETAIL_MAP_H) */
