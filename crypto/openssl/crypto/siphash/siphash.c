/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* Based on https://131002.net/siphash C reference implementation */
/*
   SipHash reference C implementation

   Copyright (c) 2012-2016 Jean-Philippe Aumasson
   Copyright (c) 2012-2014 Daniel J. Bernstein

   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.

   You should have received a copy of the CC0 Public Domain Dedication along
   with this software. If not, see
   <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include <stdlib.h>
#include <string.h>
#include <openssl/crypto.h>

#include "internal/siphash.h"
#include "siphash_local.h"

/* default: SipHash-2-4 */
#define SIPHASH_C_ROUNDS 2
#define SIPHASH_D_ROUNDS 4

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define U32TO8_LE(p, v)                                                        \
    (p)[0] = (uint8_t)((v));                                                   \
    (p)[1] = (uint8_t)((v) >> 8);                                              \
    (p)[2] = (uint8_t)((v) >> 16);                                             \
    (p)[3] = (uint8_t)((v) >> 24);

#define U64TO8_LE(p, v)                                                        \
    U32TO8_LE((p), (uint32_t)((v)));                                           \
    U32TO8_LE((p) + 4, (uint32_t)((v) >> 32));

#define U8TO64_LE(p)                                                           \
    (((uint64_t)((p)[0])) | ((uint64_t)((p)[1]) << 8) |                        \
     ((uint64_t)((p)[2]) << 16) | ((uint64_t)((p)[3]) << 24) |                 \
     ((uint64_t)((p)[4]) << 32) | ((uint64_t)((p)[5]) << 40) |                 \
     ((uint64_t)((p)[6]) << 48) | ((uint64_t)((p)[7]) << 56))

#define SIPROUND                                                               \
    do {                                                                       \
        v0 += v1;                                                              \
        v1 = ROTL(v1, 13);                                                     \
        v1 ^= v0;                                                              \
        v0 = ROTL(v0, 32);                                                     \
        v2 += v3;                                                              \
        v3 = ROTL(v3, 16);                                                     \
        v3 ^= v2;                                                              \
        v0 += v3;                                                              \
        v3 = ROTL(v3, 21);                                                     \
        v3 ^= v0;                                                              \
        v2 += v1;                                                              \
        v1 = ROTL(v1, 17);                                                     \
        v1 ^= v2;                                                              \
        v2 = ROTL(v2, 32);                                                     \
    } while (0)

size_t SipHash_ctx_size(void)
{
    return sizeof(SIPHASH);
}

size_t SipHash_hash_size(SIPHASH *ctx)
{
    return ctx->hash_size;
}

static size_t siphash_adjust_hash_size(size_t hash_size)
{
    if (hash_size == 0)
        hash_size = SIPHASH_MAX_DIGEST_SIZE;
    return hash_size;
}

int SipHash_set_hash_size(SIPHASH *ctx, size_t hash_size)
{
    hash_size = siphash_adjust_hash_size(hash_size);
    if (hash_size != SIPHASH_MIN_DIGEST_SIZE
        && hash_size != SIPHASH_MAX_DIGEST_SIZE)
        return 0;

    /*
     * It's possible that the key was set first.  If the hash size changes,
     * we need to adjust v1 (see SipHash_Init().
     */

    /* Start by adjusting the stored size, to make things easier */
    ctx->hash_size = siphash_adjust_hash_size(ctx->hash_size);

    /* Now, adjust ctx->v1 if the old and the new size differ */
    if ((size_t)ctx->hash_size != hash_size) {
        ctx->v1 ^= 0xee;
        ctx->hash_size = hash_size;
    }
    return 1;
}

/* hash_size = crounds = drounds = 0 means SipHash24 with 16-byte output */
int SipHash_Init(SIPHASH *ctx, const unsigned char *k, int crounds, int drounds)
{
    uint64_t k0 = U8TO64_LE(k);
    uint64_t k1 = U8TO64_LE(k + 8);

    /* If the hash size wasn't set, i.e. is zero */
    ctx->hash_size = siphash_adjust_hash_size(ctx->hash_size);

    if (drounds == 0)
        drounds = SIPHASH_D_ROUNDS;
    if (crounds == 0)
        crounds = SIPHASH_C_ROUNDS;

    ctx->crounds = crounds;
    ctx->drounds = drounds;

    ctx->len = 0;
    ctx->total_inlen = 0;

    ctx->v0 = 0x736f6d6570736575ULL ^ k0;
    ctx->v1 = 0x646f72616e646f6dULL ^ k1;
    ctx->v2 = 0x6c7967656e657261ULL ^ k0;
    ctx->v3 = 0x7465646279746573ULL ^ k1;

    if (ctx->hash_size == SIPHASH_MAX_DIGEST_SIZE)
        ctx->v1 ^= 0xee;

    return 1;
}

void SipHash_Update(SIPHASH *ctx, const unsigned char *in, size_t inlen)
{
    uint64_t m;
    const uint8_t *end;
    int left;
    int i;
    uint64_t v0 = ctx->v0;
    uint64_t v1 = ctx->v1;
    uint64_t v2 = ctx->v2;
    uint64_t v3 = ctx->v3;

    ctx->total_inlen += inlen;

    if (ctx->len) {
        /* deal with leavings */
        size_t available = SIPHASH_BLOCK_SIZE - ctx->len;

        /* not enough to fill leavings */
        if (inlen < available) {
            memcpy(&ctx->leavings[ctx->len], in, inlen);
            ctx->len += inlen;
            return;
        }

        /* copy data into leavings and reduce input */
        memcpy(&ctx->leavings[ctx->len], in, available);
        inlen -= available;
        in += available;

        /* process leavings */
        m = U8TO64_LE(ctx->leavings);
        v3 ^= m;
        for (i = 0; i < ctx->crounds; ++i)
            SIPROUND;
        v0 ^= m;
    }
    left = inlen & (SIPHASH_BLOCK_SIZE-1); /* gets put into leavings */
    end = in + inlen - left;

    for (; in != end; in += 8) {
        m = U8TO64_LE(in);
        v3 ^= m;
        for (i = 0; i < ctx->crounds; ++i)
            SIPROUND;
        v0 ^= m;
    }

    /* save leavings and other ctx */
    if (left)
        memcpy(ctx->leavings, end, left);
    ctx->len = left;

    ctx->v0 = v0;
    ctx->v1 = v1;
    ctx->v2 = v2;
    ctx->v3 = v3;
}

int SipHash_Final(SIPHASH *ctx, unsigned char *out, size_t outlen)
{
    /* finalize hash */
    int i;
    uint64_t b = ctx->total_inlen << 56;
    uint64_t v0 = ctx->v0;
    uint64_t v1 = ctx->v1;
    uint64_t v2 = ctx->v2;
    uint64_t v3 = ctx->v3;

    if (outlen != (size_t)ctx->hash_size)
        return 0;

    switch (ctx->len) {
    case 7:
        b |= ((uint64_t)ctx->leavings[6]) << 48;
        /* fall thru */
    case 6:
        b |= ((uint64_t)ctx->leavings[5]) << 40;
        /* fall thru */
    case 5:
        b |= ((uint64_t)ctx->leavings[4]) << 32;
        /* fall thru */
    case 4:
        b |= ((uint64_t)ctx->leavings[3]) << 24;
        /* fall thru */
    case 3:
        b |= ((uint64_t)ctx->leavings[2]) << 16;
        /* fall thru */
    case 2:
        b |= ((uint64_t)ctx->leavings[1]) <<  8;
        /* fall thru */
    case 1:
        b |= ((uint64_t)ctx->leavings[0]);
    case 0:
        break;
    }

    v3 ^= b;
    for (i = 0; i < ctx->crounds; ++i)
        SIPROUND;
    v0 ^= b;
    if (ctx->hash_size == SIPHASH_MAX_DIGEST_SIZE)
        v2 ^= 0xee;
    else
        v2 ^= 0xff;
    for (i = 0; i < ctx->drounds; ++i)
        SIPROUND;
    b = v0 ^ v1 ^ v2  ^ v3;
    U64TO8_LE(out, b);
    if (ctx->hash_size == SIPHASH_MIN_DIGEST_SIZE)
        return 1;
    v1 ^= 0xdd;
    for (i = 0; i < ctx->drounds; ++i)
        SIPROUND;
    b = v0 ^ v1 ^ v2  ^ v3;
    U64TO8_LE(out + 8, b);
    return 1;
}
