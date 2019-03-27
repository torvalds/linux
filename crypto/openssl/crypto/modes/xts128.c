/*
 * Copyright 2011-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/crypto.h>
#include "modes_lcl.h"
#include <string.h>

int CRYPTO_xts128_encrypt(const XTS128_CONTEXT *ctx,
                          const unsigned char iv[16],
                          const unsigned char *inp, unsigned char *out,
                          size_t len, int enc)
{
    const union {
        long one;
        char little;
    } is_endian = {
        1
    };
    union {
        u64 u[2];
        u32 d[4];
        u8 c[16];
    } tweak, scratch;
    unsigned int i;

    if (len < 16)
        return -1;

    memcpy(tweak.c, iv, 16);

    (*ctx->block2) (tweak.c, tweak.c, ctx->key2);

    if (!enc && (len % 16))
        len -= 16;

    while (len >= 16) {
#if defined(STRICT_ALIGNMENT)
        memcpy(scratch.c, inp, 16);
        scratch.u[0] ^= tweak.u[0];
        scratch.u[1] ^= tweak.u[1];
#else
        scratch.u[0] = ((u64 *)inp)[0] ^ tweak.u[0];
        scratch.u[1] = ((u64 *)inp)[1] ^ tweak.u[1];
#endif
        (*ctx->block1) (scratch.c, scratch.c, ctx->key1);
#if defined(STRICT_ALIGNMENT)
        scratch.u[0] ^= tweak.u[0];
        scratch.u[1] ^= tweak.u[1];
        memcpy(out, scratch.c, 16);
#else
        ((u64 *)out)[0] = scratch.u[0] ^= tweak.u[0];
        ((u64 *)out)[1] = scratch.u[1] ^= tweak.u[1];
#endif
        inp += 16;
        out += 16;
        len -= 16;

        if (len == 0)
            return 0;

        if (is_endian.little) {
            unsigned int carry, res;

            res = 0x87 & (((int)tweak.d[3]) >> 31);
            carry = (unsigned int)(tweak.u[0] >> 63);
            tweak.u[0] = (tweak.u[0] << 1) ^ res;
            tweak.u[1] = (tweak.u[1] << 1) | carry;
        } else {
            size_t c;

            for (c = 0, i = 0; i < 16; ++i) {
                /*
                 * + substitutes for |, because c is 1 bit
                 */
                c += ((size_t)tweak.c[i]) << 1;
                tweak.c[i] = (u8)c;
                c = c >> 8;
            }
            tweak.c[0] ^= (u8)(0x87 & (0 - c));
        }
    }
    if (enc) {
        for (i = 0; i < len; ++i) {
            u8 c = inp[i];
            out[i] = scratch.c[i];
            scratch.c[i] = c;
        }
        scratch.u[0] ^= tweak.u[0];
        scratch.u[1] ^= tweak.u[1];
        (*ctx->block1) (scratch.c, scratch.c, ctx->key1);
        scratch.u[0] ^= tweak.u[0];
        scratch.u[1] ^= tweak.u[1];
        memcpy(out - 16, scratch.c, 16);
    } else {
        union {
            u64 u[2];
            u8 c[16];
        } tweak1;

        if (is_endian.little) {
            unsigned int carry, res;

            res = 0x87 & (((int)tweak.d[3]) >> 31);
            carry = (unsigned int)(tweak.u[0] >> 63);
            tweak1.u[0] = (tweak.u[0] << 1) ^ res;
            tweak1.u[1] = (tweak.u[1] << 1) | carry;
        } else {
            size_t c;

            for (c = 0, i = 0; i < 16; ++i) {
                /*
                 * + substitutes for |, because c is 1 bit
                 */
                c += ((size_t)tweak.c[i]) << 1;
                tweak1.c[i] = (u8)c;
                c = c >> 8;
            }
            tweak1.c[0] ^= (u8)(0x87 & (0 - c));
        }
#if defined(STRICT_ALIGNMENT)
        memcpy(scratch.c, inp, 16);
        scratch.u[0] ^= tweak1.u[0];
        scratch.u[1] ^= tweak1.u[1];
#else
        scratch.u[0] = ((u64 *)inp)[0] ^ tweak1.u[0];
        scratch.u[1] = ((u64 *)inp)[1] ^ tweak1.u[1];
#endif
        (*ctx->block1) (scratch.c, scratch.c, ctx->key1);
        scratch.u[0] ^= tweak1.u[0];
        scratch.u[1] ^= tweak1.u[1];

        for (i = 0; i < len; ++i) {
            u8 c = inp[16 + i];
            out[16 + i] = scratch.c[i];
            scratch.c[i] = c;
        }
        scratch.u[0] ^= tweak.u[0];
        scratch.u[1] ^= tweak.u[1];
        (*ctx->block1) (scratch.c, scratch.c, ctx->key1);
#if defined(STRICT_ALIGNMENT)
        scratch.u[0] ^= tweak.u[0];
        scratch.u[1] ^= tweak.u[1];
        memcpy(out, scratch.c, 16);
#else
        ((u64 *)out)[0] = scratch.u[0] ^ tweak.u[0];
        ((u64 *)out)[1] = scratch.u[1] ^ tweak.u[1];
#endif
    }

    return 0;
}
