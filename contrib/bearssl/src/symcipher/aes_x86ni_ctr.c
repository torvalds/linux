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
const br_block_ctr_class *
br_aes_x86ni_ctr_get_vtable(void)
{
	return br_aes_x86ni_supported() ? &br_aes_x86ni_ctr_vtable : NULL;
}

/* see bearssl_block.h */
void
br_aes_x86ni_ctr_init(br_aes_x86ni_ctr_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_aes_x86ni_ctr_vtable;
	ctx->num_rounds = br_aes_x86ni_keysched_enc(ctx->skey.skni, key, len);
}

BR_TARGETS_X86_UP

/* see bearssl_block.h */
BR_TARGET("sse2,sse4.1,aes")
uint32_t
br_aes_x86ni_ctr_run(const br_aes_x86ni_ctr_keys *ctx,
	const void *iv, uint32_t cc, void *data, size_t len)
{
	unsigned char *buf;
	unsigned char ivbuf[16];
	unsigned num_rounds;
	__m128i sk[15];
	__m128i ivx;
	unsigned u;

	buf = data;
	memcpy(ivbuf, iv, 12);
	num_rounds = ctx->num_rounds;
	for (u = 0; u <= num_rounds; u ++) {
		sk[u] = _mm_loadu_si128((void *)(ctx->skey.skni + (u << 4)));
	}
	ivx = _mm_loadu_si128((void *)ivbuf);
	while (len > 0) {
		__m128i x0, x1, x2, x3;

		x0 = _mm_insert_epi32(ivx, br_bswap32(cc + 0), 3);
		x1 = _mm_insert_epi32(ivx, br_bswap32(cc + 1), 3);
		x2 = _mm_insert_epi32(ivx, br_bswap32(cc + 2), 3);
		x3 = _mm_insert_epi32(ivx, br_bswap32(cc + 3), 3);
		x0 = _mm_xor_si128(x0, sk[0]);
		x1 = _mm_xor_si128(x1, sk[0]);
		x2 = _mm_xor_si128(x2, sk[0]);
		x3 = _mm_xor_si128(x3, sk[0]);
		x0 = _mm_aesenc_si128(x0, sk[1]);
		x1 = _mm_aesenc_si128(x1, sk[1]);
		x2 = _mm_aesenc_si128(x2, sk[1]);
		x3 = _mm_aesenc_si128(x3, sk[1]);
		x0 = _mm_aesenc_si128(x0, sk[2]);
		x1 = _mm_aesenc_si128(x1, sk[2]);
		x2 = _mm_aesenc_si128(x2, sk[2]);
		x3 = _mm_aesenc_si128(x3, sk[2]);
		x0 = _mm_aesenc_si128(x0, sk[3]);
		x1 = _mm_aesenc_si128(x1, sk[3]);
		x2 = _mm_aesenc_si128(x2, sk[3]);
		x3 = _mm_aesenc_si128(x3, sk[3]);
		x0 = _mm_aesenc_si128(x0, sk[4]);
		x1 = _mm_aesenc_si128(x1, sk[4]);
		x2 = _mm_aesenc_si128(x2, sk[4]);
		x3 = _mm_aesenc_si128(x3, sk[4]);
		x0 = _mm_aesenc_si128(x0, sk[5]);
		x1 = _mm_aesenc_si128(x1, sk[5]);
		x2 = _mm_aesenc_si128(x2, sk[5]);
		x3 = _mm_aesenc_si128(x3, sk[5]);
		x0 = _mm_aesenc_si128(x0, sk[6]);
		x1 = _mm_aesenc_si128(x1, sk[6]);
		x2 = _mm_aesenc_si128(x2, sk[6]);
		x3 = _mm_aesenc_si128(x3, sk[6]);
		x0 = _mm_aesenc_si128(x0, sk[7]);
		x1 = _mm_aesenc_si128(x1, sk[7]);
		x2 = _mm_aesenc_si128(x2, sk[7]);
		x3 = _mm_aesenc_si128(x3, sk[7]);
		x0 = _mm_aesenc_si128(x0, sk[8]);
		x1 = _mm_aesenc_si128(x1, sk[8]);
		x2 = _mm_aesenc_si128(x2, sk[8]);
		x3 = _mm_aesenc_si128(x3, sk[8]);
		x0 = _mm_aesenc_si128(x0, sk[9]);
		x1 = _mm_aesenc_si128(x1, sk[9]);
		x2 = _mm_aesenc_si128(x2, sk[9]);
		x3 = _mm_aesenc_si128(x3, sk[9]);
		if (num_rounds == 10) {
			x0 = _mm_aesenclast_si128(x0, sk[10]);
			x1 = _mm_aesenclast_si128(x1, sk[10]);
			x2 = _mm_aesenclast_si128(x2, sk[10]);
			x3 = _mm_aesenclast_si128(x3, sk[10]);
		} else if (num_rounds == 12) {
			x0 = _mm_aesenc_si128(x0, sk[10]);
			x1 = _mm_aesenc_si128(x1, sk[10]);
			x2 = _mm_aesenc_si128(x2, sk[10]);
			x3 = _mm_aesenc_si128(x3, sk[10]);
			x0 = _mm_aesenc_si128(x0, sk[11]);
			x1 = _mm_aesenc_si128(x1, sk[11]);
			x2 = _mm_aesenc_si128(x2, sk[11]);
			x3 = _mm_aesenc_si128(x3, sk[11]);
			x0 = _mm_aesenclast_si128(x0, sk[12]);
			x1 = _mm_aesenclast_si128(x1, sk[12]);
			x2 = _mm_aesenclast_si128(x2, sk[12]);
			x3 = _mm_aesenclast_si128(x3, sk[12]);
		} else {
			x0 = _mm_aesenc_si128(x0, sk[10]);
			x1 = _mm_aesenc_si128(x1, sk[10]);
			x2 = _mm_aesenc_si128(x2, sk[10]);
			x3 = _mm_aesenc_si128(x3, sk[10]);
			x0 = _mm_aesenc_si128(x0, sk[11]);
			x1 = _mm_aesenc_si128(x1, sk[11]);
			x2 = _mm_aesenc_si128(x2, sk[11]);
			x3 = _mm_aesenc_si128(x3, sk[11]);
			x0 = _mm_aesenc_si128(x0, sk[12]);
			x1 = _mm_aesenc_si128(x1, sk[12]);
			x2 = _mm_aesenc_si128(x2, sk[12]);
			x3 = _mm_aesenc_si128(x3, sk[12]);
			x0 = _mm_aesenc_si128(x0, sk[13]);
			x1 = _mm_aesenc_si128(x1, sk[13]);
			x2 = _mm_aesenc_si128(x2, sk[13]);
			x3 = _mm_aesenc_si128(x3, sk[13]);
			x0 = _mm_aesenclast_si128(x0, sk[14]);
			x1 = _mm_aesenclast_si128(x1, sk[14]);
			x2 = _mm_aesenclast_si128(x2, sk[14]);
			x3 = _mm_aesenclast_si128(x3, sk[14]);
		}
		if (len >= 64) {
			x0 = _mm_xor_si128(x0,
				_mm_loadu_si128((void *)(buf +  0)));
			x1 = _mm_xor_si128(x1,
				_mm_loadu_si128((void *)(buf + 16)));
			x2 = _mm_xor_si128(x2,
				_mm_loadu_si128((void *)(buf + 32)));
			x3 = _mm_xor_si128(x3,
				_mm_loadu_si128((void *)(buf + 48)));
			_mm_storeu_si128((void *)(buf +  0), x0);
			_mm_storeu_si128((void *)(buf + 16), x1);
			_mm_storeu_si128((void *)(buf + 32), x2);
			_mm_storeu_si128((void *)(buf + 48), x3);
			buf += 64;
			len -= 64;
			cc += 4;
		} else {
			unsigned char tmp[64];

			_mm_storeu_si128((void *)(tmp +  0), x0);
			_mm_storeu_si128((void *)(tmp + 16), x1);
			_mm_storeu_si128((void *)(tmp + 32), x2);
			_mm_storeu_si128((void *)(tmp + 48), x3);
			for (u = 0; u < len; u ++) {
				buf[u] ^= tmp[u];
			}
			cc += (uint32_t)len >> 4;
			break;
		}
	}
	return cc;
}

BR_TARGETS_X86_DOWN

/* see bearssl_block.h */
const br_block_ctr_class br_aes_x86ni_ctr_vtable = {
	sizeof(br_aes_x86ni_ctr_keys),
	16,
	4,
	(void (*)(const br_block_ctr_class **, const void *, size_t))
		&br_aes_x86ni_ctr_init,
	(uint32_t (*)(const br_block_ctr_class *const *,
		const void *, uint32_t, void *, size_t))
		&br_aes_x86ni_ctr_run
};

#else

/* see bearssl_block.h */
const br_block_ctr_class *
br_aes_x86ni_ctr_get_vtable(void)
{
	return NULL;
}

#endif
