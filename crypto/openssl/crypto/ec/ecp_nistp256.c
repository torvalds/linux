/*
 * Copyright 2011-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* Copyright 2011 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 *
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * A 64-bit implementation of the NIST P-256 elliptic curve point multiplication
 *
 * OpenSSL integration was taken from Emilia Kasper's work in ecp_nistp224.c.
 * Otherwise based on Emilia's P224 work, which was inspired by my curve25519
 * work which got its smarts from Daniel J. Bernstein's work on the same.
 */

#include <openssl/opensslconf.h>
#ifdef OPENSSL_NO_EC_NISTP_64_GCC_128
NON_EMPTY_TRANSLATION_UNIT
#else

# include <stdint.h>
# include <string.h>
# include <openssl/err.h>
# include "ec_lcl.h"

# if defined(__SIZEOF_INT128__) && __SIZEOF_INT128__==16
  /* even with gcc, the typedef won't work for 32-bit platforms */
typedef __uint128_t uint128_t;  /* nonstandard; implemented by gcc on 64-bit
                                 * platforms */
typedef __int128_t int128_t;
# else
#  error "Your compiler doesn't appear to support 128-bit integer types"
# endif

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

/*
 * The underlying field. P256 operates over GF(2^256-2^224+2^192+2^96-1). We
 * can serialise an element of this field into 32 bytes. We call this an
 * felem_bytearray.
 */

typedef u8 felem_bytearray[32];

/*
 * These are the parameters of P256, taken from FIPS 186-3, page 86. These
 * values are big-endian.
 */
static const felem_bytearray nistp256_curve_params[5] = {
    {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01, /* p */
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
    {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01, /* a = -3 */
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc}, /* b */
    {0x5a, 0xc6, 0x35, 0xd8, 0xaa, 0x3a, 0x93, 0xe7,
     0xb3, 0xeb, 0xbd, 0x55, 0x76, 0x98, 0x86, 0xbc,
     0x65, 0x1d, 0x06, 0xb0, 0xcc, 0x53, 0xb0, 0xf6,
     0x3b, 0xce, 0x3c, 0x3e, 0x27, 0xd2, 0x60, 0x4b},
    {0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47, /* x */
     0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2,
     0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0,
     0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96},
    {0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b, /* y */
     0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e, 0x16,
     0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce,
     0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5}
};

/*-
 * The representation of field elements.
 * ------------------------------------
 *
 * We represent field elements with either four 128-bit values, eight 128-bit
 * values, or four 64-bit values. The field element represented is:
 *   v[0]*2^0 + v[1]*2^64 + v[2]*2^128 + v[3]*2^192  (mod p)
 * or:
 *   v[0]*2^0 + v[1]*2^64 + v[2]*2^128 + ... + v[8]*2^512  (mod p)
 *
 * 128-bit values are called 'limbs'. Since the limbs are spaced only 64 bits
 * apart, but are 128-bits wide, the most significant bits of each limb overlap
 * with the least significant bits of the next.
 *
 * A field element with four limbs is an 'felem'. One with eight limbs is a
 * 'longfelem'
 *
 * A field element with four, 64-bit values is called a 'smallfelem'. Small
 * values are used as intermediate values before multiplication.
 */

# define NLIMBS 4

typedef uint128_t limb;
typedef limb felem[NLIMBS];
typedef limb longfelem[NLIMBS * 2];
typedef u64 smallfelem[NLIMBS];

/* This is the value of the prime as four 64-bit words, little-endian. */
static const u64 kPrime[4] =
    { 0xfffffffffffffffful, 0xffffffff, 0, 0xffffffff00000001ul };
static const u64 bottom63bits = 0x7ffffffffffffffful;

/*
 * bin32_to_felem takes a little-endian byte array and converts it into felem
 * form. This assumes that the CPU is little-endian.
 */
static void bin32_to_felem(felem out, const u8 in[32])
{
    out[0] = *((u64 *)&in[0]);
    out[1] = *((u64 *)&in[8]);
    out[2] = *((u64 *)&in[16]);
    out[3] = *((u64 *)&in[24]);
}

/*
 * smallfelem_to_bin32 takes a smallfelem and serialises into a little
 * endian, 32 byte array. This assumes that the CPU is little-endian.
 */
static void smallfelem_to_bin32(u8 out[32], const smallfelem in)
{
    *((u64 *)&out[0]) = in[0];
    *((u64 *)&out[8]) = in[1];
    *((u64 *)&out[16]) = in[2];
    *((u64 *)&out[24]) = in[3];
}

/* To preserve endianness when using BN_bn2bin and BN_bin2bn */
static void flip_endian(u8 *out, const u8 *in, unsigned len)
{
    unsigned i;
    for (i = 0; i < len; ++i)
        out[i] = in[len - 1 - i];
}

/* BN_to_felem converts an OpenSSL BIGNUM into an felem */
static int BN_to_felem(felem out, const BIGNUM *bn)
{
    felem_bytearray b_in;
    felem_bytearray b_out;
    unsigned num_bytes;

    /* BN_bn2bin eats leading zeroes */
    memset(b_out, 0, sizeof(b_out));
    num_bytes = BN_num_bytes(bn);
    if (num_bytes > sizeof(b_out)) {
        ECerr(EC_F_BN_TO_FELEM, EC_R_BIGNUM_OUT_OF_RANGE);
        return 0;
    }
    if (BN_is_negative(bn)) {
        ECerr(EC_F_BN_TO_FELEM, EC_R_BIGNUM_OUT_OF_RANGE);
        return 0;
    }
    num_bytes = BN_bn2bin(bn, b_in);
    flip_endian(b_out, b_in, num_bytes);
    bin32_to_felem(out, b_out);
    return 1;
}

/* felem_to_BN converts an felem into an OpenSSL BIGNUM */
static BIGNUM *smallfelem_to_BN(BIGNUM *out, const smallfelem in)
{
    felem_bytearray b_in, b_out;
    smallfelem_to_bin32(b_in, in);
    flip_endian(b_out, b_in, sizeof(b_out));
    return BN_bin2bn(b_out, sizeof(b_out), out);
}

/*-
 * Field operations
 * ----------------
 */

static void smallfelem_one(smallfelem out)
{
    out[0] = 1;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
}

static void smallfelem_assign(smallfelem out, const smallfelem in)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
}

static void felem_assign(felem out, const felem in)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
}

/* felem_sum sets out = out + in. */
static void felem_sum(felem out, const felem in)
{
    out[0] += in[0];
    out[1] += in[1];
    out[2] += in[2];
    out[3] += in[3];
}

/* felem_small_sum sets out = out + in. */
static void felem_small_sum(felem out, const smallfelem in)
{
    out[0] += in[0];
    out[1] += in[1];
    out[2] += in[2];
    out[3] += in[3];
}

/* felem_scalar sets out = out * scalar */
static void felem_scalar(felem out, const u64 scalar)
{
    out[0] *= scalar;
    out[1] *= scalar;
    out[2] *= scalar;
    out[3] *= scalar;
}

/* longfelem_scalar sets out = out * scalar */
static void longfelem_scalar(longfelem out, const u64 scalar)
{
    out[0] *= scalar;
    out[1] *= scalar;
    out[2] *= scalar;
    out[3] *= scalar;
    out[4] *= scalar;
    out[5] *= scalar;
    out[6] *= scalar;
    out[7] *= scalar;
}

# define two105m41m9 (((limb)1) << 105) - (((limb)1) << 41) - (((limb)1) << 9)
# define two105 (((limb)1) << 105)
# define two105m41p9 (((limb)1) << 105) - (((limb)1) << 41) + (((limb)1) << 9)

/* zero105 is 0 mod p */
static const felem zero105 =
    { two105m41m9, two105, two105m41p9, two105m41p9 };

/*-
 * smallfelem_neg sets |out| to |-small|
 * On exit:
 *   out[i] < out[i] + 2^105
 */
static void smallfelem_neg(felem out, const smallfelem small)
{
    /* In order to prevent underflow, we subtract from 0 mod p. */
    out[0] = zero105[0] - small[0];
    out[1] = zero105[1] - small[1];
    out[2] = zero105[2] - small[2];
    out[3] = zero105[3] - small[3];
}

/*-
 * felem_diff subtracts |in| from |out|
 * On entry:
 *   in[i] < 2^104
 * On exit:
 *   out[i] < out[i] + 2^105
 */
static void felem_diff(felem out, const felem in)
{
    /*
     * In order to prevent underflow, we add 0 mod p before subtracting.
     */
    out[0] += zero105[0];
    out[1] += zero105[1];
    out[2] += zero105[2];
    out[3] += zero105[3];

    out[0] -= in[0];
    out[1] -= in[1];
    out[2] -= in[2];
    out[3] -= in[3];
}

# define two107m43m11 (((limb)1) << 107) - (((limb)1) << 43) - (((limb)1) << 11)
# define two107 (((limb)1) << 107)
# define two107m43p11 (((limb)1) << 107) - (((limb)1) << 43) + (((limb)1) << 11)

/* zero107 is 0 mod p */
static const felem zero107 =
    { two107m43m11, two107, two107m43p11, two107m43p11 };

/*-
 * An alternative felem_diff for larger inputs |in|
 * felem_diff_zero107 subtracts |in| from |out|
 * On entry:
 *   in[i] < 2^106
 * On exit:
 *   out[i] < out[i] + 2^107
 */
static void felem_diff_zero107(felem out, const felem in)
{
    /*
     * In order to prevent underflow, we add 0 mod p before subtracting.
     */
    out[0] += zero107[0];
    out[1] += zero107[1];
    out[2] += zero107[2];
    out[3] += zero107[3];

    out[0] -= in[0];
    out[1] -= in[1];
    out[2] -= in[2];
    out[3] -= in[3];
}

/*-
 * longfelem_diff subtracts |in| from |out|
 * On entry:
 *   in[i] < 7*2^67
 * On exit:
 *   out[i] < out[i] + 2^70 + 2^40
 */
static void longfelem_diff(longfelem out, const longfelem in)
{
    static const limb two70m8p6 =
        (((limb) 1) << 70) - (((limb) 1) << 8) + (((limb) 1) << 6);
    static const limb two70p40 = (((limb) 1) << 70) + (((limb) 1) << 40);
    static const limb two70 = (((limb) 1) << 70);
    static const limb two70m40m38p6 =
        (((limb) 1) << 70) - (((limb) 1) << 40) - (((limb) 1) << 38) +
        (((limb) 1) << 6);
    static const limb two70m6 = (((limb) 1) << 70) - (((limb) 1) << 6);

    /* add 0 mod p to avoid underflow */
    out[0] += two70m8p6;
    out[1] += two70p40;
    out[2] += two70;
    out[3] += two70m40m38p6;
    out[4] += two70m6;
    out[5] += two70m6;
    out[6] += two70m6;
    out[7] += two70m6;

    /* in[i] < 7*2^67 < 2^70 - 2^40 - 2^38 + 2^6 */
    out[0] -= in[0];
    out[1] -= in[1];
    out[2] -= in[2];
    out[3] -= in[3];
    out[4] -= in[4];
    out[5] -= in[5];
    out[6] -= in[6];
    out[7] -= in[7];
}

# define two64m0 (((limb)1) << 64) - 1
# define two110p32m0 (((limb)1) << 110) + (((limb)1) << 32) - 1
# define two64m46 (((limb)1) << 64) - (((limb)1) << 46)
# define two64m32 (((limb)1) << 64) - (((limb)1) << 32)

/* zero110 is 0 mod p */
static const felem zero110 = { two64m0, two110p32m0, two64m46, two64m32 };

/*-
 * felem_shrink converts an felem into a smallfelem. The result isn't quite
 * minimal as the value may be greater than p.
 *
 * On entry:
 *   in[i] < 2^109
 * On exit:
 *   out[i] < 2^64
 */
static void felem_shrink(smallfelem out, const felem in)
{
    felem tmp;
    u64 a, b, mask;
    u64 high, low;
    static const u64 kPrime3Test = 0x7fffffff00000001ul; /* 2^63 - 2^32 + 1 */

    /* Carry 2->3 */
    tmp[3] = zero110[3] + in[3] + ((u64)(in[2] >> 64));
    /* tmp[3] < 2^110 */

    tmp[2] = zero110[2] + (u64)in[2];
    tmp[0] = zero110[0] + in[0];
    tmp[1] = zero110[1] + in[1];
    /* tmp[0] < 2**110, tmp[1] < 2^111, tmp[2] < 2**65 */

    /*
     * We perform two partial reductions where we eliminate the high-word of
     * tmp[3]. We don't update the other words till the end.
     */
    a = tmp[3] >> 64;           /* a < 2^46 */
    tmp[3] = (u64)tmp[3];
    tmp[3] -= a;
    tmp[3] += ((limb) a) << 32;
    /* tmp[3] < 2^79 */

    b = a;
    a = tmp[3] >> 64;           /* a < 2^15 */
    b += a;                     /* b < 2^46 + 2^15 < 2^47 */
    tmp[3] = (u64)tmp[3];
    tmp[3] -= a;
    tmp[3] += ((limb) a) << 32;
    /* tmp[3] < 2^64 + 2^47 */

    /*
     * This adjusts the other two words to complete the two partial
     * reductions.
     */
    tmp[0] += b;
    tmp[1] -= (((limb) b) << 32);

    /*
     * In order to make space in tmp[3] for the carry from 2 -> 3, we
     * conditionally subtract kPrime if tmp[3] is large enough.
     */
    high = (u64)(tmp[3] >> 64);
    /* As tmp[3] < 2^65, high is either 1 or 0 */
    high = 0 - high;
    /*-
     * high is:
     *   all ones   if the high word of tmp[3] is 1
     *   all zeros  if the high word of tmp[3] if 0
     */
    low = (u64)tmp[3];
    mask = 0 - (low >> 63);
    /*-
     * mask is:
     *   all ones   if the MSB of low is 1
     *   all zeros  if the MSB of low if 0
     */
    low &= bottom63bits;
    low -= kPrime3Test;
    /* if low was greater than kPrime3Test then the MSB is zero */
    low = ~low;
    low = 0 - (low >> 63);
    /*-
     * low is:
     *   all ones   if low was > kPrime3Test
     *   all zeros  if low was <= kPrime3Test
     */
    mask = (mask & low) | high;
    tmp[0] -= mask & kPrime[0];
    tmp[1] -= mask & kPrime[1];
    /* kPrime[2] is zero, so omitted */
    tmp[3] -= mask & kPrime[3];
    /* tmp[3] < 2**64 - 2**32 + 1 */

    tmp[1] += ((u64)(tmp[0] >> 64));
    tmp[0] = (u64)tmp[0];
    tmp[2] += ((u64)(tmp[1] >> 64));
    tmp[1] = (u64)tmp[1];
    tmp[3] += ((u64)(tmp[2] >> 64));
    tmp[2] = (u64)tmp[2];
    /* tmp[i] < 2^64 */

    out[0] = tmp[0];
    out[1] = tmp[1];
    out[2] = tmp[2];
    out[3] = tmp[3];
}

/* smallfelem_expand converts a smallfelem to an felem */
static void smallfelem_expand(felem out, const smallfelem in)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
}

/*-
 * smallfelem_square sets |out| = |small|^2
 * On entry:
 *   small[i] < 2^64
 * On exit:
 *   out[i] < 7 * 2^64 < 2^67
 */
static void smallfelem_square(longfelem out, const smallfelem small)
{
    limb a;
    u64 high, low;

    a = ((uint128_t) small[0]) * small[0];
    low = a;
    high = a >> 64;
    out[0] = low;
    out[1] = high;

    a = ((uint128_t) small[0]) * small[1];
    low = a;
    high = a >> 64;
    out[1] += low;
    out[1] += low;
    out[2] = high;

    a = ((uint128_t) small[0]) * small[2];
    low = a;
    high = a >> 64;
    out[2] += low;
    out[2] *= 2;
    out[3] = high;

    a = ((uint128_t) small[0]) * small[3];
    low = a;
    high = a >> 64;
    out[3] += low;
    out[4] = high;

    a = ((uint128_t) small[1]) * small[2];
    low = a;
    high = a >> 64;
    out[3] += low;
    out[3] *= 2;
    out[4] += high;

    a = ((uint128_t) small[1]) * small[1];
    low = a;
    high = a >> 64;
    out[2] += low;
    out[3] += high;

    a = ((uint128_t) small[1]) * small[3];
    low = a;
    high = a >> 64;
    out[4] += low;
    out[4] *= 2;
    out[5] = high;

    a = ((uint128_t) small[2]) * small[3];
    low = a;
    high = a >> 64;
    out[5] += low;
    out[5] *= 2;
    out[6] = high;
    out[6] += high;

    a = ((uint128_t) small[2]) * small[2];
    low = a;
    high = a >> 64;
    out[4] += low;
    out[5] += high;

    a = ((uint128_t) small[3]) * small[3];
    low = a;
    high = a >> 64;
    out[6] += low;
    out[7] = high;
}

/*-
 * felem_square sets |out| = |in|^2
 * On entry:
 *   in[i] < 2^109
 * On exit:
 *   out[i] < 7 * 2^64 < 2^67
 */
static void felem_square(longfelem out, const felem in)
{
    u64 small[4];
    felem_shrink(small, in);
    smallfelem_square(out, small);
}

/*-
 * smallfelem_mul sets |out| = |small1| * |small2|
 * On entry:
 *   small1[i] < 2^64
 *   small2[i] < 2^64
 * On exit:
 *   out[i] < 7 * 2^64 < 2^67
 */
static void smallfelem_mul(longfelem out, const smallfelem small1,
                           const smallfelem small2)
{
    limb a;
    u64 high, low;

    a = ((uint128_t) small1[0]) * small2[0];
    low = a;
    high = a >> 64;
    out[0] = low;
    out[1] = high;

    a = ((uint128_t) small1[0]) * small2[1];
    low = a;
    high = a >> 64;
    out[1] += low;
    out[2] = high;

    a = ((uint128_t) small1[1]) * small2[0];
    low = a;
    high = a >> 64;
    out[1] += low;
    out[2] += high;

    a = ((uint128_t) small1[0]) * small2[2];
    low = a;
    high = a >> 64;
    out[2] += low;
    out[3] = high;

    a = ((uint128_t) small1[1]) * small2[1];
    low = a;
    high = a >> 64;
    out[2] += low;
    out[3] += high;

    a = ((uint128_t) small1[2]) * small2[0];
    low = a;
    high = a >> 64;
    out[2] += low;
    out[3] += high;

    a = ((uint128_t) small1[0]) * small2[3];
    low = a;
    high = a >> 64;
    out[3] += low;
    out[4] = high;

    a = ((uint128_t) small1[1]) * small2[2];
    low = a;
    high = a >> 64;
    out[3] += low;
    out[4] += high;

    a = ((uint128_t) small1[2]) * small2[1];
    low = a;
    high = a >> 64;
    out[3] += low;
    out[4] += high;

    a = ((uint128_t) small1[3]) * small2[0];
    low = a;
    high = a >> 64;
    out[3] += low;
    out[4] += high;

    a = ((uint128_t) small1[1]) * small2[3];
    low = a;
    high = a >> 64;
    out[4] += low;
    out[5] = high;

    a = ((uint128_t) small1[2]) * small2[2];
    low = a;
    high = a >> 64;
    out[4] += low;
    out[5] += high;

    a = ((uint128_t) small1[3]) * small2[1];
    low = a;
    high = a >> 64;
    out[4] += low;
    out[5] += high;

    a = ((uint128_t) small1[2]) * small2[3];
    low = a;
    high = a >> 64;
    out[5] += low;
    out[6] = high;

    a = ((uint128_t) small1[3]) * small2[2];
    low = a;
    high = a >> 64;
    out[5] += low;
    out[6] += high;

    a = ((uint128_t) small1[3]) * small2[3];
    low = a;
    high = a >> 64;
    out[6] += low;
    out[7] = high;
}

/*-
 * felem_mul sets |out| = |in1| * |in2|
 * On entry:
 *   in1[i] < 2^109
 *   in2[i] < 2^109
 * On exit:
 *   out[i] < 7 * 2^64 < 2^67
 */
static void felem_mul(longfelem out, const felem in1, const felem in2)
{
    smallfelem small1, small2;
    felem_shrink(small1, in1);
    felem_shrink(small2, in2);
    smallfelem_mul(out, small1, small2);
}

/*-
 * felem_small_mul sets |out| = |small1| * |in2|
 * On entry:
 *   small1[i] < 2^64
 *   in2[i] < 2^109
 * On exit:
 *   out[i] < 7 * 2^64 < 2^67
 */
static void felem_small_mul(longfelem out, const smallfelem small1,
                            const felem in2)
{
    smallfelem small2;
    felem_shrink(small2, in2);
    smallfelem_mul(out, small1, small2);
}

# define two100m36m4 (((limb)1) << 100) - (((limb)1) << 36) - (((limb)1) << 4)
# define two100 (((limb)1) << 100)
# define two100m36p4 (((limb)1) << 100) - (((limb)1) << 36) + (((limb)1) << 4)
/* zero100 is 0 mod p */
static const felem zero100 =
    { two100m36m4, two100, two100m36p4, two100m36p4 };

/*-
 * Internal function for the different flavours of felem_reduce.
 * felem_reduce_ reduces the higher coefficients in[4]-in[7].
 * On entry:
 *   out[0] >= in[6] + 2^32*in[6] + in[7] + 2^32*in[7]
 *   out[1] >= in[7] + 2^32*in[4]
 *   out[2] >= in[5] + 2^32*in[5]
 *   out[3] >= in[4] + 2^32*in[5] + 2^32*in[6]
 * On exit:
 *   out[0] <= out[0] + in[4] + 2^32*in[5]
 *   out[1] <= out[1] + in[5] + 2^33*in[6]
 *   out[2] <= out[2] + in[7] + 2*in[6] + 2^33*in[7]
 *   out[3] <= out[3] + 2^32*in[4] + 3*in[7]
 */
static void felem_reduce_(felem out, const longfelem in)
{
    int128_t c;
    /* combine common terms from below */
    c = in[4] + (in[5] << 32);
    out[0] += c;
    out[3] -= c;

    c = in[5] - in[7];
    out[1] += c;
    out[2] -= c;

    /* the remaining terms */
    /* 256: [(0,1),(96,-1),(192,-1),(224,1)] */
    out[1] -= (in[4] << 32);
    out[3] += (in[4] << 32);

    /* 320: [(32,1),(64,1),(128,-1),(160,-1),(224,-1)] */
    out[2] -= (in[5] << 32);

    /* 384: [(0,-1),(32,-1),(96,2),(128,2),(224,-1)] */
    out[0] -= in[6];
    out[0] -= (in[6] << 32);
    out[1] += (in[6] << 33);
    out[2] += (in[6] * 2);
    out[3] -= (in[6] << 32);

    /* 448: [(0,-1),(32,-1),(64,-1),(128,1),(160,2),(192,3)] */
    out[0] -= in[7];
    out[0] -= (in[7] << 32);
    out[2] += (in[7] << 33);
    out[3] += (in[7] * 3);
}

/*-
 * felem_reduce converts a longfelem into an felem.
 * To be called directly after felem_square or felem_mul.
 * On entry:
 *   in[0] < 2^64, in[1] < 3*2^64, in[2] < 5*2^64, in[3] < 7*2^64
 *   in[4] < 7*2^64, in[5] < 5*2^64, in[6] < 3*2^64, in[7] < 2*64
 * On exit:
 *   out[i] < 2^101
 */
static void felem_reduce(felem out, const longfelem in)
{
    out[0] = zero100[0] + in[0];
    out[1] = zero100[1] + in[1];
    out[2] = zero100[2] + in[2];
    out[3] = zero100[3] + in[3];

    felem_reduce_(out, in);

    /*-
     * out[0] > 2^100 - 2^36 - 2^4 - 3*2^64 - 3*2^96 - 2^64 - 2^96 > 0
     * out[1] > 2^100 - 2^64 - 7*2^96 > 0
     * out[2] > 2^100 - 2^36 + 2^4 - 5*2^64 - 5*2^96 > 0
     * out[3] > 2^100 - 2^36 + 2^4 - 7*2^64 - 5*2^96 - 3*2^96 > 0
     *
     * out[0] < 2^100 + 2^64 + 7*2^64 + 5*2^96 < 2^101
     * out[1] < 2^100 + 3*2^64 + 5*2^64 + 3*2^97 < 2^101
     * out[2] < 2^100 + 5*2^64 + 2^64 + 3*2^65 + 2^97 < 2^101
     * out[3] < 2^100 + 7*2^64 + 7*2^96 + 3*2^64 < 2^101
     */
}

/*-
 * felem_reduce_zero105 converts a larger longfelem into an felem.
 * On entry:
 *   in[0] < 2^71
 * On exit:
 *   out[i] < 2^106
 */
static void felem_reduce_zero105(felem out, const longfelem in)
{
    out[0] = zero105[0] + in[0];
    out[1] = zero105[1] + in[1];
    out[2] = zero105[2] + in[2];
    out[3] = zero105[3] + in[3];

    felem_reduce_(out, in);

    /*-
     * out[0] > 2^105 - 2^41 - 2^9 - 2^71 - 2^103 - 2^71 - 2^103 > 0
     * out[1] > 2^105 - 2^71 - 2^103 > 0
     * out[2] > 2^105 - 2^41 + 2^9 - 2^71 - 2^103 > 0
     * out[3] > 2^105 - 2^41 + 2^9 - 2^71 - 2^103 - 2^103 > 0
     *
     * out[0] < 2^105 + 2^71 + 2^71 + 2^103 < 2^106
     * out[1] < 2^105 + 2^71 + 2^71 + 2^103 < 2^106
     * out[2] < 2^105 + 2^71 + 2^71 + 2^71 + 2^103 < 2^106
     * out[3] < 2^105 + 2^71 + 2^103 + 2^71 < 2^106
     */
}

/*
 * subtract_u64 sets *result = *result - v and *carry to one if the
 * subtraction underflowed.
 */
static void subtract_u64(u64 *result, u64 *carry, u64 v)
{
    uint128_t r = *result;
    r -= v;
    *carry = (r >> 64) & 1;
    *result = (u64)r;
}

/*
 * felem_contract converts |in| to its unique, minimal representation. On
 * entry: in[i] < 2^109
 */
static void felem_contract(smallfelem out, const felem in)
{
    unsigned i;
    u64 all_equal_so_far = 0, result = 0, carry;

    felem_shrink(out, in);
    /* small is minimal except that the value might be > p */

    all_equal_so_far--;
    /*
     * We are doing a constant time test if out >= kPrime. We need to compare
     * each u64, from most-significant to least significant. For each one, if
     * all words so far have been equal (m is all ones) then a non-equal
     * result is the answer. Otherwise we continue.
     */
    for (i = 3; i < 4; i--) {
        u64 equal;
        uint128_t a = ((uint128_t) kPrime[i]) - out[i];
        /*
         * if out[i] > kPrime[i] then a will underflow and the high 64-bits
         * will all be set.
         */
        result |= all_equal_so_far & ((u64)(a >> 64));

        /*
         * if kPrime[i] == out[i] then |equal| will be all zeros and the
         * decrement will make it all ones.
         */
        equal = kPrime[i] ^ out[i];
        equal--;
        equal &= equal << 32;
        equal &= equal << 16;
        equal &= equal << 8;
        equal &= equal << 4;
        equal &= equal << 2;
        equal &= equal << 1;
        equal = 0 - (equal >> 63);

        all_equal_so_far &= equal;
    }

    /*
     * if all_equal_so_far is still all ones then the two values are equal
     * and so out >= kPrime is true.
     */
    result |= all_equal_so_far;

    /* if out >= kPrime then we subtract kPrime. */
    subtract_u64(&out[0], &carry, result & kPrime[0]);
    subtract_u64(&out[1], &carry, carry);
    subtract_u64(&out[2], &carry, carry);
    subtract_u64(&out[3], &carry, carry);

    subtract_u64(&out[1], &carry, result & kPrime[1]);
    subtract_u64(&out[2], &carry, carry);
    subtract_u64(&out[3], &carry, carry);

    subtract_u64(&out[2], &carry, result & kPrime[2]);
    subtract_u64(&out[3], &carry, carry);

    subtract_u64(&out[3], &carry, result & kPrime[3]);
}

static void smallfelem_square_contract(smallfelem out, const smallfelem in)
{
    longfelem longtmp;
    felem tmp;

    smallfelem_square(longtmp, in);
    felem_reduce(tmp, longtmp);
    felem_contract(out, tmp);
}

static void smallfelem_mul_contract(smallfelem out, const smallfelem in1,
                                    const smallfelem in2)
{
    longfelem longtmp;
    felem tmp;

    smallfelem_mul(longtmp, in1, in2);
    felem_reduce(tmp, longtmp);
    felem_contract(out, tmp);
}

/*-
 * felem_is_zero returns a limb with all bits set if |in| == 0 (mod p) and 0
 * otherwise.
 * On entry:
 *   small[i] < 2^64
 */
static limb smallfelem_is_zero(const smallfelem small)
{
    limb result;
    u64 is_p;

    u64 is_zero = small[0] | small[1] | small[2] | small[3];
    is_zero--;
    is_zero &= is_zero << 32;
    is_zero &= is_zero << 16;
    is_zero &= is_zero << 8;
    is_zero &= is_zero << 4;
    is_zero &= is_zero << 2;
    is_zero &= is_zero << 1;
    is_zero = 0 - (is_zero >> 63);

    is_p = (small[0] ^ kPrime[0]) |
        (small[1] ^ kPrime[1]) |
        (small[2] ^ kPrime[2]) | (small[3] ^ kPrime[3]);
    is_p--;
    is_p &= is_p << 32;
    is_p &= is_p << 16;
    is_p &= is_p << 8;
    is_p &= is_p << 4;
    is_p &= is_p << 2;
    is_p &= is_p << 1;
    is_p = 0 - (is_p >> 63);

    is_zero |= is_p;

    result = is_zero;
    result |= ((limb) is_zero) << 64;
    return result;
}

static int smallfelem_is_zero_int(const void *small)
{
    return (int)(smallfelem_is_zero(small) & ((limb) 1));
}

/*-
 * felem_inv calculates |out| = |in|^{-1}
 *
 * Based on Fermat's Little Theorem:
 *   a^p = a (mod p)
 *   a^{p-1} = 1 (mod p)
 *   a^{p-2} = a^{-1} (mod p)
 */
static void felem_inv(felem out, const felem in)
{
    felem ftmp, ftmp2;
    /* each e_I will hold |in|^{2^I - 1} */
    felem e2, e4, e8, e16, e32, e64;
    longfelem tmp;
    unsigned i;

    felem_square(tmp, in);
    felem_reduce(ftmp, tmp);    /* 2^1 */
    felem_mul(tmp, in, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^2 - 2^0 */
    felem_assign(e2, ftmp);
    felem_square(tmp, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^3 - 2^1 */
    felem_square(tmp, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^4 - 2^2 */
    felem_mul(tmp, ftmp, e2);
    felem_reduce(ftmp, tmp);    /* 2^4 - 2^0 */
    felem_assign(e4, ftmp);
    felem_square(tmp, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^5 - 2^1 */
    felem_square(tmp, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^6 - 2^2 */
    felem_square(tmp, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^7 - 2^3 */
    felem_square(tmp, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^8 - 2^4 */
    felem_mul(tmp, ftmp, e4);
    felem_reduce(ftmp, tmp);    /* 2^8 - 2^0 */
    felem_assign(e8, ftmp);
    for (i = 0; i < 8; i++) {
        felem_square(tmp, ftmp);
        felem_reduce(ftmp, tmp);
    }                           /* 2^16 - 2^8 */
    felem_mul(tmp, ftmp, e8);
    felem_reduce(ftmp, tmp);    /* 2^16 - 2^0 */
    felem_assign(e16, ftmp);
    for (i = 0; i < 16; i++) {
        felem_square(tmp, ftmp);
        felem_reduce(ftmp, tmp);
    }                           /* 2^32 - 2^16 */
    felem_mul(tmp, ftmp, e16);
    felem_reduce(ftmp, tmp);    /* 2^32 - 2^0 */
    felem_assign(e32, ftmp);
    for (i = 0; i < 32; i++) {
        felem_square(tmp, ftmp);
        felem_reduce(ftmp, tmp);
    }                           /* 2^64 - 2^32 */
    felem_assign(e64, ftmp);
    felem_mul(tmp, ftmp, in);
    felem_reduce(ftmp, tmp);    /* 2^64 - 2^32 + 2^0 */
    for (i = 0; i < 192; i++) {
        felem_square(tmp, ftmp);
        felem_reduce(ftmp, tmp);
    }                           /* 2^256 - 2^224 + 2^192 */

    felem_mul(tmp, e64, e32);
    felem_reduce(ftmp2, tmp);   /* 2^64 - 2^0 */
    for (i = 0; i < 16; i++) {
        felem_square(tmp, ftmp2);
        felem_reduce(ftmp2, tmp);
    }                           /* 2^80 - 2^16 */
    felem_mul(tmp, ftmp2, e16);
    felem_reduce(ftmp2, tmp);   /* 2^80 - 2^0 */
    for (i = 0; i < 8; i++) {
        felem_square(tmp, ftmp2);
        felem_reduce(ftmp2, tmp);
    }                           /* 2^88 - 2^8 */
    felem_mul(tmp, ftmp2, e8);
    felem_reduce(ftmp2, tmp);   /* 2^88 - 2^0 */
    for (i = 0; i < 4; i++) {
        felem_square(tmp, ftmp2);
        felem_reduce(ftmp2, tmp);
    }                           /* 2^92 - 2^4 */
    felem_mul(tmp, ftmp2, e4);
    felem_reduce(ftmp2, tmp);   /* 2^92 - 2^0 */
    felem_square(tmp, ftmp2);
    felem_reduce(ftmp2, tmp);   /* 2^93 - 2^1 */
    felem_square(tmp, ftmp2);
    felem_reduce(ftmp2, tmp);   /* 2^94 - 2^2 */
    felem_mul(tmp, ftmp2, e2);
    felem_reduce(ftmp2, tmp);   /* 2^94 - 2^0 */
    felem_square(tmp, ftmp2);
    felem_reduce(ftmp2, tmp);   /* 2^95 - 2^1 */
    felem_square(tmp, ftmp2);
    felem_reduce(ftmp2, tmp);   /* 2^96 - 2^2 */
    felem_mul(tmp, ftmp2, in);
    felem_reduce(ftmp2, tmp);   /* 2^96 - 3 */

    felem_mul(tmp, ftmp2, ftmp);
    felem_reduce(out, tmp);     /* 2^256 - 2^224 + 2^192 + 2^96 - 3 */
}

static void smallfelem_inv_contract(smallfelem out, const smallfelem in)
{
    felem tmp;

    smallfelem_expand(tmp, in);
    felem_inv(tmp, tmp);
    felem_contract(out, tmp);
}

/*-
 * Group operations
 * ----------------
 *
 * Building on top of the field operations we have the operations on the
 * elliptic curve group itself. Points on the curve are represented in Jacobian
 * coordinates
 */

/*-
 * point_double calculates 2*(x_in, y_in, z_in)
 *
 * The method is taken from:
 *   http://hyperelliptic.org/EFD/g1p/auto-shortw-jacobian-3.html#doubling-dbl-2001-b
 *
 * Outputs can equal corresponding inputs, i.e., x_out == x_in is allowed.
 * while x_out == y_in is not (maybe this works, but it's not tested).
 */
static void
point_double(felem x_out, felem y_out, felem z_out,
             const felem x_in, const felem y_in, const felem z_in)
{
    longfelem tmp, tmp2;
    felem delta, gamma, beta, alpha, ftmp, ftmp2;
    smallfelem small1, small2;

    felem_assign(ftmp, x_in);
    /* ftmp[i] < 2^106 */
    felem_assign(ftmp2, x_in);
    /* ftmp2[i] < 2^106 */

    /* delta = z^2 */
    felem_square(tmp, z_in);
    felem_reduce(delta, tmp);
    /* delta[i] < 2^101 */

    /* gamma = y^2 */
    felem_square(tmp, y_in);
    felem_reduce(gamma, tmp);
    /* gamma[i] < 2^101 */
    felem_shrink(small1, gamma);

    /* beta = x*gamma */
    felem_small_mul(tmp, small1, x_in);
    felem_reduce(beta, tmp);
    /* beta[i] < 2^101 */

    /* alpha = 3*(x-delta)*(x+delta) */
    felem_diff(ftmp, delta);
    /* ftmp[i] < 2^105 + 2^106 < 2^107 */
    felem_sum(ftmp2, delta);
    /* ftmp2[i] < 2^105 + 2^106 < 2^107 */
    felem_scalar(ftmp2, 3);
    /* ftmp2[i] < 3 * 2^107 < 2^109 */
    felem_mul(tmp, ftmp, ftmp2);
    felem_reduce(alpha, tmp);
    /* alpha[i] < 2^101 */
    felem_shrink(small2, alpha);

    /* x' = alpha^2 - 8*beta */
    smallfelem_square(tmp, small2);
    felem_reduce(x_out, tmp);
    felem_assign(ftmp, beta);
    felem_scalar(ftmp, 8);
    /* ftmp[i] < 8 * 2^101 = 2^104 */
    felem_diff(x_out, ftmp);
    /* x_out[i] < 2^105 + 2^101 < 2^106 */

    /* z' = (y + z)^2 - gamma - delta */
    felem_sum(delta, gamma);
    /* delta[i] < 2^101 + 2^101 = 2^102 */
    felem_assign(ftmp, y_in);
    felem_sum(ftmp, z_in);
    /* ftmp[i] < 2^106 + 2^106 = 2^107 */
    felem_square(tmp, ftmp);
    felem_reduce(z_out, tmp);
    felem_diff(z_out, delta);
    /* z_out[i] < 2^105 + 2^101 < 2^106 */

    /* y' = alpha*(4*beta - x') - 8*gamma^2 */
    felem_scalar(beta, 4);
    /* beta[i] < 4 * 2^101 = 2^103 */
    felem_diff_zero107(beta, x_out);
    /* beta[i] < 2^107 + 2^103 < 2^108 */
    felem_small_mul(tmp, small2, beta);
    /* tmp[i] < 7 * 2^64 < 2^67 */
    smallfelem_square(tmp2, small1);
    /* tmp2[i] < 7 * 2^64 */
    longfelem_scalar(tmp2, 8);
    /* tmp2[i] < 8 * 7 * 2^64 = 7 * 2^67 */
    longfelem_diff(tmp, tmp2);
    /* tmp[i] < 2^67 + 2^70 + 2^40 < 2^71 */
    felem_reduce_zero105(y_out, tmp);
    /* y_out[i] < 2^106 */
}

/*
 * point_double_small is the same as point_double, except that it operates on
 * smallfelems
 */
static void
point_double_small(smallfelem x_out, smallfelem y_out, smallfelem z_out,
                   const smallfelem x_in, const smallfelem y_in,
                   const smallfelem z_in)
{
    felem felem_x_out, felem_y_out, felem_z_out;
    felem felem_x_in, felem_y_in, felem_z_in;

    smallfelem_expand(felem_x_in, x_in);
    smallfelem_expand(felem_y_in, y_in);
    smallfelem_expand(felem_z_in, z_in);
    point_double(felem_x_out, felem_y_out, felem_z_out,
                 felem_x_in, felem_y_in, felem_z_in);
    felem_shrink(x_out, felem_x_out);
    felem_shrink(y_out, felem_y_out);
    felem_shrink(z_out, felem_z_out);
}

/* copy_conditional copies in to out iff mask is all ones. */
static void copy_conditional(felem out, const felem in, limb mask)
{
    unsigned i;
    for (i = 0; i < NLIMBS; ++i) {
        const limb tmp = mask & (in[i] ^ out[i]);
        out[i] ^= tmp;
    }
}

/* copy_small_conditional copies in to out iff mask is all ones. */
static void copy_small_conditional(felem out, const smallfelem in, limb mask)
{
    unsigned i;
    const u64 mask64 = mask;
    for (i = 0; i < NLIMBS; ++i) {
        out[i] = ((limb) (in[i] & mask64)) | (out[i] & ~mask);
    }
}

/*-
 * point_add calculates (x1, y1, z1) + (x2, y2, z2)
 *
 * The method is taken from:
 *   http://hyperelliptic.org/EFD/g1p/auto-shortw-jacobian-3.html#addition-add-2007-bl,
 * adapted for mixed addition (z2 = 1, or z2 = 0 for the point at infinity).
 *
 * This function includes a branch for checking whether the two input points
 * are equal, (while not equal to the point at infinity). This case never
 * happens during single point multiplication, so there is no timing leak for
 * ECDH or ECDSA signing.
 */
static void point_add(felem x3, felem y3, felem z3,
                      const felem x1, const felem y1, const felem z1,
                      const int mixed, const smallfelem x2,
                      const smallfelem y2, const smallfelem z2)
{
    felem ftmp, ftmp2, ftmp3, ftmp4, ftmp5, ftmp6, x_out, y_out, z_out;
    longfelem tmp, tmp2;
    smallfelem small1, small2, small3, small4, small5;
    limb x_equal, y_equal, z1_is_zero, z2_is_zero;

    felem_shrink(small3, z1);

    z1_is_zero = smallfelem_is_zero(small3);
    z2_is_zero = smallfelem_is_zero(z2);

    /* ftmp = z1z1 = z1**2 */
    smallfelem_square(tmp, small3);
    felem_reduce(ftmp, tmp);
    /* ftmp[i] < 2^101 */
    felem_shrink(small1, ftmp);

    if (!mixed) {
        /* ftmp2 = z2z2 = z2**2 */
        smallfelem_square(tmp, z2);
        felem_reduce(ftmp2, tmp);
        /* ftmp2[i] < 2^101 */
        felem_shrink(small2, ftmp2);

        felem_shrink(small5, x1);

        /* u1 = ftmp3 = x1*z2z2 */
        smallfelem_mul(tmp, small5, small2);
        felem_reduce(ftmp3, tmp);
        /* ftmp3[i] < 2^101 */

        /* ftmp5 = z1 + z2 */
        felem_assign(ftmp5, z1);
        felem_small_sum(ftmp5, z2);
        /* ftmp5[i] < 2^107 */

        /* ftmp5 = (z1 + z2)**2 - (z1z1 + z2z2) = 2z1z2 */
        felem_square(tmp, ftmp5);
        felem_reduce(ftmp5, tmp);
        /* ftmp2 = z2z2 + z1z1 */
        felem_sum(ftmp2, ftmp);
        /* ftmp2[i] < 2^101 + 2^101 = 2^102 */
        felem_diff(ftmp5, ftmp2);
        /* ftmp5[i] < 2^105 + 2^101 < 2^106 */

        /* ftmp2 = z2 * z2z2 */
        smallfelem_mul(tmp, small2, z2);
        felem_reduce(ftmp2, tmp);

        /* s1 = ftmp2 = y1 * z2**3 */
        felem_mul(tmp, y1, ftmp2);
        felem_reduce(ftmp6, tmp);
        /* ftmp6[i] < 2^101 */
    } else {
        /*
         * We'll assume z2 = 1 (special case z2 = 0 is handled later)
         */

        /* u1 = ftmp3 = x1*z2z2 */
        felem_assign(ftmp3, x1);
        /* ftmp3[i] < 2^106 */

        /* ftmp5 = 2z1z2 */
        felem_assign(ftmp5, z1);
        felem_scalar(ftmp5, 2);
        /* ftmp5[i] < 2*2^106 = 2^107 */

        /* s1 = ftmp2 = y1 * z2**3 */
        felem_assign(ftmp6, y1);
        /* ftmp6[i] < 2^106 */
    }

    /* u2 = x2*z1z1 */
    smallfelem_mul(tmp, x2, small1);
    felem_reduce(ftmp4, tmp);

    /* h = ftmp4 = u2 - u1 */
    felem_diff_zero107(ftmp4, ftmp3);
    /* ftmp4[i] < 2^107 + 2^101 < 2^108 */
    felem_shrink(small4, ftmp4);

    x_equal = smallfelem_is_zero(small4);

    /* z_out = ftmp5 * h */
    felem_small_mul(tmp, small4, ftmp5);
    felem_reduce(z_out, tmp);
    /* z_out[i] < 2^101 */

    /* ftmp = z1 * z1z1 */
    smallfelem_mul(tmp, small1, small3);
    felem_reduce(ftmp, tmp);

    /* s2 = tmp = y2 * z1**3 */
    felem_small_mul(tmp, y2, ftmp);
    felem_reduce(ftmp5, tmp);

    /* r = ftmp5 = (s2 - s1)*2 */
    felem_diff_zero107(ftmp5, ftmp6);
    /* ftmp5[i] < 2^107 + 2^107 = 2^108 */
    felem_scalar(ftmp5, 2);
    /* ftmp5[i] < 2^109 */
    felem_shrink(small1, ftmp5);
    y_equal = smallfelem_is_zero(small1);

    if (x_equal && y_equal && !z1_is_zero && !z2_is_zero) {
        point_double(x3, y3, z3, x1, y1, z1);
        return;
    }

    /* I = ftmp = (2h)**2 */
    felem_assign(ftmp, ftmp4);
    felem_scalar(ftmp, 2);
    /* ftmp[i] < 2*2^108 = 2^109 */
    felem_square(tmp, ftmp);
    felem_reduce(ftmp, tmp);

    /* J = ftmp2 = h * I */
    felem_mul(tmp, ftmp4, ftmp);
    felem_reduce(ftmp2, tmp);

    /* V = ftmp4 = U1 * I */
    felem_mul(tmp, ftmp3, ftmp);
    felem_reduce(ftmp4, tmp);

    /* x_out = r**2 - J - 2V */
    smallfelem_square(tmp, small1);
    felem_reduce(x_out, tmp);
    felem_assign(ftmp3, ftmp4);
    felem_scalar(ftmp4, 2);
    felem_sum(ftmp4, ftmp2);
    /* ftmp4[i] < 2*2^101 + 2^101 < 2^103 */
    felem_diff(x_out, ftmp4);
    /* x_out[i] < 2^105 + 2^101 */

    /* y_out = r(V-x_out) - 2 * s1 * J */
    felem_diff_zero107(ftmp3, x_out);
    /* ftmp3[i] < 2^107 + 2^101 < 2^108 */
    felem_small_mul(tmp, small1, ftmp3);
    felem_mul(tmp2, ftmp6, ftmp2);
    longfelem_scalar(tmp2, 2);
    /* tmp2[i] < 2*2^67 = 2^68 */
    longfelem_diff(tmp, tmp2);
    /* tmp[i] < 2^67 + 2^70 + 2^40 < 2^71 */
    felem_reduce_zero105(y_out, tmp);
    /* y_out[i] < 2^106 */

    copy_small_conditional(x_out, x2, z1_is_zero);
    copy_conditional(x_out, x1, z2_is_zero);
    copy_small_conditional(y_out, y2, z1_is_zero);
    copy_conditional(y_out, y1, z2_is_zero);
    copy_small_conditional(z_out, z2, z1_is_zero);
    copy_conditional(z_out, z1, z2_is_zero);
    felem_assign(x3, x_out);
    felem_assign(y3, y_out);
    felem_assign(z3, z_out);
}

/*
 * point_add_small is the same as point_add, except that it operates on
 * smallfelems
 */
static void point_add_small(smallfelem x3, smallfelem y3, smallfelem z3,
                            smallfelem x1, smallfelem y1, smallfelem z1,
                            smallfelem x2, smallfelem y2, smallfelem z2)
{
    felem felem_x3, felem_y3, felem_z3;
    felem felem_x1, felem_y1, felem_z1;
    smallfelem_expand(felem_x1, x1);
    smallfelem_expand(felem_y1, y1);
    smallfelem_expand(felem_z1, z1);
    point_add(felem_x3, felem_y3, felem_z3, felem_x1, felem_y1, felem_z1, 0,
              x2, y2, z2);
    felem_shrink(x3, felem_x3);
    felem_shrink(y3, felem_y3);
    felem_shrink(z3, felem_z3);
}

/*-
 * Base point pre computation
 * --------------------------
 *
 * Two different sorts of precomputed tables are used in the following code.
 * Each contain various points on the curve, where each point is three field
 * elements (x, y, z).
 *
 * For the base point table, z is usually 1 (0 for the point at infinity).
 * This table has 2 * 16 elements, starting with the following:
 * index | bits    | point
 * ------+---------+------------------------------
 *     0 | 0 0 0 0 | 0G
 *     1 | 0 0 0 1 | 1G
 *     2 | 0 0 1 0 | 2^64G
 *     3 | 0 0 1 1 | (2^64 + 1)G
 *     4 | 0 1 0 0 | 2^128G
 *     5 | 0 1 0 1 | (2^128 + 1)G
 *     6 | 0 1 1 0 | (2^128 + 2^64)G
 *     7 | 0 1 1 1 | (2^128 + 2^64 + 1)G
 *     8 | 1 0 0 0 | 2^192G
 *     9 | 1 0 0 1 | (2^192 + 1)G
 *    10 | 1 0 1 0 | (2^192 + 2^64)G
 *    11 | 1 0 1 1 | (2^192 + 2^64 + 1)G
 *    12 | 1 1 0 0 | (2^192 + 2^128)G
 *    13 | 1 1 0 1 | (2^192 + 2^128 + 1)G
 *    14 | 1 1 1 0 | (2^192 + 2^128 + 2^64)G
 *    15 | 1 1 1 1 | (2^192 + 2^128 + 2^64 + 1)G
 * followed by a copy of this with each element multiplied by 2^32.
 *
 * The reason for this is so that we can clock bits into four different
 * locations when doing simple scalar multiplies against the base point,
 * and then another four locations using the second 16 elements.
 *
 * Tables for other points have table[i] = iG for i in 0 .. 16. */

/* gmul is the table of precomputed base points */
static const smallfelem gmul[2][16][3] = {
    {{{0, 0, 0, 0},
      {0, 0, 0, 0},
      {0, 0, 0, 0}},
     {{0xf4a13945d898c296, 0x77037d812deb33a0, 0xf8bce6e563a440f2,
       0x6b17d1f2e12c4247},
      {0xcbb6406837bf51f5, 0x2bce33576b315ece, 0x8ee7eb4a7c0f9e16,
       0x4fe342e2fe1a7f9b},
      {1, 0, 0, 0}},
     {{0x90e75cb48e14db63, 0x29493baaad651f7e, 0x8492592e326e25de,
       0x0fa822bc2811aaa5},
      {0xe41124545f462ee7, 0x34b1a65050fe82f5, 0x6f4ad4bcb3df188b,
       0xbff44ae8f5dba80d},
      {1, 0, 0, 0}},
     {{0x93391ce2097992af, 0xe96c98fd0d35f1fa, 0xb257c0de95e02789,
       0x300a4bbc89d6726f},
      {0xaa54a291c08127a0, 0x5bb1eeada9d806a5, 0x7f1ddb25ff1e3c6f,
       0x72aac7e0d09b4644},
      {1, 0, 0, 0}},
     {{0x57c84fc9d789bd85, 0xfc35ff7dc297eac3, 0xfb982fd588c6766e,
       0x447d739beedb5e67},
      {0x0c7e33c972e25b32, 0x3d349b95a7fae500, 0xe12e9d953a4aaff7,
       0x2d4825ab834131ee},
      {1, 0, 0, 0}},
     {{0x13949c932a1d367f, 0xef7fbd2b1a0a11b7, 0xddc6068bb91dfc60,
       0xef9519328a9c72ff},
      {0x196035a77376d8a8, 0x23183b0895ca1740, 0xc1ee9807022c219c,
       0x611e9fc37dbb2c9b},
      {1, 0, 0, 0}},
     {{0xcae2b1920b57f4bc, 0x2936df5ec6c9bc36, 0x7dea6482e11238bf,
       0x550663797b51f5d8},
      {0x44ffe216348a964c, 0x9fb3d576dbdefbe1, 0x0afa40018d9d50e5,
       0x157164848aecb851},
      {1, 0, 0, 0}},
     {{0xe48ecafffc5cde01, 0x7ccd84e70d715f26, 0xa2e8f483f43e4391,
       0xeb5d7745b21141ea},
      {0xcac917e2731a3479, 0x85f22cfe2844b645, 0x0990e6a158006cee,
       0xeafd72ebdbecc17b},
      {1, 0, 0, 0}},
     {{0x6cf20ffb313728be, 0x96439591a3c6b94a, 0x2736ff8344315fc5,
       0xa6d39677a7849276},
      {0xf2bab833c357f5f4, 0x824a920c2284059b, 0x66b8babd2d27ecdf,
       0x674f84749b0b8816},
      {1, 0, 0, 0}},
     {{0x2df48c04677c8a3e, 0x74e02f080203a56b, 0x31855f7db8c7fedb,
       0x4e769e7672c9ddad},
      {0xa4c36165b824bbb0, 0xfb9ae16f3b9122a5, 0x1ec0057206947281,
       0x42b99082de830663},
      {1, 0, 0, 0}},
     {{0x6ef95150dda868b9, 0xd1f89e799c0ce131, 0x7fdc1ca008a1c478,
       0x78878ef61c6ce04d},
      {0x9c62b9121fe0d976, 0x6ace570ebde08d4f, 0xde53142c12309def,
       0xb6cb3f5d7b72c321},
      {1, 0, 0, 0}},
     {{0x7f991ed2c31a3573, 0x5b82dd5bd54fb496, 0x595c5220812ffcae,
       0x0c88bc4d716b1287},
      {0x3a57bf635f48aca8, 0x7c8181f4df2564f3, 0x18d1b5b39c04e6aa,
       0xdd5ddea3f3901dc6},
      {1, 0, 0, 0}},
     {{0xe96a79fb3e72ad0c, 0x43a0a28c42ba792f, 0xefe0a423083e49f3,
       0x68f344af6b317466},
      {0xcdfe17db3fb24d4a, 0x668bfc2271f5c626, 0x604ed93c24d67ff3,
       0x31b9c405f8540a20},
      {1, 0, 0, 0}},
     {{0xd36b4789a2582e7f, 0x0d1a10144ec39c28, 0x663c62c3edbad7a0,
       0x4052bf4b6f461db9},
      {0x235a27c3188d25eb, 0xe724f33999bfcc5b, 0x862be6bd71d70cc8,
       0xfecf4d5190b0fc61},
      {1, 0, 0, 0}},
     {{0x74346c10a1d4cfac, 0xafdf5cc08526a7a4, 0x123202a8f62bff7a,
       0x1eddbae2c802e41a},
      {0x8fa0af2dd603f844, 0x36e06b7e4c701917, 0x0c45f45273db33a0,
       0x43104d86560ebcfc},
      {1, 0, 0, 0}},
     {{0x9615b5110d1d78e5, 0x66b0de3225c4744b, 0x0a4a46fb6aaf363a,
       0xb48e26b484f7a21c},
      {0x06ebb0f621a01b2d, 0xc004e4048b7b0f98, 0x64131bcdfed6f668,
       0xfac015404d4d3dab},
      {1, 0, 0, 0}}},
    {{{0, 0, 0, 0},
      {0, 0, 0, 0},
      {0, 0, 0, 0}},
     {{0x3a5a9e22185a5943, 0x1ab919365c65dfb6, 0x21656b32262c71da,
       0x7fe36b40af22af89},
      {0xd50d152c699ca101, 0x74b3d5867b8af212, 0x9f09f40407dca6f1,
       0xe697d45825b63624},
      {1, 0, 0, 0}},
     {{0xa84aa9397512218e, 0xe9a521b074ca0141, 0x57880b3a18a2e902,
       0x4a5b506612a677a6},
      {0x0beada7a4c4f3840, 0x626db15419e26d9d, 0xc42604fbe1627d40,
       0xeb13461ceac089f1},
      {1, 0, 0, 0}},
     {{0xf9faed0927a43281, 0x5e52c4144103ecbc, 0xc342967aa815c857,
       0x0781b8291c6a220a},
      {0x5a8343ceeac55f80, 0x88f80eeee54a05e3, 0x97b2a14f12916434,
       0x690cde8df0151593},
      {1, 0, 0, 0}},
     {{0xaee9c75df7f82f2a, 0x9e4c35874afdf43a, 0xf5622df437371326,
       0x8a535f566ec73617},
      {0xc5f9a0ac223094b7, 0xcde533864c8c7669, 0x37e02819085a92bf,
       0x0455c08468b08bd7},
      {1, 0, 0, 0}},
     {{0x0c0a6e2c9477b5d9, 0xf9a4bf62876dc444, 0x5050a949b6cdc279,
       0x06bada7ab77f8276},
      {0xc8b4aed1ea48dac9, 0xdebd8a4b7ea1070f, 0x427d49101366eb70,
       0x5b476dfd0e6cb18a},
      {1, 0, 0, 0}},
     {{0x7c5c3e44278c340a, 0x4d54606812d66f3b, 0x29a751b1ae23c5d8,
       0x3e29864e8a2ec908},
      {0x142d2a6626dbb850, 0xad1744c4765bd780, 0x1f150e68e322d1ed,
       0x239b90ea3dc31e7e},
      {1, 0, 0, 0}},
     {{0x78c416527a53322a, 0x305dde6709776f8e, 0xdbcab759f8862ed4,
       0x820f4dd949f72ff7},
      {0x6cc544a62b5debd4, 0x75be5d937b4e8cc4, 0x1b481b1b215c14d3,
       0x140406ec783a05ec},
      {1, 0, 0, 0}},
     {{0x6a703f10e895df07, 0xfd75f3fa01876bd8, 0xeb5b06e70ce08ffe,
       0x68f6b8542783dfee},
      {0x90c76f8a78712655, 0xcf5293d2f310bf7f, 0xfbc8044dfda45028,
       0xcbe1feba92e40ce6},
      {1, 0, 0, 0}},
     {{0xe998ceea4396e4c1, 0xfc82ef0b6acea274, 0x230f729f2250e927,
       0xd0b2f94d2f420109},
      {0x4305adddb38d4966, 0x10b838f8624c3b45, 0x7db2636658954e7a,
       0x971459828b0719e5},
      {1, 0, 0, 0}},
     {{0x4bd6b72623369fc9, 0x57f2929e53d0b876, 0xc2d5cba4f2340687,
       0x961610004a866aba},
      {0x49997bcd2e407a5e, 0x69ab197d92ddcb24, 0x2cf1f2438fe5131c,
       0x7acb9fadcee75e44},
      {1, 0, 0, 0}},
     {{0x254e839423d2d4c0, 0xf57f0c917aea685b, 0xa60d880f6f75aaea,
       0x24eb9acca333bf5b},
      {0xe3de4ccb1cda5dea, 0xfeef9341c51a6b4f, 0x743125f88bac4c4d,
       0x69f891c5acd079cc},
      {1, 0, 0, 0}},
     {{0xeee44b35702476b5, 0x7ed031a0e45c2258, 0xb422d1e7bd6f8514,
       0xe51f547c5972a107},
      {0xa25bcd6fc9cf343d, 0x8ca922ee097c184e, 0xa62f98b3a9fe9a06,
       0x1c309a2b25bb1387},
      {1, 0, 0, 0}},
     {{0x9295dbeb1967c459, 0xb00148833472c98e, 0xc504977708011828,
       0x20b87b8aa2c4e503},
      {0x3063175de057c277, 0x1bd539338fe582dd, 0x0d11adef5f69a044,
       0xf5c6fa49919776be},
      {1, 0, 0, 0}},
     {{0x8c944e760fd59e11, 0x3876cba1102fad5f, 0xa454c3fad83faa56,
       0x1ed7d1b9332010b9},
      {0xa1011a270024b889, 0x05e4d0dcac0cd344, 0x52b520f0eb6a2a24,
       0x3a2b03f03217257a},
      {1, 0, 0, 0}},
     {{0xf20fc2afdf1d043d, 0xf330240db58d5a62, 0xfc7d229ca0058c3b,
       0x15fee545c78dd9f6},
      {0x501e82885bc98cda, 0x41ef80e5d046ac04, 0x557d9f49461210fb,
       0x4ab5b6b2b8753f81},
      {1, 0, 0, 0}}}
};

/*
 * select_point selects the |idx|th point from a precomputation table and
 * copies it to out.
 */
static void select_point(const u64 idx, unsigned int size,
                         const smallfelem pre_comp[16][3], smallfelem out[3])
{
    unsigned i, j;
    u64 *outlimbs = &out[0][0];

    memset(out, 0, sizeof(*out) * 3);

    for (i = 0; i < size; i++) {
        const u64 *inlimbs = (u64 *)&pre_comp[i][0][0];
        u64 mask = i ^ idx;
        mask |= mask >> 4;
        mask |= mask >> 2;
        mask |= mask >> 1;
        mask &= 1;
        mask--;
        for (j = 0; j < NLIMBS * 3; j++)
            outlimbs[j] |= inlimbs[j] & mask;
    }
}

/* get_bit returns the |i|th bit in |in| */
static char get_bit(const felem_bytearray in, int i)
{
    if ((i < 0) || (i >= 256))
        return 0;
    return (in[i >> 3] >> (i & 7)) & 1;
}

/*
 * Interleaved point multiplication using precomputed point multiples: The
 * small point multiples 0*P, 1*P, ..., 17*P are in pre_comp[], the scalars
 * in scalars[]. If g_scalar is non-NULL, we also add this multiple of the
 * generator, using certain (large) precomputed multiples in g_pre_comp.
 * Output point (X, Y, Z) is stored in x_out, y_out, z_out
 */
static void batch_mul(felem x_out, felem y_out, felem z_out,
                      const felem_bytearray scalars[],
                      const unsigned num_points, const u8 *g_scalar,
                      const int mixed, const smallfelem pre_comp[][17][3],
                      const smallfelem g_pre_comp[2][16][3])
{
    int i, skip;
    unsigned num, gen_mul = (g_scalar != NULL);
    felem nq[3], ftmp;
    smallfelem tmp[3];
    u64 bits;
    u8 sign, digit;

    /* set nq to the point at infinity */
    memset(nq, 0, sizeof(nq));

    /*
     * Loop over all scalars msb-to-lsb, interleaving additions of multiples
     * of the generator (two in each of the last 32 rounds) and additions of
     * other points multiples (every 5th round).
     */
    skip = 1;                   /* save two point operations in the first
                                 * round */
    for (i = (num_points ? 255 : 31); i >= 0; --i) {
        /* double */
        if (!skip)
            point_double(nq[0], nq[1], nq[2], nq[0], nq[1], nq[2]);

        /* add multiples of the generator */
        if (gen_mul && (i <= 31)) {
            /* first, look 32 bits upwards */
            bits = get_bit(g_scalar, i + 224) << 3;
            bits |= get_bit(g_scalar, i + 160) << 2;
            bits |= get_bit(g_scalar, i + 96) << 1;
            bits |= get_bit(g_scalar, i + 32);
            /* select the point to add, in constant time */
            select_point(bits, 16, g_pre_comp[1], tmp);

            if (!skip) {
                /* Arg 1 below is for "mixed" */
                point_add(nq[0], nq[1], nq[2],
                          nq[0], nq[1], nq[2], 1, tmp[0], tmp[1], tmp[2]);
            } else {
                smallfelem_expand(nq[0], tmp[0]);
                smallfelem_expand(nq[1], tmp[1]);
                smallfelem_expand(nq[2], tmp[2]);
                skip = 0;
            }

            /* second, look at the current position */
            bits = get_bit(g_scalar, i + 192) << 3;
            bits |= get_bit(g_scalar, i + 128) << 2;
            bits |= get_bit(g_scalar, i + 64) << 1;
            bits |= get_bit(g_scalar, i);
            /* select the point to add, in constant time */
            select_point(bits, 16, g_pre_comp[0], tmp);
            /* Arg 1 below is for "mixed" */
            point_add(nq[0], nq[1], nq[2],
                      nq[0], nq[1], nq[2], 1, tmp[0], tmp[1], tmp[2]);
        }

        /* do other additions every 5 doublings */
        if (num_points && (i % 5 == 0)) {
            /* loop over all scalars */
            for (num = 0; num < num_points; ++num) {
                bits = get_bit(scalars[num], i + 4) << 5;
                bits |= get_bit(scalars[num], i + 3) << 4;
                bits |= get_bit(scalars[num], i + 2) << 3;
                bits |= get_bit(scalars[num], i + 1) << 2;
                bits |= get_bit(scalars[num], i) << 1;
                bits |= get_bit(scalars[num], i - 1);
                ec_GFp_nistp_recode_scalar_bits(&sign, &digit, bits);

                /*
                 * select the point to add or subtract, in constant time
                 */
                select_point(digit, 17, pre_comp[num], tmp);
                smallfelem_neg(ftmp, tmp[1]); /* (X, -Y, Z) is the negative
                                               * point */
                copy_small_conditional(ftmp, tmp[1], (((limb) sign) - 1));
                felem_contract(tmp[1], ftmp);

                if (!skip) {
                    point_add(nq[0], nq[1], nq[2],
                              nq[0], nq[1], nq[2],
                              mixed, tmp[0], tmp[1], tmp[2]);
                } else {
                    smallfelem_expand(nq[0], tmp[0]);
                    smallfelem_expand(nq[1], tmp[1]);
                    smallfelem_expand(nq[2], tmp[2]);
                    skip = 0;
                }
            }
        }
    }
    felem_assign(x_out, nq[0]);
    felem_assign(y_out, nq[1]);
    felem_assign(z_out, nq[2]);
}

/* Precomputation for the group generator. */
struct nistp256_pre_comp_st {
    smallfelem g_pre_comp[2][16][3];
    CRYPTO_REF_COUNT references;
    CRYPTO_RWLOCK *lock;
};

const EC_METHOD *EC_GFp_nistp256_method(void)
{
    static const EC_METHOD ret = {
        EC_FLAGS_DEFAULT_OCT,
        NID_X9_62_prime_field,
        ec_GFp_nistp256_group_init,
        ec_GFp_simple_group_finish,
        ec_GFp_simple_group_clear_finish,
        ec_GFp_nist_group_copy,
        ec_GFp_nistp256_group_set_curve,
        ec_GFp_simple_group_get_curve,
        ec_GFp_simple_group_get_degree,
        ec_group_simple_order_bits,
        ec_GFp_simple_group_check_discriminant,
        ec_GFp_simple_point_init,
        ec_GFp_simple_point_finish,
        ec_GFp_simple_point_clear_finish,
        ec_GFp_simple_point_copy,
        ec_GFp_simple_point_set_to_infinity,
        ec_GFp_simple_set_Jprojective_coordinates_GFp,
        ec_GFp_simple_get_Jprojective_coordinates_GFp,
        ec_GFp_simple_point_set_affine_coordinates,
        ec_GFp_nistp256_point_get_affine_coordinates,
        0 /* point_set_compressed_coordinates */ ,
        0 /* point2oct */ ,
        0 /* oct2point */ ,
        ec_GFp_simple_add,
        ec_GFp_simple_dbl,
        ec_GFp_simple_invert,
        ec_GFp_simple_is_at_infinity,
        ec_GFp_simple_is_on_curve,
        ec_GFp_simple_cmp,
        ec_GFp_simple_make_affine,
        ec_GFp_simple_points_make_affine,
        ec_GFp_nistp256_points_mul,
        ec_GFp_nistp256_precompute_mult,
        ec_GFp_nistp256_have_precompute_mult,
        ec_GFp_nist_field_mul,
        ec_GFp_nist_field_sqr,
        0 /* field_div */ ,
        ec_GFp_simple_field_inv,
        0 /* field_encode */ ,
        0 /* field_decode */ ,
        0,                      /* field_set_to_one */
        ec_key_simple_priv2oct,
        ec_key_simple_oct2priv,
        0, /* set private */
        ec_key_simple_generate_key,
        ec_key_simple_check_key,
        ec_key_simple_generate_public_key,
        0, /* keycopy */
        0, /* keyfinish */
        ecdh_simple_compute_key,
        0, /* field_inverse_mod_ord */
        0, /* blind_coordinates */
        0, /* ladder_pre */
        0, /* ladder_step */
        0  /* ladder_post */
    };

    return &ret;
}

/******************************************************************************/
/*
 * FUNCTIONS TO MANAGE PRECOMPUTATION
 */

static NISTP256_PRE_COMP *nistp256_pre_comp_new(void)
{
    NISTP256_PRE_COMP *ret = OPENSSL_zalloc(sizeof(*ret));

    if (ret == NULL) {
        ECerr(EC_F_NISTP256_PRE_COMP_NEW, ERR_R_MALLOC_FAILURE);
        return ret;
    }

    ret->references = 1;

    ret->lock = CRYPTO_THREAD_lock_new();
    if (ret->lock == NULL) {
        ECerr(EC_F_NISTP256_PRE_COMP_NEW, ERR_R_MALLOC_FAILURE);
        OPENSSL_free(ret);
        return NULL;
    }
    return ret;
}

NISTP256_PRE_COMP *EC_nistp256_pre_comp_dup(NISTP256_PRE_COMP *p)
{
    int i;
    if (p != NULL)
        CRYPTO_UP_REF(&p->references, &i, p->lock);
    return p;
}

void EC_nistp256_pre_comp_free(NISTP256_PRE_COMP *pre)
{
    int i;

    if (pre == NULL)
        return;

    CRYPTO_DOWN_REF(&pre->references, &i, pre->lock);
    REF_PRINT_COUNT("EC_nistp256", x);
    if (i > 0)
        return;
    REF_ASSERT_ISNT(i < 0);

    CRYPTO_THREAD_lock_free(pre->lock);
    OPENSSL_free(pre);
}

/******************************************************************************/
/*
 * OPENSSL EC_METHOD FUNCTIONS
 */

int ec_GFp_nistp256_group_init(EC_GROUP *group)
{
    int ret;
    ret = ec_GFp_simple_group_init(group);
    group->a_is_minus3 = 1;
    return ret;
}

int ec_GFp_nistp256_group_set_curve(EC_GROUP *group, const BIGNUM *p,
                                    const BIGNUM *a, const BIGNUM *b,
                                    BN_CTX *ctx)
{
    int ret = 0;
    BN_CTX *new_ctx = NULL;
    BIGNUM *curve_p, *curve_a, *curve_b;

    if (ctx == NULL)
        if ((ctx = new_ctx = BN_CTX_new()) == NULL)
            return 0;
    BN_CTX_start(ctx);
    curve_p = BN_CTX_get(ctx);
    curve_a = BN_CTX_get(ctx);
    curve_b = BN_CTX_get(ctx);
    if (curve_b == NULL)
        goto err;
    BN_bin2bn(nistp256_curve_params[0], sizeof(felem_bytearray), curve_p);
    BN_bin2bn(nistp256_curve_params[1], sizeof(felem_bytearray), curve_a);
    BN_bin2bn(nistp256_curve_params[2], sizeof(felem_bytearray), curve_b);
    if ((BN_cmp(curve_p, p)) || (BN_cmp(curve_a, a)) || (BN_cmp(curve_b, b))) {
        ECerr(EC_F_EC_GFP_NISTP256_GROUP_SET_CURVE,
              EC_R_WRONG_CURVE_PARAMETERS);
        goto err;
    }
    group->field_mod_func = BN_nist_mod_256;
    ret = ec_GFp_simple_group_set_curve(group, p, a, b, ctx);
 err:
    BN_CTX_end(ctx);
    BN_CTX_free(new_ctx);
    return ret;
}

/*
 * Takes the Jacobian coordinates (X, Y, Z) of a point and returns (X', Y') =
 * (X/Z^2, Y/Z^3)
 */
int ec_GFp_nistp256_point_get_affine_coordinates(const EC_GROUP *group,
                                                 const EC_POINT *point,
                                                 BIGNUM *x, BIGNUM *y,
                                                 BN_CTX *ctx)
{
    felem z1, z2, x_in, y_in;
    smallfelem x_out, y_out;
    longfelem tmp;

    if (EC_POINT_is_at_infinity(group, point)) {
        ECerr(EC_F_EC_GFP_NISTP256_POINT_GET_AFFINE_COORDINATES,
              EC_R_POINT_AT_INFINITY);
        return 0;
    }
    if ((!BN_to_felem(x_in, point->X)) || (!BN_to_felem(y_in, point->Y)) ||
        (!BN_to_felem(z1, point->Z)))
        return 0;
    felem_inv(z2, z1);
    felem_square(tmp, z2);
    felem_reduce(z1, tmp);
    felem_mul(tmp, x_in, z1);
    felem_reduce(x_in, tmp);
    felem_contract(x_out, x_in);
    if (x != NULL) {
        if (!smallfelem_to_BN(x, x_out)) {
            ECerr(EC_F_EC_GFP_NISTP256_POINT_GET_AFFINE_COORDINATES,
                  ERR_R_BN_LIB);
            return 0;
        }
    }
    felem_mul(tmp, z1, z2);
    felem_reduce(z1, tmp);
    felem_mul(tmp, y_in, z1);
    felem_reduce(y_in, tmp);
    felem_contract(y_out, y_in);
    if (y != NULL) {
        if (!smallfelem_to_BN(y, y_out)) {
            ECerr(EC_F_EC_GFP_NISTP256_POINT_GET_AFFINE_COORDINATES,
                  ERR_R_BN_LIB);
            return 0;
        }
    }
    return 1;
}

/* points below is of size |num|, and tmp_smallfelems is of size |num+1| */
static void make_points_affine(size_t num, smallfelem points[][3],
                               smallfelem tmp_smallfelems[])
{
    /*
     * Runs in constant time, unless an input is the point at infinity (which
     * normally shouldn't happen).
     */
    ec_GFp_nistp_points_make_affine_internal(num,
                                             points,
                                             sizeof(smallfelem),
                                             tmp_smallfelems,
                                             (void (*)(void *))smallfelem_one,
                                             smallfelem_is_zero_int,
                                             (void (*)(void *, const void *))
                                             smallfelem_assign,
                                             (void (*)(void *, const void *))
                                             smallfelem_square_contract,
                                             (void (*)
                                              (void *, const void *,
                                               const void *))
                                             smallfelem_mul_contract,
                                             (void (*)(void *, const void *))
                                             smallfelem_inv_contract,
                                             /* nothing to contract */
                                             (void (*)(void *, const void *))
                                             smallfelem_assign);
}

/*
 * Computes scalar*generator + \sum scalars[i]*points[i], ignoring NULL
 * values Result is stored in r (r can equal one of the inputs).
 */
int ec_GFp_nistp256_points_mul(const EC_GROUP *group, EC_POINT *r,
                               const BIGNUM *scalar, size_t num,
                               const EC_POINT *points[],
                               const BIGNUM *scalars[], BN_CTX *ctx)
{
    int ret = 0;
    int j;
    int mixed = 0;
    BIGNUM *x, *y, *z, *tmp_scalar;
    felem_bytearray g_secret;
    felem_bytearray *secrets = NULL;
    smallfelem (*pre_comp)[17][3] = NULL;
    smallfelem *tmp_smallfelems = NULL;
    felem_bytearray tmp;
    unsigned i, num_bytes;
    int have_pre_comp = 0;
    size_t num_points = num;
    smallfelem x_in, y_in, z_in;
    felem x_out, y_out, z_out;
    NISTP256_PRE_COMP *pre = NULL;
    const smallfelem(*g_pre_comp)[16][3] = NULL;
    EC_POINT *generator = NULL;
    const EC_POINT *p = NULL;
    const BIGNUM *p_scalar = NULL;

    BN_CTX_start(ctx);
    x = BN_CTX_get(ctx);
    y = BN_CTX_get(ctx);
    z = BN_CTX_get(ctx);
    tmp_scalar = BN_CTX_get(ctx);
    if (tmp_scalar == NULL)
        goto err;

    if (scalar != NULL) {
        pre = group->pre_comp.nistp256;
        if (pre)
            /* we have precomputation, try to use it */
            g_pre_comp = (const smallfelem(*)[16][3])pre->g_pre_comp;
        else
            /* try to use the standard precomputation */
            g_pre_comp = &gmul[0];
        generator = EC_POINT_new(group);
        if (generator == NULL)
            goto err;
        /* get the generator from precomputation */
        if (!smallfelem_to_BN(x, g_pre_comp[0][1][0]) ||
            !smallfelem_to_BN(y, g_pre_comp[0][1][1]) ||
            !smallfelem_to_BN(z, g_pre_comp[0][1][2])) {
            ECerr(EC_F_EC_GFP_NISTP256_POINTS_MUL, ERR_R_BN_LIB);
            goto err;
        }
        if (!EC_POINT_set_Jprojective_coordinates_GFp(group,
                                                      generator, x, y, z,
                                                      ctx))
            goto err;
        if (0 == EC_POINT_cmp(group, generator, group->generator, ctx))
            /* precomputation matches generator */
            have_pre_comp = 1;
        else
            /*
             * we don't have valid precomputation: treat the generator as a
             * random point
             */
            num_points++;
    }
    if (num_points > 0) {
        if (num_points >= 3) {
            /*
             * unless we precompute multiples for just one or two points,
             * converting those into affine form is time well spent
             */
            mixed = 1;
        }
        secrets = OPENSSL_malloc(sizeof(*secrets) * num_points);
        pre_comp = OPENSSL_malloc(sizeof(*pre_comp) * num_points);
        if (mixed)
            tmp_smallfelems =
              OPENSSL_malloc(sizeof(*tmp_smallfelems) * (num_points * 17 + 1));
        if ((secrets == NULL) || (pre_comp == NULL)
            || (mixed && (tmp_smallfelems == NULL))) {
            ECerr(EC_F_EC_GFP_NISTP256_POINTS_MUL, ERR_R_MALLOC_FAILURE);
            goto err;
        }

        /*
         * we treat NULL scalars as 0, and NULL points as points at infinity,
         * i.e., they contribute nothing to the linear combination
         */
        memset(secrets, 0, sizeof(*secrets) * num_points);
        memset(pre_comp, 0, sizeof(*pre_comp) * num_points);
        for (i = 0; i < num_points; ++i) {
            if (i == num)
                /*
                 * we didn't have a valid precomputation, so we pick the
                 * generator
                 */
            {
                p = EC_GROUP_get0_generator(group);
                p_scalar = scalar;
            } else
                /* the i^th point */
            {
                p = points[i];
                p_scalar = scalars[i];
            }
            if ((p_scalar != NULL) && (p != NULL)) {
                /* reduce scalar to 0 <= scalar < 2^256 */
                if ((BN_num_bits(p_scalar) > 256)
                    || (BN_is_negative(p_scalar))) {
                    /*
                     * this is an unusual input, and we don't guarantee
                     * constant-timeness
                     */
                    if (!BN_nnmod(tmp_scalar, p_scalar, group->order, ctx)) {
                        ECerr(EC_F_EC_GFP_NISTP256_POINTS_MUL, ERR_R_BN_LIB);
                        goto err;
                    }
                    num_bytes = BN_bn2bin(tmp_scalar, tmp);
                } else
                    num_bytes = BN_bn2bin(p_scalar, tmp);
                flip_endian(secrets[i], tmp, num_bytes);
                /* precompute multiples */
                if ((!BN_to_felem(x_out, p->X)) ||
                    (!BN_to_felem(y_out, p->Y)) ||
                    (!BN_to_felem(z_out, p->Z)))
                    goto err;
                felem_shrink(pre_comp[i][1][0], x_out);
                felem_shrink(pre_comp[i][1][1], y_out);
                felem_shrink(pre_comp[i][1][2], z_out);
                for (j = 2; j <= 16; ++j) {
                    if (j & 1) {
                        point_add_small(pre_comp[i][j][0], pre_comp[i][j][1],
                                        pre_comp[i][j][2], pre_comp[i][1][0],
                                        pre_comp[i][1][1], pre_comp[i][1][2],
                                        pre_comp[i][j - 1][0],
                                        pre_comp[i][j - 1][1],
                                        pre_comp[i][j - 1][2]);
                    } else {
                        point_double_small(pre_comp[i][j][0],
                                           pre_comp[i][j][1],
                                           pre_comp[i][j][2],
                                           pre_comp[i][j / 2][0],
                                           pre_comp[i][j / 2][1],
                                           pre_comp[i][j / 2][2]);
                    }
                }
            }
        }
        if (mixed)
            make_points_affine(num_points * 17, pre_comp[0], tmp_smallfelems);
    }

    /* the scalar for the generator */
    if ((scalar != NULL) && (have_pre_comp)) {
        memset(g_secret, 0, sizeof(g_secret));
        /* reduce scalar to 0 <= scalar < 2^256 */
        if ((BN_num_bits(scalar) > 256) || (BN_is_negative(scalar))) {
            /*
             * this is an unusual input, and we don't guarantee
             * constant-timeness
             */
            if (!BN_nnmod(tmp_scalar, scalar, group->order, ctx)) {
                ECerr(EC_F_EC_GFP_NISTP256_POINTS_MUL, ERR_R_BN_LIB);
                goto err;
            }
            num_bytes = BN_bn2bin(tmp_scalar, tmp);
        } else
            num_bytes = BN_bn2bin(scalar, tmp);
        flip_endian(g_secret, tmp, num_bytes);
        /* do the multiplication with generator precomputation */
        batch_mul(x_out, y_out, z_out,
                  (const felem_bytearray(*))secrets, num_points,
                  g_secret,
                  mixed, (const smallfelem(*)[17][3])pre_comp, g_pre_comp);
    } else
        /* do the multiplication without generator precomputation */
        batch_mul(x_out, y_out, z_out,
                  (const felem_bytearray(*))secrets, num_points,
                  NULL, mixed, (const smallfelem(*)[17][3])pre_comp, NULL);
    /* reduce the output to its unique minimal representation */
    felem_contract(x_in, x_out);
    felem_contract(y_in, y_out);
    felem_contract(z_in, z_out);
    if ((!smallfelem_to_BN(x, x_in)) || (!smallfelem_to_BN(y, y_in)) ||
        (!smallfelem_to_BN(z, z_in))) {
        ECerr(EC_F_EC_GFP_NISTP256_POINTS_MUL, ERR_R_BN_LIB);
        goto err;
    }
    ret = EC_POINT_set_Jprojective_coordinates_GFp(group, r, x, y, z, ctx);

 err:
    BN_CTX_end(ctx);
    EC_POINT_free(generator);
    OPENSSL_free(secrets);
    OPENSSL_free(pre_comp);
    OPENSSL_free(tmp_smallfelems);
    return ret;
}

int ec_GFp_nistp256_precompute_mult(EC_GROUP *group, BN_CTX *ctx)
{
    int ret = 0;
    NISTP256_PRE_COMP *pre = NULL;
    int i, j;
    BN_CTX *new_ctx = NULL;
    BIGNUM *x, *y;
    EC_POINT *generator = NULL;
    smallfelem tmp_smallfelems[32];
    felem x_tmp, y_tmp, z_tmp;

    /* throw away old precomputation */
    EC_pre_comp_free(group);
    if (ctx == NULL)
        if ((ctx = new_ctx = BN_CTX_new()) == NULL)
            return 0;
    BN_CTX_start(ctx);
    x = BN_CTX_get(ctx);
    y = BN_CTX_get(ctx);
    if (y == NULL)
        goto err;
    /* get the generator */
    if (group->generator == NULL)
        goto err;
    generator = EC_POINT_new(group);
    if (generator == NULL)
        goto err;
    BN_bin2bn(nistp256_curve_params[3], sizeof(felem_bytearray), x);
    BN_bin2bn(nistp256_curve_params[4], sizeof(felem_bytearray), y);
    if (!EC_POINT_set_affine_coordinates(group, generator, x, y, ctx))
        goto err;
    if ((pre = nistp256_pre_comp_new()) == NULL)
        goto err;
    /*
     * if the generator is the standard one, use built-in precomputation
     */
    if (0 == EC_POINT_cmp(group, generator, group->generator, ctx)) {
        memcpy(pre->g_pre_comp, gmul, sizeof(pre->g_pre_comp));
        goto done;
    }
    if ((!BN_to_felem(x_tmp, group->generator->X)) ||
        (!BN_to_felem(y_tmp, group->generator->Y)) ||
        (!BN_to_felem(z_tmp, group->generator->Z)))
        goto err;
    felem_shrink(pre->g_pre_comp[0][1][0], x_tmp);
    felem_shrink(pre->g_pre_comp[0][1][1], y_tmp);
    felem_shrink(pre->g_pre_comp[0][1][2], z_tmp);
    /*
     * compute 2^64*G, 2^128*G, 2^192*G for the first table, 2^32*G, 2^96*G,
     * 2^160*G, 2^224*G for the second one
     */
    for (i = 1; i <= 8; i <<= 1) {
        point_double_small(pre->g_pre_comp[1][i][0], pre->g_pre_comp[1][i][1],
                           pre->g_pre_comp[1][i][2], pre->g_pre_comp[0][i][0],
                           pre->g_pre_comp[0][i][1],
                           pre->g_pre_comp[0][i][2]);
        for (j = 0; j < 31; ++j) {
            point_double_small(pre->g_pre_comp[1][i][0],
                               pre->g_pre_comp[1][i][1],
                               pre->g_pre_comp[1][i][2],
                               pre->g_pre_comp[1][i][0],
                               pre->g_pre_comp[1][i][1],
                               pre->g_pre_comp[1][i][2]);
        }
        if (i == 8)
            break;
        point_double_small(pre->g_pre_comp[0][2 * i][0],
                           pre->g_pre_comp[0][2 * i][1],
                           pre->g_pre_comp[0][2 * i][2],
                           pre->g_pre_comp[1][i][0], pre->g_pre_comp[1][i][1],
                           pre->g_pre_comp[1][i][2]);
        for (j = 0; j < 31; ++j) {
            point_double_small(pre->g_pre_comp[0][2 * i][0],
                               pre->g_pre_comp[0][2 * i][1],
                               pre->g_pre_comp[0][2 * i][2],
                               pre->g_pre_comp[0][2 * i][0],
                               pre->g_pre_comp[0][2 * i][1],
                               pre->g_pre_comp[0][2 * i][2]);
        }
    }
    for (i = 0; i < 2; i++) {
        /* g_pre_comp[i][0] is the point at infinity */
        memset(pre->g_pre_comp[i][0], 0, sizeof(pre->g_pre_comp[i][0]));
        /* the remaining multiples */
        /* 2^64*G + 2^128*G resp. 2^96*G + 2^160*G */
        point_add_small(pre->g_pre_comp[i][6][0], pre->g_pre_comp[i][6][1],
                        pre->g_pre_comp[i][6][2], pre->g_pre_comp[i][4][0],
                        pre->g_pre_comp[i][4][1], pre->g_pre_comp[i][4][2],
                        pre->g_pre_comp[i][2][0], pre->g_pre_comp[i][2][1],
                        pre->g_pre_comp[i][2][2]);
        /* 2^64*G + 2^192*G resp. 2^96*G + 2^224*G */
        point_add_small(pre->g_pre_comp[i][10][0], pre->g_pre_comp[i][10][1],
                        pre->g_pre_comp[i][10][2], pre->g_pre_comp[i][8][0],
                        pre->g_pre_comp[i][8][1], pre->g_pre_comp[i][8][2],
                        pre->g_pre_comp[i][2][0], pre->g_pre_comp[i][2][1],
                        pre->g_pre_comp[i][2][2]);
        /* 2^128*G + 2^192*G resp. 2^160*G + 2^224*G */
        point_add_small(pre->g_pre_comp[i][12][0], pre->g_pre_comp[i][12][1],
                        pre->g_pre_comp[i][12][2], pre->g_pre_comp[i][8][0],
                        pre->g_pre_comp[i][8][1], pre->g_pre_comp[i][8][2],
                        pre->g_pre_comp[i][4][0], pre->g_pre_comp[i][4][1],
                        pre->g_pre_comp[i][4][2]);
        /*
         * 2^64*G + 2^128*G + 2^192*G resp. 2^96*G + 2^160*G + 2^224*G
         */
        point_add_small(pre->g_pre_comp[i][14][0], pre->g_pre_comp[i][14][1],
                        pre->g_pre_comp[i][14][2], pre->g_pre_comp[i][12][0],
                        pre->g_pre_comp[i][12][1], pre->g_pre_comp[i][12][2],
                        pre->g_pre_comp[i][2][0], pre->g_pre_comp[i][2][1],
                        pre->g_pre_comp[i][2][2]);
        for (j = 1; j < 8; ++j) {
            /* odd multiples: add G resp. 2^32*G */
            point_add_small(pre->g_pre_comp[i][2 * j + 1][0],
                            pre->g_pre_comp[i][2 * j + 1][1],
                            pre->g_pre_comp[i][2 * j + 1][2],
                            pre->g_pre_comp[i][2 * j][0],
                            pre->g_pre_comp[i][2 * j][1],
                            pre->g_pre_comp[i][2 * j][2],
                            pre->g_pre_comp[i][1][0],
                            pre->g_pre_comp[i][1][1],
                            pre->g_pre_comp[i][1][2]);
        }
    }
    make_points_affine(31, &(pre->g_pre_comp[0][1]), tmp_smallfelems);

 done:
    SETPRECOMP(group, nistp256, pre);
    pre = NULL;
    ret = 1;

 err:
    BN_CTX_end(ctx);
    EC_POINT_free(generator);
    BN_CTX_free(new_ctx);
    EC_nistp256_pre_comp_free(pre);
    return ret;
}

int ec_GFp_nistp256_have_precompute_mult(const EC_GROUP *group)
{
    return HAVEPRECOMP(group, nistp256);
}
#endif
