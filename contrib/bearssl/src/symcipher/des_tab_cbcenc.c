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
br_des_tab_cbcenc_init(br_des_tab_cbcenc_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_des_tab_cbcenc_vtable;
	ctx->num_rounds = br_des_tab_keysched(ctx->skey, key, len);
}

/* see bearssl_block.h */
void
br_des_tab_cbcenc_run(const br_des_tab_cbcenc_keys *ctx,
	void *iv, void *data, size_t len)
{
	unsigned char *buf, *ivbuf;

	ivbuf = iv;
	buf = data;
	while (len > 0) {
		int i;

		for (i = 0; i < 8; i ++) {
			buf[i] ^= ivbuf[i];
		}
		br_des_tab_process_block(ctx->num_rounds, ctx->skey, buf);
		memcpy(ivbuf, buf, 8);
		buf += 8;
		len -= 8;
	}
}

/* see bearssl_block.h */
const br_block_cbcenc_class br_des_tab_cbcenc_vtable = {
	sizeof(br_des_tab_cbcenc_keys),
	8,
	3,
	(void (*)(const br_block_cbcenc_class **, const void *, size_t))
		&br_des_tab_cbcenc_init,
	(void (*)(const br_block_cbcenc_class *const *, void *, void *, size_t))
		&br_des_tab_cbcenc_run
};
