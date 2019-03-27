/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#include "bn_lcl.h"

/* signed add of b to a. */
int BN_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b)
{
    int ret, r_neg, cmp_res;

    bn_check_top(a);
    bn_check_top(b);

    if (a->neg == b->neg) {
        r_neg = a->neg;
        ret = BN_uadd(r, a, b);
    } else {
        cmp_res = BN_ucmp(a, b);
        if (cmp_res > 0) {
            r_neg = a->neg;
            ret = BN_usub(r, a, b);
        } else if (cmp_res < 0) {
            r_neg = b->neg;
            ret = BN_usub(r, b, a);
        } else {
            r_neg = 0;
            BN_zero(r);
            ret = 1;
        }
    }

    r->neg = r_neg;
    bn_check_top(r);
    return ret;
}

/* signed sub of b from a. */
int BN_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b)
{
    int ret, r_neg, cmp_res;

    bn_check_top(a);
    bn_check_top(b);

    if (a->neg != b->neg) {
        r_neg = a->neg;
        ret = BN_uadd(r, a, b);
    } else {
        cmp_res = BN_ucmp(a, b);
        if (cmp_res > 0) {
            r_neg = a->neg;
            ret = BN_usub(r, a, b);
        } else if (cmp_res < 0) {
            r_neg = !b->neg;
            ret = BN_usub(r, b, a);
        } else {
            r_neg = 0;
            BN_zero(r);
            ret = 1;
        }
    }

    r->neg = r_neg;
    bn_check_top(r);
    return ret;
}

/* unsigned add of b to a, r can be equal to a or b. */
int BN_uadd(BIGNUM *r, const BIGNUM *a, const BIGNUM *b)
{
    int max, min, dif;
    const BN_ULONG *ap, *bp;
    BN_ULONG *rp, carry, t1, t2;

    bn_check_top(a);
    bn_check_top(b);

    if (a->top < b->top) {
        const BIGNUM *tmp;

        tmp = a;
        a = b;
        b = tmp;
    }
    max = a->top;
    min = b->top;
    dif = max - min;

    if (bn_wexpand(r, max + 1) == NULL)
        return 0;

    r->top = max;

    ap = a->d;
    bp = b->d;
    rp = r->d;

    carry = bn_add_words(rp, ap, bp, min);
    rp += min;
    ap += min;

    while (dif) {
        dif--;
        t1 = *(ap++);
        t2 = (t1 + carry) & BN_MASK2;
        *(rp++) = t2;
        carry &= (t2 == 0);
    }
    *rp = carry;
    r->top += carry;

    r->neg = 0;
    bn_check_top(r);
    return 1;
}

/* unsigned subtraction of b from a, a must be larger than b. */
int BN_usub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b)
{
    int max, min, dif;
    BN_ULONG t1, t2, borrow, *rp;
    const BN_ULONG *ap, *bp;

    bn_check_top(a);
    bn_check_top(b);

    max = a->top;
    min = b->top;
    dif = max - min;

    if (dif < 0) {              /* hmm... should not be happening */
        BNerr(BN_F_BN_USUB, BN_R_ARG2_LT_ARG3);
        return 0;
    }

    if (bn_wexpand(r, max) == NULL)
        return 0;

    ap = a->d;
    bp = b->d;
    rp = r->d;

    borrow = bn_sub_words(rp, ap, bp, min);
    ap += min;
    rp += min;

    while (dif) {
        dif--;
        t1 = *(ap++);
        t2 = (t1 - borrow) & BN_MASK2;
        *(rp++) = t2;
        borrow &= (t1 == 0);
    }

    while (max && *--rp == 0)
        max--;

    r->top = max;
    r->neg = 0;
    bn_pollute(r);

    return 1;
}

