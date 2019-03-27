/*
 * Copyright 2014-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#include "bn_lcl.h"

/*
 * Determine the modified width-(w+1) Non-Adjacent Form (wNAF) of 'scalar'.
 * This is an array  r[]  of values that are either zero or odd with an
 * absolute value less than  2^w  satisfying
 *     scalar = \sum_j r[j]*2^j
 * where at most one of any  w+1  consecutive digits is non-zero
 * with the exception that the most significant digit may be only
 * w-1 zeros away from that next non-zero digit.
 */
signed char *bn_compute_wNAF(const BIGNUM *scalar, int w, size_t *ret_len)
{
    int window_val;
    signed char *r = NULL;
    int sign = 1;
    int bit, next_bit, mask;
    size_t len = 0, j;

    if (BN_is_zero(scalar)) {
        r = OPENSSL_malloc(1);
        if (r == NULL) {
            BNerr(BN_F_BN_COMPUTE_WNAF, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        r[0] = 0;
        *ret_len = 1;
        return r;
    }

    if (w <= 0 || w > 7) {      /* 'signed char' can represent integers with
                                 * absolute values less than 2^7 */
        BNerr(BN_F_BN_COMPUTE_WNAF, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    bit = 1 << w;               /* at most 128 */
    next_bit = bit << 1;        /* at most 256 */
    mask = next_bit - 1;        /* at most 255 */

    if (BN_is_negative(scalar)) {
        sign = -1;
    }

    if (scalar->d == NULL || scalar->top == 0) {
        BNerr(BN_F_BN_COMPUTE_WNAF, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    len = BN_num_bits(scalar);
    r = OPENSSL_malloc(len + 1); /*
                                  * Modified wNAF may be one digit longer than binary representation
                                  * (*ret_len will be set to the actual length, i.e. at most
                                  * BN_num_bits(scalar) + 1)
                                  */
    if (r == NULL) {
        BNerr(BN_F_BN_COMPUTE_WNAF, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    window_val = scalar->d[0] & mask;
    j = 0;
    while ((window_val != 0) || (j + w + 1 < len)) { /* if j+w+1 >= len,
                                                      * window_val will not
                                                      * increase */
        int digit = 0;

        /* 0 <= window_val <= 2^(w+1) */

        if (window_val & 1) {
            /* 0 < window_val < 2^(w+1) */

            if (window_val & bit) {
                digit = window_val - next_bit; /* -2^w < digit < 0 */

#if 1                           /* modified wNAF */
                if (j + w + 1 >= len) {
                    /*
                     * Special case for generating modified wNAFs:
                     * no new bits will be added into window_val,
                     * so using a positive digit here will decrease
                     * the total length of the representation
                     */

                    digit = window_val & (mask >> 1); /* 0 < digit < 2^w */
                }
#endif
            } else {
                digit = window_val; /* 0 < digit < 2^w */
            }

            if (digit <= -bit || digit >= bit || !(digit & 1)) {
                BNerr(BN_F_BN_COMPUTE_WNAF, ERR_R_INTERNAL_ERROR);
                goto err;
            }

            window_val -= digit;

            /*
             * now window_val is 0 or 2^(w+1) in standard wNAF generation;
             * for modified window NAFs, it may also be 2^w
             */
            if (window_val != 0 && window_val != next_bit
                && window_val != bit) {
                BNerr(BN_F_BN_COMPUTE_WNAF, ERR_R_INTERNAL_ERROR);
                goto err;
            }
        }

        r[j++] = sign * digit;

        window_val >>= 1;
        window_val += bit * BN_is_bit_set(scalar, j + w);

        if (window_val > next_bit) {
            BNerr(BN_F_BN_COMPUTE_WNAF, ERR_R_INTERNAL_ERROR);
            goto err;
        }
    }

    if (j > len + 1) {
        BNerr(BN_F_BN_COMPUTE_WNAF, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    *ret_len = j;
    return r;

 err:
    OPENSSL_free(r);
    return NULL;
}

int bn_get_top(const BIGNUM *a)
{
    return a->top;
}

int bn_get_dmax(const BIGNUM *a)
{
    return a->dmax;
}

void bn_set_all_zero(BIGNUM *a)
{
    int i;

    for (i = a->top; i < a->dmax; i++)
        a->d[i] = 0;
}

int bn_copy_words(BN_ULONG *out, const BIGNUM *in, int size)
{
    if (in->top > size)
        return 0;

    memset(out, 0, sizeof(*out) * size);
    if (in->d != NULL)
        memcpy(out, in->d, sizeof(*out) * in->top);
    return 1;
}

BN_ULONG *bn_get_words(const BIGNUM *a)
{
    return a->d;
}

void bn_set_static_words(BIGNUM *a, const BN_ULONG *words, int size)
{
    /*
     * |const| qualifier omission is compensated by BN_FLG_STATIC_DATA
     * flag, which effectively means "read-only data".
     */
    a->d = (BN_ULONG *)words;
    a->dmax = a->top = size;
    a->neg = 0;
    a->flags |= BN_FLG_STATIC_DATA;
    bn_correct_top(a);
}

int bn_set_words(BIGNUM *a, const BN_ULONG *words, int num_words)
{
    if (bn_wexpand(a, num_words) == NULL) {
        BNerr(BN_F_BN_SET_WORDS, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    memcpy(a->d, words, sizeof(BN_ULONG) * num_words);
    a->top = num_words;
    bn_correct_top(a);
    return 1;
}
