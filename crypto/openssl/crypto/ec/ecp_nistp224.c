/*
 * Copyright 2010-2019 The OpenSSL Project Authors. All Rights Reserved.
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
 * A 64-bit implementation of the NIST P-224 elliptic curve point multiplication
 *
 * Inspired by Daniel J. Bernstein's public domain nistp224 implementation
 * and Adam Langley's public domain 64-bit C implementation of curve25519
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
# else
#  error "Your compiler doesn't appear to support 128-bit integer types"
# endif

typedef uint8_t u8;
typedef uint64_t u64;

/******************************************************************************/
/*-
 * INTERNAL REPRESENTATION OF FIELD ELEMENTS
 *
 * Field elements are represented as a_0 + 2^56*a_1 + 2^112*a_2 + 2^168*a_3
 * using 64-bit coefficients called 'limbs',
 * and sometimes (for multiplication results) as
 * b_0 + 2^56*b_1 + 2^112*b_2 + 2^168*b_3 + 2^224*b_4 + 2^280*b_5 + 2^336*b_6
 * using 128-bit coefficients called 'widelimbs'.
 * A 4-limb representation is an 'felem';
 * a 7-widelimb representation is a 'widefelem'.
 * Even within felems, bits of adjacent limbs overlap, and we don't always
 * reduce the representations: we ensure that inputs to each felem
 * multiplication satisfy a_i < 2^60, so outputs satisfy b_i < 4*2^60*2^60,
 * and fit into a 128-bit word without overflow. The coefficients are then
 * again partially reduced to obtain an felem satisfying a_i < 2^57.
 * We only reduce to the unique minimal representation at the end of the
 * computation.
 */

typedef uint64_t limb;
typedef uint128_t widelimb;

typedef limb felem[4];
typedef widelimb widefelem[7];

/*
 * Field element represented as a byte array. 28*8 = 224 bits is also the
 * group order size for the elliptic curve, and we also use this type for
 * scalars for point multiplication.
 */
typedef u8 felem_bytearray[28];

static const felem_bytearray nistp224_curve_params[5] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* p */
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* a */
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE},
    {0xB4, 0x05, 0x0A, 0x85, 0x0C, 0x04, 0xB3, 0xAB, 0xF5, 0x41, /* b */
     0x32, 0x56, 0x50, 0x44, 0xB0, 0xB7, 0xD7, 0xBF, 0xD8, 0xBA,
     0x27, 0x0B, 0x39, 0x43, 0x23, 0x55, 0xFF, 0xB4},
    {0xB7, 0x0E, 0x0C, 0xBD, 0x6B, 0xB4, 0xBF, 0x7F, 0x32, 0x13, /* x */
     0x90, 0xB9, 0x4A, 0x03, 0xC1, 0xD3, 0x56, 0xC2, 0x11, 0x22,
     0x34, 0x32, 0x80, 0xD6, 0x11, 0x5C, 0x1D, 0x21},
    {0xbd, 0x37, 0x63, 0x88, 0xb5, 0xf7, 0x23, 0xfb, 0x4c, 0x22, /* y */
     0xdf, 0xe6, 0xcd, 0x43, 0x75, 0xa0, 0x5a, 0x07, 0x47, 0x64,
     0x44, 0xd5, 0x81, 0x99, 0x85, 0x00, 0x7e, 0x34}
};

/*-
 * Precomputed multiples of the standard generator
 * Points are given in coordinates (X, Y, Z) where Z normally is 1
 * (0 for the point at infinity).
 * For each field element, slice a_0 is word 0, etc.
 *
 * The table has 2 * 16 elements, starting with the following:
 * index | bits    | point
 * ------+---------+------------------------------
 *     0 | 0 0 0 0 | 0G
 *     1 | 0 0 0 1 | 1G
 *     2 | 0 0 1 0 | 2^56G
 *     3 | 0 0 1 1 | (2^56 + 1)G
 *     4 | 0 1 0 0 | 2^112G
 *     5 | 0 1 0 1 | (2^112 + 1)G
 *     6 | 0 1 1 0 | (2^112 + 2^56)G
 *     7 | 0 1 1 1 | (2^112 + 2^56 + 1)G
 *     8 | 1 0 0 0 | 2^168G
 *     9 | 1 0 0 1 | (2^168 + 1)G
 *    10 | 1 0 1 0 | (2^168 + 2^56)G
 *    11 | 1 0 1 1 | (2^168 + 2^56 + 1)G
 *    12 | 1 1 0 0 | (2^168 + 2^112)G
 *    13 | 1 1 0 1 | (2^168 + 2^112 + 1)G
 *    14 | 1 1 1 0 | (2^168 + 2^112 + 2^56)G
 *    15 | 1 1 1 1 | (2^168 + 2^112 + 2^56 + 1)G
 * followed by a copy of this with each element multiplied by 2^28.
 *
 * The reason for this is so that we can clock bits into four different
 * locations when doing simple scalar multiplies against the base point,
 * and then another four locations using the second 16 elements.
 */
static const felem gmul[2][16][3] = {
{{{0, 0, 0, 0},
  {0, 0, 0, 0},
  {0, 0, 0, 0}},
 {{0x3280d6115c1d21, 0xc1d356c2112234, 0x7f321390b94a03, 0xb70e0cbd6bb4bf},
  {0xd5819985007e34, 0x75a05a07476444, 0xfb4c22dfe6cd43, 0xbd376388b5f723},
  {1, 0, 0, 0}},
 {{0xfd9675666ebbe9, 0xbca7664d40ce5e, 0x2242df8d8a2a43, 0x1f49bbb0f99bc5},
  {0x29e0b892dc9c43, 0xece8608436e662, 0xdc858f185310d0, 0x9812dd4eb8d321},
  {1, 0, 0, 0}},
 {{0x6d3e678d5d8eb8, 0x559eed1cb362f1, 0x16e9a3bbce8a3f, 0xeedcccd8c2a748},
  {0xf19f90ed50266d, 0xabf2b4bf65f9df, 0x313865468fafec, 0x5cb379ba910a17},
  {1, 0, 0, 0}},
 {{0x0641966cab26e3, 0x91fb2991fab0a0, 0xefec27a4e13a0b, 0x0499aa8a5f8ebe},
  {0x7510407766af5d, 0x84d929610d5450, 0x81d77aae82f706, 0x6916f6d4338c5b},
  {1, 0, 0, 0}},
 {{0xea95ac3b1f15c6, 0x086000905e82d4, 0xdd323ae4d1c8b1, 0x932b56be7685a3},
  {0x9ef93dea25dbbf, 0x41665960f390f0, 0xfdec76dbe2a8a7, 0x523e80f019062a},
  {1, 0, 0, 0}},
 {{0x822fdd26732c73, 0xa01c83531b5d0f, 0x363f37347c1ba4, 0xc391b45c84725c},
  {0xbbd5e1b2d6ad24, 0xddfbcde19dfaec, 0xc393da7e222a7f, 0x1efb7890ede244},
  {1, 0, 0, 0}},
 {{0x4c9e90ca217da1, 0xd11beca79159bb, 0xff8d33c2c98b7c, 0x2610b39409f849},
  {0x44d1352ac64da0, 0xcdbb7b2c46b4fb, 0x966c079b753c89, 0xfe67e4e820b112},
  {1, 0, 0, 0}},
 {{0xe28cae2df5312d, 0xc71b61d16f5c6e, 0x79b7619a3e7c4c, 0x05c73240899b47},
  {0x9f7f6382c73e3a, 0x18615165c56bda, 0x641fab2116fd56, 0x72855882b08394},
  {1, 0, 0, 0}},
 {{0x0469182f161c09, 0x74a98ca8d00fb5, 0xb89da93489a3e0, 0x41c98768fb0c1d},
  {0xe5ea05fb32da81, 0x3dce9ffbca6855, 0x1cfe2d3fbf59e6, 0x0e5e03408738a7},
  {1, 0, 0, 0}},
 {{0xdab22b2333e87f, 0x4430137a5dd2f6, 0xe03ab9f738beb8, 0xcb0c5d0dc34f24},
  {0x764a7df0c8fda5, 0x185ba5c3fa2044, 0x9281d688bcbe50, 0xc40331df893881},
  {1, 0, 0, 0}},
 {{0xb89530796f0f60, 0xade92bd26909a3, 0x1a0c83fb4884da, 0x1765bf22a5a984},
  {0x772a9ee75db09e, 0x23bc6c67cec16f, 0x4c1edba8b14e2f, 0xe2a215d9611369},
  {1, 0, 0, 0}},
 {{0x571e509fb5efb3, 0xade88696410552, 0xc8ae85fada74fe, 0x6c7e4be83bbde3},
  {0xff9f51160f4652, 0xb47ce2495a6539, 0xa2946c53b582f4, 0x286d2db3ee9a60},
  {1, 0, 0, 0}},
 {{0x40bbd5081a44af, 0x0995183b13926c, 0xbcefba6f47f6d0, 0x215619e9cc0057},
  {0x8bc94d3b0df45e, 0xf11c54a3694f6f, 0x8631b93cdfe8b5, 0xe7e3f4b0982db9},
  {1, 0, 0, 0}},
 {{0xb17048ab3e1c7b, 0xac38f36ff8a1d8, 0x1c29819435d2c6, 0xc813132f4c07e9},
  {0x2891425503b11f, 0x08781030579fea, 0xf5426ba5cc9674, 0x1e28ebf18562bc},
  {1, 0, 0, 0}},
 {{0x9f31997cc864eb, 0x06cd91d28b5e4c, 0xff17036691a973, 0xf1aef351497c58},
  {0xdd1f2d600564ff, 0xdead073b1402db, 0x74a684435bd693, 0xeea7471f962558},
  {1, 0, 0, 0}}},
{{{0, 0, 0, 0},
  {0, 0, 0, 0},
  {0, 0, 0, 0}},
 {{0x9665266dddf554, 0x9613d78b60ef2d, 0xce27a34cdba417, 0xd35ab74d6afc31},
  {0x85ccdd22deb15e, 0x2137e5783a6aab, 0xa141cffd8c93c6, 0x355a1830e90f2d},
  {1, 0, 0, 0}},
 {{0x1a494eadaade65, 0xd6da4da77fe53c, 0xe7992996abec86, 0x65c3553c6090e3},
  {0xfa610b1fb09346, 0xf1c6540b8a4aaf, 0xc51a13ccd3cbab, 0x02995b1b18c28a},
  {1, 0, 0, 0}},
 {{0x7874568e7295ef, 0x86b419fbe38d04, 0xdc0690a7550d9a, 0xd3966a44beac33},
  {0x2b7280ec29132f, 0xbeaa3b6a032df3, 0xdc7dd88ae41200, 0xd25e2513e3a100},
  {1, 0, 0, 0}},
 {{0x924857eb2efafd, 0xac2bce41223190, 0x8edaa1445553fc, 0x825800fd3562d5},
  {0x8d79148ea96621, 0x23a01c3dd9ed8d, 0xaf8b219f9416b5, 0xd8db0cc277daea},
  {1, 0, 0, 0}},
 {{0x76a9c3b1a700f0, 0xe9acd29bc7e691, 0x69212d1a6b0327, 0x6322e97fe154be},
  {0x469fc5465d62aa, 0x8d41ed18883b05, 0x1f8eae66c52b88, 0xe4fcbe9325be51},
  {1, 0, 0, 0}},
 {{0x825fdf583cac16, 0x020b857c7b023a, 0x683c17744b0165, 0x14ffd0a2daf2f1},
  {0x323b36184218f9, 0x4944ec4e3b47d4, 0xc15b3080841acf, 0x0bced4b01a28bb},
  {1, 0, 0, 0}},
 {{0x92ac22230df5c4, 0x52f33b4063eda8, 0xcb3f19870c0c93, 0x40064f2ba65233},
  {0xfe16f0924f8992, 0x012da25af5b517, 0x1a57bb24f723a6, 0x06f8bc76760def},
  {1, 0, 0, 0}},
 {{0x4a7084f7817cb9, 0xbcab0738ee9a78, 0x3ec11e11d9c326, 0xdc0fe90e0f1aae},
  {0xcf639ea5f98390, 0x5c350aa22ffb74, 0x9afae98a4047b7, 0x956ec2d617fc45},
  {1, 0, 0, 0}},
 {{0x4306d648c1be6a, 0x9247cd8bc9a462, 0xf5595e377d2f2e, 0xbd1c3caff1a52e},
  {0x045e14472409d0, 0x29f3e17078f773, 0x745a602b2d4f7d, 0x191837685cdfbb},
  {1, 0, 0, 0}},
 {{0x5b6ee254a8cb79, 0x4953433f5e7026, 0xe21faeb1d1def4, 0xc4c225785c09de},
  {0x307ce7bba1e518, 0x31b125b1036db8, 0x47e91868839e8f, 0xc765866e33b9f3},
  {1, 0, 0, 0}},
 {{0x3bfece24f96906, 0x4794da641e5093, 0xde5df64f95db26, 0x297ecd89714b05},
  {0x701bd3ebb2c3aa, 0x7073b4f53cb1d5, 0x13c5665658af16, 0x9895089d66fe58},
  {1, 0, 0, 0}},
 {{0x0fef05f78c4790, 0x2d773633b05d2e, 0x94229c3a951c94, 0xbbbd70df4911bb},
  {0xb2c6963d2c1168, 0x105f47a72b0d73, 0x9fdf6111614080, 0x7b7e94b39e67b0},
  {1, 0, 0, 0}},
 {{0xad1a7d6efbe2b3, 0xf012482c0da69d, 0x6b3bdf12438345, 0x40d7558d7aa4d9},
  {0x8a09fffb5c6d3d, 0x9a356e5d9ffd38, 0x5973f15f4f9b1c, 0xdcd5f59f63c3ea},
  {1, 0, 0, 0}},
 {{0xacf39f4c5ca7ab, 0x4c8071cc5fd737, 0xc64e3602cd1184, 0x0acd4644c9abba},
  {0x6c011a36d8bf6e, 0xfecd87ba24e32a, 0x19f6f56574fad8, 0x050b204ced9405},
  {1, 0, 0, 0}},
 {{0xed4f1cae7d9a96, 0x5ceef7ad94c40a, 0x778e4a3bf3ef9b, 0x7405783dc3b55e},
  {0x32477c61b6e8c6, 0xb46a97570f018b, 0x91176d0a7e95d1, 0x3df90fbc4c7d0e},
  {1, 0, 0, 0}}}
};

/* Precomputation for the group generator. */
struct nistp224_pre_comp_st {
    felem g_pre_comp[2][16][3];
    CRYPTO_REF_COUNT references;
    CRYPTO_RWLOCK *lock;
};

const EC_METHOD *EC_GFp_nistp224_method(void)
{
    static const EC_METHOD ret = {
        EC_FLAGS_DEFAULT_OCT,
        NID_X9_62_prime_field,
        ec_GFp_nistp224_group_init,
        ec_GFp_simple_group_finish,
        ec_GFp_simple_group_clear_finish,
        ec_GFp_nist_group_copy,
        ec_GFp_nistp224_group_set_curve,
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
        ec_GFp_nistp224_point_get_affine_coordinates,
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
        ec_GFp_nistp224_points_mul,
        ec_GFp_nistp224_precompute_mult,
        ec_GFp_nistp224_have_precompute_mult,
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

/*
 * Helper functions to convert field elements to/from internal representation
 */
static void bin28_to_felem(felem out, const u8 in[28])
{
    out[0] = *((const uint64_t *)(in)) & 0x00ffffffffffffff;
    out[1] = (*((const uint64_t *)(in + 7))) & 0x00ffffffffffffff;
    out[2] = (*((const uint64_t *)(in + 14))) & 0x00ffffffffffffff;
    out[3] = (*((const uint64_t *)(in+20))) >> 8;
}

static void felem_to_bin28(u8 out[28], const felem in)
{
    unsigned i;
    for (i = 0; i < 7; ++i) {
        out[i] = in[0] >> (8 * i);
        out[i + 7] = in[1] >> (8 * i);
        out[i + 14] = in[2] >> (8 * i);
        out[i + 21] = in[3] >> (8 * i);
    }
}

/* To preserve endianness when using BN_bn2bin and BN_bin2bn */
static void flip_endian(u8 *out, const u8 *in, unsigned len)
{
    unsigned i;
    for (i = 0; i < len; ++i)
        out[i] = in[len - 1 - i];
}

/* From OpenSSL BIGNUM to internal representation */
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
    bin28_to_felem(out, b_out);
    return 1;
}

/* From internal representation to OpenSSL BIGNUM */
static BIGNUM *felem_to_BN(BIGNUM *out, const felem in)
{
    felem_bytearray b_in, b_out;
    felem_to_bin28(b_in, in);
    flip_endian(b_out, b_in, sizeof(b_out));
    return BN_bin2bn(b_out, sizeof(b_out), out);
}

/******************************************************************************/
/*-
 *                              FIELD OPERATIONS
 *
 * Field operations, using the internal representation of field elements.
 * NB! These operations are specific to our point multiplication and cannot be
 * expected to be correct in general - e.g., multiplication with a large scalar
 * will cause an overflow.
 *
 */

static void felem_one(felem out)
{
    out[0] = 1;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
}

static void felem_assign(felem out, const felem in)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
}

/* Sum two field elements: out += in */
static void felem_sum(felem out, const felem in)
{
    out[0] += in[0];
    out[1] += in[1];
    out[2] += in[2];
    out[3] += in[3];
}

/* Subtract field elements: out -= in */
/* Assumes in[i] < 2^57 */
static void felem_diff(felem out, const felem in)
{
    static const limb two58p2 = (((limb) 1) << 58) + (((limb) 1) << 2);
    static const limb two58m2 = (((limb) 1) << 58) - (((limb) 1) << 2);
    static const limb two58m42m2 = (((limb) 1) << 58) -
        (((limb) 1) << 42) - (((limb) 1) << 2);

    /* Add 0 mod 2^224-2^96+1 to ensure out > in */
    out[0] += two58p2;
    out[1] += two58m42m2;
    out[2] += two58m2;
    out[3] += two58m2;

    out[0] -= in[0];
    out[1] -= in[1];
    out[2] -= in[2];
    out[3] -= in[3];
}

/* Subtract in unreduced 128-bit mode: out -= in */
/* Assumes in[i] < 2^119 */
static void widefelem_diff(widefelem out, const widefelem in)
{
    static const widelimb two120 = ((widelimb) 1) << 120;
    static const widelimb two120m64 = (((widelimb) 1) << 120) -
        (((widelimb) 1) << 64);
    static const widelimb two120m104m64 = (((widelimb) 1) << 120) -
        (((widelimb) 1) << 104) - (((widelimb) 1) << 64);

    /* Add 0 mod 2^224-2^96+1 to ensure out > in */
    out[0] += two120;
    out[1] += two120m64;
    out[2] += two120m64;
    out[3] += two120;
    out[4] += two120m104m64;
    out[5] += two120m64;
    out[6] += two120m64;

    out[0] -= in[0];
    out[1] -= in[1];
    out[2] -= in[2];
    out[3] -= in[3];
    out[4] -= in[4];
    out[5] -= in[5];
    out[6] -= in[6];
}

/* Subtract in mixed mode: out128 -= in64 */
/* in[i] < 2^63 */
static void felem_diff_128_64(widefelem out, const felem in)
{
    static const widelimb two64p8 = (((widelimb) 1) << 64) +
        (((widelimb) 1) << 8);
    static const widelimb two64m8 = (((widelimb) 1) << 64) -
        (((widelimb) 1) << 8);
    static const widelimb two64m48m8 = (((widelimb) 1) << 64) -
        (((widelimb) 1) << 48) - (((widelimb) 1) << 8);

    /* Add 0 mod 2^224-2^96+1 to ensure out > in */
    out[0] += two64p8;
    out[1] += two64m48m8;
    out[2] += two64m8;
    out[3] += two64m8;

    out[0] -= in[0];
    out[1] -= in[1];
    out[2] -= in[2];
    out[3] -= in[3];
}

/*
 * Multiply a field element by a scalar: out = out * scalar The scalars we
 * actually use are small, so results fit without overflow
 */
static void felem_scalar(felem out, const limb scalar)
{
    out[0] *= scalar;
    out[1] *= scalar;
    out[2] *= scalar;
    out[3] *= scalar;
}

/*
 * Multiply an unreduced field element by a scalar: out = out * scalar The
 * scalars we actually use are small, so results fit without overflow
 */
static void widefelem_scalar(widefelem out, const widelimb scalar)
{
    out[0] *= scalar;
    out[1] *= scalar;
    out[2] *= scalar;
    out[3] *= scalar;
    out[4] *= scalar;
    out[5] *= scalar;
    out[6] *= scalar;
}

/* Square a field element: out = in^2 */
static void felem_square(widefelem out, const felem in)
{
    limb tmp0, tmp1, tmp2;
    tmp0 = 2 * in[0];
    tmp1 = 2 * in[1];
    tmp2 = 2 * in[2];
    out[0] = ((widelimb) in[0]) * in[0];
    out[1] = ((widelimb) in[0]) * tmp1;
    out[2] = ((widelimb) in[0]) * tmp2 + ((widelimb) in[1]) * in[1];
    out[3] = ((widelimb) in[3]) * tmp0 + ((widelimb) in[1]) * tmp2;
    out[4] = ((widelimb) in[3]) * tmp1 + ((widelimb) in[2]) * in[2];
    out[5] = ((widelimb) in[3]) * tmp2;
    out[6] = ((widelimb) in[3]) * in[3];
}

/* Multiply two field elements: out = in1 * in2 */
static void felem_mul(widefelem out, const felem in1, const felem in2)
{
    out[0] = ((widelimb) in1[0]) * in2[0];
    out[1] = ((widelimb) in1[0]) * in2[1] + ((widelimb) in1[1]) * in2[0];
    out[2] = ((widelimb) in1[0]) * in2[2] + ((widelimb) in1[1]) * in2[1] +
             ((widelimb) in1[2]) * in2[0];
    out[3] = ((widelimb) in1[0]) * in2[3] + ((widelimb) in1[1]) * in2[2] +
             ((widelimb) in1[2]) * in2[1] + ((widelimb) in1[3]) * in2[0];
    out[4] = ((widelimb) in1[1]) * in2[3] + ((widelimb) in1[2]) * in2[2] +
             ((widelimb) in1[3]) * in2[1];
    out[5] = ((widelimb) in1[2]) * in2[3] + ((widelimb) in1[3]) * in2[2];
    out[6] = ((widelimb) in1[3]) * in2[3];
}

/*-
 * Reduce seven 128-bit coefficients to four 64-bit coefficients.
 * Requires in[i] < 2^126,
 * ensures out[0] < 2^56, out[1] < 2^56, out[2] < 2^56, out[3] <= 2^56 + 2^16 */
static void felem_reduce(felem out, const widefelem in)
{
    static const widelimb two127p15 = (((widelimb) 1) << 127) +
        (((widelimb) 1) << 15);
    static const widelimb two127m71 = (((widelimb) 1) << 127) -
        (((widelimb) 1) << 71);
    static const widelimb two127m71m55 = (((widelimb) 1) << 127) -
        (((widelimb) 1) << 71) - (((widelimb) 1) << 55);
    widelimb output[5];

    /* Add 0 mod 2^224-2^96+1 to ensure all differences are positive */
    output[0] = in[0] + two127p15;
    output[1] = in[1] + two127m71m55;
    output[2] = in[2] + two127m71;
    output[3] = in[3];
    output[4] = in[4];

    /* Eliminate in[4], in[5], in[6] */
    output[4] += in[6] >> 16;
    output[3] += (in[6] & 0xffff) << 40;
    output[2] -= in[6];

    output[3] += in[5] >> 16;
    output[2] += (in[5] & 0xffff) << 40;
    output[1] -= in[5];

    output[2] += output[4] >> 16;
    output[1] += (output[4] & 0xffff) << 40;
    output[0] -= output[4];

    /* Carry 2 -> 3 -> 4 */
    output[3] += output[2] >> 56;
    output[2] &= 0x00ffffffffffffff;

    output[4] = output[3] >> 56;
    output[3] &= 0x00ffffffffffffff;

    /* Now output[2] < 2^56, output[3] < 2^56, output[4] < 2^72 */

    /* Eliminate output[4] */
    output[2] += output[4] >> 16;
    /* output[2] < 2^56 + 2^56 = 2^57 */
    output[1] += (output[4] & 0xffff) << 40;
    output[0] -= output[4];

    /* Carry 0 -> 1 -> 2 -> 3 */
    output[1] += output[0] >> 56;
    out[0] = output[0] & 0x00ffffffffffffff;

    output[2] += output[1] >> 56;
    /* output[2] < 2^57 + 2^72 */
    out[1] = output[1] & 0x00ffffffffffffff;
    output[3] += output[2] >> 56;
    /* output[3] <= 2^56 + 2^16 */
    out[2] = output[2] & 0x00ffffffffffffff;

    /*-
     * out[0] < 2^56, out[1] < 2^56, out[2] < 2^56,
     * out[3] <= 2^56 + 2^16 (due to final carry),
     * so out < 2*p
     */
    out[3] = output[3];
}

static void felem_square_reduce(felem out, const felem in)
{
    widefelem tmp;
    felem_square(tmp, in);
    felem_reduce(out, tmp);
}

static void felem_mul_reduce(felem out, const felem in1, const felem in2)
{
    widefelem tmp;
    felem_mul(tmp, in1, in2);
    felem_reduce(out, tmp);
}

/*
 * Reduce to unique minimal representation. Requires 0 <= in < 2*p (always
 * call felem_reduce first)
 */
static void felem_contract(felem out, const felem in)
{
    static const int64_t two56 = ((limb) 1) << 56;
    /* 0 <= in < 2*p, p = 2^224 - 2^96 + 1 */
    /* if in > p , reduce in = in - 2^224 + 2^96 - 1 */
    int64_t tmp[4], a;
    tmp[0] = in[0];
    tmp[1] = in[1];
    tmp[2] = in[2];
    tmp[3] = in[3];
    /* Case 1: a = 1 iff in >= 2^224 */
    a = (in[3] >> 56);
    tmp[0] -= a;
    tmp[1] += a << 40;
    tmp[3] &= 0x00ffffffffffffff;
    /*
     * Case 2: a = 0 iff p <= in < 2^224, i.e., the high 128 bits are all 1
     * and the lower part is non-zero
     */
    a = ((in[3] & in[2] & (in[1] | 0x000000ffffffffff)) + 1) |
        (((int64_t) (in[0] + (in[1] & 0x000000ffffffffff)) - 1) >> 63);
    a &= 0x00ffffffffffffff;
    /* turn a into an all-one mask (if a = 0) or an all-zero mask */
    a = (a - 1) >> 63;
    /* subtract 2^224 - 2^96 + 1 if a is all-one */
    tmp[3] &= a ^ 0xffffffffffffffff;
    tmp[2] &= a ^ 0xffffffffffffffff;
    tmp[1] &= (a ^ 0xffffffffffffffff) | 0x000000ffffffffff;
    tmp[0] -= 1 & a;

    /*
     * eliminate negative coefficients: if tmp[0] is negative, tmp[1] must be
     * non-zero, so we only need one step
     */
    a = tmp[0] >> 63;
    tmp[0] += two56 & a;
    tmp[1] -= 1 & a;

    /* carry 1 -> 2 -> 3 */
    tmp[2] += tmp[1] >> 56;
    tmp[1] &= 0x00ffffffffffffff;

    tmp[3] += tmp[2] >> 56;
    tmp[2] &= 0x00ffffffffffffff;

    /* Now 0 <= out < p */
    out[0] = tmp[0];
    out[1] = tmp[1];
    out[2] = tmp[2];
    out[3] = tmp[3];
}

/*
 * Get negative value: out = -in
 * Requires in[i] < 2^63,
 * ensures out[0] < 2^56, out[1] < 2^56, out[2] < 2^56, out[3] <= 2^56 + 2^16
 */
static void felem_neg(felem out, const felem in)
{
    widefelem tmp = {0};
    felem_diff_128_64(tmp, in);
    felem_reduce(out, tmp);
}

/*
 * Zero-check: returns 1 if input is 0, and 0 otherwise. We know that field
 * elements are reduced to in < 2^225, so we only need to check three cases:
 * 0, 2^224 - 2^96 + 1, and 2^225 - 2^97 + 2
 */
static limb felem_is_zero(const felem in)
{
    limb zero, two224m96p1, two225m97p2;

    zero = in[0] | in[1] | in[2] | in[3];
    zero = (((int64_t) (zero) - 1) >> 63) & 1;
    two224m96p1 = (in[0] ^ 1) | (in[1] ^ 0x00ffff0000000000)
        | (in[2] ^ 0x00ffffffffffffff) | (in[3] ^ 0x00ffffffffffffff);
    two224m96p1 = (((int64_t) (two224m96p1) - 1) >> 63) & 1;
    two225m97p2 = (in[0] ^ 2) | (in[1] ^ 0x00fffe0000000000)
        | (in[2] ^ 0x00ffffffffffffff) | (in[3] ^ 0x01ffffffffffffff);
    two225m97p2 = (((int64_t) (two225m97p2) - 1) >> 63) & 1;
    return (zero | two224m96p1 | two225m97p2);
}

static int felem_is_zero_int(const void *in)
{
    return (int)(felem_is_zero(in) & ((limb) 1));
}

/* Invert a field element */
/* Computation chain copied from djb's code */
static void felem_inv(felem out, const felem in)
{
    felem ftmp, ftmp2, ftmp3, ftmp4;
    widefelem tmp;
    unsigned i;

    felem_square(tmp, in);
    felem_reduce(ftmp, tmp);    /* 2 */
    felem_mul(tmp, in, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^2 - 1 */
    felem_square(tmp, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^3 - 2 */
    felem_mul(tmp, in, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^3 - 1 */
    felem_square(tmp, ftmp);
    felem_reduce(ftmp2, tmp);   /* 2^4 - 2 */
    felem_square(tmp, ftmp2);
    felem_reduce(ftmp2, tmp);   /* 2^5 - 4 */
    felem_square(tmp, ftmp2);
    felem_reduce(ftmp2, tmp);   /* 2^6 - 8 */
    felem_mul(tmp, ftmp2, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^6 - 1 */
    felem_square(tmp, ftmp);
    felem_reduce(ftmp2, tmp);   /* 2^7 - 2 */
    for (i = 0; i < 5; ++i) {   /* 2^12 - 2^6 */
        felem_square(tmp, ftmp2);
        felem_reduce(ftmp2, tmp);
    }
    felem_mul(tmp, ftmp2, ftmp);
    felem_reduce(ftmp2, tmp);   /* 2^12 - 1 */
    felem_square(tmp, ftmp2);
    felem_reduce(ftmp3, tmp);   /* 2^13 - 2 */
    for (i = 0; i < 11; ++i) {  /* 2^24 - 2^12 */
        felem_square(tmp, ftmp3);
        felem_reduce(ftmp3, tmp);
    }
    felem_mul(tmp, ftmp3, ftmp2);
    felem_reduce(ftmp2, tmp);   /* 2^24 - 1 */
    felem_square(tmp, ftmp2);
    felem_reduce(ftmp3, tmp);   /* 2^25 - 2 */
    for (i = 0; i < 23; ++i) {  /* 2^48 - 2^24 */
        felem_square(tmp, ftmp3);
        felem_reduce(ftmp3, tmp);
    }
    felem_mul(tmp, ftmp3, ftmp2);
    felem_reduce(ftmp3, tmp);   /* 2^48 - 1 */
    felem_square(tmp, ftmp3);
    felem_reduce(ftmp4, tmp);   /* 2^49 - 2 */
    for (i = 0; i < 47; ++i) {  /* 2^96 - 2^48 */
        felem_square(tmp, ftmp4);
        felem_reduce(ftmp4, tmp);
    }
    felem_mul(tmp, ftmp3, ftmp4);
    felem_reduce(ftmp3, tmp);   /* 2^96 - 1 */
    felem_square(tmp, ftmp3);
    felem_reduce(ftmp4, tmp);   /* 2^97 - 2 */
    for (i = 0; i < 23; ++i) {  /* 2^120 - 2^24 */
        felem_square(tmp, ftmp4);
        felem_reduce(ftmp4, tmp);
    }
    felem_mul(tmp, ftmp2, ftmp4);
    felem_reduce(ftmp2, tmp);   /* 2^120 - 1 */
    for (i = 0; i < 6; ++i) {   /* 2^126 - 2^6 */
        felem_square(tmp, ftmp2);
        felem_reduce(ftmp2, tmp);
    }
    felem_mul(tmp, ftmp2, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^126 - 1 */
    felem_square(tmp, ftmp);
    felem_reduce(ftmp, tmp);    /* 2^127 - 2 */
    felem_mul(tmp, ftmp, in);
    felem_reduce(ftmp, tmp);    /* 2^127 - 1 */
    for (i = 0; i < 97; ++i) {  /* 2^224 - 2^97 */
        felem_square(tmp, ftmp);
        felem_reduce(ftmp, tmp);
    }
    felem_mul(tmp, ftmp, ftmp3);
    felem_reduce(out, tmp);     /* 2^224 - 2^96 - 1 */
}

/*
 * Copy in constant time: if icopy == 1, copy in to out, if icopy == 0, copy
 * out to itself.
 */
static void copy_conditional(felem out, const felem in, limb icopy)
{
    unsigned i;
    /*
     * icopy is a (64-bit) 0 or 1, so copy is either all-zero or all-one
     */
    const limb copy = -icopy;
    for (i = 0; i < 4; ++i) {
        const limb tmp = copy & (in[i] ^ out[i]);
        out[i] ^= tmp;
    }
}

/******************************************************************************/
/*-
 *                       ELLIPTIC CURVE POINT OPERATIONS
 *
 * Points are represented in Jacobian projective coordinates:
 * (X, Y, Z) corresponds to the affine point (X/Z^2, Y/Z^3),
 * or to the point at infinity if Z == 0.
 *
 */

/*-
 * Double an elliptic curve point:
 * (X', Y', Z') = 2 * (X, Y, Z), where
 * X' = (3 * (X - Z^2) * (X + Z^2))^2 - 8 * X * Y^2
 * Y' = 3 * (X - Z^2) * (X + Z^2) * (4 * X * Y^2 - X') - 8 * Y^4
 * Z' = (Y + Z)^2 - Y^2 - Z^2 = 2 * Y * Z
 * Outputs can equal corresponding inputs, i.e., x_out == x_in is allowed,
 * while x_out == y_in is not (maybe this works, but it's not tested).
 */
static void
point_double(felem x_out, felem y_out, felem z_out,
             const felem x_in, const felem y_in, const felem z_in)
{
    widefelem tmp, tmp2;
    felem delta, gamma, beta, alpha, ftmp, ftmp2;

    felem_assign(ftmp, x_in);
    felem_assign(ftmp2, x_in);

    /* delta = z^2 */
    felem_square(tmp, z_in);
    felem_reduce(delta, tmp);

    /* gamma = y^2 */
    felem_square(tmp, y_in);
    felem_reduce(gamma, tmp);

    /* beta = x*gamma */
    felem_mul(tmp, x_in, gamma);
    felem_reduce(beta, tmp);

    /* alpha = 3*(x-delta)*(x+delta) */
    felem_diff(ftmp, delta);
    /* ftmp[i] < 2^57 + 2^58 + 2 < 2^59 */
    felem_sum(ftmp2, delta);
    /* ftmp2[i] < 2^57 + 2^57 = 2^58 */
    felem_scalar(ftmp2, 3);
    /* ftmp2[i] < 3 * 2^58 < 2^60 */
    felem_mul(tmp, ftmp, ftmp2);
    /* tmp[i] < 2^60 * 2^59 * 4 = 2^121 */
    felem_reduce(alpha, tmp);

    /* x' = alpha^2 - 8*beta */
    felem_square(tmp, alpha);
    /* tmp[i] < 4 * 2^57 * 2^57 = 2^116 */
    felem_assign(ftmp, beta);
    felem_scalar(ftmp, 8);
    /* ftmp[i] < 8 * 2^57 = 2^60 */
    felem_diff_128_64(tmp, ftmp);
    /* tmp[i] < 2^116 + 2^64 + 8 < 2^117 */
    felem_reduce(x_out, tmp);

    /* z' = (y + z)^2 - gamma - delta */
    felem_sum(delta, gamma);
    /* delta[i] < 2^57 + 2^57 = 2^58 */
    felem_assign(ftmp, y_in);
    felem_sum(ftmp, z_in);
    /* ftmp[i] < 2^57 + 2^57 = 2^58 */
    felem_square(tmp, ftmp);
    /* tmp[i] < 4 * 2^58 * 2^58 = 2^118 */
    felem_diff_128_64(tmp, delta);
    /* tmp[i] < 2^118 + 2^64 + 8 < 2^119 */
    felem_reduce(z_out, tmp);

    /* y' = alpha*(4*beta - x') - 8*gamma^2 */
    felem_scalar(beta, 4);
    /* beta[i] < 4 * 2^57 = 2^59 */
    felem_diff(beta, x_out);
    /* beta[i] < 2^59 + 2^58 + 2 < 2^60 */
    felem_mul(tmp, alpha, beta);
    /* tmp[i] < 4 * 2^57 * 2^60 = 2^119 */
    felem_square(tmp2, gamma);
    /* tmp2[i] < 4 * 2^57 * 2^57 = 2^116 */
    widefelem_scalar(tmp2, 8);
    /* tmp2[i] < 8 * 2^116 = 2^119 */
    widefelem_diff(tmp, tmp2);
    /* tmp[i] < 2^119 + 2^120 < 2^121 */
    felem_reduce(y_out, tmp);
}

/*-
 * Add two elliptic curve points:
 * (X_1, Y_1, Z_1) + (X_2, Y_2, Z_2) = (X_3, Y_3, Z_3), where
 * X_3 = (Z_1^3 * Y_2 - Z_2^3 * Y_1)^2 - (Z_1^2 * X_2 - Z_2^2 * X_1)^3 -
 * 2 * Z_2^2 * X_1 * (Z_1^2 * X_2 - Z_2^2 * X_1)^2
 * Y_3 = (Z_1^3 * Y_2 - Z_2^3 * Y_1) * (Z_2^2 * X_1 * (Z_1^2 * X_2 - Z_2^2 * X_1)^2 - X_3) -
 *        Z_2^3 * Y_1 * (Z_1^2 * X_2 - Z_2^2 * X_1)^3
 * Z_3 = (Z_1^2 * X_2 - Z_2^2 * X_1) * (Z_1 * Z_2)
 *
 * This runs faster if 'mixed' is set, which requires Z_2 = 1 or Z_2 = 0.
 */

/*
 * This function is not entirely constant-time: it includes a branch for
 * checking whether the two input points are equal, (while not equal to the
 * point at infinity). This case never happens during single point
 * multiplication, so there is no timing leak for ECDH or ECDSA signing.
 */
static void point_add(felem x3, felem y3, felem z3,
                      const felem x1, const felem y1, const felem z1,
                      const int mixed, const felem x2, const felem y2,
                      const felem z2)
{
    felem ftmp, ftmp2, ftmp3, ftmp4, ftmp5, x_out, y_out, z_out;
    widefelem tmp, tmp2;
    limb z1_is_zero, z2_is_zero, x_equal, y_equal;

    if (!mixed) {
        /* ftmp2 = z2^2 */
        felem_square(tmp, z2);
        felem_reduce(ftmp2, tmp);

        /* ftmp4 = z2^3 */
        felem_mul(tmp, ftmp2, z2);
        felem_reduce(ftmp4, tmp);

        /* ftmp4 = z2^3*y1 */
        felem_mul(tmp2, ftmp4, y1);
        felem_reduce(ftmp4, tmp2);

        /* ftmp2 = z2^2*x1 */
        felem_mul(tmp2, ftmp2, x1);
        felem_reduce(ftmp2, tmp2);
    } else {
        /*
         * We'll assume z2 = 1 (special case z2 = 0 is handled later)
         */

        /* ftmp4 = z2^3*y1 */
        felem_assign(ftmp4, y1);

        /* ftmp2 = z2^2*x1 */
        felem_assign(ftmp2, x1);
    }

    /* ftmp = z1^2 */
    felem_square(tmp, z1);
    felem_reduce(ftmp, tmp);

    /* ftmp3 = z1^3 */
    felem_mul(tmp, ftmp, z1);
    felem_reduce(ftmp3, tmp);

    /* tmp = z1^3*y2 */
    felem_mul(tmp, ftmp3, y2);
    /* tmp[i] < 4 * 2^57 * 2^57 = 2^116 */

    /* ftmp3 = z1^3*y2 - z2^3*y1 */
    felem_diff_128_64(tmp, ftmp4);
    /* tmp[i] < 2^116 + 2^64 + 8 < 2^117 */
    felem_reduce(ftmp3, tmp);

    /* tmp = z1^2*x2 */
    felem_mul(tmp, ftmp, x2);
    /* tmp[i] < 4 * 2^57 * 2^57 = 2^116 */

    /* ftmp = z1^2*x2 - z2^2*x1 */
    felem_diff_128_64(tmp, ftmp2);
    /* tmp[i] < 2^116 + 2^64 + 8 < 2^117 */
    felem_reduce(ftmp, tmp);

    /*
     * the formulae are incorrect if the points are equal so we check for
     * this and do doubling if this happens
     */
    x_equal = felem_is_zero(ftmp);
    y_equal = felem_is_zero(ftmp3);
    z1_is_zero = felem_is_zero(z1);
    z2_is_zero = felem_is_zero(z2);
    /* In affine coordinates, (X_1, Y_1) == (X_2, Y_2) */
    if (x_equal && y_equal && !z1_is_zero && !z2_is_zero) {
        point_double(x3, y3, z3, x1, y1, z1);
        return;
    }

    /* ftmp5 = z1*z2 */
    if (!mixed) {
        felem_mul(tmp, z1, z2);
        felem_reduce(ftmp5, tmp);
    } else {
        /* special case z2 = 0 is handled later */
        felem_assign(ftmp5, z1);
    }

    /* z_out = (z1^2*x2 - z2^2*x1)*(z1*z2) */
    felem_mul(tmp, ftmp, ftmp5);
    felem_reduce(z_out, tmp);

    /* ftmp = (z1^2*x2 - z2^2*x1)^2 */
    felem_assign(ftmp5, ftmp);
    felem_square(tmp, ftmp);
    felem_reduce(ftmp, tmp);

    /* ftmp5 = (z1^2*x2 - z2^2*x1)^3 */
    felem_mul(tmp, ftmp, ftmp5);
    felem_reduce(ftmp5, tmp);

    /* ftmp2 = z2^2*x1*(z1^2*x2 - z2^2*x1)^2 */
    felem_mul(tmp, ftmp2, ftmp);
    felem_reduce(ftmp2, tmp);

    /* tmp = z2^3*y1*(z1^2*x2 - z2^2*x1)^3 */
    felem_mul(tmp, ftmp4, ftmp5);
    /* tmp[i] < 4 * 2^57 * 2^57 = 2^116 */

    /* tmp2 = (z1^3*y2 - z2^3*y1)^2 */
    felem_square(tmp2, ftmp3);
    /* tmp2[i] < 4 * 2^57 * 2^57 < 2^116 */

    /* tmp2 = (z1^3*y2 - z2^3*y1)^2 - (z1^2*x2 - z2^2*x1)^3 */
    felem_diff_128_64(tmp2, ftmp5);
    /* tmp2[i] < 2^116 + 2^64 + 8 < 2^117 */

    /* ftmp5 = 2*z2^2*x1*(z1^2*x2 - z2^2*x1)^2 */
    felem_assign(ftmp5, ftmp2);
    felem_scalar(ftmp5, 2);
    /* ftmp5[i] < 2 * 2^57 = 2^58 */

    /*-
     * x_out = (z1^3*y2 - z2^3*y1)^2 - (z1^2*x2 - z2^2*x1)^3 -
     *  2*z2^2*x1*(z1^2*x2 - z2^2*x1)^2
     */
    felem_diff_128_64(tmp2, ftmp5);
    /* tmp2[i] < 2^117 + 2^64 + 8 < 2^118 */
    felem_reduce(x_out, tmp2);

    /* ftmp2 = z2^2*x1*(z1^2*x2 - z2^2*x1)^2 - x_out */
    felem_diff(ftmp2, x_out);
    /* ftmp2[i] < 2^57 + 2^58 + 2 < 2^59 */

    /*
     * tmp2 = (z1^3*y2 - z2^3*y1)*(z2^2*x1*(z1^2*x2 - z2^2*x1)^2 - x_out)
     */
    felem_mul(tmp2, ftmp3, ftmp2);
    /* tmp2[i] < 4 * 2^57 * 2^59 = 2^118 */

    /*-
     * y_out = (z1^3*y2 - z2^3*y1)*(z2^2*x1*(z1^2*x2 - z2^2*x1)^2 - x_out) -
     *  z2^3*y1*(z1^2*x2 - z2^2*x1)^3
     */
    widefelem_diff(tmp2, tmp);
    /* tmp2[i] < 2^118 + 2^120 < 2^121 */
    felem_reduce(y_out, tmp2);

    /*
     * the result (x_out, y_out, z_out) is incorrect if one of the inputs is
     * the point at infinity, so we need to check for this separately
     */

    /*
     * if point 1 is at infinity, copy point 2 to output, and vice versa
     */
    copy_conditional(x_out, x2, z1_is_zero);
    copy_conditional(x_out, x1, z2_is_zero);
    copy_conditional(y_out, y2, z1_is_zero);
    copy_conditional(y_out, y1, z2_is_zero);
    copy_conditional(z_out, z2, z1_is_zero);
    copy_conditional(z_out, z1, z2_is_zero);
    felem_assign(x3, x_out);
    felem_assign(y3, y_out);
    felem_assign(z3, z_out);
}

/*
 * select_point selects the |idx|th point from a precomputation table and
 * copies it to out.
 * The pre_comp array argument should be size of |size| argument
 */
static void select_point(const u64 idx, unsigned int size,
                         const felem pre_comp[][3], felem out[3])
{
    unsigned i, j;
    limb *outlimbs = &out[0][0];

    memset(out, 0, sizeof(*out) * 3);
    for (i = 0; i < size; i++) {
        const limb *inlimbs = &pre_comp[i][0][0];
        u64 mask = i ^ idx;
        mask |= mask >> 4;
        mask |= mask >> 2;
        mask |= mask >> 1;
        mask &= 1;
        mask--;
        for (j = 0; j < 4 * 3; j++)
            outlimbs[j] |= inlimbs[j] & mask;
    }
}

/* get_bit returns the |i|th bit in |in| */
static char get_bit(const felem_bytearray in, unsigned i)
{
    if (i >= 224)
        return 0;
    return (in[i >> 3] >> (i & 7)) & 1;
}

/*
 * Interleaved point multiplication using precomputed point multiples: The
 * small point multiples 0*P, 1*P, ..., 16*P are in pre_comp[], the scalars
 * in scalars[]. If g_scalar is non-NULL, we also add this multiple of the
 * generator, using certain (large) precomputed multiples in g_pre_comp.
 * Output point (X, Y, Z) is stored in x_out, y_out, z_out
 */
static void batch_mul(felem x_out, felem y_out, felem z_out,
                      const felem_bytearray scalars[],
                      const unsigned num_points, const u8 *g_scalar,
                      const int mixed, const felem pre_comp[][17][3],
                      const felem g_pre_comp[2][16][3])
{
    int i, skip;
    unsigned num;
    unsigned gen_mul = (g_scalar != NULL);
    felem nq[3], tmp[4];
    u64 bits;
    u8 sign, digit;

    /* set nq to the point at infinity */
    memset(nq, 0, sizeof(nq));

    /*
     * Loop over all scalars msb-to-lsb, interleaving additions of multiples
     * of the generator (two in each of the last 28 rounds) and additions of
     * other points multiples (every 5th round).
     */
    skip = 1;                   /* save two point operations in the first
                                 * round */
    for (i = (num_points ? 220 : 27); i >= 0; --i) {
        /* double */
        if (!skip)
            point_double(nq[0], nq[1], nq[2], nq[0], nq[1], nq[2]);

        /* add multiples of the generator */
        if (gen_mul && (i <= 27)) {
            /* first, look 28 bits upwards */
            bits = get_bit(g_scalar, i + 196) << 3;
            bits |= get_bit(g_scalar, i + 140) << 2;
            bits |= get_bit(g_scalar, i + 84) << 1;
            bits |= get_bit(g_scalar, i + 28);
            /* select the point to add, in constant time */
            select_point(bits, 16, g_pre_comp[1], tmp);

            if (!skip) {
                /* value 1 below is argument for "mixed" */
                point_add(nq[0], nq[1], nq[2],
                          nq[0], nq[1], nq[2], 1, tmp[0], tmp[1], tmp[2]);
            } else {
                memcpy(nq, tmp, 3 * sizeof(felem));
                skip = 0;
            }

            /* second, look at the current position */
            bits = get_bit(g_scalar, i + 168) << 3;
            bits |= get_bit(g_scalar, i + 112) << 2;
            bits |= get_bit(g_scalar, i + 56) << 1;
            bits |= get_bit(g_scalar, i);
            /* select the point to add, in constant time */
            select_point(bits, 16, g_pre_comp[0], tmp);
            point_add(nq[0], nq[1], nq[2],
                      nq[0], nq[1], nq[2],
                      1 /* mixed */ , tmp[0], tmp[1], tmp[2]);
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

                /* select the point to add or subtract */
                select_point(digit, 17, pre_comp[num], tmp);
                felem_neg(tmp[3], tmp[1]); /* (X, -Y, Z) is the negative
                                            * point */
                copy_conditional(tmp[1], tmp[3], sign);

                if (!skip) {
                    point_add(nq[0], nq[1], nq[2],
                              nq[0], nq[1], nq[2],
                              mixed, tmp[0], tmp[1], tmp[2]);
                } else {
                    memcpy(nq, tmp, 3 * sizeof(felem));
                    skip = 0;
                }
            }
        }
    }
    felem_assign(x_out, nq[0]);
    felem_assign(y_out, nq[1]);
    felem_assign(z_out, nq[2]);
}

/******************************************************************************/
/*
 * FUNCTIONS TO MANAGE PRECOMPUTATION
 */

static NISTP224_PRE_COMP *nistp224_pre_comp_new(void)
{
    NISTP224_PRE_COMP *ret = OPENSSL_zalloc(sizeof(*ret));

    if (!ret) {
        ECerr(EC_F_NISTP224_PRE_COMP_NEW, ERR_R_MALLOC_FAILURE);
        return ret;
    }

    ret->references = 1;

    ret->lock = CRYPTO_THREAD_lock_new();
    if (ret->lock == NULL) {
        ECerr(EC_F_NISTP224_PRE_COMP_NEW, ERR_R_MALLOC_FAILURE);
        OPENSSL_free(ret);
        return NULL;
    }
    return ret;
}

NISTP224_PRE_COMP *EC_nistp224_pre_comp_dup(NISTP224_PRE_COMP *p)
{
    int i;
    if (p != NULL)
        CRYPTO_UP_REF(&p->references, &i, p->lock);
    return p;
}

void EC_nistp224_pre_comp_free(NISTP224_PRE_COMP *p)
{
    int i;

    if (p == NULL)
        return;

    CRYPTO_DOWN_REF(&p->references, &i, p->lock);
    REF_PRINT_COUNT("EC_nistp224", x);
    if (i > 0)
        return;
    REF_ASSERT_ISNT(i < 0);

    CRYPTO_THREAD_lock_free(p->lock);
    OPENSSL_free(p);
}

/******************************************************************************/
/*
 * OPENSSL EC_METHOD FUNCTIONS
 */

int ec_GFp_nistp224_group_init(EC_GROUP *group)
{
    int ret;
    ret = ec_GFp_simple_group_init(group);
    group->a_is_minus3 = 1;
    return ret;
}

int ec_GFp_nistp224_group_set_curve(EC_GROUP *group, const BIGNUM *p,
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
    BN_bin2bn(nistp224_curve_params[0], sizeof(felem_bytearray), curve_p);
    BN_bin2bn(nistp224_curve_params[1], sizeof(felem_bytearray), curve_a);
    BN_bin2bn(nistp224_curve_params[2], sizeof(felem_bytearray), curve_b);
    if ((BN_cmp(curve_p, p)) || (BN_cmp(curve_a, a)) || (BN_cmp(curve_b, b))) {
        ECerr(EC_F_EC_GFP_NISTP224_GROUP_SET_CURVE,
              EC_R_WRONG_CURVE_PARAMETERS);
        goto err;
    }
    group->field_mod_func = BN_nist_mod_224;
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
int ec_GFp_nistp224_point_get_affine_coordinates(const EC_GROUP *group,
                                                 const EC_POINT *point,
                                                 BIGNUM *x, BIGNUM *y,
                                                 BN_CTX *ctx)
{
    felem z1, z2, x_in, y_in, x_out, y_out;
    widefelem tmp;

    if (EC_POINT_is_at_infinity(group, point)) {
        ECerr(EC_F_EC_GFP_NISTP224_POINT_GET_AFFINE_COORDINATES,
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
        if (!felem_to_BN(x, x_out)) {
            ECerr(EC_F_EC_GFP_NISTP224_POINT_GET_AFFINE_COORDINATES,
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
        if (!felem_to_BN(y, y_out)) {
            ECerr(EC_F_EC_GFP_NISTP224_POINT_GET_AFFINE_COORDINATES,
                  ERR_R_BN_LIB);
            return 0;
        }
    }
    return 1;
}

static void make_points_affine(size_t num, felem points[ /* num */ ][3],
                               felem tmp_felems[ /* num+1 */ ])
{
    /*
     * Runs in constant time, unless an input is the point at infinity (which
     * normally shouldn't happen).
     */
    ec_GFp_nistp_points_make_affine_internal(num,
                                             points,
                                             sizeof(felem),
                                             tmp_felems,
                                             (void (*)(void *))felem_one,
                                             felem_is_zero_int,
                                             (void (*)(void *, const void *))
                                             felem_assign,
                                             (void (*)(void *, const void *))
                                             felem_square_reduce, (void (*)
                                                                   (void *,
                                                                    const void
                                                                    *,
                                                                    const void
                                                                    *))
                                             felem_mul_reduce,
                                             (void (*)(void *, const void *))
                                             felem_inv,
                                             (void (*)(void *, const void *))
                                             felem_contract);
}

/*
 * Computes scalar*generator + \sum scalars[i]*points[i], ignoring NULL
 * values Result is stored in r (r can equal one of the inputs).
 */
int ec_GFp_nistp224_points_mul(const EC_GROUP *group, EC_POINT *r,
                               const BIGNUM *scalar, size_t num,
                               const EC_POINT *points[],
                               const BIGNUM *scalars[], BN_CTX *ctx)
{
    int ret = 0;
    int j;
    unsigned i;
    int mixed = 0;
    BIGNUM *x, *y, *z, *tmp_scalar;
    felem_bytearray g_secret;
    felem_bytearray *secrets = NULL;
    felem (*pre_comp)[17][3] = NULL;
    felem *tmp_felems = NULL;
    felem_bytearray tmp;
    unsigned num_bytes;
    int have_pre_comp = 0;
    size_t num_points = num;
    felem x_in, y_in, z_in, x_out, y_out, z_out;
    NISTP224_PRE_COMP *pre = NULL;
    const felem(*g_pre_comp)[16][3] = NULL;
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
        pre = group->pre_comp.nistp224;
        if (pre)
            /* we have precomputation, try to use it */
            g_pre_comp = (const felem(*)[16][3])pre->g_pre_comp;
        else
            /* try to use the standard precomputation */
            g_pre_comp = &gmul[0];
        generator = EC_POINT_new(group);
        if (generator == NULL)
            goto err;
        /* get the generator from precomputation */
        if (!felem_to_BN(x, g_pre_comp[0][1][0]) ||
            !felem_to_BN(y, g_pre_comp[0][1][1]) ||
            !felem_to_BN(z, g_pre_comp[0][1][2])) {
            ECerr(EC_F_EC_GFP_NISTP224_POINTS_MUL, ERR_R_BN_LIB);
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
            num_points = num_points + 1;
    }

    if (num_points > 0) {
        if (num_points >= 3) {
            /*
             * unless we precompute multiples for just one or two points,
             * converting those into affine form is time well spent
             */
            mixed = 1;
        }
        secrets = OPENSSL_zalloc(sizeof(*secrets) * num_points);
        pre_comp = OPENSSL_zalloc(sizeof(*pre_comp) * num_points);
        if (mixed)
            tmp_felems =
                OPENSSL_malloc(sizeof(felem) * (num_points * 17 + 1));
        if ((secrets == NULL) || (pre_comp == NULL)
            || (mixed && (tmp_felems == NULL))) {
            ECerr(EC_F_EC_GFP_NISTP224_POINTS_MUL, ERR_R_MALLOC_FAILURE);
            goto err;
        }

        /*
         * we treat NULL scalars as 0, and NULL points as points at infinity,
         * i.e., they contribute nothing to the linear combination
         */
        for (i = 0; i < num_points; ++i) {
            if (i == num)
                /* the generator */
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
                /* reduce scalar to 0 <= scalar < 2^224 */
                if ((BN_num_bits(p_scalar) > 224)
                    || (BN_is_negative(p_scalar))) {
                    /*
                     * this is an unusual input, and we don't guarantee
                     * constant-timeness
                     */
                    if (!BN_nnmod(tmp_scalar, p_scalar, group->order, ctx)) {
                        ECerr(EC_F_EC_GFP_NISTP224_POINTS_MUL, ERR_R_BN_LIB);
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
                felem_assign(pre_comp[i][1][0], x_out);
                felem_assign(pre_comp[i][1][1], y_out);
                felem_assign(pre_comp[i][1][2], z_out);
                for (j = 2; j <= 16; ++j) {
                    if (j & 1) {
                        point_add(pre_comp[i][j][0], pre_comp[i][j][1],
                                  pre_comp[i][j][2], pre_comp[i][1][0],
                                  pre_comp[i][1][1], pre_comp[i][1][2], 0,
                                  pre_comp[i][j - 1][0],
                                  pre_comp[i][j - 1][1],
                                  pre_comp[i][j - 1][2]);
                    } else {
                        point_double(pre_comp[i][j][0], pre_comp[i][j][1],
                                     pre_comp[i][j][2], pre_comp[i][j / 2][0],
                                     pre_comp[i][j / 2][1],
                                     pre_comp[i][j / 2][2]);
                    }
                }
            }
        }
        if (mixed)
            make_points_affine(num_points * 17, pre_comp[0], tmp_felems);
    }

    /* the scalar for the generator */
    if ((scalar != NULL) && (have_pre_comp)) {
        memset(g_secret, 0, sizeof(g_secret));
        /* reduce scalar to 0 <= scalar < 2^224 */
        if ((BN_num_bits(scalar) > 224) || (BN_is_negative(scalar))) {
            /*
             * this is an unusual input, and we don't guarantee
             * constant-timeness
             */
            if (!BN_nnmod(tmp_scalar, scalar, group->order, ctx)) {
                ECerr(EC_F_EC_GFP_NISTP224_POINTS_MUL, ERR_R_BN_LIB);
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
                  mixed, (const felem(*)[17][3])pre_comp, g_pre_comp);
    } else
        /* do the multiplication without generator precomputation */
        batch_mul(x_out, y_out, z_out,
                  (const felem_bytearray(*))secrets, num_points,
                  NULL, mixed, (const felem(*)[17][3])pre_comp, NULL);
    /* reduce the output to its unique minimal representation */
    felem_contract(x_in, x_out);
    felem_contract(y_in, y_out);
    felem_contract(z_in, z_out);
    if ((!felem_to_BN(x, x_in)) || (!felem_to_BN(y, y_in)) ||
        (!felem_to_BN(z, z_in))) {
        ECerr(EC_F_EC_GFP_NISTP224_POINTS_MUL, ERR_R_BN_LIB);
        goto err;
    }
    ret = EC_POINT_set_Jprojective_coordinates_GFp(group, r, x, y, z, ctx);

 err:
    BN_CTX_end(ctx);
    EC_POINT_free(generator);
    OPENSSL_free(secrets);
    OPENSSL_free(pre_comp);
    OPENSSL_free(tmp_felems);
    return ret;
}

int ec_GFp_nistp224_precompute_mult(EC_GROUP *group, BN_CTX *ctx)
{
    int ret = 0;
    NISTP224_PRE_COMP *pre = NULL;
    int i, j;
    BN_CTX *new_ctx = NULL;
    BIGNUM *x, *y;
    EC_POINT *generator = NULL;
    felem tmp_felems[32];

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
    BN_bin2bn(nistp224_curve_params[3], sizeof(felem_bytearray), x);
    BN_bin2bn(nistp224_curve_params[4], sizeof(felem_bytearray), y);
    if (!EC_POINT_set_affine_coordinates(group, generator, x, y, ctx))
        goto err;
    if ((pre = nistp224_pre_comp_new()) == NULL)
        goto err;
    /*
     * if the generator is the standard one, use built-in precomputation
     */
    if (0 == EC_POINT_cmp(group, generator, group->generator, ctx)) {
        memcpy(pre->g_pre_comp, gmul, sizeof(pre->g_pre_comp));
        goto done;
    }
    if ((!BN_to_felem(pre->g_pre_comp[0][1][0], group->generator->X)) ||
        (!BN_to_felem(pre->g_pre_comp[0][1][1], group->generator->Y)) ||
        (!BN_to_felem(pre->g_pre_comp[0][1][2], group->generator->Z)))
        goto err;
    /*
     * compute 2^56*G, 2^112*G, 2^168*G for the first table, 2^28*G, 2^84*G,
     * 2^140*G, 2^196*G for the second one
     */
    for (i = 1; i <= 8; i <<= 1) {
        point_double(pre->g_pre_comp[1][i][0], pre->g_pre_comp[1][i][1],
                     pre->g_pre_comp[1][i][2], pre->g_pre_comp[0][i][0],
                     pre->g_pre_comp[0][i][1], pre->g_pre_comp[0][i][2]);
        for (j = 0; j < 27; ++j) {
            point_double(pre->g_pre_comp[1][i][0], pre->g_pre_comp[1][i][1],
                         pre->g_pre_comp[1][i][2], pre->g_pre_comp[1][i][0],
                         pre->g_pre_comp[1][i][1], pre->g_pre_comp[1][i][2]);
        }
        if (i == 8)
            break;
        point_double(pre->g_pre_comp[0][2 * i][0],
                     pre->g_pre_comp[0][2 * i][1],
                     pre->g_pre_comp[0][2 * i][2], pre->g_pre_comp[1][i][0],
                     pre->g_pre_comp[1][i][1], pre->g_pre_comp[1][i][2]);
        for (j = 0; j < 27; ++j) {
            point_double(pre->g_pre_comp[0][2 * i][0],
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
        /* 2^56*G + 2^112*G resp. 2^84*G + 2^140*G */
        point_add(pre->g_pre_comp[i][6][0], pre->g_pre_comp[i][6][1],
                  pre->g_pre_comp[i][6][2], pre->g_pre_comp[i][4][0],
                  pre->g_pre_comp[i][4][1], pre->g_pre_comp[i][4][2],
                  0, pre->g_pre_comp[i][2][0], pre->g_pre_comp[i][2][1],
                  pre->g_pre_comp[i][2][2]);
        /* 2^56*G + 2^168*G resp. 2^84*G + 2^196*G */
        point_add(pre->g_pre_comp[i][10][0], pre->g_pre_comp[i][10][1],
                  pre->g_pre_comp[i][10][2], pre->g_pre_comp[i][8][0],
                  pre->g_pre_comp[i][8][1], pre->g_pre_comp[i][8][2],
                  0, pre->g_pre_comp[i][2][0], pre->g_pre_comp[i][2][1],
                  pre->g_pre_comp[i][2][2]);
        /* 2^112*G + 2^168*G resp. 2^140*G + 2^196*G */
        point_add(pre->g_pre_comp[i][12][0], pre->g_pre_comp[i][12][1],
                  pre->g_pre_comp[i][12][2], pre->g_pre_comp[i][8][0],
                  pre->g_pre_comp[i][8][1], pre->g_pre_comp[i][8][2],
                  0, pre->g_pre_comp[i][4][0], pre->g_pre_comp[i][4][1],
                  pre->g_pre_comp[i][4][2]);
        /*
         * 2^56*G + 2^112*G + 2^168*G resp. 2^84*G + 2^140*G + 2^196*G
         */
        point_add(pre->g_pre_comp[i][14][0], pre->g_pre_comp[i][14][1],
                  pre->g_pre_comp[i][14][2], pre->g_pre_comp[i][12][0],
                  pre->g_pre_comp[i][12][1], pre->g_pre_comp[i][12][2],
                  0, pre->g_pre_comp[i][2][0], pre->g_pre_comp[i][2][1],
                  pre->g_pre_comp[i][2][2]);
        for (j = 1; j < 8; ++j) {
            /* odd multiples: add G resp. 2^28*G */
            point_add(pre->g_pre_comp[i][2 * j + 1][0],
                      pre->g_pre_comp[i][2 * j + 1][1],
                      pre->g_pre_comp[i][2 * j + 1][2],
                      pre->g_pre_comp[i][2 * j][0],
                      pre->g_pre_comp[i][2 * j][1],
                      pre->g_pre_comp[i][2 * j][2], 0,
                      pre->g_pre_comp[i][1][0], pre->g_pre_comp[i][1][1],
                      pre->g_pre_comp[i][1][2]);
        }
    }
    make_points_affine(31, &(pre->g_pre_comp[0][1]), tmp_felems);

 done:
    SETPRECOMP(group, nistp224, pre);
    pre = NULL;
    ret = 1;
 err:
    BN_CTX_end(ctx);
    EC_POINT_free(generator);
    BN_CTX_free(new_ctx);
    EC_nistp224_pre_comp_free(pre);
    return ret;
}

int ec_GFp_nistp224_have_precompute_mult(const EC_GROUP *group)
{
    return HAVEPRECOMP(group, nistp224);
}

#endif
