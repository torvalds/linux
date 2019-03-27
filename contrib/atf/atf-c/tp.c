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

#include "atf-c/tp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "atf-c/detail/fs.h"
#include "atf-c/detail/map.h"
#include "atf-c/detail/sanity.h"
#include "atf-c/error.h"
#include "atf-c/tc.h"

struct atf_tp_impl {
    atf_list_t m_tcs;
    atf_map_t m_config;
};

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

static
const atf_tc_t *
find_tc(const atf_tp_t *tp, const char *ident)
{
    const atf_tc_t *tc;
    atf_list_citer_t iter;

    tc = NULL;
    atf_list_for_each_c(iter, &tp->pimpl->m_tcs) {
        const atf_tc_t *tc2;
        tc2 = atf_list_citer_data(iter);
        if (strcmp(atf_tc_get_ident(tc2), ident) == 0) {
            tc = tc2;
            break;
        }
    }
    return tc;
}

/* ---------------------------------------------------------------------
 * The "atf_tp" type.
 * --------------------------------------------------------------------- */

/*
 * Constructors/destructors.
 */

atf_error_t
atf_tp_init(atf_tp_t *tp, const char *const *config)
{
    atf_error_t err;

    PRE(config != NULL);

    tp->pimpl = malloc(sizeof(struct atf_tp_impl));
    if (tp->pimpl == NULL)
        return atf_no_memory_error();

    err = atf_list_init(&tp->pimpl->m_tcs);
    if (atf_is_error(err))
        goto out;

    err = atf_map_init_charpp(&tp->pimpl->m_config, config);
    if (atf_is_error(err)) {
        atf_list_fini(&tp->pimpl->m_tcs);
        goto out;
    }

    INV(!atf_is_error(err));
out:
    return err;
}

void
atf_tp_fini(atf_tp_t *tp)
{
    atf_list_iter_t iter;

    atf_map_fini(&tp->pimpl->m_config);

    atf_list_for_each(iter, &tp->pimpl->m_tcs) {
        atf_tc_t *tc = atf_list_iter_data(iter);
        atf_tc_fini(tc);
    }
    atf_list_fini(&tp->pimpl->m_tcs);

    free(tp->pimpl);
}

/*
 * Getters.
 */

char **
atf_tp_get_config(const atf_tp_t *tp)
{
    return atf_map_to_charpp(&tp->pimpl->m_config);
}

bool
atf_tp_has_tc(const atf_tp_t *tp, const char *id)
{
    const atf_tc_t *tc = find_tc(tp, id);
    return tc != NULL;
}

const atf_tc_t *
atf_tp_get_tc(const atf_tp_t *tp, const char *id)
{
    const atf_tc_t *tc = find_tc(tp, id);
    PRE(tc != NULL);
    return tc;
}

const atf_tc_t *const *
atf_tp_get_tcs(const atf_tp_t *tp)
{
    const atf_tc_t **array;
    atf_list_citer_t iter;
    size_t i;

    array = malloc(sizeof(atf_tc_t *) *
                   (atf_list_size(&tp->pimpl->m_tcs) + 1));
    if (array == NULL)
        goto out;

    i = 0;
    atf_list_for_each_c(iter, &tp->pimpl->m_tcs) {
        array[i] = atf_list_citer_data(iter);
        if (array[i] == NULL) {
            free(array);
            array = NULL;
            goto out;
        }

        i++;
    }
    array[i] = NULL;

out:
    return array;
}

/*
 * Modifiers.
 */

atf_error_t
atf_tp_add_tc(atf_tp_t *tp, atf_tc_t *tc)
{
    atf_error_t err;

    PRE(find_tc(tp, atf_tc_get_ident(tc)) == NULL);

    err = atf_list_append(&tp->pimpl->m_tcs, tc, false);

    POST(find_tc(tp, atf_tc_get_ident(tc)) != NULL);

    return err;
}

/* ---------------------------------------------------------------------
 * Free functions.
 * --------------------------------------------------------------------- */

atf_error_t
atf_tp_run(const atf_tp_t *tp, const char *tcname, const char *resfile)
{
    const atf_tc_t *tc;

    tc = find_tc(tp, tcname);
    PRE(tc != NULL);

    return atf_tc_run(tc, resfile);
}

atf_error_t
atf_tp_cleanup(const atf_tp_t *tp, const char *tcname)
{
    const atf_tc_t *tc;

    tc = find_tc(tp, tcname);
    PRE(tc != NULL);

    return atf_tc_cleanup(tc);
}
