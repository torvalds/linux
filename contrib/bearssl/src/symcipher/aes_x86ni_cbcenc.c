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

#define BR_ENABLE_INTRINSICS   1
#include "inner.h"

#if BR_AES_X86NI

/* see bearssl_block.h */
const br_block_cbcenc_class *
br_aes_x86ni_cbcenc_get_vtable(void)
{
	return br_aes_x86ni_supported() ? &br_aes_x86ni_cbcenc_vtable : NULL;
}

/* see bearssl_block.h */
void
br_aes_x86ni_cbcenc_init(br_aes_x86ni_cbcenc_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_aes_x86ni_cbcenc_vtable;
	ctx->num_rounds = br_aes_x86ni_keysched_enc(ctx->skey.skni, key, len);
}

BR_TARGETS_X86_UP

/* see bearssl_block.h */
BR_TARGET("sse2,aes")
void
br_aes_x86ni_cbcenc_run(const br_aes_x86ni_cbcenc_keys *ctx,
	void *iv, void *data, size_t len)
{
	unsigned char *buf;
	unsigned num_rounds;
	__m128i sk[15], ivx;
	unsigned u;

	buf = data;
	ivx = _mm_loadu_si128(iv);
	num_rounds = ctx->num_rounds;
	for (u = 0; u <= num_rounds; u ++) {
		sk[u] = _mm_loadu_si128((void *)(ctx->skey.skni + (u << 4)));
	}
	while (len > 0) {
		__m128i x;

		x = _mm_xor_si128(_mm_loadu_si128((void *)buf), ivx);
		x = _mm_xor_si128(x, sk[0]);
		x = _mm_aesenc_si128(x, sk[1]);
		x = _mm_aesenc_si128(x, sk[2]);
		x = _mm_aesenc_si128(x, sk[3]);
		x = _mm_aesenc_si128(x, sk[4]);
		x = _mm_aesenc_si128(x, sk[5]);
		x = _mm_aesenc_si128(x, sk[6]);
		x = _mm_aesenc_si128(x, sk[7]);
		x = _mm_aesenc_si128(x, sk[8]);
		x = _mm_aesenc_si128(x, sk[9]);
		if (num_rounds == 10) {
			x = _mm_aesenclast_si128(x, sk[10]);
		} else if (num_rounds == 12) {
			x = _mm_aesenc_si128(x, sk[10]);
			x = _mm_aesenc_si128(x, sk[11]);
			x = _mm_aesenclast_si128(x, sk[12]);
		} else {
			x = _mm_aesenc_si128(x, sk[10]);
			x = _mm_aesenc_si128(x, sk[11]);
			x = _mm_aesenc_si128(x, sk[12]);
			x = _mm_aesenc_si128(x, sk[13]);
			x = _mm_aesenclast_si128(x, sk[14]);
		}
		ivx = x;
		_mm_storeu_si128((void *)buf, x);
		buf += 16;
		len -= 16;
	}
	_mm_storeu_si128(iv, ivx);
}

BR_TARGETS_X86_DOWN

/* see bearssl_block.h */
const br_block_cbcenc_class br_aes_x86ni_cbcenc_vtable = {
	sizeof(br_aes_x86ni_cbcenc_keys),
	16,
	4,
	(void (*)(const br_block_cbcenc_class **, const void *, size_t))
		&br_aes_x86ni_cbcenc_init,
	(void (*)(const br_block_cbcenc_class *const *, void *, void *, size_t))
		&br_aes_x86ni_cbcenc_run
};

#else

/* see bearssl_block.h */
const br_block_cbcenc_class *
br_aes_x86ni_cbcenc_get_vtable(void)
{
	return NULL;
}

#endif
