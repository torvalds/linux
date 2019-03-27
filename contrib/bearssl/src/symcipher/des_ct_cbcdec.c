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
br_des_ct_cbcdec_init(br_des_ct_cbcdec_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_des_ct_cbcdec_vtable;
	ctx->num_rounds = br_des_ct_keysched(ctx->skey, key, len);
	if (len == 8) {
		br_des_rev_skey(ctx->skey);
	} else {
		int i;

		for (i = 0; i < 48; i += 2) {
			uint32_t t;

			t = ctx->skey[i];
			ctx->skey[i] = ctx->skey[94 - i];
			ctx->skey[94 - i] = t;
			t = ctx->skey[i + 1];
			ctx->skey[i + 1] = ctx->skey[95 - i];
			ctx->skey[95 - i] = t;
		}
	}
}

/* see bearssl_block.h */
void
br_des_ct_cbcdec_run(const br_des_ct_cbcdec_keys *ctx,
	void *iv, void *data, size_t len)
{
	unsigned char *buf, *ivbuf;
	uint32_t sk_exp[288];

	br_des_ct_skey_expand(sk_exp, ctx->num_rounds, ctx->skey);
	ivbuf = iv;
	buf = data;
	while (len > 0) {
		unsigned char tmp[8];
		int i;

		memcpy(tmp, buf, 8);
		br_des_ct_process_block(ctx->num_rounds, sk_exp, buf);
		for (i = 0; i < 8; i ++) {
			buf[i] ^= ivbuf[i];
		}
		memcpy(ivbuf, tmp, 8);
		buf += 8;
		len -= 8;
	}
}

/* see bearssl_block.h */
const br_block_cbcdec_class br_des_ct_cbcdec_vtable = {
	sizeof(br_des_ct_cbcdec_keys),
	8,
	3,
	(void (*)(const br_block_cbcdec_class **, const void *, size_t))
		&br_des_ct_cbcdec_init,
	(void (*)(const br_block_cbcdec_class *const *, void *, void *, size_t))
		&br_des_ct_cbcdec_run
};
