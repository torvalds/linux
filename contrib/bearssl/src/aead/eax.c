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
 * so some buffering is necessary. Moreover, EAX has a special padding
 * rule for CBC-MAC, which implies that we cannot compute the MAC over
 * the last received full block until we know whether we are at the
 * end of the data or not.
 *
 *  - 'ptr' contains a value from 1 to 16, which is the number of bytes
 *    accumulated in buf[] that still needs to be processed with the
 *    current OMAC computation. Beware that this can go to 16: a
 *    complete block cannot be processed until it is known whether it
 *    is the last block or not. However, it can never be 0, because
 *    OMAC^t works on an input that is at least one-block long.
 *
 *  - When processing the message itself, CTR encryption/decryption is
 *    also done at the same time. The first 'ptr' bytes of buf[] then
 *    contains the encrypted bytes, while the last '16 - ptr' bytes of
 *    buf[] are the remnants of the stream block, to be used against
 *    the next input bytes, when available.
 *
 *  - The current counter and running CBC-MAC values are kept in 'ctr'
 *    and 'cbcmac', respectively.
 *
 *  - The derived keys for padding are kept in L2 and L4 (double and
 *    quadruple of Enc_K(0^n), in GF(2^128), respectively).
 */

/*
 * Start an OMAC computation; the first block is the big-endian
 * representation of the provided value ('val' must fit on one byte).
 * We make it a delayed block because it may also be the last one,
 */
static void
omac_start(br_eax_context *ctx, unsigned val)
{
	memset(ctx->cbcmac, 0, sizeof ctx->cbcmac);
	memset(ctx->buf, 0, sizeof ctx->buf);
	ctx->buf[15] = val;
	ctx->ptr = 16;
}

/*
 * Double a value in finite field GF(2^128), defined with modulus
 * X^128+X^7+X^2+X+1.
 */
static void
double_gf128(unsigned char *dst, const unsigned char *src)
{
	unsigned cc;
	int i;

	cc = 0x87 & -((unsigned)src[0] >> 7);
	for (i = 15; i >= 0; i --) {
		unsigned z;

		z = (src[i] << 1) ^ cc;
		cc = z >> 8;
		dst[i] = (unsigned char)z;
	}
}

/*
 * Apply padding to the last block, currently in ctx->buf (with
 * ctx->ptr bytes), and finalize OMAC computation.
 */
static void
do_pad(br_eax_context *ctx)
{
	unsigned char *pad;
	size_t ptr, u;

	ptr = ctx->ptr;
	if (ptr == 16) {
		pad = ctx->L2;
	} else {
		ctx->buf[ptr ++] = 0x80;
		memset(ctx->buf + ptr, 0x00, 16 - ptr);
		pad = ctx->L4;
	}
	for (u = 0; u < sizeof ctx->buf; u ++) {
		ctx->buf[u] ^= pad[u];
	}
	(*ctx->bctx)->mac(ctx->bctx, ctx->cbcmac, ctx->buf, sizeof ctx->buf);
}

/*
 * Apply CBC-MAC on the provided data, with buffering management.
 *
 * Upon entry, two situations are acceptable:
 *
 *   ctx->ptr == 0: there is no data to process in ctx->buf
 *   ctx->ptr == 16: there is a full block of unprocessed data in ctx->buf
 *
 * Upon exit, ctx->ptr may be zero only if it was already zero on entry,
 * and len == 0. In all other situations, ctx->ptr will be non-zero on
 * exit (and may have value 16).
 */
static void
do_cbcmac_chunk(br_eax_context *ctx, const void *data, size_t len)
{
	size_t ptr;

	if (len == 0) {
		return;
	}
	ptr = len & (size_t)15;
	if (ptr == 0) {
		len -= 16;
		ptr = 16;
	} else {
		len -= ptr;
	}
	if (ctx->ptr == 16) {
		(*ctx->bctx)->mac(ctx->bctx, ctx->cbcmac,
			ctx->buf, sizeof ctx->buf);
	}
	(*ctx->bctx)->mac(ctx->bctx, ctx->cbcmac, data, len);
	memcpy(ctx->buf, (const unsigned char *)data + len, ptr);
	ctx->ptr = ptr;
}

/* see bearssl_aead.h */
void
br_eax_init(br_eax_context *ctx, const br_block_ctrcbc_class **bctx)
{
	unsigned char tmp[16], iv[16];

	ctx->vtable = &br_eax_vtable;
	ctx->bctx = bctx;

	/*
	 * Encrypt a whole-zero block to compute L2 and L4.
	 */
	memset(tmp, 0, sizeof tmp);
	memset(iv, 0, sizeof iv);
	(*bctx)->ctr(bctx, iv, tmp, sizeof tmp);
	double_gf128(ctx->L2, tmp);
	double_gf128(ctx->L4, ctx->L2);
}

/* see bearssl_aead.h */
void
br_eax_capture(const br_eax_context *ctx, br_eax_state *st)
{
	/*
	 * We capture the three OMAC* states _after_ processing the
	 * initial block (assuming that nonce, message and AAD are
	 * all non-empty).
	 */
	int i;

	memset(st->st, 0, sizeof st->st);
	for (i = 0; i < 3; i ++) {
		unsigned char tmp[16];

		memset(tmp, 0, sizeof tmp);
		tmp[15] = (unsigned char)i;
		(*ctx->bctx)->mac(ctx->bctx, st->st[i], tmp, sizeof tmp);
	}
}

/* see bearssl_aead.h */
void
br_eax_reset(br_eax_context *ctx, const void *nonce, size_t len)
{
	/*
	 * Process nonce with OMAC^0.
	 */
	omac_start(ctx, 0);
	do_cbcmac_chunk(ctx, nonce, len);
	do_pad(ctx);
	memcpy(ctx->nonce, ctx->cbcmac, sizeof ctx->cbcmac);

	/*
	 * Start OMAC^1 for the AAD ("header" in the EAX specification).
	 */
	omac_start(ctx, 1);

	/*
	 * We use ctx->head[0] as temporary flag to mark that we are
	 * using a "normal" reset().
	 */
	ctx->head[0] = 0;
}

/* see bearssl_aead.h */
void
br_eax_reset_pre_aad(br_eax_context *ctx, const br_eax_state *st,
	const void *nonce, size_t len)
{
	if (len == 0) {
		omac_start(ctx, 0);
	} else {
		memcpy(ctx->cbcmac, st->st[0], sizeof ctx->cbcmac);
		ctx->ptr = 0;
		do_cbcmac_chunk(ctx, nonce, len);
	}
	do_pad(ctx);
	memcpy(ctx->nonce, ctx->cbcmac, sizeof ctx->cbcmac);

	memcpy(ctx->cbcmac, st->st[1], sizeof ctx->cbcmac);
	ctx->ptr = 0;

	memcpy(ctx->ctr, st->st[2], sizeof ctx->ctr);

	/*
	 * We use ctx->head[0] as a flag to indicate that we use a
	 * a recorded state, with ctx->ctr containing the preprocessed
	 * first block for OMAC^2.
	 */
	ctx->head[0] = 1;
}

/* see bearssl_aead.h */
void
br_eax_reset_post_aad(br_eax_context *ctx, const br_eax_state *st,
	const void *nonce, size_t len)
{
	if (len == 0) {
		omac_start(ctx, 0);
	} else {
		memcpy(ctx->cbcmac, st->st[0], sizeof ctx->cbcmac);
		ctx->ptr = 0;
		do_cbcmac_chunk(ctx, nonce, len);
	}
	do_pad(ctx);
	memcpy(ctx->nonce, ctx->cbcmac, sizeof ctx->cbcmac);
	memcpy(ctx->ctr, ctx->nonce, sizeof ctx->nonce);

	memcpy(ctx->head, st->st[1], sizeof ctx->head);

	memcpy(ctx->cbcmac, st->st[2], sizeof ctx->cbcmac);
	ctx->ptr = 0;
}

/* see bearssl_aead.h */
void
br_eax_aad_inject(br_eax_context *ctx, const void *data, size_t len)
{
	size_t ptr;

	ptr = ctx->ptr;

	/*
	 * If there is a partial block, first complete it.
	 */
	if (ptr < 16) {
		size_t clen;

		clen = 16 - ptr;
		if (len <= clen) {
			memcpy(ctx->buf + ptr, data, len);
			ctx->ptr = ptr + len;
			return;
		}
		memcpy(ctx->buf + ptr, data, clen);
		data = (const unsigned char *)data + clen;
		len -= clen;
	}

	/*
	 * We now have a full block in buf[], and this is not the last
	 * block.
	 */
	do_cbcmac_chunk(ctx, data, len);
}

/* see bearssl_aead.h */
void
br_eax_flip(br_eax_context *ctx)
{
	int from_capture;

	/*
	 * ctx->head[0] may be non-zero if the context was reset with
	 * a pre-AAD captured state. In that case, ctx->ctr[] contains
	 * the state for OMAC^2 _after_ processing the first block.
	 */
	from_capture = ctx->head[0];

	/*
	 * Complete the OMAC computation on the AAD.
	 */
	do_pad(ctx);
	memcpy(ctx->head, ctx->cbcmac, sizeof ctx->cbcmac);

	/*
	 * Start OMAC^2 for the encrypted data.
	 * If the context was initialized from a captured state, then
	 * the OMAC^2 value is in the ctr[] array.
	 */
	if (from_capture) {
		memcpy(ctx->cbcmac, ctx->ctr, sizeof ctx->cbcmac);
		ctx->ptr = 0;
	} else {
		omac_start(ctx, 2);
	}

	/*
	 * Initial counter value for CTR is the processed nonce.
	 */
	memcpy(ctx->ctr, ctx->nonce, sizeof ctx->nonce);
}

/* see bearssl_aead.h */
void
br_eax_run(br_eax_context *ctx, int encrypt, void *data, size_t len)
{
	unsigned char *dbuf;
	size_t ptr;

	/*
	 * Ensure that there is actual data to process.
	 */
	if (len == 0) {
		return;
	}

	dbuf = data;
	ptr = ctx->ptr;

	/*
	 * We may have ptr == 0 here if we initialized from a captured
	 * state. In that case, there is no partially consumed block
	 * or unprocessed data.
	 */
	if (ptr != 0 && ptr != 16) {
		/*
		 * We have a partially consumed block.
		 */
		size_t u, clen;

		clen = 16 - ptr;
		if (len <= clen) {
			clen = len;
		}
		if (encrypt) {
			for (u = 0; u < clen; u ++) {
				ctx->buf[ptr + u] ^= dbuf[u];
			}
			memcpy(dbuf, ctx->buf + ptr, clen);
		} else {
			for (u = 0; u < clen; u ++) {
				unsigned dx, sx;

				sx = ctx->buf[ptr + u];
				dx = dbuf[u];
				ctx->buf[ptr + u] = dx;
				dbuf[u] = sx ^ dx;
			}
		}

		if (len <= clen) {
			ctx->ptr = ptr + clen;
			return;
		}
		dbuf += clen;
		len -= clen;
	}

	/*
	 * We now have a complete encrypted block in buf[] that must still
	 * be processed with OMAC, and this is not the final buf.
	 * Exception: when ptr == 0, no block has been produced yet.
	 */
	if (ptr != 0) {
		(*ctx->bctx)->mac(ctx->bctx, ctx->cbcmac,
			ctx->buf, sizeof ctx->buf);
	}

	/*
	 * Do CTR encryption or decryption and CBC-MAC for all full blocks
	 * except the last.
	 */
	ptr = len & (size_t)15;
	if (ptr == 0) {
		len -= 16;
		ptr = 16;
	} else {
		len -= ptr;
	}
	if (encrypt) {
		(*ctx->bctx)->encrypt(ctx->bctx, ctx->ctr, ctx->cbcmac,
			dbuf, len);
	} else {
		(*ctx->bctx)->decrypt(ctx->bctx, ctx->ctr, ctx->cbcmac,
			dbuf, len);
	}
	dbuf += len;

	/*
	 * Compute next block of CTR stream, and use it to finish
	 * encrypting or decrypting the data.
	 */
	memset(ctx->buf, 0, sizeof ctx->buf);
	(*ctx->bctx)->ctr(ctx->bctx, ctx->ctr, ctx->buf, sizeof ctx->buf);
	if (encrypt) {
		size_t u;

		for (u = 0; u < ptr; u ++) {
			ctx->buf[u] ^= dbuf[u];
		}
		memcpy(dbuf, ctx->buf, ptr);
	} else {
		size_t u;

		for (u = 0; u < ptr; u ++) {
			unsigned dx, sx;

			sx = ctx->buf[u];
			dx = dbuf[u];
			ctx->buf[u] = dx;
			dbuf[u] = sx ^ dx;
		}
	}
	ctx->ptr = ptr;
}

/*
 * Complete tag computation. The final tag is written in ctx->cbcmac.
 */
static void
do_final(br_eax_context *ctx)
{
	size_t u;

	do_pad(ctx);

	/*
	 * Authentication tag is the XOR of the three OMAC outputs for
	 * the nonce, AAD and encrypted data.
	 */
	for (u = 0; u < 16; u ++) {
		ctx->cbcmac[u] ^= ctx->nonce[u] ^ ctx->head[u];
	}
}

/* see bearssl_aead.h */
void
br_eax_get_tag(br_eax_context *ctx, void *tag)
{
	do_final(ctx);
	memcpy(tag, ctx->cbcmac, sizeof ctx->cbcmac);
}

/* see bearssl_aead.h */
void
br_eax_get_tag_trunc(br_eax_context *ctx, void *tag, size_t len)
{
	do_final(ctx);
	memcpy(tag, ctx->cbcmac, len);
}

/* see bearssl_aead.h */
uint32_t
br_eax_check_tag_trunc(br_eax_context *ctx, const void *tag, size_t len)
{
	unsigned char tmp[16];
	size_t u;
	int x;

	br_eax_get_tag(ctx, tmp);
	x = 0;
	for (u = 0; u < len; u ++) {
		x |= tmp[u] ^ ((const unsigned char *)tag)[u];
	}
	return EQ0(x);
}

/* see bearssl_aead.h */
uint32_t
br_eax_check_tag(br_eax_context *ctx, const void *tag)
{
	return br_eax_check_tag_trunc(ctx, tag, 16);
}

/* see bearssl_aead.h */
const br_aead_class br_eax_vtable = {
	16,
	(void (*)(const br_aead_class **, const void *, size_t))
		&br_eax_reset,
	(void (*)(const br_aead_class **, const void *, size_t))
		&br_eax_aad_inject,
	(void (*)(const br_aead_class **))
		&br_eax_flip,
	(void (*)(const br_aead_class **, int, void *, size_t))
		&br_eax_run,
	(void (*)(const br_aead_class **, void *))
		&br_eax_get_tag,
	(uint32_t (*)(const br_aead_class **, const void *))
		&br_eax_check_tag,
	(void (*)(const br_aead_class **, void *, size_t))
		&br_eax_get_tag_trunc,
	(uint32_t (*)(const br_aead_class **, const void *, size_t))
		&br_eax_check_tag_trunc
};
