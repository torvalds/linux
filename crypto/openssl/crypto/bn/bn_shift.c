/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <assert.h>
#include "internal/cryptlib.h"
#include "bn_lcl.h"

int BN_lshift1(BIGNUM *r, const BIGNUM *a)
{
    register BN_ULONG *ap, *rp, t, c;
    int i;

    bn_check_top(r);
    bn_check_top(a);

    if (r != a) {
        r->neg = a->neg;
        if (bn_wexpand(r, a->top + 1) == NULL)
            return 0;
        r->top = a->top;
    } else {
        if (bn_wexpand(r, a->top + 1) == NULL)
            return 0;
    }
    ap = a->d;
    rp = r->d;
    c = 0;
    for (i = 0; i < a->top; i++) {
        t = *(ap++);
        *(rp++) = ((t << 1) | c) & BN_MASK2;
        c = (t & BN_TBIT) ? 1 : 0;
    }
    if (c) {
        *rp = 1;
        r->top++;
    }
    bn_check_top(r);
    return 1;
}

int BN_rshift1(BIGNUM *r, const BIGNUM *a)
{
    BN_ULONG *ap, *rp, t, c;
    int i, j;

    bn_check_top(r);
    bn_check_top(a);

    if (BN_is_zero(a)) {
        BN_zero(r);
        return 1;
    }
    i = a->top;
    ap = a->d;
    j = i - (ap[i - 1] == 1);
    if (a != r) {
        if (bn_wexpand(r, j) == NULL)
            return 0;
        r->neg = a->neg;
    }
    rp = r->d;
    t = ap[--i];
    c = (t & 1) ? BN_TBIT : 0;
    if (t >>= 1)
        rp[i] = t;
    while (i > 0) {
        t = ap[--i];
        rp[i] = ((t >> 1) & BN_MASK2) | c;
        c = (t & 1) ? BN_TBIT : 0;
    }
    r->top = j;
    if (!r->top)
        r->neg = 0; /* don't allow negative zero */
    bn_check_top(r);
    return 1;
}

int BN_lshift(BIGNUM *r, const BIGNUM *a, int n)
{
    int ret;

    if (n < 0) {
        BNerr(BN_F_BN_LSHIFT, BN_R_INVALID_SHIFT);
        return 0;
    }

    ret = bn_lshift_fixed_top(r, a, n);

    bn_correct_top(r);
    bn_check_top(r);

    return ret;
}

/*
 * In respect to shift factor the execution time is invariant of
 * |n % BN_BITS2|, but not |n / BN_BITS2|. Or in other words pre-condition
 * for constant-time-ness is |n < BN_BITS2| or |n / BN_BITS2| being
 * non-secret.
 */
int bn_lshift_fixed_top(BIGNUM *r, const BIGNUM *a, int n)
{
    int i, nw;
    unsigned int lb, rb;
    BN_ULONG *t, *f;
    BN_ULONG l, m, rmask = 0;

    assert(n >= 0);

    bn_check_top(r);
    bn_check_top(a);

    nw = n / BN_BITS2;
    if (bn_wexpand(r, a->top + nw + 1) == NULL)
        return 0;

    if (a->top != 0) {
        lb = (unsigned int)n % BN_BITS2;
        rb = BN_BITS2 - lb;
        rb %= BN_BITS2;            /* say no to undefined behaviour */
        rmask = (BN_ULONG)0 - rb;  /* rmask = 0 - (rb != 0) */
        rmask |= rmask >> 8;
        f = &(a->d[0]);
        t = &(r->d[nw]);
        l = f[a->top - 1];
        t[a->top] = (l >> rb) & rmask;
        for (i = a->top - 1; i > 0; i--) {
            m = l << lb;
            l = f[i - 1];
            t[i] = (m | ((l >> rb) & rmask)) & BN_MASK2;
        }
        t[0] = (l << lb) & BN_MASK2;
    } else {
        /* shouldn't happen, but formally required */
        r->d[nw] = 0;
    }
    if (nw != 0)
        memset(r->d, 0, sizeof(*t) * nw);

    r->neg = a->neg;
    r->top = a->top + nw + 1;
    r->flags |= BN_FLG_FIXED_TOP;

    return 1;
}

int BN_rshift(BIGNUM *r, const BIGNUM *a, int n)
{
    int i, j, nw, lb, rb;
    BN_ULONG *t, *f;
    BN_ULONG l, tmp;

    bn_check_top(r);
    bn_check_top(a);

    if (n < 0) {
        BNerr(BN_F_BN_RSHIFT, BN_R_INVALID_SHIFT);
        return 0;
    }

    nw = n / BN_BITS2;
    rb = n % BN_BITS2;
    lb = BN_BITS2 - rb;
    if (nw >= a->top || a->top == 0) {
        BN_zero(r);
        return 1;
    }
    i = (BN_num_bits(a) - n + (BN_BITS2 - 1)) / BN_BITS2;
    if (r != a) {
        if (bn_wexpand(r, i) == NULL)
            return 0;
        r->neg = a->neg;
    } else {
        if (n == 0)
            return 1;           /* or the copying loop will go berserk */
    }

    f = &(a->d[nw]);
    t = r->d;
    j = a->top - nw;
    r->top = i;

    if (rb == 0) {
        for (i = j; i != 0; i--)
            *(t++) = *(f++);
    } else {
        l = *(f++);
        for (i = j - 1; i != 0; i--) {
            tmp = (l >> rb) & BN_MASK2;
            l = *(f++);
            *(t++) = (tmp | (l << lb)) & BN_MASK2;
        }
        if ((l = (l >> rb) & BN_MASK2))
            *(t) = l;
    }
    if (!r->top)
        r->neg = 0; /* don't allow negative zero */
    bn_check_top(r);
    return 1;
}

/*
 * In respect to shift factor the execution time is invariant of
 * |n % BN_BITS2|, but not |n / BN_BITS2|. Or in other words pre-condition
 * for constant-time-ness for sufficiently[!] zero-padded inputs is
 * |n < BN_BITS2| or |n / BN_BITS2| being non-secret.
 */
int bn_rshift_fixed_top(BIGNUM *r, const BIGNUM *a, int n)
{
    int i, top, nw;
    unsigned int lb, rb;
    BN_ULONG *t, *f;
    BN_ULONG l, m, mask;

    bn_check_top(r);
    bn_check_top(a);

    assert(n >= 0);

    nw = n / BN_BITS2;
    if (nw >= a->top) {
        /* shouldn't happen, but formally required */
        BN_zero(r);
        return 1;
    }

    rb = (unsigned int)n % BN_BITS2;
    lb = BN_BITS2 - rb;
    lb %= BN_BITS2;            /* say no to undefined behaviour */
    mask = (BN_ULONG)0 - lb;   /* mask = 0 - (lb != 0) */
    mask |= mask >> 8;
    top = a->top - nw;
    if (r != a && bn_wexpand(r, top) == NULL)
        return 0;

    t = &(r->d[0]);
    f = &(a->d[nw]);
    l = f[0];
    for (i = 0; i < top - 1; i++) {
        m = f[i + 1];
        t[i] = (l >> rb) | ((m << lb) & mask);
        l = m;
    }
    t[i] = l >> rb;

    r->neg = a->neg;
    r->top = top;
    r->flags |= BN_FLG_FIXED_TOP;

    return 1;
}
