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

#include "atf-c/detail/list.h"

#include <stdlib.h>
#include <string.h>

#include "atf-c/detail/sanity.h"
#include "atf-c/error.h"
#include "atf-c/utils.h"

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

struct list_entry {
    struct list_entry *m_prev;
    struct list_entry *m_next;
    void *m_object;
    bool m_managed;
};

static
atf_list_citer_t
entry_to_citer(const atf_list_t *l, const struct list_entry *le)
{
    atf_list_citer_t iter;
    iter.m_list = l;
    iter.m_entry = le;
    return iter;
}

static
atf_list_iter_t
entry_to_iter(atf_list_t *l, struct list_entry *le)
{
    atf_list_iter_t iter;
    iter.m_list = l;
    iter.m_entry = le;
    return iter;
}

static
struct list_entry *
new_entry(void *object, bool managed)
{
    struct list_entry *le;

    le = (struct list_entry *)malloc(sizeof(*le));
    if (le != NULL) {
        le->m_prev = le->m_next = NULL;
        le->m_object = object;
        le->m_managed = managed;
    } else
        free(object);

    return le;
}

static
void
delete_entry(struct list_entry *le)
{
    if (le->m_managed)
        free(le->m_object);

    free(le);
}

static
struct list_entry *
new_entry_and_link(void *object, bool managed, struct list_entry *prev,
                   struct list_entry *next)
{
    struct list_entry *le;

    le = new_entry(object, managed);
    if (le != NULL) {
        le->m_prev = prev;
        le->m_next = next;

        prev->m_next = le;
        next->m_prev = le;
    }

    return le;
}

/* ---------------------------------------------------------------------
 * The "atf_list_citer" type.
 * --------------------------------------------------------------------- */

/*
 * Getters.
 */

const void *
atf_list_citer_data(const atf_list_citer_t citer)
{
    const struct list_entry *le = citer.m_entry;
    PRE(le != NULL);
    return le->m_object;
}

atf_list_citer_t
atf_list_citer_next(const atf_list_citer_t citer)
{
    const struct list_entry *le = citer.m_entry;
    atf_list_citer_t newciter;

    PRE(le != NULL);

    newciter = citer;
    newciter.m_entry = le->m_next;

    return newciter;
}

bool
atf_equal_list_citer_list_citer(const atf_list_citer_t i1,
                                const atf_list_citer_t i2)
{
    return i1.m_list == i2.m_list && i1.m_entry == i2.m_entry;
}

/* ---------------------------------------------------------------------
 * The "atf_list_iter" type.
 * --------------------------------------------------------------------- */

/*
 * Getters.
 */

void *
atf_list_iter_data(const atf_list_iter_t iter)
{
    const struct list_entry *le = iter.m_entry;
    PRE(le != NULL);
    return le->m_object;
}

atf_list_iter_t
atf_list_iter_next(const atf_list_iter_t iter)
{
    const struct list_entry *le = iter.m_entry;
    atf_list_iter_t newiter;

    PRE(le != NULL);

    newiter = iter;
    newiter.m_entry = le->m_next;

    return newiter;
}

bool
atf_equal_list_iter_list_iter(const atf_list_iter_t i1,
                              const atf_list_iter_t i2)
{
    return i1.m_list == i2.m_list && i1.m_entry == i2.m_entry;
}

/* ---------------------------------------------------------------------
 * The "atf_list" type.
 * --------------------------------------------------------------------- */

/*
 * Constructors and destructors.
 */

atf_error_t
atf_list_init(atf_list_t *l)
{
    struct list_entry *lebeg, *leend;

    lebeg = new_entry(NULL, false);
    if (lebeg == NULL) {
        return atf_no_memory_error();
    }

    leend = new_entry(NULL, false);
    if (leend == NULL) {
        free(lebeg);
        return atf_no_memory_error();
    }

    lebeg->m_next = leend;
    lebeg->m_prev = NULL;

    leend->m_next = NULL;
    leend->m_prev = lebeg;

    l->m_size = 0;
    l->m_begin = lebeg;
    l->m_end = leend;

    return atf_no_error();
}

void
atf_list_fini(atf_list_t *l)
{
    struct list_entry *le;
    size_t freed;

    le = (struct list_entry *)l->m_begin;
    freed = 0;
    while (le != NULL) {
        struct list_entry *lenext;

        lenext = le->m_next;
        delete_entry(le);
        le = lenext;

        freed++;
    }
    INV(freed == l->m_size + 2);
}

/*
 * Getters.
 */

atf_list_iter_t
atf_list_begin(atf_list_t *l)
{
    struct list_entry *le = l->m_begin;
    return entry_to_iter(l, le->m_next);
}

atf_list_citer_t
atf_list_begin_c(const atf_list_t *l)
{
    const struct list_entry *le = l->m_begin;
    return entry_to_citer(l, le->m_next);
}

atf_list_iter_t
atf_list_end(atf_list_t *l)
{
    return entry_to_iter(l, l->m_end);
}

atf_list_citer_t
atf_list_end_c(const atf_list_t *l)
{
    return entry_to_citer(l, l->m_end);
}

void *
atf_list_index(atf_list_t *list, const size_t idx)
{
    atf_list_iter_t iter;

    PRE(idx < atf_list_size(list));

    iter = atf_list_begin(list);
    {
        size_t pos = 0;
        while (pos < idx &&
               !atf_equal_list_iter_list_iter((iter), atf_list_end(list))) {
            iter = atf_list_iter_next(iter);
            pos++;
        }
    }
    return atf_list_iter_data(iter);
}

const void *
atf_list_index_c(const atf_list_t *list, const size_t idx)
{
    atf_list_citer_t iter;

    PRE(idx < atf_list_size(list));

    iter = atf_list_begin_c(list);
    {
        size_t pos = 0;
        while (pos < idx &&
               !atf_equal_list_citer_list_citer((iter),
                                                atf_list_end_c(list))) {
            iter = atf_list_citer_next(iter);
            pos++;
        }
    }
    return atf_list_citer_data(iter);
}

size_t
atf_list_size(const atf_list_t *l)
{
    return l->m_size;
}

char **
atf_list_to_charpp(const atf_list_t *l)
{
    char **array;
    atf_list_citer_t iter;
    size_t i;

    array = malloc(sizeof(char *) * (atf_list_size(l) + 1));
    if (array == NULL)
        goto out;

    i = 0;
    atf_list_for_each_c(iter, l) {
        array[i] = strdup((const char *)atf_list_citer_data(iter));
        if (array[i] == NULL) {
            atf_utils_free_charpp(array);
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
atf_list_append(atf_list_t *l, void *data, bool managed)
{
    struct list_entry *le, *next, *prev;
    atf_error_t err;

    next = (struct list_entry *)l->m_end;
    prev = next->m_prev;
    le = new_entry_and_link(data, managed, prev, next);
    if (le == NULL)
        err = atf_no_memory_error();
    else {
        l->m_size++;
        err = atf_no_error();
    }

    return err;
}

void
atf_list_append_list(atf_list_t *l, atf_list_t *src)
{
    struct list_entry *e1, *e2, *ghost1, *ghost2;

    ghost1 = (struct list_entry *)l->m_end;
    ghost2 = (struct list_entry *)src->m_begin;

    e1 = ghost1->m_prev;
    e2 = ghost2->m_next;

    delete_entry(ghost1);
    delete_entry(ghost2);

    e1->m_next = e2;
    e2->m_prev = e1;

    l->m_end = src->m_end;
    l->m_size += src->m_size;
}
