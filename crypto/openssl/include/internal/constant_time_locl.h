/*
 * Copyright 2014-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_CONSTANT_TIME_LOCL_H
# define HEADER_CONSTANT_TIME_LOCL_H

# include <stdlib.h>
# include <string.h>
# include <openssl/e_os2.h>              /* For 'ossl_inline' */

/*-
 * The boolean methods return a bitmask of all ones (0xff...f) for true
 * and 0 for false. This is useful for choosing a value based on the result
 * of a conditional in constant time. For example,
 *      if (a < b) {
 *        c = a;
 *      } else {
 *        c = b;
 *      }
 * can be written as
 *      unsigned int lt = constant_time_lt(a, b);
 *      c = constant_time_select(lt, a, b);
 */

/* Returns the given value with the MSB copied to all the other bits. */
static ossl_inline unsigned int constant_time_msb(unsigned int a);
/* Convenience method for uint32_t. */
static ossl_inline uint32_t constant_time_msb_32(uint32_t a);
/* Convenience method for uint64_t. */
static ossl_inline uint64_t constant_time_msb_64(uint64_t a);

/* Returns 0xff..f if a < b and 0 otherwise. */
static ossl_inline unsigned int constant_time_lt(unsigned int a,
                                                 unsigned int b);
/* Convenience method for getting an 8-bit mask. */
static ossl_inline unsigned char constant_time_lt_8(unsigned int a,
                                                    unsigned int b);
/* Convenience method for uint64_t. */
static ossl_inline uint64_t constant_time_lt_64(uint64_t a, uint64_t b);

/* Returns 0xff..f if a >= b and 0 otherwise. */
static ossl_inline unsigned int constant_time_ge(unsigned int a,
                                                 unsigned int b);
/* Convenience method for getting an 8-bit mask. */
static ossl_inline unsigned char constant_time_ge_8(unsigned int a,
                                                    unsigned int b);

/* Returns 0xff..f if a == 0 and 0 otherwise. */
static ossl_inline unsigned int constant_time_is_zero(unsigned int a);
/* Convenience method for getting an 8-bit mask. */
static ossl_inline unsigned char constant_time_is_zero_8(unsigned int a);
/* Convenience method for getting a 32-bit mask. */
static ossl_inline uint32_t constant_time_is_zero_32(uint32_t a);

/* Returns 0xff..f if a == b and 0 otherwise. */
static ossl_inline unsigned int constant_time_eq(unsigned int a,
                                                 unsigned int b);
/* Convenience method for getting an 8-bit mask. */
static ossl_inline unsigned char constant_time_eq_8(unsigned int a,
                                                    unsigned int b);
/* Signed integers. */
static ossl_inline unsigned int constant_time_eq_int(int a, int b);
/* Convenience method for getting an 8-bit mask. */
static ossl_inline unsigned char constant_time_eq_int_8(int a, int b);

/*-
 * Returns (mask & a) | (~mask & b).
 *
 * When |mask| is all 1s or all 0s (as returned by the methods above),
 * the select methods return either |a| (if |mask| is nonzero) or |b|
 * (if |mask| is zero).
 */
static ossl_inline unsigned int constant_time_select(unsigned int mask,
                                                     unsigned int a,
                                                     unsigned int b);
/* Convenience method for unsigned chars. */
static ossl_inline unsigned char constant_time_select_8(unsigned char mask,
                                                        unsigned char a,
                                                        unsigned char b);

/* Convenience method for uint32_t. */
static ossl_inline uint32_t constant_time_select_32(uint32_t mask, uint32_t a,
                                                    uint32_t b);

/* Convenience method for uint64_t. */
static ossl_inline uint64_t constant_time_select_64(uint64_t mask, uint64_t a,
                                                    uint64_t b);
/* Convenience method for signed integers. */
static ossl_inline int constant_time_select_int(unsigned int mask, int a,
                                                int b);


static ossl_inline unsigned int constant_time_msb(unsigned int a)
{
    return 0 - (a >> (sizeof(a) * 8 - 1));
}


static ossl_inline uint32_t constant_time_msb_32(uint32_t a)
{
    return 0 - (a >> 31);
}

static ossl_inline uint64_t constant_time_msb_64(uint64_t a)
{
    return 0 - (a >> 63);
}

static ossl_inline size_t constant_time_msb_s(size_t a)
{
    return 0 - (a >> (sizeof(a) * 8 - 1));
}

static ossl_inline unsigned int constant_time_lt(unsigned int a,
                                                 unsigned int b)
{
    return constant_time_msb(a ^ ((a ^ b) | ((a - b) ^ b)));
}

static ossl_inline size_t constant_time_lt_s(size_t a, size_t b)
{
    return constant_time_msb_s(a ^ ((a ^ b) | ((a - b) ^ b)));
}

static ossl_inline unsigned char constant_time_lt_8(unsigned int a,
                                                    unsigned int b)
{
    return (unsigned char)constant_time_lt(a, b);
}

static ossl_inline uint64_t constant_time_lt_64(uint64_t a, uint64_t b)
{
    return constant_time_msb_64(a ^ ((a ^ b) | ((a - b) ^ b)));
}

static ossl_inline unsigned int constant_time_ge(unsigned int a,
                                                 unsigned int b)
{
    return ~constant_time_lt(a, b);
}

static ossl_inline size_t constant_time_ge_s(size_t a, size_t b)
{
    return ~constant_time_lt_s(a, b);
}

static ossl_inline unsigned char constant_time_ge_8(unsigned int a,
                                                    unsigned int b)
{
    return (unsigned char)constant_time_ge(a, b);
}

static ossl_inline unsigned char constant_time_ge_8_s(size_t a, size_t b)
{
    return (unsigned char)constant_time_ge_s(a, b);
}

static ossl_inline unsigned int constant_time_is_zero(unsigned int a)
{
    return constant_time_msb(~a & (a - 1));
}

static ossl_inline size_t constant_time_is_zero_s(size_t a)
{
    return constant_time_msb_s(~a & (a - 1));
}

static ossl_inline unsigned char constant_time_is_zero_8(unsigned int a)
{
    return (unsigned char)constant_time_is_zero(a);
}

static ossl_inline uint32_t constant_time_is_zero_32(uint32_t a)
{
    return constant_time_msb_32(~a & (a - 1));
}

static ossl_inline unsigned int constant_time_eq(unsigned int a,
                                                 unsigned int b)
{
    return constant_time_is_zero(a ^ b);
}

static ossl_inline size_t constant_time_eq_s(size_t a, size_t b)
{
    return constant_time_is_zero_s(a ^ b);
}

static ossl_inline unsigned char constant_time_eq_8(unsigned int a,
                                                    unsigned int b)
{
    return (unsigned char)constant_time_eq(a, b);
}

static ossl_inline unsigned char constant_time_eq_8_s(size_t a, size_t b)
{
    return (unsigned char)constant_time_eq_s(a, b);
}

static ossl_inline unsigned int constant_time_eq_int(int a, int b)
{
    return constant_time_eq((unsigned)(a), (unsigned)(b));
}

static ossl_inline unsigned char constant_time_eq_int_8(int a, int b)
{
    return constant_time_eq_8((unsigned)(a), (unsigned)(b));
}

static ossl_inline unsigned int constant_time_select(unsigned int mask,
                                                     unsigned int a,
                                                     unsigned int b)
{
    return (mask & a) | (~mask & b);
}

static ossl_inline size_t constant_time_select_s(size_t mask,
                                                 size_t a,
                                                 size_t b)
{
    return (mask & a) | (~mask & b);
}

static ossl_inline unsigned char constant_time_select_8(unsigned char mask,
                                                        unsigned char a,
                                                        unsigned char b)
{
    return (unsigned char)constant_time_select(mask, a, b);
}

static ossl_inline int constant_time_select_int(unsigned int mask, int a,
                                                int b)
{
    return (int)constant_time_select(mask, (unsigned)(a), (unsigned)(b));
}

static ossl_inline int constant_time_select_int_s(size_t mask, int a, int b)
{
    return (int)constant_time_select((unsigned)mask, (unsigned)(a),
                                      (unsigned)(b));
}

static ossl_inline uint32_t constant_time_select_32(uint32_t mask, uint32_t a,
                                                    uint32_t b)
{
    return (mask & a) | (~mask & b);
}

static ossl_inline uint64_t constant_time_select_64(uint64_t mask, uint64_t a,
                                                    uint64_t b)
{
    return (mask & a) | (~mask & b);
}

/*
 * mask must be 0xFFFFFFFF or 0x00000000.
 *
 * if (mask) {
 *     uint32_t tmp = *a;
 *
 *     *a = *b;
 *     *b = tmp;
 * }
 */
static ossl_inline void constant_time_cond_swap_32(uint32_t mask, uint32_t *a,
                                                   uint32_t *b)
{
    uint32_t xor = *a ^ *b;

    xor &= mask;
    *a ^= xor;
    *b ^= xor;
}

/*
 * mask must be 0xFFFFFFFF or 0x00000000.
 *
 * if (mask) {
 *     uint64_t tmp = *a;
 *
 *     *a = *b;
 *     *b = tmp;
 * }
 */
static ossl_inline void constant_time_cond_swap_64(uint64_t mask, uint64_t *a,
                                                   uint64_t *b)
{
    uint64_t xor = *a ^ *b;

    xor &= mask;
    *a ^= xor;
    *b ^= xor;
}

/*
 * table is a two dimensional array of bytes. Each row has rowsize elements.
 * Copies row number idx into out. rowsize and numrows are not considered
 * private.
 */
static ossl_inline void constant_time_lookup(void *out,
                                             const void *table,
                                             size_t rowsize,
                                             size_t numrows,
                                             size_t idx)
{
    size_t i, j;
    const unsigned char *tablec = (const unsigned char *)table;
    unsigned char *outc = (unsigned char *)out;
    unsigned char mask;

    memset(out, 0, rowsize);

    /* Note idx may underflow - but that is well defined */
    for (i = 0; i < numrows; i++, idx--) {
        mask = (unsigned char)constant_time_is_zero_s(idx);
        for (j = 0; j < rowsize; j++)
            *(outc + j) |= constant_time_select_8(mask, *(tablec++), 0);
    }
}

/*
 * Expected usage pattern is to unconditionally set error and then
 * wipe it if there was no actual error. |clear| is 1 or 0.
 */
void err_clear_last_constant_time(int clear);

#endif                          /* HEADER_CONSTANT_TIME_LOCL_H */
