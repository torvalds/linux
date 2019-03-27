/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * This module is meant to be used as template for base 2^44 assembly
 * implementation[s]. On side note compiler-generated code is not
 * slower than compiler-generated base 2^64 code on [high-end] x86_64,
 * even though amount of multiplications is 50% higher. Go figure...
 */
#include <stdlib.h>

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;
typedef unsigned __int128 u128;

typedef struct {
    u64 h[3];
    u64 s[2];
    u64 r[3];
} poly1305_internal;

#define POLY1305_BLOCK_SIZE 16

/* pick 64-bit unsigned integer in little endian order */
static u64 U8TOU64(const unsigned char *p)
{
    return (((u64)(p[0] & 0xff)) |
            ((u64)(p[1] & 0xff) << 8) |
            ((u64)(p[2] & 0xff) << 16) |
            ((u64)(p[3] & 0xff) << 24) |
            ((u64)(p[4] & 0xff) << 32) |
            ((u64)(p[5] & 0xff) << 40) |
            ((u64)(p[6] & 0xff) << 48) |
            ((u64)(p[7] & 0xff) << 56));
}

/* store a 64-bit unsigned integer in little endian */
static void U64TO8(unsigned char *p, u64 v)
{
    p[0] = (unsigned char)((v) & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
    p[4] = (unsigned char)((v >> 32) & 0xff);
    p[5] = (unsigned char)((v >> 40) & 0xff);
    p[6] = (unsigned char)((v >> 48) & 0xff);
    p[7] = (unsigned char)((v >> 56) & 0xff);
}

int poly1305_init(void *ctx, const unsigned char key[16])
{
    poly1305_internal *st = (poly1305_internal *)ctx;
    u64 r0, r1;

    /* h = 0 */
    st->h[0] = 0;
    st->h[1] = 0;
    st->h[2] = 0;

    r0 = U8TOU64(&key[0]) & 0x0ffffffc0fffffff;
    r1 = U8TOU64(&key[8]) & 0x0ffffffc0ffffffc;

    /* break r1:r0 to three 44-bit digits, masks are 1<<44-1 */
    st->r[0] = r0 & 0x0fffffffffff;
    st->r[1] = ((r0 >> 44) | (r1 << 20))  & 0x0fffffffffff;
    st->r[2] = (r1 >> 24);

    st->s[0] = (st->r[1] + (st->r[1] << 2)) << 2;
    st->s[1] = (st->r[2] + (st->r[2] << 2)) << 2;

    return 0;
}

void poly1305_blocks(void *ctx, const unsigned char *inp, size_t len,
                     u32 padbit)
{
    poly1305_internal *st = (poly1305_internal *)ctx;
    u64 r0, r1, r2;
    u64 s1, s2;
    u64 h0, h1, h2, c;
    u128 d0, d1, d2;
    u64 pad = (u64)padbit << 40;

    r0 = st->r[0];
    r1 = st->r[1];
    r2 = st->r[2];

    s1 = st->s[0];
    s2 = st->s[1];

    h0 = st->h[0];
    h1 = st->h[1];
    h2 = st->h[2];

    while (len >= POLY1305_BLOCK_SIZE) {
        u64 m0, m1;

        m0 = U8TOU64(inp + 0);
        m1 = U8TOU64(inp + 8);

        /* h += m[i], m[i] is broken to 44-bit digits */
        h0 += m0 & 0x0fffffffffff;
        h1 += ((m0 >> 44) | (m1 << 20))  & 0x0fffffffffff;
        h2 +=  (m1 >> 24) + pad;

        /* h *= r "%" p, where "%" stands for "partial remainder" */
        d0 = ((u128)h0 * r0) + ((u128)h1 * s2) + ((u128)h2 * s1);
        d1 = ((u128)h0 * r1) + ((u128)h1 * r0) + ((u128)h2 * s2);
        d2 = ((u128)h0 * r2) + ((u128)h1 * r1) + ((u128)h2 * r0);

        /* "lazy" reduction step */
        h0 = (u64)d0 & 0x0fffffffffff;
        h1 = (u64)(d1 += (u64)(d0 >> 44)) & 0x0fffffffffff;
        h2 = (u64)(d2 += (u64)(d1 >> 44)) & 0x03ffffffffff; /* last 42 bits */

        c = (d2 >> 42);
        h0 += c + (c << 2);

        inp += POLY1305_BLOCK_SIZE;
        len -= POLY1305_BLOCK_SIZE;
    }

    st->h[0] = h0;
    st->h[1] = h1;
    st->h[2] = h2;
}

void poly1305_emit(void *ctx, unsigned char mac[16], const u32 nonce[4])
{
    poly1305_internal *st = (poly1305_internal *) ctx;
    u64 h0, h1, h2;
    u64 g0, g1, g2;
    u128 t;
    u64 mask;

    h0 = st->h[0];
    h1 = st->h[1];
    h2 = st->h[2];

    /* after "lazy" reduction, convert 44+bit digits to 64-bit ones */
    h0 = (u64)(t = (u128)h0 + (h1 << 44));              h1 >>= 20;
    h1 = (u64)(t = (u128)h1 + (h2 << 24) + (t >> 64));  h2 >>= 40;
    h2 += (u64)(t >> 64);

    /* compare to modulus by computing h + -p */
    g0 = (u64)(t = (u128)h0 + 5);
    g1 = (u64)(t = (u128)h1 + (t >> 64));
    g2 = h2 + (u64)(t >> 64);

    /* if there was carry into 131st bit, h1:h0 = g1:g0 */
    mask = 0 - (g2 >> 2);
    g0 &= mask;
    g1 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;

    /* mac = (h + nonce) % (2^128) */
    h0 = (u64)(t = (u128)h0 + nonce[0] + ((u64)nonce[1]<<32));
    h1 = (u64)(t = (u128)h1 + nonce[2] + ((u64)nonce[3]<<32) + (t >> 64));

    U64TO8(mac + 0, h0);
    U64TO8(mac + 8, h1);
}
