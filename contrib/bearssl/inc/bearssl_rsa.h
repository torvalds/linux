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

#ifndef BR_BEARSSL_RSA_H__
#define BR_BEARSSL_RSA_H__

#include <stddef.h>
#include <stdint.h>

#include "bearssl_hash.h"
#include "bearssl_rand.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file bearssl_rsa.h
 *
 * # RSA
 *
 * This file documents the RSA implementations provided with BearSSL.
 * Note that the SSL engine accesses these implementations through a
 * configurable API, so it is possible to, for instance, run a SSL
 * server which uses a RSA engine which is not based on this code.
 *
 * ## Key Elements
 *
 * RSA public and private keys consist in lists of big integers. All
 * such integers are represented with big-endian unsigned notation:
 * first byte is the most significant, and the value is positive (so
 * there is no dedicated "sign bit"). Public and private key structures
 * thus contain, for each such integer, a pointer to the first value byte
 * (`unsigned char *`), and a length (`size_t`) which is the number of
 * relevant bytes. As a general rule, minimal-length encoding is not
 * enforced: values may have extra leading bytes of value 0.
 *
 * RSA public keys consist in two integers:
 *
 *   - the modulus (`n`);
 *   - the public exponent (`e`).
 *
 * RSA private keys, as defined in
 * [PKCS#1](https://tools.ietf.org/html/rfc3447), contain eight integers:
 *
 *   - the modulus (`n`);
 *   - the public exponent (`e`);
 *   - the private exponent (`d`);
 *   - the first prime factor (`p`);
 *   - the second prime factor (`q`);
 *   - the first reduced exponent (`dp`, which is `d` modulo `p-1`);
 *   - the second reduced exponent (`dq`, which is `d` modulo `q-1`);
 *   - the CRT coefficient (`iq`, the inverse of `q` modulo `p`).
 *
 * However, the implementations defined in BearSSL use only five of
 * these integers: `p`, `q`, `dp`, `dq` and `iq`.
 *
 * ## Security Features and Limitations
 *
 * The implementations contained in BearSSL have the following limitations
 * and features:
 *
 *   - They are constant-time. This means that the execution time and
 *     memory access pattern may depend on the _lengths_ of the private
 *     key components, but not on their value, nor on the value of
 *     the operand. Note that this property is not achieved through
 *     random masking, but "true" constant-time code.
 *
 *   - They support only private keys with two prime factors. RSA private
 *     keys with three or more prime factors are nominally supported, but
 *     rarely used; they may offer faster operations, at the expense of
 *     more code and potentially a reduction in security if there are
 *     "too many" prime factors.
 *
 *   - The public exponent may have arbitrary length. Of course, it is
 *     a good idea to keep public exponents small, so that public key
 *     operations are fast; but, contrary to some widely deployed
 *     implementations, BearSSL has no problem with public exponents
 *     longer than 32 bits.
 *
 *   - The two prime factors of the modulus need not have the same length
 *     (but severely imbalanced factor lengths might reduce security).
 *     Similarly, there is no requirement that the first factor (`p`)
 *     be greater than the second factor (`q`).
 *
 *   - Prime factors and modulus must be smaller than a compile-time limit.
 *     This is made necessary by the use of fixed-size stack buffers, and
 *     the limit has been adjusted to keep stack usage under 2 kB for the
 *     RSA operations. Currently, the maximum modulus size is 4096 bits,
 *     and the maximum prime factor size is 2080 bits.
 *
 *   - The RSA functions themselves do not enforce lower size limits,
 *     except that which is absolutely necessary for the operation to
 *     mathematically make sense (e.g. a PKCS#1 v1.5 signature with
 *     SHA-1 requires a modulus of at least 361 bits). It is up to users
 *     of this code to enforce size limitations when appropriate (e.g.
 *     the X.509 validation engine, by default, rejects RSA keys of
 *     less than 1017 bits).
 *
 *   - Within the size constraints expressed above, arbitrary bit lengths
 *     are supported. There is no requirement that prime factors or
 *     modulus have a size multiple of 8 or 16.
 *
 *   - When verifying PKCS#1 v1.5 signatures, both variants of the hash
 *     function identifying header (with and without the ASN.1 NULL) are
 *     supported. When producing such signatures, the variant with the
 *     ASN.1 NULL is used.
 *
 * ## Implementations
 *
 * Three RSA implementations are included:
 *
 *   - The **i32** implementation internally represents big integers
 *     as arrays of 32-bit integers. It is perfunctory and portable,
 *     but not very efficient.
 *
 *   - The **i31** implementation uses 32-bit integers, each containing
 *     31 bits worth of integer data. The i31 implementation is somewhat
 *     faster than the i32 implementation (the reduced integer size makes
 *     carry propagation easier) for a similar code footprint, but uses
 *     very slightly larger stack buffers (about 4% bigger).
 *
 *   - The **i62** implementation is similar to the i31 implementation,
 *     except that it internally leverages the 64x64->128 multiplication
 *     opcode. This implementation is available only on architectures
 *     where such an opcode exists. It is much faster than i31.
 *
 *   - The **i15** implementation uses 16-bit integers, each containing
 *     15 bits worth of integer data. Multiplication results fit on
 *     32 bits, so this won't use the "widening" multiplication routine
 *     on ARM Cortex M0/M0+, for much better performance and constant-time
 *     execution.
 */

/**
 * \brief RSA public key.
 *
 * The structure references the modulus and the public exponent. Both
 * integers use unsigned big-endian representation; extra leading bytes
 * of value 0 are allowed.
 */
typedef struct {
	/** \brief Modulus. */
	unsigned char *n;
	/** \brief Modulus length (in bytes). */
	size_t nlen;
	/** \brief Public exponent. */
	unsigned char *e;
	/** \brief Public exponent length (in bytes). */
	size_t elen;
} br_rsa_public_key;

/**
 * \brief RSA private key.
 *
 * The structure references the private factors, reduced private
 * exponents, and CRT coefficient. It also contains the bit length of
 * the modulus. The big integers use unsigned big-endian representation;
 * extra leading bytes of value 0 are allowed. However, the modulus bit
 * length (`n_bitlen`) MUST be exact.
 */
typedef struct {
	/** \brief Modulus bit length (in bits, exact value). */
	uint32_t n_bitlen;
	/** \brief First prime factor. */
	unsigned char *p;
	/** \brief First prime factor length (in bytes). */
	size_t plen;
	/** \brief Second prime factor. */
	unsigned char *q;
	/** \brief Second prime factor length (in bytes). */
	size_t qlen;
	/** \brief First reduced private exponent. */
	unsigned char *dp;
	/** \brief First reduced private exponent length (in bytes). */
	size_t dplen;
	/** \brief Second reduced private exponent. */
	unsigned char *dq;
	/** \brief Second reduced private exponent length (in bytes). */
	size_t dqlen;
	/** \brief CRT coefficient. */
	unsigned char *iq;
	/** \brief CRT coefficient length (in bytes). */
	size_t iqlen;
} br_rsa_private_key;

/**
 * \brief Type for a RSA public key engine.
 *
 * The public key engine performs the modular exponentiation of the
 * provided value with the public exponent. The value is modified in
 * place.
 *
 * The value length (`xlen`) is verified to have _exactly_ the same
 * length as the modulus (actual modulus length, without extra leading
 * zeros in the modulus representation in memory). If the length does
 * not match, then this function returns 0 and `x[]` is unmodified.
 * 
 * It `xlen` is correct, then `x[]` is modified. Returned value is 1
 * on success, 0 on error. Error conditions include an oversized `x[]`
 * (the array has the same length as the modulus, but the numerical value
 * is not lower than the modulus) and an invalid modulus (e.g. an even
 * integer). If an error is reported, then the new contents of `x[]` are
 * unspecified.
 *
 * \param x      operand to exponentiate.
 * \param xlen   length of the operand (in bytes).
 * \param pk     RSA public key.
 * \return  1 on success, 0 on error.
 */
typedef uint32_t (*br_rsa_public)(unsigned char *x, size_t xlen,
	const br_rsa_public_key *pk);

/**
 * \brief Type for a RSA signature verification engine (PKCS#1 v1.5).
 *
 * Parameters are:
 *
 *   - The signature itself. The provided array is NOT modified.
 *
 *   - The encoded OID for the hash function. The provided array must begin
 *     with a single byte that contains the length of the OID value (in
 *     bytes), followed by exactly that many bytes. This parameter may
 *     also be `NULL`, in which case the raw hash value should be used
 *     with the PKCS#1 v1.5 "type 1" padding (as used in SSL/TLS up
 *     to TLS-1.1, with a 36-byte hash value).
 *
 *   - The hash output length, in bytes.
 *
 *   - The public key.
 *
 *   - An output buffer for the hash value. The caller must still compare
 *     it with the hash of the data over which the signature is computed.
 *
 * **Constraints:**
 *
 *   - Hash length MUST be no more than 64 bytes.
 *
 *   - OID value length MUST be no more than 32 bytes (i.e. `hash_oid[0]`
 *     must have a value in the 0..32 range, inclusive).
 *
 * This function verifies that the signature length (`xlen`) matches the
 * modulus length (this function returns 0 on mismatch). If the modulus
 * size exceeds the maximum supported RSA size, then the function also
 * returns 0.
 *
 * Returned value is 1 on success, 0 on error.
 *
 * Implementations of this type need not be constant-time.
 *
 * \param x          signature buffer.
 * \param xlen       signature length (in bytes).
 * \param hash_oid   encoded hash algorithm OID (or `NULL`).
 * \param hash_len   expected hash value length (in bytes).
 * \param pk         RSA public key.
 * \param hash_out   output buffer for the hash value.
 * \return  1 on success, 0 on error.
 */
typedef uint32_t (*br_rsa_pkcs1_vrfy)(const unsigned char *x, size_t xlen,
	const unsigned char *hash_oid, size_t hash_len,
	const br_rsa_public_key *pk, unsigned char *hash_out);

/**
 * \brief Type for a RSA signature verification engine (PSS).
 *
 * Parameters are:
 *
 *   - The signature itself. The provided array is NOT modified.
 *
 *   - The hash function which was used to hash the message.
 *
 *   - The hash function to use with MGF1 within the PSS padding. This
 *     is not necessarily the same hash function as the one which was
 *     used to hash the signed message.
 *
 *   - The hashed message (as an array of bytes).
 *
 *   - The PSS salt length (in bytes).
 *
 *   - The public key.
 *
 * **Constraints:**
 *
 *   - Hash message length MUST be no more than 64 bytes.
 *
 * Note that, contrary to PKCS#1 v1.5 signature, the hash value of the
 * signed data cannot be extracted from the signature; it must be
 * provided to the verification function.
 *
 * This function verifies that the signature length (`xlen`) matches the
 * modulus length (this function returns 0 on mismatch). If the modulus
 * size exceeds the maximum supported RSA size, then the function also
 * returns 0.
 *
 * Returned value is 1 on success, 0 on error.
 *
 * Implementations of this type need not be constant-time.
 *
 * \param x          signature buffer.
 * \param xlen       signature length (in bytes).
 * \param hf_data    hash function applied on the message.
 * \param hf_mgf1    hash function to use with MGF1.
 * \param hash       hash value of the signed message.
 * \param salt_len   PSS salt length (in bytes).
 * \param pk         RSA public key.
 * \return  1 on success, 0 on error.
 */
typedef uint32_t (*br_rsa_pss_vrfy)(const unsigned char *x, size_t xlen,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1, 
	const void *hash, size_t salt_len, const br_rsa_public_key *pk);

/**
 * \brief Type for a RSA encryption engine (OAEP).
 *
 * Parameters are:
 *
 *   - A source of random bytes. The source must be already initialized.
 *
 *   - A hash function, used internally with the mask generation function
 *     (MGF1).
 *
 *   - A label. The `label` pointer may be `NULL` if `label_len` is zero
 *     (an empty label, which is the default in PKCS#1 v2.2).
 *
 *   - The public key.
 *
 *   - The destination buffer. Its maximum length (in bytes) is provided;
 *     if that length is lower than the public key length, then an error
 *     is reported.
 *
 *   - The source message.
 *
 * The encrypted message output has exactly the same length as the modulus
 * (mathematical length, in bytes, not counting extra leading zeros in the
 * modulus representation in the public key).
 *
 * The source message (`src`, length `src_len`) may overlap with the
 * destination buffer (`dst`, length `dst_max_len`).
 *
 * This function returns the actual encrypted message length, in bytes;
 * on error, zero is returned. An error is reported if the output buffer
 * is not large enough, or the public is invalid, or the public key
 * modulus exceeds the maximum supported RSA size.
 *
 * \param rnd           source of random bytes.
 * \param dig           hash function to use with MGF1.
 * \param label         label value (may be `NULL` if `label_len` is zero).
 * \param label_len     label length, in bytes.
 * \param pk            RSA public key.
 * \param dst           destination buffer.
 * \param dst_max_len   destination buffer length (maximum encrypted data size).
 * \param src           message to encrypt.
 * \param src_len       source message length (in bytes).
 * \return  encrypted message length (in bytes), or 0 on error.
 */
typedef size_t (*br_rsa_oaep_encrypt)(
	const br_prng_class **rnd, const br_hash_class *dig,
	const void *label, size_t label_len,
	const br_rsa_public_key *pk,
	void *dst, size_t dst_max_len,
	const void *src, size_t src_len);

/**
 * \brief Type for a RSA private key engine.
 *
 * The `x[]` buffer is modified in place, and its length is inferred from
 * the modulus length (`x[]` is assumed to have a length of
 * `(sk->n_bitlen+7)/8` bytes).
 *
 * Returned value is 1 on success, 0 on error.
 *
 * \param x    operand to exponentiate.
 * \param sk   RSA private key.
 * \return  1 on success, 0 on error.
 */
typedef uint32_t (*br_rsa_private)(unsigned char *x,
	const br_rsa_private_key *sk);

/**
 * \brief Type for a RSA signature generation engine (PKCS#1 v1.5).
 *
 * Parameters are:
 *
 *   - The encoded OID for the hash function. The provided array must begin
 *     with a single byte that contains the length of the OID value (in
 *     bytes), followed by exactly that many bytes. This parameter may
 *     also be `NULL`, in which case the raw hash value should be used
 *     with the PKCS#1 v1.5 "type 1" padding (as used in SSL/TLS up
 *     to TLS-1.1, with a 36-byte hash value).
 *
 *   - The hash value computes over the data to sign (its length is
 *     expressed in bytes).
 *
 *   - The RSA private key.
 *
 *   - The output buffer, that receives the signature.
 *
 * Returned value is 1 on success, 0 on error. Error conditions include
 * a too small modulus for the provided hash OID and value, or some
 * invalid key parameters. The signature length is exactly
 * `(sk->n_bitlen+7)/8` bytes.
 *
 * This function is expected to be constant-time with regards to the
 * private key bytes (lengths of the modulus and the individual factors
 * may leak, though) and to the hashed data.
 *
 * \param hash_oid   encoded hash algorithm OID (or `NULL`).
 * \param hash       hash value.
 * \param hash_len   hash value length (in bytes).
 * \param sk         RSA private key.
 * \param x          output buffer for the signature value.
 * \return  1 on success, 0 on error.
 */
typedef uint32_t (*br_rsa_pkcs1_sign)(const unsigned char *hash_oid,
	const unsigned char *hash, size_t hash_len,
	const br_rsa_private_key *sk, unsigned char *x);

/**
 * \brief Type for a RSA signature generation engine (PSS).
 *
 * Parameters are:
 *
 *   - An initialized PRNG for salt generation. If the salt length is
 *     zero (`salt_len` parameter), then the PRNG is optional (this is
 *     not the typical case, as the security proof of RSA/PSS is
 *     tighter when a non-empty salt is used).
 *
 *   - The hash function which was used to hash the message.
 *
 *   - The hash function to use with MGF1 within the PSS padding. This
 *     is not necessarily the same function as the one used to hash the
 *     message.
 *
 *   - The hashed message.
 *
 *   - The salt length, in bytes.
 *
 *   - The RSA private key.
 *
 *   - The output buffer, that receives the signature.
 *
 * Returned value is 1 on success, 0 on error. Error conditions include
 * a too small modulus for the provided hash and salt lengths, or some
 * invalid key parameters. The signature length is exactly
 * `(sk->n_bitlen+7)/8` bytes.
 *
 * This function is expected to be constant-time with regards to the
 * private key bytes (lengths of the modulus and the individual factors
 * may leak, though) and to the hashed data.
 *
 * \param rng        PRNG for salt generation (`NULL` if `salt_len` is zero).
 * \param hf_data    hash function used to hash the signed data.
 * \param hf_mgf1    hash function to use with MGF1.
 * \param hash       hashed message.
 * \param salt_len   salt length (in bytes).
 * \param sk         RSA private key.
 * \param x          output buffer for the signature value.
 * \return  1 on success, 0 on error.
 */
typedef uint32_t (*br_rsa_pss_sign)(const br_prng_class **rng,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1,
	const unsigned char *hash_value, size_t salt_len,
	const br_rsa_private_key *sk, unsigned char *x);

/**
 * \brief Encoded OID for SHA-1 (in RSA PKCS#1 signatures).
 */
#define BR_HASH_OID_SHA1     \
	((const unsigned char *)"\x05\x2B\x0E\x03\x02\x1A")

/**
 * \brief Encoded OID for SHA-224 (in RSA PKCS#1 signatures).
 */
#define BR_HASH_OID_SHA224   \
	((const unsigned char *)"\x09\x60\x86\x48\x01\x65\x03\x04\x02\x04")

/**
 * \brief Encoded OID for SHA-256 (in RSA PKCS#1 signatures).
 */
#define BR_HASH_OID_SHA256   \
	((const unsigned char *)"\x09\x60\x86\x48\x01\x65\x03\x04\x02\x01")

/**
 * \brief Encoded OID for SHA-384 (in RSA PKCS#1 signatures).
 */
#define BR_HASH_OID_SHA384   \
	((const unsigned char *)"\x09\x60\x86\x48\x01\x65\x03\x04\x02\x02")

/**
 * \brief Encoded OID for SHA-512 (in RSA PKCS#1 signatures).
 */
#define BR_HASH_OID_SHA512   \
	((const unsigned char *)"\x09\x60\x86\x48\x01\x65\x03\x04\x02\x03")

/**
 * \brief Type for a RSA decryption engine (OAEP).
 *
 * Parameters are:
 *
 *   - A hash function, used internally with the mask generation function
 *     (MGF1).
 *
 *   - A label. The `label` pointer may be `NULL` if `label_len` is zero
 *     (an empty label, which is the default in PKCS#1 v2.2).
 *
 *   - The private key.
 *
 *   - The source and destination buffer. The buffer initially contains
 *     the encrypted message; the buffer contents are altered, and the
 *     decrypted message is written at the start of that buffer
 *     (decrypted message is always shorter than the encrypted message).
 *
 * If decryption fails in any way, then `*len` is unmodified, and the
 * function returns 0. Otherwise, `*len` is set to the decrypted message
 * length, and 1 is returned. The implementation is responsible for
 * checking that the input message length matches the key modulus length,
 * and that the padding is correct.
 *
 * Implementations MUST use constant-time check of the validity of the
 * OAEP padding, at least until the leading byte and hash value have
 * been checked. Whether overall decryption worked, and the length of
 * the decrypted message, may leak.
 *
 * \param dig         hash function to use with MGF1.
 * \param label       label value (may be `NULL` if `label_len` is zero).
 * \param label_len   label length, in bytes.
 * \param sk          RSA private key.
 * \param data        input/output buffer.
 * \param len         encrypted/decrypted message length.
 * \return  1 on success, 0 on error.
 */
typedef uint32_t (*br_rsa_oaep_decrypt)(
	const br_hash_class *dig, const void *label, size_t label_len,
	const br_rsa_private_key *sk, void *data, size_t *len);

/*
 * RSA "i32" engine. Integers are internally represented as arrays of
 * 32-bit integers, and the core multiplication primitive is the
 * 32x32->64 multiplication.
 */

/**
 * \brief RSA public key engine "i32".
 *
 * \see br_rsa_public
 *
 * \param x      operand to exponentiate.
 * \param xlen   length of the operand (in bytes).
 * \param pk     RSA public key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i32_public(unsigned char *x, size_t xlen,
	const br_rsa_public_key *pk);

/**
 * \brief RSA signature verification engine "i32" (PKCS#1 v1.5 signatures).
 *
 * \see br_rsa_pkcs1_vrfy
 *
 * \param x          signature buffer.
 * \param xlen       signature length (in bytes).
 * \param hash_oid   encoded hash algorithm OID (or `NULL`).
 * \param hash_len   expected hash value length (in bytes).
 * \param pk         RSA public key.
 * \param hash_out   output buffer for the hash value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i32_pkcs1_vrfy(const unsigned char *x, size_t xlen,
	const unsigned char *hash_oid, size_t hash_len,
	const br_rsa_public_key *pk, unsigned char *hash_out);

/**
 * \brief RSA signature verification engine "i32" (PSS signatures).
 *
 * \see br_rsa_pss_vrfy
 *
 * \param x          signature buffer.
 * \param xlen       signature length (in bytes).
 * \param hf_data    hash function applied on the message.
 * \param hf_mgf1    hash function to use with MGF1.
 * \param hash       hash value of the signed message.
 * \param salt_len   PSS salt length (in bytes).
 * \param pk         RSA public key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i32_pss_vrfy(const unsigned char *x, size_t xlen,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1, 
	const void *hash, size_t salt_len, const br_rsa_public_key *pk);

/**
 * \brief RSA private key engine "i32".
 *
 * \see br_rsa_private
 *
 * \param x    operand to exponentiate.
 * \param sk   RSA private key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i32_private(unsigned char *x,
	const br_rsa_private_key *sk);

/**
 * \brief RSA signature generation engine "i32" (PKCS#1 v1.5 signatures).
 *
 * \see br_rsa_pkcs1_sign
 *
 * \param hash_oid   encoded hash algorithm OID (or `NULL`).
 * \param hash       hash value.
 * \param hash_len   hash value length (in bytes).
 * \param sk         RSA private key.
 * \param x          output buffer for the hash value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i32_pkcs1_sign(const unsigned char *hash_oid,
	const unsigned char *hash, size_t hash_len,
	const br_rsa_private_key *sk, unsigned char *x);

/**
 * \brief RSA signature generation engine "i32" (PSS signatures).
 *
 * \see br_rsa_pss_sign
 *
 * \param rng        PRNG for salt generation (`NULL` if `salt_len` is zero).
 * \param hf_data    hash function used to hash the signed data.
 * \param hf_mgf1    hash function to use with MGF1.
 * \param hash       hashed message.
 * \param salt_len   salt length (in bytes).
 * \param sk         RSA private key.
 * \param x          output buffer for the signature value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i32_pss_sign(const br_prng_class **rng,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1,
	const unsigned char *hash_value, size_t salt_len,
	const br_rsa_private_key *sk, unsigned char *x);

/*
 * RSA "i31" engine. Similar to i32, but only 31 bits are used per 32-bit
 * word. This uses slightly more stack space (about 4% more) and code
 * space, but it quite faster.
 */

/**
 * \brief RSA public key engine "i31".
 *
 * \see br_rsa_public
 *
 * \param x      operand to exponentiate.
 * \param xlen   length of the operand (in bytes).
 * \param pk     RSA public key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i31_public(unsigned char *x, size_t xlen,
	const br_rsa_public_key *pk);

/**
 * \brief RSA signature verification engine "i31" (PKCS#1 v1.5 signatures).
 *
 * \see br_rsa_pkcs1_vrfy
 *
 * \param x          signature buffer.
 * \param xlen       signature length (in bytes).
 * \param hash_oid   encoded hash algorithm OID (or `NULL`).
 * \param hash_len   expected hash value length (in bytes).
 * \param pk         RSA public key.
 * \param hash_out   output buffer for the hash value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i31_pkcs1_vrfy(const unsigned char *x, size_t xlen,
	const unsigned char *hash_oid, size_t hash_len,
	const br_rsa_public_key *pk, unsigned char *hash_out);

/**
 * \brief RSA signature verification engine "i31" (PSS signatures).
 *
 * \see br_rsa_pss_vrfy
 *
 * \param x          signature buffer.
 * \param xlen       signature length (in bytes).
 * \param hf_data    hash function applied on the message.
 * \param hf_mgf1    hash function to use with MGF1.
 * \param hash       hash value of the signed message.
 * \param salt_len   PSS salt length (in bytes).
 * \param pk         RSA public key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i31_pss_vrfy(const unsigned char *x, size_t xlen,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1, 
	const void *hash, size_t salt_len, const br_rsa_public_key *pk);

/**
 * \brief RSA private key engine "i31".
 *
 * \see br_rsa_private
 *
 * \param x    operand to exponentiate.
 * \param sk   RSA private key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i31_private(unsigned char *x,
	const br_rsa_private_key *sk);

/**
 * \brief RSA signature generation engine "i31" (PKCS#1 v1.5 signatures).
 *
 * \see br_rsa_pkcs1_sign
 *
 * \param hash_oid   encoded hash algorithm OID (or `NULL`).
 * \param hash       hash value.
 * \param hash_len   hash value length (in bytes).
 * \param sk         RSA private key.
 * \param x          output buffer for the hash value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i31_pkcs1_sign(const unsigned char *hash_oid,
	const unsigned char *hash, size_t hash_len,
	const br_rsa_private_key *sk, unsigned char *x);

/**
 * \brief RSA signature generation engine "i31" (PSS signatures).
 *
 * \see br_rsa_pss_sign
 *
 * \param rng        PRNG for salt generation (`NULL` if `salt_len` is zero).
 * \param hf_data    hash function used to hash the signed data.
 * \param hf_mgf1    hash function to use with MGF1.
 * \param hash       hashed message.
 * \param salt_len   salt length (in bytes).
 * \param sk         RSA private key.
 * \param x          output buffer for the signature value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i31_pss_sign(const br_prng_class **rng,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1,
	const unsigned char *hash_value, size_t salt_len,
	const br_rsa_private_key *sk, unsigned char *x);

/*
 * RSA "i62" engine. Similar to i31, but internal multiplication use
 * 64x64->128 multiplications. This is available only on architecture
 * that offer such an opcode.
 */

/**
 * \brief RSA public key engine "i62".
 *
 * This function is defined only on architecture that offer a 64x64->128
 * opcode. Use `br_rsa_i62_public_get()` to dynamically obtain a pointer
 * to that function.
 *
 * \see br_rsa_public
 *
 * \param x      operand to exponentiate.
 * \param xlen   length of the operand (in bytes).
 * \param pk     RSA public key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i62_public(unsigned char *x, size_t xlen,
	const br_rsa_public_key *pk);

/**
 * \brief RSA signature verification engine "i62" (PKCS#1 v1.5 signatures).
 *
 * This function is defined only on architecture that offer a 64x64->128
 * opcode. Use `br_rsa_i62_pkcs1_vrfy_get()` to dynamically obtain a pointer
 * to that function.
 *
 * \see br_rsa_pkcs1_vrfy
 *
 * \param x          signature buffer.
 * \param xlen       signature length (in bytes).
 * \param hash_oid   encoded hash algorithm OID (or `NULL`).
 * \param hash_len   expected hash value length (in bytes).
 * \param pk         RSA public key.
 * \param hash_out   output buffer for the hash value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i62_pkcs1_vrfy(const unsigned char *x, size_t xlen,
	const unsigned char *hash_oid, size_t hash_len,
	const br_rsa_public_key *pk, unsigned char *hash_out);

/**
 * \brief RSA signature verification engine "i62" (PSS signatures).
 *
 * This function is defined only on architecture that offer a 64x64->128
 * opcode. Use `br_rsa_i62_pss_vrfy_get()` to dynamically obtain a pointer
 * to that function.
 *
 * \see br_rsa_pss_vrfy
 *
 * \param x          signature buffer.
 * \param xlen       signature length (in bytes).
 * \param hf_data    hash function applied on the message.
 * \param hf_mgf1    hash function to use with MGF1.
 * \param hash       hash value of the signed message.
 * \param salt_len   PSS salt length (in bytes).
 * \param pk         RSA public key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i62_pss_vrfy(const unsigned char *x, size_t xlen,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1, 
	const void *hash, size_t salt_len, const br_rsa_public_key *pk);

/**
 * \brief RSA private key engine "i62".
 *
 * This function is defined only on architecture that offer a 64x64->128
 * opcode. Use `br_rsa_i62_private_get()` to dynamically obtain a pointer
 * to that function.
 *
 * \see br_rsa_private
 *
 * \param x    operand to exponentiate.
 * \param sk   RSA private key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i62_private(unsigned char *x,
	const br_rsa_private_key *sk);

/**
 * \brief RSA signature generation engine "i62" (PKCS#1 v1.5 signatures).
 *
 * This function is defined only on architecture that offer a 64x64->128
 * opcode. Use `br_rsa_i62_pkcs1_sign_get()` to dynamically obtain a pointer
 * to that function.
 *
 * \see br_rsa_pkcs1_sign
 *
 * \param hash_oid   encoded hash algorithm OID (or `NULL`).
 * \param hash       hash value.
 * \param hash_len   hash value length (in bytes).
 * \param sk         RSA private key.
 * \param x          output buffer for the hash value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i62_pkcs1_sign(const unsigned char *hash_oid,
	const unsigned char *hash, size_t hash_len,
	const br_rsa_private_key *sk, unsigned char *x);

/**
 * \brief RSA signature generation engine "i62" (PSS signatures).
 *
 * This function is defined only on architecture that offer a 64x64->128
 * opcode. Use `br_rsa_i62_pss_sign_get()` to dynamically obtain a pointer
 * to that function.
 *
 * \see br_rsa_pss_sign
 *
 * \param rng        PRNG for salt generation (`NULL` if `salt_len` is zero).
 * \param hf_data    hash function used to hash the signed data.
 * \param hf_mgf1    hash function to use with MGF1.
 * \param hash       hashed message.
 * \param salt_len   salt length (in bytes).
 * \param sk         RSA private key.
 * \param x          output buffer for the signature value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i62_pss_sign(const br_prng_class **rng,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1,
	const unsigned char *hash_value, size_t salt_len,
	const br_rsa_private_key *sk, unsigned char *x);

/**
 * \brief Get the RSA "i62" implementation (public key operations),
 * if available.
 *
 * \return  the implementation, or 0.
 */
br_rsa_public br_rsa_i62_public_get(void);

/**
 * \brief Get the RSA "i62" implementation (PKCS#1 v1.5 signature verification),
 * if available.
 *
 * \return  the implementation, or 0.
 */
br_rsa_pkcs1_vrfy br_rsa_i62_pkcs1_vrfy_get(void);

/**
 * \brief Get the RSA "i62" implementation (PSS signature verification),
 * if available.
 *
 * \return  the implementation, or 0.
 */
br_rsa_pss_vrfy br_rsa_i62_pss_vrfy_get(void);

/**
 * \brief Get the RSA "i62" implementation (private key operations),
 * if available.
 *
 * \return  the implementation, or 0.
 */
br_rsa_private br_rsa_i62_private_get(void);

/**
 * \brief Get the RSA "i62" implementation (PKCS#1 v1.5 signature generation),
 * if available.
 *
 * \return  the implementation, or 0.
 */
br_rsa_pkcs1_sign br_rsa_i62_pkcs1_sign_get(void);

/**
 * \brief Get the RSA "i62" implementation (PSS signature generation),
 * if available.
 *
 * \return  the implementation, or 0.
 */
br_rsa_pss_sign br_rsa_i62_pss_sign_get(void);

/**
 * \brief Get the RSA "i62" implementation (OAEP encryption),
 * if available.
 *
 * \return  the implementation, or 0.
 */
br_rsa_oaep_encrypt br_rsa_i62_oaep_encrypt_get(void);

/**
 * \brief Get the RSA "i62" implementation (OAEP decryption),
 * if available.
 *
 * \return  the implementation, or 0.
 */
br_rsa_oaep_decrypt br_rsa_i62_oaep_decrypt_get(void);

/*
 * RSA "i15" engine. Integers are represented as 15-bit integers, so
 * the code uses only 32-bit multiplication (no 64-bit result), which
 * is vastly faster (and constant-time) on the ARM Cortex M0/M0+.
 */

/**
 * \brief RSA public key engine "i15".
 *
 * \see br_rsa_public
 *
 * \param x      operand to exponentiate.
 * \param xlen   length of the operand (in bytes).
 * \param pk     RSA public key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i15_public(unsigned char *x, size_t xlen,
	const br_rsa_public_key *pk);

/**
 * \brief RSA signature verification engine "i15" (PKCS#1 v1.5 signatures).
 *
 * \see br_rsa_pkcs1_vrfy
 *
 * \param x          signature buffer.
 * \param xlen       signature length (in bytes).
 * \param hash_oid   encoded hash algorithm OID (or `NULL`).
 * \param hash_len   expected hash value length (in bytes).
 * \param pk         RSA public key.
 * \param hash_out   output buffer for the hash value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i15_pkcs1_vrfy(const unsigned char *x, size_t xlen,
	const unsigned char *hash_oid, size_t hash_len,
	const br_rsa_public_key *pk, unsigned char *hash_out);

/**
 * \brief RSA signature verification engine "i15" (PSS signatures).
 *
 * \see br_rsa_pss_vrfy
 *
 * \param x          signature buffer.
 * \param xlen       signature length (in bytes).
 * \param hf_data    hash function applied on the message.
 * \param hf_mgf1    hash function to use with MGF1.
 * \param hash       hash value of the signed message.
 * \param salt_len   PSS salt length (in bytes).
 * \param pk         RSA public key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i15_pss_vrfy(const unsigned char *x, size_t xlen,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1, 
	const void *hash, size_t salt_len, const br_rsa_public_key *pk);

/**
 * \brief RSA private key engine "i15".
 *
 * \see br_rsa_private
 *
 * \param x    operand to exponentiate.
 * \param sk   RSA private key.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i15_private(unsigned char *x,
	const br_rsa_private_key *sk);

/**
 * \brief RSA signature generation engine "i15" (PKCS#1 v1.5 signatures).
 *
 * \see br_rsa_pkcs1_sign
 *
 * \param hash_oid   encoded hash algorithm OID (or `NULL`).
 * \param hash       hash value.
 * \param hash_len   hash value length (in bytes).
 * \param sk         RSA private key.
 * \param x          output buffer for the hash value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i15_pkcs1_sign(const unsigned char *hash_oid,
	const unsigned char *hash, size_t hash_len,
	const br_rsa_private_key *sk, unsigned char *x);

/**
 * \brief RSA signature generation engine "i15" (PSS signatures).
 *
 * \see br_rsa_pss_sign
 *
 * \param rng        PRNG for salt generation (`NULL` if `salt_len` is zero).
 * \param hf_data    hash function used to hash the signed data.
 * \param hf_mgf1    hash function to use with MGF1.
 * \param hash       hashed message.
 * \param salt_len   salt length (in bytes).
 * \param sk         RSA private key.
 * \param x          output buffer for the signature value.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i15_pss_sign(const br_prng_class **rng,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1,
	const unsigned char *hash_value, size_t salt_len,
	const br_rsa_private_key *sk, unsigned char *x);

/**
 * \brief Get "default" RSA implementation (public-key operations).
 *
 * This returns the preferred implementation of RSA (public-key operations)
 * on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_public br_rsa_public_get_default(void);

/**
 * \brief Get "default" RSA implementation (private-key operations).
 *
 * This returns the preferred implementation of RSA (private-key operations)
 * on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_private br_rsa_private_get_default(void);

/**
 * \brief Get "default" RSA implementation (PKCS#1 v1.5 signature verification).
 *
 * This returns the preferred implementation of RSA (signature verification)
 * on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_pkcs1_vrfy br_rsa_pkcs1_vrfy_get_default(void);

/**
 * \brief Get "default" RSA implementation (PSS signature verification).
 *
 * This returns the preferred implementation of RSA (signature verification)
 * on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_pss_vrfy br_rsa_pss_vrfy_get_default(void);

/**
 * \brief Get "default" RSA implementation (PKCS#1 v1.5 signature generation).
 *
 * This returns the preferred implementation of RSA (signature generation)
 * on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_pkcs1_sign br_rsa_pkcs1_sign_get_default(void);

/**
 * \brief Get "default" RSA implementation (PSS signature generation).
 *
 * This returns the preferred implementation of RSA (signature generation)
 * on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_pss_sign br_rsa_pss_sign_get_default(void);

/**
 * \brief Get "default" RSA implementation (OAEP encryption).
 *
 * This returns the preferred implementation of RSA (OAEP encryption)
 * on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_oaep_encrypt br_rsa_oaep_encrypt_get_default(void);

/**
 * \brief Get "default" RSA implementation (OAEP decryption).
 *
 * This returns the preferred implementation of RSA (OAEP decryption)
 * on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_oaep_decrypt br_rsa_oaep_decrypt_get_default(void);

/**
 * \brief RSA decryption helper, for SSL/TLS.
 *
 * This function performs the RSA decryption for a RSA-based key exchange
 * in a SSL/TLS server. The provided RSA engine is used. The `data`
 * parameter points to the value to decrypt, of length `len` bytes. On
 * success, the 48-byte pre-master secret is copied into `data`, starting
 * at the first byte of that buffer; on error, the contents of `data`
 * become indeterminate.
 *
 * This function first checks that the provided value length (`len`) is
 * not lower than 59 bytes, and matches the RSA modulus length; if neither
 * of this property is met, then this function returns 0 and the buffer
 * is unmodified.
 *
 * Otherwise, decryption and then padding verification are performed, both
 * in constant-time. A decryption error, or a bad padding, or an
 * incorrect decrypted value length are reported with a returned value of
 * 0; on success, 1 is returned. The caller (SSL server engine) is supposed
 * to proceed with a random pre-master secret in case of error.
 *
 * \param core   RSA private key engine.
 * \param sk     RSA private key.
 * \param data   input/output buffer.
 * \param len    length (in bytes) of the data to decrypt.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_ssl_decrypt(br_rsa_private core, const br_rsa_private_key *sk,
	unsigned char *data, size_t len);

/**
 * \brief RSA encryption (OAEP) with the "i15" engine.
 *
 * \see br_rsa_oaep_encrypt
 *
 * \param rnd           source of random bytes.
 * \param dig           hash function to use with MGF1.
 * \param label         label value (may be `NULL` if `label_len` is zero).
 * \param label_len     label length, in bytes.
 * \param pk            RSA public key.
 * \param dst           destination buffer.
 * \param dst_max_len   destination buffer length (maximum encrypted data size).
 * \param src           message to encrypt.
 * \param src_len       source message length (in bytes).
 * \return  encrypted message length (in bytes), or 0 on error.
 */
size_t br_rsa_i15_oaep_encrypt(
	const br_prng_class **rnd, const br_hash_class *dig,
	const void *label, size_t label_len,
	const br_rsa_public_key *pk,
	void *dst, size_t dst_max_len,
	const void *src, size_t src_len);

/**
 * \brief RSA decryption (OAEP) with the "i15" engine.
 *
 * \see br_rsa_oaep_decrypt
 *
 * \param dig         hash function to use with MGF1.
 * \param label       label value (may be `NULL` if `label_len` is zero).
 * \param label_len   label length, in bytes.
 * \param sk          RSA private key.
 * \param data        input/output buffer.
 * \param len         encrypted/decrypted message length.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i15_oaep_decrypt(
	const br_hash_class *dig, const void *label, size_t label_len,
	const br_rsa_private_key *sk, void *data, size_t *len);

/**
 * \brief RSA encryption (OAEP) with the "i31" engine.
 *
 * \see br_rsa_oaep_encrypt
 *
 * \param rnd           source of random bytes.
 * \param dig           hash function to use with MGF1.
 * \param label         label value (may be `NULL` if `label_len` is zero).
 * \param label_len     label length, in bytes.
 * \param pk            RSA public key.
 * \param dst           destination buffer.
 * \param dst_max_len   destination buffer length (maximum encrypted data size).
 * \param src           message to encrypt.
 * \param src_len       source message length (in bytes).
 * \return  encrypted message length (in bytes), or 0 on error.
 */
size_t br_rsa_i31_oaep_encrypt(
	const br_prng_class **rnd, const br_hash_class *dig,
	const void *label, size_t label_len,
	const br_rsa_public_key *pk,
	void *dst, size_t dst_max_len,
	const void *src, size_t src_len);

/**
 * \brief RSA decryption (OAEP) with the "i31" engine.
 *
 * \see br_rsa_oaep_decrypt
 *
 * \param dig         hash function to use with MGF1.
 * \param label       label value (may be `NULL` if `label_len` is zero).
 * \param label_len   label length, in bytes.
 * \param sk          RSA private key.
 * \param data        input/output buffer.
 * \param len         encrypted/decrypted message length.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i31_oaep_decrypt(
	const br_hash_class *dig, const void *label, size_t label_len,
	const br_rsa_private_key *sk, void *data, size_t *len);

/**
 * \brief RSA encryption (OAEP) with the "i32" engine.
 *
 * \see br_rsa_oaep_encrypt
 *
 * \param rnd           source of random bytes.
 * \param dig           hash function to use with MGF1.
 * \param label         label value (may be `NULL` if `label_len` is zero).
 * \param label_len     label length, in bytes.
 * \param pk            RSA public key.
 * \param dst           destination buffer.
 * \param dst_max_len   destination buffer length (maximum encrypted data size).
 * \param src           message to encrypt.
 * \param src_len       source message length (in bytes).
 * \return  encrypted message length (in bytes), or 0 on error.
 */
size_t br_rsa_i32_oaep_encrypt(
	const br_prng_class **rnd, const br_hash_class *dig,
	const void *label, size_t label_len,
	const br_rsa_public_key *pk,
	void *dst, size_t dst_max_len,
	const void *src, size_t src_len);

/**
 * \brief RSA decryption (OAEP) with the "i32" engine.
 *
 * \see br_rsa_oaep_decrypt
 *
 * \param dig         hash function to use with MGF1.
 * \param label       label value (may be `NULL` if `label_len` is zero).
 * \param label_len   label length, in bytes.
 * \param sk          RSA private key.
 * \param data        input/output buffer.
 * \param len         encrypted/decrypted message length.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i32_oaep_decrypt(
	const br_hash_class *dig, const void *label, size_t label_len,
	const br_rsa_private_key *sk, void *data, size_t *len);

/**
 * \brief RSA encryption (OAEP) with the "i62" engine.
 *
 * This function is defined only on architecture that offer a 64x64->128
 * opcode. Use `br_rsa_i62_oaep_encrypt_get()` to dynamically obtain a pointer
 * to that function.
 *
 * \see br_rsa_oaep_encrypt
 *
 * \param rnd           source of random bytes.
 * \param dig           hash function to use with MGF1.
 * \param label         label value (may be `NULL` if `label_len` is zero).
 * \param label_len     label length, in bytes.
 * \param pk            RSA public key.
 * \param dst           destination buffer.
 * \param dst_max_len   destination buffer length (maximum encrypted data size).
 * \param src           message to encrypt.
 * \param src_len       source message length (in bytes).
 * \return  encrypted message length (in bytes), or 0 on error.
 */
size_t br_rsa_i62_oaep_encrypt(
	const br_prng_class **rnd, const br_hash_class *dig,
	const void *label, size_t label_len,
	const br_rsa_public_key *pk,
	void *dst, size_t dst_max_len,
	const void *src, size_t src_len);

/**
 * \brief RSA decryption (OAEP) with the "i62" engine.
 *
 * This function is defined only on architecture that offer a 64x64->128
 * opcode. Use `br_rsa_i62_oaep_decrypt_get()` to dynamically obtain a pointer
 * to that function.
 *
 * \see br_rsa_oaep_decrypt
 *
 * \param dig         hash function to use with MGF1.
 * \param label       label value (may be `NULL` if `label_len` is zero).
 * \param label_len   label length, in bytes.
 * \param sk          RSA private key.
 * \param data        input/output buffer.
 * \param len         encrypted/decrypted message length.
 * \return  1 on success, 0 on error.
 */
uint32_t br_rsa_i62_oaep_decrypt(
	const br_hash_class *dig, const void *label, size_t label_len,
	const br_rsa_private_key *sk, void *data, size_t *len);

/**
 * \brief Get buffer size to hold RSA private key elements.
 *
 * This macro returns the length (in bytes) of the buffer needed to
 * receive the elements of a RSA private key, as generated by one of
 * the `br_rsa_*_keygen()` functions. If the provided size is a constant
 * expression, then the whole macro evaluates to a constant expression.
 *
 * \param size   target key size (modulus size, in bits)
 * \return  the length of the private key buffer, in bytes.
 */
#define BR_RSA_KBUF_PRIV_SIZE(size)    (5 * (((size) + 15) >> 4))

/**
 * \brief Get buffer size to hold RSA public key elements.
 *
 * This macro returns the length (in bytes) of the buffer needed to
 * receive the elements of a RSA public key, as generated by one of
 * the `br_rsa_*_keygen()` functions. If the provided size is a constant
 * expression, then the whole macro evaluates to a constant expression.
 *
 * \param size   target key size (modulus size, in bits)
 * \return  the length of the public key buffer, in bytes.
 */
#define BR_RSA_KBUF_PUB_SIZE(size)     (4 + (((size) + 7) >> 3))

/**
 * \brief Type for RSA key pair generator implementation.
 *
 * This function generates a new RSA key pair whose modulus has bit
 * length `size` bits. The private key elements are written in the
 * `kbuf_priv` buffer, and pointer values and length fields to these
 * elements are populated in the provided private key structure `sk`.
 * Similarly, the public key elements are written in `kbuf_pub`, with
 * pointers and lengths set in `pk`.
 *
 * If `pk` is `NULL`, then `kbuf_pub` may be `NULL`, and only the
 * private key is set.
 *
 * If `pubexp` is not zero, then its value will be used as public
 * exponent. Valid RSA public exponent values are odd integers
 * greater than 1. If `pubexp` is zero, then the public exponent will
 * have value 3.
 *
 * The provided PRNG (`rng_ctx`) must have already been initialized
 * and seeded.
 *
 * Returned value is 1 on success, 0 on error. An error is reported
 * if the requested range is outside of the supported key sizes, or
 * if an invalid non-zero public exponent value is provided. Supported
 * range starts at 512 bits, and up to an implementation-defined
 * maximum (by default 4096 bits). Note that key sizes up to 768 bits
 * have been broken in practice, and sizes lower than 2048 bits are
 * usually considered to be weak and should not be used.
 *
 * \param rng_ctx     source PRNG context (already initialized)
 * \param sk          RSA private key structure (destination)
 * \param kbuf_priv   buffer for private key elements
 * \param pk          RSA public key structure (destination), or `NULL`
 * \param kbuf_pub    buffer for public key elements, or `NULL`
 * \param size        target RSA modulus size (in bits)
 * \param pubexp      public exponent to use, or zero
 * \return  1 on success, 0 on error (invalid parameters)
 */
typedef uint32_t (*br_rsa_keygen)(
	const br_prng_class **rng_ctx,
	br_rsa_private_key *sk, void *kbuf_priv,
	br_rsa_public_key *pk, void *kbuf_pub,
	unsigned size, uint32_t pubexp);

/**
 * \brief RSA key pair generation with the "i15" engine.
 *
 * \see br_rsa_keygen
 *
 * \param rng_ctx     source PRNG context (already initialized)
 * \param sk          RSA private key structure (destination)
 * \param kbuf_priv   buffer for private key elements
 * \param pk          RSA public key structure (destination), or `NULL`
 * \param kbuf_pub    buffer for public key elements, or `NULL`
 * \param size        target RSA modulus size (in bits)
 * \param pubexp      public exponent to use, or zero
 * \return  1 on success, 0 on error (invalid parameters)
 */
uint32_t br_rsa_i15_keygen(
	const br_prng_class **rng_ctx,
	br_rsa_private_key *sk, void *kbuf_priv,
	br_rsa_public_key *pk, void *kbuf_pub,
	unsigned size, uint32_t pubexp);

/**
 * \brief RSA key pair generation with the "i31" engine.
 *
 * \see br_rsa_keygen
 *
 * \param rng_ctx     source PRNG context (already initialized)
 * \param sk          RSA private key structure (destination)
 * \param kbuf_priv   buffer for private key elements
 * \param pk          RSA public key structure (destination), or `NULL`
 * \param kbuf_pub    buffer for public key elements, or `NULL`
 * \param size        target RSA modulus size (in bits)
 * \param pubexp      public exponent to use, or zero
 * \return  1 on success, 0 on error (invalid parameters)
 */
uint32_t br_rsa_i31_keygen(
	const br_prng_class **rng_ctx,
	br_rsa_private_key *sk, void *kbuf_priv,
	br_rsa_public_key *pk, void *kbuf_pub,
	unsigned size, uint32_t pubexp);

/**
 * \brief RSA key pair generation with the "i62" engine.
 *
 * This function is defined only on architecture that offer a 64x64->128
 * opcode. Use `br_rsa_i62_keygen_get()` to dynamically obtain a pointer
 * to that function.
 *
 * \see br_rsa_keygen
 *
 * \param rng_ctx     source PRNG context (already initialized)
 * \param sk          RSA private key structure (destination)
 * \param kbuf_priv   buffer for private key elements
 * \param pk          RSA public key structure (destination), or `NULL`
 * \param kbuf_pub    buffer for public key elements, or `NULL`
 * \param size        target RSA modulus size (in bits)
 * \param pubexp      public exponent to use, or zero
 * \return  1 on success, 0 on error (invalid parameters)
 */
uint32_t br_rsa_i62_keygen(
	const br_prng_class **rng_ctx,
	br_rsa_private_key *sk, void *kbuf_priv,
	br_rsa_public_key *pk, void *kbuf_pub,
	unsigned size, uint32_t pubexp);

/**
 * \brief Get the RSA "i62" implementation (key pair generation),
 * if available.
 *
 * \return  the implementation, or 0.
 */
br_rsa_keygen br_rsa_i62_keygen_get(void);

/**
 * \brief Get "default" RSA implementation (key pair generation).
 *
 * This returns the preferred implementation of RSA (key pair generation)
 * on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_keygen br_rsa_keygen_get_default(void);

/**
 * \brief Type for a modulus computing function.
 *
 * Such a function computes the public modulus from the private key. The
 * encoded modulus (unsigned big-endian) is written on `n`, and the size
 * (in bytes) is returned. If `n` is `NULL`, then the size is returned but
 * the modulus itself is not computed.
 *
 * If the key size exceeds an internal limit, 0 is returned.
 *
 * \param n    destination buffer (or `NULL`).
 * \param sk   RSA private key.
 * \return  the modulus length (in bytes), or 0.
 */
typedef size_t (*br_rsa_compute_modulus)(void *n, const br_rsa_private_key *sk);

/**
 * \brief Recompute RSA modulus ("i15" engine).
 *
 * \see br_rsa_compute_modulus
 *
 * \param n    destination buffer (or `NULL`).
 * \param sk   RSA private key.
 * \return  the modulus length (in bytes), or 0.
 */
size_t br_rsa_i15_compute_modulus(void *n, const br_rsa_private_key *sk);

/**
 * \brief Recompute RSA modulus ("i31" engine).
 *
 * \see br_rsa_compute_modulus
 *
 * \param n    destination buffer (or `NULL`).
 * \param sk   RSA private key.
 * \return  the modulus length (in bytes), or 0.
 */
size_t br_rsa_i31_compute_modulus(void *n, const br_rsa_private_key *sk);

/**
 * \brief Get "default" RSA implementation (recompute modulus).
 *
 * This returns the preferred implementation of RSA (recompute modulus)
 * on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_compute_modulus br_rsa_compute_modulus_get_default(void);

/**
 * \brief Type for a public exponent computing function.
 *
 * Such a function recomputes the public exponent from the private key.
 * 0 is returned if any of the following occurs:
 *
 *   - Either `p` or `q` is not equal to 3 modulo 4.
 *
 *   - The public exponent does not fit on 32 bits.
 *
 *   - An internal limit is exceeded.
 *
 *   - The private key is invalid in some way.
 *
 * For all private keys produced by the key generator functions
 * (`br_rsa_keygen` type), this function succeeds and returns the true
 * public exponent. The public exponent is always an odd integer greater
 * than 1.
 *
 * \return  the public exponent, or 0.
 */
typedef uint32_t (*br_rsa_compute_pubexp)(const br_rsa_private_key *sk);

/**
 * \brief Recompute RSA public exponent ("i15" engine).
 *
 * \see br_rsa_compute_pubexp
 *
 * \return  the public exponent, or 0.
 */
uint32_t br_rsa_i15_compute_pubexp(const br_rsa_private_key *sk);

/**
 * \brief Recompute RSA public exponent ("i31" engine).
 *
 * \see br_rsa_compute_pubexp
 *
 * \return  the public exponent, or 0.
 */
uint32_t br_rsa_i31_compute_pubexp(const br_rsa_private_key *sk);

/**
 * \brief Get "default" RSA implementation (recompute public exponent).
 *
 * This returns the preferred implementation of RSA (recompute public
 * exponent) on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_compute_pubexp br_rsa_compute_pubexp_get_default(void);

/**
 * \brief Type for a private exponent computing function.
 *
 * An RSA private key (`br_rsa_private_key`) contains two reduced
 * private exponents, which are sufficient to perform private key
 * operations. However, standard encoding formats for RSA private keys
 * require also a copy of the complete private exponent (non-reduced),
 * which this function recomputes.
 *
 * This function suceeds if all the following conditions hold:
 *
 *   - Both private factors `p` and `q` are equal to 3 modulo 4.
 *
 *   - The provided public exponent `pubexp` is correct, and, in particular,
 *     is odd, relatively prime to `p-1` and `q-1`, and greater than 1.
 *
 *   - No internal storage limit is exceeded.
 *
 * For all private keys produced by the key generator functions
 * (`br_rsa_keygen` type), this function succeeds. Note that the API
 * restricts the public exponent to a maximum size of 32 bits.
 *
 * The encoded private exponent is written in `d` (unsigned big-endian
 * convention), and the length (in bytes) is returned. If `d` is `NULL`,
 * then the exponent is not written anywhere, but the length is still
 * returned. On error, 0 is returned.
 *
 * Not all error conditions are detected when `d` is `NULL`; therefore, the
 * returned value shall be checked also when actually producing the value.
 *
 * \param d        destination buffer (or `NULL`).
 * \param sk       RSA private key.
 * \param pubexp   the public exponent.
 * \return  the private exponent length (in bytes), or 0.
 */
typedef size_t (*br_rsa_compute_privexp)(void *d,
	const br_rsa_private_key *sk, uint32_t pubexp);

/**
 * \brief Recompute RSA private exponent ("i15" engine).
 *
 * \see br_rsa_compute_privexp
 *
 * \param d        destination buffer (or `NULL`).
 * \param sk       RSA private key.
 * \param pubexp   the public exponent.
 * \return  the private exponent length (in bytes), or 0.
 */
size_t br_rsa_i15_compute_privexp(void *d,
	const br_rsa_private_key *sk, uint32_t pubexp);

/**
 * \brief Recompute RSA private exponent ("i31" engine).
 *
 * \see br_rsa_compute_privexp
 *
 * \param d        destination buffer (or `NULL`).
 * \param sk       RSA private key.
 * \param pubexp   the public exponent.
 * \return  the private exponent length (in bytes), or 0.
 */
size_t br_rsa_i31_compute_privexp(void *d,
	const br_rsa_private_key *sk, uint32_t pubexp);

/**
 * \brief Get "default" RSA implementation (recompute private exponent).
 *
 * This returns the preferred implementation of RSA (recompute private
 * exponent) on the current system.
 *
 * \return  the default implementation.
 */
br_rsa_compute_privexp br_rsa_compute_privexp_get_default(void);

#ifdef __cplusplus
}
#endif

#endif
