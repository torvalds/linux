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
br_aes_small_ctr_init(br_aes_small_ctr_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_aes_small_ctr_vtable;
	ctx->num_rounds = br_aes_keysched(ctx->skey, key, len);
}

static void
xorbuf(void *dst, const void *src, size_t len)
{
	unsigned char *d;
	const unsigned char *s;

	d = dst;
	s = src;
	while (len -- > 0) {
		*d ++ ^= *s ++;
	}
}

/* see bearssl_block.h */
uint32_t
br_aes_small_ctr_run(const br_aes_small_ctr_keys *ctx,
	const void *iv, uint32_t cc, void *data, size_t len)
{
	unsigned char *buf;

	buf = data;
	while (len > 0) {
		unsigned char tmp[16];

		memcpy(tmp, iv, 12);
		br_enc32be(tmp + 12, cc ++);
		br_aes_small_encrypt(ctx->num_rounds, ctx->skey, tmp);
		if (len <= 16) {
			xorbuf(buf, tmp, len);
			break;
		}
		xorbuf(buf, tmp, 16);
		buf += 16;
		len -= 16;
	}
	return cc;
}

/* see bearssl_block.h */
const br_block_ctr_class br_aes_small_ctr_vtable = {
	sizeof(br_aes_small_ctr_keys),
	16,
	4,
	(void (*)(const br_block_ctr_class **, const void *, size_t))
		&br_aes_small_ctr_init,
	(uint32_t (*)(const br_block_ctr_class *const *,
		const void *, uint32_t, void *, size_t))
		&br_aes_small_ctr_run
};
