/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h>
#include "ec_lcl.h"
#include <openssl/sha.h>

#if defined(X25519_ASM) && (defined(__x86_64) || defined(__x86_64__) || \
                            defined(_M_AMD64) || defined(_M_X64))

# define BASE_2_64_IMPLEMENTED

typedef uint64_t fe64[4];

int x25519_fe64_eligible(void);

/*
 * Following subroutines perform corresponding operations modulo
 * 2^256-38, i.e. double the curve modulus. However, inputs and
 * outputs are permitted to be partially reduced, i.e. to remain
 * in [0..2^256) range. It's all tied up in final fe64_tobytes
 * that performs full reduction modulo 2^255-19.
 *
 * There are no reference C implementations for these.
 */
void x25519_fe64_mul(fe64 h, const fe64 f, const fe64 g);
void x25519_fe64_sqr(fe64 h, const fe64 f);
void x25519_fe64_mul121666(fe64 h, fe64 f);
void x25519_fe64_add(fe64 h, const fe64 f, const fe64 g);
void x25519_fe64_sub(fe64 h, const fe64 f, const fe64 g);
void x25519_fe64_tobytes(uint8_t *s, const fe64 f);
# define fe64_mul x25519_fe64_mul
# define fe64_sqr x25519_fe64_sqr
# define fe64_mul121666 x25519_fe64_mul121666
# define fe64_add x25519_fe64_add
# define fe64_sub x25519_fe64_sub
# define fe64_tobytes x25519_fe64_tobytes

static uint64_t load_8(const uint8_t *in)
{
    uint64_t result;

    result = in[0];
    result |= ((uint64_t)in[1]) << 8;
    result |= ((uint64_t)in[2]) << 16;
    result |= ((uint64_t)in[3]) << 24;
    result |= ((uint64_t)in[4]) << 32;
    result |= ((uint64_t)in[5]) << 40;
    result |= ((uint64_t)in[6]) << 48;
    result |= ((uint64_t)in[7]) << 56;

    return result;
}

static void fe64_frombytes(fe64 h, const uint8_t *s)
{
    h[0] = load_8(s);
    h[1] = load_8(s + 8);
    h[2] = load_8(s + 16);
    h[3] = load_8(s + 24) & 0x7fffffffffffffff;
}

static void fe64_0(fe64 h)
{
    h[0] = 0;
    h[1] = 0;
    h[2] = 0;
    h[3] = 0;
}

static void fe64_1(fe64 h)
{
    h[0] = 1;
    h[1] = 0;
    h[2] = 0;
    h[3] = 0;
}

static void fe64_copy(fe64 h, const fe64 f)
{
    h[0] = f[0];
    h[1] = f[1];
    h[2] = f[2];
    h[3] = f[3];
}

static void fe64_cswap(fe64 f, fe64 g, unsigned int b)
{
    int i;
    uint64_t mask = 0 - (uint64_t)b;

    for (i = 0; i < 4; i++) {
        uint64_t x = f[i] ^ g[i];
        x &= mask;
        f[i] ^= x;
        g[i] ^= x;
    }
}

static void fe64_invert(fe64 out, const fe64 z)
{
    fe64 t0;
    fe64 t1;
    fe64 t2;
    fe64 t3;
    int i;

    /*
     * Compute z ** -1 = z ** (2 ** 255 - 19 - 2) with the exponent as
     * 2 ** 255 - 21 = (2 ** 5) * (2 ** 250 - 1) + 11.
     */

    /* t0 = z ** 2 */
    fe64_sqr(t0, z);

    /* t1 = t0 ** (2 ** 2) = z ** 8 */
    fe64_sqr(t1, t0);
    fe64_sqr(t1, t1);

    /* t1 = z * t1 = z ** 9 */
    fe64_mul(t1, z, t1);
    /* t0 = t0 * t1 = z ** 11 -- stash t0 away for the end. */
    fe64_mul(t0, t0, t1);

    /* t2 = t0 ** 2 = z ** 22 */
    fe64_sqr(t2, t0);

    /* t1 = t1 * t2 = z ** (2 ** 5 - 1) */
    fe64_mul(t1, t1, t2);

    /* t2 = t1 ** (2 ** 5) = z ** ((2 ** 5) * (2 ** 5 - 1)) */
    fe64_sqr(t2, t1);
    for (i = 1; i < 5; ++i)
        fe64_sqr(t2, t2);

    /* t1 = t1 * t2 = z ** ((2 ** 5 + 1) * (2 ** 5 - 1)) = z ** (2 ** 10 - 1) */
    fe64_mul(t1, t2, t1);

    /* Continuing similarly... */

    /* t2 = z ** (2 ** 20 - 1) */
    fe64_sqr(t2, t1);
    for (i = 1; i < 10; ++i)
        fe64_sqr(t2, t2);

    fe64_mul(t2, t2, t1);

    /* t2 = z ** (2 ** 40 - 1) */
    fe64_sqr(t3, t2);
    for (i = 1; i < 20; ++i)
        fe64_sqr(t3, t3);

    fe64_mul(t2, t3, t2);

    /* t2 = z ** (2 ** 10) * (2 ** 40 - 1) */
    for (i = 0; i < 10; ++i)
        fe64_sqr(t2, t2);

    /* t1 = z ** (2 ** 50 - 1) */
    fe64_mul(t1, t2, t1);

    /* t2 = z ** (2 ** 100 - 1) */
    fe64_sqr(t2, t1);
    for (i = 1; i < 50; ++i)
        fe64_sqr(t2, t2);

    fe64_mul(t2, t2, t1);

    /* t2 = z ** (2 ** 200 - 1) */
    fe64_sqr(t3, t2);
    for (i = 1; i < 100; ++i)
        fe64_sqr(t3, t3);

    fe64_mul(t2, t3, t2);

    /* t2 = z ** ((2 ** 50) * (2 ** 200 - 1) */
    for (i = 0; i < 50; ++i)
        fe64_sqr(t2, t2);

    /* t1 = z ** (2 ** 250 - 1) */
    fe64_mul(t1, t2, t1);

    /* t1 = z ** ((2 ** 5) * (2 ** 250 - 1)) */
    for (i = 0; i < 5; ++i)
        fe64_sqr(t1, t1);

    /* Recall t0 = z ** 11; out = z ** (2 ** 255 - 21) */
    fe64_mul(out, t1, t0);
}

/*
 * Duplicate of original x25519_scalar_mult_generic, but using
 * fe64_* subroutines.
 */
static void x25519_scalar_mulx(uint8_t out[32], const uint8_t scalar[32],
                               const uint8_t point[32])
{
    fe64 x1, x2, z2, x3, z3, tmp0, tmp1;
    uint8_t e[32];
    unsigned swap = 0;
    int pos;

    memcpy(e, scalar, 32);
    e[0]  &= 0xf8;
    e[31] &= 0x7f;
    e[31] |= 0x40;
    fe64_frombytes(x1, point);
    fe64_1(x2);
    fe64_0(z2);
    fe64_copy(x3, x1);
    fe64_1(z3);

    for (pos = 254; pos >= 0; --pos) {
        unsigned int b = 1 & (e[pos / 8] >> (pos & 7));

        swap ^= b;
        fe64_cswap(x2, x3, swap);
        fe64_cswap(z2, z3, swap);
        swap = b;
        fe64_sub(tmp0, x3, z3);
        fe64_sub(tmp1, x2, z2);
        fe64_add(x2, x2, z2);
        fe64_add(z2, x3, z3);
        fe64_mul(z3, x2, tmp0);
        fe64_mul(z2, z2, tmp1);
        fe64_sqr(tmp0, tmp1);
        fe64_sqr(tmp1, x2);
        fe64_add(x3, z3, z2);
        fe64_sub(z2, z3, z2);
        fe64_mul(x2, tmp1, tmp0);
        fe64_sub(tmp1, tmp1, tmp0);
        fe64_sqr(z2, z2);
        fe64_mul121666(z3, tmp1);
        fe64_sqr(x3, x3);
        fe64_add(tmp0, tmp0, z3);
        fe64_mul(z3, x1, z2);
        fe64_mul(z2, tmp1, tmp0);
    }

    fe64_invert(z2, z2);
    fe64_mul(x2, x2, z2);
    fe64_tobytes(out, x2);

    OPENSSL_cleanse(e, sizeof(e));
}
#endif

#if defined(X25519_ASM) \
    || ( (defined(__SIZEOF_INT128__) && __SIZEOF_INT128__ == 16) \
         && !defined(__sparc__) \
         && !(defined(__ANDROID__) && !defined(__clang__)) )
/*
 * Base 2^51 implementation. It's virtually no different from reference
 * base 2^25.5 implementation in respect to lax boundary conditions for
 * intermediate values and even individual limbs. So that whatever you
 * know about the reference, applies even here...
 */
# define BASE_2_51_IMPLEMENTED

typedef uint64_t fe51[5];

static const uint64_t MASK51 = 0x7ffffffffffff;

static uint64_t load_7(const uint8_t *in)
{
    uint64_t result;

    result = in[0];
    result |= ((uint64_t)in[1]) << 8;
    result |= ((uint64_t)in[2]) << 16;
    result |= ((uint64_t)in[3]) << 24;
    result |= ((uint64_t)in[4]) << 32;
    result |= ((uint64_t)in[5]) << 40;
    result |= ((uint64_t)in[6]) << 48;

    return result;
}

static uint64_t load_6(const uint8_t *in)
{
    uint64_t result;

    result = in[0];
    result |= ((uint64_t)in[1]) << 8;
    result |= ((uint64_t)in[2]) << 16;
    result |= ((uint64_t)in[3]) << 24;
    result |= ((uint64_t)in[4]) << 32;
    result |= ((uint64_t)in[5]) << 40;

    return result;
}

static void fe51_frombytes(fe51 h, const uint8_t *s)
{
    uint64_t h0 = load_7(s);                                /* 56 bits */
    uint64_t h1 = load_6(s + 7) << 5;                       /* 53 bits */
    uint64_t h2 = load_7(s + 13) << 2;                      /* 58 bits */
    uint64_t h3 = load_6(s + 20) << 7;                      /* 55 bits */
    uint64_t h4 = (load_6(s + 26) & 0x7fffffffffff) << 4;   /* 51 bits */

    h1 |= h0 >> 51; h0 &= MASK51;
    h2 |= h1 >> 51; h1 &= MASK51;
    h3 |= h2 >> 51; h2 &= MASK51;
    h4 |= h3 >> 51; h3 &= MASK51;

    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
}

static void fe51_tobytes(uint8_t *s, const fe51 h)
{
    uint64_t h0 = h[0];
    uint64_t h1 = h[1];
    uint64_t h2 = h[2];
    uint64_t h3 = h[3];
    uint64_t h4 = h[4];
    uint64_t q;

    /* compare to modulus */
    q = (h0 + 19) >> 51;
    q = (h1 + q) >> 51;
    q = (h2 + q) >> 51;
    q = (h3 + q) >> 51;
    q = (h4 + q) >> 51;

    /* full reduce */
    h0 += 19 * q;
    h1 += h0 >> 51; h0 &= MASK51;
    h2 += h1 >> 51; h1 &= MASK51;
    h3 += h2 >> 51; h2 &= MASK51;
    h4 += h3 >> 51; h3 &= MASK51;
                    h4 &= MASK51;

    /* smash */
    s[0] = (uint8_t)(h0 >> 0);
    s[1] = (uint8_t)(h0 >> 8);
    s[2] = (uint8_t)(h0 >> 16);
    s[3] = (uint8_t)(h0 >> 24);
    s[4] = (uint8_t)(h0 >> 32);
    s[5] = (uint8_t)(h0 >> 40);
    s[6] = (uint8_t)((h0 >> 48) | ((uint32_t)h1 << 3));
    s[7] = (uint8_t)(h1 >> 5);
    s[8] = (uint8_t)(h1 >> 13);
    s[9] = (uint8_t)(h1 >> 21);
    s[10] = (uint8_t)(h1 >> 29);
    s[11] = (uint8_t)(h1 >> 37);
    s[12] = (uint8_t)((h1 >> 45) | ((uint32_t)h2 << 6));
    s[13] = (uint8_t)(h2 >> 2);
    s[14] = (uint8_t)(h2 >> 10);
    s[15] = (uint8_t)(h2 >> 18);
    s[16] = (uint8_t)(h2 >> 26);
    s[17] = (uint8_t)(h2 >> 34);
    s[18] = (uint8_t)(h2 >> 42);
    s[19] = (uint8_t)((h2 >> 50) | ((uint32_t)h3 << 1));
    s[20] = (uint8_t)(h3 >> 7);
    s[21] = (uint8_t)(h3 >> 15);
    s[22] = (uint8_t)(h3 >> 23);
    s[23] = (uint8_t)(h3 >> 31);
    s[24] = (uint8_t)(h3 >> 39);
    s[25] = (uint8_t)((h3 >> 47) | ((uint32_t)h4 << 4));
    s[26] = (uint8_t)(h4 >> 4);
    s[27] = (uint8_t)(h4 >> 12);
    s[28] = (uint8_t)(h4 >> 20);
    s[29] = (uint8_t)(h4 >> 28);
    s[30] = (uint8_t)(h4 >> 36);
    s[31] = (uint8_t)(h4 >> 44);
}

# if defined(X25519_ASM)
void x25519_fe51_mul(fe51 h, const fe51 f, const fe51 g);
void x25519_fe51_sqr(fe51 h, const fe51 f);
void x25519_fe51_mul121666(fe51 h, fe51 f);
#  define fe51_mul x25519_fe51_mul
#  define fe51_sq  x25519_fe51_sqr
#  define fe51_mul121666 x25519_fe51_mul121666
# else

typedef __uint128_t u128;

static void fe51_mul(fe51 h, const fe51 f, const fe51 g)
{
    u128 h0, h1, h2, h3, h4;
    uint64_t f_i, g0, g1, g2, g3, g4;

    f_i = f[0];
    h0 = (u128)f_i * (g0 = g[0]);
    h1 = (u128)f_i * (g1 = g[1]);
    h2 = (u128)f_i * (g2 = g[2]);
    h3 = (u128)f_i * (g3 = g[3]);
    h4 = (u128)f_i * (g4 = g[4]);

    f_i = f[1];
    h0 += (u128)f_i * (g4 *= 19);
    h1 += (u128)f_i * g0;
    h2 += (u128)f_i * g1;
    h3 += (u128)f_i * g2;
    h4 += (u128)f_i * g3;

    f_i = f[2];
    h0 += (u128)f_i * (g3 *= 19);
    h1 += (u128)f_i * g4;
    h2 += (u128)f_i * g0;
    h3 += (u128)f_i * g1;
    h4 += (u128)f_i * g2;

    f_i = f[3];
    h0 += (u128)f_i * (g2 *= 19);
    h1 += (u128)f_i * g3;
    h2 += (u128)f_i * g4;
    h3 += (u128)f_i * g0;
    h4 += (u128)f_i * g1;

    f_i = f[4];
    h0 += (u128)f_i * (g1 *= 19);
    h1 += (u128)f_i * g2;
    h2 += (u128)f_i * g3;
    h3 += (u128)f_i * g4;
    h4 += (u128)f_i * g0;

    /* partial [lazy] reduction */
    h3 += (uint64_t)(h2 >> 51); g2 = (uint64_t)h2 & MASK51;
    h1 += (uint64_t)(h0 >> 51); g0 = (uint64_t)h0 & MASK51;

    h4 += (uint64_t)(h3 >> 51); g3 = (uint64_t)h3 & MASK51;
    g2 += (uint64_t)(h1 >> 51); g1 = (uint64_t)h1 & MASK51;

    g0 += (uint64_t)(h4 >> 51) * 19; g4 = (uint64_t)h4 & MASK51;
    g3 += g2 >> 51; g2 &= MASK51;
    g1 += g0 >> 51; g0 &= MASK51;

    h[0] = g0;
    h[1] = g1;
    h[2] = g2;
    h[3] = g3;
    h[4] = g4;
}

static void fe51_sq(fe51 h, const fe51 f)
{
#  if defined(OPENSSL_SMALL_FOOTPRINT)
    fe51_mul(h, f, f);
#  else
    /* dedicated squaring gives 16-25% overall improvement */
    uint64_t g0 = f[0];
    uint64_t g1 = f[1];
    uint64_t g2 = f[2];
    uint64_t g3 = f[3];
    uint64_t g4 = f[4];
    u128 h0, h1, h2, h3, h4;

    h0 = (u128)g0 * g0;     g0 *= 2;
    h1 = (u128)g0 * g1;
    h2 = (u128)g0 * g2;
    h3 = (u128)g0 * g3;
    h4 = (u128)g0 * g4;

    g0 = g4;                /* borrow g0 */
    h3 += (u128)g0 * (g4 *= 19);

    h2 += (u128)g1 * g1;    g1 *= 2;
    h3 += (u128)g1 * g2;
    h4 += (u128)g1 * g3;
    h0 += (u128)g1 * g4;

    g0 = g3;                /* borrow g0 */
    h1 += (u128)g0 * (g3 *= 19);
    h2 += (u128)(g0 * 2) * g4;

    h4 += (u128)g2 * g2;    g2 *= 2;
    h0 += (u128)g2 * g3;
    h1 += (u128)g2 * g4;

    /* partial [lazy] reduction */
    h3 += (uint64_t)(h2 >> 51); g2 = (uint64_t)h2 & MASK51;
    h1 += (uint64_t)(h0 >> 51); g0 = (uint64_t)h0 & MASK51;

    h4 += (uint64_t)(h3 >> 51); g3 = (uint64_t)h3 & MASK51;
    g2 += (uint64_t)(h1 >> 51); g1 = (uint64_t)h1 & MASK51;

    g0 += (uint64_t)(h4 >> 51) * 19; g4 = (uint64_t)h4 & MASK51;
    g3 += g2 >> 51; g2 &= MASK51;
    g1 += g0 >> 51; g0 &= MASK51;

    h[0] = g0;
    h[1] = g1;
    h[2] = g2;
    h[3] = g3;
    h[4] = g4;
#  endif
}

static void fe51_mul121666(fe51 h, fe51 f)
{
    u128 h0 = f[0] * (u128)121666;
    u128 h1 = f[1] * (u128)121666;
    u128 h2 = f[2] * (u128)121666;
    u128 h3 = f[3] * (u128)121666;
    u128 h4 = f[4] * (u128)121666;
    uint64_t g0, g1, g2, g3, g4;

    h3 += (uint64_t)(h2 >> 51); g2 = (uint64_t)h2 & MASK51;
    h1 += (uint64_t)(h0 >> 51); g0 = (uint64_t)h0 & MASK51;

    h4 += (uint64_t)(h3 >> 51); g3 = (uint64_t)h3 & MASK51;
    g2 += (uint64_t)(h1 >> 51); g1 = (uint64_t)h1 & MASK51;

    g0 += (uint64_t)(h4 >> 51) * 19; g4 = (uint64_t)h4 & MASK51;
    g3 += g2 >> 51; g2 &= MASK51;
    g1 += g0 >> 51; g0 &= MASK51;

    h[0] = g0;
    h[1] = g1;
    h[2] = g2;
    h[3] = g3;
    h[4] = g4;
}
# endif

static void fe51_add(fe51 h, const fe51 f, const fe51 g)
{
    h[0] = f[0] + g[0];
    h[1] = f[1] + g[1];
    h[2] = f[2] + g[2];
    h[3] = f[3] + g[3];
    h[4] = f[4] + g[4];
}

static void fe51_sub(fe51 h, const fe51 f, const fe51 g)
{
    /*
     * Add 2*modulus to ensure that result remains positive
     * even if subtrahend is partially reduced.
     */
    h[0] = (f[0] + 0xfffffffffffda) - g[0];
    h[1] = (f[1] + 0xffffffffffffe) - g[1];
    h[2] = (f[2] + 0xffffffffffffe) - g[2];
    h[3] = (f[3] + 0xffffffffffffe) - g[3];
    h[4] = (f[4] + 0xffffffffffffe) - g[4];
}

static void fe51_0(fe51 h)
{
    h[0] = 0;
    h[1] = 0;
    h[2] = 0;
    h[3] = 0;
    h[4] = 0;
}

static void fe51_1(fe51 h)
{
    h[0] = 1;
    h[1] = 0;
    h[2] = 0;
    h[3] = 0;
    h[4] = 0;
}

static void fe51_copy(fe51 h, const fe51 f)
{
    h[0] = f[0];
    h[1] = f[1];
    h[2] = f[2];
    h[3] = f[3];
    h[4] = f[4];
}

static void fe51_cswap(fe51 f, fe51 g, unsigned int b)
{
    int i;
    uint64_t mask = 0 - (uint64_t)b;

    for (i = 0; i < 5; i++) {
        int64_t x = f[i] ^ g[i];
        x &= mask;
        f[i] ^= x;
        g[i] ^= x;
    }
}

static void fe51_invert(fe51 out, const fe51 z)
{
    fe51 t0;
    fe51 t1;
    fe51 t2;
    fe51 t3;
    int i;

    /*
     * Compute z ** -1 = z ** (2 ** 255 - 19 - 2) with the exponent as
     * 2 ** 255 - 21 = (2 ** 5) * (2 ** 250 - 1) + 11.
     */

    /* t0 = z ** 2 */
    fe51_sq(t0, z);

    /* t1 = t0 ** (2 ** 2) = z ** 8 */
    fe51_sq(t1, t0);
    fe51_sq(t1, t1);

    /* t1 = z * t1 = z ** 9 */
    fe51_mul(t1, z, t1);
    /* t0 = t0 * t1 = z ** 11 -- stash t0 away for the end. */
    fe51_mul(t0, t0, t1);

    /* t2 = t0 ** 2 = z ** 22 */
    fe51_sq(t2, t0);

    /* t1 = t1 * t2 = z ** (2 ** 5 - 1) */
    fe51_mul(t1, t1, t2);

    /* t2 = t1 ** (2 ** 5) = z ** ((2 ** 5) * (2 ** 5 - 1)) */
    fe51_sq(t2, t1);
    for (i = 1; i < 5; ++i)
        fe51_sq(t2, t2);

    /* t1 = t1 * t2 = z ** ((2 ** 5 + 1) * (2 ** 5 - 1)) = z ** (2 ** 10 - 1) */
    fe51_mul(t1, t2, t1);

    /* Continuing similarly... */

    /* t2 = z ** (2 ** 20 - 1) */
    fe51_sq(t2, t1);
    for (i = 1; i < 10; ++i)
        fe51_sq(t2, t2);

    fe51_mul(t2, t2, t1);

    /* t2 = z ** (2 ** 40 - 1) */
    fe51_sq(t3, t2);
    for (i = 1; i < 20; ++i)
        fe51_sq(t3, t3);

    fe51_mul(t2, t3, t2);

    /* t2 = z ** (2 ** 10) * (2 ** 40 - 1) */
    for (i = 0; i < 10; ++i)
        fe51_sq(t2, t2);

    /* t1 = z ** (2 ** 50 - 1) */
    fe51_mul(t1, t2, t1);

    /* t2 = z ** (2 ** 100 - 1) */
    fe51_sq(t2, t1);
    for (i = 1; i < 50; ++i)
        fe51_sq(t2, t2);

    fe51_mul(t2, t2, t1);

    /* t2 = z ** (2 ** 200 - 1) */
    fe51_sq(t3, t2);
    for (i = 1; i < 100; ++i)
        fe51_sq(t3, t3);

    fe51_mul(t2, t3, t2);

    /* t2 = z ** ((2 ** 50) * (2 ** 200 - 1) */
    for (i = 0; i < 50; ++i)
        fe51_sq(t2, t2);

    /* t1 = z ** (2 ** 250 - 1) */
    fe51_mul(t1, t2, t1);

    /* t1 = z ** ((2 ** 5) * (2 ** 250 - 1)) */
    for (i = 0; i < 5; ++i)
        fe51_sq(t1, t1);

    /* Recall t0 = z ** 11; out = z ** (2 ** 255 - 21) */
    fe51_mul(out, t1, t0);
}

/*
 * Duplicate of original x25519_scalar_mult_generic, but using
 * fe51_* subroutines.
 */
static void x25519_scalar_mult(uint8_t out[32], const uint8_t scalar[32],
                               const uint8_t point[32])
{
    fe51 x1, x2, z2, x3, z3, tmp0, tmp1;
    uint8_t e[32];
    unsigned swap = 0;
    int pos;

# ifdef BASE_2_64_IMPLEMENTED
    if (x25519_fe64_eligible()) {
        x25519_scalar_mulx(out, scalar, point);
        return;
    }
# endif

    memcpy(e, scalar, 32);
    e[0]  &= 0xf8;
    e[31] &= 0x7f;
    e[31] |= 0x40;
    fe51_frombytes(x1, point);
    fe51_1(x2);
    fe51_0(z2);
    fe51_copy(x3, x1);
    fe51_1(z3);

    for (pos = 254; pos >= 0; --pos) {
        unsigned int b = 1 & (e[pos / 8] >> (pos & 7));

        swap ^= b;
        fe51_cswap(x2, x3, swap);
        fe51_cswap(z2, z3, swap);
        swap = b;
        fe51_sub(tmp0, x3, z3);
        fe51_sub(tmp1, x2, z2);
        fe51_add(x2, x2, z2);
        fe51_add(z2, x3, z3);
        fe51_mul(z3, tmp0, x2);
        fe51_mul(z2, z2, tmp1);
        fe51_sq(tmp0, tmp1);
        fe51_sq(tmp1, x2);
        fe51_add(x3, z3, z2);
        fe51_sub(z2, z3, z2);
        fe51_mul(x2, tmp1, tmp0);
        fe51_sub(tmp1, tmp1, tmp0);
        fe51_sq(z2, z2);
        fe51_mul121666(z3, tmp1);
        fe51_sq(x3, x3);
        fe51_add(tmp0, tmp0, z3);
        fe51_mul(z3, x1, z2);
        fe51_mul(z2, tmp1, tmp0);
    }

    fe51_invert(z2, z2);
    fe51_mul(x2, x2, z2);
    fe51_tobytes(out, x2);

    OPENSSL_cleanse(e, sizeof(e));
}
#endif

/*
 * Reference base 2^25.5 implementation.
 *
 * This code is mostly taken from the ref10 version of Ed25519 in SUPERCOP
 * 20141124 (http://bench.cr.yp.to/supercop.html).
 *
 * The field functions are shared by Ed25519 and X25519 where possible.
 */

/*
 * fe means field element. Here the field is \Z/(2^255-19). An element t,
 * entries t[0]...t[9], represents the integer t[0]+2^26 t[1]+2^51 t[2]+2^77
 * t[3]+2^102 t[4]+...+2^230 t[9]. Bounds on each t[i] vary depending on
 * context.
 */
typedef int32_t fe[10];

static const int64_t kBottom21Bits =  0x1fffffLL;
static const int64_t kBottom25Bits = 0x1ffffffLL;
static const int64_t kBottom26Bits = 0x3ffffffLL;
static const int64_t kTop39Bits = 0xfffffffffe000000LL;
static const int64_t kTop38Bits = 0xfffffffffc000000LL;

static uint64_t load_3(const uint8_t *in)
{
    uint64_t result;

    result  = ((uint64_t)in[0]);
    result |= ((uint64_t)in[1]) << 8;
    result |= ((uint64_t)in[2]) << 16;
    return result;
}

static uint64_t load_4(const uint8_t *in)
{
    uint64_t result;

    result  = ((uint64_t)in[0]);
    result |= ((uint64_t)in[1]) << 8;
    result |= ((uint64_t)in[2]) << 16;
    result |= ((uint64_t)in[3]) << 24;
    return result;
}

static void fe_frombytes(fe h, const uint8_t *s)
{
    /* Ignores top bit of h. */
    int64_t h0 =  load_4(s);
    int64_t h1 =  load_3(s +  4) << 6;
    int64_t h2 =  load_3(s +  7) << 5;
    int64_t h3 =  load_3(s + 10) << 3;
    int64_t h4 =  load_3(s + 13) << 2;
    int64_t h5 =  load_4(s + 16);
    int64_t h6 =  load_3(s + 20) << 7;
    int64_t h7 =  load_3(s + 23) << 5;
    int64_t h8 =  load_3(s + 26) << 4;
    int64_t h9 = (load_3(s + 29) & 0x7fffff) << 2;
    int64_t carry0;
    int64_t carry1;
    int64_t carry2;
    int64_t carry3;
    int64_t carry4;
    int64_t carry5;
    int64_t carry6;
    int64_t carry7;
    int64_t carry8;
    int64_t carry9;

    carry9 = h9 + (1 << 24); h0 += (carry9 >> 25) * 19; h9 -= carry9 & kTop39Bits;
    carry1 = h1 + (1 << 24); h2 += carry1 >> 25; h1 -= carry1 & kTop39Bits;
    carry3 = h3 + (1 << 24); h4 += carry3 >> 25; h3 -= carry3 & kTop39Bits;
    carry5 = h5 + (1 << 24); h6 += carry5 >> 25; h5 -= carry5 & kTop39Bits;
    carry7 = h7 + (1 << 24); h8 += carry7 >> 25; h7 -= carry7 & kTop39Bits;

    carry0 = h0 + (1 << 25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;
    carry2 = h2 + (1 << 25); h3 += carry2 >> 26; h2 -= carry2 & kTop38Bits;
    carry4 = h4 + (1 << 25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;
    carry6 = h6 + (1 << 25); h7 += carry6 >> 26; h6 -= carry6 & kTop38Bits;
    carry8 = h8 + (1 << 25); h9 += carry8 >> 26; h8 -= carry8 & kTop38Bits;

    h[0] = (int32_t)h0;
    h[1] = (int32_t)h1;
    h[2] = (int32_t)h2;
    h[3] = (int32_t)h3;
    h[4] = (int32_t)h4;
    h[5] = (int32_t)h5;
    h[6] = (int32_t)h6;
    h[7] = (int32_t)h7;
    h[8] = (int32_t)h8;
    h[9] = (int32_t)h9;
}

/*
 * Preconditions:
 *   |h| bounded by 1.1*2^26,1.1*2^25,1.1*2^26,1.1*2^25,etc.
 *
 * Write p=2^255-19; q=floor(h/p).
 * Basic claim: q = floor(2^(-255)(h + 19 2^(-25)h9 + 2^(-1))).
 *
 * Proof:
 *   Have |h|<=p so |q|<=1 so |19^2 2^(-255) q|<1/4.
 *   Also have |h-2^230 h9|<2^231 so |19 2^(-255)(h-2^230 h9)|<1/4.
 *
 *   Write y=2^(-1)-19^2 2^(-255)q-19 2^(-255)(h-2^230 h9).
 *   Then 0<y<1.
 *
 *   Write r=h-pq.
 *   Have 0<=r<=p-1=2^255-20.
 *   Thus 0<=r+19(2^-255)r<r+19(2^-255)2^255<=2^255-1.
 *
 *   Write x=r+19(2^-255)r+y.
 *   Then 0<x<2^255 so floor(2^(-255)x) = 0 so floor(q+2^(-255)x) = q.
 *
 *   Have q+2^(-255)x = 2^(-255)(h + 19 2^(-25) h9 + 2^(-1))
 *   so floor(2^(-255)(h + 19 2^(-25) h9 + 2^(-1))) = q.
 */
static void fe_tobytes(uint8_t *s, const fe h)
{
    int32_t h0 = h[0];
    int32_t h1 = h[1];
    int32_t h2 = h[2];
    int32_t h3 = h[3];
    int32_t h4 = h[4];
    int32_t h5 = h[5];
    int32_t h6 = h[6];
    int32_t h7 = h[7];
    int32_t h8 = h[8];
    int32_t h9 = h[9];
    int32_t q;

    q = (19 * h9 + (((int32_t) 1) << 24)) >> 25;
    q = (h0 + q) >> 26;
    q = (h1 + q) >> 25;
    q = (h2 + q) >> 26;
    q = (h3 + q) >> 25;
    q = (h4 + q) >> 26;
    q = (h5 + q) >> 25;
    q = (h6 + q) >> 26;
    q = (h7 + q) >> 25;
    q = (h8 + q) >> 26;
    q = (h9 + q) >> 25;

    /* Goal: Output h-(2^255-19)q, which is between 0 and 2^255-20. */
    h0 += 19 * q;
    /* Goal: Output h-2^255 q, which is between 0 and 2^255-20. */

    h1 += h0 >> 26; h0 &= kBottom26Bits;
    h2 += h1 >> 25; h1 &= kBottom25Bits;
    h3 += h2 >> 26; h2 &= kBottom26Bits;
    h4 += h3 >> 25; h3 &= kBottom25Bits;
    h5 += h4 >> 26; h4 &= kBottom26Bits;
    h6 += h5 >> 25; h5 &= kBottom25Bits;
    h7 += h6 >> 26; h6 &= kBottom26Bits;
    h8 += h7 >> 25; h7 &= kBottom25Bits;
    h9 += h8 >> 26; h8 &= kBottom26Bits;
                    h9 &= kBottom25Bits;
    /* h10 = carry9 */

    /*
     * Goal: Output h0+...+2^255 h10-2^255 q, which is between 0 and 2^255-20.
     * Have h0+...+2^230 h9 between 0 and 2^255-1;
     * evidently 2^255 h10-2^255 q = 0.
     * Goal: Output h0+...+2^230 h9.
     */
    s[ 0] = (uint8_t) (h0 >>  0);
    s[ 1] = (uint8_t) (h0 >>  8);
    s[ 2] = (uint8_t) (h0 >> 16);
    s[ 3] = (uint8_t)((h0 >> 24) | ((uint32_t)(h1) << 2));
    s[ 4] = (uint8_t) (h1 >>  6);
    s[ 5] = (uint8_t) (h1 >> 14);
    s[ 6] = (uint8_t)((h1 >> 22) | ((uint32_t)(h2) << 3));
    s[ 7] = (uint8_t) (h2 >>  5);
    s[ 8] = (uint8_t) (h2 >> 13);
    s[ 9] = (uint8_t)((h2 >> 21) | ((uint32_t)(h3) << 5));
    s[10] = (uint8_t) (h3 >>  3);
    s[11] = (uint8_t) (h3 >> 11);
    s[12] = (uint8_t)((h3 >> 19) | ((uint32_t)(h4) << 6));
    s[13] = (uint8_t) (h4 >>  2);
    s[14] = (uint8_t) (h4 >> 10);
    s[15] = (uint8_t) (h4 >> 18);
    s[16] = (uint8_t) (h5 >>  0);
    s[17] = (uint8_t) (h5 >>  8);
    s[18] = (uint8_t) (h5 >> 16);
    s[19] = (uint8_t)((h5 >> 24) | ((uint32_t)(h6) << 1));
    s[20] = (uint8_t) (h6 >>  7);
    s[21] = (uint8_t) (h6 >> 15);
    s[22] = (uint8_t)((h6 >> 23) | ((uint32_t)(h7) << 3));
    s[23] = (uint8_t) (h7 >>  5);
    s[24] = (uint8_t) (h7 >> 13);
    s[25] = (uint8_t)((h7 >> 21) | ((uint32_t)(h8) << 4));
    s[26] = (uint8_t) (h8 >>  4);
    s[27] = (uint8_t) (h8 >> 12);
    s[28] = (uint8_t)((h8 >> 20) | ((uint32_t)(h9) << 6));
    s[29] = (uint8_t) (h9 >>  2);
    s[30] = (uint8_t) (h9 >> 10);
    s[31] = (uint8_t) (h9 >> 18);
}

/* h = f */
static void fe_copy(fe h, const fe f)
{
    memmove(h, f, sizeof(int32_t) * 10);
}

/* h = 0 */
static void fe_0(fe h)
{
    memset(h, 0, sizeof(int32_t) * 10);
}

/* h = 1 */
static void fe_1(fe h)
{
    memset(h, 0, sizeof(int32_t) * 10);
    h[0] = 1;
}

/*
 * h = f + g
 *
 * Can overlap h with f or g.
 *
 * Preconditions:
 *    |f| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc.
 *    |g| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.1*2^26,1.1*2^25,1.1*2^26,1.1*2^25,etc.
 */
static void fe_add(fe h, const fe f, const fe g)
{
    unsigned i;

    for (i = 0; i < 10; i++) {
        h[i] = f[i] + g[i];
    }
}

/*
 * h = f - g
 *
 * Can overlap h with f or g.
 *
 * Preconditions:
 *    |f| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc.
 *    |g| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.1*2^26,1.1*2^25,1.1*2^26,1.1*2^25,etc.
 */
static void fe_sub(fe h, const fe f, const fe g)
{
    unsigned i;

    for (i = 0; i < 10; i++) {
        h[i] = f[i] - g[i];
    }
}

/*
 * h = f * g
 *
 * Can overlap h with f or g.
 *
 * Preconditions:
 *    |f| bounded by 1.65*2^26,1.65*2^25,1.65*2^26,1.65*2^25,etc.
 *    |g| bounded by 1.65*2^26,1.65*2^25,1.65*2^26,1.65*2^25,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.01*2^25,1.01*2^24,1.01*2^25,1.01*2^24,etc.
 *
 * Notes on implementation strategy:
 *
 * Using schoolbook multiplication.
 * Karatsuba would save a little in some cost models.
 *
 * Most multiplications by 2 and 19 are 32-bit precomputations;
 * cheaper than 64-bit postcomputations.
 *
 * There is one remaining multiplication by 19 in the carry chain;
 * one *19 precomputation can be merged into this,
 * but the resulting data flow is considerably less clean.
 *
 * There are 12 carries below.
 * 10 of them are 2-way parallelizable and vectorizable.
 * Can get away with 11 carries, but then data flow is much deeper.
 *
 * With tighter constraints on inputs can squeeze carries into int32.
 */
static void fe_mul(fe h, const fe f, const fe g)
{
    int32_t f0 = f[0];
    int32_t f1 = f[1];
    int32_t f2 = f[2];
    int32_t f3 = f[3];
    int32_t f4 = f[4];
    int32_t f5 = f[5];
    int32_t f6 = f[6];
    int32_t f7 = f[7];
    int32_t f8 = f[8];
    int32_t f9 = f[9];
    int32_t g0 = g[0];
    int32_t g1 = g[1];
    int32_t g2 = g[2];
    int32_t g3 = g[3];
    int32_t g4 = g[4];
    int32_t g5 = g[5];
    int32_t g6 = g[6];
    int32_t g7 = g[7];
    int32_t g8 = g[8];
    int32_t g9 = g[9];
    int32_t g1_19 = 19 * g1; /* 1.959375*2^29 */
    int32_t g2_19 = 19 * g2; /* 1.959375*2^30; still ok */
    int32_t g3_19 = 19 * g3;
    int32_t g4_19 = 19 * g4;
    int32_t g5_19 = 19 * g5;
    int32_t g6_19 = 19 * g6;
    int32_t g7_19 = 19 * g7;
    int32_t g8_19 = 19 * g8;
    int32_t g9_19 = 19 * g9;
    int32_t f1_2 = 2 * f1;
    int32_t f3_2 = 2 * f3;
    int32_t f5_2 = 2 * f5;
    int32_t f7_2 = 2 * f7;
    int32_t f9_2 = 2 * f9;
    int64_t f0g0    = f0   * (int64_t) g0;
    int64_t f0g1    = f0   * (int64_t) g1;
    int64_t f0g2    = f0   * (int64_t) g2;
    int64_t f0g3    = f0   * (int64_t) g3;
    int64_t f0g4    = f0   * (int64_t) g4;
    int64_t f0g5    = f0   * (int64_t) g5;
    int64_t f0g6    = f0   * (int64_t) g6;
    int64_t f0g7    = f0   * (int64_t) g7;
    int64_t f0g8    = f0   * (int64_t) g8;
    int64_t f0g9    = f0   * (int64_t) g9;
    int64_t f1g0    = f1   * (int64_t) g0;
    int64_t f1g1_2  = f1_2 * (int64_t) g1;
    int64_t f1g2    = f1   * (int64_t) g2;
    int64_t f1g3_2  = f1_2 * (int64_t) g3;
    int64_t f1g4    = f1   * (int64_t) g4;
    int64_t f1g5_2  = f1_2 * (int64_t) g5;
    int64_t f1g6    = f1   * (int64_t) g6;
    int64_t f1g7_2  = f1_2 * (int64_t) g7;
    int64_t f1g8    = f1   * (int64_t) g8;
    int64_t f1g9_38 = f1_2 * (int64_t) g9_19;
    int64_t f2g0    = f2   * (int64_t) g0;
    int64_t f2g1    = f2   * (int64_t) g1;
    int64_t f2g2    = f2   * (int64_t) g2;
    int64_t f2g3    = f2   * (int64_t) g3;
    int64_t f2g4    = f2   * (int64_t) g4;
    int64_t f2g5    = f2   * (int64_t) g5;
    int64_t f2g6    = f2   * (int64_t) g6;
    int64_t f2g7    = f2   * (int64_t) g7;
    int64_t f2g8_19 = f2   * (int64_t) g8_19;
    int64_t f2g9_19 = f2   * (int64_t) g9_19;
    int64_t f3g0    = f3   * (int64_t) g0;
    int64_t f3g1_2  = f3_2 * (int64_t) g1;
    int64_t f3g2    = f3   * (int64_t) g2;
    int64_t f3g3_2  = f3_2 * (int64_t) g3;
    int64_t f3g4    = f3   * (int64_t) g4;
    int64_t f3g5_2  = f3_2 * (int64_t) g5;
    int64_t f3g6    = f3   * (int64_t) g6;
    int64_t f3g7_38 = f3_2 * (int64_t) g7_19;
    int64_t f3g8_19 = f3   * (int64_t) g8_19;
    int64_t f3g9_38 = f3_2 * (int64_t) g9_19;
    int64_t f4g0    = f4   * (int64_t) g0;
    int64_t f4g1    = f4   * (int64_t) g1;
    int64_t f4g2    = f4   * (int64_t) g2;
    int64_t f4g3    = f4   * (int64_t) g3;
    int64_t f4g4    = f4   * (int64_t) g4;
    int64_t f4g5    = f4   * (int64_t) g5;
    int64_t f4g6_19 = f4   * (int64_t) g6_19;
    int64_t f4g7_19 = f4   * (int64_t) g7_19;
    int64_t f4g8_19 = f4   * (int64_t) g8_19;
    int64_t f4g9_19 = f4   * (int64_t) g9_19;
    int64_t f5g0    = f5   * (int64_t) g0;
    int64_t f5g1_2  = f5_2 * (int64_t) g1;
    int64_t f5g2    = f5   * (int64_t) g2;
    int64_t f5g3_2  = f5_2 * (int64_t) g3;
    int64_t f5g4    = f5   * (int64_t) g4;
    int64_t f5g5_38 = f5_2 * (int64_t) g5_19;
    int64_t f5g6_19 = f5   * (int64_t) g6_19;
    int64_t f5g7_38 = f5_2 * (int64_t) g7_19;
    int64_t f5g8_19 = f5   * (int64_t) g8_19;
    int64_t f5g9_38 = f5_2 * (int64_t) g9_19;
    int64_t f6g0    = f6   * (int64_t) g0;
    int64_t f6g1    = f6   * (int64_t) g1;
    int64_t f6g2    = f6   * (int64_t) g2;
    int64_t f6g3    = f6   * (int64_t) g3;
    int64_t f6g4_19 = f6   * (int64_t) g4_19;
    int64_t f6g5_19 = f6   * (int64_t) g5_19;
    int64_t f6g6_19 = f6   * (int64_t) g6_19;
    int64_t f6g7_19 = f6   * (int64_t) g7_19;
    int64_t f6g8_19 = f6   * (int64_t) g8_19;
    int64_t f6g9_19 = f6   * (int64_t) g9_19;
    int64_t f7g0    = f7   * (int64_t) g0;
    int64_t f7g1_2  = f7_2 * (int64_t) g1;
    int64_t f7g2    = f7   * (int64_t) g2;
    int64_t f7g3_38 = f7_2 * (int64_t) g3_19;
    int64_t f7g4_19 = f7   * (int64_t) g4_19;
    int64_t f7g5_38 = f7_2 * (int64_t) g5_19;
    int64_t f7g6_19 = f7   * (int64_t) g6_19;
    int64_t f7g7_38 = f7_2 * (int64_t) g7_19;
    int64_t f7g8_19 = f7   * (int64_t) g8_19;
    int64_t f7g9_38 = f7_2 * (int64_t) g9_19;
    int64_t f8g0    = f8   * (int64_t) g0;
    int64_t f8g1    = f8   * (int64_t) g1;
    int64_t f8g2_19 = f8   * (int64_t) g2_19;
    int64_t f8g3_19 = f8   * (int64_t) g3_19;
    int64_t f8g4_19 = f8   * (int64_t) g4_19;
    int64_t f8g5_19 = f8   * (int64_t) g5_19;
    int64_t f8g6_19 = f8   * (int64_t) g6_19;
    int64_t f8g7_19 = f8   * (int64_t) g7_19;
    int64_t f8g8_19 = f8   * (int64_t) g8_19;
    int64_t f8g9_19 = f8   * (int64_t) g9_19;
    int64_t f9g0    = f9   * (int64_t) g0;
    int64_t f9g1_38 = f9_2 * (int64_t) g1_19;
    int64_t f9g2_19 = f9   * (int64_t) g2_19;
    int64_t f9g3_38 = f9_2 * (int64_t) g3_19;
    int64_t f9g4_19 = f9   * (int64_t) g4_19;
    int64_t f9g5_38 = f9_2 * (int64_t) g5_19;
    int64_t f9g6_19 = f9   * (int64_t) g6_19;
    int64_t f9g7_38 = f9_2 * (int64_t) g7_19;
    int64_t f9g8_19 = f9   * (int64_t) g8_19;
    int64_t f9g9_38 = f9_2 * (int64_t) g9_19;
    int64_t h0 = f0g0 + f1g9_38 + f2g8_19 + f3g7_38 + f4g6_19 + f5g5_38 + f6g4_19 + f7g3_38 + f8g2_19 + f9g1_38;
    int64_t h1 = f0g1 + f1g0    + f2g9_19 + f3g8_19 + f4g7_19 + f5g6_19 + f6g5_19 + f7g4_19 + f8g3_19 + f9g2_19;
    int64_t h2 = f0g2 + f1g1_2  + f2g0    + f3g9_38 + f4g8_19 + f5g7_38 + f6g6_19 + f7g5_38 + f8g4_19 + f9g3_38;
    int64_t h3 = f0g3 + f1g2    + f2g1    + f3g0    + f4g9_19 + f5g8_19 + f6g7_19 + f7g6_19 + f8g5_19 + f9g4_19;
    int64_t h4 = f0g4 + f1g3_2  + f2g2    + f3g1_2  + f4g0    + f5g9_38 + f6g8_19 + f7g7_38 + f8g6_19 + f9g5_38;
    int64_t h5 = f0g5 + f1g4    + f2g3    + f3g2    + f4g1    + f5g0    + f6g9_19 + f7g8_19 + f8g7_19 + f9g6_19;
    int64_t h6 = f0g6 + f1g5_2  + f2g4    + f3g3_2  + f4g2    + f5g1_2  + f6g0    + f7g9_38 + f8g8_19 + f9g7_38;
    int64_t h7 = f0g7 + f1g6    + f2g5    + f3g4    + f4g3    + f5g2    + f6g1    + f7g0    + f8g9_19 + f9g8_19;
    int64_t h8 = f0g8 + f1g7_2  + f2g6    + f3g5_2  + f4g4    + f5g3_2  + f6g2    + f7g1_2  + f8g0    + f9g9_38;
    int64_t h9 = f0g9 + f1g8    + f2g7    + f3g6    + f4g5    + f5g4    + f6g3    + f7g2    + f8g1    + f9g0   ;
    int64_t carry0;
    int64_t carry1;
    int64_t carry2;
    int64_t carry3;
    int64_t carry4;
    int64_t carry5;
    int64_t carry6;
    int64_t carry7;
    int64_t carry8;
    int64_t carry9;

    /* |h0| <= (1.65*1.65*2^52*(1+19+19+19+19)+1.65*1.65*2^50*(38+38+38+38+38))
     *   i.e. |h0| <= 1.4*2^60; narrower ranges for h2, h4, h6, h8
     * |h1| <= (1.65*1.65*2^51*(1+1+19+19+19+19+19+19+19+19))
     *   i.e. |h1| <= 1.7*2^59; narrower ranges for h3, h5, h7, h9 */

    carry0 = h0 + (1 << 25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;
    carry4 = h4 + (1 << 25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;
    /* |h0| <= 2^25 */
    /* |h4| <= 2^25 */
    /* |h1| <= 1.71*2^59 */
    /* |h5| <= 1.71*2^59 */

    carry1 = h1 + (1 << 24); h2 += carry1 >> 25; h1 -= carry1 & kTop39Bits;
    carry5 = h5 + (1 << 24); h6 += carry5 >> 25; h5 -= carry5 & kTop39Bits;
    /* |h1| <= 2^24; from now on fits into int32 */
    /* |h5| <= 2^24; from now on fits into int32 */
    /* |h2| <= 1.41*2^60 */
    /* |h6| <= 1.41*2^60 */

    carry2 = h2 + (1 << 25); h3 += carry2 >> 26; h2 -= carry2 & kTop38Bits;
    carry6 = h6 + (1 << 25); h7 += carry6 >> 26; h6 -= carry6 & kTop38Bits;
    /* |h2| <= 2^25; from now on fits into int32 unchanged */
    /* |h6| <= 2^25; from now on fits into int32 unchanged */
    /* |h3| <= 1.71*2^59 */
    /* |h7| <= 1.71*2^59 */

    carry3 = h3 + (1 << 24); h4 += carry3 >> 25; h3 -= carry3 & kTop39Bits;
    carry7 = h7 + (1 << 24); h8 += carry7 >> 25; h7 -= carry7 & kTop39Bits;
    /* |h3| <= 2^24; from now on fits into int32 unchanged */
    /* |h7| <= 2^24; from now on fits into int32 unchanged */
    /* |h4| <= 1.72*2^34 */
    /* |h8| <= 1.41*2^60 */

    carry4 = h4 + (1 << 25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;
    carry8 = h8 + (1 << 25); h9 += carry8 >> 26; h8 -= carry8 & kTop38Bits;
    /* |h4| <= 2^25; from now on fits into int32 unchanged */
    /* |h8| <= 2^25; from now on fits into int32 unchanged */
    /* |h5| <= 1.01*2^24 */
    /* |h9| <= 1.71*2^59 */

    carry9 = h9 + (1 << 24); h0 += (carry9 >> 25) * 19; h9 -= carry9 & kTop39Bits;
    /* |h9| <= 2^24; from now on fits into int32 unchanged */
    /* |h0| <= 1.1*2^39 */

    carry0 = h0 + (1 << 25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;
    /* |h0| <= 2^25; from now on fits into int32 unchanged */
    /* |h1| <= 1.01*2^24 */

    h[0] = (int32_t)h0;
    h[1] = (int32_t)h1;
    h[2] = (int32_t)h2;
    h[3] = (int32_t)h3;
    h[4] = (int32_t)h4;
    h[5] = (int32_t)h5;
    h[6] = (int32_t)h6;
    h[7] = (int32_t)h7;
    h[8] = (int32_t)h8;
    h[9] = (int32_t)h9;
}

/*
 * h = f * f
 *
 * Can overlap h with f.
 *
 * Preconditions:
 *    |f| bounded by 1.65*2^26,1.65*2^25,1.65*2^26,1.65*2^25,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.01*2^25,1.01*2^24,1.01*2^25,1.01*2^24,etc.
 *
 * See fe_mul.c for discussion of implementation strategy.
 */
static void fe_sq(fe h, const fe f)
{
    int32_t f0 = f[0];
    int32_t f1 = f[1];
    int32_t f2 = f[2];
    int32_t f3 = f[3];
    int32_t f4 = f[4];
    int32_t f5 = f[5];
    int32_t f6 = f[6];
    int32_t f7 = f[7];
    int32_t f8 = f[8];
    int32_t f9 = f[9];
    int32_t f0_2 = 2 * f0;
    int32_t f1_2 = 2 * f1;
    int32_t f2_2 = 2 * f2;
    int32_t f3_2 = 2 * f3;
    int32_t f4_2 = 2 * f4;
    int32_t f5_2 = 2 * f5;
    int32_t f6_2 = 2 * f6;
    int32_t f7_2 = 2 * f7;
    int32_t f5_38 = 38 * f5; /* 1.959375*2^30 */
    int32_t f6_19 = 19 * f6; /* 1.959375*2^30 */
    int32_t f7_38 = 38 * f7; /* 1.959375*2^30 */
    int32_t f8_19 = 19 * f8; /* 1.959375*2^30 */
    int32_t f9_38 = 38 * f9; /* 1.959375*2^30 */
    int64_t f0f0    = f0   * (int64_t) f0;
    int64_t f0f1_2  = f0_2 * (int64_t) f1;
    int64_t f0f2_2  = f0_2 * (int64_t) f2;
    int64_t f0f3_2  = f0_2 * (int64_t) f3;
    int64_t f0f4_2  = f0_2 * (int64_t) f4;
    int64_t f0f5_2  = f0_2 * (int64_t) f5;
    int64_t f0f6_2  = f0_2 * (int64_t) f6;
    int64_t f0f7_2  = f0_2 * (int64_t) f7;
    int64_t f0f8_2  = f0_2 * (int64_t) f8;
    int64_t f0f9_2  = f0_2 * (int64_t) f9;
    int64_t f1f1_2  = f1_2 * (int64_t) f1;
    int64_t f1f2_2  = f1_2 * (int64_t) f2;
    int64_t f1f3_4  = f1_2 * (int64_t) f3_2;
    int64_t f1f4_2  = f1_2 * (int64_t) f4;
    int64_t f1f5_4  = f1_2 * (int64_t) f5_2;
    int64_t f1f6_2  = f1_2 * (int64_t) f6;
    int64_t f1f7_4  = f1_2 * (int64_t) f7_2;
    int64_t f1f8_2  = f1_2 * (int64_t) f8;
    int64_t f1f9_76 = f1_2 * (int64_t) f9_38;
    int64_t f2f2    = f2   * (int64_t) f2;
    int64_t f2f3_2  = f2_2 * (int64_t) f3;
    int64_t f2f4_2  = f2_2 * (int64_t) f4;
    int64_t f2f5_2  = f2_2 * (int64_t) f5;
    int64_t f2f6_2  = f2_2 * (int64_t) f6;
    int64_t f2f7_2  = f2_2 * (int64_t) f7;
    int64_t f2f8_38 = f2_2 * (int64_t) f8_19;
    int64_t f2f9_38 = f2   * (int64_t) f9_38;
    int64_t f3f3_2  = f3_2 * (int64_t) f3;
    int64_t f3f4_2  = f3_2 * (int64_t) f4;
    int64_t f3f5_4  = f3_2 * (int64_t) f5_2;
    int64_t f3f6_2  = f3_2 * (int64_t) f6;
    int64_t f3f7_76 = f3_2 * (int64_t) f7_38;
    int64_t f3f8_38 = f3_2 * (int64_t) f8_19;
    int64_t f3f9_76 = f3_2 * (int64_t) f9_38;
    int64_t f4f4    = f4   * (int64_t) f4;
    int64_t f4f5_2  = f4_2 * (int64_t) f5;
    int64_t f4f6_38 = f4_2 * (int64_t) f6_19;
    int64_t f4f7_38 = f4   * (int64_t) f7_38;
    int64_t f4f8_38 = f4_2 * (int64_t) f8_19;
    int64_t f4f9_38 = f4   * (int64_t) f9_38;
    int64_t f5f5_38 = f5   * (int64_t) f5_38;
    int64_t f5f6_38 = f5_2 * (int64_t) f6_19;
    int64_t f5f7_76 = f5_2 * (int64_t) f7_38;
    int64_t f5f8_38 = f5_2 * (int64_t) f8_19;
    int64_t f5f9_76 = f5_2 * (int64_t) f9_38;
    int64_t f6f6_19 = f6   * (int64_t) f6_19;
    int64_t f6f7_38 = f6   * (int64_t) f7_38;
    int64_t f6f8_38 = f6_2 * (int64_t) f8_19;
    int64_t f6f9_38 = f6   * (int64_t) f9_38;
    int64_t f7f7_38 = f7   * (int64_t) f7_38;
    int64_t f7f8_38 = f7_2 * (int64_t) f8_19;
    int64_t f7f9_76 = f7_2 * (int64_t) f9_38;
    int64_t f8f8_19 = f8   * (int64_t) f8_19;
    int64_t f8f9_38 = f8   * (int64_t) f9_38;
    int64_t f9f9_38 = f9   * (int64_t) f9_38;
    int64_t h0 = f0f0   + f1f9_76 + f2f8_38 + f3f7_76 + f4f6_38 + f5f5_38;
    int64_t h1 = f0f1_2 + f2f9_38 + f3f8_38 + f4f7_38 + f5f6_38;
    int64_t h2 = f0f2_2 + f1f1_2  + f3f9_76 + f4f8_38 + f5f7_76 + f6f6_19;
    int64_t h3 = f0f3_2 + f1f2_2  + f4f9_38 + f5f8_38 + f6f7_38;
    int64_t h4 = f0f4_2 + f1f3_4  + f2f2    + f5f9_76 + f6f8_38 + f7f7_38;
    int64_t h5 = f0f5_2 + f1f4_2  + f2f3_2  + f6f9_38 + f7f8_38;
    int64_t h6 = f0f6_2 + f1f5_4  + f2f4_2  + f3f3_2  + f7f9_76 + f8f8_19;
    int64_t h7 = f0f7_2 + f1f6_2  + f2f5_2  + f3f4_2  + f8f9_38;
    int64_t h8 = f0f8_2 + f1f7_4  + f2f6_2  + f3f5_4  + f4f4    + f9f9_38;
    int64_t h9 = f0f9_2 + f1f8_2  + f2f7_2  + f3f6_2  + f4f5_2;
    int64_t carry0;
    int64_t carry1;
    int64_t carry2;
    int64_t carry3;
    int64_t carry4;
    int64_t carry5;
    int64_t carry6;
    int64_t carry7;
    int64_t carry8;
    int64_t carry9;

    carry0 = h0 + (1 << 25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;
    carry4 = h4 + (1 << 25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;

    carry1 = h1 + (1 << 24); h2 += carry1 >> 25; h1 -= carry1 & kTop39Bits;
    carry5 = h5 + (1 << 24); h6 += carry5 >> 25; h5 -= carry5 & kTop39Bits;

    carry2 = h2 + (1 << 25); h3 += carry2 >> 26; h2 -= carry2 & kTop38Bits;
    carry6 = h6 + (1 << 25); h7 += carry6 >> 26; h6 -= carry6 & kTop38Bits;

    carry3 = h3 + (1 << 24); h4 += carry3 >> 25; h3 -= carry3 & kTop39Bits;
    carry7 = h7 + (1 << 24); h8 += carry7 >> 25; h7 -= carry7 & kTop39Bits;

    carry4 = h4 + (1 << 25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;
    carry8 = h8 + (1 << 25); h9 += carry8 >> 26; h8 -= carry8 & kTop38Bits;

    carry9 = h9 + (1 << 24); h0 += (carry9 >> 25) * 19; h9 -= carry9 & kTop39Bits;

    carry0 = h0 + (1 << 25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;

    h[0] = (int32_t)h0;
    h[1] = (int32_t)h1;
    h[2] = (int32_t)h2;
    h[3] = (int32_t)h3;
    h[4] = (int32_t)h4;
    h[5] = (int32_t)h5;
    h[6] = (int32_t)h6;
    h[7] = (int32_t)h7;
    h[8] = (int32_t)h8;
    h[9] = (int32_t)h9;
}

static void fe_invert(fe out, const fe z)
{
    fe t0;
    fe t1;
    fe t2;
    fe t3;
    int i;

    /*
     * Compute z ** -1 = z ** (2 ** 255 - 19 - 2) with the exponent as
     * 2 ** 255 - 21 = (2 ** 5) * (2 ** 250 - 1) + 11.
     */

    /* t0 = z ** 2 */
    fe_sq(t0, z);

    /* t1 = t0 ** (2 ** 2) = z ** 8 */
    fe_sq(t1, t0);
    fe_sq(t1, t1);

    /* t1 = z * t1 = z ** 9 */
    fe_mul(t1, z, t1);
    /* t0 = t0 * t1 = z ** 11 -- stash t0 away for the end. */
    fe_mul(t0, t0, t1);

    /* t2 = t0 ** 2 = z ** 22 */
    fe_sq(t2, t0);

    /* t1 = t1 * t2 = z ** (2 ** 5 - 1) */
    fe_mul(t1, t1, t2);

    /* t2 = t1 ** (2 ** 5) = z ** ((2 ** 5) * (2 ** 5 - 1)) */
    fe_sq(t2, t1);
    for (i = 1; i < 5; ++i) {
        fe_sq(t2, t2);
    }

    /* t1 = t1 * t2 = z ** ((2 ** 5 + 1) * (2 ** 5 - 1)) = z ** (2 ** 10 - 1) */
    fe_mul(t1, t2, t1);

    /* Continuing similarly... */

    /* t2 = z ** (2 ** 20 - 1) */
    fe_sq(t2, t1);
    for (i = 1; i < 10; ++i) {
        fe_sq(t2, t2);
    }
    fe_mul(t2, t2, t1);

    /* t2 = z ** (2 ** 40 - 1) */
    fe_sq(t3, t2);
    for (i = 1; i < 20; ++i) {
        fe_sq(t3, t3);
    }
    fe_mul(t2, t3, t2);

    /* t2 = z ** (2 ** 10) * (2 ** 40 - 1) */
    for (i = 0; i < 10; ++i) {
        fe_sq(t2, t2);
    }
    /* t1 = z ** (2 ** 50 - 1) */
    fe_mul(t1, t2, t1);

    /* t2 = z ** (2 ** 100 - 1) */
    fe_sq(t2, t1);
    for (i = 1; i < 50; ++i) {
        fe_sq(t2, t2);
    }
    fe_mul(t2, t2, t1);

    /* t2 = z ** (2 ** 200 - 1) */
    fe_sq(t3, t2);
    for (i = 1; i < 100; ++i) {
        fe_sq(t3, t3);
    }
    fe_mul(t2, t3, t2);

    /* t2 = z ** ((2 ** 50) * (2 ** 200 - 1) */
    fe_sq(t2, t2);
    for (i = 1; i < 50; ++i) {
        fe_sq(t2, t2);
    }

    /* t1 = z ** (2 ** 250 - 1) */
    fe_mul(t1, t2, t1);

    /* t1 = z ** ((2 ** 5) * (2 ** 250 - 1)) */
    fe_sq(t1, t1);
    for (i = 1; i < 5; ++i) {
        fe_sq(t1, t1);
    }

    /* Recall t0 = z ** 11; out = z ** (2 ** 255 - 21) */
    fe_mul(out, t1, t0);
}

/*
 * h = -f
 *
 * Preconditions:
 *    |f| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc.
 */
static void fe_neg(fe h, const fe f)
{
    unsigned i;

    for (i = 0; i < 10; i++) {
        h[i] = -f[i];
    }
}

/*
 * Replace (f,g) with (g,g) if b == 1;
 * replace (f,g) with (f,g) if b == 0.
 *
 * Preconditions: b in {0,1}.
 */
static void fe_cmov(fe f, const fe g, unsigned b)
{
    size_t i;

    b = 0-b;
    for (i = 0; i < 10; i++) {
        int32_t x = f[i] ^ g[i];
        x &= b;
        f[i] ^= x;
    }
}

/*
 * return 0 if f == 0
 * return 1 if f != 0
 *
 * Preconditions:
 *    |f| bounded by 1.1*2^26,1.1*2^25,1.1*2^26,1.1*2^25,etc.
 */
static int fe_isnonzero(const fe f)
{
    uint8_t s[32];
    static const uint8_t zero[32] = {0};

    fe_tobytes(s, f);

    return CRYPTO_memcmp(s, zero, sizeof(zero)) != 0;
}

/*
 * return 1 if f is in {1,3,5,...,q-2}
 * return 0 if f is in {0,2,4,...,q-1}
 *
 * Preconditions:
 *    |f| bounded by 1.1*2^26,1.1*2^25,1.1*2^26,1.1*2^25,etc.
 */
static int fe_isnegative(const fe f)
{
    uint8_t s[32];

    fe_tobytes(s, f);
    return s[0] & 1;
}

/*
 * h = 2 * f * f
 *
 * Can overlap h with f.
 *
 * Preconditions:
 *    |f| bounded by 1.65*2^26,1.65*2^25,1.65*2^26,1.65*2^25,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.01*2^25,1.01*2^24,1.01*2^25,1.01*2^24,etc.
 *
 * See fe_mul.c for discussion of implementation strategy.
 */
static void fe_sq2(fe h, const fe f)
{
    int32_t f0 = f[0];
    int32_t f1 = f[1];
    int32_t f2 = f[2];
    int32_t f3 = f[3];
    int32_t f4 = f[4];
    int32_t f5 = f[5];
    int32_t f6 = f[6];
    int32_t f7 = f[7];
    int32_t f8 = f[8];
    int32_t f9 = f[9];
    int32_t f0_2 = 2 * f0;
    int32_t f1_2 = 2 * f1;
    int32_t f2_2 = 2 * f2;
    int32_t f3_2 = 2 * f3;
    int32_t f4_2 = 2 * f4;
    int32_t f5_2 = 2 * f5;
    int32_t f6_2 = 2 * f6;
    int32_t f7_2 = 2 * f7;
    int32_t f5_38 = 38 * f5; /* 1.959375*2^30 */
    int32_t f6_19 = 19 * f6; /* 1.959375*2^30 */
    int32_t f7_38 = 38 * f7; /* 1.959375*2^30 */
    int32_t f8_19 = 19 * f8; /* 1.959375*2^30 */
    int32_t f9_38 = 38 * f9; /* 1.959375*2^30 */
    int64_t f0f0    = f0   * (int64_t) f0;
    int64_t f0f1_2  = f0_2 * (int64_t) f1;
    int64_t f0f2_2  = f0_2 * (int64_t) f2;
    int64_t f0f3_2  = f0_2 * (int64_t) f3;
    int64_t f0f4_2  = f0_2 * (int64_t) f4;
    int64_t f0f5_2  = f0_2 * (int64_t) f5;
    int64_t f0f6_2  = f0_2 * (int64_t) f6;
    int64_t f0f7_2  = f0_2 * (int64_t) f7;
    int64_t f0f8_2  = f0_2 * (int64_t) f8;
    int64_t f0f9_2  = f0_2 * (int64_t) f9;
    int64_t f1f1_2  = f1_2 * (int64_t) f1;
    int64_t f1f2_2  = f1_2 * (int64_t) f2;
    int64_t f1f3_4  = f1_2 * (int64_t) f3_2;
    int64_t f1f4_2  = f1_2 * (int64_t) f4;
    int64_t f1f5_4  = f1_2 * (int64_t) f5_2;
    int64_t f1f6_2  = f1_2 * (int64_t) f6;
    int64_t f1f7_4  = f1_2 * (int64_t) f7_2;
    int64_t f1f8_2  = f1_2 * (int64_t) f8;
    int64_t f1f9_76 = f1_2 * (int64_t) f9_38;
    int64_t f2f2    = f2   * (int64_t) f2;
    int64_t f2f3_2  = f2_2 * (int64_t) f3;
    int64_t f2f4_2  = f2_2 * (int64_t) f4;
    int64_t f2f5_2  = f2_2 * (int64_t) f5;
    int64_t f2f6_2  = f2_2 * (int64_t) f6;
    int64_t f2f7_2  = f2_2 * (int64_t) f7;
    int64_t f2f8_38 = f2_2 * (int64_t) f8_19;
    int64_t f2f9_38 = f2   * (int64_t) f9_38;
    int64_t f3f3_2  = f3_2 * (int64_t) f3;
    int64_t f3f4_2  = f3_2 * (int64_t) f4;
    int64_t f3f5_4  = f3_2 * (int64_t) f5_2;
    int64_t f3f6_2  = f3_2 * (int64_t) f6;
    int64_t f3f7_76 = f3_2 * (int64_t) f7_38;
    int64_t f3f8_38 = f3_2 * (int64_t) f8_19;
    int64_t f3f9_76 = f3_2 * (int64_t) f9_38;
    int64_t f4f4    = f4   * (int64_t) f4;
    int64_t f4f5_2  = f4_2 * (int64_t) f5;
    int64_t f4f6_38 = f4_2 * (int64_t) f6_19;
    int64_t f4f7_38 = f4   * (int64_t) f7_38;
    int64_t f4f8_38 = f4_2 * (int64_t) f8_19;
    int64_t f4f9_38 = f4   * (int64_t) f9_38;
    int64_t f5f5_38 = f5   * (int64_t) f5_38;
    int64_t f5f6_38 = f5_2 * (int64_t) f6_19;
    int64_t f5f7_76 = f5_2 * (int64_t) f7_38;
    int64_t f5f8_38 = f5_2 * (int64_t) f8_19;
    int64_t f5f9_76 = f5_2 * (int64_t) f9_38;
    int64_t f6f6_19 = f6   * (int64_t) f6_19;
    int64_t f6f7_38 = f6   * (int64_t) f7_38;
    int64_t f6f8_38 = f6_2 * (int64_t) f8_19;
    int64_t f6f9_38 = f6   * (int64_t) f9_38;
    int64_t f7f7_38 = f7   * (int64_t) f7_38;
    int64_t f7f8_38 = f7_2 * (int64_t) f8_19;
    int64_t f7f9_76 = f7_2 * (int64_t) f9_38;
    int64_t f8f8_19 = f8   * (int64_t) f8_19;
    int64_t f8f9_38 = f8   * (int64_t) f9_38;
    int64_t f9f9_38 = f9   * (int64_t) f9_38;
    int64_t h0 = f0f0   + f1f9_76 + f2f8_38 + f3f7_76 + f4f6_38 + f5f5_38;
    int64_t h1 = f0f1_2 + f2f9_38 + f3f8_38 + f4f7_38 + f5f6_38;
    int64_t h2 = f0f2_2 + f1f1_2  + f3f9_76 + f4f8_38 + f5f7_76 + f6f6_19;
    int64_t h3 = f0f3_2 + f1f2_2  + f4f9_38 + f5f8_38 + f6f7_38;
    int64_t h4 = f0f4_2 + f1f3_4  + f2f2    + f5f9_76 + f6f8_38 + f7f7_38;
    int64_t h5 = f0f5_2 + f1f4_2  + f2f3_2  + f6f9_38 + f7f8_38;
    int64_t h6 = f0f6_2 + f1f5_4  + f2f4_2  + f3f3_2  + f7f9_76 + f8f8_19;
    int64_t h7 = f0f7_2 + f1f6_2  + f2f5_2  + f3f4_2  + f8f9_38;
    int64_t h8 = f0f8_2 + f1f7_4  + f2f6_2  + f3f5_4  + f4f4    + f9f9_38;
    int64_t h9 = f0f9_2 + f1f8_2  + f2f7_2  + f3f6_2  + f4f5_2;
    int64_t carry0;
    int64_t carry1;
    int64_t carry2;
    int64_t carry3;
    int64_t carry4;
    int64_t carry5;
    int64_t carry6;
    int64_t carry7;
    int64_t carry8;
    int64_t carry9;

    h0 += h0;
    h1 += h1;
    h2 += h2;
    h3 += h3;
    h4 += h4;
    h5 += h5;
    h6 += h6;
    h7 += h7;
    h8 += h8;
    h9 += h9;

    carry0 = h0 + (1 << 25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;
    carry4 = h4 + (1 << 25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;

    carry1 = h1 + (1 << 24); h2 += carry1 >> 25; h1 -= carry1 & kTop39Bits;
    carry5 = h5 + (1 << 24); h6 += carry5 >> 25; h5 -= carry5 & kTop39Bits;

    carry2 = h2 + (1 << 25); h3 += carry2 >> 26; h2 -= carry2 & kTop38Bits;
    carry6 = h6 + (1 << 25); h7 += carry6 >> 26; h6 -= carry6 & kTop38Bits;

    carry3 = h3 + (1 << 24); h4 += carry3 >> 25; h3 -= carry3 & kTop39Bits;
    carry7 = h7 + (1 << 24); h8 += carry7 >> 25; h7 -= carry7 & kTop39Bits;

    carry4 = h4 + (1 << 25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;
    carry8 = h8 + (1 << 25); h9 += carry8 >> 26; h8 -= carry8 & kTop38Bits;

    carry9 = h9 + (1 << 24); h0 += (carry9 >> 25) * 19; h9 -= carry9 & kTop39Bits;

    carry0 = h0 + (1 << 25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;

    h[0] = (int32_t)h0;
    h[1] = (int32_t)h1;
    h[2] = (int32_t)h2;
    h[3] = (int32_t)h3;
    h[4] = (int32_t)h4;
    h[5] = (int32_t)h5;
    h[6] = (int32_t)h6;
    h[7] = (int32_t)h7;
    h[8] = (int32_t)h8;
    h[9] = (int32_t)h9;
}

static void fe_pow22523(fe out, const fe z)
{
    fe t0;
    fe t1;
    fe t2;
    int i;

    fe_sq(t0, z);
    fe_sq(t1, t0);
    for (i = 1; i < 2; ++i) {
        fe_sq(t1, t1);
    }
    fe_mul(t1, z, t1);
    fe_mul(t0, t0, t1);
    fe_sq(t0, t0);
    fe_mul(t0, t1, t0);
    fe_sq(t1, t0);
    for (i = 1; i < 5; ++i) {
        fe_sq(t1, t1);
    }
    fe_mul(t0, t1, t0);
    fe_sq(t1, t0);
    for (i = 1; i < 10; ++i) {
        fe_sq(t1, t1);
    }
    fe_mul(t1, t1, t0);
    fe_sq(t2, t1);
    for (i = 1; i < 20; ++i) {
        fe_sq(t2, t2);
    }
    fe_mul(t1, t2, t1);
    fe_sq(t1, t1);
    for (i = 1; i < 10; ++i) {
        fe_sq(t1, t1);
    }
    fe_mul(t0, t1, t0);
    fe_sq(t1, t0);
    for (i = 1; i < 50; ++i) {
        fe_sq(t1, t1);
    }
    fe_mul(t1, t1, t0);
    fe_sq(t2, t1);
    for (i = 1; i < 100; ++i) {
        fe_sq(t2, t2);
    }
    fe_mul(t1, t2, t1);
    fe_sq(t1, t1);
    for (i = 1; i < 50; ++i) {
        fe_sq(t1, t1);
    }
    fe_mul(t0, t1, t0);
    fe_sq(t0, t0);
    for (i = 1; i < 2; ++i) {
        fe_sq(t0, t0);
    }
    fe_mul(out, t0, z);
}

/*
 * ge means group element.
 *
 * Here the group is the set of pairs (x,y) of field elements (see fe.h)
 * satisfying -x^2 + y^2 = 1 + d x^2y^2
 * where d = -121665/121666.
 *
 * Representations:
 *   ge_p2 (projective): (X:Y:Z) satisfying x=X/Z, y=Y/Z
 *   ge_p3 (extended): (X:Y:Z:T) satisfying x=X/Z, y=Y/Z, XY=ZT
 *   ge_p1p1 (completed): ((X:Z),(Y:T)) satisfying x=X/Z, y=Y/T
 *   ge_precomp (Duif): (y+x,y-x,2dxy)
 */
typedef struct {
    fe X;
    fe Y;
    fe Z;
} ge_p2;

typedef struct {
    fe X;
    fe Y;
    fe Z;
    fe T;
} ge_p3;

typedef struct {
    fe X;
    fe Y;
    fe Z;
    fe T;
} ge_p1p1;

typedef struct {
    fe yplusx;
    fe yminusx;
    fe xy2d;
} ge_precomp;

typedef struct {
    fe YplusX;
    fe YminusX;
    fe Z;
    fe T2d;
} ge_cached;

static void ge_tobytes(uint8_t *s, const ge_p2 *h)
{
    fe recip;
    fe x;
    fe y;

    fe_invert(recip, h->Z);
    fe_mul(x, h->X, recip);
    fe_mul(y, h->Y, recip);
    fe_tobytes(s, y);
    s[31] ^= fe_isnegative(x) << 7;
}

static void ge_p3_tobytes(uint8_t *s, const ge_p3 *h)
{
    fe recip;
    fe x;
    fe y;

    fe_invert(recip, h->Z);
    fe_mul(x, h->X, recip);
    fe_mul(y, h->Y, recip);
    fe_tobytes(s, y);
    s[31] ^= fe_isnegative(x) << 7;
}

static const fe d = {
    -10913610, 13857413, -15372611, 6949391,   114729,
    -8787816,  -6275908, -3247719,  -18696448, -12055116
};

static const fe sqrtm1 = {
    -32595792, -7943725,  9377950,  3500415, 12389472,
    -272473,   -25146209, -2005654, 326686,  11406482
};

static int ge_frombytes_vartime(ge_p3 *h, const uint8_t *s)
{
    fe u;
    fe v;
    fe v3;
    fe vxx;
    fe check;

    fe_frombytes(h->Y, s);
    fe_1(h->Z);
    fe_sq(u, h->Y);
    fe_mul(v, u, d);
    fe_sub(u, u, h->Z); /* u = y^2-1 */
    fe_add(v, v, h->Z); /* v = dy^2+1 */

    fe_sq(v3, v);
    fe_mul(v3, v3, v); /* v3 = v^3 */
    fe_sq(h->X, v3);
    fe_mul(h->X, h->X, v);
    fe_mul(h->X, h->X, u); /* x = uv^7 */

    fe_pow22523(h->X, h->X); /* x = (uv^7)^((q-5)/8) */
    fe_mul(h->X, h->X, v3);
    fe_mul(h->X, h->X, u); /* x = uv^3(uv^7)^((q-5)/8) */

    fe_sq(vxx, h->X);
    fe_mul(vxx, vxx, v);
    fe_sub(check, vxx, u); /* vx^2-u */
    if (fe_isnonzero(check)) {
        fe_add(check, vxx, u); /* vx^2+u */
        if (fe_isnonzero(check)) {
            return -1;
        }
        fe_mul(h->X, h->X, sqrtm1);
    }

    if (fe_isnegative(h->X) != (s[31] >> 7)) {
        fe_neg(h->X, h->X);
    }

    fe_mul(h->T, h->X, h->Y);
    return 0;
}

static void ge_p2_0(ge_p2 *h)
{
    fe_0(h->X);
    fe_1(h->Y);
    fe_1(h->Z);
}

static void ge_p3_0(ge_p3 *h)
{
    fe_0(h->X);
    fe_1(h->Y);
    fe_1(h->Z);
    fe_0(h->T);
}

static void ge_precomp_0(ge_precomp *h)
{
    fe_1(h->yplusx);
    fe_1(h->yminusx);
    fe_0(h->xy2d);
}

/* r = p */
static void ge_p3_to_p2(ge_p2 *r, const ge_p3 *p)
{
    fe_copy(r->X, p->X);
    fe_copy(r->Y, p->Y);
    fe_copy(r->Z, p->Z);
}

static const fe d2 = {
    -21827239, -5839606,  -30745221, 13898782, 229458,
    15978800,  -12551817, -6495438,  29715968, 9444199
};

/* r = p */
static void ge_p3_to_cached(ge_cached *r, const ge_p3 *p)
{
    fe_add(r->YplusX, p->Y, p->X);
    fe_sub(r->YminusX, p->Y, p->X);
    fe_copy(r->Z, p->Z);
    fe_mul(r->T2d, p->T, d2);
}

/* r = p */
static void ge_p1p1_to_p2(ge_p2 *r, const ge_p1p1 *p)
{
    fe_mul(r->X, p->X, p->T);
    fe_mul(r->Y, p->Y, p->Z);
    fe_mul(r->Z, p->Z, p->T);
}

/* r = p */
static void ge_p1p1_to_p3(ge_p3 *r, const ge_p1p1 *p)
{
    fe_mul(r->X, p->X, p->T);
    fe_mul(r->Y, p->Y, p->Z);
    fe_mul(r->Z, p->Z, p->T);
    fe_mul(r->T, p->X, p->Y);
}

/* r = 2 * p */
static void ge_p2_dbl(ge_p1p1 *r, const ge_p2 *p)
{
    fe t0;

    fe_sq(r->X, p->X);
    fe_sq(r->Z, p->Y);
    fe_sq2(r->T, p->Z);
    fe_add(r->Y, p->X, p->Y);
    fe_sq(t0, r->Y);
    fe_add(r->Y, r->Z, r->X);
    fe_sub(r->Z, r->Z, r->X);
    fe_sub(r->X, t0, r->Y);
    fe_sub(r->T, r->T, r->Z);
}

/* r = 2 * p */
static void ge_p3_dbl(ge_p1p1 *r, const ge_p3 *p)
{
    ge_p2 q;
    ge_p3_to_p2(&q, p);
    ge_p2_dbl(r, &q);
}

/* r = p + q */
static void ge_madd(ge_p1p1 *r, const ge_p3 *p, const ge_precomp *q)
{
    fe t0;

    fe_add(r->X, p->Y, p->X);
    fe_sub(r->Y, p->Y, p->X);
    fe_mul(r->Z, r->X, q->yplusx);
    fe_mul(r->Y, r->Y, q->yminusx);
    fe_mul(r->T, q->xy2d, p->T);
    fe_add(t0, p->Z, p->Z);
    fe_sub(r->X, r->Z, r->Y);
    fe_add(r->Y, r->Z, r->Y);
    fe_add(r->Z, t0, r->T);
    fe_sub(r->T, t0, r->T);
}

/* r = p - q */
static void ge_msub(ge_p1p1 *r, const ge_p3 *p, const ge_precomp *q)
{
    fe t0;

    fe_add(r->X, p->Y, p->X);
    fe_sub(r->Y, p->Y, p->X);
    fe_mul(r->Z, r->X, q->yminusx);
    fe_mul(r->Y, r->Y, q->yplusx);
    fe_mul(r->T, q->xy2d, p->T);
    fe_add(t0, p->Z, p->Z);
    fe_sub(r->X, r->Z, r->Y);
    fe_add(r->Y, r->Z, r->Y);
    fe_sub(r->Z, t0, r->T);
    fe_add(r->T, t0, r->T);
}

/* r = p + q */
static void ge_add(ge_p1p1 *r, const ge_p3 *p, const ge_cached *q)
{
    fe t0;

    fe_add(r->X, p->Y, p->X);
    fe_sub(r->Y, p->Y, p->X);
    fe_mul(r->Z, r->X, q->YplusX);
    fe_mul(r->Y, r->Y, q->YminusX);
    fe_mul(r->T, q->T2d, p->T);
    fe_mul(r->X, p->Z, q->Z);
    fe_add(t0, r->X, r->X);
    fe_sub(r->X, r->Z, r->Y);
    fe_add(r->Y, r->Z, r->Y);
    fe_add(r->Z, t0, r->T);
    fe_sub(r->T, t0, r->T);
}

/* r = p - q */
static void ge_sub(ge_p1p1 *r, const ge_p3 *p, const ge_cached *q)
{
    fe t0;

    fe_add(r->X, p->Y, p->X);
    fe_sub(r->Y, p->Y, p->X);
    fe_mul(r->Z, r->X, q->YminusX);
    fe_mul(r->Y, r->Y, q->YplusX);
    fe_mul(r->T, q->T2d, p->T);
    fe_mul(r->X, p->Z, q->Z);
    fe_add(t0, r->X, r->X);
    fe_sub(r->X, r->Z, r->Y);
    fe_add(r->Y, r->Z, r->Y);
    fe_sub(r->Z, t0, r->T);
    fe_add(r->T, t0, r->T);
}

static uint8_t equal(signed char b, signed char c)
{
    uint8_t ub = b;
    uint8_t uc = c;
    uint8_t x = ub ^ uc; /* 0: yes; 1..255: no */
    uint32_t y = x;      /* 0: yes; 1..255: no */
    y -= 1;              /* 4294967295: yes; 0..254: no */
    y >>= 31;            /* 1: yes; 0: no */
    return y;
}

static void cmov(ge_precomp *t, const ge_precomp *u, uint8_t b)
{
    fe_cmov(t->yplusx, u->yplusx, b);
    fe_cmov(t->yminusx, u->yminusx, b);
    fe_cmov(t->xy2d, u->xy2d, b);
}

/* k25519Precomp[i][j] = (j+1)*256^i*B */
static const ge_precomp k25519Precomp[32][8] = {
    {
        {
            {25967493, -14356035, 29566456, 3660896, -12694345, 4014787,
             27544626, -11754271, -6079156, 2047605},
            {-12545711, 934262, -2722910, 3049990, -727428, 9406986, 12720692,
             5043384, 19500929, -15469378},
            {-8738181, 4489570, 9688441, -14785194, 10184609, -12363380,
             29287919, 11864899, -24514362, -4438546},
        },
        {
            {-12815894, -12976347, -21581243, 11784320, -25355658, -2750717,
             -11717903, -3814571, -358445, -10211303},
            {-21703237, 6903825, 27185491, 6451973, -29577724, -9554005,
             -15616551, 11189268, -26829678, -5319081},
            {26966642, 11152617, 32442495, 15396054, 14353839, -12752335,
             -3128826, -9541118, -15472047, -4166697},
        },
        {
            {15636291, -9688557, 24204773, -7912398, 616977, -16685262,
             27787600, -14772189, 28944400, -1550024},
            {16568933, 4717097, -11556148, -1102322, 15682896, -11807043,
             16354577, -11775962, 7689662, 11199574},
            {30464156, -5976125, -11779434, -15670865, 23220365, 15915852,
             7512774, 10017326, -17749093, -9920357},
        },
        {
            {-17036878, 13921892, 10945806, -6033431, 27105052, -16084379,
             -28926210, 15006023, 3284568, -6276540},
            {23599295, -8306047, -11193664, -7687416, 13236774, 10506355,
             7464579, 9656445, 13059162, 10374397},
            {7798556, 16710257, 3033922, 2874086, 28997861, 2835604, 32406664,
             -3839045, -641708, -101325},
        },
        {
            {10861363, 11473154, 27284546, 1981175, -30064349, 12577861,
             32867885, 14515107, -15438304, 10819380},
            {4708026, 6336745, 20377586, 9066809, -11272109, 6594696, -25653668,
             12483688, -12668491, 5581306},
            {19563160, 16186464, -29386857, 4097519, 10237984, -4348115,
             28542350, 13850243, -23678021, -15815942},
        },
        {
            {-15371964, -12862754, 32573250, 4720197, -26436522, 5875511,
             -19188627, -15224819, -9818940, -12085777},
            {-8549212, 109983, 15149363, 2178705, 22900618, 4543417, 3044240,
             -15689887, 1762328, 14866737},
            {-18199695, -15951423, -10473290, 1707278, -17185920, 3916101,
             -28236412, 3959421, 27914454, 4383652},
        },
        {
            {5153746, 9909285, 1723747, -2777874, 30523605, 5516873, 19480852,
             5230134, -23952439, -15175766},
            {-30269007, -3463509, 7665486, 10083793, 28475525, 1649722,
             20654025, 16520125, 30598449, 7715701},
            {28881845, 14381568, 9657904, 3680757, -20181635, 7843316,
             -31400660, 1370708, 29794553, -1409300},
        },
        {
            {14499471, -2729599, -33191113, -4254652, 28494862, 14271267,
             30290735, 10876454, -33154098, 2381726},
            {-7195431, -2655363, -14730155, 462251, -27724326, 3941372,
             -6236617, 3696005, -32300832, 15351955},
            {27431194, 8222322, 16448760, -3907995, -18707002, 11938355,
             -32961401, -2970515, 29551813, 10109425},
        },
    },
    {
        {
            {-13657040, -13155431, -31283750, 11777098, 21447386, 6519384,
             -2378284, -1627556, 10092783, -4764171},
            {27939166, 14210322, 4677035, 16277044, -22964462, -12398139,
             -32508754, 12005538, -17810127, 12803510},
            {17228999, -15661624, -1233527, 300140, -1224870, -11714777,
             30364213, -9038194, 18016357, 4397660},
        },
        {
            {-10958843, -7690207, 4776341, -14954238, 27850028, -15602212,
             -26619106, 14544525, -17477504, 982639},
            {29253598, 15796703, -2863982, -9908884, 10057023, 3163536, 7332899,
             -4120128, -21047696, 9934963},
            {5793303, 16271923, -24131614, -10116404, 29188560, 1206517,
             -14747930, 4559895, -30123922, -10897950},
        },
        {
            {-27643952, -11493006, 16282657, -11036493, 28414021, -15012264,
             24191034, 4541697, -13338309, 5500568},
            {12650548, -1497113, 9052871, 11355358, -17680037, -8400164,
             -17430592, 12264343, 10874051, 13524335},
            {25556948, -3045990, 714651, 2510400, 23394682, -10415330, 33119038,
             5080568, -22528059, 5376628},
        },
        {
            {-26088264, -4011052, -17013699, -3537628, -6726793, 1920897,
             -22321305, -9447443, 4535768, 1569007},
            {-2255422, 14606630, -21692440, -8039818, 28430649, 8775819,
             -30494562, 3044290, 31848280, 12543772},
            {-22028579, 2943893, -31857513, 6777306, 13784462, -4292203,
             -27377195, -2062731, 7718482, 14474653},
        },
        {
            {2385315, 2454213, -22631320, 46603, -4437935, -15680415, 656965,
             -7236665, 24316168, -5253567},
            {13741529, 10911568, -33233417, -8603737, -20177830, -1033297,
             33040651, -13424532, -20729456, 8321686},
            {21060490, -2212744, 15712757, -4336099, 1639040, 10656336,
             23845965, -11874838, -9984458, 608372},
        },
        {
            {-13672732, -15087586, -10889693, -7557059, -6036909, 11305547,
             1123968, -6780577, 27229399, 23887},
            {-23244140, -294205, -11744728, 14712571, -29465699, -2029617,
             12797024, -6440308, -1633405, 16678954},
            {-29500620, 4770662, -16054387, 14001338, 7830047, 9564805,
             -1508144, -4795045, -17169265, 4904953},
        },
        {
            {24059557, 14617003, 19037157, -15039908, 19766093, -14906429,
             5169211, 16191880, 2128236, -4326833},
            {-16981152, 4124966, -8540610, -10653797, 30336522, -14105247,
             -29806336, 916033, -6882542, -2986532},
            {-22630907, 12419372, -7134229, -7473371, -16478904, 16739175,
             285431, 2763829, 15736322, 4143876},
        },
        {
            {2379352, 11839345, -4110402, -5988665, 11274298, 794957, 212801,
             -14594663, 23527084, -16458268},
            {33431127, -11130478, -17838966, -15626900, 8909499, 8376530,
             -32625340, 4087881, -15188911, -14416214},
            {1767683, 7197987, -13205226, -2022635, -13091350, 448826, 5799055,
             4357868, -4774191, -16323038},
        },
    },
    {
        {
            {6721966, 13833823, -23523388, -1551314, 26354293, -11863321,
             23365147, -3949732, 7390890, 2759800},
            {4409041, 2052381, 23373853, 10530217, 7676779, -12885954, 21302353,
             -4264057, 1244380, -12919645},
            {-4421239, 7169619, 4982368, -2957590, 30256825, -2777540, 14086413,
             9208236, 15886429, 16489664},
        },
        {
            {1996075, 10375649, 14346367, 13311202, -6874135, -16438411,
             -13693198, 398369, -30606455, -712933},
            {-25307465, 9795880, -2777414, 14878809, -33531835, 14780363,
             13348553, 12076947, -30836462, 5113182},
            {-17770784, 11797796, 31950843, 13929123, -25888302, 12288344,
             -30341101, -7336386, 13847711, 5387222},
        },
        {
            {-18582163, -3416217, 17824843, -2340966, 22744343, -10442611,
             8763061, 3617786, -19600662, 10370991},
            {20246567, -14369378, 22358229, -543712, 18507283, -10413996,
             14554437, -8746092, 32232924, 16763880},
            {9648505, 10094563, 26416693, 14745928, -30374318, -6472621,
             11094161, 15689506, 3140038, -16510092},
        },
        {
            {-16160072, 5472695, 31895588, 4744994, 8823515, 10365685,
             -27224800, 9448613, -28774454, 366295},
            {19153450, 11523972, -11096490, -6503142, -24647631, 5420647,
             28344573, 8041113, 719605, 11671788},
            {8678025, 2694440, -6808014, 2517372, 4964326, 11152271, -15432916,
             -15266516, 27000813, -10195553},
        },
        {
            {-15157904, 7134312, 8639287, -2814877, -7235688, 10421742, 564065,
             5336097, 6750977, -14521026},
            {11836410, -3979488, 26297894, 16080799, 23455045, 15735944,
             1695823, -8819122, 8169720, 16220347},
            {-18115838, 8653647, 17578566, -6092619, -8025777, -16012763,
             -11144307, -2627664, -5990708, -14166033},
        },
        {
            {-23308498, -10968312, 15213228, -10081214, -30853605, -11050004,
             27884329, 2847284, 2655861, 1738395},
            {-27537433, -14253021, -25336301, -8002780, -9370762, 8129821,
             21651608, -3239336, -19087449, -11005278},
            {1533110, 3437855, 23735889, 459276, 29970501, 11335377, 26030092,
             5821408, 10478196, 8544890},
        },
        {
            {32173121, -16129311, 24896207, 3921497, 22579056, -3410854,
             19270449, 12217473, 17789017, -3395995},
            {-30552961, -2228401, -15578829, -10147201, 13243889, 517024,
             15479401, -3853233, 30460520, 1052596},
            {-11614875, 13323618, 32618793, 8175907, -15230173, 12596687,
             27491595, -4612359, 3179268, -9478891},
        },
        {
            {31947069, -14366651, -4640583, -15339921, -15125977, -6039709,
             -14756777, -16411740, 19072640, -9511060},
            {11685058, 11822410, 3158003, -13952594, 33402194, -4165066,
             5977896, -5215017, 473099, 5040608},
            {-20290863, 8198642, -27410132, 11602123, 1290375, -2799760,
             28326862, 1721092, -19558642, -3131606},
        },
    },
    {
        {
            {7881532, 10687937, 7578723, 7738378, -18951012, -2553952, 21820786,
             8076149, -27868496, 11538389},
            {-19935666, 3899861, 18283497, -6801568, -15728660, -11249211,
             8754525, 7446702, -5676054, 5797016},
            {-11295600, -3793569, -15782110, -7964573, 12708869, -8456199,
             2014099, -9050574, -2369172, -5877341},
        },
        {
            {-22472376, -11568741, -27682020, 1146375, 18956691, 16640559,
             1192730, -3714199, 15123619, 10811505},
            {14352098, -3419715, -18942044, 10822655, 32750596, 4699007, -70363,
             15776356, -28886779, -11974553},
            {-28241164, -8072475, -4978962, -5315317, 29416931, 1847569,
             -20654173, -16484855, 4714547, -9600655},
        },
        {
            {15200332, 8368572, 19679101, 15970074, -31872674, 1959451,
             24611599, -4543832, -11745876, 12340220},
            {12876937, -10480056, 33134381, 6590940, -6307776, 14872440,
             9613953, 8241152, 15370987, 9608631},
            {-4143277, -12014408, 8446281, -391603, 4407738, 13629032, -7724868,
             15866074, -28210621, -8814099},
        },
        {
            {26660628, -15677655, 8393734, 358047, -7401291, 992988, -23904233,
             858697, 20571223, 8420556},
            {14620715, 13067227, -15447274, 8264467, 14106269, 15080814,
             33531827, 12516406, -21574435, -12476749},
            {236881, 10476226, 57258, -14677024, 6472998, 2466984, 17258519,
             7256740, 8791136, 15069930},
        },
        {
            {1276410, -9371918, 22949635, -16322807, -23493039, -5702186,
             14711875, 4874229, -30663140, -2331391},
            {5855666, 4990204, -13711848, 7294284, -7804282, 1924647, -1423175,
             -7912378, -33069337, 9234253},
            {20590503, -9018988, 31529744, -7352666, -2706834, 10650548,
             31559055, -11609587, 18979186, 13396066},
        },
        {
            {24474287, 4968103, 22267082, 4407354, 24063882, -8325180,
             -18816887, 13594782, 33514650, 7021958},
            {-11566906, -6565505, -21365085, 15928892, -26158305, 4315421,
             -25948728, -3916677, -21480480, 12868082},
            {-28635013, 13504661, 19988037, -2132761, 21078225, 6443208,
             -21446107, 2244500, -12455797, -8089383},
        },
        {
            {-30595528, 13793479, -5852820, 319136, -25723172, -6263899,
             33086546, 8957937, -15233648, 5540521},
            {-11630176, -11503902, -8119500, -7643073, 2620056, 1022908,
             -23710744, -1568984, -16128528, -14962807},
            {23152971, 775386, 27395463, 14006635, -9701118, 4649512, 1689819,
             892185, -11513277, -15205948},
        },
        {
            {9770129, 9586738, 26496094, 4324120, 1556511, -3550024, 27453819,
             4763127, -19179614, 5867134},
            {-32765025, 1927590, 31726409, -4753295, 23962434, -16019500,
             27846559, 5931263, -29749703, -16108455},
            {27461885, -2977536, 22380810, 1815854, -23033753, -3031938,
             7283490, -15148073, -19526700, 7734629},
        },
    },
    {
        {
            {-8010264, -9590817, -11120403, 6196038, 29344158, -13430885,
             7585295, -3176626, 18549497, 15302069},
            {-32658337, -6171222, -7672793, -11051681, 6258878, 13504381,
             10458790, -6418461, -8872242, 8424746},
            {24687205, 8613276, -30667046, -3233545, 1863892, -1830544,
             19206234, 7134917, -11284482, -828919},
        },
        {
            {11334899, -9218022, 8025293, 12707519, 17523892, -10476071,
             10243738, -14685461, -5066034, 16498837},
            {8911542, 6887158, -9584260, -6958590, 11145641, -9543680, 17303925,
             -14124238, 6536641, 10543906},
            {-28946384, 15479763, -17466835, 568876, -1497683, 11223454,
             -2669190, -16625574, -27235709, 8876771},
        },
        {
            {-25742899, -12566864, -15649966, -846607, -33026686, -796288,
             -33481822, 15824474, -604426, -9039817},
            {10330056, 70051, 7957388, -9002667, 9764902, 15609756, 27698697,
             -4890037, 1657394, 3084098},
            {10477963, -7470260, 12119566, -13250805, 29016247, -5365589,
             31280319, 14396151, -30233575, 15272409},
        },
        {
            {-12288309, 3169463, 28813183, 16658753, 25116432, -5630466,
             -25173957, -12636138, -25014757, 1950504},
            {-26180358, 9489187, 11053416, -14746161, -31053720, 5825630,
             -8384306, -8767532, 15341279, 8373727},
            {28685821, 7759505, -14378516, -12002860, -31971820, 4079242,
             298136, -10232602, -2878207, 15190420},
        },
        {
            {-32932876, 13806336, -14337485, -15794431, -24004620, 10940928,
             8669718, 2742393, -26033313, -6875003},
            {-1580388, -11729417, -25979658, -11445023, -17411874, -10912854,
             9291594, -16247779, -12154742, 6048605},
            {-30305315, 14843444, 1539301, 11864366, 20201677, 1900163,
             13934231, 5128323, 11213262, 9168384},
        },
        {
            {-26280513, 11007847, 19408960, -940758, -18592965, -4328580,
             -5088060, -11105150, 20470157, -16398701},
            {-23136053, 9282192, 14855179, -15390078, -7362815, -14408560,
             -22783952, 14461608, 14042978, 5230683},
            {29969567, -2741594, -16711867, -8552442, 9175486, -2468974,
             21556951, 3506042, -5933891, -12449708},
        },
        {
            {-3144746, 8744661, 19704003, 4581278, -20430686, 6830683,
             -21284170, 8971513, -28539189, 15326563},
            {-19464629, 10110288, -17262528, -3503892, -23500387, 1355669,
             -15523050, 15300988, -20514118, 9168260},
            {-5353335, 4488613, -23803248, 16314347, 7780487, -15638939,
             -28948358, 9601605, 33087103, -9011387},
        },
        {
            {-19443170, -15512900, -20797467, -12445323, -29824447, 10229461,
             -27444329, -15000531, -5996870, 15664672},
            {23294591, -16632613, -22650781, -8470978, 27844204, 11461195,
             13099750, -2460356, 18151676, 13417686},
            {-24722913, -4176517, -31150679, 5988919, -26858785, 6685065,
             1661597, -12551441, 15271676, -15452665},
        },
    },
    {
        {
            {11433042, -13228665, 8239631, -5279517, -1985436, -725718,
             -18698764, 2167544, -6921301, -13440182},
            {-31436171, 15575146, 30436815, 12192228, -22463353, 9395379,
             -9917708, -8638997, 12215110, 12028277},
            {14098400, 6555944, 23007258, 5757252, -15427832, -12950502,
             30123440, 4617780, -16900089, -655628},
        },
        {
            {-4026201, -15240835, 11893168, 13718664, -14809462, 1847385,
             -15819999, 10154009, 23973261, -12684474},
            {-26531820, -3695990, -1908898, 2534301, -31870557, -16550355,
             18341390, -11419951, 32013174, -10103539},
            {-25479301, 10876443, -11771086, -14625140, -12369567, 1838104,
             21911214, 6354752, 4425632, -837822},
        },
        {
            {-10433389, -14612966, 22229858, -3091047, -13191166, 776729,
             -17415375, -12020462, 4725005, 14044970},
            {19268650, -7304421, 1555349, 8692754, -21474059, -9910664, 6347390,
             -1411784, -19522291, -16109756},
            {-24864089, 12986008, -10898878, -5558584, -11312371, -148526,
             19541418, 8180106, 9282262, 10282508},
        },
        {
            {-26205082, 4428547, -8661196, -13194263, 4098402, -14165257,
             15522535, 8372215, 5542595, -10702683},
            {-10562541, 14895633, 26814552, -16673850, -17480754, -2489360,
             -2781891, 6993761, -18093885, 10114655},
            {-20107055, -929418, 31422704, 10427861, -7110749, 6150669,
             -29091755, -11529146, 25953725, -106158},
        },
        {
            {-4234397, -8039292, -9119125, 3046000, 2101609, -12607294,
             19390020, 6094296, -3315279, 12831125},
            {-15998678, 7578152, 5310217, 14408357, -33548620, -224739,
             31575954, 6326196, 7381791, -2421839},
            {-20902779, 3296811, 24736065, -16328389, 18374254, 7318640,
             6295303, 8082724, -15362489, 12339664},
        },
        {
            {27724736, 2291157, 6088201, -14184798, 1792727, 5857634, 13848414,
             15768922, 25091167, 14856294},
            {-18866652, 8331043, 24373479, 8541013, -701998, -9269457, 12927300,
             -12695493, -22182473, -9012899},
            {-11423429, -5421590, 11632845, 3405020, 30536730, -11674039,
             -27260765, 13866390, 30146206, 9142070},
        },
        {
            {3924129, -15307516, -13817122, -10054960, 12291820, -668366,
             -27702774, 9326384, -8237858, 4171294},
            {-15921940, 16037937, 6713787, 16606682, -21612135, 2790944,
             26396185, 3731949, 345228, -5462949},
            {-21327538, 13448259, 25284571, 1143661, 20614966, -8849387,
             2031539, -12391231, -16253183, -13582083},
        },
        {
            {31016211, -16722429, 26371392, -14451233, -5027349, 14854137,
             17477601, 3842657, 28012650, -16405420},
            {-5075835, 9368966, -8562079, -4600902, -15249953, 6970560,
             -9189873, 16292057, -8867157, 3507940},
            {29439664, 3537914, 23333589, 6997794, -17555561, -11018068,
             -15209202, -15051267, -9164929, 6580396},
        },
    },
    {
        {
            {-12185861, -7679788, 16438269, 10826160, -8696817, -6235611,
             17860444, -9273846, -2095802, 9304567},
            {20714564, -4336911, 29088195, 7406487, 11426967, -5095705,
             14792667, -14608617, 5289421, -477127},
            {-16665533, -10650790, -6160345, -13305760, 9192020, -1802462,
             17271490, 12349094, 26939669, -3752294},
        },
        {
            {-12889898, 9373458, 31595848, 16374215, 21471720, 13221525,
             -27283495, -12348559, -3698806, 117887},
            {22263325, -6560050, 3984570, -11174646, -15114008, -566785,
             28311253, 5358056, -23319780, 541964},
            {16259219, 3261970, 2309254, -15534474, -16885711, -4581916,
             24134070, -16705829, -13337066, -13552195},
        },
        {
            {9378160, -13140186, -22845982, -12745264, 28198281, -7244098,
             -2399684, -717351, 690426, 14876244},
            {24977353, -314384, -8223969, -13465086, 28432343, -1176353,
             -13068804, -12297348, -22380984, 6618999},
            {-1538174, 11685646, 12944378, 13682314, -24389511, -14413193,
             8044829, -13817328, 32239829, -5652762},
        },
        {
            {-18603066, 4762990, -926250, 8885304, -28412480, -3187315, 9781647,
             -10350059, 32779359, 5095274},
            {-33008130, -5214506, -32264887, -3685216, 9460461, -9327423,
             -24601656, 14506724, 21639561, -2630236},
            {-16400943, -13112215, 25239338, 15531969, 3987758, -4499318,
             -1289502, -6863535, 17874574, 558605},
        },
        {
            {-13600129, 10240081, 9171883, 16131053, -20869254, 9599700,
             33499487, 5080151, 2085892, 5119761},
            {-22205145, -2519528, -16381601, 414691, -25019550, 2170430,
             30634760, -8363614, -31999993, -5759884},
            {-6845704, 15791202, 8550074, -1312654, 29928809, -12092256,
             27534430, -7192145, -22351378, 12961482},
        },
        {
            {-24492060, -9570771, 10368194, 11582341, -23397293, -2245287,
             16533930, 8206996, -30194652, -5159638},
            {-11121496, -3382234, 2307366, 6362031, -135455, 8868177, -16835630,
             7031275, 7589640, 8945490},
            {-32152748, 8917967, 6661220, -11677616, -1192060, -15793393,
             7251489, -11182180, 24099109, -14456170},
        },
        {
            {5019558, -7907470, 4244127, -14714356, -26933272, 6453165,
             -19118182, -13289025, -6231896, -10280736},
            {10853594, 10721687, 26480089, 5861829, -22995819, 1972175,
             -1866647, -10557898, -3363451, -6441124},
            {-17002408, 5906790, 221599, -6563147, 7828208, -13248918, 24362661,
             -2008168, -13866408, 7421392},
        },
        {
            {8139927, -6546497, 32257646, -5890546, 30375719, 1886181,
             -21175108, 15441252, 28826358, -4123029},
            {6267086, 9695052, 7709135, -16603597, -32869068, -1886135,
             14795160, -7840124, 13746021, -1742048},
            {28584902, 7787108, -6732942, -15050729, 22846041, -7571236,
             -3181936, -363524, 4771362, -8419958},
        },
    },
    {
        {
            {24949256, 6376279, -27466481, -8174608, -18646154, -9930606,
             33543569, -12141695, 3569627, 11342593},
            {26514989, 4740088, 27912651, 3697550, 19331575, -11472339, 6809886,
             4608608, 7325975, -14801071},
            {-11618399, -14554430, -24321212, 7655128, -1369274, 5214312,
             -27400540, 10258390, -17646694, -8186692},
        },
        {
            {11431204, 15823007, 26570245, 14329124, 18029990, 4796082,
             -31446179, 15580664, 9280358, -3973687},
            {-160783, -10326257, -22855316, -4304997, -20861367, -13621002,
             -32810901, -11181622, -15545091, 4387441},
            {-20799378, 12194512, 3937617, -5805892, -27154820, 9340370,
             -24513992, 8548137, 20617071, -7482001},
        },
        {
            {-938825, -3930586, -8714311, 16124718, 24603125, -6225393,
             -13775352, -11875822, 24345683, 10325460},
            {-19855277, -1568885, -22202708, 8714034, 14007766, 6928528,
             16318175, -1010689, 4766743, 3552007},
            {-21751364, -16730916, 1351763, -803421, -4009670, 3950935, 3217514,
             14481909, 10988822, -3994762},
        },
        {
            {15564307, -14311570, 3101243, 5684148, 30446780, -8051356,
             12677127, -6505343, -8295852, 13296005},
            {-9442290, 6624296, -30298964, -11913677, -4670981, -2057379,
             31521204, 9614054, -30000824, 12074674},
            {4771191, -135239, 14290749, -13089852, 27992298, 14998318,
             -1413936, -1556716, 29832613, -16391035},
        },
        {
            {7064884, -7541174, -19161962, -5067537, -18891269, -2912736,
             25825242, 5293297, -27122660, 13101590},
            {-2298563, 2439670, -7466610, 1719965, -27267541, -16328445,
             32512469, -5317593, -30356070, -4190957},
            {-30006540, 10162316, -33180176, 3981723, -16482138, -13070044,
             14413974, 9515896, 19568978, 9628812},
        },
        {
            {33053803, 199357, 15894591, 1583059, 27380243, -4580435, -17838894,
             -6106839, -6291786, 3437740},
            {-18978877, 3884493, 19469877, 12726490, 15913552, 13614290,
             -22961733, 70104, 7463304, 4176122},
            {-27124001, 10659917, 11482427, -16070381, 12771467, -6635117,
             -32719404, -5322751, 24216882, 5944158},
        },
        {
            {8894125, 7450974, -2664149, -9765752, -28080517, -12389115,
             19345746, 14680796, 11632993, 5847885},
            {26942781, -2315317, 9129564, -4906607, 26024105, 11769399,
             -11518837, 6367194, -9727230, 4782140},
            {19916461, -4828410, -22910704, -11414391, 25606324, -5972441,
             33253853, 8220911, 6358847, -1873857},
        },
        {
            {801428, -2081702, 16569428, 11065167, 29875704, 96627, 7908388,
             -4480480, -13538503, 1387155},
            {19646058, 5720633, -11416706, 12814209, 11607948, 12749789,
             14147075, 15156355, -21866831, 11835260},
            {19299512, 1155910, 28703737, 14890794, 2925026, 7269399, 26121523,
             15467869, -26560550, 5052483},
        },
    },
    {
        {
            {-3017432, 10058206, 1980837, 3964243, 22160966, 12322533, -6431123,
             -12618185, 12228557, -7003677},
            {32944382, 14922211, -22844894, 5188528, 21913450, -8719943,
             4001465, 13238564, -6114803, 8653815},
            {22865569, -4652735, 27603668, -12545395, 14348958, 8234005,
             24808405, 5719875, 28483275, 2841751},
        },
        {
            {-16420968, -1113305, -327719, -12107856, 21886282, -15552774,
             -1887966, -315658, 19932058, -12739203},
            {-11656086, 10087521, -8864888, -5536143, -19278573, -3055912,
             3999228, 13239134, -4777469, -13910208},
            {1382174, -11694719, 17266790, 9194690, -13324356, 9720081,
             20403944, 11284705, -14013818, 3093230},
        },
        {
            {16650921, -11037932, -1064178, 1570629, -8329746, 7352753, -302424,
             16271225, -24049421, -6691850},
            {-21911077, -5927941, -4611316, -5560156, -31744103, -10785293,
             24123614, 15193618, -21652117, -16739389},
            {-9935934, -4289447, -25279823, 4372842, 2087473, 10399484,
             31870908, 14690798, 17361620, 11864968},
        },
        {
            {-11307610, 6210372, 13206574, 5806320, -29017692, -13967200,
             -12331205, -7486601, -25578460, -16240689},
            {14668462, -12270235, 26039039, 15305210, 25515617, 4542480,
             10453892, 6577524, 9145645, -6443880},
            {5974874, 3053895, -9433049, -10385191, -31865124, 3225009,
             -7972642, 3936128, -5652273, -3050304},
        },
        {
            {30625386, -4729400, -25555961, -12792866, -20484575, 7695099,
             17097188, -16303496, -27999779, 1803632},
            {-3553091, 9865099, -5228566, 4272701, -5673832, -16689700,
             14911344, 12196514, -21405489, 7047412},
            {20093277, 9920966, -11138194, -5343857, 13161587, 12044805,
             -32856851, 4124601, -32343828, -10257566},
        },
        {
            {-20788824, 14084654, -13531713, 7842147, 19119038, -13822605,
             4752377, -8714640, -21679658, 2288038},
            {-26819236, -3283715, 29965059, 3039786, -14473765, 2540457,
             29457502, 14625692, -24819617, 12570232},
            {-1063558, -11551823, 16920318, 12494842, 1278292, -5869109,
             -21159943, -3498680, -11974704, 4724943},
        },
        {
            {17960970, -11775534, -4140968, -9702530, -8876562, -1410617,
             -12907383, -8659932, -29576300, 1903856},
            {23134274, -14279132, -10681997, -1611936, 20684485, 15770816,
             -12989750, 3190296, 26955097, 14109738},
            {15308788, 5320727, -30113809, -14318877, 22902008, 7767164,
             29425325, -11277562, 31960942, 11934971},
        },
        {
            {-27395711, 8435796, 4109644, 12222639, -24627868, 14818669,
             20638173, 4875028, 10491392, 1379718},
            {-13159415, 9197841, 3875503, -8936108, -1383712, -5879801,
             33518459, 16176658, 21432314, 12180697},
            {-11787308, 11500838, 13787581, -13832590, -22430679, 10140205,
             1465425, 12689540, -10301319, -13872883},
        },
    },
    {
        {
            {5414091, -15386041, -21007664, 9643570, 12834970, 1186149,
             -2622916, -1342231, 26128231, 6032912},
            {-26337395, -13766162, 32496025, -13653919, 17847801, -12669156,
             3604025, 8316894, -25875034, -10437358},
            {3296484, 6223048, 24680646, -12246460, -23052020, 5903205,
             -8862297, -4639164, 12376617, 3188849},
        },
        {
            {29190488, -14659046, 27549113, -1183516, 3520066, -10697301,
             32049515, -7309113, -16109234, -9852307},
            {-14744486, -9309156, 735818, -598978, -20407687, -5057904,
             25246078, -15795669, 18640741, -960977},
            {-6928835, -16430795, 10361374, 5642961, 4910474, 12345252,
             -31638386, -494430, 10530747, 1053335},
        },
        {
            {-29265967, -14186805, -13538216, -12117373, -19457059, -10655384,
             -31462369, -2948985, 24018831, 15026644},
            {-22592535, -3145277, -2289276, 5953843, -13440189, 9425631,
             25310643, 13003497, -2314791, -15145616},
            {-27419985, -603321, -8043984, -1669117, -26092265, 13987819,
             -27297622, 187899, -23166419, -2531735},
        },
        {
            {-21744398, -13810475, 1844840, 5021428, -10434399, -15911473,
             9716667, 16266922, -5070217, 726099},
            {29370922, -6053998, 7334071, -15342259, 9385287, 2247707,
             -13661962, -4839461, 30007388, -15823341},
            {-936379, 16086691, 23751945, -543318, -1167538, -5189036, 9137109,
             730663, 9835848, 4555336},
        },
        {
            {-23376435, 1410446, -22253753, -12899614, 30867635, 15826977,
             17693930, 544696, -11985298, 12422646},
            {31117226, -12215734, -13502838, 6561947, -9876867, -12757670,
             -5118685, -4096706, 29120153, 13924425},
            {-17400879, -14233209, 19675799, -2734756, -11006962, -5858820,
             -9383939, -11317700, 7240931, -237388},
        },
        {
            {-31361739, -11346780, -15007447, -5856218, -22453340, -12152771,
             1222336, 4389483, 3293637, -15551743},
            {-16684801, -14444245, 11038544, 11054958, -13801175, -3338533,
             -24319580, 7733547, 12796905, -6335822},
            {-8759414, -10817836, -25418864, 10783769, -30615557, -9746811,
             -28253339, 3647836, 3222231, -11160462},
        },
        {
            {18606113, 1693100, -25448386, -15170272, 4112353, 10045021,
             23603893, -2048234, -7550776, 2484985},
            {9255317, -3131197, -12156162, -1004256, 13098013, -9214866,
             16377220, -2102812, -19802075, -3034702},
            {-22729289, 7496160, -5742199, 11329249, 19991973, -3347502,
             -31718148, 9936966, -30097688, -10618797},
        },
        {
            {21878590, -5001297, 4338336, 13643897, -3036865, 13160960,
             19708896, 5415497, -7360503, -4109293},
            {27736861, 10103576, 12500508, 8502413, -3413016, -9633558,
             10436918, -1550276, -23659143, -8132100},
            {19492550, -12104365, -29681976, -852630, -3208171, 12403437,
             30066266, 8367329, 13243957, 8709688},
        },
    },
    {
        {
            {12015105, 2801261, 28198131, 10151021, 24818120, -4743133,
             -11194191, -5645734, 5150968, 7274186},
            {2831366, -12492146, 1478975, 6122054, 23825128, -12733586,
             31097299, 6083058, 31021603, -9793610},
            {-2529932, -2229646, 445613, 10720828, -13849527, -11505937,
             -23507731, 16354465, 15067285, -14147707},
        },
        {
            {7840942, 14037873, -33364863, 15934016, -728213, -3642706,
             21403988, 1057586, -19379462, -12403220},
            {915865, -16469274, 15608285, -8789130, -24357026, 6060030,
             -17371319, 8410997, -7220461, 16527025},
            {32922597, -556987, 20336074, -16184568, 10903705, -5384487,
             16957574, 52992, 23834301, 6588044},
        },
        {
            {32752030, 11232950, 3381995, -8714866, 22652988, -10744103,
             17159699, 16689107, -20314580, -1305992},
            {-4689649, 9166776, -25710296, -10847306, 11576752, 12733943,
             7924251, -2752281, 1976123, -7249027},
            {21251222, 16309901, -2983015, -6783122, 30810597, 12967303, 156041,
             -3371252, 12331345, -8237197},
        },
        {
            {8651614, -4477032, -16085636, -4996994, 13002507, 2950805,
             29054427, -5106970, 10008136, -4667901},
            {31486080, 15114593, -14261250, 12951354, 14369431, -7387845,
             16347321, -13662089, 8684155, -10532952},
            {19443825, 11385320, 24468943, -9659068, -23919258, 2187569,
             -26263207, -6086921, 31316348, 14219878},
        },
        {
            {-28594490, 1193785, 32245219, 11392485, 31092169, 15722801,
             27146014, 6992409, 29126555, 9207390},
            {32382935, 1110093, 18477781, 11028262, -27411763, -7548111,
             -4980517, 10843782, -7957600, -14435730},
            {2814918, 7836403, 27519878, -7868156, -20894015, -11553689,
             -21494559, 8550130, 28346258, 1994730},
        },
        {
            {-19578299, 8085545, -14000519, -3948622, 2785838, -16231307,
             -19516951, 7174894, 22628102, 8115180},
            {-30405132, 955511, -11133838, -15078069, -32447087, -13278079,
             -25651578, 3317160, -9943017, 930272},
            {-15303681, -6833769, 28856490, 1357446, 23421993, 1057177,
             24091212, -1388970, -22765376, -10650715},
        },
        {
            {-22751231, -5303997, -12907607, -12768866, -15811511, -7797053,
             -14839018, -16554220, -1867018, 8398970},
            {-31969310, 2106403, -4736360, 1362501, 12813763, 16200670,
             22981545, -6291273, 18009408, -15772772},
            {-17220923, -9545221, -27784654, 14166835, 29815394, 7444469,
             29551787, -3727419, 19288549, 1325865},
        },
        {
            {15100157, -15835752, -23923978, -1005098, -26450192, 15509408,
             12376730, -3479146, 33166107, -8042750},
            {20909231, 13023121, -9209752, 16251778, -5778415, -8094914,
             12412151, 10018715, 2213263, -13878373},
            {32529814, -11074689, 30361439, -16689753, -9135940, 1513226,
             22922121, 6382134, -5766928, 8371348},
        },
    },
    {
        {
            {9923462, 11271500, 12616794, 3544722, -29998368, -1721626,
             12891687, -8193132, -26442943, 10486144},
            {-22597207, -7012665, 8587003, -8257861, 4084309, -12970062, 361726,
             2610596, -23921530, -11455195},
            {5408411, -1136691, -4969122, 10561668, 24145918, 14240566,
             31319731, -4235541, 19985175, -3436086},
        },
        {
            {-13994457, 16616821, 14549246, 3341099, 32155958, 13648976,
             -17577068, 8849297, 65030, 8370684},
            {-8320926, -12049626, 31204563, 5839400, -20627288, -1057277,
             -19442942, 6922164, 12743482, -9800518},
            {-2361371, 12678785, 28815050, 4759974, -23893047, 4884717,
             23783145, 11038569, 18800704, 255233},
        },
        {
            {-5269658, -1773886, 13957886, 7990715, 23132995, 728773, 13393847,
             9066957, 19258688, -14753793},
            {-2936654, -10827535, -10432089, 14516793, -3640786, 4372541,
             -31934921, 2209390, -1524053, 2055794},
            {580882, 16705327, 5468415, -2683018, -30926419, -14696000,
             -7203346, -8994389, -30021019, 7394435},
        },
        {
            {23838809, 1822728, -15738443, 15242727, 8318092, -3733104,
             -21672180, -3492205, -4821741, 14799921},
            {13345610, 9759151, 3371034, -16137791, 16353039, 8577942, 31129804,
             13496856, -9056018, 7402518},
            {2286874, -4435931, -20042458, -2008336, -13696227, 5038122,
             11006906, -15760352, 8205061, 1607563},
        },
        {
            {14414086, -8002132, 3331830, -3208217, 22249151, -5594188,
             18364661, -2906958, 30019587, -9029278},
            {-27688051, 1585953, -10775053, 931069, -29120221, -11002319,
             -14410829, 12029093, 9944378, 8024},
            {4368715, -3709630, 29874200, -15022983, -20230386, -11410704,
             -16114594, -999085, -8142388, 5640030},
        },
        {
            {10299610, 13746483, 11661824, 16234854, 7630238, 5998374, 9809887,
             -16694564, 15219798, -14327783},
            {27425505, -5719081, 3055006, 10660664, 23458024, 595578, -15398605,
             -1173195, -18342183, 9742717},
            {6744077, 2427284, 26042789, 2720740, -847906, 1118974, 32324614,
             7406442, 12420155, 1994844},
        },
        {
            {14012521, -5024720, -18384453, -9578469, -26485342, -3936439,
             -13033478, -10909803, 24319929, -6446333},
            {16412690, -4507367, 10772641, 15929391, -17068788, -4658621,
             10555945, -10484049, -30102368, -4739048},
            {22397382, -7767684, -9293161, -12792868, 17166287, -9755136,
             -27333065, 6199366, 21880021, -12250760},
        },
        {
            {-4283307, 5368523, -31117018, 8163389, -30323063, 3209128,
             16557151, 8890729, 8840445, 4957760},
            {-15447727, 709327, -6919446, -10870178, -29777922, 6522332,
             -21720181, 12130072, -14796503, 5005757},
            {-2114751, -14308128, 23019042, 15765735, -25269683, 6002752,
             10183197, -13239326, -16395286, -2176112},
        },
    },
    {
        {
            {-19025756, 1632005, 13466291, -7995100, -23640451, 16573537,
             -32013908, -3057104, 22208662, 2000468},
            {3065073, -1412761, -25598674, -361432, -17683065, -5703415,
             -8164212, 11248527, -3691214, -7414184},
            {10379208, -6045554, 8877319, 1473647, -29291284, -12507580,
             16690915, 2553332, -3132688, 16400289},
        },
        {
            {15716668, 1254266, -18472690, 7446274, -8448918, 6344164,
             -22097271, -7285580, 26894937, 9132066},
            {24158887, 12938817, 11085297, -8177598, -28063478, -4457083,
             -30576463, 64452, -6817084, -2692882},
            {13488534, 7794716, 22236231, 5989356, 25426474, -12578208, 2350710,
             -3418511, -4688006, 2364226},
        },
        {
            {16335052, 9132434, 25640582, 6678888, 1725628, 8517937, -11807024,
             -11697457, 15445875, -7798101},
            {29004207, -7867081, 28661402, -640412, -12794003, -7943086,
             31863255, -4135540, -278050, -15759279},
            {-6122061, -14866665, -28614905, 14569919, -10857999, -3591829,
             10343412, -6976290, -29828287, -10815811},
        },
        {
            {27081650, 3463984, 14099042, -4517604, 1616303, -6205604, 29542636,
             15372179, 17293797, 960709},
            {20263915, 11434237, -5765435, 11236810, 13505955, -10857102,
             -16111345, 6493122, -19384511, 7639714},
            {-2830798, -14839232, 25403038, -8215196, -8317012, -16173699,
             18006287, -16043750, 29994677, -15808121},
        },
        {
            {9769828, 5202651, -24157398, -13631392, -28051003, -11561624,
             -24613141, -13860782, -31184575, 709464},
            {12286395, 13076066, -21775189, -1176622, -25003198, 4057652,
             -32018128, -8890874, 16102007, 13205847},
            {13733362, 5599946, 10557076, 3195751, -5557991, 8536970, -25540170,
             8525972, 10151379, 10394400},
        },
        {
            {4024660, -16137551, 22436262, 12276534, -9099015, -2686099,
             19698229, 11743039, -33302334, 8934414},
            {-15879800, -4525240, -8580747, -2934061, 14634845, -698278,
             -9449077, 3137094, -11536886, 11721158},
            {17555939, -5013938, 8268606, 2331751, -22738815, 9761013, 9319229,
             8835153, -9205489, -1280045},
        },
        {
            {-461409, -7830014, 20614118, 16688288, -7514766, -4807119,
             22300304, 505429, 6108462, -6183415},
            {-5070281, 12367917, -30663534, 3234473, 32617080, -8422642,
             29880583, -13483331, -26898490, -7867459},
            {-31975283, 5726539, 26934134, 10237677, -3173717, -605053,
             24199304, 3795095, 7592688, -14992079},
        },
        {
            {21594432, -14964228, 17466408, -4077222, 32537084, 2739898,
             6407723, 12018833, -28256052, 4298412},
            {-20650503, -11961496, -27236275, 570498, 3767144, -1717540,
             13891942, -1569194, 13717174, 10805743},
            {-14676630, -15644296, 15287174, 11927123, 24177847, -8175568,
             -796431, 14860609, -26938930, -5863836},
        },
    },
    {
        {
            {12962541, 5311799, -10060768, 11658280, 18855286, -7954201,
             13286263, -12808704, -4381056, 9882022},
            {18512079, 11319350, -20123124, 15090309, 18818594, 5271736,
             -22727904, 3666879, -23967430, -3299429},
            {-6789020, -3146043, 16192429, 13241070, 15898607, -14206114,
             -10084880, -6661110, -2403099, 5276065},
        },
        {
            {30169808, -5317648, 26306206, -11750859, 27814964, 7069267,
             7152851, 3684982, 1449224, 13082861},
            {10342826, 3098505, 2119311, 193222, 25702612, 12233820, 23697382,
             15056736, -21016438, -8202000},
            {-33150110, 3261608, 22745853, 7948688, 19370557, -15177665,
             -26171976, 6482814, -10300080, -11060101},
        },
        {
            {32869458, -5408545, 25609743, 15678670, -10687769, -15471071,
             26112421, 2521008, -22664288, 6904815},
            {29506923, 4457497, 3377935, -9796444, -30510046, 12935080, 1561737,
             3841096, -29003639, -6657642},
            {10340844, -6630377, -18656632, -2278430, 12621151, -13339055,
             30878497, -11824370, -25584551, 5181966},
        },
        {
            {25940115, -12658025, 17324188, -10307374, -8671468, 15029094,
             24396252, -16450922, -2322852, -12388574},
            {-21765684, 9916823, -1300409, 4079498, -1028346, 11909559, 1782390,
             12641087, 20603771, -6561742},
            {-18882287, -11673380, 24849422, 11501709, 13161720, -4768874,
             1925523, 11914390, 4662781, 7820689},
        },
        {
            {12241050, -425982, 8132691, 9393934, 32846760, -1599620, 29749456,
             12172924, 16136752, 15264020},
            {-10349955, -14680563, -8211979, 2330220, -17662549, -14545780,
             10658213, 6671822, 19012087, 3772772},
            {3753511, -3421066, 10617074, 2028709, 14841030, -6721664, 28718732,
             -15762884, 20527771, 12988982},
        },
        {
            {-14822485, -5797269, -3707987, 12689773, -898983, -10914866,
             -24183046, -10564943, 3299665, -12424953},
            {-16777703, -15253301, -9642417, 4978983, 3308785, 8755439, 6943197,
             6461331, -25583147, 8991218},
            {-17226263, 1816362, -1673288, -6086439, 31783888, -8175991,
             -32948145, 7417950, -30242287, 1507265},
        },
        {
            {29692663, 6829891, -10498800, 4334896, 20945975, -11906496,
             -28887608, 8209391, 14606362, -10647073},
            {-3481570, 8707081, 32188102, 5672294, 22096700, 1711240, -33020695,
             9761487, 4170404, -2085325},
            {-11587470, 14855945, -4127778, -1531857, -26649089, 15084046,
             22186522, 16002000, -14276837, -8400798},
        },
        {
            {-4811456, 13761029, -31703877, -2483919, -3312471, 7869047,
             -7113572, -9620092, 13240845, 10965870},
            {-7742563, -8256762, -14768334, -13656260, -23232383, 12387166,
             4498947, 14147411, 29514390, 4302863},
            {-13413405, -12407859, 20757302, -13801832, 14785143, 8976368,
             -5061276, -2144373, 17846988, -13971927},
        },
    },
    {
        {
            {-2244452, -754728, -4597030, -1066309, -6247172, 1455299,
             -21647728, -9214789, -5222701, 12650267},
            {-9906797, -16070310, 21134160, 12198166, -27064575, 708126, 387813,
             13770293, -19134326, 10958663},
            {22470984, 12369526, 23446014, -5441109, -21520802, -9698723,
             -11772496, -11574455, -25083830, 4271862},
        },
        {
            {-25169565, -10053642, -19909332, 15361595, -5984358, 2159192,
             75375, -4278529, -32526221, 8469673},
            {15854970, 4148314, -8893890, 7259002, 11666551, 13824734,
             -30531198, 2697372, 24154791, -9460943},
            {15446137, -15806644, 29759747, 14019369, 30811221, -9610191,
             -31582008, 12840104, 24913809, 9815020},
        },
        {
            {-4709286, -5614269, -31841498, -12288893, -14443537, 10799414,
             -9103676, 13438769, 18735128, 9466238},
            {11933045, 9281483, 5081055, -5183824, -2628162, -4905629, -7727821,
             -10896103, -22728655, 16199064},
            {14576810, 379472, -26786533, -8317236, -29426508, -10812974,
             -102766, 1876699, 30801119, 2164795},
        },
        {
            {15995086, 3199873, 13672555, 13712240, -19378835, -4647646,
             -13081610, -15496269, -13492807, 1268052},
            {-10290614, -3659039, -3286592, 10948818, 23037027, 3794475,
             -3470338, -12600221, -17055369, 3565904},
            {29210088, -9419337, -5919792, -4952785, 10834811, -13327726,
             -16512102, -10820713, -27162222, -14030531},
        },
        {
            {-13161890, 15508588, 16663704, -8156150, -28349942, 9019123,
             -29183421, -3769423, 2244111, -14001979},
            {-5152875, -3800936, -9306475, -6071583, 16243069, 14684434,
             -25673088, -16180800, 13491506, 4641841},
            {10813417, 643330, -19188515, -728916, 30292062, -16600078,
             27548447, -7721242, 14476989, -12767431},
        },
        {
            {10292079, 9984945, 6481436, 8279905, -7251514, 7032743, 27282937,
             -1644259, -27912810, 12651324},
            {-31185513, -813383, 22271204, 11835308, 10201545, 15351028,
             17099662, 3988035, 21721536, -3148940},
            {10202177, -6545839, -31373232, -9574638, -32150642, -8119683,
             -12906320, 3852694, 13216206, 14842320},
        },
        {
            {-15815640, -10601066, -6538952, -7258995, -6984659, -6581778,
             -31500847, 13765824, -27434397, 9900184},
            {14465505, -13833331, -32133984, -14738873, -27443187, 12990492,
             33046193, 15796406, -7051866, -8040114},
            {30924417, -8279620, 6359016, -12816335, 16508377, 9071735,
             -25488601, 15413635, 9524356, -7018878},
        },
        {
            {12274201, -13175547, 32627641, -1785326, 6736625, 13267305,
             5237659, -5109483, 15663516, 4035784},
            {-2951309, 8903985, 17349946, 601635, -16432815, -4612556,
             -13732739, -15889334, -22258478, 4659091},
            {-16916263, -4952973, -30393711, -15158821, 20774812, 15897498,
             5736189, 15026997, -2178256, -13455585},
        },
    },
    {
        {
            {-8858980, -2219056, 28571666, -10155518, -474467, -10105698,
             -3801496, 278095, 23440562, -290208},
            {10226241, -5928702, 15139956, 120818, -14867693, 5218603, 32937275,
             11551483, -16571960, -7442864},
            {17932739, -12437276, -24039557, 10749060, 11316803, 7535897,
             22503767, 5561594, -3646624, 3898661},
        },
        {
            {7749907, -969567, -16339731, -16464, -25018111, 15122143, -1573531,
             7152530, 21831162, 1245233},
            {26958459, -14658026, 4314586, 8346991, -5677764, 11960072,
             -32589295, -620035, -30402091, -16716212},
            {-12165896, 9166947, 33491384, 13673479, 29787085, 13096535,
             6280834, 14587357, -22338025, 13987525},
        },
        {
            {-24349909, 7778775, 21116000, 15572597, -4833266, -5357778,
             -4300898, -5124639, -7469781, -2858068},
            {9681908, -6737123, -31951644, 13591838, -6883821, 386950, 31622781,
             6439245, -14581012, 4091397},
            {-8426427, 1470727, -28109679, -1596990, 3978627, -5123623,
             -19622683, 12092163, 29077877, -14741988},
        },
        {
            {5269168, -6859726, -13230211, -8020715, 25932563, 1763552,
             -5606110, -5505881, -20017847, 2357889},
            {32264008, -15407652, -5387735, -1160093, -2091322, -3946900,
             23104804, -12869908, 5727338, 189038},
            {14609123, -8954470, -6000566, -16622781, -14577387, -7743898,
             -26745169, 10942115, -25888931, -14884697},
        },
        {
            {20513500, 5557931, -15604613, 7829531, 26413943, -2019404,
             -21378968, 7471781, 13913677, -5137875},
            {-25574376, 11967826, 29233242, 12948236, -6754465, 4713227,
             -8940970, 14059180, 12878652, 8511905},
            {-25656801, 3393631, -2955415, -7075526, -2250709, 9366908,
             -30223418, 6812974, 5568676, -3127656},
        },
        {
            {11630004, 12144454, 2116339, 13606037, 27378885, 15676917,
             -17408753, -13504373, -14395196, 8070818},
            {27117696, -10007378, -31282771, -5570088, 1127282, 12772488,
             -29845906, 10483306, -11552749, -1028714},
            {10637467, -5688064, 5674781, 1072708, -26343588, -6982302,
             -1683975, 9177853, -27493162, 15431203},
        },
        {
            {20525145, 10892566, -12742472, 12779443, -29493034, 16150075,
             -28240519, 14943142, -15056790, -7935931},
            {-30024462, 5626926, -551567, -9981087, 753598, 11981191, 25244767,
             -3239766, -3356550, 9594024},
            {-23752644, 2636870, -5163910, -10103818, 585134, 7877383, 11345683,
             -6492290, 13352335, -10977084},
        },
        {
            {-1931799, -5407458, 3304649, -12884869, 17015806, -4877091,
             -29783850, -7752482, -13215537, -319204},
            {20239939, 6607058, 6203985, 3483793, -18386976, -779229, -20723742,
             15077870, -22750759, 14523817},
            {27406042, -6041657, 27423596, -4497394, 4996214, 10002360,
             -28842031, -4545494, -30172742, -4805667},
        },
    },
    {
        {
            {11374242, 12660715, 17861383, -12540833, 10935568, 1099227,
             -13886076, -9091740, -27727044, 11358504},
            {-12730809, 10311867, 1510375, 10778093, -2119455, -9145702,
             32676003, 11149336, -26123651, 4985768},
            {-19096303, 341147, -6197485, -239033, 15756973, -8796662, -983043,
             13794114, -19414307, -15621255},
        },
        {
            {6490081, 11940286, 25495923, -7726360, 8668373, -8751316, 3367603,
             6970005, -1691065, -9004790},
            {1656497, 13457317, 15370807, 6364910, 13605745, 8362338, -19174622,
             -5475723, -16796596, -5031438},
            {-22273315, -13524424, -64685, -4334223, -18605636, -10921968,
             -20571065, -7007978, -99853, -10237333},
        },
        {
            {17747465, 10039260, 19368299, -4050591, -20630635, -16041286,
             31992683, -15857976, -29260363, -5511971},
            {31932027, -4986141, -19612382, 16366580, 22023614, 88450, 11371999,
             -3744247, 4882242, -10626905},
            {29796507, 37186, 19818052, 10115756, -11829032, 3352736, 18551198,
             3272828, -5190932, -4162409},
        },
        {
            {12501286, 4044383, -8612957, -13392385, -32430052, 5136599,
             -19230378, -3529697, 330070, -3659409},
            {6384877, 2899513, 17807477, 7663917, -2358888, 12363165, 25366522,
             -8573892, -271295, 12071499},
            {-8365515, -4042521, 25133448, -4517355, -6211027, 2265927,
             -32769618, 1936675, -5159697, 3829363},
        },
        {
            {28425966, -5835433, -577090, -4697198, -14217555, 6870930, 7921550,
             -6567787, 26333140, 14267664},
            {-11067219, 11871231, 27385719, -10559544, -4585914, -11189312,
             10004786, -8709488, -21761224, 8930324},
            {-21197785, -16396035, 25654216, -1725397, 12282012, 11008919,
             1541940, 4757911, -26491501, -16408940},
        },
        {
            {13537262, -7759490, -20604840, 10961927, -5922820, -13218065,
             -13156584, 6217254, -15943699, 13814990},
            {-17422573, 15157790, 18705543, 29619, 24409717, -260476, 27361681,
             9257833, -1956526, -1776914},
            {-25045300, -10191966, 15366585, 15166509, -13105086, 8423556,
             -29171540, 12361135, -18685978, 4578290},
        },
        {
            {24579768, 3711570, 1342322, -11180126, -27005135, 14124956,
             -22544529, 14074919, 21964432, 8235257},
            {-6528613, -2411497, 9442966, -5925588, 12025640, -1487420,
             -2981514, -1669206, 13006806, 2355433},
            {-16304899, -13605259, -6632427, -5142349, 16974359, -10911083,
             27202044, 1719366, 1141648, -12796236},
        },
        {
            {-12863944, -13219986, -8318266, -11018091, -6810145, -4843894,
             13475066, -3133972, 32674895, 13715045},
            {11423335, -5468059, 32344216, 8962751, 24989809, 9241752,
             -13265253, 16086212, -28740881, -15642093},
            {-1409668, 12530728, -6368726, 10847387, 19531186, -14132160,
             -11709148, 7791794, -27245943, 4383347},
        },
    },
    {
        {
            {-28970898, 5271447, -1266009, -9736989, -12455236, 16732599,
             -4862407, -4906449, 27193557, 6245191},
            {-15193956, 5362278, -1783893, 2695834, 4960227, 12840725, 23061898,
             3260492, 22510453, 8577507},
            {-12632451, 11257346, -32692994, 13548177, -721004, 10879011,
             31168030, 13952092, -29571492, -3635906},
        },
        {
            {3877321, -9572739, 32416692, 5405324, -11004407, -13656635,
             3759769, 11935320, 5611860, 8164018},
            {-16275802, 14667797, 15906460, 12155291, -22111149, -9039718,
             32003002, -8832289, 5773085, -8422109},
            {-23788118, -8254300, 1950875, 8937633, 18686727, 16459170, -905725,
             12376320, 31632953, 190926},
        },
        {
            {-24593607, -16138885, -8423991, 13378746, 14162407, 6901328,
             -8288749, 4508564, -25341555, -3627528},
            {8884438, -5884009, 6023974, 10104341, -6881569, -4941533, 18722941,
             -14786005, -1672488, 827625},
            {-32720583, -16289296, -32503547, 7101210, 13354605, 2659080,
             -1800575, -14108036, -24878478, 1541286},
        },
        {
            {2901347, -1117687, 3880376, -10059388, -17620940, -3612781,
             -21802117, -3567481, 20456845, -1885033},
            {27019610, 12299467, -13658288, -1603234, -12861660, -4861471,
             -19540150, -5016058, 29439641, 15138866},
            {21536104, -6626420, -32447818, -10690208, -22408077, 5175814,
             -5420040, -16361163, 7779328, 109896},
        },
        {
            {30279744, 14648750, -8044871, 6425558, 13639621, -743509, 28698390,
             12180118, 23177719, -554075},
            {26572847, 3405927, -31701700, 12890905, -19265668, 5335866,
             -6493768, 2378492, 4439158, -13279347},
            {-22716706, 3489070, -9225266, -332753, 18875722, -1140095,
             14819434, -12731527, -17717757, -5461437},
        },
        {
            {-5056483, 16566551, 15953661, 3767752, -10436499, 15627060,
             -820954, 2177225, 8550082, -15114165},
            {-18473302, 16596775, -381660, 15663611, 22860960, 15585581,
             -27844109, -3582739, -23260460, -8428588},
            {-32480551, 15707275, -8205912, -5652081, 29464558, 2713815,
             -22725137, 15860482, -21902570, 1494193},
        },
        {
            {-19562091, -14087393, -25583872, -9299552, 13127842, 759709,
             21923482, 16529112, 8742704, 12967017},
            {-28464899, 1553205, 32536856, -10473729, -24691605, -406174,
             -8914625, -2933896, -29903758, 15553883},
            {21877909, 3230008, 9881174, 10539357, -4797115, 2841332, 11543572,
             14513274, 19375923, -12647961},
        },
        {
            {8832269, -14495485, 13253511, 5137575, 5037871, 4078777, 24880818,
             -6222716, 2862653, 9455043},
            {29306751, 5123106, 20245049, -14149889, 9592566, 8447059, -2077124,
             -2990080, 15511449, 4789663},
            {-20679756, 7004547, 8824831, -9434977, -4045704, -3750736,
             -5754762, 108893, 23513200, 16652362},
        },
    },
    {
        {
            {-33256173, 4144782, -4476029, -6579123, 10770039, -7155542,
             -6650416, -12936300, -18319198, 10212860},
            {2756081, 8598110, 7383731, -6859892, 22312759, -1105012, 21179801,
             2600940, -9988298, -12506466},
            {-24645692, 13317462, -30449259, -15653928, 21365574, -10869657,
             11344424, 864440, -2499677, -16710063},
        },
        {
            {-26432803, 6148329, -17184412, -14474154, 18782929, -275997,
             -22561534, 211300, 2719757, 4940997},
            {-1323882, 3911313, -6948744, 14759765, -30027150, 7851207,
             21690126, 8518463, 26699843, 5276295},
            {-13149873, -6429067, 9396249, 365013, 24703301, -10488939, 1321586,
             149635, -15452774, 7159369},
        },
        {
            {9987780, -3404759, 17507962, 9505530, 9731535, -2165514, 22356009,
             8312176, 22477218, -8403385},
            {18155857, -16504990, 19744716, 9006923, 15154154, -10538976,
             24256460, -4864995, -22548173, 9334109},
            {2986088, -4911893, 10776628, -3473844, 10620590, -7083203,
             -21413845, 14253545, -22587149, 536906},
        },
        {
            {4377756, 8115836, 24567078, 15495314, 11625074, 13064599, 7390551,
             10589625, 10838060, -15420424},
            {-19342404, 867880, 9277171, -3218459, -14431572, -1986443,
             19295826, -15796950, 6378260, 699185},
            {7895026, 4057113, -7081772, -13077756, -17886831, -323126, -716039,
             15693155, -5045064, -13373962},
        },
        {
            {-7737563, -5869402, -14566319, -7406919, 11385654, 13201616,
             31730678, -10962840, -3918636, -9669325},
            {10188286, -15770834, -7336361, 13427543, 22223443, 14896287,
             30743455, 7116568, -21786507, 5427593},
            {696102, 13206899, 27047647, -10632082, 15285305, -9853179,
             10798490, -4578720, 19236243, 12477404},
        },
        {
            {-11229439, 11243796, -17054270, -8040865, -788228, -8167967,
             -3897669, 11180504, -23169516, 7733644},
            {17800790, -14036179, -27000429, -11766671, 23887827, 3149671,
             23466177, -10538171, 10322027, 15313801},
            {26246234, 11968874, 32263343, -5468728, 6830755, -13323031,
             -15794704, -101982, -24449242, 10890804},
        },
        {
            {-31365647, 10271363, -12660625, -6267268, 16690207, -13062544,
             -14982212, 16484931, 25180797, -5334884},
            {-586574, 10376444, -32586414, -11286356, 19801893, 10997610,
             2276632, 9482883, 316878, 13820577},
            {-9882808, -4510367, -2115506, 16457136, -11100081, 11674996,
             30756178, -7515054, 30696930, -3712849},
        },
        {
            {32988917, -9603412, 12499366, 7910787, -10617257, -11931514,
             -7342816, -9985397, -32349517, 7392473},
            {-8855661, 15927861, 9866406, -3649411, -2396914, -16655781,
             -30409476, -9134995, 25112947, -2926644},
            {-2504044, -436966, 25621774, -5678772, 15085042, -5479877,
             -24884878, -13526194, 5537438, -13914319},
        },
    },
    {
        {
            {-11225584, 2320285, -9584280, 10149187, -33444663, 5808648,
             -14876251, -1729667, 31234590, 6090599},
            {-9633316, 116426, 26083934, 2897444, -6364437, -2688086, 609721,
             15878753, -6970405, -9034768},
            {-27757857, 247744, -15194774, -9002551, 23288161, -10011936,
             -23869595, 6503646, 20650474, 1804084},
        },
        {
            {-27589786, 15456424, 8972517, 8469608, 15640622, 4439847, 3121995,
             -10329713, 27842616, -202328},
            {-15306973, 2839644, 22530074, 10026331, 4602058, 5048462, 28248656,
             5031932, -11375082, 12714369},
            {20807691, -7270825, 29286141, 11421711, -27876523, -13868230,
             -21227475, 1035546, -19733229, 12796920},
        },
        {
            {12076899, -14301286, -8785001, -11848922, -25012791, 16400684,
             -17591495, -12899438, 3480665, -15182815},
            {-32361549, 5457597, 28548107, 7833186, 7303070, -11953545,
             -24363064, -15921875, -33374054, 2771025},
            {-21389266, 421932, 26597266, 6860826, 22486084, -6737172,
             -17137485, -4210226, -24552282, 15673397},
        },
        {
            {-20184622, 2338216, 19788685, -9620956, -4001265, -8740893,
             -20271184, 4733254, 3727144, -12934448},
            {6120119, 814863, -11794402, -622716, 6812205, -15747771, 2019594,
             7975683, 31123697, -10958981},
            {30069250, -11435332, 30434654, 2958439, 18399564, -976289,
             12296869, 9204260, -16432438, 9648165},
        },
        {
            {32705432, -1550977, 30705658, 7451065, -11805606, 9631813, 3305266,
             5248604, -26008332, -11377501},
            {17219865, 2375039, -31570947, -5575615, -19459679, 9219903, 294711,
             15298639, 2662509, -16297073},
            {-1172927, -7558695, -4366770, -4287744, -21346413, -8434326,
             32087529, -1222777, 32247248, -14389861},
        },
        {
            {14312628, 1221556, 17395390, -8700143, -4945741, -8684635,
             -28197744, -9637817, -16027623, -13378845},
            {-1428825, -9678990, -9235681, 6549687, -7383069, -468664, 23046502,
             9803137, 17597934, 2346211},
            {18510800, 15337574, 26171504, 981392, -22241552, 7827556,
             -23491134, -11323352, 3059833, -11782870},
        },
        {
            {10141598, 6082907, 17829293, -1947643, 9830092, 13613136,
             -25556636, -5544586, -33502212, 3592096},
            {33114168, -15889352, -26525686, -13343397, 33076705, 8716171,
             1151462, 1521897, -982665, -6837803},
            {-32939165, -4255815, 23947181, -324178, -33072974, -12305637,
             -16637686, 3891704, 26353178, 693168},
        },
        {
            {30374239, 1595580, -16884039, 13186931, 4600344, 406904, 9585294,
             -400668, 31375464, 14369965},
            {-14370654, -7772529, 1510301, 6434173, -18784789, -6262728,
             32732230, -13108839, 17901441, 16011505},
            {18171223, -11934626, -12500402, 15197122, -11038147, -15230035,
             -19172240, -16046376, 8764035, 12309598},
        },
    },
    {
        {
            {5975908, -5243188, -19459362, -9681747, -11541277, 14015782,
             -23665757, 1228319, 17544096, -10593782},
            {5811932, -1715293, 3442887, -2269310, -18367348, -8359541,
             -18044043, -15410127, -5565381, 12348900},
            {-31399660, 11407555, 25755363, 6891399, -3256938, 14872274,
             -24849353, 8141295, -10632534, -585479},
        },
        {
            {-12675304, 694026, -5076145, 13300344, 14015258, -14451394,
             -9698672, -11329050, 30944593, 1130208},
            {8247766, -6710942, -26562381, -7709309, -14401939, -14648910,
             4652152, 2488540, 23550156, -271232},
            {17294316, -3788438, 7026748, 15626851, 22990044, 113481, 2267737,
             -5908146, -408818, -137719},
        },
        {
            {16091085, -16253926, 18599252, 7340678, 2137637, -1221657,
             -3364161, 14550936, 3260525, -7166271},
            {-4910104, -13332887, 18550887, 10864893, -16459325, -7291596,
             -23028869, -13204905, -12748722, 2701326},
            {-8574695, 16099415, 4629974, -16340524, -20786213, -6005432,
             -10018363, 9276971, 11329923, 1862132},
        },
        {
            {14763076, -15903608, -30918270, 3689867, 3511892, 10313526,
             -21951088, 12219231, -9037963, -940300},
            {8894987, -3446094, 6150753, 3013931, 301220, 15693451, -31981216,
             -2909717, -15438168, 11595570},
            {15214962, 3537601, -26238722, -14058872, 4418657, -15230761,
             13947276, 10730794, -13489462, -4363670},
        },
        {
            {-2538306, 7682793, 32759013, 263109, -29984731, -7955452,
             -22332124, -10188635, 977108, 699994},
            {-12466472, 4195084, -9211532, 550904, -15565337, 12917920,
             19118110, -439841, -30534533, -14337913},
            {31788461, -14507657, 4799989, 7372237, 8808585, -14747943, 9408237,
             -10051775, 12493932, -5409317},
        },
        {
            {-25680606, 5260744, -19235809, -6284470, -3695942, 16566087,
             27218280, 2607121, 29375955, 6024730},
            {842132, -2794693, -4763381, -8722815, 26332018, -12405641,
             11831880, 6985184, -9940361, 2854096},
            {-4847262, -7969331, 2516242, -5847713, 9695691, -7221186, 16512645,
             960770, 12121869, 16648078},
        },
        {
            {-15218652, 14667096, -13336229, 2013717, 30598287, -464137,
             -31504922, -7882064, 20237806, 2838411},
            {-19288047, 4453152, 15298546, -16178388, 22115043, -15972604,
             12544294, -13470457, 1068881, -12499905},
            {-9558883, -16518835, 33238498, 13506958, 30505848, -1114596,
             -8486907, -2630053, 12521378, 4845654},
        },
        {
            {-28198521, 10744108, -2958380, 10199664, 7759311, -13088600,
             3409348, -873400, -6482306, -12885870},
            {-23561822, 6230156, -20382013, 10655314, -24040585, -11621172,
             10477734, -1240216, -3113227, 13974498},
            {12966261, 15550616, -32038948, -1615346, 21025980, -629444,
             5642325, 7188737, 18895762, 12629579},
        },
    },
    {
        {
            {14741879, -14946887, 22177208, -11721237, 1279741, 8058600,
             11758140, 789443, 32195181, 3895677},
            {10758205, 15755439, -4509950, 9243698, -4879422, 6879879, -2204575,
             -3566119, -8982069, 4429647},
            {-2453894, 15725973, -20436342, -10410672, -5803908, -11040220,
             -7135870, -11642895, 18047436, -15281743},
        },
        {
            {-25173001, -11307165, 29759956, 11776784, -22262383, -15820455,
             10993114, -12850837, -17620701, -9408468},
            {21987233, 700364, -24505048, 14972008, -7774265, -5718395,
             32155026, 2581431, -29958985, 8773375},
            {-25568350, 454463, -13211935, 16126715, 25240068, 8594567,
             20656846, 12017935, -7874389, -13920155},
        },
        {
            {6028182, 6263078, -31011806, -11301710, -818919, 2461772,
             -31841174, -5468042, -1721788, -2776725},
            {-12278994, 16624277, 987579, -5922598, 32908203, 1248608, 7719845,
             -4166698, 28408820, 6816612},
            {-10358094, -8237829, 19549651, -12169222, 22082623, 16147817,
             20613181, 13982702, -10339570, 5067943},
        },
        {
            {-30505967, -3821767, 12074681, 13582412, -19877972, 2443951,
             -19719286, 12746132, 5331210, -10105944},
            {30528811, 3601899, -1957090, 4619785, -27361822, -15436388,
             24180793, -12570394, 27679908, -1648928},
            {9402404, -13957065, 32834043, 10838634, -26580150, -13237195,
             26653274, -8685565, 22611444, -12715406},
        },
        {
            {22190590, 1118029, 22736441, 15130463, -30460692, -5991321,
             19189625, -4648942, 4854859, 6622139},
            {-8310738, -2953450, -8262579, -3388049, -10401731, -271929,
             13424426, -3567227, 26404409, 13001963},
            {-31241838, -15415700, -2994250, 8939346, 11562230, -12840670,
             -26064365, -11621720, -15405155, 11020693},
        },
        {
            {1866042, -7949489, -7898649, -10301010, 12483315, 13477547,
             3175636, -12424163, 28761762, 1406734},
            {-448555, -1777666, 13018551, 3194501, -9580420, -11161737,
             24760585, -4347088, 25577411, -13378680},
            {-24290378, 4759345, -690653, -1852816, 2066747, 10693769,
             -29595790, 9884936, -9368926, 4745410},
        },
        {
            {-9141284, 6049714, -19531061, -4341411, -31260798, 9944276,
             -15462008, -11311852, 10931924, -11931931},
            {-16561513, 14112680, -8012645, 4817318, -8040464, -11414606,
             -22853429, 10856641, -20470770, 13434654},
            {22759489, -10073434, -16766264, -1871422, 13637442, -10168091,
             1765144, -12654326, 28445307, -5364710},
        },
        {
            {29875063, 12493613, 2795536, -3786330, 1710620, 15181182,
             -10195717, -8788675, 9074234, 1167180},
            {-26205683, 11014233, -9842651, -2635485, -26908120, 7532294,
             -18716888, -9535498, 3843903, 9367684},
            {-10969595, -6403711, 9591134, 9582310, 11349256, 108879, 16235123,
             8601684, -139197, 4242895},
        },
    },
    {
        {
            {22092954, -13191123, -2042793, -11968512, 32186753, -11517388,
             -6574341, 2470660, -27417366, 16625501},
            {-11057722, 3042016, 13770083, -9257922, 584236, -544855, -7770857,
             2602725, -27351616, 14247413},
            {6314175, -10264892, -32772502, 15957557, -10157730, 168750,
             -8618807, 14290061, 27108877, -1180880},
        },
        {
            {-8586597, -7170966, 13241782, 10960156, -32991015, -13794596,
             33547976, -11058889, -27148451, 981874},
            {22833440, 9293594, -32649448, -13618667, -9136966, 14756819,
             -22928859, -13970780, -10479804, -16197962},
            {-7768587, 3326786, -28111797, 10783824, 19178761, 14905060,
             22680049, 13906969, -15933690, 3797899},
        },
        {
            {21721356, -4212746, -12206123, 9310182, -3882239, -13653110,
             23740224, -2709232, 20491983, -8042152},
            {9209270, -15135055, -13256557, -6167798, -731016, 15289673,
             25947805, 15286587, 30997318, -6703063},
            {7392032, 16618386, 23946583, -8039892, -13265164, -1533858,
             -14197445, -2321576, 17649998, -250080},
        },
        {
            {-9301088, -14193827, 30609526, -3049543, -25175069, -1283752,
             -15241566, -9525724, -2233253, 7662146},
            {-17558673, 1763594, -33114336, 15908610, -30040870, -12174295,
             7335080, -8472199, -3174674, 3440183},
            {-19889700, -5977008, -24111293, -9688870, 10799743, -16571957,
             40450, -4431835, 4862400, 1133},
        },
        {
            {-32856209, -7873957, -5422389, 14860950, -16319031, 7956142,
             7258061, 311861, -30594991, -7379421},
            {-3773428, -1565936, 28985340, 7499440, 24445838, 9325937, 29727763,
             16527196, 18278453, 15405622},
            {-4381906, 8508652, -19898366, -3674424, -5984453, 15149970,
             -13313598, 843523, -21875062, 13626197},
        },
        {
            {2281448, -13487055, -10915418, -2609910, 1879358, 16164207,
             -10783882, 3953792, 13340839, 15928663},
            {31727126, -7179855, -18437503, -8283652, 2875793, -16390330,
             -25269894, -7014826, -23452306, 5964753},
            {4100420, -5959452, -17179337, 6017714, -18705837, 12227141,
             -26684835, 11344144, 2538215, -7570755},
        },
        {
            {-9433605, 6123113, 11159803, -2156608, 30016280, 14966241,
             -20474983, 1485421, -629256, -15958862},
            {-26804558, 4260919, 11851389, 9658551, -32017107, 16367492,
             -20205425, -13191288, 11659922, -11115118},
            {26180396, 10015009, -30844224, -8581293, 5418197, 9480663, 2231568,
             -10170080, 33100372, -1306171},
        },
        {
            {15121113, -5201871, -10389905, 15427821, -27509937, -15992507,
             21670947, 4486675, -5931810, -14466380},
            {16166486, -9483733, -11104130, 6023908, -31926798, -1364923,
             2340060, -16254968, -10735770, -10039824},
            {28042865, -3557089, -12126526, 12259706, -3717498, -6945899,
             6766453, -8689599, 18036436, 5803270},
        },
    },
    {
        {
            {-817581, 6763912, 11803561, 1585585, 10958447, -2671165, 23855391,
             4598332, -6159431, -14117438},
            {-31031306, -14256194, 17332029, -2383520, 31312682, -5967183,
             696309, 50292, -20095739, 11763584},
            {-594563, -2514283, -32234153, 12643980, 12650761, 14811489, 665117,
             -12613632, -19773211, -10713562},
        },
        {
            {30464590, -11262872, -4127476, -12734478, 19835327, -7105613,
             -24396175, 2075773, -17020157, 992471},
            {18357185, -6994433, 7766382, 16342475, -29324918, 411174, 14578841,
             8080033, -11574335, -10601610},
            {19598397, 10334610, 12555054, 2555664, 18821899, -10339780,
             21873263, 16014234, 26224780, 16452269},
        },
        {
            {-30223925, 5145196, 5944548, 16385966, 3976735, 2009897, -11377804,
             -7618186, -20533829, 3698650},
            {14187449, 3448569, -10636236, -10810935, -22663880, -3433596,
             7268410, -10890444, 27394301, 12015369},
            {19695761, 16087646, 28032085, 12999827, 6817792, 11427614,
             20244189, -1312777, -13259127, -3402461},
        },
        {
            {30860103, 12735208, -1888245, -4699734, -16974906, 2256940,
             -8166013, 12298312, -8550524, -10393462},
            {-5719826, -11245325, -1910649, 15569035, 26642876, -7587760,
             -5789354, -15118654, -4976164, 12651793},
            {-2848395, 9953421, 11531313, -5282879, 26895123, -12697089,
             -13118820, -16517902, 9768698, -2533218},
        },
        {
            {-24719459, 1894651, -287698, -4704085, 15348719, -8156530,
             32767513, 12765450, 4940095, 10678226},
            {18860224, 15980149, -18987240, -1562570, -26233012, -11071856,
             -7843882, 13944024, -24372348, 16582019},
            {-15504260, 4970268, -29893044, 4175593, -20993212, -2199756,
             -11704054, 15444560, -11003761, 7989037},
        },
        {
            {31490452, 5568061, -2412803, 2182383, -32336847, 4531686,
             -32078269, 6200206, -19686113, -14800171},
            {-17308668, -15879940, -31522777, -2831, -32887382, 16375549,
             8680158, -16371713, 28550068, -6857132},
            {-28126887, -5688091, 16837845, -1820458, -6850681, 12700016,
             -30039981, 4364038, 1155602, 5988841},
        },
        {
            {21890435, -13272907, -12624011, 12154349, -7831873, 15300496,
             23148983, -4470481, 24618407, 8283181},
            {-33136107, -10512751, 9975416, 6841041, -31559793, 16356536,
             3070187, -7025928, 1466169, 10740210},
            {-1509399, -15488185, -13503385, -10655916, 32799044, 909394,
             -13938903, -5779719, -32164649, -15327040},
        },
        {
            {3960823, -14267803, -28026090, -15918051, -19404858, 13146868,
             15567327, 951507, -3260321, -573935},
            {24740841, 5052253, -30094131, 8961361, 25877428, 6165135,
             -24368180, 14397372, -7380369, -6144105},
            {-28888365, 3510803, -28103278, -1158478, -11238128, -10631454,
             -15441463, -14453128, -1625486, -6494814},
        },
    },
    {
        {
            {793299, -9230478, 8836302, -6235707, -27360908, -2369593, 33152843,
             -4885251, -9906200, -621852},
            {5666233, 525582, 20782575, -8038419, -24538499, 14657740, 16099374,
             1468826, -6171428, -15186581},
            {-4859255, -3779343, -2917758, -6748019, 7778750, 11688288,
             -30404353, -9871238, -1558923, -9863646},
        },
        {
            {10896332, -7719704, 824275, 472601, -19460308, 3009587, 25248958,
             14783338, -30581476, -15757844},
            {10566929, 12612572, -31944212, 11118703, -12633376, 12362879,
             21752402, 8822496, 24003793, 14264025},
            {27713862, -7355973, -11008240, 9227530, 27050101, 2504721,
             23886875, -13117525, 13958495, -5732453},
        },
        {
            {-23481610, 4867226, -27247128, 3900521, 29838369, -8212291,
             -31889399, -10041781, 7340521, -15410068},
            {4646514, -8011124, -22766023, -11532654, 23184553, 8566613,
             31366726, -1381061, -15066784, -10375192},
            {-17270517, 12723032, -16993061, 14878794, 21619651, -6197576,
             27584817, 3093888, -8843694, 3849921},
        },
        {
            {-9064912, 2103172, 25561640, -15125738, -5239824, 9582958,
             32477045, -9017955, 5002294, -15550259},
            {-12057553, -11177906, 21115585, -13365155, 8808712, -12030708,
             16489530, 13378448, -25845716, 12741426},
            {-5946367, 10645103, -30911586, 15390284, -3286982, -7118677,
             24306472, 15852464, 28834118, -7646072},
        },
        {
            {-17335748, -9107057, -24531279, 9434953, -8472084, -583362,
             -13090771, 455841, 20461858, 5491305},
            {13669248, -16095482, -12481974, -10203039, -14569770, -11893198,
             -24995986, 11293807, -28588204, -9421832},
            {28497928, 6272777, -33022994, 14470570, 8906179, -1225630,
             18504674, -14165166, 29867745, -8795943},
        },
        {
            {-16207023, 13517196, -27799630, -13697798, 24009064, -6373891,
             -6367600, -13175392, 22853429, -4012011},
            {24191378, 16712145, -13931797, 15217831, 14542237, 1646131,
             18603514, -11037887, 12876623, -2112447},
            {17902668, 4518229, -411702, -2829247, 26878217, 5258055, -12860753,
             608397, 16031844, 3723494},
        },
        {
            {-28632773, 12763728, -20446446, 7577504, 33001348, -13017745,
             17558842, -7872890, 23896954, -4314245},
            {-20005381, -12011952, 31520464, 605201, 2543521, 5991821, -2945064,
             7229064, -9919646, -8826859},
            {28816045, 298879, -28165016, -15920938, 19000928, -1665890,
             -12680833, -2949325, -18051778, -2082915},
        },
        {
            {16000882, -344896, 3493092, -11447198, -29504595, -13159789,
             12577740, 16041268, -19715240, 7847707},
            {10151868, 10572098, 27312476, 7922682, 14825339, 4723128,
             -32855931, -6519018, -10020567, 3852848},
            {-11430470, 15697596, -21121557, -4420647, 5386314, 15063598,
             16514493, -15932110, 29330899, -15076224},
        },
    },
    {
        {
            {-25499735, -4378794, -15222908, -6901211, 16615731, 2051784,
             3303702, 15490, -27548796, 12314391},
            {15683520, -6003043, 18109120, -9980648, 15337968, -5997823,
             -16717435, 15921866, 16103996, -3731215},
            {-23169824, -10781249, 13588192, -1628807, -3798557, -1074929,
             -19273607, 5402699, -29815713, -9841101},
        },
        {
            {23190676, 2384583, -32714340, 3462154, -29903655, -1529132,
             -11266856, 8911517, -25205859, 2739713},
            {21374101, -3554250, -33524649, 9874411, 15377179, 11831242,
             -33529904, 6134907, 4931255, 11987849},
            {-7732, -2978858, -16223486, 7277597, 105524, -322051, -31480539,
             13861388, -30076310, 10117930},
        },
        {
            {-29501170, -10744872, -26163768, 13051539, -25625564, 5089643,
             -6325503, 6704079, 12890019, 15728940},
            {-21972360, -11771379, -951059, -4418840, 14704840, 2695116, 903376,
             -10428139, 12885167, 8311031},
            {-17516482, 5352194, 10384213, -13811658, 7506451, 13453191,
             26423267, 4384730, 1888765, -5435404},
        },
        {
            {-25817338, -3107312, -13494599, -3182506, 30896459, -13921729,
             -32251644, -12707869, -19464434, -3340243},
            {-23607977, -2665774, -526091, 4651136, 5765089, 4618330, 6092245,
             14845197, 17151279, -9854116},
            {-24830458, -12733720, -15165978, 10367250, -29530908, -265356,
             22825805, -7087279, -16866484, 16176525},
        },
        {
            {-23583256, 6564961, 20063689, 3798228, -4740178, 7359225, 2006182,
             -10363426, -28746253, -10197509},
            {-10626600, -4486402, -13320562, -5125317, 3432136, -6393229,
             23632037, -1940610, 32808310, 1099883},
            {15030977, 5768825, -27451236, -2887299, -6427378, -15361371,
             -15277896, -6809350, 2051441, -15225865},
        },
        {
            {-3362323, -7239372, 7517890, 9824992, 23555850, 295369, 5148398,
             -14154188, -22686354, 16633660},
            {4577086, -16752288, 13249841, -15304328, 19958763, -14537274,
             18559670, -10759549, 8402478, -9864273},
            {-28406330, -1051581, -26790155, -907698, -17212414, -11030789,
             9453451, -14980072, 17983010, 9967138},
        },
        {
            {-25762494, 6524722, 26585488, 9969270, 24709298, 1220360, -1677990,
             7806337, 17507396, 3651560},
            {-10420457, -4118111, 14584639, 15971087, -15768321, 8861010,
             26556809, -5574557, -18553322, -11357135},
            {2839101, 14284142, 4029895, 3472686, 14402957, 12689363, -26642121,
             8459447, -5605463, -7621941},
        },
        {
            {-4839289, -3535444, 9744961, 2871048, 25113978, 3187018, -25110813,
             -849066, 17258084, -7977739},
            {18164541, -10595176, -17154882, -1542417, 19237078, -9745295,
             23357533, -15217008, 26908270, 12150756},
            {-30264870, -7647865, 5112249, -7036672, -1499807, -6974257, 43168,
             -5537701, -32302074, 16215819},
        },
    },
    {
        {
            {-6898905, 9824394, -12304779, -4401089, -31397141, -6276835,
             32574489, 12532905, -7503072, -8675347},
            {-27343522, -16515468, -27151524, -10722951, 946346, 16291093,
             254968, 7168080, 21676107, -1943028},
            {21260961, -8424752, -16831886, -11920822, -23677961, 3968121,
             -3651949, -6215466, -3556191, -7913075},
        },
        {
            {16544754, 13250366, -16804428, 15546242, -4583003, 12757258,
             -2462308, -8680336, -18907032, -9662799},
            {-2415239, -15577728, 18312303, 4964443, -15272530, -12653564,
             26820651, 16690659, 25459437, -4564609},
            {-25144690, 11425020, 28423002, -11020557, -6144921, -15826224,
             9142795, -2391602, -6432418, -1644817},
        },
        {
            {-23104652, 6253476, 16964147, -3768872, -25113972, -12296437,
             -27457225, -16344658, 6335692, 7249989},
            {-30333227, 13979675, 7503222, -12368314, -11956721, -4621693,
             -30272269, 2682242, 25993170, -12478523},
            {4364628, 5930691, 32304656, -10044554, -8054781, 15091131,
             22857016, -10598955, 31820368, 15075278},
        },
        {
            {31879134, -8918693, 17258761, 90626, -8041836, -4917709, 24162788,
             -9650886, -17970238, 12833045},
            {19073683, 14851414, -24403169, -11860168, 7625278, 11091125,
             -19619190, 2074449, -9413939, 14905377},
            {24483667, -11935567, -2518866, -11547418, -1553130, 15355506,
             -25282080, 9253129, 27628530, -7555480},
        },
        {
            {17597607, 8340603, 19355617, 552187, 26198470, -3176583, 4593324,
             -9157582, -14110875, 15297016},
            {510886, 14337390, -31785257, 16638632, 6328095, 2713355, -20217417,
             -11864220, 8683221, 2921426},
            {18606791, 11874196, 27155355, -5281482, -24031742, 6265446,
             -25178240, -1278924, 4674690, 13890525},
        },
        {
            {13609624, 13069022, -27372361, -13055908, 24360586, 9592974,
             14977157, 9835105, 4389687, 288396},
            {9922506, -519394, 13613107, 5883594, -18758345, -434263, -12304062,
             8317628, 23388070, 16052080},
            {12720016, 11937594, -31970060, -5028689, 26900120, 8561328,
             -20155687, -11632979, -14754271, -10812892},
        },
        {
            {15961858, 14150409, 26716931, -665832, -22794328, 13603569,
             11829573, 7467844, -28822128, 929275},
            {11038231, -11582396, -27310482, -7316562, -10498527, -16307831,
             -23479533, -9371869, -21393143, 2465074},
            {20017163, -4323226, 27915242, 1529148, 12396362, 15675764,
             13817261, -9658066, 2463391, -4622140},
        },
        {
            {-16358878, -12663911, -12065183, 4996454, -1256422, 1073572,
             9583558, 12851107, 4003896, 12673717},
            {-1731589, -15155870, -3262930, 16143082, 19294135, 13385325,
             14741514, -9103726, 7903886, 2348101},
            {24536016, -16515207, 12715592, -3862155, 1511293, 10047386,
             -3842346, -7129159, -28377538, 10048127},
        },
    },
    {
        {
            {-12622226, -6204820, 30718825, 2591312, -10617028, 12192840,
             18873298, -7297090, -32297756, 15221632},
            {-26478122, -11103864, 11546244, -1852483, 9180880, 7656409,
             -21343950, 2095755, 29769758, 6593415},
            {-31994208, -2907461, 4176912, 3264766, 12538965, -868111, 26312345,
             -6118678, 30958054, 8292160},
        },
        {
            {31429822, -13959116, 29173532, 15632448, 12174511, -2760094,
             32808831, 3977186, 26143136, -3148876},
            {22648901, 1402143, -22799984, 13746059, 7936347, 365344, -8668633,
             -1674433, -3758243, -2304625},
            {-15491917, 8012313, -2514730, -12702462, -23965846, -10254029,
             -1612713, -1535569, -16664475, 8194478},
        },
        {
            {27338066, -7507420, -7414224, 10140405, -19026427, -6589889,
             27277191, 8855376, 28572286, 3005164},
            {26287124, 4821776, 25476601, -4145903, -3764513, -15788984,
             -18008582, 1182479, -26094821, -13079595},
            {-7171154, 3178080, 23970071, 6201893, -17195577, -4489192,
             -21876275, -13982627, 32208683, -1198248},
        },
        {
            {-16657702, 2817643, -10286362, 14811298, 6024667, 13349505,
             -27315504, -10497842, -27672585, -11539858},
            {15941029, -9405932, -21367050, 8062055, 31876073, -238629,
             -15278393, -1444429, 15397331, -4130193},
            {8934485, -13485467, -23286397, -13423241, -32446090, 14047986,
             31170398, -1441021, -27505566, 15087184},
        },
        {
            {-18357243, -2156491, 24524913, -16677868, 15520427, -6360776,
             -15502406, 11461896, 16788528, -5868942},
            {-1947386, 16013773, 21750665, 3714552, -17401782, -16055433,
             -3770287, -10323320, 31322514, -11615635},
            {21426655, -5650218, -13648287, -5347537, -28812189, -4920970,
             -18275391, -14621414, 13040862, -12112948},
        },
        {
            {11293895, 12478086, -27136401, 15083750, -29307421, 14748872,
             14555558, -13417103, 1613711, 4896935},
            {-25894883, 15323294, -8489791, -8057900, 25967126, -13425460,
             2825960, -4897045, -23971776, -11267415},
            {-15924766, -5229880, -17443532, 6410664, 3622847, 10243618,
             20615400, 12405433, -23753030, -8436416},
        },
        {
            {-7091295, 12556208, -20191352, 9025187, -17072479, 4333801,
             4378436, 2432030, 23097949, -566018},
            {4565804, -16025654, 20084412, -7842817, 1724999, 189254, 24767264,
             10103221, -18512313, 2424778},
            {366633, -11976806, 8173090, -6890119, 30788634, 5745705, -7168678,
             1344109, -3642553, 12412659},
        },
        {
            {-24001791, 7690286, 14929416, -168257, -32210835, -13412986,
             24162697, -15326504, -3141501, 11179385},
            {18289522, -14724954, 8056945, 16430056, -21729724, 7842514,
             -6001441, -1486897, -18684645, -11443503},
            {476239, 6601091, -6152790, -9723375, 17503545, -4863900, 27672959,
             13403813, 11052904, 5219329},
        },
    },
    {
        {
            {20678546, -8375738, -32671898, 8849123, -5009758, 14574752,
             31186971, -3973730, 9014762, -8579056},
            {-13644050, -10350239, -15962508, 5075808, -1514661, -11534600,
             -33102500, 9160280, 8473550, -3256838},
            {24900749, 14435722, 17209120, -15292541, -22592275, 9878983,
             -7689309, -16335821, -24568481, 11788948},
        },
        {
            {-3118155, -11395194, -13802089, 14797441, 9652448, -6845904,
             -20037437, 10410733, -24568470, -1458691},
            {-15659161, 16736706, -22467150, 10215878, -9097177, 7563911,
             11871841, -12505194, -18513325, 8464118},
            {-23400612, 8348507, -14585951, -861714, -3950205, -6373419,
             14325289, 8628612, 33313881, -8370517},
        },
        {
            {-20186973, -4967935, 22367356, 5271547, -1097117, -4788838,
             -24805667, -10236854, -8940735, -5818269},
            {-6948785, -1795212, -32625683, -16021179, 32635414, -7374245,
             15989197, -12838188, 28358192, -4253904},
            {-23561781, -2799059, -32351682, -1661963, -9147719, 10429267,
             -16637684, 4072016, -5351664, 5596589},
        },
        {
            {-28236598, -3390048, 12312896, 6213178, 3117142, 16078565,
             29266239, 2557221, 1768301, 15373193},
            {-7243358, -3246960, -4593467, -7553353, -127927, -912245, -1090902,
             -4504991, -24660491, 3442910},
            {-30210571, 5124043, 14181784, 8197961, 18964734, -11939093,
             22597931, 7176455, -18585478, 13365930},
        },
        {
            {-7877390, -1499958, 8324673, 4690079, 6261860, 890446, 24538107,
             -8570186, -9689599, -3031667},
            {25008904, -10771599, -4305031, -9638010, 16265036, 15721635,
             683793, -11823784, 15723479, -15163481},
            {-9660625, 12374379, -27006999, -7026148, -7724114, -12314514,
             11879682, 5400171, 519526, -1235876},
        },
        {
            {22258397, -16332233, -7869817, 14613016, -22520255, -2950923,
             -20353881, 7315967, 16648397, 7605640},
            {-8081308, -8464597, -8223311, 9719710, 19259459, -15348212,
             23994942, -5281555, -9468848, 4763278},
            {-21699244, 9220969, -15730624, 1084137, -25476107, -2852390,
             31088447, -7764523, -11356529, 728112},
        },
        {
            {26047220, -11751471, -6900323, -16521798, 24092068, 9158119,
             -4273545, -12555558, -29365436, -5498272},
            {17510331, -322857, 5854289, 8403524, 17133918, -3112612, -28111007,
             12327945, 10750447, 10014012},
            {-10312768, 3936952, 9156313, -8897683, 16498692, -994647,
             -27481051, -666732, 3424691, 7540221},
        },
        {
            {30322361, -6964110, 11361005, -4143317, 7433304, 4989748, -7071422,
             -16317219, -9244265, 15258046},
            {13054562, -2779497, 19155474, 469045, -12482797, 4566042, 5631406,
             2711395, 1062915, -5136345},
            {-19240248, -11254599, -29509029, -7499965, -5835763, 13005411,
             -6066489, 12194497, 32960380, 1459310},
        },
    },
    {
        {
            {19852034, 7027924, 23669353, 10020366, 8586503, -6657907, 394197,
             -6101885, 18638003, -11174937},
            {31395534, 15098109, 26581030, 8030562, -16527914, -5007134,
             9012486, -7584354, -6643087, -5442636},
            {-9192165, -2347377, -1997099, 4529534, 25766844, 607986, -13222,
             9677543, -32294889, -6456008},
        },
        {
            {-2444496, -149937, 29348902, 8186665, 1873760, 12489863, -30934579,
             -7839692, -7852844, -8138429},
            {-15236356, -15433509, 7766470, 746860, 26346930, -10221762,
             -27333451, 10754588, -9431476, 5203576},
            {31834314, 14135496, -770007, 5159118, 20917671, -16768096,
             -7467973, -7337524, 31809243, 7347066},
        },
        {
            {-9606723, -11874240, 20414459, 13033986, 13716524, -11691881,
             19797970, -12211255, 15192876, -2087490},
            {-12663563, -2181719, 1168162, -3804809, 26747877, -14138091,
             10609330, 12694420, 33473243, -13382104},
            {33184999, 11180355, 15832085, -11385430, -1633671, 225884,
             15089336, -11023903, -6135662, 14480053},
        },
        {
            {31308717, -5619998, 31030840, -1897099, 15674547, -6582883,
             5496208, 13685227, 27595050, 8737275},
            {-20318852, -15150239, 10933843, -16178022, 8335352, -7546022,
             -31008351, -12610604, 26498114, 66511},
            {22644454, -8761729, -16671776, 4884562, -3105614, -13559366,
             30540766, -4286747, -13327787, -7515095},
        },
        {
            {-28017847, 9834845, 18617207, -2681312, -3401956, -13307506,
             8205540, 13585437, -17127465, 15115439},
            {23711543, -672915, 31206561, -8362711, 6164647, -9709987,
             -33535882, -1426096, 8236921, 16492939},
            {-23910559, -13515526, -26299483, -4503841, 25005590, -7687270,
             19574902, 10071562, 6708380, -6222424},
        },
        {
            {2101391, -4930054, 19702731, 2367575, -15427167, 1047675, 5301017,
             9328700, 29955601, -11678310},
            {3096359, 9271816, -21620864, -15521844, -14847996, -7592937,
             -25892142, -12635595, -9917575, 6216608},
            {-32615849, 338663, -25195611, 2510422, -29213566, -13820213,
             24822830, -6146567, -26767480, 7525079},
        },
        {
            {-23066649, -13985623, 16133487, -7896178, -3389565, 778788,
             -910336, -2782495, -19386633, 11994101},
            {21691500, -13624626, -641331, -14367021, 3285881, -3483596,
             -25064666, 9718258, -7477437, 13381418},
            {18445390, -4202236, 14979846, 11622458, -1727110, -3582980,
             23111648, -6375247, 28535282, 15779576},
        },
        {
            {30098053, 3089662, -9234387, 16662135, -21306940, 11308411,
             -14068454, 12021730, 9955285, -16303356},
            {9734894, -14576830, -7473633, -9138735, 2060392, 11313496,
             -18426029, 9924399, 20194861, 13380996},
            {-26378102, -7965207, -22167821, 15789297, -18055342, -6168792,
             -1984914, 15707771, 26342023, 10146099},
        },
    },
    {
        {
            {-26016874, -219943, 21339191, -41388, 19745256, -2878700,
             -29637280, 2227040, 21612326, -545728},
            {-13077387, 1184228, 23562814, -5970442, -20351244, -6348714,
             25764461, 12243797, -20856566, 11649658},
            {-10031494, 11262626, 27384172, 2271902, 26947504, -15997771, 39944,
             6114064, 33514190, 2333242},
        },
        {
            {-21433588, -12421821, 8119782, 7219913, -21830522, -9016134,
             -6679750, -12670638, 24350578, -13450001},
            {-4116307, -11271533, -23886186, 4843615, -30088339, 690623,
             -31536088, -10406836, 8317860, 12352766},
            {18200138, -14475911, -33087759, -2696619, -23702521, -9102511,
             -23552096, -2287550, 20712163, 6719373},
        },
        {
            {26656208, 6075253, -7858556, 1886072, -28344043, 4262326, 11117530,
             -3763210, 26224235, -3297458},
            {-17168938, -14854097, -3395676, -16369877, -19954045, 14050420,
             21728352, 9493610, 18620611, -16428628},
            {-13323321, 13325349, 11432106, 5964811, 18609221, 6062965,
             -5269471, -9725556, -30701573, -16479657},
        },
        {
            {-23860538, -11233159, 26961357, 1640861, -32413112, -16737940,
             12248509, -5240639, 13735342, 1934062},
            {25089769, 6742589, 17081145, -13406266, 21909293, -16067981,
             -15136294, -3765346, -21277997, 5473616},
            {31883677, -7961101, 1083432, -11572403, 22828471, 13290673,
             -7125085, 12469656, 29111212, -5451014},
        },
        {
            {24244947, -15050407, -26262976, 2791540, -14997599, 16666678,
             24367466, 6388839, -10295587, 452383},
            {-25640782, -3417841, 5217916, 16224624, 19987036, -4082269,
             -24236251, -5915248, 15766062, 8407814},
            {-20406999, 13990231, 15495425, 16395525, 5377168, 15166495,
             -8917023, -4388953, -8067909, 2276718},
        },
        {
            {30157918, 12924066, -17712050, 9245753, 19895028, 3368142,
             -23827587, 5096219, 22740376, -7303417},
            {2041139, -14256350, 7783687, 13876377, -25946985, -13352459,
             24051124, 13742383, -15637599, 13295222},
            {33338237, -8505733, 12532113, 7977527, 9106186, -1715251,
             -17720195, -4612972, -4451357, -14669444},
        },
        {
            {-20045281, 5454097, -14346548, 6447146, 28862071, 1883651,
             -2469266, -4141880, 7770569, 9620597},
            {23208068, 7979712, 33071466, 8149229, 1758231, -10834995, 30945528,
             -1694323, -33502340, -14767970},
            {1439958, -16270480, -1079989, -793782, 4625402, 10647766, -5043801,
             1220118, 30494170, -11440799},
        },
        {
            {-5037580, -13028295, -2970559, -3061767, 15640974, -6701666,
             -26739026, 926050, -1684339, -13333647},
            {13908495, -3549272, 30919928, -6273825, -21521863, 7989039,
             9021034, 9078865, 3353509, 4033511},
            {-29663431, -15113610, 32259991, -344482, 24295849, -12912123,
             23161163, 8839127, 27485041, 7356032},
        },
    },
    {
        {
            {9661027, 705443, 11980065, -5370154, -1628543, 14661173, -6346142,
             2625015, 28431036, -16771834},
            {-23839233, -8311415, -25945511, 7480958, -17681669, -8354183,
             -22545972, 14150565, 15970762, 4099461},
            {29262576, 16756590, 26350592, -8793563, 8529671, -11208050,
             13617293, -9937143, 11465739, 8317062},
        },
        {
            {-25493081, -6962928, 32500200, -9419051, -23038724, -2302222,
             14898637, 3848455, 20969334, -5157516},
            {-20384450, -14347713, -18336405, 13884722, -33039454, 2842114,
             -21610826, -3649888, 11177095, 14989547},
            {-24496721, -11716016, 16959896, 2278463, 12066309, 10137771,
             13515641, 2581286, -28487508, 9930240},
        },
        {
            {-17751622, -2097826, 16544300, -13009300, -15914807, -14949081,
             18345767, -13403753, 16291481, -5314038},
            {-33229194, 2553288, 32678213, 9875984, 8534129, 6889387, -9676774,
             6957617, 4368891, 9788741},
            {16660756, 7281060, -10830758, 12911820, 20108584, -8101676,
             -21722536, -8613148, 16250552, -11111103},
        },
        {
            {-19765507, 2390526, -16551031, 14161980, 1905286, 6414907, 4689584,
             10604807, -30190403, 4782747},
            {-1354539, 14736941, -7367442, -13292886, 7710542, -14155590,
             -9981571, 4383045, 22546403, 437323},
            {31665577, -12180464, -16186830, 1491339, -18368625, 3294682,
             27343084, 2786261, -30633590, -14097016},
        },
        {
            {-14467279, -683715, -33374107, 7448552, 19294360, 14334329,
             -19690631, 2355319, -19284671, -6114373},
            {15121312, -15796162, 6377020, -6031361, -10798111, -12957845,
             18952177, 15496498, -29380133, 11754228},
            {-2637277, -13483075, 8488727, -14303896, 12728761, -1622493,
             7141596, 11724556, 22761615, -10134141},
        },
        {
            {16918416, 11729663, -18083579, 3022987, -31015732, -13339659,
             -28741185, -12227393, 32851222, 11717399},
            {11166634, 7338049, -6722523, 4531520, -29468672, -7302055,
             31474879, 3483633, -1193175, -4030831},
            {-185635, 9921305, 31456609, -13536438, -12013818, 13348923,
             33142652, 6546660, -19985279, -3948376},
        },
        {
            {-32460596, 11266712, -11197107, -7899103, 31703694, 3855903,
             -8537131, -12833048, -30772034, -15486313},
            {-18006477, 12709068, 3991746, -6479188, -21491523, -10550425,
             -31135347, -16049879, 10928917, 3011958},
            {-6957757, -15594337, 31696059, 334240, 29576716, 14796075,
             -30831056, -12805180, 18008031, 10258577},
        },
        {
            {-22448644, 15655569, 7018479, -4410003, -30314266, -1201591,
             -1853465, 1367120, 25127874, 6671743},
            {29701166, -14373934, -10878120, 9279288, -17568, 13127210,
             21382910, 11042292, 25838796, 4642684},
            {-20430234, 14955537, -24126347, 8124619, -5369288, -5990470,
             30468147, -13900640, 18423289, 4177476},
        },
    },
};

static uint8_t negative(signed char b)
{
    uint32_t x = b;

    x >>= 31; /* 1: yes; 0: no */
    return x;
}

static void table_select(ge_precomp *t, int pos, signed char b)
{
    ge_precomp minust;
    uint8_t bnegative = negative(b);
    uint8_t babs = b - ((uint8_t)((-bnegative) & b) << 1);

    ge_precomp_0(t);
    cmov(t, &k25519Precomp[pos][0], equal(babs, 1));
    cmov(t, &k25519Precomp[pos][1], equal(babs, 2));
    cmov(t, &k25519Precomp[pos][2], equal(babs, 3));
    cmov(t, &k25519Precomp[pos][3], equal(babs, 4));
    cmov(t, &k25519Precomp[pos][4], equal(babs, 5));
    cmov(t, &k25519Precomp[pos][5], equal(babs, 6));
    cmov(t, &k25519Precomp[pos][6], equal(babs, 7));
    cmov(t, &k25519Precomp[pos][7], equal(babs, 8));
    fe_copy(minust.yplusx, t->yminusx);
    fe_copy(minust.yminusx, t->yplusx);
    fe_neg(minust.xy2d, t->xy2d);
    cmov(t, &minust, bnegative);
}

/*
 * h = a * B
 *
 * where a = a[0]+256*a[1]+...+256^31 a[31]
 * B is the Ed25519 base point (x,4/5) with x positive.
 *
 * Preconditions:
 *   a[31] <= 127
 */
static void ge_scalarmult_base(ge_p3 *h, const uint8_t *a)
{
    signed char e[64];
    signed char carry;
    ge_p1p1 r;
    ge_p2 s;
    ge_precomp t;
    int i;

    for (i = 0; i < 32; ++i) {
        e[2 * i + 0] = (a[i] >> 0) & 15;
        e[2 * i + 1] = (a[i] >> 4) & 15;
    }
    /* each e[i] is between 0 and 15 */
    /* e[63] is between 0 and 7 */

    carry = 0;
    for (i = 0; i < 63; ++i) {
        e[i] += carry;
        carry = e[i] + 8;
        carry >>= 4;
        e[i] -= carry << 4;
    }
    e[63] += carry;
    /* each e[i] is between -8 and 8 */

    ge_p3_0(h);
    for (i = 1; i < 64; i += 2) {
        table_select(&t, i / 2, e[i]);
        ge_madd(&r, h, &t);
        ge_p1p1_to_p3(h, &r);
    }

    ge_p3_dbl(&r, h);
    ge_p1p1_to_p2(&s, &r);
    ge_p2_dbl(&r, &s);
    ge_p1p1_to_p2(&s, &r);
    ge_p2_dbl(&r, &s);
    ge_p1p1_to_p2(&s, &r);
    ge_p2_dbl(&r, &s);
    ge_p1p1_to_p3(h, &r);

    for (i = 0; i < 64; i += 2) {
        table_select(&t, i / 2, e[i]);
        ge_madd(&r, h, &t);
        ge_p1p1_to_p3(h, &r);
    }

    OPENSSL_cleanse(e, sizeof(e));
}

#if !defined(BASE_2_51_IMPLEMENTED)
/*
 * Replace (f,g) with (g,f) if b == 1;
 * replace (f,g) with (f,g) if b == 0.
 *
 * Preconditions: b in {0,1}.
 */
static void fe_cswap(fe f, fe g, unsigned int b)
{
    size_t i;

    b = 0-b;
    for (i = 0; i < 10; i++) {
        int32_t x = f[i] ^ g[i];
        x &= b;
        f[i] ^= x;
        g[i] ^= x;
    }
}

/*
 * h = f * 121666
 *
 * Can overlap h with f.
 *
 * Preconditions:
 *    |f| bounded by 1.1*2^26,1.1*2^25,1.1*2^26,1.1*2^25,etc.
 *
 * Postconditions:
 *    |h| bounded by 1.1*2^25,1.1*2^24,1.1*2^25,1.1*2^24,etc.
 */
static void fe_mul121666(fe h, fe f)
{
    int32_t f0 = f[0];
    int32_t f1 = f[1];
    int32_t f2 = f[2];
    int32_t f3 = f[3];
    int32_t f4 = f[4];
    int32_t f5 = f[5];
    int32_t f6 = f[6];
    int32_t f7 = f[7];
    int32_t f8 = f[8];
    int32_t f9 = f[9];
    int64_t h0 = f0 * (int64_t) 121666;
    int64_t h1 = f1 * (int64_t) 121666;
    int64_t h2 = f2 * (int64_t) 121666;
    int64_t h3 = f3 * (int64_t) 121666;
    int64_t h4 = f4 * (int64_t) 121666;
    int64_t h5 = f5 * (int64_t) 121666;
    int64_t h6 = f6 * (int64_t) 121666;
    int64_t h7 = f7 * (int64_t) 121666;
    int64_t h8 = f8 * (int64_t) 121666;
    int64_t h9 = f9 * (int64_t) 121666;
    int64_t carry0;
    int64_t carry1;
    int64_t carry2;
    int64_t carry3;
    int64_t carry4;
    int64_t carry5;
    int64_t carry6;
    int64_t carry7;
    int64_t carry8;
    int64_t carry9;

    carry9 = h9 + (1 << 24); h0 += (carry9 >> 25) * 19; h9 -= carry9 & kTop39Bits;
    carry1 = h1 + (1 << 24); h2 += carry1 >> 25; h1 -= carry1 & kTop39Bits;
    carry3 = h3 + (1 << 24); h4 += carry3 >> 25; h3 -= carry3 & kTop39Bits;
    carry5 = h5 + (1 << 24); h6 += carry5 >> 25; h5 -= carry5 & kTop39Bits;
    carry7 = h7 + (1 << 24); h8 += carry7 >> 25; h7 -= carry7 & kTop39Bits;

    carry0 = h0 + (1 << 25); h1 += carry0 >> 26; h0 -= carry0 & kTop38Bits;
    carry2 = h2 + (1 << 25); h3 += carry2 >> 26; h2 -= carry2 & kTop38Bits;
    carry4 = h4 + (1 << 25); h5 += carry4 >> 26; h4 -= carry4 & kTop38Bits;
    carry6 = h6 + (1 << 25); h7 += carry6 >> 26; h6 -= carry6 & kTop38Bits;
    carry8 = h8 + (1 << 25); h9 += carry8 >> 26; h8 -= carry8 & kTop38Bits;

    h[0] = (int32_t)h0;
    h[1] = (int32_t)h1;
    h[2] = (int32_t)h2;
    h[3] = (int32_t)h3;
    h[4] = (int32_t)h4;
    h[5] = (int32_t)h5;
    h[6] = (int32_t)h6;
    h[7] = (int32_t)h7;
    h[8] = (int32_t)h8;
    h[9] = (int32_t)h9;
}

static void x25519_scalar_mult_generic(uint8_t out[32],
                                       const uint8_t scalar[32],
                                       const uint8_t point[32]) {
    fe x1, x2, z2, x3, z3, tmp0, tmp1;
    uint8_t e[32];
    unsigned swap = 0;
    int pos;

    memcpy(e, scalar, 32);
    e[0] &= 248;
    e[31] &= 127;
    e[31] |= 64;
    fe_frombytes(x1, point);
    fe_1(x2);
    fe_0(z2);
    fe_copy(x3, x1);
    fe_1(z3);

    for (pos = 254; pos >= 0; --pos) {
        unsigned b = 1 & (e[pos / 8] >> (pos & 7));
        swap ^= b;
        fe_cswap(x2, x3, swap);
        fe_cswap(z2, z3, swap);
        swap = b;
        fe_sub(tmp0, x3, z3);
        fe_sub(tmp1, x2, z2);
        fe_add(x2, x2, z2);
        fe_add(z2, x3, z3);
        fe_mul(z3, tmp0, x2);
        fe_mul(z2, z2, tmp1);
        fe_sq(tmp0, tmp1);
        fe_sq(tmp1, x2);
        fe_add(x3, z3, z2);
        fe_sub(z2, z3, z2);
        fe_mul(x2, tmp1, tmp0);
        fe_sub(tmp1, tmp1, tmp0);
        fe_sq(z2, z2);
        fe_mul121666(z3, tmp1);
        fe_sq(x3, x3);
        fe_add(tmp0, tmp0, z3);
        fe_mul(z3, x1, z2);
        fe_mul(z2, tmp1, tmp0);
    }

    fe_invert(z2, z2);
    fe_mul(x2, x2, z2);
    fe_tobytes(out, x2);

    OPENSSL_cleanse(e, sizeof(e));
}

static void x25519_scalar_mult(uint8_t out[32], const uint8_t scalar[32],
                               const uint8_t point[32]) {
    x25519_scalar_mult_generic(out, scalar, point);
}
#endif

static void slide(signed char *r, const uint8_t *a)
{
    int i;
    int b;
    int k;

    for (i = 0; i < 256; ++i) {
        r[i] = 1 & (a[i >> 3] >> (i & 7));
    }

    for (i = 0; i < 256; ++i) {
        if (r[i]) {
            for (b = 1; b <= 6 && i + b < 256; ++b) {
                if (r[i + b]) {
                    if (r[i] + (r[i + b] << b) <= 15) {
                        r[i] += r[i + b] << b;
                        r[i + b] = 0;
                    } else if (r[i] - (r[i + b] << b) >= -15) {
                        r[i] -= r[i + b] << b;
                        for (k = i + b; k < 256; ++k) {
                            if (!r[k]) {
                                r[k] = 1;
                                break;
                            }
                            r[k] = 0;
                        }
                    } else {
                        break;
                    }
                }
            }
        }
    }
}

static const ge_precomp Bi[8] = {
    {
        {25967493, -14356035, 29566456, 3660896, -12694345, 4014787, 27544626,
         -11754271, -6079156, 2047605},
        {-12545711, 934262, -2722910, 3049990, -727428, 9406986, 12720692,
         5043384, 19500929, -15469378},
        {-8738181, 4489570, 9688441, -14785194, 10184609, -12363380, 29287919,
         11864899, -24514362, -4438546},
    },
    {
        {15636291, -9688557, 24204773, -7912398, 616977, -16685262, 27787600,
         -14772189, 28944400, -1550024},
        {16568933, 4717097, -11556148, -1102322, 15682896, -11807043, 16354577,
         -11775962, 7689662, 11199574},
        {30464156, -5976125, -11779434, -15670865, 23220365, 15915852, 7512774,
         10017326, -17749093, -9920357},
    },
    {
        {10861363, 11473154, 27284546, 1981175, -30064349, 12577861, 32867885,
         14515107, -15438304, 10819380},
        {4708026, 6336745, 20377586, 9066809, -11272109, 6594696, -25653668,
         12483688, -12668491, 5581306},
        {19563160, 16186464, -29386857, 4097519, 10237984, -4348115, 28542350,
         13850243, -23678021, -15815942},
    },
    {
        {5153746, 9909285, 1723747, -2777874, 30523605, 5516873, 19480852,
         5230134, -23952439, -15175766},
        {-30269007, -3463509, 7665486, 10083793, 28475525, 1649722, 20654025,
         16520125, 30598449, 7715701},
        {28881845, 14381568, 9657904, 3680757, -20181635, 7843316, -31400660,
         1370708, 29794553, -1409300},
    },
    {
        {-22518993, -6692182, 14201702, -8745502, -23510406, 8844726, 18474211,
         -1361450, -13062696, 13821877},
        {-6455177, -7839871, 3374702, -4740862, -27098617, -10571707, 31655028,
         -7212327, 18853322, -14220951},
        {4566830, -12963868, -28974889, -12240689, -7602672, -2830569, -8514358,
         -10431137, 2207753, -3209784},
    },
    {
        {-25154831, -4185821, 29681144, 7868801, -6854661, -9423865, -12437364,
         -663000, -31111463, -16132436},
        {25576264, -2703214, 7349804, -11814844, 16472782, 9300885, 3844789,
         15725684, 171356, 6466918},
        {23103977, 13316479, 9739013, -16149481, 817875, -15038942, 8965339,
         -14088058, -30714912, 16193877},
    },
    {
        {-33521811, 3180713, -2394130, 14003687, -16903474, -16270840, 17238398,
         4729455, -18074513, 9256800},
        {-25182317, -4174131, 32336398, 5036987, -21236817, 11360617, 22616405,
         9761698, -19827198, 630305},
        {-13720693, 2639453, -24237460, -7406481, 9494427, -5774029, -6554551,
         -15960994, -2449256, -14291300},
    },
    {
        {-3151181, -5046075, 9282714, 6866145, -31907062, -863023, -18940575,
         15033784, 25105118, -7894876},
        {-24326370, 15950226, -31801215, -14592823, -11662737, -5090925,
         1573892, -2625887, 2198790, -15804619},
        {-3099351, 10324967, -2241613, 7453183, -5446979, -2735503, -13812022,
         -16236442, -32461234, -12290683},
    },
};

/*
 * r = a * A + b * B
 *
 * where a = a[0]+256*a[1]+...+256^31 a[31].
 * and b = b[0]+256*b[1]+...+256^31 b[31].
 * B is the Ed25519 base point (x,4/5) with x positive.
 */
static void ge_double_scalarmult_vartime(ge_p2 *r, const uint8_t *a,
                                         const ge_p3 *A, const uint8_t *b)
{
    signed char aslide[256];
    signed char bslide[256];
    ge_cached Ai[8]; /* A,3A,5A,7A,9A,11A,13A,15A */
    ge_p1p1 t;
    ge_p3 u;
    ge_p3 A2;
    int i;

    slide(aslide, a);
    slide(bslide, b);

    ge_p3_to_cached(&Ai[0], A);
    ge_p3_dbl(&t, A);
    ge_p1p1_to_p3(&A2, &t);
    ge_add(&t, &A2, &Ai[0]);
    ge_p1p1_to_p3(&u, &t);
    ge_p3_to_cached(&Ai[1], &u);
    ge_add(&t, &A2, &Ai[1]);
    ge_p1p1_to_p3(&u, &t);
    ge_p3_to_cached(&Ai[2], &u);
    ge_add(&t, &A2, &Ai[2]);
    ge_p1p1_to_p3(&u, &t);
    ge_p3_to_cached(&Ai[3], &u);
    ge_add(&t, &A2, &Ai[3]);
    ge_p1p1_to_p3(&u, &t);
    ge_p3_to_cached(&Ai[4], &u);
    ge_add(&t, &A2, &Ai[4]);
    ge_p1p1_to_p3(&u, &t);
    ge_p3_to_cached(&Ai[5], &u);
    ge_add(&t, &A2, &Ai[5]);
    ge_p1p1_to_p3(&u, &t);
    ge_p3_to_cached(&Ai[6], &u);
    ge_add(&t, &A2, &Ai[6]);
    ge_p1p1_to_p3(&u, &t);
    ge_p3_to_cached(&Ai[7], &u);

    ge_p2_0(r);

    for (i = 255; i >= 0; --i) {
        if (aslide[i] || bslide[i]) {
            break;
        }
    }

    for (; i >= 0; --i) {
        ge_p2_dbl(&t, r);

        if (aslide[i] > 0) {
            ge_p1p1_to_p3(&u, &t);
            ge_add(&t, &u, &Ai[aslide[i] / 2]);
        } else if (aslide[i] < 0) {
            ge_p1p1_to_p3(&u, &t);
            ge_sub(&t, &u, &Ai[(-aslide[i]) / 2]);
        }

        if (bslide[i] > 0) {
            ge_p1p1_to_p3(&u, &t);
            ge_madd(&t, &u, &Bi[bslide[i] / 2]);
        } else if (bslide[i] < 0) {
            ge_p1p1_to_p3(&u, &t);
            ge_msub(&t, &u, &Bi[(-bslide[i]) / 2]);
        }

        ge_p1p1_to_p2(r, &t);
    }
}

/*
 * The set of scalars is \Z/l
 * where l = 2^252 + 27742317777372353535851937790883648493.
 *
 * Input:
 *   s[0]+256*s[1]+...+256^63*s[63] = s
 *
 * Output:
 *   s[0]+256*s[1]+...+256^31*s[31] = s mod l
 *   where l = 2^252 + 27742317777372353535851937790883648493.
 *   Overwrites s in place.
*/
static void x25519_sc_reduce(uint8_t *s)
{
    int64_t s0  = kBottom21Bits &  load_3(s);
    int64_t s1  = kBottom21Bits & (load_4(s +  2) >> 5);
    int64_t s2  = kBottom21Bits & (load_3(s +  5) >> 2);
    int64_t s3  = kBottom21Bits & (load_4(s +  7) >> 7);
    int64_t s4  = kBottom21Bits & (load_4(s + 10) >> 4);
    int64_t s5  = kBottom21Bits & (load_3(s + 13) >> 1);
    int64_t s6  = kBottom21Bits & (load_4(s + 15) >> 6);
    int64_t s7  = kBottom21Bits & (load_3(s + 18) >> 3);
    int64_t s8  = kBottom21Bits &  load_3(s + 21);
    int64_t s9  = kBottom21Bits & (load_4(s + 23) >> 5);
    int64_t s10 = kBottom21Bits & (load_3(s + 26) >> 2);
    int64_t s11 = kBottom21Bits & (load_4(s + 28) >> 7);
    int64_t s12 = kBottom21Bits & (load_4(s + 31) >> 4);
    int64_t s13 = kBottom21Bits & (load_3(s + 34) >> 1);
    int64_t s14 = kBottom21Bits & (load_4(s + 36) >> 6);
    int64_t s15 = kBottom21Bits & (load_3(s + 39) >> 3);
    int64_t s16 = kBottom21Bits &  load_3(s + 42);
    int64_t s17 = kBottom21Bits & (load_4(s + 44) >> 5);
    int64_t s18 = kBottom21Bits & (load_3(s + 47) >> 2);
    int64_t s19 = kBottom21Bits & (load_4(s + 49) >> 7);
    int64_t s20 = kBottom21Bits & (load_4(s + 52) >> 4);
    int64_t s21 = kBottom21Bits & (load_3(s + 55) >> 1);
    int64_t s22 = kBottom21Bits & (load_4(s + 57) >> 6);
    int64_t s23 =                 (load_4(s + 60) >> 3);
    int64_t carry0;
    int64_t carry1;
    int64_t carry2;
    int64_t carry3;
    int64_t carry4;
    int64_t carry5;
    int64_t carry6;
    int64_t carry7;
    int64_t carry8;
    int64_t carry9;
    int64_t carry10;
    int64_t carry11;
    int64_t carry12;
    int64_t carry13;
    int64_t carry14;
    int64_t carry15;
    int64_t carry16;

    s11 += s23 * 666643;
    s12 += s23 * 470296;
    s13 += s23 * 654183;
    s14 -= s23 * 997805;
    s15 += s23 * 136657;
    s16 -= s23 * 683901;
    s23  = 0;

    s10 += s22 * 666643;
    s11 += s22 * 470296;
    s12 += s22 * 654183;
    s13 -= s22 * 997805;
    s14 += s22 * 136657;
    s15 -= s22 * 683901;
    s22  = 0;

    s9  += s21 * 666643;
    s10 += s21 * 470296;
    s11 += s21 * 654183;
    s12 -= s21 * 997805;
    s13 += s21 * 136657;
    s14 -= s21 * 683901;
    s21  = 0;

    s8  += s20 * 666643;
    s9  += s20 * 470296;
    s10 += s20 * 654183;
    s11 -= s20 * 997805;
    s12 += s20 * 136657;
    s13 -= s20 * 683901;
    s20  = 0;

    s7  += s19 * 666643;
    s8  += s19 * 470296;
    s9  += s19 * 654183;
    s10 -= s19 * 997805;
    s11 += s19 * 136657;
    s12 -= s19 * 683901;
    s19  = 0;

    s6  += s18 * 666643;
    s7  += s18 * 470296;
    s8  += s18 * 654183;
    s9  -= s18 * 997805;
    s10 += s18 * 136657;
    s11 -= s18 * 683901;
    s18  = 0;

    carry6 = (s6 + (1 << 20)) >> 21;
    s7  += carry6;
    s6  -= carry6 * (1 << 21);
    carry8 = (s8 + (1 << 20)) >> 21;
    s9  += carry8;
    s8  -= carry8 * (1 << 21);
    carry10 = (s10 + (1 << 20)) >> 21;
    s11 += carry10;
    s10 -= carry10 * (1 << 21);
    carry12 = (s12 + (1 << 20)) >> 21;
    s13 += carry12;
    s12 -= carry12 * (1 << 21);
    carry14 = (s14 + (1 << 20)) >> 21;
    s15 += carry14;
    s14 -= carry14 * (1 << 21);
    carry16 = (s16 + (1 << 20)) >> 21;
    s17 += carry16;
    s16 -= carry16 * (1 << 21);

    carry7 = (s7 + (1 << 20)) >> 21;
    s8  += carry7;
    s7  -= carry7 * (1 << 21);
    carry9 = (s9 + (1 << 20)) >> 21;
    s10 += carry9;
    s9  -= carry9 * (1 << 21);
    carry11 = (s11 + (1 << 20)) >> 21;
    s12 += carry11;
    s11 -= carry11 * (1 << 21);
    carry13 = (s13 + (1 << 20)) >> 21;
    s14 += carry13;
    s13 -= carry13 * (1 << 21);
    carry15 = (s15 + (1 << 20)) >> 21;
    s16 += carry15;
    s15 -= carry15 * (1 << 21);

    s5  += s17 * 666643;
    s6  += s17 * 470296;
    s7  += s17 * 654183;
    s8  -= s17 * 997805;
    s9  += s17 * 136657;
    s10 -= s17 * 683901;
    s17  = 0;

    s4  += s16 * 666643;
    s5  += s16 * 470296;
    s6  += s16 * 654183;
    s7  -= s16 * 997805;
    s8  += s16 * 136657;
    s9  -= s16 * 683901;
    s16  = 0;

    s3  += s15 * 666643;
    s4  += s15 * 470296;
    s5  += s15 * 654183;
    s6  -= s15 * 997805;
    s7  += s15 * 136657;
    s8  -= s15 * 683901;
    s15  = 0;

    s2  += s14 * 666643;
    s3  += s14 * 470296;
    s4  += s14 * 654183;
    s5  -= s14 * 997805;
    s6  += s14 * 136657;
    s7  -= s14 * 683901;
    s14  = 0;

    s1  += s13 * 666643;
    s2  += s13 * 470296;
    s3  += s13 * 654183;
    s4  -= s13 * 997805;
    s5  += s13 * 136657;
    s6  -= s13 * 683901;
    s13  = 0;

    s0  += s12 * 666643;
    s1  += s12 * 470296;
    s2  += s12 * 654183;
    s3  -= s12 * 997805;
    s4  += s12 * 136657;
    s5  -= s12 * 683901;
    s12  = 0;

    carry0 = (s0 + (1 << 20)) >> 21;
    s1  += carry0;
    s0  -= carry0 * (1 << 21);
    carry2 = (s2 + (1 << 20)) >> 21;
    s3  += carry2;
    s2  -= carry2 * (1 << 21);
    carry4 = (s4 + (1 << 20)) >> 21;
    s5  += carry4;
    s4  -= carry4 * (1 << 21);
    carry6 = (s6 + (1 << 20)) >> 21;
    s7 += carry6;
    s6 -= carry6 * (1 << 21);
    carry8 = (s8 + (1 << 20)) >> 21;
    s9  += carry8;
    s8  -= carry8 * (1 << 21);
    carry10 = (s10 + (1 << 20)) >> 21;
    s11 += carry10;
    s10 -= carry10 * (1 << 21);

    carry1 = (s1 + (1 << 20)) >> 21;
    s2  += carry1;
    s1  -= carry1 * (1 << 21);
    carry3 = (s3 + (1 << 20)) >> 21;
    s4  += carry3;
    s3  -= carry3 * (1 << 21);
    carry5 = (s5 + (1 << 20)) >> 21;
    s6  += carry5;
    s5  -= carry5 * (1 << 21);
    carry7 = (s7 + (1 << 20)) >> 21;
    s8  += carry7;
    s7  -= carry7 * (1 << 21);
    carry9 = (s9 + (1 << 20)) >> 21;
    s10 += carry9;
    s9  -= carry9 * (1 << 21);
    carry11 = (s11 + (1 << 20)) >> 21;
    s12 += carry11;
    s11 -= carry11 * (1 << 21);

    s0  += s12 * 666643;
    s1  += s12 * 470296;
    s2  += s12 * 654183;
    s3  -= s12 * 997805;
    s4  += s12 * 136657;
    s5  -= s12 * 683901;
    s12  = 0;

    carry0 = s0 >> 21;
    s1  += carry0;
    s0  -= carry0 * (1 << 21);
    carry1 = s1 >> 21;
    s2  += carry1;
    s1  -= carry1 * (1 << 21);
    carry2 = s2 >> 21;
    s3  += carry2;
    s2  -= carry2 * (1 << 21);
    carry3 = s3 >> 21;
    s4  += carry3;
    s3  -= carry3 * (1 << 21);
    carry4 = s4 >> 21;
    s5  += carry4;
    s4  -= carry4 * (1 << 21);
    carry5 = s5 >> 21;
    s6  += carry5;
    s5  -= carry5 * (1 << 21);
    carry6 = s6 >> 21;
    s7  += carry6;
    s6  -= carry6 * (1 << 21);
    carry7 = s7 >> 21;
    s8  += carry7;
    s7  -= carry7 * (1 << 21);
    carry8 = s8 >> 21;
    s9  += carry8;
    s8  -= carry8 * (1 << 21);
    carry9 = s9 >> 21;
    s10 += carry9;
    s9  -= carry9 * (1 << 21);
    carry10 = s10 >> 21;
    s11 += carry10;
    s10 -= carry10 * (1 << 21);
    carry11 = s11 >> 21;
    s12 += carry11;
    s11 -= carry11 * (1 << 21);

    s0  += s12 * 666643;
    s1  += s12 * 470296;
    s2  += s12 * 654183;
    s3  -= s12 * 997805;
    s4  += s12 * 136657;
    s5  -= s12 * 683901;
    s12  = 0;

    carry0 = s0 >> 21;
    s1  += carry0;
    s0  -= carry0 * (1 << 21);
    carry1 = s1 >> 21;
    s2  += carry1;
    s1  -= carry1 * (1 << 21);
    carry2 = s2 >> 21;
    s3  += carry2;
    s2  -= carry2 * (1 << 21);
    carry3 = s3 >> 21;
    s4  += carry3;
    s3  -= carry3 * (1 << 21);
    carry4 = s4 >> 21;
    s5  += carry4;
    s4  -= carry4 * (1 << 21);
    carry5 = s5 >> 21;
    s6  += carry5;
    s5  -= carry5 * (1 << 21);
    carry6 = s6 >> 21;
    s7  += carry6;
    s6  -= carry6 * (1 << 21);
    carry7 = s7 >> 21;
    s8  += carry7;
    s7  -= carry7 * (1 << 21);
    carry8 = s8 >> 21;
    s9  += carry8;
    s8  -= carry8 * (1 << 21);
    carry9 = s9 >> 21;
    s10 += carry9;
    s9  -= carry9 * (1 << 21);
    carry10 = s10 >> 21;
    s11 += carry10;
    s10 -= carry10 * (1 << 21);

    s[ 0] = (uint8_t) (s0  >>  0);
    s[ 1] = (uint8_t) (s0  >>  8);
    s[ 2] = (uint8_t)((s0  >> 16) | (s1  <<  5));
    s[ 3] = (uint8_t) (s1  >>  3);
    s[ 4] = (uint8_t) (s1  >> 11);
    s[ 5] = (uint8_t)((s1  >> 19) | (s2  <<  2));
    s[ 6] = (uint8_t) (s2  >>  6);
    s[ 7] = (uint8_t)((s2  >> 14) | (s3  <<  7));
    s[ 8] = (uint8_t) (s3  >>  1);
    s[ 9] = (uint8_t) (s3  >>  9);
    s[10] = (uint8_t)((s3  >> 17) | (s4  <<  4));
    s[11] = (uint8_t) (s4  >>  4);
    s[12] = (uint8_t) (s4  >> 12);
    s[13] = (uint8_t)((s4  >> 20) | (s5  <<  1));
    s[14] = (uint8_t) (s5  >>  7);
    s[15] = (uint8_t)((s5  >> 15) | (s6  <<  6));
    s[16] = (uint8_t) (s6  >>  2);
    s[17] = (uint8_t) (s6  >> 10);
    s[18] = (uint8_t)((s6  >> 18) | (s7  <<  3));
    s[19] = (uint8_t) (s7  >>  5);
    s[20] = (uint8_t) (s7  >> 13);
    s[21] = (uint8_t) (s8  >>  0);
    s[22] = (uint8_t) (s8  >>  8);
    s[23] = (uint8_t)((s8  >> 16) | (s9  <<  5));
    s[24] = (uint8_t) (s9  >>  3);
    s[25] = (uint8_t) (s9  >> 11);
    s[26] = (uint8_t)((s9  >> 19) | (s10 <<  2));
    s[27] = (uint8_t) (s10 >>  6);
    s[28] = (uint8_t)((s10 >> 14) | (s11 <<  7));
    s[29] = (uint8_t) (s11 >>  1);
    s[30] = (uint8_t) (s11 >>  9);
    s[31] = (uint8_t) (s11 >> 17);
}

/*
 * Input:
 *   a[0]+256*a[1]+...+256^31*a[31] = a
 *   b[0]+256*b[1]+...+256^31*b[31] = b
 *   c[0]+256*c[1]+...+256^31*c[31] = c
 *
 * Output:
 *   s[0]+256*s[1]+...+256^31*s[31] = (ab+c) mod l
 *   where l = 2^252 + 27742317777372353535851937790883648493.
 */
static void sc_muladd(uint8_t *s, const uint8_t *a, const uint8_t *b,
                      const uint8_t *c)
{
    int64_t a0  = kBottom21Bits &  load_3(a);
    int64_t a1  = kBottom21Bits & (load_4(a +  2) >> 5);
    int64_t a2  = kBottom21Bits & (load_3(a +  5) >> 2);
    int64_t a3  = kBottom21Bits & (load_4(a +  7) >> 7);
    int64_t a4  = kBottom21Bits & (load_4(a + 10) >> 4);
    int64_t a5  = kBottom21Bits & (load_3(a + 13) >> 1);
    int64_t a6  = kBottom21Bits & (load_4(a + 15) >> 6);
    int64_t a7  = kBottom21Bits & (load_3(a + 18) >> 3);
    int64_t a8  = kBottom21Bits &  load_3(a + 21);
    int64_t a9  = kBottom21Bits & (load_4(a + 23) >> 5);
    int64_t a10 = kBottom21Bits & (load_3(a + 26) >> 2);
    int64_t a11 =                 (load_4(a + 28) >> 7);
    int64_t b0  = kBottom21Bits &  load_3(b);
    int64_t b1  = kBottom21Bits & (load_4(b +  2) >> 5);
    int64_t b2  = kBottom21Bits & (load_3(b +  5) >> 2);
    int64_t b3  = kBottom21Bits & (load_4(b +  7) >> 7);
    int64_t b4  = kBottom21Bits & (load_4(b + 10) >> 4);
    int64_t b5  = kBottom21Bits & (load_3(b + 13) >> 1);
    int64_t b6  = kBottom21Bits & (load_4(b + 15) >> 6);
    int64_t b7  = kBottom21Bits & (load_3(b + 18) >> 3);
    int64_t b8  = kBottom21Bits &  load_3(b + 21);
    int64_t b9  = kBottom21Bits & (load_4(b + 23) >> 5);
    int64_t b10 = kBottom21Bits & (load_3(b + 26) >> 2);
    int64_t b11 =                 (load_4(b + 28) >> 7);
    int64_t c0  = kBottom21Bits &  load_3(c);
    int64_t c1  = kBottom21Bits & (load_4(c +  2) >> 5);
    int64_t c2  = kBottom21Bits & (load_3(c +  5) >> 2);
    int64_t c3  = kBottom21Bits & (load_4(c +  7) >> 7);
    int64_t c4  = kBottom21Bits & (load_4(c + 10) >> 4);
    int64_t c5  = kBottom21Bits & (load_3(c + 13) >> 1);
    int64_t c6  = kBottom21Bits & (load_4(c + 15) >> 6);
    int64_t c7  = kBottom21Bits & (load_3(c + 18) >> 3);
    int64_t c8  = kBottom21Bits &  load_3(c + 21);
    int64_t c9  = kBottom21Bits & (load_4(c + 23) >> 5);
    int64_t c10 = kBottom21Bits & (load_3(c + 26) >> 2);
    int64_t c11 =                 (load_4(c + 28) >> 7);
    int64_t s0;
    int64_t s1;
    int64_t s2;
    int64_t s3;
    int64_t s4;
    int64_t s5;
    int64_t s6;
    int64_t s7;
    int64_t s8;
    int64_t s9;
    int64_t s10;
    int64_t s11;
    int64_t s12;
    int64_t s13;
    int64_t s14;
    int64_t s15;
    int64_t s16;
    int64_t s17;
    int64_t s18;
    int64_t s19;
    int64_t s20;
    int64_t s21;
    int64_t s22;
    int64_t s23;
    int64_t carry0;
    int64_t carry1;
    int64_t carry2;
    int64_t carry3;
    int64_t carry4;
    int64_t carry5;
    int64_t carry6;
    int64_t carry7;
    int64_t carry8;
    int64_t carry9;
    int64_t carry10;
    int64_t carry11;
    int64_t carry12;
    int64_t carry13;
    int64_t carry14;
    int64_t carry15;
    int64_t carry16;
    int64_t carry17;
    int64_t carry18;
    int64_t carry19;
    int64_t carry20;
    int64_t carry21;
    int64_t carry22;

    s0  = c0   +   a0 * b0;
    s1  = c1   +   a0 * b1   +   a1 * b0;
    s2  = c2   +   a0 * b2   +   a1 * b1   +   a2 * b0;
    s3  = c3   +   a0 * b3   +   a1 * b2   +   a2 * b1  +   a3 * b0;
    s4  = c4   +   a0 * b4   +   a1 * b3   +   a2 * b2  +   a3 * b1  +   a4 * b0;
    s5  = c5   +   a0 * b5   +   a1 * b4   +   a2 * b3  +   a3 * b2  +   a4 * b1  +   a5 * b0;
    s6  = c6   +   a0 * b6   +   a1 * b5   +   a2 * b4  +   a3 * b3  +   a4 * b2  +   a5 * b1 +   a6 * b0;
    s7  = c7   +   a0 * b7   +   a1 * b6   +   a2 * b5  +   a3 * b4  +   a4 * b3  +   a5 * b2 +   a6 * b1   +   a7 * b0;
    s8  = c8   +   a0 * b8   +   a1 * b7   +   a2 * b6  +   a3 * b5  +   a4 * b4  +   a5 * b3 +   a6 * b2   +   a7 * b1   +   a8 * b0;
    s9  = c9   +   a0 * b9   +   a1 * b8   +   a2 * b7  +   a3 * b6  +   a4 * b5  +   a5 * b4 +   a6 * b3   +   a7 * b2   +   a8 * b1  +   a9 * b0;
    s10 = c10  +   a0 * b10  +   a1 * b9   +   a2 * b8  +   a3 * b7  +   a4 * b6  +   a5 * b5 +   a6 * b4   +   a7 * b3   +   a8 * b2  +   a9 * b1  +  a10 * b0;
    s11 = c11  +   a0 * b11  +   a1 * b10  +   a2 * b9  +   a3 * b8  +   a4 * b7  +   a5 * b6 +   a6 * b5   +   a7 * b4   +   a8 * b3  +   a9 * b2  +  a10 * b1  +  a11 * b0;
    s12 =          a1 * b11  +   a2 * b10  +   a3 * b9  +   a4 * b8  +   a5 * b7  +   a6 * b6 +   a7 * b5   +   a8 * b4   +   a9 * b3  +  a10 * b2  +  a11 * b1;
    s13 =          a2 * b11  +   a3 * b10  +   a4 * b9  +   a5 * b8  +   a6 * b7  +   a7 * b6 +   a8 * b5   +   a9 * b4   +  a10 * b3  +  a11 * b2;
    s14 =          a3 * b11  +   a4 * b10  +   a5 * b9  +   a6 * b8  +   a7 * b7  +   a8 * b6 +   a9 * b5   +  a10 * b4   +  a11 * b3;
    s15 =          a4 * b11  +   a5 * b10  +   a6 * b9  +   a7 * b8  +   a8 * b7  +   a9 * b6 +  a10 * b5   +  a11 * b4;
    s16 =          a5 * b11  +   a6 * b10  +   a7 * b9  +   a8 * b8  +   a9 * b7  +  a10 * b6 +  a11 * b5;
    s17 =          a6 * b11  +   a7 * b10  +   a8 * b9  +   a9 * b8  +  a10 * b7  +  a11 * b6;
    s18 =          a7 * b11  +   a8 * b10  +   a9 * b9  +  a10 * b8  +  a11 * b7;
    s19 =          a8 * b11  +   a9 * b10  +  a10 * b9  +  a11 * b8;
    s20 =          a9 * b11  +  a10 * b10  +  a11 * b9;
    s21 =         a10 * b11  +  a11 * b10;
    s22 =         a11 * b11;
    s23 =         0;

    carry0 = (s0 + (1 << 20)) >> 21;
    s1  += carry0;
    s0  -= carry0 * (1 << 21);
    carry2 = (s2 + (1 << 20)) >> 21;
    s3  += carry2;
    s2  -= carry2 * (1 << 21);
    carry4 = (s4 + (1 << 20)) >> 21;
    s5  += carry4;
    s4  -= carry4 * (1 << 21);
    carry6 = (s6 + (1 << 20)) >> 21;
    s7  += carry6;
    s6  -= carry6 * (1 << 21);
    carry8 = (s8 + (1 << 20)) >> 21;
    s9  += carry8;
    s8  -= carry8 * (1 << 21);
    carry10 = (s10 + (1 << 20)) >> 21;
    s11 += carry10;
    s10 -= carry10 * (1 << 21);
    carry12 = (s12 + (1 << 20)) >> 21;
    s13 += carry12;
    s12 -= carry12 * (1 << 21);
    carry14 = (s14 + (1 << 20)) >> 21;
    s15 += carry14;
    s14 -= carry14 * (1 << 21);
    carry16 = (s16 + (1 << 20)) >> 21;
    s17 += carry16;
    s16 -= carry16 * (1 << 21);
    carry18 = (s18 + (1 << 20)) >> 21;
    s19 += carry18;
    s18 -= carry18 * (1 << 21);
    carry20 = (s20 + (1 << 20)) >> 21;
    s21 += carry20;
    s20 -= carry20 * (1 << 21);
    carry22 = (s22 + (1 << 20)) >> 21;
    s23 += carry22;
    s22 -= carry22 * (1 << 21);

    carry1 = (s1 + (1 << 20)) >> 21;
    s2  += carry1;
    s1  -= carry1 * (1 << 21);
    carry3 = (s3 + (1 << 20)) >> 21;
    s4  += carry3;
    s3  -= carry3 * (1 << 21);
    carry5 = (s5 + (1 << 20)) >> 21;
    s6  += carry5;
    s5  -= carry5 * (1 << 21);
    carry7 = (s7 + (1 << 20)) >> 21;
    s8  += carry7;
    s7  -= carry7 * (1 << 21);
    carry9 = (s9 + (1 << 20)) >> 21;
    s10 += carry9;
    s9  -= carry9 * (1 << 21);
    carry11 = (s11 + (1 << 20)) >> 21;
    s12 += carry11;
    s11 -= carry11 * (1 << 21);
    carry13 = (s13 + (1 << 20)) >> 21;
    s14 += carry13;
    s13 -= carry13 * (1 << 21);
    carry15 = (s15 + (1 << 20)) >> 21;
    s16 += carry15;
    s15 -= carry15 * (1 << 21);
    carry17 = (s17 + (1 << 20)) >> 21;
    s18 += carry17;
    s17 -= carry17 * (1 << 21);
    carry19 = (s19 + (1 << 20)) >> 21;
    s20 += carry19;
    s19 -= carry19 * (1 << 21);
    carry21 = (s21 + (1 << 20)) >> 21;
    s22 += carry21;
    s21 -= carry21 * (1 << 21);

    s11 += s23 * 666643;
    s12 += s23 * 470296;
    s13 += s23 * 654183;
    s14 -= s23 * 997805;
    s15 += s23 * 136657;
    s16 -= s23 * 683901;
    s23  = 0;

    s10 += s22 * 666643;
    s11 += s22 * 470296;
    s12 += s22 * 654183;
    s13 -= s22 * 997805;
    s14 += s22 * 136657;
    s15 -= s22 * 683901;
    s22  = 0;

    s9  += s21 * 666643;
    s10 += s21 * 470296;
    s11 += s21 * 654183;
    s12 -= s21 * 997805;
    s13 += s21 * 136657;
    s14 -= s21 * 683901;
    s21  = 0;

    s8  += s20 * 666643;
    s9  += s20 * 470296;
    s10 += s20 * 654183;
    s11 -= s20 * 997805;
    s12 += s20 * 136657;
    s13 -= s20 * 683901;
    s20  = 0;

    s7  += s19 * 666643;
    s8  += s19 * 470296;
    s9  += s19 * 654183;
    s10 -= s19 * 997805;
    s11 += s19 * 136657;
    s12 -= s19 * 683901;
    s19  = 0;

    s6  += s18 * 666643;
    s7  += s18 * 470296;
    s8  += s18 * 654183;
    s9  -= s18 * 997805;
    s10 += s18 * 136657;
    s11 -= s18 * 683901;
    s18  = 0;

    carry6 = (s6 + (1 << 20)) >> 21;
    s7  += carry6;
    s6  -= carry6 * (1 << 21);
    carry8 = (s8 + (1 << 20)) >> 21;
    s9  += carry8;
    s8  -= carry8 * (1 << 21);
    carry10 = (s10 + (1 << 20)) >> 21;
    s11 += carry10;
    s10 -= carry10 * (1 << 21);
    carry12 = (s12 + (1 << 20)) >> 21;
    s13 += carry12;
    s12 -= carry12 * (1 << 21);
    carry14 = (s14 + (1 << 20)) >> 21;
    s15 += carry14;
    s14 -= carry14 * (1 << 21);
    carry16 = (s16 + (1 << 20)) >> 21;
    s17 += carry16;
    s16 -= carry16 * (1 << 21);

    carry7 = (s7 + (1 << 20)) >> 21;
    s8  += carry7;
    s7  -= carry7 * (1 << 21);
    carry9 = (s9 + (1 << 20)) >> 21;
    s10 += carry9;
    s9  -= carry9 * (1 << 21);
    carry11 = (s11 + (1 << 20)) >> 21;
    s12 += carry11;
    s11 -= carry11 * (1 << 21);
    carry13 = (s13 + (1 << 20)) >> 21;
    s14 += carry13;
    s13 -= carry13 * (1 << 21);
    carry15 = (s15 + (1 << 20)) >> 21;
    s16 += carry15;
    s15 -= carry15 * (1 << 21);

    s5  += s17 * 666643;
    s6  += s17 * 470296;
    s7  += s17 * 654183;
    s8  -= s17 * 997805;
    s9  += s17 * 136657;
    s10 -= s17 * 683901;
    s17  = 0;

    s4  += s16 * 666643;
    s5  += s16 * 470296;
    s6  += s16 * 654183;
    s7  -= s16 * 997805;
    s8  += s16 * 136657;
    s9  -= s16 * 683901;
    s16  = 0;

    s3  += s15 * 666643;
    s4  += s15 * 470296;
    s5  += s15 * 654183;
    s6  -= s15 * 997805;
    s7  += s15 * 136657;
    s8  -= s15 * 683901;
    s15  = 0;

    s2  += s14 * 666643;
    s3  += s14 * 470296;
    s4  += s14 * 654183;
    s5  -= s14 * 997805;
    s6  += s14 * 136657;
    s7  -= s14 * 683901;
    s14  = 0;

    s1  += s13 * 666643;
    s2  += s13 * 470296;
    s3  += s13 * 654183;
    s4  -= s13 * 997805;
    s5  += s13 * 136657;
    s6  -= s13 * 683901;
    s13  = 0;

    s0  += s12 * 666643;
    s1  += s12 * 470296;
    s2  += s12 * 654183;
    s3  -= s12 * 997805;
    s4  += s12 * 136657;
    s5  -= s12 * 683901;
    s12 = 0;

    carry0 = (s0 + (1 << 20)) >> 21;
    s1  += carry0;
    s0  -= carry0 * (1 << 21);
    carry2 = (s2 + (1 << 20)) >> 21;
    s3  += carry2;
    s2  -= carry2 * (1 << 21);
    carry4 = (s4 + (1 << 20)) >> 21;
    s5  += carry4;
    s4  -= carry4 * (1 << 21);
    carry6 = (s6 + (1 << 20)) >> 21;
    s7  += carry6;
    s6  -= carry6 * (1 << 21);
    carry8 = (s8 + (1 << 20)) >> 21;
    s9  += carry8;
    s8  -= carry8 * (1 << 21);
    carry10 = (s10 + (1 << 20)) >> 21;
    s11 += carry10;
    s10 -= carry10 * (1 << 21);

    carry1 = (s1 + (1 << 20)) >> 21;
    s2  += carry1;
    s1  -= carry1 * (1 << 21);
    carry3 = (s3 + (1 << 20)) >> 21;
    s4  += carry3;
    s3  -= carry3 * (1 << 21);
    carry5 = (s5 + (1 << 20)) >> 21;
    s6  += carry5;
    s5  -= carry5 * (1 << 21);
    carry7 = (s7 + (1 << 20)) >> 21;
    s8  += carry7;
    s7  -= carry7 * (1 << 21);
    carry9 = (s9 + (1 << 20)) >> 21;
    s10 += carry9;
    s9  -= carry9 * (1 << 21);
    carry11 = (s11 + (1 << 20)) >> 21;
    s12 += carry11;
    s11 -= carry11 * (1 << 21);

    s0  += s12 * 666643;
    s1  += s12 * 470296;
    s2  += s12 * 654183;
    s3  -= s12 * 997805;
    s4  += s12 * 136657;
    s5  -= s12 * 683901;
    s12  = 0;

    carry0 = s0 >> 21;
    s1  += carry0;
    s0  -= carry0 * (1 << 21);
    carry1 = s1 >> 21;
    s2  += carry1;
    s1  -= carry1 * (1 << 21);
    carry2 = s2 >> 21;
    s3  += carry2;
    s2  -= carry2 * (1 << 21);
    carry3 = s3 >> 21;
    s4  += carry3;
    s3  -= carry3 * (1 << 21);
    carry4 = s4 >> 21;
    s5  += carry4;
    s4  -= carry4 * (1 << 21);
    carry5 = s5 >> 21;
    s6  += carry5;
    s5  -= carry5 * (1 << 21);
    carry6 = s6 >> 21;
    s7  += carry6;
    s6  -= carry6 * (1 << 21);
    carry7 = s7 >> 21;
    s8  += carry7;
    s7  -= carry7 * (1 << 21);
    carry8 = s8 >> 21;
    s9  += carry8;
    s8  -= carry8 * (1 << 21);
    carry9 = s9 >> 21;
    s10 += carry9;
    s9  -= carry9 * (1 << 21);
    carry10 = s10 >> 21;
    s11 += carry10;
    s10 -= carry10 * (1 << 21);
    carry11 = s11 >> 21;
    s12 += carry11;
    s11 -= carry11 * (1 << 21);

    s0  += s12 * 666643;
    s1  += s12 * 470296;
    s2  += s12 * 654183;
    s3  -= s12 * 997805;
    s4  += s12 * 136657;
    s5  -= s12 * 683901;
    s12  = 0;

    carry0 = s0 >> 21;
    s1  += carry0;
    s0  -= carry0 * (1 << 21);
    carry1 = s1 >> 21;
    s2  += carry1;
    s1  -= carry1 * (1 << 21);
    carry2 = s2 >> 21;
    s3  += carry2;
    s2  -= carry2 * (1 << 21);
    carry3 = s3 >> 21;
    s4  += carry3;
    s3  -= carry3 * (1 << 21);
    carry4 = s4 >> 21;
    s5  += carry4;
    s4  -= carry4 * (1 << 21);
    carry5 = s5 >> 21;
    s6  += carry5;
    s5  -= carry5 * (1 << 21);
    carry6 = s6 >> 21;
    s7  += carry6;
    s6  -= carry6 * (1 << 21);
    carry7 = s7 >> 21;
    s8  += carry7;
    s7  -= carry7 * (1 << 21);
    carry8 = s8 >> 21;
    s9  += carry8;
    s8  -= carry8 * (1 << 21);
    carry9 = s9 >> 21;
    s10 += carry9;
    s9  -= carry9 * (1 << 21);
    carry10 = s10 >> 21;
    s11 += carry10;
    s10 -= carry10 * (1 << 21);

    s[ 0] = (uint8_t) (s0  >>  0);
    s[ 1] = (uint8_t) (s0  >>  8);
    s[ 2] = (uint8_t)((s0  >> 16) | (s1 << 5));
    s[ 3] = (uint8_t) (s1  >>  3);
    s[ 4] = (uint8_t) (s1  >> 11);
    s[ 5] = (uint8_t)((s1  >> 19) | (s2 << 2));
    s[ 6] = (uint8_t) (s2  >>  6);
    s[ 7] = (uint8_t)((s2  >> 14) | (s3 << 7));
    s[ 8] = (uint8_t) (s3  >>  1);
    s[ 9] = (uint8_t) (s3  >>  9);
    s[10] = (uint8_t)((s3  >> 17) | (s4 << 4));
    s[11] = (uint8_t) (s4  >>  4);
    s[12] = (uint8_t) (s4  >> 12);
    s[13] = (uint8_t)((s4  >> 20) | (s5 << 1));
    s[14] = (uint8_t) (s5  >>  7);
    s[15] = (uint8_t)((s5  >> 15) | (s6 << 6));
    s[16] = (uint8_t) (s6  >>  2);
    s[17] = (uint8_t) (s6  >> 10);
    s[18] = (uint8_t)((s6  >> 18) | (s7 << 3));
    s[19] = (uint8_t) (s7  >>  5);
    s[20] = (uint8_t) (s7  >> 13);
    s[21] = (uint8_t) (s8  >>  0);
    s[22] = (uint8_t) (s8  >>  8);
    s[23] = (uint8_t)((s8  >> 16) | (s9 << 5));
    s[24] = (uint8_t) (s9  >>  3);
    s[25] = (uint8_t) (s9  >> 11);
    s[26] = (uint8_t)((s9  >> 19) | (s10 << 2));
    s[27] = (uint8_t) (s10 >>  6);
    s[28] = (uint8_t)((s10 >> 14) | (s11 << 7));
    s[29] = (uint8_t) (s11 >>  1);
    s[30] = (uint8_t) (s11 >>  9);
    s[31] = (uint8_t) (s11 >> 17);
}

int ED25519_sign(uint8_t *out_sig, const uint8_t *message, size_t message_len,
                 const uint8_t public_key[32], const uint8_t private_key[32])
{
    uint8_t az[SHA512_DIGEST_LENGTH];
    uint8_t nonce[SHA512_DIGEST_LENGTH];
    ge_p3 R;
    uint8_t hram[SHA512_DIGEST_LENGTH];
    SHA512_CTX hash_ctx;

    SHA512_Init(&hash_ctx);
    SHA512_Update(&hash_ctx, private_key, 32);
    SHA512_Final(az, &hash_ctx);

    az[0] &= 248;
    az[31] &= 63;
    az[31] |= 64;

    SHA512_Init(&hash_ctx);
    SHA512_Update(&hash_ctx, az + 32, 32);
    SHA512_Update(&hash_ctx, message, message_len);
    SHA512_Final(nonce, &hash_ctx);

    x25519_sc_reduce(nonce);
    ge_scalarmult_base(&R, nonce);
    ge_p3_tobytes(out_sig, &R);

    SHA512_Init(&hash_ctx);
    SHA512_Update(&hash_ctx, out_sig, 32);
    SHA512_Update(&hash_ctx, public_key, 32);
    SHA512_Update(&hash_ctx, message, message_len);
    SHA512_Final(hram, &hash_ctx);

    x25519_sc_reduce(hram);
    sc_muladd(out_sig + 32, hram, az, nonce);

    OPENSSL_cleanse(&hash_ctx, sizeof(hash_ctx));
    OPENSSL_cleanse(nonce, sizeof(nonce));
    OPENSSL_cleanse(az, sizeof(az));

    return 1;
}

static const char allzeroes[15];

int ED25519_verify(const uint8_t *message, size_t message_len,
                   const uint8_t signature[64], const uint8_t public_key[32])
{
    int i;
    ge_p3 A;
    const uint8_t *r, *s;
    SHA512_CTX hash_ctx;
    ge_p2 R;
    uint8_t rcheck[32];
    uint8_t h[SHA512_DIGEST_LENGTH];
    /* 27742317777372353535851937790883648493 in little endian format */
    const uint8_t l_low[16] = {
        0xED, 0xD3, 0xF5, 0x5C, 0x1A, 0x63, 0x12, 0x58, 0xD6, 0x9C, 0xF7, 0xA2,
        0xDE, 0xF9, 0xDE, 0x14
    };

    r = signature;
    s = signature + 32;

    /*
     * Check 0 <= s < L where L = 2^252 + 27742317777372353535851937790883648493
     *
     * If not the signature is publicly invalid. Since it's public we can do the
     * check in variable time.
     *
     * First check the most significant byte
     */
    if (s[31] > 0x10)
        return 0;
    if (s[31] == 0x10) {
        /*
         * Most significant byte indicates a value close to 2^252 so check the
         * rest
         */
        if (memcmp(s + 16, allzeroes, sizeof(allzeroes)) != 0)
            return 0;
        for (i = 15; i >= 0; i--) {
            if (s[i] < l_low[i])
                break;
            if (s[i] > l_low[i])
                return 0;
        }
        if (i < 0)
            return 0;
    }

    if (ge_frombytes_vartime(&A, public_key) != 0) {
        return 0;
    }

    fe_neg(A.X, A.X);
    fe_neg(A.T, A.T);

    SHA512_Init(&hash_ctx);
    SHA512_Update(&hash_ctx, r, 32);
    SHA512_Update(&hash_ctx, public_key, 32);
    SHA512_Update(&hash_ctx, message, message_len);
    SHA512_Final(h, &hash_ctx);

    x25519_sc_reduce(h);

    ge_double_scalarmult_vartime(&R, h, &A, s);

    ge_tobytes(rcheck, &R);

    return CRYPTO_memcmp(rcheck, r, sizeof(rcheck)) == 0;
}

void ED25519_public_from_private(uint8_t out_public_key[32],
                                 const uint8_t private_key[32])
{
    uint8_t az[SHA512_DIGEST_LENGTH];
    ge_p3 A;

    SHA512(private_key, 32, az);

    az[0] &= 248;
    az[31] &= 63;
    az[31] |= 64;

    ge_scalarmult_base(&A, az);
    ge_p3_tobytes(out_public_key, &A);

    OPENSSL_cleanse(az, sizeof(az));
}

int X25519(uint8_t out_shared_key[32], const uint8_t private_key[32],
           const uint8_t peer_public_value[32])
{
    static const uint8_t kZeros[32] = {0};
    x25519_scalar_mult(out_shared_key, private_key, peer_public_value);
    /* The all-zero output results when the input is a point of small order. */
    return CRYPTO_memcmp(kZeros, out_shared_key, 32) != 0;
}

void X25519_public_from_private(uint8_t out_public_value[32],
                                const uint8_t private_key[32])
{
    uint8_t e[32];
    ge_p3 A;
    fe zplusy, zminusy, zminusy_inv;

    memcpy(e, private_key, 32);
    e[0] &= 248;
    e[31] &= 127;
    e[31] |= 64;

    ge_scalarmult_base(&A, e);

    /*
     * We only need the u-coordinate of the curve25519 point.
     * The map is u=(y+1)/(1-y). Since y=Y/Z, this gives
     * u=(Z+Y)/(Z-Y).
     */
    fe_add(zplusy, A.Z, A.Y);
    fe_sub(zminusy, A.Z, A.Y);
    fe_invert(zminusy_inv, zminusy);
    fe_mul(zplusy, zplusy, zminusy_inv);
    fe_tobytes(out_public_value, zplusy);

    OPENSSL_cleanse(e, sizeof(e));
}
