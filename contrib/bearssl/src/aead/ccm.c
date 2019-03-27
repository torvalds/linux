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
 * The combined CTR + CBC-MAC functions can only handle full blocks,
 * so some buffering is necessary.
 *
 *  - 'ptr' contains a value from 0 to 15, which is the number of bytes
 *    accumulated in buf[] that still needs to be processed with the
 *    current CBC-MAC computation.
 *
 *  - When processing the message itself, CTR encryption/decryption is
 *    also done at the same time. The first 'ptr' bytes of buf[] then
 *    contains the plaintext bytes, while the last '16 - ptr' bytes of
 *    buf[] are the remnants of the stream block, to be used against
 *    the next input bytes, when available. When 'ptr' is 0, the
 *    contents of buf[] are to be ignored.
 *
 *  - The current counter and running CBC-MAC values are kept in 'ctr'
 *    and 'cbcmac', respectively.
 */

/* see bearssl_block.h */
void
br_ccm_init(br_ccm_context *ctx, const br_block_ctrcbc_class **bctx)
{
	ctx->bctx = bctx;
}

/* see bearssl_block.h */
int
br_ccm_reset(br_ccm_context *ctx, const void *nonce, size_t nonce_len,
	uint64_t aad_len, uint64_t data_len, size_t tag_len)
{
	unsigned char tmp[16];
	unsigned u, q;

	if (nonce_len < 7 || nonce_len > 13) {
		return 0;
	}
	if (tag_len < 4 || tag_len > 16 || (tag_len & 1) != 0) {
		return 0;
	}
	q = 15 - (unsigned)nonce_len;
	ctx->tag_len = tag_len;

	/*
	 * Block B0, to start CBC-MAC.
	 */
	tmp[0] = (aad_len > 0 ? 0x40 : 0x00)
		| (((unsigned)tag_len - 2) << 2)
		| (q - 1);
	memcpy(tmp + 1, nonce, nonce_len);
	for (u = 0; u < q; u ++) {
		tmp[15 - u] = (unsigned char)data_len;
		data_len >>= 8;
	}
	if (data_len != 0) {
		/*
		 * If the data length was not entirely consumed in the
		 * loop above, then it exceeds the maximum limit of
		 * q bytes (when encoded).
		 */
		return 0;
	}

	/*
	 * Start CBC-MAC.
	 */
	memset(ctx->cbcmac, 0, sizeof ctx->cbcmac);
	(*ctx->bctx)->mac(ctx->bctx, ctx->cbcmac, tmp, sizeof tmp);

	/*
	 * Assemble AAD length header.
	 */
	if ((aad_len >> 32) != 0) {
		ctx->buf[0] = 0xFF;
		ctx->buf[1] = 0xFF;
		br_enc64be(ctx->buf + 2, aad_len);
		ctx->ptr = 10;
	} else if (aad_len >= 0xFF00) {
		ctx->buf[0] = 0xFF;
		ctx->buf[1] = 0xFE;
		br_enc32be(ctx->buf + 2, (uint32_t)aad_len);
		ctx->ptr = 6;
	} else if (aad_len > 0) {
		br_enc16be(ctx->buf, (unsigned)aad_len);
		ctx->ptr = 2;
	} else {
		ctx->ptr = 0;
	}

	/*
	 * Make initial counter value and compute tag mask.
	 */
	ctx->ctr[0] = q - 1;
	memcpy(ctx->ctr + 1, nonce, nonce_len);
	memset(ctx->ctr + 1 + nonce_len, 0, q);
	memset(ctx->tagmask, 0, sizeof ctx->tagmask);
	(*ctx->bctx)->ctr(ctx->bctx, ctx->ctr,
		ctx->tagmask, sizeof ctx->tagmask);

	return 1;
}

/* see bearssl_block.h */
void
br_ccm_aad_inject(br_ccm_context *ctx, const void *data, size_t len)
{
	const unsigned char *dbuf;
	size_t ptr;

	dbuf = data;

	/*
	 * Complete partial block, if needed.
	 */
	ptr = ctx->ptr;
	if (ptr != 0) {
		size_t clen;

		clen = (sizeof ctx->buf) - ptr;
		if (clen > len) {
			memcpy(ctx->buf + ptr, dbuf, len);
			ctx->ptr = ptr + len;
			return;
		}
		memcpy(ctx->buf + ptr, dbuf, clen);
		dbuf += clen;
		len -= clen;
		(*ctx->bctx)->mac(ctx->bctx, ctx->cbcmac,
			ctx->buf, sizeof ctx->buf);
	}

	/*
	 * Process complete blocks.
	 */
	ptr = len & 15;
	len -= ptr;
	(*ctx->bctx)->mac(ctx->bctx, ctx->cbcmac, dbuf, len);
	dbuf += len;

	/*
	 * Copy last partial block in the context buffer.
	 */
	memcpy(ctx->buf, dbuf, ptr);
	ctx->ptr = ptr;
}

/* see bearssl_block.h */
void
br_ccm_flip(br_ccm_context *ctx)
{
	size_t ptr;

	/*
	 * Complete AAD partial block with zeros, if necessary.
	 */
	ptr = ctx->ptr;
	if (ptr != 0) {
		memset(ctx->buf + ptr, 0, (sizeof ctx->buf) - ptr);
		(*ctx->bctx)->mac(ctx->bctx, ctx->cbcmac,
			ctx->buf, sizeof ctx->buf);
		ctx->ptr = 0;
	}

	/*
	 * Counter was already set by br_ccm_reset().
	 */
}

/* see bearssl_block.h */
void
br_ccm_run(br_ccm_context *ctx, int encrypt, void *data, size_t len)
{
	unsigned char *dbuf;
	size_t ptr;

	dbuf = data;

	/*
	 * Complete a partial block, if any: ctx->buf[] contains
	 * ctx->ptr plaintext bytes (already reported), and the other
	 * bytes are CTR stream output.
	 */
	ptr = ctx->ptr;
	if (ptr != 0) {
		size_t clen;
		size_t u;

		clen = (sizeof ctx->buf) - ptr;
		if (clen > len) {
			clen = len;
		}
		if (encrypt) {
			for (u = 0; u < clen; u ++) {
				unsigned w, x;

				w = ctx->buf[ptr + u];
				x = dbuf[u];
				ctx->buf[ptr + u] = x;
				dbuf[u] = w ^ x;
			}
		} else {
			for (u = 0; u < clen; u ++) {
				unsigned w;

				w = ctx->buf[ptr + u] ^ dbuf[u];
				dbuf[u] = w;
				ctx->buf[ptr + u] = w;
			}
		}
		dbuf += clen;
		len -= clen;
		ptr += clen;
		if (ptr < sizeof ctx->buf) {
			ctx->ptr = ptr;
			return;
		}
		(*ctx->bctx)->mac(ctx->bctx,
			ctx->cbcmac, ctx->buf, sizeof ctx->buf);
	}

	/*
	 * Process all complete blocks. Note that the ctrcbc API is for
	 * encrypt-then-MAC (CBC-MAC is computed over the encrypted
	 * blocks) while CCM uses MAC-and-encrypt (CBC-MAC is computed
	 * over the plaintext blocks). Therefore, we need to use the
	 * _decryption_ function for encryption, and the encryption
	 * function for decryption (this works because CTR encryption
	 * and decryption are identical, so the choice really is about
	 * computing the CBC-MAC before or after XORing with the CTR
	 * stream).
	 */
	ptr = len & 15;
	len -= ptr;
	if (encrypt) {
		(*ctx->bctx)->decrypt(ctx->bctx, ctx->ctr, ctx->cbcmac,
			dbuf, len);
	} else {
		(*ctx->bctx)->encrypt(ctx->bctx, ctx->ctr, ctx->cbcmac,
			dbuf, len);
	}
	dbuf += len;

	/*
	 * If there is some remaining data, then we need to compute an
	 * extra block of CTR stream.
	 */
	if (ptr != 0) {
		size_t u;

		memset(ctx->buf, 0, sizeof ctx->buf);
		(*ctx->bctx)->ctr(ctx->bctx, ctx->ctr,
			ctx->buf, sizeof ctx->buf);
		if (encrypt) {
			for (u = 0; u < ptr; u ++) {
				unsigned w, x;

				w = ctx->buf[u];
				x = dbuf[u];
				ctx->buf[u] = x;
				dbuf[u] = w ^ x;
			}
		} else {
			for (u = 0; u < ptr; u ++) {
				unsigned w;

				w = ctx->buf[u] ^ dbuf[u];
				dbuf[u] = w;
				ctx->buf[u] = w;
			}
		}
	}
	ctx->ptr = ptr;
}

/* see bearssl_block.h */
size_t
br_ccm_get_tag(br_ccm_context *ctx, void *tag)
{
	size_t ptr;
	size_t u;

	/*
	 * If there is some buffered data, then we need to pad it with
	 * zeros and finish up CBC-MAC.
	 */
	ptr = ctx->ptr;
	if (ptr != 0) {
		memset(ctx->buf + ptr, 0, (sizeof ctx->buf) - ptr);
		(*ctx->bctx)->mac(ctx->bctx, ctx->cbcmac,
			ctx->buf, sizeof ctx->buf);
	}

	/*
	 * XOR the tag mask into the CBC-MAC output.
	 */
	for (u = 0; u < ctx->tag_len; u ++) {
		ctx->cbcmac[u] ^= ctx->tagmask[u];
	}
	memcpy(tag, ctx->cbcmac, ctx->tag_len);
	return ctx->tag_len;
}

/* see bearssl_block.h */
uint32_t
br_ccm_check_tag(br_ccm_context *ctx, const void *tag)
{
	unsigned char tmp[16];
	size_t u, tag_len;
	uint32_t z;

	tag_len = br_ccm_get_tag(ctx, tmp);
	z = 0;
	for (u = 0; u < tag_len; u ++) {
		z |= tmp[u] ^ ((const unsigned char *)tag)[u];
	}
	return EQ0(z);
}
