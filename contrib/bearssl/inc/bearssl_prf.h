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

#ifndef BR_BEARSSL_PRF_H__
#define BR_BEARSSL_PRF_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \file bearssl_prf.h
 *
 * # The TLS PRF
 *
 * The "PRF" is the pseudorandom function used internally during the
 * SSL/TLS handshake, notably to expand negotiated shared secrets into
 * the symmetric encryption keys that will be used to process the
 * application data.
 *
 * TLS 1.0 and 1.1 define a PRF that is based on both MD5 and SHA-1. This
 * is implemented by the `br_tls10_prf()` function.
 *
 * TLS 1.2 redefines the PRF, using an explicit hash function. The
 * `br_tls12_sha256_prf()` and `br_tls12_sha384_prf()` functions apply that
 * PRF with, respectively, SHA-256 and SHA-384. Most standard cipher suites
 * rely on the SHA-256 based PRF, but some use SHA-384.
 *
 * The PRF always uses as input three parameters: a "secret" (some
 * bytes), a "label" (ASCII string), and a "seed" (again some bytes). An
 * arbitrary output length can be produced. The "seed" is provided as an
 * arbitrary number of binary chunks, that gets internally concatenated.
 */

/**
 * \brief Type for a seed chunk.
 *
 * Each chunk may have an arbitrary length, and may be empty (no byte at
 * all). If the chunk length is zero, then the pointer to the chunk data
 * may be `NULL`.
 */
typedef struct {
	/**
	 * \brief Pointer to the chunk data.
	 */
	const void *data;

	/**
	 * \brief Chunk length (in bytes).
	 */
	size_t len;
} br_tls_prf_seed_chunk;

/**
 * \brief PRF implementation for TLS 1.0 and 1.1.
 *
 * This PRF is the one specified by TLS 1.0 and 1.1. It internally uses
 * MD5 and SHA-1.
 *
 * \param dst          destination buffer.
 * \param len          output length (in bytes).
 * \param secret       secret value (key) for this computation.
 * \param secret_len   length of "secret" (in bytes).
 * \param label        PRF label (zero-terminated ASCII string).
 * \param seed_num     number of seed chunks.
 * \param seed         seed chnks for this computation (usually non-secret).
 */
void br_tls10_prf(void *dst, size_t len,
	const void *secret, size_t secret_len, const char *label,
	size_t seed_num, const br_tls_prf_seed_chunk *seed);

/**
 * \brief PRF implementation for TLS 1.2, with SHA-256.
 *
 * This PRF is the one specified by TLS 1.2, when the underlying hash
 * function is SHA-256.
 *
 * \param dst          destination buffer.
 * \param len          output length (in bytes).
 * \param secret       secret value (key) for this computation.
 * \param secret_len   length of "secret" (in bytes).
 * \param label        PRF label (zero-terminated ASCII string).
 * \param seed_num     number of seed chunks.
 * \param seed         seed chnks for this computation (usually non-secret).
 */
void br_tls12_sha256_prf(void *dst, size_t len,
	const void *secret, size_t secret_len, const char *label,
	size_t seed_num, const br_tls_prf_seed_chunk *seed);

/**
 * \brief PRF implementation for TLS 1.2, with SHA-384.
 *
 * This PRF is the one specified by TLS 1.2, when the underlying hash
 * function is SHA-384.
 *
 * \param dst          destination buffer.
 * \param len          output length (in bytes).
 * \param secret       secret value (key) for this computation.
 * \param secret_len   length of "secret" (in bytes).
 * \param label        PRF label (zero-terminated ASCII string).
 * \param seed_num     number of seed chunks.
 * \param seed         seed chnks for this computation (usually non-secret).
 */
void br_tls12_sha384_prf(void *dst, size_t len,
	const void *secret, size_t secret_len, const char *label,
	size_t seed_num, const br_tls_prf_seed_chunk *seed);

/** 
 * brief A convenient type name for a PRF implementation.
 *
 * \param dst          destination buffer.
 * \param len          output length (in bytes).
 * \param secret       secret value (key) for this computation.
 * \param secret_len   length of "secret" (in bytes).
 * \param label        PRF label (zero-terminated ASCII string).
 * \param seed_num     number of seed chunks.
 * \param seed         seed chnks for this computation (usually non-secret).
 */
typedef void (*br_tls_prf_impl)(void *dst, size_t len,
	const void *secret, size_t secret_len, const char *label,
	size_t seed_num, const br_tls_prf_seed_chunk *seed);

#ifdef __cplusplus
}
#endif

#endif
