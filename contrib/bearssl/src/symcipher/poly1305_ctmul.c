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

/*
 * Perform the inner processing of blocks for Poly1305. The accumulator
 * and the r key are provided as arrays of 26-bit words (these words
 * are allowed to have an extra bit, i.e. use 27 bits).
 *
 * On output, all accumulator words fit on 26 bits, except acc[1], which
 * may be slightly larger (but by a very small amount only).
 */
static void
poly1305_inner(uint32_t *acc, const uint32_t *r, const void *data, size_t len)
{
	/*
	 * Implementation notes: we split the 130-bit values into five
	 * 26-bit words. This gives us some space for carries.
	 *
	 * This code is inspired from the public-domain code available
	 * on:
	 *      https://github.com/floodyberry/poly1305-donna
	 *
	 * Since we compute modulo 2^130-5, the "upper words" become
	 * low words with a factor of 5; that is, x*2^130 = x*5 mod p.
	 */
	const unsigned char *buf;
	uint32_t a0, a1, a2, a3, a4;
	uint32_t r0, r1, r2, r3, r4;
	uint32_t u1, u2, u3, u4;

	r0 = r[0];
	r1 = r[1];
	r2 = r[2];
	r3 = r[3];
	r4 = r[4];

	u1 = r1 * 5;
	u2 = r2 * 5;
	u3 = r3 * 5;
	u4 = r4 * 5;

	a0 = acc[0];
	a1 = acc[1];
	a2 = acc[2];
	a3 = acc[3];
	a4 = acc[4];

	buf = data;
	while (len > 0) {
		uint64_t w0, w1, w2, w3, w4;
		uint64_t c;
		unsigned char tmp[16];

		/*
		 * If there is a partial block, right-pad it with zeros.
		 */
		if (len < 16) {
			memset(tmp, 0, sizeof tmp);
			memcpy(tmp, buf, len);
			buf = tmp;
			len = 16;
		}

		/*
		 * Decode next block and apply the "high bit"; that value
		 * is added to the accumulator.
		 */
		a0 += br_dec32le(buf) & 0x03FFFFFF;
		a1 += (br_dec32le(buf +  3) >> 2) & 0x03FFFFFF;
		a2 += (br_dec32le(buf +  6) >> 4) & 0x03FFFFFF;
		a3 += (br_dec32le(buf +  9) >> 6) & 0x03FFFFFF;
		a4 += (br_dec32le(buf + 12) >> 8) | 0x01000000;

		/*
		 * Compute multiplication.
		 */
#define M(x, y)   ((uint64_t)(x) * (uint64_t)(y))

		w0 = M(a0, r0) + M(a1, u4) + M(a2, u3) + M(a3, u2) + M(a4, u1);
		w1 = M(a0, r1) + M(a1, r0) + M(a2, u4) + M(a3, u3) + M(a4, u2);
		w2 = M(a0, r2) + M(a1, r1) + M(a2, r0) + M(a3, u4) + M(a4, u3);
		w3 = M(a0, r3) + M(a1, r2) + M(a2, r1) + M(a3, r0) + M(a4, u4);
		w4 = M(a0, r4) + M(a1, r3) + M(a2, r2) + M(a3, r1) + M(a4, r0);

#undef M
		/*
		 * Perform some (partial) modular reduction. This step is
		 * enough to keep values in ranges such that there won't
		 * be carry overflows. Most of the reduction was done in
		 * the multiplication step (by using the 'u*' values, and
		 * using the fact that 2^130 = -5 mod p); here we perform
		 * some carry propagation.
		 */
		c = w0 >> 26;
		a0 = (uint32_t)w0 & 0x3FFFFFF;
		w1 += c;
		c = w1 >> 26;
		a1 = (uint32_t)w1 & 0x3FFFFFF;
		w2 += c;
		c = w2 >> 26;
		a2 = (uint32_t)w2 & 0x3FFFFFF;
		w3 += c;
		c = w3 >> 26;
		a3 = (uint32_t)w3 & 0x3FFFFFF;
		w4 += c;
		c = w4 >> 26;
		a4 = (uint32_t)w4 & 0x3FFFFFF;
		a0 += (uint32_t)c * 5;
		a1 += a0 >> 26;
		a0 &= 0x3FFFFFF;

		buf += 16;
		len -= 16;
	}

	acc[0] = a0;
	acc[1] = a1;
	acc[2] = a2;
	acc[3] = a3;
	acc[4] = a4;
}

/* see bearssl_block.h */
void
br_poly1305_ctmul_run(const void *key, const void *iv,
	void *data, size_t len, const void *aad, size_t aad_len,
	void *tag, br_chacha20_run ichacha, int encrypt)
{
	unsigned char pkey[32], foot[16];
	uint32_t r[5], acc[5], cc, ctl, hi;
	uint64_t w;
	int i;

	/*
	 * Compute the MAC key. The 'r' value is the first 16 bytes of
	 * pkey[].
	 */
	memset(pkey, 0, sizeof pkey);
	ichacha(key, iv, 0, pkey, sizeof pkey);

	/*
	 * If encrypting, ChaCha20 must run first, followed by Poly1305.
	 * When decrypting, the operations are reversed.
	 */
	if (encrypt) {
		ichacha(key, iv, 1, data, len);
	}

	/*
	 * Run Poly1305. We must process the AAD, then ciphertext, then
	 * the footer (with the lengths). Note that the AAD and ciphertext
	 * are meant to be padded with zeros up to the next multiple of 16,
	 * and the length of the footer is 16 bytes as well.
	 */

	/*
	 * Decode the 'r' value into 26-bit words, with the "clamping"
	 * operation applied.
	 */
	r[0] = br_dec32le(pkey) & 0x03FFFFFF;
	r[1] = (br_dec32le(pkey +  3) >> 2) & 0x03FFFF03;
	r[2] = (br_dec32le(pkey +  6) >> 4) & 0x03FFC0FF;
	r[3] = (br_dec32le(pkey +  9) >> 6) & 0x03F03FFF;
	r[4] = (br_dec32le(pkey + 12) >> 8) & 0x000FFFFF;

	/*
	 * Accumulator is 0.
	 */
	memset(acc, 0, sizeof acc);

	/*
	 * Process the additional authenticated data, ciphertext, and
	 * footer in due order.
	 */
	br_enc64le(foot, (uint64_t)aad_len);
	br_enc64le(foot + 8, (uint64_t)len);
	poly1305_inner(acc, r, aad, aad_len);
	poly1305_inner(acc, r, data, len);
	poly1305_inner(acc, r, foot, sizeof foot);

	/*
	 * Finalise modular reduction. This is done with carry propagation
	 * and applying the '2^130 = -5 mod p' rule. Note that the output
	 * of poly1035_inner() is already mostly reduced, since only
	 * acc[1] may be (very slightly) above 2^26. A single loop back
	 * to acc[1] will be enough to make the value fit in 130 bits.
	 */
	cc = 0;
	for (i = 1; i <= 6; i ++) {
		int j;

		j = (i >= 5) ? i - 5 : i;
		acc[j] += cc;
		cc = acc[j] >> 26;
		acc[j] &= 0x03FFFFFF;
	}

	/*
	 * We may still have a value in the 2^130-5..2^130-1 range, in
	 * which case we must reduce it again. The code below selects,
	 * in constant-time, between 'acc' and 'acc-p',
	 */
	ctl = GT(acc[0], 0x03FFFFFA);
	for (i = 1; i < 5; i ++) {
		ctl &= EQ(acc[i], 0x03FFFFFF);
	}
	cc = 5;
	for (i = 0; i < 5; i ++) {
		uint32_t t;

		t = (acc[i] + cc);
		cc = t >> 26;
		t &= 0x03FFFFFF;
		acc[i] = MUX(ctl, t, acc[i]);
	}

	/*
	 * Convert back the accumulator to 32-bit words, and add the
	 * 's' value (second half of pkey[]). That addition is done
	 * modulo 2^128.
	 */
	w = (uint64_t)acc[0] + ((uint64_t)acc[1] << 26) + br_dec32le(pkey + 16);
	br_enc32le((unsigned char *)tag, (uint32_t)w);
	w = (w >> 32) + ((uint64_t)acc[2] << 20) + br_dec32le(pkey + 20);
	br_enc32le((unsigned char *)tag + 4, (uint32_t)w);
	w = (w >> 32) + ((uint64_t)acc[3] << 14) + br_dec32le(pkey + 24);
	br_enc32le((unsigned char *)tag + 8, (uint32_t)w);
	hi = (uint32_t)(w >> 32) + (acc[4] << 8) + br_dec32le(pkey + 28);
	br_enc32le((unsigned char *)tag + 12, hi);

	/*
	 * If decrypting, then ChaCha20 runs _after_ Poly1305.
	 */
	if (!encrypt) {
		ichacha(key, iv, 1, data, len);
	}
}
