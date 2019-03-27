/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef BR_BEARSSL_EC_H__
#define BR_BEARSSL_EC_H__

#include <stddef.h>
#include <stdint.h>

#include "bearssl_rand.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file bearssl_ec.h
 *
 * # Elliptic Curves
 *
 * This file documents the EC implementations provided with BearSSL, and
 * ECDSA.
 *
 * ## Elliptic Curve API
 *
 * Only "named curves" are supported. Each EC implementation supports
 * one or several named curves, identified by symbolic identifiers.
 * These identifiers are small integers, that correspond to the values
 * registered by the
 * [IANA](http://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-8).
 *
 * Since all currently defined elliptic curve identifiers are in the 0..31
 * range, it is convenient to encode support of some curves in a 32-bit
 * word, such that bit x corresponds to curve of identifier x.
 *
 * An EC implementation is incarnated by a `br_ec_impl` instance, that
 * offers the following fields:
 *
 *   - `supported_curves`
 *
 *      A 32-bit word that documents the identifiers of the curves supported
 *      by this implementation.
 *
 *   - `generator()`
 *
 *      Callback method that returns a pointer to the conventional generator
 *      point for that curve.
 *
 *   - `order()`
 *
 *      Callback method that returns a pointer to the subgroup order for
 *      that curve. That value uses unsigned big-endian encoding.
 *
 *   - `xoff()`
 *
 *      Callback method that returns the offset and length of the X
 *      coordinate in an encoded point.
 *
 *   - `mul()`
 *
 *      Multiply a curve point with an integer.
 *
 *   - `mulgen()`
 *
 *      Multiply the curve generator with an integer. This may be faster
 *      than the generic `mul()`.
 *
 *   - `muladd()`
 *
 *      Multiply two curve points by two integers, and return the sum of
 *      the two products.
 *
 * All curve points are represented in uncompressed format. The `mul()`
 * and `muladd()` methods take care to validate that the provided points
 * are really part of the relevant curve subgroup.
 *
 * For all point multiplication functions, the following holds:
 *
 *   - Functions validate that the provided points are valid members
 *     of the relevant curve subgroup. An error is reported if that is
 *     not the case.
 *
 *   - Processing is constant-time, even if the point operands are not
 *     valid. This holds for both the source and resulting points, and
 *     the multipliers (integers). Only the byte length of the provided
 *     multiplier arrays (not their actual value length in bits) may
 *     leak through timing-based side channels.
 *
 *   - The multipliers (integers) MUST be lower than the subgroup order.
 *     If this property is not met, then the result is indeterminate,
 *     but an error value is not ncessearily returned.
 * 
 *
 * ## ECDSA
 *
 * ECDSA signatures have two standard formats, called "raw" and "asn1".
 * Internally, such a signature is a pair of modular integers `(r,s)`.
 * The "raw" format is the concatenation of the unsigned big-endian
 * encodings of these two integers, possibly left-padded with zeros so
 * that they have the same encoded length. The "asn1" format is the
 * DER encoding of an ASN.1 structure that contains the two integer
 * values:
 *
 *     ECDSASignature ::= SEQUENCE {
 *         r   INTEGER,
 *         s   INTEGER
 *     }
 *
 * In general, in all of X.509 and SSL/TLS, the "asn1" format is used.
 * BearSSL offers ECDSA implementations for both formats; conversion
 * functions between the two formats are also provided. Conversion of a
 * "raw" format signature into "asn1" may enlarge a signature by no more
 * than 9 bytes for all supported curves; conversely, conversion of an
 * "asn1" signature to "raw" may expand the signature but the "raw"
 * length will never be more than twice the length of the "asn1" length
 * (and usually it will be shorter).
 *
 * Note that for a given signature, the "raw" format is not fully
 * deterministic, in that it does not enforce a minimal common length.
 */

/*
 * Standard curve ID. These ID are equal to the assigned numerical
 * identifiers assigned to these curves for TLS:
 *    http://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-8
 */

/** \brief Identifier for named curve sect163k1. */
#define BR_EC_sect163k1           1

/** \brief Identifier for named curve sect163r1. */
#define BR_EC_sect163r1           2

/** \brief Identifier for named curve sect163r2. */
#define BR_EC_sect163r2           3

/** \brief Identifier for named curve sect193r1. */
#define BR_EC_sect193r1           4

/** \brief Identifier for named curve sect193r2. */
#define BR_EC_sect193r2           5

/** \brief Identifier for named curve sect233k1. */
#define BR_EC_sect233k1           6

/** \brief Identifier for named curve sect233r1. */
#define BR_EC_sect233r1           7

/** \brief Identifier for named curve sect239k1. */
#define BR_EC_sect239k1           8

/** \brief Identifier for named curve sect283k1. */
#define BR_EC_sect283k1           9

/** \brief Identifier for named curve sect283r1. */
#define BR_EC_sect283r1          10

/** \brief Identifier for named curve sect409k1. */
#define BR_EC_sect409k1          11

/** \brief Identifier for named curve sect409r1. */
#define BR_EC_sect409r1          12

/** \brief Identifier for named curve sect571k1. */
#define BR_EC_sect571k1          13

/** \brief Identifier for named curve sect571r1. */
#define BR_EC_sect571r1          14

/** \brief Identifier for named curve secp160k1. */
#define BR_EC_secp160k1          15

/** \brief Identifier for named curve secp160r1. */
#define BR_EC_secp160r1          16

/** \brief Identifier for named curve secp160r2. */
#define BR_EC_secp160r2          17

/** \brief Identifier for named curve secp192k1. */
#define BR_EC_secp192k1          18

/** \brief Identifier for named curve secp192r1. */
#define BR_EC_secp192r1          19

/** \brief Identifier for named curve secp224k1. */
#define BR_EC_secp224k1          20

/** \brief Identifier for named curve secp224r1. */
#define BR_EC_secp224r1          21

/** \brief Identifier for named curve secp256k1. */
#define BR_EC_secp256k1          22

/** \brief Identifier for named curve secp256r1. */
#define BR_EC_secp256r1          23

/** \brief Identifier for named curve secp384r1. */
#define BR_EC_secp384r1          24

/** \brief Identifier for named curve secp521r1. */
#define BR_EC_secp521r1          25

/** \brief Identifier for named curve brainpoolP256r1. */
#define BR_EC_brainpoolP256r1    26

/** \brief Identifier for named curve brainpoolP384r1. */
#define BR_EC_brainpoolP384r1    27

/** \brief Identifier for named curve brainpoolP512r1. */
#define BR_EC_brainpoolP512r1    28

/** \brief Identifier for named curve Curve25519. */
#define BR_EC_curve25519         29

/** \brief Identifier for named curve Curve448. */
#define BR_EC_curve448           30

/**
 * \brief Structure for an EC public key.
 */
typedef struct {
	/** \brief Identifier for the curve used by this key. */
	int curve;
	/** \brief Public curve point (uncompressed format). */
	unsigned char *q;
	/** \brief Length of public curve point (in bytes). */
	size_t qlen;
} br_ec_public_key;

/**
 * \brief Structure for an EC private key.
 *
 * The private key is an integer modulo the curve subgroup order. The
 * encoding below tolerates extra leading zeros. In general, it is
 * recommended that the private key has the same length as the curve
 * subgroup order.
 */
typedef struct {
	/** \brief Identifier for the curve used by this key. */
	int curve;
	/** \brief Private key (integer, unsigned big-endian encoding). */
	unsigned char *x;
	/** \brief Private key length (in bytes). */
	size_t xlen;
} br_ec_private_key;

/**
 * \brief Type for an EC implementation.
 */
typedef struct {
	/**
	 * \brief Supported curves.
	 *
	 * This word is a bitfield: bit `x` is set if the curve of ID `x`
	 * is supported. E.g. an implementation supporting both NIST P-256
	 * (secp256r1, ID 23) and NIST P-384 (secp384r1, ID 24) will have
	 * value `0x01800000` in this field.
	 */
	uint32_t supported_curves;

	/**
	 * \brief Get the conventional generator.
	 *
	 * This function returns the conventional generator (encoded
	 * curve point) for the specified curve. This function MUST NOT
	 * be called if the curve is not supported.
	 *
	 * \param curve   curve identifier.
	 * \param len     receiver for the encoded generator length (in bytes).
	 * \return  the encoded generator.
	 */
	const unsigned char *(*generator)(int curve, size_t *len);

	/**
	 * \brief Get the subgroup order.
	 *
	 * This function returns the order of the subgroup generated by
	 * the conventional generator, for the specified curve. Unsigned
	 * big-endian encoding is used. This function MUST NOT be called
	 * if the curve is not supported.
	 *
	 * \param curve   curve identifier.
	 * \param len     receiver for the encoded order length (in bytes).
	 * \return  the encoded order.
	 */
	const unsigned char *(*order)(int curve, size_t *len);

	/**
	 * \brief Get the offset and length for the X coordinate.
	 *
	 * This function returns the offset and length (in bytes) of
	 * the X coordinate in an encoded non-zero point.
	 *
	 * \param curve   curve identifier.
	 * \param len     receiver for the X coordinate length (in bytes).
	 * \return  the offset for the X coordinate (in bytes).
	 */
	size_t (*xoff)(int curve, size_t *len);

	/**
	 * \brief Multiply a curve point by an integer.
	 *
	 * The source point is provided in array `G` (of size `Glen` bytes);
	 * the multiplication result is written over it. The multiplier
	 * `x` (of size `xlen` bytes) uses unsigned big-endian encoding.
	 *
	 * Rules:
	 *
	 *   - The specified curve MUST be supported.
	 *
	 *   - The source point must be a valid point on the relevant curve
	 *     subgroup (and not the "point at infinity" either). If this is
	 *     not the case, then this function returns an error (0).
	 *
	 *   - The multiplier integer MUST be non-zero and less than the
	 *     curve subgroup order. If this property does not hold, then
	 *     the result is indeterminate and an error code is not
	 *     guaranteed.
	 *
	 * Returned value is 1 on success, 0 on error. On error, the
	 * contents of `G` are indeterminate.
	 *
	 * \param G       point to multiply.
	 * \param Glen    length of the encoded point (in bytes).
	 * \param x       multiplier (unsigned big-endian).
	 * \param xlen    multiplier length (in bytes).
	 * \param curve   curve identifier.
	 * \return  1 on success, 0 on error.
	 */
	uint32_t (*mul)(unsigned char *G, size_t Glen,
		const unsigned char *x, size_t xlen, int curve);

	/**
	 * \brief Multiply the generator by an integer.
	 *
	 * The multiplier MUST be non-zero and less than the curve
	 * subgroup order. Results are indeterminate if this property
	 * does not hold.
	 *
	 * \param R       output buffer for the point.
	 * \param x       multiplier (unsigned big-endian).
	 * \param xlen    multiplier length (in bytes).
	 * \param curve   curve identifier.
	 * \return  encoded result point length (in bytes).
	 */
	size_t (*mulgen)(unsigned char *R,
		const unsigned char *x, size_t xlen, int curve);

	/**
	 * \brief Multiply two points by two integers and add the
	 * results.
	 *
	 * The point `x*A + y*B` is computed and written back in the `A`
	 * array.
	 *
	 * Rules:
	 *
	 *   - The specified curve MUST be supported.
	 *
	 *   - The source points (`A` and `B`)  must be valid points on
	 *     the relevant curve subgroup (and not the "point at
	 *     infinity" either). If this is not the case, then this
	 *     function returns an error (0).
	 *
	 *   - If the `B` pointer is `NULL`, then the conventional
	 *     subgroup generator is used. With some implementations,
	 *     this may be faster than providing a pointer to the
	 *     generator.
	 *
	 *   - The multiplier integers (`x` and `y`) MUST be non-zero
	 *     and less than the curve subgroup order. If either integer
	 *     is zero, then an error is reported, but if one of them is
	 *     not lower than the subgroup order, then the result is
	 *     indeterminate and an error code is not guaranteed.
	 *
	 *   - If the final result is the point at infinity, then an
	 *     error is returned.
	 *
	 * Returned value is 1 on success, 0 on error. On error, the
	 * contents of `A` are indeterminate.
	 *
	 * \param A       first point to multiply.
	 * \param B       second point to multiply (`NULL` for the generator).
	 * \param len     common length of the encoded points (in bytes).
	 * \param x       multiplier for `A` (unsigned big-endian).
	 * \param xlen    length of multiplier for `A` (in bytes).
	 * \param y       multiplier for `A` (unsigned big-endian).
	 * \param ylen    length of multiplier for `A` (in bytes).
	 * \param curve   curve identifier.
	 * \return  1 on success, 0 on error.
	 */
	uint32_t (*muladd)(unsigned char *A, const unsigned char *B, size_t len,
		const unsigned char *x, size_t xlen,
		const unsigned char *y, size_t ylen, int curve);
} br_ec_impl;

/**
 * \brief EC implementation "i31".
 *
 * This implementation internally uses generic code for modular integers,
 * with a representation as sequences of 31-bit words. It supports secp256r1,
 * secp384r1 and secp521r1 (aka NIST curves P-256, P-384 and P-521).
 */
extern const br_ec_impl br_ec_prime_i31;

/**
 * \brief EC implementation "i15".
 *
 * This implementation internally uses generic code for modular integers,
 * with a representation as sequences of 15-bit words. It supports secp256r1,
 * secp384r1 and secp521r1 (aka NIST curves P-256, P-384 and P-521).
 */
extern const br_ec_impl br_ec_prime_i15;

/**
 * \brief EC implementation "m15" for P-256.
 *
 * This implementation uses specialised code for curve secp256r1 (also
 * known as NIST P-256), with optional Karatsuba decomposition, and fast
 * modular reduction thanks to the field modulus special format. Only
 * 32-bit multiplications are used (with 32-bit results, not 64-bit).
 */
extern const br_ec_impl br_ec_p256_m15;

/**
 * \brief EC implementation "m31" for P-256.
 *
 * This implementation uses specialised code for curve secp256r1 (also
 * known as NIST P-256), relying on multiplications of 31-bit values
 * (MUL31).
 */
extern const br_ec_impl br_ec_p256_m31;

/**
 * \brief EC implementation "m62" (specialised code) for P-256.
 *
 * This implementation uses custom code relying on multiplication of
 * integers up to 64 bits, with a 128-bit result. This implementation is
 * defined only on platforms that offer the 64x64->128 multiplication
 * support; use `br_ec_p256_m62_get()` to dynamically obtain a pointer
 * to that implementation.
 */
extern const br_ec_impl br_ec_p256_m62;

/**
 * \brief Get the "m62" implementation of P-256, if available.
 *
 * \return  the implementation, or 0.
 */
const br_ec_impl *br_ec_p256_m62_get(void);

/**
 * \brief EC implementation "m64" (specialised code) for P-256.
 *
 * This implementation uses custom code relying on multiplication of
 * integers up to 64 bits, with a 128-bit result. This implementation is
 * defined only on platforms that offer the 64x64->128 multiplication
 * support; use `br_ec_p256_m64_get()` to dynamically obtain a pointer
 * to that implementation.
 */
extern const br_ec_impl br_ec_p256_m64;

/**
 * \brief Get the "m64" implementation of P-256, if available.
 *
 * \return  the implementation, or 0.
 */
const br_ec_impl *br_ec_p256_m64_get(void);

/**
 * \brief EC implementation "i15" (generic code) for Curve25519.
 *
 * This implementation uses the generic code for modular integers (with
 * 15-bit words) to support Curve25519. Due to the specificities of the
 * curve definition, the following applies:
 *
 *   - `muladd()` is not implemented (the function returns 0 systematically).
 *   - `order()` returns 2^255-1, since the point multiplication algorithm
 *     accepts any 32-bit integer as input (it clears the top bit and low
 *     three bits systematically).
 */
extern const br_ec_impl br_ec_c25519_i15;

/**
 * \brief EC implementation "i31" (generic code) for Curve25519.
 *
 * This implementation uses the generic code for modular integers (with
 * 31-bit words) to support Curve25519. Due to the specificities of the
 * curve definition, the following applies:
 *
 *   - `muladd()` is not implemented (the function returns 0 systematically).
 *   - `order()` returns 2^255-1, since the point multiplication algorithm
 *     accepts any 32-bit integer as input (it clears the top bit and low
 *     three bits systematically).
 */
extern const br_ec_impl br_ec_c25519_i31;

/**
 * \brief EC implementation "m15" (specialised code) for Curve25519.
 *
 * This implementation uses custom code relying on multiplication of
 * integers up to 15 bits. Due to the specificities of the curve
 * definition, the following applies:
 *
 *   - `muladd()` is not implemented (the function returns 0 systematically).
 *   - `order()` returns 2^255-1, since the point multiplication algorithm
 *     accepts any 32-bit integer as input (it clears the top bit and low
 *     three bits systematically).
 */
extern const br_ec_impl br_ec_c25519_m15;

/**
 * \brief EC implementation "m31" (specialised code) for Curve25519.
 *
 * This implementation uses custom code relying on multiplication of
 * integers up to 31 bits. Due to the specificities of the curve
 * definition, the following applies:
 *
 *   - `muladd()` is not implemented (the function returns 0 systematically).
 *   - `order()` returns 2^255-1, since the point multiplication algorithm
 *     accepts any 32-bit integer as input (it clears the top bit and low
 *     three bits systematically).
 */
extern const br_ec_impl br_ec_c25519_m31;

/**
 * \brief EC implementation "m62" (specialised code) for Curve25519.
 *
 * This implementation uses custom code relying on multiplication of
 * integers up to 62 bits, with a 124-bit result. This implementation is
 * defined only on platforms that offer the 64x64->128 multiplication
 * support; use `br_ec_c25519_m62_get()` to dynamically obtain a pointer
 * to that implementation. Due to the specificities of the curve
 * definition, the following applies:
 *
 *   - `muladd()` is not implemented (the function returns 0 systematically).
 *   - `order()` returns 2^255-1, since the point multiplication algorithm
 *     accepts any 32-bit integer as input (it clears the top bit and low
 *     three bits systematically).
 */
extern const br_ec_impl br_ec_c25519_m62;

/**
 * \brief Get the "m62" implementation of Curve25519, if available.
 *
 * \return  the implementation, or 0.
 */
const br_ec_impl *br_ec_c25519_m62_get(void);

/**
 * \brief EC implementation "m64" (specialised code) for Curve25519.
 *
 * This implementation uses custom code relying on multiplication of
 * integers up to 64 bits, with a 128-bit result. This implementation is
 * defined only on platforms that offer the 64x64->128 multiplication
 * support; use `br_ec_c25519_m64_get()` to dynamically obtain a pointer
 * to that implementation. Due to the specificities of the curve
 * definition, the following applies:
 *
 *   - `muladd()` is not implemented (the function returns 0 systematically).
 *   - `order()` returns 2^255-1, since the point multiplication algorithm
 *     accepts any 32-bit integer as input (it clears the top bit and low
 *     three bits systematically).
 */
extern const br_ec_impl br_ec_c25519_m64;

/**
 * \brief Get the "m64" implementation of Curve25519, if available.
 *
 * \return  the implementation, or 0.
 */
const br_ec_impl *br_ec_c25519_m64_get(void);

/**
 * \brief Aggregate EC implementation "m15".
 *
 * This implementation is a wrapper for:
 *
 *   - `br_ec_c25519_m15` for Curve25519
 *   - `br_ec_p256_m15` for NIST P-256
 *   - `br_ec_prime_i15` for other curves (NIST P-384 and NIST-P512)
 */
extern const br_ec_impl br_ec_all_m15;

/**
 * \brief Aggregate EC implementation "m31".
 *
 * This implementation is a wrapper for:
 *
 *   - `br_ec_c25519_m31` for Curve25519
 *   - `br_ec_p256_m31` for NIST P-256
 *   - `br_ec_prime_i31` for other curves (NIST P-384 and NIST-P512)
 */
extern const br_ec_impl br_ec_all_m31;

/**
 * \brief Get the "default" EC implementation for the current system.
 *
 * This returns a pointer to the preferred implementation on the
 * current system.
 *
 * \return  the default EC implementation.
 */
const br_ec_impl *br_ec_get_default(void);

/**
 * \brief Convert a signature from "raw" to "asn1".
 *
 * Conversion is done "in place" and the new length is returned.
 * Conversion may enlarge the signature, but by no more than 9 bytes at
 * most. On error, 0 is returned (error conditions include an odd raw
 * signature length, or an oversized integer).
 *
 * \param sig       signature to convert.
 * \param sig_len   signature length (in bytes).
 * \return  the new signature length, or 0 on error.
 */
size_t br_ecdsa_raw_to_asn1(void *sig, size_t sig_len);

/**
 * \brief Convert a signature from "asn1" to "raw".
 *
 * Conversion is done "in place" and the new length is returned.
 * Conversion may enlarge the signature, but the new signature length
 * will be less than twice the source length at most. On error, 0 is
 * returned (error conditions include an invalid ASN.1 structure or an
 * oversized integer).
 *
 * \param sig       signature to convert.
 * \param sig_len   signature length (in bytes).
 * \return  the new signature length, or 0 on error.
 */
size_t br_ecdsa_asn1_to_raw(void *sig, size_t sig_len);

/**
 * \brief Type for an ECDSA signer function.
 *
 * A pointer to the EC implementation is provided. The hash value is
 * assumed to have the length inferred from the designated hash function
 * class.
 *
 * Signature is written in the buffer pointed to by `sig`, and the length
 * (in bytes) is returned. On error, nothing is written in the buffer,
 * and 0 is returned. This function returns 0 if the specified curve is
 * not supported by the provided EC implementation.
 *
 * The signature format is either "raw" or "asn1", depending on the
 * implementation; maximum length is predictable from the implemented
 * curve:
 *
 * | curve      | raw | asn1 |
 * | :--------- | --: | ---: |
 * | NIST P-256 |  64 |   72 |
 * | NIST P-384 |  96 |  104 |
 * | NIST P-521 | 132 |  139 |
 *
 * \param impl         EC implementation to use.
 * \param hf           hash function used to process the data.
 * \param hash_value   signed data (hashed).
 * \param sk           EC private key.
 * \param sig          destination buffer.
 * \return  the signature length (in bytes), or 0 on error.
 */
typedef size_t (*br_ecdsa_sign)(const br_ec_impl *impl,
	const br_hash_class *hf, const void *hash_value,
	const br_ec_private_key *sk, void *sig);

/**
 * \brief Type for an ECDSA signature verification function.
 *
 * A pointer to the EC implementation is provided. The hashed value,
 * computed over the purportedly signed data, is also provided with
 * its length.
 *
 * The signature format is either "raw" or "asn1", depending on the
 * implementation.
 *
 * Returned value is 1 on success (valid signature), 0 on error. This
 * function returns 0 if the specified curve is not supported by the
 * provided EC implementation.
 *
 * \param impl       EC implementation to use.
 * \param hash       signed data (hashed).
 * \param hash_len   hash value length (in bytes).
 * \param pk         EC public key.
 * \param sig        signature.
 * \param sig_len    signature length (in bytes).
 * \return  1 on success, 0 on error.
 */
typedef uint32_t (*br_ecdsa_vrfy)(const br_ec_impl *impl,
	const void *hash, size_t hash_len,
	const br_ec_public_key *pk, const void *sig, size_t sig_len);

/**
 * \brief ECDSA signature generator, "i31" implementation, "asn1" format.
 *
 * \see br_ecdsa_sign()
 *
 * \param impl         EC implementation to use.
 * \param hf           hash function used to process the data.
 * \param hash_value   signed data (hashed).
 * \param sk           EC private key.
 * \param sig          destination buffer.
 * \return  the signature length (in bytes), or 0 on error.
 */
size_t br_ecdsa_i31_sign_asn1(const br_ec_impl *impl,
	const br_hash_class *hf, const void *hash_value,
	const br_ec_private_key *sk, void *sig);

/**
 * \brief ECDSA signature generator, "i31" implementation, "raw" format.
 *
 * \see br_ecdsa_sign()
 *
 * \param impl         EC implementation to use.
 * \param hf           hash function used to process the data.
 * \param hash_value   signed data (hashed).
 * \param sk           EC private key.
 * \param sig          destination buffer.
 * \return  the signature length (in bytes), or 0 on error.
 */
size_t br_ecdsa_i31_sign_raw(const br_ec_impl *impl,
	const br_hash_class *hf, const void *hash_value,
	const br_ec_private_key *sk, void *sig);

/**
 * \brief ECDSA signature verifier, "i31" implementation, "asn1" format.
 *
 * \see br_ecdsa_vrfy()
 *
 * \param impl       EC implementation to use.
 * \param hash       signed data (hashed).
 * \param hash_len   hash value length (in bytes).
 * \param pk         EC public key.
 * \param sig        signature.
 * \param sig_len    signature length (in bytes).
 * \return  1 on success, 0 on error.
 */
uint32_t br_ecdsa_i31_vrfy_asn1(const br_ec_impl *impl,
	const void *hash, size_t hash_len,
	const br_ec_public_key *pk, const void *sig, size_t sig_len);

/**
 * \brief ECDSA signature verifier, "i31" implementation, "raw" format.
 *
 * \see br_ecdsa_vrfy()
 *
 * \param impl       EC implementation to use.
 * \param hash       signed data (hashed).
 * \param hash_len   hash value length (in bytes).
 * \param pk         EC public key.
 * \param sig        signature.
 * \param sig_len    signature length (in bytes).
 * \return  1 on success, 0 on error.
 */
uint32_t br_ecdsa_i31_vrfy_raw(const br_ec_impl *impl,
	const void *hash, size_t hash_len,
	const br_ec_public_key *pk, const void *sig, size_t sig_len);

/**
 * \brief ECDSA signature generator, "i15" implementation, "asn1" format.
 *
 * \see br_ecdsa_sign()
 *
 * \param impl         EC implementation to use.
 * \param hf           hash function used to process the data.
 * \param hash_value   signed data (hashed).
 * \param sk           EC private key.
 * \param sig          destination buffer.
 * \return  the signature length (in bytes), or 0 on error.
 */
size_t br_ecdsa_i15_sign_asn1(const br_ec_impl *impl,
	const br_hash_class *hf, const void *hash_value,
	const br_ec_private_key *sk, void *sig);

/**
 * \brief ECDSA signature generator, "i15" implementation, "raw" format.
 *
 * \see br_ecdsa_sign()
 *
 * \param impl         EC implementation to use.
 * \param hf           hash function used to process the data.
 * \param hash_value   signed data (hashed).
 * \param sk           EC private key.
 * \param sig          destination buffer.
 * \return  the signature length (in bytes), or 0 on error.
 */
size_t br_ecdsa_i15_sign_raw(const br_ec_impl *impl,
	const br_hash_class *hf, const void *hash_value,
	const br_ec_private_key *sk, void *sig);

/**
 * \brief ECDSA signature verifier, "i15" implementation, "asn1" format.
 *
 * \see br_ecdsa_vrfy()
 *
 * \param impl       EC implementation to use.
 * \param hash       signed data (hashed).
 * \param hash_len   hash value length (in bytes).
 * \param pk         EC public key.
 * \param sig        signature.
 * \param sig_len    signature length (in bytes).
 * \return  1 on success, 0 on error.
 */
uint32_t br_ecdsa_i15_vrfy_asn1(const br_ec_impl *impl,
	const void *hash, size_t hash_len,
	const br_ec_public_key *pk, const void *sig, size_t sig_len);

/**
 * \brief ECDSA signature verifier, "i15" implementation, "raw" format.
 *
 * \see br_ecdsa_vrfy()
 *
 * \param impl       EC implementation to use.
 * \param hash       signed data (hashed).
 * \param hash_len   hash value length (in bytes).
 * \param pk         EC public key.
 * \param sig        signature.
 * \param sig_len    signature length (in bytes).
 * \return  1 on success, 0 on error.
 */
uint32_t br_ecdsa_i15_vrfy_raw(const br_ec_impl *impl,
	const void *hash, size_t hash_len,
	const br_ec_public_key *pk, const void *sig, size_t sig_len);

/**
 * \brief Get "default" ECDSA implementation (signer, asn1 format).
 *
 * This returns the preferred implementation of ECDSA signature generation
 * ("asn1" output format) on the current system.
 *
 * \return  the default implementation.
 */
br_ecdsa_sign br_ecdsa_sign_asn1_get_default(void);

/**
 * \brief Get "default" ECDSA implementation (signer, raw format).
 *
 * This returns the preferred implementation of ECDSA signature generation
 * ("raw" output format) on the current system.
 *
 * \return  the default implementation.
 */
br_ecdsa_sign br_ecdsa_sign_raw_get_default(void);

/**
 * \brief Get "default" ECDSA implementation (verifier, asn1 format).
 *
 * This returns the preferred implementation of ECDSA signature verification
 * ("asn1" output format) on the current system.
 *
 * \return  the default implementation.
 */
br_ecdsa_vrfy br_ecdsa_vrfy_asn1_get_default(void);

/**
 * \brief Get "default" ECDSA implementation (verifier, raw format).
 *
 * This returns the preferred implementation of ECDSA signature verification
 * ("raw" output format) on the current system.
 *
 * \return  the default implementation.
 */
br_ecdsa_vrfy br_ecdsa_vrfy_raw_get_default(void);

/**
 * \brief Maximum size for EC private key element buffer.
 *
 * This is the largest number of bytes that `br_ec_keygen()` may need or
 * ever return.
 */
#define BR_EC_KBUF_PRIV_MAX_SIZE   72

/**
 * \brief Maximum size for EC public key element buffer.
 *
 * This is the largest number of bytes that `br_ec_compute_public()` may
 * need or ever return.
 */
#define BR_EC_KBUF_PUB_MAX_SIZE    145

/**
 * \brief Generate a new EC private key.
 *
 * If the specified `curve` is not supported by the elliptic curve
 * implementation (`impl`), then this function returns zero.
 *
 * The `sk` structure fields are set to the new private key data. In
 * particular, `sk.x` is made to point to the provided key buffer (`kbuf`),
 * in which the actual private key data is written. That buffer is assumed
 * to be large enough. The `BR_EC_KBUF_PRIV_MAX_SIZE` defines the maximum
 * size for all supported curves.
 *
 * The number of bytes used in `kbuf` is returned. If `kbuf` is `NULL`, then
 * the private key is not actually generated, and `sk` may also be `NULL`;
 * the minimum length for `kbuf` is still computed and returned.
 *
 * If `sk` is `NULL` but `kbuf` is not `NULL`, then the private key is
 * still generated and stored in `kbuf`.
 *
 * \param rng_ctx   source PRNG context (already initialized).
 * \param impl      the elliptic curve implementation.
 * \param sk        the private key structure to fill, or `NULL`.
 * \param kbuf      the key element buffer, or `NULL`.
 * \param curve     the curve identifier.
 * \return  the key data length (in bytes), or zero.
 */
size_t br_ec_keygen(const br_prng_class **rng_ctx,
	const br_ec_impl *impl, br_ec_private_key *sk,
	void *kbuf, int curve);

/**
 * \brief Compute EC public key from EC private key.
 *
 * This function uses the provided elliptic curve implementation (`impl`)
 * to compute the public key corresponding to the private key held in `sk`.
 * The public key point is written into `kbuf`, which is then linked from
 * the `*pk` structure. The size of the public key point, i.e. the number
 * of bytes used in `kbuf`, is returned.
 *
 * If `kbuf` is `NULL`, then the public key point is NOT computed, and
 * the public key structure `*pk` is unmodified (`pk` may be `NULL` in
 * that case). The size of the public key point is still returned.
 *
 * If `pk` is `NULL` but `kbuf` is not `NULL`, then the public key
 * point is computed and stored in `kbuf`, and its size is returned.
 *
 * If the curve used by the private key is not supported by the curve
 * implementation, then this function returns zero.
 *
 * The private key MUST be valid. An off-range private key value is not
 * necessarily detected, and leads to unpredictable results.
 *
 * \param impl   the elliptic curve implementation.
 * \param pk     the public key structure to fill (or `NULL`).
 * \param kbuf   the public key point buffer (or `NULL`).
 * \param sk     the source private key.
 * \return  the public key point length (in bytes), or zero.
 */
size_t br_ec_compute_pub(const br_ec_impl *impl, br_ec_public_key *pk,
	void *kbuf, const br_ec_private_key *sk);

#ifdef __cplusplus
}
#endif

#endif
