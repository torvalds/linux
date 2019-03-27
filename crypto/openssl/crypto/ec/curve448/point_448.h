/*
 * Copyright 2017-2019 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright 2015-2016 Cryptography Research, Inc.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * Originally written by Mike Hamburg
 */

#ifndef HEADER_POINT_448_H
# define HEADER_POINT_448_H

# include "curve448utils.h"
# include "field.h"

/* Comb config: number of combs, n, t, s. */
#define COMBS_N 5
#define COMBS_T 5
#define COMBS_S 18

/* Projective Niels coordinates */
typedef struct {
    gf a, b, c;
} niels_s, niels_t[1];
typedef struct {
    niels_t n;
    gf z;
} pniels_t[1];

/* Precomputed base */
struct curve448_precomputed_s {
    niels_t table[COMBS_N << (COMBS_T - 1)];
};

# define C448_SCALAR_LIMBS ((446-1)/C448_WORD_BITS+1)

/* The number of bits in a scalar */
# define C448_SCALAR_BITS 446

/* Number of bytes in a serialized scalar. */
# define C448_SCALAR_BYTES 56

/* X448 encoding ratio. */
# define X448_ENCODE_RATIO 2

/* Number of bytes in an x448 public key */
# define X448_PUBLIC_BYTES 56

/* Number of bytes in an x448 private key */
# define X448_PRIVATE_BYTES 56

/* Twisted Edwards extended homogeneous coordinates */
typedef struct curve448_point_s {
    gf x, y, z, t;
} curve448_point_t[1];

/* Precomputed table based on a point.  Can be trivial implementation. */
struct curve448_precomputed_s;

/* Precomputed table based on a point.  Can be trivial implementation. */
typedef struct curve448_precomputed_s curve448_precomputed_s;

/* Scalar is stored packed, because we don't need the speed. */
typedef struct curve448_scalar_s {
    c448_word_t limb[C448_SCALAR_LIMBS];
} curve448_scalar_t[1];

/* A scalar equal to 1. */
extern const curve448_scalar_t curve448_scalar_one;

/* A scalar equal to 0. */
extern const curve448_scalar_t curve448_scalar_zero;

/* The identity point on the curve. */
extern const curve448_point_t curve448_point_identity;

/* Precomputed table for the base point on the curve. */
extern const struct curve448_precomputed_s *curve448_precomputed_base;
extern const niels_t *curve448_wnaf_base;

/*
 * Read a scalar from wire format or from bytes.
 *
 * ser (in): Serialized form of a scalar.
 * out (out): Deserialized form.
 *
 * Returns:
 * C448_SUCCESS: The scalar was correctly encoded.
 * C448_FAILURE: The scalar was greater than the modulus, and has been reduced
 * modulo that modulus.
 */
c448_error_t curve448_scalar_decode(curve448_scalar_t out,
                                    const unsigned char ser[C448_SCALAR_BYTES]);

/*
 * Read a scalar from wire format or from bytes.  Reduces mod scalar prime.
 *
 * ser (in): Serialized form of a scalar.
 * ser_len (in): Length of serialized form.
 * out (out): Deserialized form.
 */
void curve448_scalar_decode_long(curve448_scalar_t out,
                                 const unsigned char *ser, size_t ser_len);

/*
 * Serialize a scalar to wire format.
 *
 * ser (out): Serialized form of a scalar.
 * s (in): Deserialized scalar.
 */
void curve448_scalar_encode(unsigned char ser[C448_SCALAR_BYTES],
                            const curve448_scalar_t s);

/*
 * Add two scalars. |a|, |b| and |out| may alias each other.
 *
 * a (in): One scalar.
 * b (in): Another scalar.
 * out (out): a+b.
 */
void curve448_scalar_add(curve448_scalar_t out,
                         const curve448_scalar_t a, const curve448_scalar_t b);

/*
 * Subtract two scalars.  |a|, |b| and |out| may alias each other.
 * a (in): One scalar.
 * b (in): Another scalar.
 * out (out): a-b.
 */
void curve448_scalar_sub(curve448_scalar_t out,
                         const curve448_scalar_t a, const curve448_scalar_t b);

/*
 * Multiply two scalars. |a|, |b| and |out| may alias each other.
 *
 * a (in): One scalar.
 * b (in): Another scalar.
 * out (out): a*b.
 */
void curve448_scalar_mul(curve448_scalar_t out,
                         const curve448_scalar_t a, const curve448_scalar_t b);

/*
* Halve a scalar.  |a| and |out| may alias each other.
*
* a (in): A scalar.
* out (out): a/2.
*/
void curve448_scalar_halve(curve448_scalar_t out, const curve448_scalar_t a);

/*
 * Copy a scalar.  The scalars may alias each other, in which case this
 * function does nothing.
 *
 * a (in): A scalar.
 * out (out): Will become a copy of a.
 */
static ossl_inline void curve448_scalar_copy(curve448_scalar_t out,
                                             const curve448_scalar_t a)
{
    *out = *a;
}

/*
 * Copy a point.  The input and output may alias, in which case this function
 * does nothing.
 *
 * a (out): A copy of the point.
 * b (in): Any point.
 */
static ossl_inline void curve448_point_copy(curve448_point_t a,
                                            const curve448_point_t b)
{
    *a = *b;
}

/*
 * Test whether two points are equal.  If yes, return C448_TRUE, else return
 * C448_FALSE.
 *
 * a (in): A point.
 * b (in): Another point.
 *
 * Returns:
 * C448_TRUE: The points are equal.
 * C448_FALSE: The points are not equal.
 */
__owur c448_bool_t curve448_point_eq(const curve448_point_t a,
                                     const curve448_point_t b);

/*
 * Double a point. Equivalent to curve448_point_add(two_a,a,a), but potentially
 * faster.
 *
 * two_a (out): The sum a+a.
 * a (in): A point.
 */
void curve448_point_double(curve448_point_t two_a, const curve448_point_t a);

/*
 * RFC 7748 Diffie-Hellman scalarmul.  This function uses a different
 * (non-Decaf) encoding.
 *
 * out (out): The scaled point base*scalar
 * base (in): The point to be scaled.
 * scalar (in): The scalar to multiply by.
 *
 * Returns:
 * C448_SUCCESS: The scalarmul succeeded.
 * C448_FAILURE: The scalarmul didn't succeed, because the base point is in a
 * small subgroup.
 */
__owur c448_error_t x448_int(uint8_t out[X448_PUBLIC_BYTES],
                             const uint8_t base[X448_PUBLIC_BYTES],
                             const uint8_t scalar[X448_PRIVATE_BYTES]);

/*
 * Multiply a point by X448_ENCODE_RATIO, then encode it like RFC 7748.
 *
 * This function is mainly used internally, but is exported in case
 * it will be useful.
 *
 * The ratio is necessary because the internal representation doesn't
 * track the cofactor information, so on output we must clear the cofactor.
 * This would multiply by the cofactor, but in fact internally points are always
 * even, so it multiplies by half the cofactor instead.
 *
 * As it happens, this aligns with the base point definitions; that is,
 * if you pass the Decaf/Ristretto base point to this function, the result
 * will be X448_ENCODE_RATIO times the X448
 * base point.
 *
 * out (out): The scaled and encoded point.
 * p (in): The point to be scaled and encoded.
 */
void curve448_point_mul_by_ratio_and_encode_like_x448(
                                        uint8_t out[X448_PUBLIC_BYTES],
                                        const curve448_point_t p);

/*
 * RFC 7748 Diffie-Hellman base point scalarmul.  This function uses a different
 * (non-Decaf) encoding.
 *
 * out (out): The scaled point base*scalar
 * scalar (in): The scalar to multiply by.
 */
void x448_derive_public_key(uint8_t out[X448_PUBLIC_BYTES],
                            const uint8_t scalar[X448_PRIVATE_BYTES]);

/*
 * Multiply a precomputed base point by a scalar: out = scalar*base.
 *
 * scaled (out): The scaled point base*scalar
 * base (in): The point to be scaled.
 * scalar (in): The scalar to multiply by.
 */
void curve448_precomputed_scalarmul(curve448_point_t scaled,
                                    const curve448_precomputed_s * base,
                                    const curve448_scalar_t scalar);

/*
 * Multiply two base points by two scalars:
 * combo = scalar1*curve448_point_base + scalar2*base2.
 *
 * Otherwise equivalent to curve448_point_double_scalarmul, but may be
 * faster at the expense of being variable time.
 *
 * combo (out): The linear combination scalar1*base + scalar2*base2.
 * scalar1 (in): A first scalar to multiply by.
 * base2 (in): A second point to be scaled.
 * scalar2 (in) A second scalar to multiply by.
 *
 * Warning: This function takes variable time, and may leak the scalars used.
 * It is designed for signature verification.
 */
void curve448_base_double_scalarmul_non_secret(curve448_point_t combo,
                                               const curve448_scalar_t scalar1,
                                               const curve448_point_t base2,
                                               const curve448_scalar_t scalar2);

/*
 * Test that a point is valid, for debugging purposes.
 *
 * to_test (in): The point to test.
 *
 * Returns:
 * C448_TRUE The point is valid.
 * C448_FALSE The point is invalid.
 */
__owur c448_bool_t curve448_point_valid(const curve448_point_t to_test);

/* Overwrite scalar with zeros. */
void curve448_scalar_destroy(curve448_scalar_t scalar);

/* Overwrite point with zeros. */
void curve448_point_destroy(curve448_point_t point);

#endif                          /* HEADER_POINT_448_H */
