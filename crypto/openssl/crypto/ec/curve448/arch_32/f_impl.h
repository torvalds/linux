/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright 2014-2016 Cryptography Research, Inc.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * Originally written by Mike Hamburg
 */

#ifndef HEADER_ARCH_32_F_IMPL_H
# define HEADER_ARCH_32_F_IMPL_H

# define GF_HEADROOM 2
# define LIMB(x) ((x) & ((1 << 28) - 1)), ((x) >> 28)
# define FIELD_LITERAL(a, b, c, d, e, f, g, h) \
    {{LIMB(a), LIMB(b), LIMB(c), LIMB(d), LIMB(e), LIMB(f), LIMB(g), LIMB(h)}}

# define LIMB_PLACE_VALUE(i) 28

void gf_add_RAW(gf out, const gf a, const gf b)
{
    unsigned int i;

    for (i = 0; i < NLIMBS; i++)
        out->limb[i] = a->limb[i] + b->limb[i];
}

void gf_sub_RAW(gf out, const gf a, const gf b)
{
    unsigned int i;

    for (i = 0; i < NLIMBS; i++)
        out->limb[i] = a->limb[i] - b->limb[i];
}

void gf_bias(gf a, int amt)
{
    unsigned int i;
    uint32_t co1 = ((1 << 28) - 1) * amt, co2 = co1 - amt;

    for (i = 0; i < NLIMBS; i++)
        a->limb[i] += (i == NLIMBS / 2) ? co2 : co1;
}

void gf_weak_reduce(gf a)
{
    uint32_t mask = (1 << 28) - 1;
    uint32_t tmp = a->limb[NLIMBS - 1] >> 28;
    unsigned int i;

    a->limb[NLIMBS / 2] += tmp;
    for (i = NLIMBS - 1; i > 0; i--)
        a->limb[i] = (a->limb[i] & mask) + (a->limb[i - 1] >> 28);
    a->limb[0] = (a->limb[0] & mask) + tmp;
}

#endif                  /* HEADER_ARCH_32_F_IMPL_H */
