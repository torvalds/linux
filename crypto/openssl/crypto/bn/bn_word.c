/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#include "bn_lcl.h"

BN_ULONG BN_mod_word(const BIGNUM *a, BN_ULONG w)
{
#ifndef BN_LLONG
    BN_ULONG ret = 0;
#else
    BN_ULLONG ret = 0;
#endif
    int i;

    if (w == 0)
        return (BN_ULONG)-1;

#ifndef BN_LLONG
    /*
     * If |w| is too long and we don't have BN_ULLONG then we need to fall
     * back to using BN_div_word
     */
    if (w > ((BN_ULONG)1 << BN_BITS4)) {
        BIGNUM *tmp = BN_dup(a);
        if (tmp == NULL)
            return (BN_ULONG)-1;

        ret = BN_div_word(tmp, w);
        BN_free(tmp);

        return ret;
    }
#endif

    bn_check_top(a);
    w &= BN_MASK2;
    for (i = a->top - 1; i >= 0; i--) {
#ifndef BN_LLONG
        /*
         * We can assume here that | w <= ((BN_ULONG)1 << BN_BITS4) | and so
         * | ret < ((BN_ULONG)1 << BN_BITS4) | and therefore the shifts here are
         * safe and will not overflow
         */
        ret = ((ret << BN_BITS4) | ((a->d[i] >> BN_BITS4) & BN_MASK2l)) % w;
        ret = ((ret << BN_BITS4) | (a->d[i] & BN_MASK2l)) % w;
#else
        ret = (BN_ULLONG) (((ret << (BN_ULLONG) BN_BITS2) | a->d[i]) %
                           (BN_ULLONG) w);
#endif
    }
    return (BN_ULONG)ret;
}

BN_ULONG BN_div_word(BIGNUM *a, BN_ULONG w)
{
    BN_ULONG ret = 0;
    int i, j;

    bn_check_top(a);
    w &= BN_MASK2;

    if (!w)
        /* actually this an error (division by zero) */
        return (BN_ULONG)-1;
    if (a->top == 0)
        return 0;

    /* normalize input (so bn_div_words doesn't complain) */
    j = BN_BITS2 - BN_num_bits_word(w);
    w <<= j;
    if (!BN_lshift(a, a, j))
        return (BN_ULONG)-1;

    for (i = a->top - 1; i >= 0; i--) {
        BN_ULONG l, d;

        l = a->d[i];
        d = bn_div_words(ret, l, w);
        ret = (l - ((d * w) & BN_MASK2)) & BN_MASK2;
        a->d[i] = d;
    }
    if ((a->top > 0) && (a->d[a->top - 1] == 0))
        a->top--;
    ret >>= j;
    if (!a->top)
        a->neg = 0; /* don't allow negative zero */
    bn_check_top(a);
    return ret;
}

int BN_add_word(BIGNUM *a, BN_ULONG w)
{
    BN_ULONG l;
    int i;

    bn_check_top(a);
    w &= BN_MASK2;

    /* degenerate case: w is zero */
    if (!w)
        return 1;
    /* degenerate case: a is zero */
    if (BN_is_zero(a))
        return BN_set_word(a, w);
    /* handle 'a' when negative */
    if (a->neg) {
        a->neg = 0;
        i = BN_sub_word(a, w);
        if (!BN_is_zero(a))
            a->neg = !(a->neg);
        return i;
    }
    for (i = 0; w != 0 && i < a->top; i++) {
        a->d[i] = l = (a->d[i] + w) & BN_MASK2;
        w = (w > l) ? 1 : 0;
    }
    if (w && i == a->top) {
        if (bn_wexpand(a, a->top + 1) == NULL)
            return 0;
        a->top++;
        a->d[i] = w;
    }
    bn_check_top(a);
    return 1;
}

int BN_sub_word(BIGNUM *a, BN_ULONG w)
{
    int i;

    bn_check_top(a);
    w &= BN_MASK2;

    /* degenerate case: w is zero */
    if (!w)
        return 1;
    /* degenerate case: a is zero */
    if (BN_is_zero(a)) {
        i = BN_set_word(a, w);
        if (i != 0)
            BN_set_negative(a, 1);
        return i;
    }
    /* handle 'a' when negative */
    if (a->neg) {
        a->neg = 0;
        i = BN_add_word(a, w);
        a->neg = 1;
        return i;
    }

    if ((a->top == 1) && (a->d[0] < w)) {
        a->d[0] = w - a->d[0];
        a->neg = 1;
        return 1;
    }
    i = 0;
    for (;;) {
        if (a->d[i] >= w) {
            a->d[i] -= w;
            break;
        } else {
            a->d[i] = (a->d[i] - w) & BN_MASK2;
            i++;
            w = 1;
        }
    }
    if ((a->d[i] == 0) && (i == (a->top - 1)))
        a->top--;
    bn_check_top(a);
    return 1;
}

int BN_mul_word(BIGNUM *a, BN_ULONG w)
{
    BN_ULONG ll;

    bn_check_top(a);
    w &= BN_MASK2;
    if (a->top) {
        if (w == 0)
            BN_zero(a);
        else {
            ll = bn_mul_words(a->d, a->d, a->top, w);
            if (ll) {
                if (bn_wexpand(a, a->top + 1) == NULL)
                    return 0;
                a->d[a->top++] = ll;
            }
        }
    }
    bn_check_top(a);
    return 1;
}
