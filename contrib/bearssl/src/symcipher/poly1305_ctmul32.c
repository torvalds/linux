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
 * Perform the inner processing of blocks for Poly1305.
 */
static void
poly1305_inner(uint32_t *a, const uint32_t *r, const void *data, size_t len)
{
	/*
	 * Implementation notes: we split the 130-bit values into ten
	 * 13-bit words. This gives us some space for carries and allows
	 * using only 32x32->32 multiplications, which are way faster than
	 * 32x32->64 multiplications on the ARM Cortex-M0/M0+, and also
	 * help in making constant-time code on the Cortex-M3.
	 *
	 * Since we compute modulo 2^130-5, the "upper words" become
	 * low words with a factor of 5; that is, x*2^130 = x*5 mod p.
	 * This has already been integrated in the r[] array, which
	 * is extended to the 0..18 range.
	 *
	 * In each loop iteration, a[] and r[] words are 13-bit each,
	 * except a[1] which may use 14 bits.
	 */
	const unsigned char *buf;

	buf = data;
	while (len > 0) {
		unsigned char tmp[16];
		uint32_t b[10];
		unsigned u, v;
		uint32_t z, cc1, cc2;

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
		v = br_dec16le(buf);
		a[0] += v & 0x01FFF;
		v >>= 13;
		v |= buf[2] << 3;
		v |= buf[3] << 11;
		a[1] += v & 0x01FFF;
		v >>= 13;
		v |= buf[4] << 6;
		a[2] += v & 0x01FFF;
		v >>= 13;
		v |= buf[5] << 1;
		v |= buf[6] << 9;
		a[3] += v & 0x01FFF;
		v >>= 13;
		v |= buf[7] << 4;
		v |= buf[8] << 12;
		a[4] += v & 0x01FFF;
		v >>= 13;
		v |= buf[9] << 7;
		a[5] += v & 0x01FFF;
		v >>= 13;
		v |= buf[10] << 2;
		v |= buf[11] << 10;
		a[6] += v & 0x01FFF;
		v >>= 13;
		v |= buf[12] << 5;
		a[7] += v & 0x01FFF;
		v = br_dec16le(buf + 13);
		a[8] += v & 0x01FFF;
		v >>= 13;
		v |= buf[15] << 3;
		a[9] += v | 0x00800;

		/*
		 * At that point, all a[] values fit on 14 bits, while
		 * all r[] values fit on 13 bits. Thus products fit on
		 * 27 bits, and we can accumulate up to 31 of them in
		 * a 32-bit word and still have some room for carries.
		 */

		/*
		 * Now a[] contains words with values up to 14 bits each.
		 * We perform the multiplication with r[].
		 *
		 * The extended words of r[] may be larger than 13 bits
		 * (they are 5 times a 13-bit word) so the full summation
		 * may yield values up to 46 times a 27-bit word, which
		 * does not fit on a 32-bit word. To avoid that issue, we
		 * must split the loop below in two, with a carry
		 * propagation operation in the middle.
		 */
		cc1 = 0;
		for (u = 0; u < 10; u ++) {
			uint32_t s;

			s = cc1
				+ MUL15(a[0], r[u + 9 - 0])
				+ MUL15(a[1], r[u + 9 - 1])
				+ MUL15(a[2], r[u + 9 - 2])
				+ MUL15(a[3], r[u + 9 - 3])
				+ MUL15(a[4], r[u + 9 - 4]);
			b[u] = s & 0x1FFF;
			cc1 = s >> 13;
		}
		cc2 = 0;
		for (u = 0; u < 10; u ++) {
			uint32_t s;

			s = b[u] + cc2
				+ MUL15(a[5], r[u + 9 - 5])
				+ MUL15(a[6], r[u + 9 - 6])
				+ MUL15(a[7], r[u + 9 - 7])
				+ MUL15(a[8], r[u + 9 - 8])
				+ MUL15(a[9], r[u + 9 - 9]);
			b[u] = s & 0x1FFF;
			cc2 = s >> 13;
		}
		memcpy(a, b, sizeof b);

		/*
		 * The two carries "loop back" with a factor of 5. We
		 * propagate them into a[0] and a[1].
		 */
		z = cc1 + cc2;
		z += (z << 2) + a[0];
		a[0] = z & 0x1FFF;
		a[1] += z >> 13;

		buf += 16;
		len -= 16;
	}
}

/* see bearssl_block.h */
void
br_poly1305_ctmul32_run(const void *key, const void *iv,
	void *data, size_t len, const void *aad, size_t aad_len,
	void *tag, br_chacha20_run ichacha, int encrypt)
{
	unsigned char pkey[32], foot[16];
	uint32_t z, r[19], acc[10], cc, ctl;
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
	 * Decode the 'r' value into 13-bit words, with the "clamping"
	 * operation applied.
	 */
	z = br_dec32le(pkey) & 0x03FFFFFF;
	r[9] = z & 0x1FFF;
	r[10] = z >> 13;
	z = (br_dec32le(pkey +  3) >> 2) & 0x03FFFF03;
	r[11] = z & 0x1FFF;
	r[12] = z >> 13;
	z = (br_dec32le(pkey +  6) >> 4) & 0x03FFC0FF;
	r[13] = z & 0x1FFF;
	r[14] = z >> 13;
	z = (br_dec32le(pkey +  9) >> 6) & 0x03F03FFF;
	r[15] = z & 0x1FFF;
	r[16] = z >> 13;
	z = (br_dec32le(pkey + 12) >> 8) & 0x000FFFFF;
	r[17] = z & 0x1FFF;
	r[18] = z >> 13;

	/*
	 * Extend r[] with the 5x factor pre-applied.
	 */
	for (i = 0; i < 9; i ++) {
		r[i] = MUL15(5, r[i + 10]);
	}

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
	 * acc[1] may be (very slightly) above 2^13. A single loop back
	 * to acc[1] will be enough to make the value fit in 130 bits.
	 */
	cc = 0;
	for (i = 1; i < 10; i ++) {
		z = acc[i] + cc;
		acc[i] = z & 0x1FFF;
		cc = z >> 13;
	}
	z = acc[0] + cc + (cc << 2);
	acc[0] = z & 0x1FFF;
	acc[1] += z >> 13;

	/*
	 * We may still have a value in the 2^130-5..2^130-1 range, in
	 * which case we must reduce it again. The code below selects,
	 * in constant-time, between 'acc' and 'acc-p',
	 */
	ctl = GT(acc[0], 0x1FFA);
	for (i = 1; i < 10; i ++) {
		ctl &= EQ(acc[i], 0x1FFF);
	}
	acc[0] = MUX(ctl, acc[0] - 0x1FFB, acc[0]);
	for (i = 1; i < 10; i ++) {
		acc[i] &= ~(-ctl);
	}

	/*
	 * Convert back the accumulator to 32-bit words, and add the
	 * 's' value (second half of pkey[]). That addition is done
	 * modulo 2^128.
	 */
	z = acc[0] + (acc[1] << 13) + br_dec16le(pkey + 16);
	br_enc16le((unsigned char *)tag, z & 0xFFFF);
	z = (z >> 16) + (acc[2] << 10) + br_dec16le(pkey + 18);
	br_enc16le((unsigned char *)tag + 2, z & 0xFFFF);
	z = (z >> 16) + (acc[3] << 7) + br_dec16le(pkey + 20);
	br_enc16le((unsigned char *)tag + 4, z & 0xFFFF);
	z = (z >> 16) + (acc[4] << 4) + br_dec16le(pkey + 22);
	br_enc16le((unsigned char *)tag + 6, z & 0xFFFF);
	z = (z >> 16) + (acc[5] << 1) + (acc[6] << 14) + br_dec16le(pkey + 24);
	br_enc16le((unsigned char *)tag + 8, z & 0xFFFF);
	z = (z >> 16) + (acc[7] << 11) + br_dec16le(pkey + 26);
	br_enc16le((unsigned char *)tag + 10, z & 0xFFFF);
	z = (z >> 16) + (acc[8] << 8) + br_dec16le(pkey + 28);
	br_enc16le((unsigned char *)tag + 12, z & 0xFFFF);
	z = (z >> 16) + (acc[9] << 5) + br_dec16le(pkey + 30);
	br_enc16le((unsigned char *)tag + 14, z & 0xFFFF);

	/*
	 * If decrypting, then ChaCha20 runs _after_ Poly1305.
	 */
	if (!encrypt) {
		ichacha(key, iv, 1, data, len);
	}
}
