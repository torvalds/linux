/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "md5_locl.h"
#include <openssl/opensslv.h>

/*
 * Implemented from RFC1321 The MD5 Message-Digest Algorithm
 */

#define INIT_DATA_A (unsigned long)0x67452301L
#define INIT_DATA_B (unsigned long)0xefcdab89L
#define INIT_DATA_C (unsigned long)0x98badcfeL
#define INIT_DATA_D (unsigned long)0x10325476L

int MD5_Init(MD5_CTX *c)
{
    memset(c, 0, sizeof(*c));
    c->A = INIT_DATA_A;
    c->B = INIT_DATA_B;
    c->C = INIT_DATA_C;
    c->D = INIT_DATA_D;
    return 1;
}

#ifndef md5_block_data_order
# ifdef X
#  undef X
# endif
void md5_block_data_order(MD5_CTX *c, const void *data_, size_t num)
{
    const unsigned char *data = data_;
    register unsigned MD32_REG_T A, B, C, D, l;
# ifndef MD32_XARRAY
    /* See comment in crypto/sha/sha_locl.h for details. */
    unsigned MD32_REG_T XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7,
        XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15;
#  define X(i)   XX##i
# else
    MD5_LONG XX[MD5_LBLOCK];
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
        R0(A, B, C, D, X(0), 7, 0xd76aa478L);
        (void)HOST_c2l(data, l);
        X(2) = l;
        R0(D, A, B, C, X(1), 12, 0xe8c7b756L);
        (void)HOST_c2l(data, l);
        X(3) = l;
        R0(C, D, A, B, X(2), 17, 0x242070dbL);
        (void)HOST_c2l(data, l);
        X(4) = l;
        R0(B, C, D, A, X(3), 22, 0xc1bdceeeL);
        (void)HOST_c2l(data, l);
        X(5) = l;
        R0(A, B, C, D, X(4), 7, 0xf57c0fafL);
        (void)HOST_c2l(data, l);
        X(6) = l;
        R0(D, A, B, C, X(5), 12, 0x4787c62aL);
        (void)HOST_c2l(data, l);
        X(7) = l;
        R0(C, D, A, B, X(6), 17, 0xa8304613L);
        (void)HOST_c2l(data, l);
        X(8) = l;
        R0(B, C, D, A, X(7), 22, 0xfd469501L);
        (void)HOST_c2l(data, l);
        X(9) = l;
        R0(A, B, C, D, X(8), 7, 0x698098d8L);
        (void)HOST_c2l(data, l);
        X(10) = l;
        R0(D, A, B, C, X(9), 12, 0x8b44f7afL);
        (void)HOST_c2l(data, l);
        X(11) = l;
        R0(C, D, A, B, X(10), 17, 0xffff5bb1L);
        (void)HOST_c2l(data, l);
        X(12) = l;
        R0(B, C, D, A, X(11), 22, 0x895cd7beL);
        (void)HOST_c2l(data, l);
        X(13) = l;
        R0(A, B, C, D, X(12), 7, 0x6b901122L);
        (void)HOST_c2l(data, l);
        X(14) = l;
        R0(D, A, B, C, X(13), 12, 0xfd987193L);
        (void)HOST_c2l(data, l);
        X(15) = l;
        R0(C, D, A, B, X(14), 17, 0xa679438eL);
        R0(B, C, D, A, X(15), 22, 0x49b40821L);
        /* Round 1 */
        R1(A, B, C, D, X(1), 5, 0xf61e2562L);
        R1(D, A, B, C, X(6), 9, 0xc040b340L);
        R1(C, D, A, B, X(11), 14, 0x265e5a51L);
        R1(B, C, D, A, X(0), 20, 0xe9b6c7aaL);
        R1(A, B, C, D, X(5), 5, 0xd62f105dL);
        R1(D, A, B, C, X(10), 9, 0x02441453L);
        R1(C, D, A, B, X(15), 14, 0xd8a1e681L);
        R1(B, C, D, A, X(4), 20, 0xe7d3fbc8L);
        R1(A, B, C, D, X(9), 5, 0x21e1cde6L);
        R1(D, A, B, C, X(14), 9, 0xc33707d6L);
        R1(C, D, A, B, X(3), 14, 0xf4d50d87L);
        R1(B, C, D, A, X(8), 20, 0x455a14edL);
        R1(A, B, C, D, X(13), 5, 0xa9e3e905L);
        R1(D, A, B, C, X(2), 9, 0xfcefa3f8L);
        R1(C, D, A, B, X(7), 14, 0x676f02d9L);
        R1(B, C, D, A, X(12), 20, 0x8d2a4c8aL);
        /* Round 2 */
        R2(A, B, C, D, X(5), 4, 0xfffa3942L);
        R2(D, A, B, C, X(8), 11, 0x8771f681L);
        R2(C, D, A, B, X(11), 16, 0x6d9d6122L);
        R2(B, C, D, A, X(14), 23, 0xfde5380cL);
        R2(A, B, C, D, X(1), 4, 0xa4beea44L);
        R2(D, A, B, C, X(4), 11, 0x4bdecfa9L);
        R2(C, D, A, B, X(7), 16, 0xf6bb4b60L);
        R2(B, C, D, A, X(10), 23, 0xbebfbc70L);
        R2(A, B, C, D, X(13), 4, 0x289b7ec6L);
        R2(D, A, B, C, X(0), 11, 0xeaa127faL);
        R2(C, D, A, B, X(3), 16, 0xd4ef3085L);
        R2(B, C, D, A, X(6), 23, 0x04881d05L);
        R2(A, B, C, D, X(9), 4, 0xd9d4d039L);
        R2(D, A, B, C, X(12), 11, 0xe6db99e5L);
        R2(C, D, A, B, X(15), 16, 0x1fa27cf8L);
        R2(B, C, D, A, X(2), 23, 0xc4ac5665L);
        /* Round 3 */
        R3(A, B, C, D, X(0), 6, 0xf4292244L);
        R3(D, A, B, C, X(7), 10, 0x432aff97L);
        R3(C, D, A, B, X(14), 15, 0xab9423a7L);
        R3(B, C, D, A, X(5), 21, 0xfc93a039L);
        R3(A, B, C, D, X(12), 6, 0x655b59c3L);
        R3(D, A, B, C, X(3), 10, 0x8f0ccc92L);
        R3(C, D, A, B, X(10), 15, 0xffeff47dL);
        R3(B, C, D, A, X(1), 21, 0x85845dd1L);
        R3(A, B, C, D, X(8), 6, 0x6fa87e4fL);
        R3(D, A, B, C, X(15), 10, 0xfe2ce6e0L);
        R3(C, D, A, B, X(6), 15, 0xa3014314L);
        R3(B, C, D, A, X(13), 21, 0x4e0811a1L);
        R3(A, B, C, D, X(4), 6, 0xf7537e82L);
        R3(D, A, B, C, X(11), 10, 0xbd3af235L);
        R3(C, D, A, B, X(2), 15, 0x2ad7d2bbL);
        R3(B, C, D, A, X(9), 21, 0xeb86d391L);

        A = c->A += A;
        B = c->B += B;
        C = c->C += C;
        D = c->D += D;
    }
}
#endif
