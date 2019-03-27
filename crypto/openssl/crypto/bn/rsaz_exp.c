/*
 * Copyright 2013-2016 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2012, Intel Corporation. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * Originally written by Shay Gueron (1, 2), and Vlad Krasnov (1)
 * (1) Intel Corporation, Israel Development Center, Haifa, Israel
 * (2) University of Haifa, Israel
 */

#include <openssl/opensslconf.h>
#include "rsaz_exp.h"

#ifndef RSAZ_ENABLED
NON_EMPTY_TRANSLATION_UNIT
#else

/*
 * See crypto/bn/asm/rsaz-avx2.pl for further details.
 */
void rsaz_1024_norm2red_avx2(void *red, const void *norm);
void rsaz_1024_mul_avx2(void *ret, const void *a, const void *b,
                        const void *n, BN_ULONG k);
void rsaz_1024_sqr_avx2(void *ret, const void *a, const void *n, BN_ULONG k,
                        int cnt);
void rsaz_1024_scatter5_avx2(void *tbl, const void *val, int i);
void rsaz_1024_gather5_avx2(void *val, const void *tbl, int i);
void rsaz_1024_red2norm_avx2(void *norm, const void *red);

#if defined(__GNUC__)
# define ALIGN64        __attribute__((aligned(64)))
#elif defined(_MSC_VER)
# define ALIGN64        __declspec(align(64))
#elif defined(__SUNPRO_C)
# define ALIGN64
# pragma align 64(one,two80)
#else
/* not fatal, might hurt performance a little */
# define ALIGN64
#endif

ALIGN64 static const BN_ULONG one[40] = {
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

ALIGN64 static const BN_ULONG two80[40] = {
    0, 0, 1 << 22, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void RSAZ_1024_mod_exp_avx2(BN_ULONG result_norm[16],
                            const BN_ULONG base_norm[16],
                            const BN_ULONG exponent[16],
                            const BN_ULONG m_norm[16], const BN_ULONG RR[16],
                            BN_ULONG k0)
{
    unsigned char storage[320 * 3 + 32 * 9 * 16 + 64]; /* 5.5KB */
    unsigned char *p_str = storage + (64 - ((size_t)storage % 64));
    unsigned char *a_inv, *m, *result;
    unsigned char *table_s = p_str + 320 * 3;
    unsigned char *R2 = table_s; /* borrow */
    int index;
    int wvalue;

    if ((((size_t)p_str & 4095) + 320) >> 12) {
        result = p_str;
        a_inv = p_str + 320;
        m = p_str + 320 * 2;    /* should not cross page */
    } else {
        m = p_str;              /* should not cross page */
        result = p_str + 320;
        a_inv = p_str + 320 * 2;
    }

    rsaz_1024_norm2red_avx2(m, m_norm);
    rsaz_1024_norm2red_avx2(a_inv, base_norm);
    rsaz_1024_norm2red_avx2(R2, RR);

    rsaz_1024_mul_avx2(R2, R2, R2, m, k0);
    rsaz_1024_mul_avx2(R2, R2, two80, m, k0);

    /* table[0] = 1 */
    rsaz_1024_mul_avx2(result, R2, one, m, k0);
    /* table[1] = a_inv^1 */
    rsaz_1024_mul_avx2(a_inv, a_inv, R2, m, k0);

    rsaz_1024_scatter5_avx2(table_s, result, 0);
    rsaz_1024_scatter5_avx2(table_s, a_inv, 1);

    /* table[2] = a_inv^2 */
    rsaz_1024_sqr_avx2(result, a_inv, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 2);
#if 0
    /* this is almost 2x smaller and less than 1% slower */
    for (index = 3; index < 32; index++) {
        rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
        rsaz_1024_scatter5_avx2(table_s, result, index);
    }
#else
    /* table[4] = a_inv^4 */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 4);
    /* table[8] = a_inv^8 */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 8);
    /* table[16] = a_inv^16 */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 16);
    /* table[17] = a_inv^17 */
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 17);

    /* table[3] */
    rsaz_1024_gather5_avx2(result, table_s, 2);
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 3);
    /* table[6] */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 6);
    /* table[12] */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 12);
    /* table[24] */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 24);
    /* table[25] */
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 25);

    /* table[5] */
    rsaz_1024_gather5_avx2(result, table_s, 4);
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 5);
    /* table[10] */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 10);
    /* table[20] */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 20);
    /* table[21] */
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 21);

    /* table[7] */
    rsaz_1024_gather5_avx2(result, table_s, 6);
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 7);
    /* table[14] */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 14);
    /* table[28] */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 28);
    /* table[29] */
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 29);

    /* table[9] */
    rsaz_1024_gather5_avx2(result, table_s, 8);
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 9);
    /* table[18] */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 18);
    /* table[19] */
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 19);

    /* table[11] */
    rsaz_1024_gather5_avx2(result, table_s, 10);
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 11);
    /* table[22] */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 22);
    /* table[23] */
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 23);

    /* table[13] */
    rsaz_1024_gather5_avx2(result, table_s, 12);
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 13);
    /* table[26] */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 26);
    /* table[27] */
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 27);

    /* table[15] */
    rsaz_1024_gather5_avx2(result, table_s, 14);
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 15);
    /* table[30] */
    rsaz_1024_sqr_avx2(result, result, m, k0, 1);
    rsaz_1024_scatter5_avx2(table_s, result, 30);
    /* table[31] */
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    rsaz_1024_scatter5_avx2(table_s, result, 31);
#endif

    /* load first window */
    p_str = (unsigned char *)exponent;
    wvalue = p_str[127] >> 3;
    rsaz_1024_gather5_avx2(result, table_s, wvalue);

    index = 1014;

    while (index > -1) {        /* loop for the remaining 127 windows */

        rsaz_1024_sqr_avx2(result, result, m, k0, 5);

        wvalue = (p_str[(index / 8) + 1] << 8) | p_str[index / 8];
        wvalue = (wvalue >> (index % 8)) & 31;
        index -= 5;

        rsaz_1024_gather5_avx2(a_inv, table_s, wvalue); /* borrow a_inv */
        rsaz_1024_mul_avx2(result, result, a_inv, m, k0);
    }

    /* square four times */
    rsaz_1024_sqr_avx2(result, result, m, k0, 4);

    wvalue = p_str[0] & 15;

    rsaz_1024_gather5_avx2(a_inv, table_s, wvalue); /* borrow a_inv */
    rsaz_1024_mul_avx2(result, result, a_inv, m, k0);

    /* from Montgomery */
    rsaz_1024_mul_avx2(result, result, one, m, k0);

    rsaz_1024_red2norm_avx2(result_norm, result);

    OPENSSL_cleanse(storage, sizeof(storage));
}

/*
 * See crypto/bn/rsaz-x86_64.pl for further details.
 */
void rsaz_512_mul(void *ret, const void *a, const void *b, const void *n,
                  BN_ULONG k);
void rsaz_512_mul_scatter4(void *ret, const void *a, const void *n,
                           BN_ULONG k, const void *tbl, unsigned int power);
void rsaz_512_mul_gather4(void *ret, const void *a, const void *tbl,
                          const void *n, BN_ULONG k, unsigned int power);
void rsaz_512_mul_by_one(void *ret, const void *a, const void *n, BN_ULONG k);
void rsaz_512_sqr(void *ret, const void *a, const void *n, BN_ULONG k,
                  int cnt);
void rsaz_512_scatter4(void *tbl, const BN_ULONG *val, int power);
void rsaz_512_gather4(BN_ULONG *val, const void *tbl, int power);

void RSAZ_512_mod_exp(BN_ULONG result[8],
                      const BN_ULONG base[8], const BN_ULONG exponent[8],
                      const BN_ULONG m[8], BN_ULONG k0, const BN_ULONG RR[8])
{
    unsigned char storage[16 * 8 * 8 + 64 * 2 + 64]; /* 1.2KB */
    unsigned char *table = storage + (64 - ((size_t)storage % 64));
    BN_ULONG *a_inv = (BN_ULONG *)(table + 16 * 8 * 8);
    BN_ULONG *temp = (BN_ULONG *)(table + 16 * 8 * 8 + 8 * 8);
    unsigned char *p_str = (unsigned char *)exponent;
    int index;
    unsigned int wvalue;

    /* table[0] = 1_inv */
    temp[0] = 0 - m[0];
    temp[1] = ~m[1];
    temp[2] = ~m[2];
    temp[3] = ~m[3];
    temp[4] = ~m[4];
    temp[5] = ~m[5];
    temp[6] = ~m[6];
    temp[7] = ~m[7];
    rsaz_512_scatter4(table, temp, 0);

    /* table [1] = a_inv^1 */
    rsaz_512_mul(a_inv, base, RR, m, k0);
    rsaz_512_scatter4(table, a_inv, 1);

    /* table [2] = a_inv^2 */
    rsaz_512_sqr(temp, a_inv, m, k0, 1);
    rsaz_512_scatter4(table, temp, 2);

    for (index = 3; index < 16; index++)
        rsaz_512_mul_scatter4(temp, a_inv, m, k0, table, index);

    /* load first window */
    wvalue = p_str[63];

    rsaz_512_gather4(temp, table, wvalue >> 4);
    rsaz_512_sqr(temp, temp, m, k0, 4);
    rsaz_512_mul_gather4(temp, temp, table, m, k0, wvalue & 0xf);

    for (index = 62; index >= 0; index--) {
        wvalue = p_str[index];

        rsaz_512_sqr(temp, temp, m, k0, 4);
        rsaz_512_mul_gather4(temp, temp, table, m, k0, wvalue >> 4);

        rsaz_512_sqr(temp, temp, m, k0, 4);
        rsaz_512_mul_gather4(temp, temp, table, m, k0, wvalue & 0x0f);
    }

    /* from Montgomery */
    rsaz_512_mul_by_one(result, temp, m, k0);

    OPENSSL_cleanse(storage, sizeof(storage));
}

#endif
