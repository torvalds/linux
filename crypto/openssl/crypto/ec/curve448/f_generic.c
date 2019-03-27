/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright 2015-2016 Cryptography Research, Inc.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * Originally written by Mike Hamburg
 */
#include "field.h"

static const gf MODULUS = {
    FIELD_LITERAL(0xffffffffffffff, 0xffffffffffffff, 0xffffffffffffff,
                  0xffffffffffffff, 0xfffffffffffffe, 0xffffffffffffff,
                  0xffffffffffffff, 0xffffffffffffff)
};

/* Serialize to wire format. */
void gf_serialize(uint8_t serial[SER_BYTES], const gf x, int with_hibit)
{
    unsigned int j = 0, fill = 0;
    dword_t buffer = 0;
    int i;
    gf red;

    gf_copy(red, x);
    gf_strong_reduce(red);
    if (!with_hibit)
        assert(gf_hibit(red) == 0);

    for (i = 0; i < (with_hibit ? X_SER_BYTES : SER_BYTES); i++) {
        if (fill < 8 && j < NLIMBS) {
            buffer |= ((dword_t) red->limb[LIMBPERM(j)]) << fill;
            fill += LIMB_PLACE_VALUE(LIMBPERM(j));
            j++;
        }
        serial[i] = (uint8_t)buffer;
        fill -= 8;
        buffer >>= 8;
    }
}

/* Return high bit of x = low bit of 2x mod p */
mask_t gf_hibit(const gf x)
{
    gf y;

    gf_add(y, x, x);
    gf_strong_reduce(y);
    return 0 - (y->limb[0] & 1);
}

/* Return high bit of x = low bit of 2x mod p */
mask_t gf_lobit(const gf x)
{
    gf y;

    gf_copy(y, x);
    gf_strong_reduce(y);
    return 0 - (y->limb[0] & 1);
}

/* Deserialize from wire format; return -1 on success and 0 on failure. */
mask_t gf_deserialize(gf x, const uint8_t serial[SER_BYTES], int with_hibit,
                      uint8_t hi_nmask)
{
    unsigned int j = 0, fill = 0;
    dword_t buffer = 0;
    dsword_t scarry = 0;
    const unsigned nbytes = with_hibit ? X_SER_BYTES : SER_BYTES;
    unsigned int i;
    mask_t succ;

    for (i = 0; i < NLIMBS; i++) {
        while (fill < LIMB_PLACE_VALUE(LIMBPERM(i)) && j < nbytes) {
            uint8_t sj;

            sj = serial[j];
            if (j == nbytes - 1)
                sj &= ~hi_nmask;
            buffer |= ((dword_t) sj) << fill;
            fill += 8;
            j++;
        }
        x->limb[LIMBPERM(i)] = (word_t)
            ((i < NLIMBS - 1) ? buffer & LIMB_MASK(LIMBPERM(i)) : buffer);
        fill -= LIMB_PLACE_VALUE(LIMBPERM(i));
        buffer >>= LIMB_PLACE_VALUE(LIMBPERM(i));
        scarry =
            (scarry + x->limb[LIMBPERM(i)] -
             MODULUS->limb[LIMBPERM(i)]) >> (8 * sizeof(word_t));
    }
    succ = with_hibit ? 0 - (mask_t) 1 : ~gf_hibit(x);
    return succ & word_is_zero((word_t)buffer) & ~word_is_zero((word_t)scarry);
}

/* Reduce to canonical form. */
void gf_strong_reduce(gf a)
{
    dsword_t scarry;
    word_t scarry_0;
    dword_t carry = 0;
    unsigned int i;

    /* first, clear high */
    gf_weak_reduce(a);          /* Determined to have negligible perf impact. */

    /* now the total is less than 2p */

    /* compute total_value - p.  No need to reduce mod p. */
    scarry = 0;
    for (i = 0; i < NLIMBS; i++) {
        scarry = scarry + a->limb[LIMBPERM(i)] - MODULUS->limb[LIMBPERM(i)];
        a->limb[LIMBPERM(i)] = scarry & LIMB_MASK(LIMBPERM(i));
        scarry >>= LIMB_PLACE_VALUE(LIMBPERM(i));
    }

    /*
     * uncommon case: it was >= p, so now scarry = 0 and this = x common case:
     * it was < p, so now scarry = -1 and this = x - p + 2^255 so let's add
     * back in p.  will carry back off the top for 2^255.
     */
    assert(scarry == 0 || scarry == -1);

    scarry_0 = (word_t)scarry;

    /* add it back */
    for (i = 0; i < NLIMBS; i++) {
        carry =
            carry + a->limb[LIMBPERM(i)] +
            (scarry_0 & MODULUS->limb[LIMBPERM(i)]);
        a->limb[LIMBPERM(i)] = carry & LIMB_MASK(LIMBPERM(i));
        carry >>= LIMB_PLACE_VALUE(LIMBPERM(i));
    }

    assert(carry < 2 && ((word_t)carry + scarry_0) == 0);
}

/* Subtract two gf elements d=a-b */
void gf_sub(gf d, const gf a, const gf b)
{
    gf_sub_RAW(d, a, b);
    gf_bias(d, 2);
    gf_weak_reduce(d);
}

/* Add two field elements d = a+b */
void gf_add(gf d, const gf a, const gf b)
{
    gf_add_RAW(d, a, b);
    gf_weak_reduce(d);
}

/* Compare a==b */
mask_t gf_eq(const gf a, const gf b)
{
    gf c;
    mask_t ret = 0;
    unsigned int i;

    gf_sub(c, a, b);
    gf_strong_reduce(c);

    for (i = 0; i < NLIMBS; i++)
        ret |= c->limb[LIMBPERM(i)];

    return word_is_zero(ret);
}

mask_t gf_isr(gf a, const gf x)
{
    gf L0, L1, L2;

    gf_sqr(L1, x);
    gf_mul(L2, x, L1);
    gf_sqr(L1, L2);
    gf_mul(L2, x, L1);
    gf_sqrn(L1, L2, 3);
    gf_mul(L0, L2, L1);
    gf_sqrn(L1, L0, 3);
    gf_mul(L0, L2, L1);
    gf_sqrn(L2, L0, 9);
    gf_mul(L1, L0, L2);
    gf_sqr(L0, L1);
    gf_mul(L2, x, L0);
    gf_sqrn(L0, L2, 18);
    gf_mul(L2, L1, L0);
    gf_sqrn(L0, L2, 37);
    gf_mul(L1, L2, L0);
    gf_sqrn(L0, L1, 37);
    gf_mul(L1, L2, L0);
    gf_sqrn(L0, L1, 111);
    gf_mul(L2, L1, L0);
    gf_sqr(L0, L2);
    gf_mul(L1, x, L0);
    gf_sqrn(L0, L1, 223);
    gf_mul(L1, L2, L0);
    gf_sqr(L2, L1);
    gf_mul(L0, L2, x);
    gf_copy(a, L1);
    return gf_eq(L0, ONE);
}
