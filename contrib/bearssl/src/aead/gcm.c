/*
 * Copyright (c) 2017 Thomas Pornin <pornin@bolet.org>
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

/*
 * Implementation Notes
 * ====================
 *
 * Since CTR and GHASH implementations can handle only full blocks, a
 * 16-byte buffer (buf[]) is maintained in the context:
 *
 *  - When processing AAD, buf[] contains the 0-15 unprocessed bytes.
 *
 *  - When doing CTR encryption / decryption, buf[] contains the AES output
 *    for the last partial block, to be used with the next few bytes of
 *    data, as well as the already encrypted bytes. For instance, if the
 *    processed data length so far is 21 bytes, then buf[0..4] contains
 *    the five last encrypted bytes, and buf[5..15] contains the next 11
 *    AES output bytes to be XORed with the next 11 bytes of input.
 *
 *    The recorded AES output bytes are used to complete the block when
 *    the corresponding bytes are obtained. Note that buf[] always
 *    contains the _encrypted_ bytes, whether we apply encryption or
 *    decryption: these bytes are used as input to GHASH when the block
 *    is complete.
 *
 * In both cases, the low bits of the data length counters (count_aad,
 * count_ctr) are used to work out the current situation.
 */

/* see bearssl_aead.h */
void
br_gcm_init(br_gcm_context *ctx, const br_block_ctr_class **bctx, br_ghash gh)
{
	unsigned char iv[12];

	ctx->vtable = &br_gcm_vtable;
	ctx->bctx = bctx;
	ctx->gh = gh;

	/*
	 * The GHASH key h[] is the raw encryption of the all-zero
	 * block. Since we only have a CTR implementation, we use it
	 * with an all-zero IV and a zero counter, to CTR-encrypt an
	 * all-zero block.
	 */
	memset(ctx->h, 0, sizeof ctx->h);
	memset(iv, 0, sizeof iv);
	(*bctx)->run(bctx, iv, 0, ctx->h, sizeof ctx->h);
}

/* see bearssl_aead.h */
void
br_gcm_reset(br_gcm_context *ctx, const void *iv, size_t len)
{
	/*
	 * If the provided nonce is 12 bytes, then this is the initial
	 * IV for CTR mode; it will be used with a counter that starts
	 * at 2 (value 1 is for encrypting the GHASH output into the tag).
	 *
	 * If the provided nonce has any other length, then it is hashed
	 * (with GHASH) into a 16-byte value that will be the IV for CTR
	 * (both 12-byte IV and 32-bit counter).
	 */
	if (len == 12) {
		memcpy(ctx->j0_1, iv, 12);
		ctx->j0_2 = 1;
	} else {
		unsigned char ty[16], tmp[16];

		memset(ty, 0, sizeof ty);
		ctx->gh(ty, ctx->h, iv, len);
		memset(tmp, 0, 8);
		br_enc64be(tmp + 8, (uint64_t)len << 3);
		ctx->gh(ty, ctx->h, tmp, 16);
		memcpy(ctx->j0_1, ty, 12);
		ctx->j0_2 = br_dec32be(ty + 12);
	}
	ctx->jc = ctx->j0_2 + 1;
	memset(ctx->y, 0, sizeof ctx->y);
	ctx->count_aad = 0;
	ctx->count_ctr = 0;
}

/* see bearssl_aead.h */
void
br_gcm_aad_inject(br_gcm_context *ctx, const void *data, size_t len)
{
	size_t ptr, dlen;

	ptr = (size_t)ctx->count_aad & (size_t)15;
	if (ptr != 0) {
		/*
		 * If there is a partial block, then we first try to
		 * complete it.
		 */
		size_t clen;

		clen = 16 - ptr;
		if (len < clen) {
			memcpy(ctx->buf + ptr, data, len);
			ctx->count_aad += (uint64_t)len;
			return;
		}
		memcpy(ctx->buf + ptr, data, clen);
		ctx->gh(ctx->y, ctx->h, ctx->buf, 16);
		data = (const unsigned char *)data + clen;
		len -= clen;
		ctx->count_aad += (uint64_t)clen;
	}

	/*
	 * Now AAD is aligned on a 16-byte block (with regards to GHASH).
	 * We process all complete blocks, and save the last partial
	 * block.
	 */
	dlen = len & ~(size_t)15;
	ctx->gh(ctx->y, ctx->h, data, dlen);
	memcpy(ctx->buf, (const unsigned char *)data + dlen, len - dlen);
	ctx->count_aad += (uint64_t)len;
}

/* see bearssl_aead.h */
void
br_gcm_flip(br_gcm_context *ctx)
{
	/*
	 * We complete the GHASH computation if there is a partial block.
	 * The GHASH implementation automatically applies padding with
	 * zeros.
	 */
	size_t ptr;

	ptr = (size_t)ctx->count_aad & (size_t)15;
	if (ptr != 0) {
		ctx->gh(ctx->y, ctx->h, ctx->buf, ptr);
	}
}

/* see bearssl_aead.h */
void
br_gcm_run(br_gcm_context *ctx, int encrypt, void *data, size_t len)
{
	unsigned char *buf;
	size_t ptr, dlen;

	buf = data;
	ptr = (size_t)ctx->count_ctr & (size_t)15;
	if (ptr != 0) {
		/*
		 * If we have a partial block, then we try to complete it.
		 */
		size_t u, clen;

		clen = 16 - ptr;
		if (len < clen) {
			clen = len;
		}
		for (u = 0; u < clen; u ++) {
			unsigned x, y;

			x = buf[u];
			y = x ^ ctx->buf[ptr + u];
			ctx->buf[ptr + u] = encrypt ? y : x;
			buf[u] = y;
		}
		ctx->count_ctr += (uint64_t)clen;
		buf += clen;
		len -= clen;
		if (ptr + clen < 16) {
			return;
		}
		ctx->gh(ctx->y, ctx->h, ctx->buf, 16);
	}

	/*
	 * Process full blocks.
	 */
	dlen = len & ~(size_t)15;
	if (!encrypt) {
		ctx->gh(ctx->y, ctx->h, buf, dlen);
	}
	ctx->jc = (*ctx->bctx)->run(ctx->bctx, ctx->j0_1, ctx->jc, buf, dlen);
	if (encrypt) {
		ctx->gh(ctx->y, ctx->h, buf, dlen);
	}
	buf += dlen;
	len -= dlen;
	ctx->count_ctr += (uint64_t)dlen;

	if (len > 0) {
		/*
		 * There is a partial block.
		 */
		size_t u;

		memset(ctx->buf, 0, sizeof ctx->buf);
		ctx->jc = (*ctx->bctx)->run(ctx->bctx, ctx->j0_1,
			ctx->jc, ctx->buf, 16);
		for (u = 0; u < len; u ++) {
			unsigned x, y;

			x = buf[u];
			y = x ^ ctx->buf[u];
			ctx->buf[u] = encrypt ? y : x;
			buf[u] = y;
		}
		ctx->count_ctr += (uint64_t)len;
	}
}

/* see bearssl_aead.h */
void
br_gcm_get_tag(br_gcm_context *ctx, void *tag)
{
	size_t ptr;
	unsigned char tmp[16];

	ptr = (size_t)ctx->count_ctr & (size_t)15;
	if (ptr > 0) {
		/*
		 * There is a partial block: encrypted/decrypted data has
		 * been produced, but the encrypted bytes must still be
		 * processed by GHASH.
		 */
		ctx->gh(ctx->y, ctx->h, ctx->buf, ptr);
	}

	/*
	 * Final block for GHASH: the AAD and plaintext lengths (in bits).
	 */
	br_enc64be(tmp, ctx->count_aad << 3);
	br_enc64be(tmp + 8, ctx->count_ctr << 3);
	ctx->gh(ctx->y, ctx->h, tmp, 16);

	/*
	 * Tag is the GHASH output XORed with the encryption of the
	 * nonce with the initial counter value.
	 */
	memcpy(tag, ctx->y, 16);
	(*ctx->bctx)->run(ctx->bctx, ctx->j0_1, ctx->j0_2, tag, 16);
}

/* see bearssl_aead.h */
void
br_gcm_get_tag_trunc(br_gcm_context *ctx, void *tag, size_t len)
{
	unsigned char tmp[16];

	br_gcm_get_tag(ctx, tmp);
	memcpy(tag, tmp, len);
}

/* see bearssl_aead.h */
uint32_t
br_gcm_check_tag_trunc(br_gcm_context *ctx, const void *tag, size_t len)
{
	unsigned char tmp[16];
	size_t u;
	int x;

	br_gcm_get_tag(ctx, tmp);
	x = 0;
	for (u = 0; u < len; u ++) {
		x |= tmp[u] ^ ((const unsigned char *)tag)[u];
	}
	return EQ0(x);
}

/* see bearssl_aead.h */
uint32_t
br_gcm_check_tag(br_gcm_context *ctx, const void *tag)
{
	return br_gcm_check_tag_trunc(ctx, tag, 16);
}

/* see bearssl_aead.h */
const br_aead_class br_gcm_vtable = {
	16,
	(void (*)(const br_aead_class **, const void *, size_t))
		&br_gcm_reset,
	(void (*)(const br_aead_class **, const void *, size_t))
		&br_gcm_aad_inject,
	(void (*)(const br_aead_class **))
		&br_gcm_flip,
	(void (*)(const br_aead_class **, int, void *, size_t))
		&br_gcm_run,
	(void (*)(const br_aead_class **, void *))
		&br_gcm_get_tag,
	(uint32_t (*)(const br_aead_class **, const void *))
		&br_gcm_check_tag,
	(void (*)(const br_aead_class **, void *, size_t))
		&br_gcm_get_tag_trunc,
	(uint32_t (*)(const br_aead_class **, const void *, size_t))
		&br_gcm_check_tag_trunc
};
