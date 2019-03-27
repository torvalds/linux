/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <openssl/opensslv.h>
#include "md4_locl.h"

/*
 * Implemented from RFC1186 The MD4 Message-Digest Algorithm
 */

#define INIT_DATA_A (unsigned long)0x67452301L
#define INIT_DATA_B (unsigned long)0xefcdab89L
#define INIT_DATA_C (unsigned long)0x98badcfeL
#define INIT_DATA_D (unsigned long)0x10325476L

int MD4_Init(MD4_CTX *c)
{
    memset(c, 0, sizeof(*c));
    c->A = INIT_DATA_A;
    c->B = INIT_DATA_B;
    c->C = INIT_DATA_C;
    c->D = INIT_DATA_D;
    return 1;
}

#ifndef md4_block_data_order
# ifdef X
#  undef X
# endif
void md4_block_data_order(MD4_CTX *c, const void *data_, size_t num)
{
    const unsigned char *data = data_;
    register unsigned MD32_REG_T A, B, C, D, l;
# ifndef MD32_XARRAY
    /* See comment in crypto/sha/sha_locl.h for details. */
    unsigned MD32_REG_T XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7,
        XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15;
#  define X(i)   XX##i
# else
    MD4_LONG XX[MD4_LBLOCK];
#  define X(i)   XX[i]
# endif

    A = c->A;
    B = c->B;
    C = c->C;
    D = c->D;

    for (; num--;) {
        (void)HOST_c2l(data, l);
        X(0) = l;
        (void)HOST_c2l(data, l);
        X(1) = l;
        /* Round 0 */
        R0(A, B, C, D, X(0), 3, 0);
        (void)HOST_c2l(data, l);
        X(2) = l;
        R0(D, A, B, C, X(1), 7, 0);
        (void)HOST_c2l(data, l);
        X(3) = l;
        R0(C, D, A, B, X(2), 11, 0);
        (void)HOST_c2l(data, l);
        X(4) = l;
        R0(B, C, D, A, X(3), 19, 0);
        (void)HOST_c2l(data, l);
        X(5) = l;
        R0(A, B, C, D, X(4), 3, 0);
        (void)HOST_c2l(data, l);
        X(6) = l;
        R0(D, A, B, C, X(5), 7, 0);
        (void)HOST_c2l(data, l);
        X(7) = l;
        R0(C, D, A, B, X(6), 11, 0);
        (void)HOST_c2l(data, l);
        X(8) = l;
        R0(B, C, D, A, X(7), 19, 0);
        (void)HOST_c2l(data, l);
        X(9) = l;
        R0(A, B, C, D, X(8), 3, 0);
        (void)HOST_c2l(data, l);
        X(10) = l;
        R0(D, A, B, C, X(9), 7, 0);
        (void)HOST_c2l(data, l);
        X(11) = l;
        R0(C, D, A, B, X(10), 11, 0);
        (void)HOST_c2l(data, l);
        X(12) = l;
        R0(B, C, D, A, X(11), 19, 0);
        (void)HOST_c2l(data, l);
        X(13) = l;
        R0(A, B, C, D, X(12), 3, 0);
        (void)HOST_c2l(data, l);
        X(14) = l;
        R0(D, A, B, C, X(13), 7, 0);
        (void)HOST_c2l(data, l);
        X(15) = l;
        R0(C, D, A, B, X(14), 11, 0);
        R0(B, C, D, A, X(15), 19, 0);
        /* Round 1 */
        R1(A, B, C, D, X(0), 3, 0x5A827999L);
        R1(D, A, B, C, X(4), 5, 0x5A827999L);
        R1(C, D, A, B, X(8), 9, 0x5A827999L);
        R1(B, C, D, A, X(12), 13, 0x5A827999L);
        R1(A, B, C, D, X(1), 3, 0x5A827999L);
        R1(D, A, B, C, X(5), 5, 0x5A827999L);
        R1(C, D, A, B, X(9), 9, 0x5A827999L);
        R1(B, C, D, A, X(13), 13, 0x5A827999L);
        R1(A, B, C, D, X(2), 3, 0x5A827999L);
        R1(D, A, B, C, X(6), 5, 0x5A827999L);
        R1(C, D, A, B, X(10), 9, 0x5A827999L);
        R1(B, C, D, A, X(14), 13, 0x5A827999L);
        R1(A, B, C, D, X(3), 3, 0x5A827999L);
        R1(D, A, B, C, X(7), 5, 0x5A827999L);
        R1(C, D, A, B, X(11), 9, 0x5A827999L);
        R1(B, C, D, A, X(15), 13, 0x5A827999L);
        /* Round 2 */
        R2(A, B, C, D, X(0), 3, 0x6ED9EBA1L);
        R2(D, A, B, C, X(8), 9, 0x6ED9EBA1L);
        R2(C, D, A, B, X(4), 11, 0x6ED9EBA1L);
        R2(B, C, D, A, X(12), 15, 0x6ED9EBA1L);
        R2(A, B, C, D, X(2), 3, 0x6ED9EBA1L);
        R2(D, A, B, C, X(10), 9, 0x6ED9EBA1L);
        R2(C, D, A, B, X(6), 11, 0x6ED9EBA1L);
        R2(B, C, D, A, X(14), 15, 0x6ED9EBA1L);
        R2(A, B, C, D, X(1), 3, 0x6ED9EBA1L);
        R2(D, A, B, C, X(9), 9, 0x6ED9EBA1L);
        R2(C, D, A, B, X(5), 11, 0x6ED9EBA1L);
        R2(B, C, D, A, X(13), 15, 0x6ED9EBA1L);
        R2(A, B, C, D, X(3), 3, 0x6ED9EBA1L);
        R2(D, A, B, C, X(11), 9, 0x6ED9EBA1L);
        R2(C, D, A, B, X(7), 11, 0x6ED9EBA1L);
        R2(B, C, D, A, X(15), 15, 0x6ED9EBA1L);

        A = c->A += A;
        B = c->B += B;
        C = c->C += C;
        D = c->D += D;
    }
}
#endif
