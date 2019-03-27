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
 * This is a "reference" implementation of Poly1305 that uses the
 * generic "i15" code for big integers. It is slow, but it handles all
 * big-integer operations with generic code, thereby avoiding most
 * tricky situations with carry propagation and modular reduction.
 */

/*
 * Modulus: 2^130-5.
 */
static const uint16_t P1305[] = {
	0x008A,
	0x7FFB, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x03FF
};

/*
 * -p mod 2^15.
 */
#define P0I   0x4CCD

/*
 * R^2 mod p, for conversion to Montgomery representation (R = 2^135,
 * since we use 9 words of 15 bits each, and 15*9 = 135).
 */
static const uint16_t R2[] = {
	0x008A,
	0x6400, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

/*
 * Perform the inner processing of blocks for Poly1305. The "r" array
 * is in Montgomery representation, while the "a" array is not.
 */
static void
poly1305_inner(uint16_t *a, const uint16_t *r, const void *data, size_t len)
{
	const unsigned char *buf;

	buf = data;
	while (len > 0) {
		unsigned char tmp[16], rev[16];
		uint16_t b[10];
		uint32_t ctl;
		int i;

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
		 * Decode next block and apply the "high bit". Since
		 * decoding is little-endian, we must byte-swap the buffer.
		 */
		for (i = 0; i < 16; i ++) {
			rev[i] = buf[15 - i];
		}
		br_i15_decode_mod(b, rev, sizeof rev, P1305);
		b[9] |= 0x0100;

		/*
		 * Add the accumulator to the decoded block (modular
		 * addition).
		 */
		ctl = br_i15_add(b, a, 1);
		ctl |= NOT(br_i15_sub(b, P1305, 0));
		br_i15_sub(b, P1305, ctl);

		/*
		 * Multiply by r, result is the new accumulator value.
		 */
		br_i15_montymul(a, b, r, P1305, P0I);

		buf += 16;
		len -= 16;
	}
}

/*
 * Byteswap a 16-byte value.
 */
static void
byteswap16(unsigned char *buf)
{
	int i;

	for (i = 0; i < 8; i ++) {
		unsigned x;

		x = buf[i];
		buf[i] = buf[15 - i];
		buf[15 - i] = x;
	}
}

/* see bearssl_block.h */
void
br_poly1305_i15_run(const void *key, const void *iv,
	void *data, size_t len, const void *aad, size_t aad_len,
	void *tag, br_chacha20_run ichacha, int encrypt)
{
	unsigned char pkey[32], foot[16];
	uint16_t t[10], r[10], acc[10];

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
	 * Apply the "clamping" operation on the encoded 'r' value.
	 */
	pkey[ 3] &= 0x0F;
	pkey[ 7] &= 0x0F;
	pkey[11] &= 0x0F;
	pkey[15] &= 0x0F;
	pkey[ 4] &= 0xFC;
	pkey[ 8] &= 0xFC;
	pkey[12] &= 0xFC;

	/*
	 * Decode the clamped 'r' value. Decoding should use little-endian
	 * so we must byteswap the value first.
	 */
	byteswap16(pkey);
	br_i15_decode_mod(t, pkey, 16, P1305);

	/*
	 * Convert 'r' to Montgomery representation.
	 */
	br_i15_montymul(r, t, R2, P1305, P0I);

	/*
	 * Accumulator is 0.
	 */
	br_i15_zero(acc, 0x8A);

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
	 * Decode the value 's'. Again, a byteswap is needed.
	 */
	byteswap16(pkey + 16);
	br_i15_decode_mod(t, pkey + 16, 16, P1305);

	/*
	 * Add the value 's' to the accumulator. That addition is done
	 * modulo 2^128, so we just ignore the carry.
	 */
	br_i15_add(acc, t, 1);

	/*
	 * Encode the result (128 low bits) to the tag. Encoding should
	 * be little-endian.
	 */
	br_i15_encode(tag, 16, acc);
	byteswap16(tag);

	/*
	 * If decrypting, then ChaCha20 runs _after_ Poly1305.
	 */
	if (!encrypt) {
		ichacha(key, iv, 1, data, len);
	}
}
