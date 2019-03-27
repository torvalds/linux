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

/* see bearssl_block.h */
void
br_aes_ct_cbcenc_init(br_aes_ct_cbcenc_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_aes_ct_cbcenc_vtable;
	ctx->num_rounds = br_aes_ct_keysched(ctx->skey, key, len);
}

/* see bearssl_block.h */
void
br_aes_ct_cbcenc_run(const br_aes_ct_cbcenc_keys *ctx,
	void *iv, void *data, size_t len)
{
	unsigned char *buf, *ivbuf;
	uint32_t q[8];
	uint32_t iv0, iv1, iv2, iv3;
	uint32_t sk_exp[120];

	q[1] = 0;
	q[3] = 0;
	q[5] = 0;
	q[7] = 0;
	br_aes_ct_skey_expand(sk_exp, ctx->num_rounds, ctx->skey);
	ivbuf = iv;
	iv0 = br_dec32le(ivbuf);
	iv1 = br_dec32le(ivbuf + 4);
	iv2 = br_dec32le(ivbuf + 8);
	iv3 = br_dec32le(ivbuf + 12);
	buf = data;
	while (len > 0) {
		q[0] = iv0 ^ br_dec32le(buf);
		q[2] = iv1 ^ br_dec32le(buf + 4);
		q[4] = iv2 ^ br_dec32le(buf + 8);
		q[6] = iv3 ^ br_dec32le(buf + 12);
		br_aes_ct_ortho(q);
		br_aes_ct_bitslice_encrypt(ctx->num_rounds, sk_exp, q);
		br_aes_ct_ortho(q);
		iv0 = q[0];
		iv1 = q[2];
		iv2 = q[4];
		iv3 = q[6];
		br_enc32le(buf, iv0);
		br_enc32le(buf + 4, iv1);
		br_enc32le(buf + 8, iv2);
		br_enc32le(buf + 12, iv3);
		buf += 16;
		len -= 16;
	}
	br_enc32le(ivbuf, iv0);
	br_enc32le(ivbuf + 4, iv1);
	br_enc32le(ivbuf + 8, iv2);
	br_enc32le(ivbuf + 12, iv3);
}

/* see bearssl_block.h */
const br_block_cbcenc_class br_aes_ct_cbcenc_vtable = {
	sizeof(br_aes_ct_cbcenc_keys),
	16,
	4,
	(void (*)(const br_block_cbcenc_class **, const void *, size_t))
		&br_aes_ct_cbcenc_init,
	(void (*)(const br_block_cbcenc_class *const *, void *, void *, size_t))
		&br_aes_ct_cbcenc_run
};
