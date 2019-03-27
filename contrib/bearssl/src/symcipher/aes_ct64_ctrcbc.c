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

/* see bearssl_block.h */
void
br_aes_ct64_ctrcbc_init(br_aes_ct64_ctrcbc_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_aes_ct64_ctrcbc_vtable;
	ctx->num_rounds = br_aes_ct64_keysched(ctx->skey, key, len);
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
void
br_aes_ct64_ctrcbc_ctr(const br_aes_ct64_ctrcbc_keys *ctx,
	void *ctr, void *data, size_t len)
{
	unsigned char *buf;
	unsigned char *ivbuf;
	uint32_t iv0, iv1, iv2, iv3;
	uint64_t sk_exp[120];

	br_aes_ct64_skey_expand(sk_exp, ctx->num_rounds, ctx->skey);

	/*
	 * We keep the counter as four 32-bit values, with big-endian
	 * convention, because that's what is expected for purposes of
	 * incrementing the counter value.
	 */
	ivbuf = ctr;
	iv0 = br_dec32be(ivbuf +  0);
	iv1 = br_dec32be(ivbuf +  4);
	iv2 = br_dec32be(ivbuf +  8);
	iv3 = br_dec32be(ivbuf + 12);

	buf = data;
	while (len > 0) {
		uint64_t q[8];
		uint32_t w[16];
		unsigned char tmp[64];
		int i, j;

		/*
		 * The bitslice implementation expects values in
		 * little-endian convention, so we have to byteswap them.
		 */
		j = (len >= 64) ? 16 : (int)(len >> 2);
		for (i = 0; i < j; i += 4) {
			uint32_t carry;

			w[i + 0] = br_swap32(iv0);
			w[i + 1] = br_swap32(iv1);
			w[i + 2] = br_swap32(iv2);
			w[i + 3] = br_swap32(iv3);
			iv3 ++;
			carry = ~(iv3 | -iv3) >> 31;
			iv2 += carry;
			carry &= -(~(iv2 | -iv2) >> 31);
			iv1 += carry;
			carry &= -(~(iv1 | -iv1) >> 31);
			iv0 += carry;
		}
		memset(w + i, 0, (16 - i) * sizeof(uint32_t));

		for (i = 0; i < 4; i ++) {
			br_aes_ct64_interleave_in(
				&q[i], &q[i + 4], w + (i << 2));
		}
		br_aes_ct64_ortho(q);
		br_aes_ct64_bitslice_encrypt(ctx->num_rounds, sk_exp, q);
		br_aes_ct64_ortho(q);
		for (i = 0; i < 4; i ++) {
			br_aes_ct64_interleave_out(
				w + (i << 2), q[i], q[i + 4]);
		}

		br_range_enc32le(tmp, w, 16);
		if (len <= 64) {
			xorbuf(buf, tmp, len);
			break;
		}
		xorbuf(buf, tmp, 64);
		buf += 64;
		len -= 64;
	}
	br_enc32be(ivbuf +  0, iv0);
	br_enc32be(ivbuf +  4, iv1);
	br_enc32be(ivbuf +  8, iv2);
	br_enc32be(ivbuf + 12, iv3);
}

/* see bearssl_block.h */
void
br_aes_ct64_ctrcbc_mac(const br_aes_ct64_ctrcbc_keys *ctx,
	void *cbcmac, const void *data, size_t len)
{
	const unsigned char *buf;
	uint32_t cm0, cm1, cm2, cm3;
	uint64_t q[8];
	uint64_t sk_exp[120];

	br_aes_ct64_skey_expand(sk_exp, ctx->num_rounds, ctx->skey);

	cm0 = br_dec32le((unsigned char *)cbcmac +  0);
	cm1 = br_dec32le((unsigned char *)cbcmac +  4);
	cm2 = br_dec32le((unsigned char *)cbcmac +  8);
	cm3 = br_dec32le((unsigned char *)cbcmac + 12);

	buf = data;
	memset(q, 0, sizeof q);
	while (len > 0) {
		uint32_t w[4];

		w[0] = cm0 ^ br_dec32le(buf +  0);
		w[1] = cm1 ^ br_dec32le(buf +  4);
		w[2] = cm2 ^ br_dec32le(buf +  8);
		w[3] = cm3 ^ br_dec32le(buf + 12);

		br_aes_ct64_interleave_in(&q[0], &q[4], w);
		br_aes_ct64_ortho(q);
		br_aes_ct64_bitslice_encrypt(ctx->num_rounds, sk_exp, q);
		br_aes_ct64_ortho(q);
		br_aes_ct64_interleave_out(w, q[0], q[4]);

		cm0 = w[0];
		cm1 = w[1];
		cm2 = w[2];
		cm3 = w[3];
		buf += 16;
		len -= 16;
	}

	br_enc32le((unsigned char *)cbcmac +  0, cm0);
	br_enc32le((unsigned char *)cbcmac +  4, cm1);
	br_enc32le((unsigned char *)cbcmac +  8, cm2);
	br_enc32le((unsigned char *)cbcmac + 12, cm3);
}

/* see bearssl_block.h */
void
br_aes_ct64_ctrcbc_encrypt(const br_aes_ct64_ctrcbc_keys *ctx,
	void *ctr, void *cbcmac, void *data, size_t len)
{
	/*
	 * When encrypting, the CBC-MAC processing must be lagging by
	 * one block, since it operates on the encrypted values, so
	 * it must wait for that encryption to complete.
	 */

	unsigned char *buf;
	unsigned char *ivbuf;
	uint32_t iv0, iv1, iv2, iv3;
	uint32_t cm0, cm1, cm2, cm3;
	uint64_t sk_exp[120];
	uint64_t q[8];
	int first_iter;

	br_aes_ct64_skey_expand(sk_exp, ctx->num_rounds, ctx->skey);

	/*
	 * We keep the counter as four 32-bit values, with big-endian
	 * convention, because that's what is expected for purposes of
	 * incrementing the counter value.
	 */
	ivbuf = ctr;
	iv0 = br_dec32be(ivbuf +  0);
	iv1 = br_dec32be(ivbuf +  4);
	iv2 = br_dec32be(ivbuf +  8);
	iv3 = br_dec32be(ivbuf + 12);

	/*
	 * The current CBC-MAC value is kept in little-endian convention.
	 */
	cm0 = br_dec32le((unsigned char *)cbcmac +  0);
	cm1 = br_dec32le((unsigned char *)cbcmac +  4);
	cm2 = br_dec32le((unsigned char *)cbcmac +  8);
	cm3 = br_dec32le((unsigned char *)cbcmac + 12);

	buf = data;
	first_iter = 1;
	memset(q, 0, sizeof q);
	while (len > 0) {
		uint32_t w[8], carry;

		/*
		 * The bitslice implementation expects values in
		 * little-endian convention, so we have to byteswap them.
		 */
		w[0] = br_swap32(iv0);
		w[1] = br_swap32(iv1);
		w[2] = br_swap32(iv2);
		w[3] = br_swap32(iv3);
		iv3 ++;
		carry = ~(iv3 | -iv3) >> 31;
		iv2 += carry;
		carry &= -(~(iv2 | -iv2) >> 31);
		iv1 += carry;
		carry &= -(~(iv1 | -iv1) >> 31);
		iv0 += carry;

		/*
		 * The block for CBC-MAC.
		 */
		w[4] = cm0;
		w[5] = cm1;
		w[6] = cm2;
		w[7] = cm3;

		br_aes_ct64_interleave_in(&q[0], &q[4], w);
		br_aes_ct64_interleave_in(&q[1], &q[5], w + 4);
		br_aes_ct64_ortho(q);
		br_aes_ct64_bitslice_encrypt(ctx->num_rounds, sk_exp, q);
		br_aes_ct64_ortho(q);
		br_aes_ct64_interleave_out(w, q[0], q[4]);
		br_aes_ct64_interleave_out(w + 4, q[1], q[5]);

		/*
		 * We do the XOR with the plaintext in 32-bit registers,
		 * so that the value are available for CBC-MAC processing
		 * as well.
		 */
		w[0] ^= br_dec32le(buf +  0);
		w[1] ^= br_dec32le(buf +  4);
		w[2] ^= br_dec32le(buf +  8);
		w[3] ^= br_dec32le(buf + 12);
		br_enc32le(buf +  0, w[0]);
		br_enc32le(buf +  4, w[1]);
		br_enc32le(buf +  8, w[2]);
		br_enc32le(buf + 12, w[3]);

		buf += 16;
		len -= 16;

		/*
		 * We set the cm* values to the block to encrypt in the
		 * next iteration.
		 */
		if (first_iter) {
			first_iter = 0;
			cm0 ^= w[0];
			cm1 ^= w[1];
			cm2 ^= w[2];
			cm3 ^= w[3];
		} else {
			cm0 = w[0] ^ w[4];
			cm1 = w[1] ^ w[5];
			cm2 = w[2] ^ w[6];
			cm3 = w[3] ^ w[7];
		}

		/*
		 * If this was the last iteration, then compute the
		 * extra block encryption to complete CBC-MAC.
		 */
		if (len == 0) {
			w[0] = cm0;
			w[1] = cm1;
			w[2] = cm2;
			w[3] = cm3;
			br_aes_ct64_interleave_in(&q[0], &q[4], w);
			br_aes_ct64_ortho(q);
			br_aes_ct64_bitslice_encrypt(
				ctx->num_rounds, sk_exp, q);
			br_aes_ct64_ortho(q);
			br_aes_ct64_interleave_out(w, q[0], q[4]);
			cm0 = w[0];
			cm1 = w[1];
			cm2 = w[2];
			cm3 = w[3];
			break;
		}
	}

	br_enc32be(ivbuf +  0, iv0);
	br_enc32be(ivbuf +  4, iv1);
	br_enc32be(ivbuf +  8, iv2);
	br_enc32be(ivbuf + 12, iv3);
	br_enc32le((unsigned char *)cbcmac +  0, cm0);
	br_enc32le((unsigned char *)cbcmac +  4, cm1);
	br_enc32le((unsigned char *)cbcmac +  8, cm2);
	br_enc32le((unsigned char *)cbcmac + 12, cm3);
}

/* see bearssl_block.h */
void
br_aes_ct64_ctrcbc_decrypt(const br_aes_ct64_ctrcbc_keys *ctx,
	void *ctr, void *cbcmac, void *data, size_t len)
{
	unsigned char *buf;
	unsigned char *ivbuf;
	uint32_t iv0, iv1, iv2, iv3;
	uint32_t cm0, cm1, cm2, cm3;
	uint64_t sk_exp[120];
	uint64_t q[8];

	br_aes_ct64_skey_expand(sk_exp, ctx->num_rounds, ctx->skey);

	/*
	 * We keep the counter as four 32-bit values, with big-endian
	 * convention, because that's what is expected for purposes of
	 * incrementing the counter value.
	 */
	ivbuf = ctr;
	iv0 = br_dec32be(ivbuf +  0);
	iv1 = br_dec32be(ivbuf +  4);
	iv2 = br_dec32be(ivbuf +  8);
	iv3 = br_dec32be(ivbuf + 12);

	/*
	 * The current CBC-MAC value is kept in little-endian convention.
	 */
	cm0 = br_dec32le((unsigned char *)cbcmac +  0);
	cm1 = br_dec32le((unsigned char *)cbcmac +  4);
	cm2 = br_dec32le((unsigned char *)cbcmac +  8);
	cm3 = br_dec32le((unsigned char *)cbcmac + 12);

	buf = data;
	memset(q, 0, sizeof q);
	while (len > 0) {
		uint32_t w[8], carry;
		unsigned char tmp[16];

		/*
		 * The bitslice implementation expects values in
		 * little-endian convention, so we have to byteswap them.
		 */
		w[0] = br_swap32(iv0);
		w[1] = br_swap32(iv1);
		w[2] = br_swap32(iv2);
		w[3] = br_swap32(iv3);
		iv3 ++;
		carry = ~(iv3 | -iv3) >> 31;
		iv2 += carry;
		carry &= -(~(iv2 | -iv2) >> 31);
		iv1 += carry;
		carry &= -(~(iv1 | -iv1) >> 31);
		iv0 += carry;

		/*
		 * The block for CBC-MAC.
		 */
		w[4] = cm0 ^ br_dec32le(buf +  0);
		w[5] = cm1 ^ br_dec32le(buf +  4);
		w[6] = cm2 ^ br_dec32le(buf +  8);
		w[7] = cm3 ^ br_dec32le(buf + 12);

		br_aes_ct64_interleave_in(&q[0], &q[4], w);
		br_aes_ct64_interleave_in(&q[1], &q[5], w + 4);
		br_aes_ct64_ortho(q);
		br_aes_ct64_bitslice_encrypt(ctx->num_rounds, sk_exp, q);
		br_aes_ct64_ortho(q);
		br_aes_ct64_interleave_out(w, q[0], q[4]);
		br_aes_ct64_interleave_out(w + 4, q[1], q[5]);

		br_enc32le(tmp +  0, w[0]);
		br_enc32le(tmp +  4, w[1]);
		br_enc32le(tmp +  8, w[2]);
		br_enc32le(tmp + 12, w[3]);
		xorbuf(buf, tmp, 16);
		cm0 = w[4];
		cm1 = w[5];
		cm2 = w[6];
		cm3 = w[7];
		buf += 16;
		len -= 16;
	}

	br_enc32be(ivbuf +  0, iv0);
	br_enc32be(ivbuf +  4, iv1);
	br_enc32be(ivbuf +  8, iv2);
	br_enc32be(ivbuf + 12, iv3);
	br_enc32le((unsigned char *)cbcmac +  0, cm0);
	br_enc32le((unsigned char *)cbcmac +  4, cm1);
	br_enc32le((unsigned char *)cbcmac +  8, cm2);
	br_enc32le((unsigned char *)cbcmac + 12, cm3);
}

/* see bearssl_block.h */
const br_block_ctrcbc_class br_aes_ct64_ctrcbc_vtable = {
	sizeof(br_aes_ct64_ctrcbc_keys),
	16,
	4,
	(void (*)(const br_block_ctrcbc_class **, const void *, size_t))
		&br_aes_ct64_ctrcbc_init,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, void *, void *, size_t))
		&br_aes_ct64_ctrcbc_encrypt,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, void *, void *, size_t))
		&br_aes_ct64_ctrcbc_decrypt,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, void *, size_t))
		&br_aes_ct64_ctrcbc_ctr,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, const void *, size_t))
		&br_aes_ct64_ctrcbc_mac
};
