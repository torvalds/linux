/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include "internal/numbers.h"
#include <openssl/stack.h>
#include <openssl/objects.h>
#include <errno.h>
#include <openssl/e_os2.h>      /* For ossl_inline */

/*
 * The initial number of nodes in the array.
 */
static const int min_nodes = 4;
static const int max_nodes = SIZE_MAX / sizeof(void *) < INT_MAX
                             ? (int)(SIZE_MAX / sizeof(void *))
                             : INT_MAX;

struct stack_st {
    int num;
    const void **data;
    int sorted;
    int num_alloc;
    OPENSSL_sk_compfunc comp;
};

OPENSSL_sk_compfunc OPENSSL_sk_set_cmp_func(OPENSSL_STACK *sk, OPENSSL_sk_compfunc c)
{
    OPENSSL_sk_compfunc old = sk->comp;

    if (sk->comp != c)
        sk->sorted = 0;
    sk->comp = c;

    return old;
}

OPENSSL_STACK *OPENSSL_sk_dup(const OPENSSL_STACK *sk)
{
    OPENSSL_STACK *ret;

    if ((ret = OPENSSL_malloc(sizeof(*ret))) == NULL) {
        CRYPTOerr(CRYPTO_F_OPENSSL_SK_DUP, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    /* direct structure assignment */
    *ret = *sk;

    if (sk->num == 0) {
        /* postpone |ret->data| allocation */
        ret->data = NULL;
        ret->num_alloc = 0;
        return ret;
    }
    /* duplicate |sk->data| content */
    if ((ret->data = OPENSSL_malloc(sizeof(*ret->data) * sk->num_alloc)) == NULL)
        goto err;
    memcpy(ret->data, sk->data, sizeof(void *) * sk->num);
    return ret;
 err:
    OPENSSL_sk_free(ret);
    return NULL;
}

OPENSSL_STACK *OPENSSL_sk_deep_copy(const OPENSSL_STACK *sk,
                             OPENSSL_sk_copyfunc copy_func,
                             OPENSSL_sk_freefunc free_func)
{
    OPENSSL_STACK *ret;
    int i;

    if ((ret = OPENSSL_malloc(sizeof(*ret))) == NULL) {
        CRYPTOerr(CRYPTO_F_OPENSSL_SK_DEEP_COPY, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    /* direct structure assignment */
    *ret = *sk;

    if (sk->num == 0) {
        /* postpone |ret| data allocation */
        ret->data = NULL;
        ret->num_alloc = 0;
        return ret;
    }

    ret->num_alloc = sk->num > min_nodes ? sk->num : min_nodes;
    ret->data = OPENSSL_zalloc(sizeof(*ret->data) * ret->num_alloc);
    if (ret->data == NULL) {
        OPENSSL_free(ret);
        return NULL;
    }

    for (i = 0; i < ret->num; ++i) {
        if (sk->data[i] == NULL)
            continue;
        if ((ret->data[i] = copy_func(sk->data[i])) == NULL) {
            while (--i >= 0)
                if (ret->data[i] != NULL)
                    free_func((void *)ret->data[i]);
            OPENSSL_sk_free(ret);
            return NULL;
        }
    }
    return ret;
}

OPENSSL_STACK *OPENSSL_sk_new_null(void)
{
    return OPENSSL_sk_new_reserve(NULL, 0);
}

OPENSSL_STACK *OPENSSL_sk_new(OPENSSL_sk_compfunc c)
{
    return OPENSSL_sk_new_reserve(c, 0);
}

/*
 * Calculate the array growth based on the target size.
 *
 * The growth fraction is a rational number and is defined by a numerator
 * and a denominator.  According to Andrew Koenig in his paper "Why Are
 * Vectors Efficient?" from JOOP 11(5) 1998, this factor should be less
 * than the golden ratio (1.618...).
 *
 * We use 3/2 = 1.5 for simplicity of calculation and overflow checking.
 * Another option 8/5 = 1.6 allows for slightly faster growth, although safe
 * computation is more difficult.
 *
 * The limit to avoid overflow is spot on.  The modulo three correction term
 * ensures that the limit is the largest number than can be expanded by the
 * growth factor without exceeding the hard limit.
 *
 * Do not call it with |current| lower than 2, or it will infinitely loop.
 */
static ossl_inline int compute_growth(int target, int current)
{
    const int limit = (max_nodes / 3) * 2 + (max_nodes % 3 ? 1 : 0);

    while (current < target) {
        /* Check to see if we're at the hard limit */
        if (current >= max_nodes)
            return 0;

        /* Expand the size by a factor of 3/2 if it is within range */
        current = current < limit ? current + current / 2 : max_nodes;
    }
    return current;
}

/* internal STACK storage allocation */
static int sk_reserve(OPENSSL_STACK *st, int n, int exact)
{
    const void **tmpdata;
    int num_alloc;

    /* Check to see the reservation isn't exceeding the hard limit */
    if (n > max_nodes - st->num)
        return 0;

    /* Figure out the new size */
    num_alloc = st->num + n;
    if (num_alloc < min_nodes)
        num_alloc = min_nodes;

    /* If |st->data| allocation was postponed */
    if (st->data == NULL) {
        /*
         * At this point, |st->num_alloc| and |st->num| are 0;
         * so |num_alloc| value is |n| or |min_nodes| if greater than |n|.
         */
        if ((st->data = OPENSSL_zalloc(sizeof(void *) * num_alloc)) == NULL) {
            CRYPTOerr(CRYPTO_F_SK_RESERVE, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        st->num_alloc = num_alloc;
        return 1;
    }

    if (!exact) {
        if (num_alloc <= st->num_alloc)
            return 1;
        num_alloc = compute_growth(num_alloc, st->num_alloc);
        if (num_alloc == 0)
            return 0;
    } else if (num_alloc == st->num_alloc) {
        return 1;
    }

    tmpdata = OPENSSL_realloc((void *)st->data, sizeof(void *) * num_alloc);
    if (tmpdata == NULL)
        return 0;

    st->data = tmpdata;
    st->num_alloc = num_alloc;
    return 1;
}

OPENSSL_STACK *OPENSSL_sk_new_reserve(OPENSSL_sk_compfunc c, int n)
{
    OPENSSL_STACK *st = OPENSSL_zalloc(sizeof(OPENSSL_STACK));

    if (st == NULL)
        return NULL;

    st->comp = c;

    if (n <= 0)
        return st;

    if (!sk_reserve(st, n, 1)) {
        OPENSSL_sk_free(st);
        return NULL;
    }

    return st;
}

int OPENSSL_sk_reserve(OPENSSL_STACK *st, int n)
{
    if (st == NULL)
        return 0;

    if (n < 0)
        return 1;
    return sk_reserve(st, n, 1);
}

int OPENSSL_sk_insert(OPENSSL_STACK *st, const void *data, int loc)
{
    if (st == NULL || st->num == max_nodes)
        return 0;

    if (!sk_reserve(st, 1, 0))
        return 0;

    if ((loc >= st->num) || (loc < 0)) {
        st->data[st->num] = data;
    } else {
        memmove(&st->data[loc + 1], &st->data[loc],
                sizeof(st->data[0]) * (st->num - loc));
        st->data[loc] = data;
    }
    st->num++;
    st->sorted = 0;
    return st->num;
}

static ossl_inline void *internal_delete(OPENSSL_STACK *st, int loc)
{
    const void *ret = st->data[loc];

    if (loc != st->num - 1)
         memmove(&st->data[loc], &st->data[loc + 1],
                 sizeof(st->data[0]) * (st->num - loc - 1));
    st->num--;

    return (void *)ret;
}

void *OPENSSL_sk_delete_ptr(OPENSSL_STACK *st, const void *p)
{
    int i;

    for (i = 0; i < st->num; i++)
        if (st->data[i] == p)
            return internal_delete(st, i);
    return NULL;
}

void *OPENSSL_sk_delete(OPENSSL_STACK *st, int loc)
{
    if (st == NULL || loc < 0 || loc >= st->num)
        return NULL;

    return internal_delete(st, loc);
}

static int internal_find(OPENSSL_STACK *st, const void *data,
                         int ret_val_options)
{
    const void *r;
    int i;

    if (st == NULL || st->num == 0)
        return -1;

    if (st->comp == NULL) {
        for (i = 0; i < st->num; i++)
            if (st->data[i] == data)
                return i;
        return -1;
    }

    if (!st->sorted) {
        if (st->num > 1)
            qsort(st->data, st->num, sizeof(void *), st->comp);
        st->sorted = 1; /* empty or single-element stack is considered sorted */
    }
    if (data == NULL)
        return -1;
    r = OBJ_bsearch_ex_(&data, st->data, st->num, sizeof(void *), st->comp,
                        ret_val_options);

    return r == NULL ? -1 : (int)((const void **)r - st->data);
}

int OPENSSL_sk_find(OPENSSL_STACK *st, const void *data)
{
    return internal_find(st, data, OBJ_BSEARCH_FIRST_VALUE_ON_MATCH);
}

int OPENSSL_sk_find_ex(OPENSSL_STACK *st, const void *data)
{
    return internal_find(st, data, OBJ_BSEARCH_VALUE_ON_NOMATCH);
}

int OPENSSL_sk_push(OPENSSL_STACK *st, const void *data)
{
    if (st == NULL)
        return -1;
    return OPENSSL_sk_insert(st, data, st->num);
}

int OPENSSL_sk_unshift(OPENSSL_STACK *st, const void *data)
{
    return OPENSSL_sk_insert(st, data, 0);
}

void *OPENSSL_sk_shift(OPENSSL_STACK *st)
{
    if (st == NULL || st->num == 0)
        return NULL;
    return internal_delete(st, 0);
}

void *OPENSSL_sk_pop(OPENSSL_STACK *st)
{
    if (st == NULL || st->num == 0)
        return NULL;
    return internal_delete(st, st->num - 1);
}

void OPENSSL_sk_zero(OPENSSL_STACK *st)
{
    if (st == NULL || st->num == 0)
        return;
    memset(st->data, 0, sizeof(*st->data) * st->num);
    st->num = 0;
}

void OPENSSL_sk_pop_free(OPENSSL_STACK *st, OPENSSL_sk_freefunc func)
{
    int i;

    if (st == NULL)
        return;
    for (i = 0; i < st->num; i++)
        if (st->data[i] != NULL)
            func((char *)st->data[i]);
    OPENSSL_sk_free(st);
}

void OPENSSL_sk_free(OPENSSL_STACK *st)
{
    if (st == NULL)
        return;
    OPENSSL_free(st->data);
    OPENSSL_free(st);
}

int OPENSSL_sk_num(const OPENSSL_STACK *st)
{
    return st == NULL ? -1 : st->num;
}

void *OPENSSL_sk_value(const OPENSSL_STACK *st, int i)
{
    if (st == NULL || i < 0 || i >= st->num)
        return NULL;
    return (void *)st->data[i];
}

void *OPENSSL_sk_set(OPENSSL_STACK *st, int i, const void *data)
{
    if (st == NULL || i < 0 || i >= st->num)
        return NULL;
    st->data[i] = data;
    st->sorted = 0;
    return (void *)st->data[i];
}

void OPENSSL_sk_sort(OPENSSL_STACK *st)
{
    if (st != NULL && !st->sorted && st->comp != NULL) {
        if (st->num > 1)
            qsort(st->data, st->num, sizeof(void *), st->comp);
        st->sorted = 1; /* empty or single-element stack is considered sorted */
    }
}

int OPENSSL_sk_is_sorted(const OPENSSL_STACK *st)
{
    return st == NULL ? 1 : st->sorted;
}
