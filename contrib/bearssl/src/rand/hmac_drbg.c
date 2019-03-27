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

#include "inner.h"

/* see bearssl.h */
void
br_hmac_drbg_init(br_hmac_drbg_context *ctx,
	const br_hash_class *digest_class, const void *seed, size_t len)
{
	size_t hlen;

	ctx->vtable = &br_hmac_drbg_vtable;
	hlen = br_digest_size(digest_class);
	memset(ctx->K, 0x00, hlen);
	memset(ctx->V, 0x01, hlen);
	ctx->digest_class = digest_class;
	br_hmac_drbg_update(ctx, seed, len);
}

/* see bearssl.h */
void
br_hmac_drbg_generate(br_hmac_drbg_context *ctx, void *out, size_t len)
{
	const br_hash_class *dig;
	br_hmac_key_context kc;
	br_hmac_context hc;
	size_t hlen;
	unsigned char *buf;
	unsigned char x;

	dig = ctx->digest_class;
	hlen = br_digest_size(dig);
	br_hmac_key_init(&kc, dig, ctx->K, hlen);
	buf = out;
	while (len > 0) {
		size_t clen;

		br_hmac_init(&hc, &kc, 0);
		br_hmac_update(&hc, ctx->V, hlen);
		br_hmac_out(&hc, ctx->V);
		clen = hlen;
		if (clen > len) {
			clen = len;
		}
		memcpy(buf, ctx->V, clen);
		buf += clen;
		len -= clen;
	}

	/*
	 * To prepare the state for the next request, we should call
	 * br_hmac_drbg_update() with an empty additional seed. However,
	 * we already have an initialized HMAC context with the right
	 * initial key, and we don't want to push another one on the
	 * stack, so we inline that update() call here.
	 */
	br_hmac_init(&hc, &kc, 0);
	br_hmac_update(&hc, ctx->V, hlen);
	x = 0x00;
	br_hmac_update(&hc, &x, 1);
	br_hmac_out(&hc, ctx->K);
	br_hmac_key_init(&kc, dig, ctx->K, hlen);
	br_hmac_init(&hc, &kc, 0);
	br_hmac_update(&hc, ctx->V, hlen);
	br_hmac_out(&hc, ctx->V);
}

/* see bearssl.h */
void
br_hmac_drbg_update(br_hmac_drbg_context *ctx, const void *seed, size_t len)
{
	const br_hash_class *dig;
	br_hmac_key_context kc;
	br_hmac_context hc;
	size_t hlen;
	unsigned char x;

	dig = ctx->digest_class;
	hlen = br_digest_size(dig);

	/*
	 * 1. K = HMAC(K, V || 0x00 || seed)
	 */
	br_hmac_key_init(&kc, dig, ctx->K, hlen);
	br_hmac_init(&hc, &kc, 0);
	br_hmac_update(&hc, ctx->V, hlen);
	x = 0x00;
	br_hmac_update(&hc, &x, 1);
	br_hmac_update(&hc, seed, len);
	br_hmac_out(&hc, ctx->K);
	br_hmac_key_init(&kc, dig, ctx->K, hlen);

	/*
	 * 2. V = HMAC(K, V)
	 */
	br_hmac_init(&hc, &kc, 0);
	br_hmac_update(&hc, ctx->V, hlen);
	br_hmac_out(&hc, ctx->V);

	/*
	 * 3. If the additional seed is empty, then stop here.
	 */
	if (len == 0) {
		return;
	}

	/*
	 * 4. K = HMAC(K, V || 0x01 || seed)
	 */
	br_hmac_init(&hc, &kc, 0);
	br_hmac_update(&hc, ctx->V, hlen);
	x = 0x01;
	br_hmac_update(&hc, &x, 1);
	br_hmac_update(&hc, seed, len);
	br_hmac_out(&hc, ctx->K);
	br_hmac_key_init(&kc, dig, ctx->K, hlen);

	/*
	 * 5. V = HMAC(K, V)
	 */
	br_hmac_init(&hc, &kc, 0);
	br_hmac_update(&hc, ctx->V, hlen);
	br_hmac_out(&hc, ctx->V);
}

/* see bearssl.h */
const br_prng_class br_hmac_drbg_vtable = {
	sizeof(br_hmac_drbg_context),
	(void (*)(const br_prng_class **, const void *, const void *, size_t))
		&br_hmac_drbg_init,
	(void (*)(const br_prng_class **, void *, size_t))
		&br_hmac_drbg_generate,
	(void (*)(const br_prng_class **, const void *, size_t))
		&br_hmac_drbg_update
};
