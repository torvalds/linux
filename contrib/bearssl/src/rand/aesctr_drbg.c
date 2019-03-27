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

#include "inner.h"

/* see bearssl_rand.h */
void
br_aesctr_drbg_init(br_aesctr_drbg_context *ctx,
	const br_block_ctr_class *aesctr,
	const void *seed, size_t len)
{
	unsigned char tmp[16];

	ctx->vtable = &br_aesctr_drbg_vtable;
	memset(tmp, 0, sizeof tmp);
	aesctr->init(&ctx->sk.vtable, tmp, 16);
	ctx->cc = 0;
	br_aesctr_drbg_update(ctx, seed, len);
}

/* see bearssl_rand.h */
void
br_aesctr_drbg_generate(br_aesctr_drbg_context *ctx, void *out, size_t len)
{
	unsigned char *buf;
	unsigned char iv[12];

	buf = out;
	memset(iv, 0, sizeof iv);
	while (len > 0) {
		size_t clen;

		/*
		 * We generate data by blocks of at most 65280 bytes. This
		 * allows for unambiguously testing the counter overflow
		 * condition; also, it should work on 16-bit architectures
		 * (where 'size_t' is 16 bits only).
		 */
		clen = len;
		if (clen > 65280) {
			clen = 65280;
		}

		/*
		 * We make sure that the counter won't exceed the configured
		 * limit.
		 */
		if ((uint32_t)(ctx->cc + ((clen + 15) >> 4)) > 32768) {
			clen = (32768 - ctx->cc) << 4;
			if (clen > len) {
				clen = len;
			}
		}

		/*
		 * Run CTR.
		 */
		memset(buf, 0, clen);
		ctx->cc = ctx->sk.vtable->run(&ctx->sk.vtable,
			iv, ctx->cc, buf, clen);
		buf += clen;
		len -= clen;

		/*
		 * Every 32768 blocks, we force a state update.
		 */
		if (ctx->cc >= 32768) {
			br_aesctr_drbg_update(ctx, NULL, 0);
		}
	}
}

/* see bearssl_rand.h */
void
br_aesctr_drbg_update(br_aesctr_drbg_context *ctx, const void *seed, size_t len)
{
	/*
	 * We use a Hirose construction on AES-256 to make a hash function.
	 * Function definition:
	 *  - running state consists in two 16-byte blocks G and H
	 *  - initial values of G and H are conventional
	 *  - there is a fixed block-sized constant C
	 *  - for next data block m:
	 *      set AES key to H||m
	 *      G' = E(G) xor G
	 *      H' = E(G xor C) xor G xor C
	 *      G <- G', H <- H'
	 *  - once all blocks have been processed, output is H||G
	 *
	 * Constants:
	 *   G_init = B6 B6 ... B6
	 *   H_init = A5 A5 ... A5
	 *   C      = 01 00 ... 00
	 *
	 * With this hash function h(), we compute the new state as
	 * follows:
	 *  - produce a state-dependent value s as encryption of an
	 *    all-one block with AES and the current key
	 *  - compute the new key as the first 128 bits of h(s||seed)
	 *
	 * Original Hirose article:
	 *    https://www.iacr.org/archive/fse2006/40470213/40470213.pdf
	 */

	unsigned char s[16], iv[12];
	unsigned char G[16], H[16];
	int first;

	/*
	 * Use an all-one IV to get a fresh output block that depends on the
	 * current seed.
	 */
	memset(iv, 0xFF, sizeof iv);
	memset(s, 0, 16);
	ctx->sk.vtable->run(&ctx->sk.vtable, iv, 0xFFFFFFFF, s, 16);

	/*
	 * Set G[] and H[] to conventional start values.
	 */
	memset(G, 0xB6, sizeof G);
	memset(H, 0x5A, sizeof H);

	/*
	 * Process the concatenation of the current state and the seed
	 * with the custom hash function.
	 */
	first = 1;
	for (;;) {
		unsigned char tmp[32];
		unsigned char newG[16];

		/*
		 * Assemble new key H||m into tmp[].
		 */
		memcpy(tmp, H, 16);
		if (first) {
			memcpy(tmp + 16, s, 16);
			first = 0;
		} else {
			size_t clen;

			if (len == 0) {
				break;
			}
			clen = len < 16 ? len : 16;
			memcpy(tmp + 16, seed, clen);
			memset(tmp + 16 + clen, 0, 16 - clen);
			seed = (const unsigned char *)seed + clen;
			len -= clen;
		}
		ctx->sk.vtable->init(&ctx->sk.vtable, tmp, 32);

		/*
		 * Compute new G and H values.
		 */
		memcpy(iv, G, 12);
		memcpy(newG, G, 16);
		ctx->sk.vtable->run(&ctx->sk.vtable, iv,
			br_dec32be(G + 12), newG, 16);
		iv[0] ^= 0x01;
		memcpy(H, G, 16);
		H[0] ^= 0x01;
		ctx->sk.vtable->run(&ctx->sk.vtable, iv,
			br_dec32be(G + 12), H, 16);
		memcpy(G, newG, 16);
	}

	/*
	 * Output hash value is H||G. We truncate it to its first 128 bits,
	 * i.e. H; that's our new AES key.
	 */
	ctx->sk.vtable->init(&ctx->sk.vtable, H, 16);
	ctx->cc = 0;
}

/* see bearssl_rand.h */
const br_prng_class br_aesctr_drbg_vtable = {
	sizeof(br_aesctr_drbg_context),
	(void (*)(const br_prng_class **, const void *, const void *, size_t))
		&br_aesctr_drbg_init,
	(void (*)(const br_prng_class **, void *, size_t))
		&br_aesctr_drbg_generate,
	(void (*)(const br_prng_class **, const void *, size_t))
		&br_aesctr_drbg_update
};
