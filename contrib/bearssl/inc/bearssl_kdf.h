/*
 * Copyright (c) 2018 Thomas Pornin <pornin@bolet.org>
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

#ifndef BR_BEARSSL_KDF_H__
#define BR_BEARSSL_KDF_H__

#include <stddef.h>
#include <stdint.h>

#include "bearssl_hash.h"
#include "bearssl_hmac.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file bearssl_kdf.h
 *
 * # Key Derivation Functions
 *
 * KDF are functions that takes a variable length input, and provide a
 * variable length output, meant to be used to derive subkeys from a
 * master key.
 *
 * ## HKDF
 *
 * HKDF is a KDF defined by [RFC 5869](https://tools.ietf.org/html/rfc5869).
 * It is based on HMAC, itself using an underlying hash function. Any
 * hash function can be used, as long as it is compatible with the rules
 * for the HMAC implementation (i.e. output size is 64 bytes or less, hash
 * internal state size is 64 bytes or less, and the internal block length is
 * a power of 2 between 16 and 256 bytes). HKDF has two phases:
 *
 *  - HKDF-Extract: the input data in ingested, along with a "salt" value.
 *
 *  - HKDF-Expand: the output is produced, from the result of processing
 *    the input and salt, and using an extra non-secret parameter called
 *    "info".
 *
 * The "salt" and "info" strings are non-secret and can be empty. Their role
 * is normally to bind the input and output, respectively, to conventional
 * identifiers that qualifu them within the used protocol or application.
 *
 * The implementation defined in this file uses the following functions:
 *
 *  - `br_hkdf_init()`: initialize an HKDF context, with a hash function,
 *    and the salt. This starts the HKDF-Extract process.
 *
 *  - `br_hkdf_inject()`: inject more input bytes. This function may be
 *    called repeatedly if the input data is provided by chunks.
 *
 *  - `br_hkdf_flip()`: end the HKDF-Extract process, and start the
 *    HKDF-Expand process.
 *
 *  - `br_hkdf_produce()`: get the next bytes of output. This function
 *    may be called several times to obtain the full output by chunks.
 *    For correct HKDF processing, the same "info" string must be
 *    provided for each call.
 *
 * Note that the HKDF total output size (the number of bytes that
 * HKDF-Expand is willing to produce) is limited: if the hash output size
 * is _n_ bytes, then the maximum output size is _255*n_.
 *
 * ## SHAKE
 *
 * SHAKE is defined in
 * [FIPS 202](https://csrc.nist.gov/publications/detail/fips/202/final)
 * under two versions: SHAKE128 and SHAKE256, offering an alleged
 * "security level" of 128 and 256 bits, respectively (SHAKE128 is
 * about 20 to 25% faster than SHAKE256). SHAKE internally relies on
 * the Keccak family of sponge functions, not on any externally provided
 * hash function. Contrary to HKDF, SHAKE does not have a concept of
 * either a "salt" or an "info" string. The API consists in four
 * functions:
 *
 *  - `br_shake_init()`: initialize a SHAKE context for a given
 *    security level.
 *
 *  - `br_shake_inject()`: inject more input bytes. This function may be
 *    called repeatedly if the input data is provided by chunks.
 *
 *  - `br_shake_flip()`: end the data injection process, and start the
 *    data production process.
 *
 *  - `br_shake_produce()`: get the next bytes of output. This function
 *    may be called several times to obtain the full output by chunks.
 */

/**
 * \brief HKDF context.
 *
 * The HKDF context is initialized with a hash function implementation
 * and a salt value. Contents are opaque (callers should not access them
 * directly). The caller is responsible for allocating the context where
 * appropriate. Context initialisation and usage incurs no dynamic
 * allocation, so there is no release function.
 */
typedef struct {
#ifndef BR_DOXYGEN_IGNORE
	union {
		br_hmac_context hmac_ctx;
		br_hmac_key_context prk_ctx;
	} u;
	unsigned char buf[64];
	size_t ptr;
	size_t dig_len;
	unsigned chunk_num;
#endif
} br_hkdf_context;

/**
 * \brief HKDF context initialization.
 *
 * The underlying hash function and salt value are provided. Arbitrary
 * salt lengths can be used.
 *
 * HKDF makes a difference between a salt of length zero, and an
 * absent salt (the latter being equivalent to a salt consisting of
 * bytes of value zero, of the same length as the hash function output).
 * If `salt_len` is zero, then this function assumes that the salt is
 * present but of length zero. To specify an _absent_ salt, use
 * `BR_HKDF_NO_SALT` as `salt` parameter (`salt_len` is then ignored).
 *
 * \param hc              HKDF context to initialise.
 * \param digest_vtable   pointer to the hash function implementation vtable.
 * \param salt            HKDF-Extract salt.
 * \param salt_len        HKDF-Extract salt length (in bytes).
 */
void br_hkdf_init(br_hkdf_context *hc, const br_hash_class *digest_vtable,
	const void *salt, size_t salt_len);

/**
 * \brief The special "absent salt" value for HKDF.
 */
#define BR_HKDF_NO_SALT   (&br_hkdf_no_salt)

#ifndef BR_DOXYGEN_IGNORE
extern const unsigned char br_hkdf_no_salt;
#endif

/**
 * \brief HKDF input injection (HKDF-Extract).
 *
 * This function injects some more input bytes ("key material") into
 * HKDF. This function may be called several times, after `br_hkdf_init()`
 * but before `br_hkdf_flip()`.
 *
 * \param hc        HKDF context.
 * \param ikm       extra input bytes.
 * \param ikm_len   number of extra input bytes.
 */
void br_hkdf_inject(br_hkdf_context *hc, const void *ikm, size_t ikm_len);

/**
 * \brief HKDF switch to the HKDF-Expand phase.
 *
 * This call terminates the HKDF-Extract process (input injection), and
 * starts the HKDF-Expand process (output production).
 *
 * \param hc   HKDF context.
 */
void br_hkdf_flip(br_hkdf_context *hc);

/**
 * \brief HKDF output production (HKDF-Expand).
 *
 * Produce more output bytes from the current state. This function may be
 * called several times, but only after `br_hkdf_flip()`.
 *
 * Returned value is the number of actually produced bytes. The total
 * output length is limited to 255 times the output length of the
 * underlying hash function.
 *
 * \param hc         HKDF context.
 * \param info       application specific information string.
 * \param info_len   application specific information string length (in bytes).
 * \param out        destination buffer for the HKDF output.
 * \param out_len    the length of the requested output (in bytes).
 * \return  the produced output length (in bytes).
 */
size_t br_hkdf_produce(br_hkdf_context *hc,
	const void *info, size_t info_len, void *out, size_t out_len);

/**
 * \brief SHAKE context.
 *
 * The HKDF context is initialized with a "security level". The internal
 * notion is called "capacity"; the capacity is twice the security level
 * (for instance, SHAKE128 has capacity 256).
 *
 * The caller is responsible for allocating the context where
 * appropriate. Context initialisation and usage incurs no dynamic
 * allocation, so there is no release function.
 */
typedef struct {
#ifndef BR_DOXYGEN_IGNORE
	unsigned char dbuf[200];
	size_t dptr;
	size_t rate;
	uint64_t A[25];
#endif
} br_shake_context;

/**
 * \brief SHAKE context initialization.
 *
 * The context is initialized for the provided "security level".
 * Internally, this sets the "capacity" to twice the security level;
 * thus, for SHAKE128, the `security_level` parameter should be 128,
 * which corresponds to a 256-bit capacity.
 *
 * Allowed security levels are all multiples of 32, from 32 to 768,
 * inclusive. Larger security levels imply lower performance; levels
 * beyond 256 bits don't make much sense. Standard levels are 128
 * and 256 bits (for SHAKE128 and SHAKE256, respectively).
 *
 * \param sc               SHAKE context to initialise.
 * \param security_level   security level (in bits).
 */
void br_shake_init(br_shake_context *sc, int security_level);

/**
 * \brief SHAKE input injection.
 *
 * This function injects some more input bytes ("key material") into
 * SHAKE. This function may be called several times, after `br_shake_init()`
 * but before `br_shake_flip()`.
 *
 * \param sc     SHAKE context.
 * \param data   extra input bytes.
 * \param len    number of extra input bytes.
 */
void br_shake_inject(br_shake_context *sc, const void *data, size_t len);

/**
 * \brief SHAKE switch to production phase.
 *
 * This call terminates the input injection process, and starts the
 * output production process.
 *
 * \param sc   SHAKE context.
 */
void br_shake_flip(br_shake_context *hc);

/**
 * \brief SHAKE output production.
 *
 * Produce more output bytes from the current state. This function may be
 * called several times, but only after `br_shake_flip()`.
 *
 * There is no practical limit to the number of bytes that may be produced.
 *
 * \param sc    SHAKE context.
 * \param out   destination buffer for the SHAKE output.
 * \param len   the length of the requested output (in bytes).
 */
void br_shake_produce(br_shake_context *sc, void *out, size_t len);

#ifdef __cplusplus
}
#endif

#endif
