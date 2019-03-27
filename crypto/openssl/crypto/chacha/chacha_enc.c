/*
 * Copyright 2015-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* Adapted from the public domain code by D. Bernstein from SUPERCOP. */

#include <string.h>

#include "internal/chacha.h"

typedef unsigned int u32;
typedef unsigned char u8;
typedef union {
    u32 u[16];
    u8 c[64];
} chacha_buf;

# define ROTATE(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

# define U32TO8_LITTLE(p, v) do { \
                                (p)[0] = (u8)(v >>  0); \
                                (p)[1] = (u8)(v >>  8); \
                                (p)[2] = (u8)(v >> 16); \
                                (p)[3] = (u8)(v >> 24); \
                                } while(0)

/* QUARTERROUND updates a, b, c, d with a ChaCha "quarter" round. */
# define QUARTERROUND(a,b,c,d) ( \
                x[a] += x[b], x[d] = ROTATE((x[d] ^ x[a]),16), \
                x[c] += x[d], x[b] = ROTATE((x[b] ^ x[c]),12), \
                x[a] += x[b], x[d] = ROTATE((x[d] ^ x[a]), 8), \
                x[c] += x[d], x[b] = ROTATE((x[b] ^ x[c]), 7)  )

/* chacha_core performs 20 rounds of ChaCha on the input words in
 * |input| and writes the 64 output bytes to |output|. */
static void chacha20_core(chacha_buf *output, const u32 input[16])
{
    u32 x[16];
    int i;
    const union {
        long one;
        char little;
    } is_endian = { 1 };

    memcpy(x, input, sizeof(x));

    for (i = 20; i > 0; i -= 2) {
        QUARTERROUND(0, 4, 8, 12);
        QUARTERROUND(1, 5, 9, 13);
        QUARTERROUND(2, 6, 10, 14);
        QUARTERROUND(3, 7, 11, 15);
        QUARTERROUND(0, 5, 10, 15);
        QUARTERROUND(1, 6, 11, 12);
        QUARTERROUND(2, 7, 8, 13);
        QUARTERROUND(3, 4, 9, 14);
    }

    if (is_endian.little) {
        for (i = 0; i < 16; ++i)
            output->u[i] = x[i] + input[i];
    } else {
        for (i = 0; i < 16; ++i)
            U32TO8_LITTLE(output->c + 4 * i, (x[i] + input[i]));
    }
}

void ChaCha20_ctr32(unsigned char *out, const unsigned char *inp,
                    size_t len, const unsigned int key[8],
                    const unsigned int counter[4])
{
    u32 input[16];
    chacha_buf buf;
    size_t todo, i;

    /* sigma constant "expand 32-byte k" in little-endian encoding */
    input[0] = ((u32)'e') | ((u32)'x'<<8) | ((u32)'p'<<16) | ((u32)'a'<<24);
    input[1] = ((u32)'n') | ((u32)'d'<<8) | ((u32)' '<<16) | ((u32)'3'<<24);
    input[2] = ((u32)'2') | ((u32)'-'<<8) | ((u32)'b'<<16) | ((u32)'y'<<24);
    input[3] = ((u32)'t') | ((u32)'e'<<8) | ((u32)' '<<16) | ((u32)'k'<<24);

    input[4] = key[0];
    input[5] = key[1];
    input[6] = key[2];
    input[7] = key[3];
    input[8] = key[4];
    input[9] = key[5];
    input[10] = key[6];
    input[11] = key[7];

    input[12] = counter[0];
    input[13] = counter[1];
    input[14] = counter[2];
    input[15] = counter[3];

    while (len > 0) {
        todo = sizeof(buf);
        if (len < todo)
            todo = len;

        chacha20_core(&buf, input);

        for (i = 0; i < todo; i++)
            out[i] = inp[i] ^ buf.c[i];
        out += todo;
        inp += todo;
        len -= todo;

        /*
         * Advance 32-bit counter. Note that as subroutine is so to
         * say nonce-agnostic, this limited counter width doesn't
         * prevent caller from implementing wider counter. It would
         * simply take two calls split on counter overflow...
         */
        input[12]++;
    }
}
