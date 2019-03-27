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
 * This is the 64-bit variant of br_ghash_ctmul32(), with 64-bit operands
 * and bit reversal of 64-bit words.
 */

static inline uint64_t
bmul64(uint64_t x, uint64_t y)
{
	uint64_t x0, x1, x2, x3;
	uint64_t y0, y1, y2, y3;
	uint64_t z0, z1, z2, z3;

	x0 = x & (uint64_t)0x1111111111111111;
	x1 = x & (uint64_t)0x2222222222222222;
	x2 = x & (uint64_t)0x4444444444444444;
	x3 = x & (uint64_t)0x8888888888888888;
	y0 = y & (uint64_t)0x1111111111111111;
	y1 = y & (uint64_t)0x2222222222222222;
	y2 = y & (uint64_t)0x4444444444444444;
	y3 = y & (uint64_t)0x8888888888888888;
	z0 = (x0 * y0) ^ (x1 * y3) ^ (x2 * y2) ^ (x3 * y1);
	z1 = (x0 * y1) ^ (x1 * y0) ^ (x2 * y3) ^ (x3 * y2);
	z2 = (x0 * y2) ^ (x1 * y1) ^ (x2 * y0) ^ (x3 * y3);
	z3 = (x0 * y3) ^ (x1 * y2) ^ (x2 * y1) ^ (x3 * y0);
	z0 &= (uint64_t)0x1111111111111111;
	z1 &= (uint64_t)0x2222222222222222;
	z2 &= (uint64_t)0x4444444444444444;
	z3 &= (uint64_t)0x8888888888888888;
	return z0 | z1 | z2 | z3;
}

static uint64_t
rev64(uint64_t x)
{
#define RMS(m, s)   do { \
		x = ((x & (uint64_t)(m)) << (s)) \
			| ((x >> (s)) & (uint64_t)(m)); \
	} while (0)

	RMS(0x5555555555555555,  1);
	RMS(0x3333333333333333,  2);
	RMS(0x0F0F0F0F0F0F0F0F,  4);
	RMS(0x00FF00FF00FF00FF,  8);
	RMS(0x0000FFFF0000FFFF, 16);
	return (x << 32) | (x >> 32);

#undef RMS
}

/* see bearssl_ghash.h */
void
br_ghash_ctmul64(void *y, const void *h, const void *data, size_t len)
{
	const unsigned char *buf, *hb;
	unsigned char *yb;
	uint64_t y0, y1;
	uint64_t h0, h1, h2, h0r, h1r, h2r;

	buf = data;
	yb = y;
	hb = h;
	y1 = br_dec64be(yb);
	y0 = br_dec64be(yb + 8);
	h1 = br_dec64be(hb);
	h0 = br_dec64be(hb + 8);
	h0r = rev64(h0);
	h1r = rev64(h1);
	h2 = h0 ^ h1;
	h2r = h0r ^ h1r;
	while (len > 0) {
		const unsigned char *src;
		unsigned char tmp[16];
		uint64_t y0r, y1r, y2, y2r;
		uint64_t z0, z1, z2, z0h, z1h, z2h;
		uint64_t v0, v1, v2, v3;

		if (len >= 16) {
			src = buf;
			buf += 16;
			len -= 16;
		} else {
			memcpy(tmp, buf, len);
			memset(tmp + len, 0, (sizeof tmp) - len);
			src = tmp;
			len = 0;
		}
		y1 ^= br_dec64be(src);
		y0 ^= br_dec64be(src + 8);

		y0r = rev64(y0);
		y1r = rev64(y1);
		y2 = y0 ^ y1;
		y2r = y0r ^ y1r;

		z0 = bmul64(y0, h0);
		z1 = bmul64(y1, h1);
		z2 = bmul64(y2, h2);
		z0h = bmul64(y0r, h0r);
		z1h = bmul64(y1r, h1r);
		z2h = bmul64(y2r, h2r);
		z2 ^= z0 ^ z1;
		z2h ^= z0h ^ z1h;
		z0h = rev64(z0h) >> 1;
		z1h = rev64(z1h) >> 1;
		z2h = rev64(z2h) >> 1;

		v0 = z0;
		v1 = z0h ^ z2;
		v2 = z1 ^ z2h;
		v3 = z1h;

		v3 = (v3 << 1) | (v2 >> 63);
		v2 = (v2 << 1) | (v1 >> 63);
		v1 = (v1 << 1) | (v0 >> 63);
		v0 = (v0 << 1);

		v2 ^= v0 ^ (v0 >> 1) ^ (v0 >> 2) ^ (v0 >> 7);
		v1 ^= (v0 << 63) ^ (v0 << 62) ^ (v0 << 57);
		v3 ^= v1 ^ (v1 >> 1) ^ (v1 >> 2) ^ (v1 >> 7);
		v2 ^= (v1 << 63) ^ (v1 << 62) ^ (v1 << 57);

		y0 = v2;
		y1 = v3;
	}

	br_enc64be(yb, y1);
	br_enc64be(yb + 8, y0);
}
