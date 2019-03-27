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

#ifndef BR_BEARSSL_HMAC_H__
#define BR_BEARSSL_HMAC_H__

#include <stddef.h>
#include <stdint.h>

#include "bearssl_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file bearssl_hmac.h
 *
 * # HMAC
 *
 * HMAC is initialized with a key and an underlying hash function; it
 * then fills a "key context". That context contains the processed
 * key.
 *
 * With the key context, a HMAC context can be initialized to process
 * the input bytes and obtain the MAC output. The key context is not
 * modified during that process, and can be reused.
 *
 * IMPORTANT: HMAC shall be used only with functions that have the
 * following properties:
 *
 *   - hash output size does not exceed 64 bytes;
 *   - hash internal state size does not exceed 64 bytes;
 *   - internal block length is a power of 2 between 16 and 256 bytes.
 */

/**
 * \brief HMAC key context.
 *
 * The HMAC key context is initialised with a hash function implementation
 * and a secret key. Contents are opaque (callers should not access them
 * directly). The caller is responsible for allocating the context where
 * appropriate. Context initialisation and usage incurs no dynamic
 * allocation, so there is no release function.
 */
typedef struct {
#ifndef BR_DOXYGEN_IGNORE
	const br_hash_class *dig_vtable;
	unsigned char ksi[64], kso[64];
#endif
} br_hmac_key_context;

/**
 * \brief HMAC key context initialisation.
 *
 * Initialise the key context with the provided key, using the hash function
 * identified by `digest_vtable`. This supports arbitrary key lengths.
 *
 * \param kc              HMAC key context to initialise.
 * \param digest_vtable   pointer to the hash function implementation vtable.
 * \param key             pointer to the HMAC secret key.
 * \param key_len         HMAC secret key length (in bytes).
 */
void br_hmac_key_init(br_hmac_key_context *kc,
	const br_hash_class *digest_vtable, const void *key, size_t key_len);

/*
 * \brief Get the underlying hash function.
 *
 * This function returns a pointer to the implementation vtable of the
 * hash function used for this HMAC key context.
 *
 * \param kc   HMAC key context.
 * \return  the hash function implementation.
 */
static inline const br_hash_class *br_hmac_key_get_digest(
	const br_hmac_key_context *kc)
{
	return kc->dig_vtable;
}

/**
 * \brief HMAC computation context.
 *
 * The HMAC computation context maintains the state for a single HMAC
 * computation. It is modified as input bytes are injected. The context
 * is caller-allocated and has no release function since it does not
 * dynamically allocate external resources. Its contents are opaque.
 */
typedef struct {
#ifndef BR_DOXYGEN_IGNORE
	br_hash_compat_context dig;
	unsigned char kso[64];
	size_t out_len;
#endif
} br_hmac_context;

/**
 * \brief HMAC computation initialisation.
 *
 * Initialise a HMAC context with a key context. The key context is
 * unmodified. Relevant data from the key context is immediately copied;
 * the key context can thus be independently reused, modified or released
 * without impacting this HMAC computation.
 *
 * An explicit output length can be specified; the actual output length
 * will be the minimum of that value and the natural HMAC output length.
 * If `out_len` is 0, then the natural HMAC output length is selected. The
 * "natural output length" is the output length of the underlying hash
 * function.
 *
 * \param ctx       HMAC context to initialise.
 * \param kc        HMAC key context (already initialised with the key).
 * \param out_len   HMAC output length (0 to select "natural length").
 */
void br_hmac_init(br_hmac_context *ctx,
	const br_hmac_key_context *kc, size_t out_len);

/**
 * \brief Get the HMAC output size.
 *
 * The HMAC output size is the number of bytes that will actually be
 * produced with `br_hmac_out()` with the provided context. This function
 * MUST NOT be called on a non-initialised HMAC computation context.
 * The returned value is the minimum of the HMAC natural length (output
 * size of the underlying hash function) and the `out_len` parameter which
 * was used with the last `br_hmac_init()` call on that context (if the
 * initialisation `out_len` parameter was 0, then this function will
 * return the HMAC natural length).
 *
 * \param ctx   the (already initialised) HMAC computation context.
 * \return  the HMAC actual output size.
 */
static inline size_t
br_hmac_size(br_hmac_context *ctx)
{
	return ctx->out_len;
}

/*
 * \brief Get the underlying hash function.
 *
 * This function returns a pointer to the implementation vtable of the
 * hash function used for this HMAC context.
 *
 * \param hc   HMAC context.
 * \return  the hash function implementation.
 */
static inline const br_hash_class *br_hmac_get_digest(
	const br_hmac_context *hc)
{
	return hc->dig.vtable;
}

/**
 * \brief Inject some bytes in HMAC.
 *
 * The provided `len` bytes are injected as extra input in the HMAC
 * computation incarnated by the `ctx` HMAC context. It is acceptable
 * that `len` is zero, in which case `data` is ignored (and may be
 * `NULL`) and this function does nothing.
 */
void br_hmac_update(br_hmac_context *ctx, const void *data, size_t len);

/**
 * \brief Compute the HMAC output.
 *
 * The destination buffer MUST be large enough to accommodate the result;
 * its length is at most the "natural length" of HMAC (i.e. the output
 * length of the underlying hash function). The context is NOT modified;
 * further bytes may be processed. Thus, "partial HMAC" values can be
 * efficiently obtained.
 *
 * Returned value is the output length (in bytes).
 *
 * \param ctx   HMAC computation context.
 * \param out   destination buffer for the HMAC output.
 * \return  the produced value length (in bytes).
 */
size_t br_hmac_out(const br_hmac_context *ctx, void *out);

/**
 * \brief Constant-time HMAC computation.
 *
 * This function compute the HMAC output in constant time. Some extra
 * input bytes are processed, then the output is computed. The extra
 * input consists in the `len` bytes pointed to by `data`. The `len`
 * parameter must lie between `min_len` and `max_len` (inclusive);
 * `max_len` bytes are actually read from `data`. Computing time (and
 * memory access pattern) will not depend upon the data byte contents or
 * the value of `len`.
 *
 * The output is written in the `out` buffer, that MUST be large enough
 * to receive it.
 *
 * The difference `max_len - min_len` MUST be less than 2<sup>30</sup>
 * (i.e. about one gigabyte).
 *
 * This function computes the output properly only if the underlying
 * hash function uses MD padding (i.e. MD5, SHA-1, SHA-224, SHA-256,
 * SHA-384 or SHA-512).
 *
 * The provided context is NOT modified.
 *
 * \param ctx       the (already initialised) HMAC computation context.
 * \param data      the extra input bytes.
 * \param len       the extra input length (in bytes).
 * \param min_len   minimum extra input length (in bytes).
 * \param max_len   maximum extra input length (in bytes).
 * \param out       destination buffer for the HMAC output.
 * \return  the produced value length (in bytes).
 */
size_t br_hmac_outCT(const br_hmac_context *ctx,
	const void *data, size_t len, size_t min_len, size_t max_len,
	void *out);

#ifdef __cplusplus
}
#endif

#endif
