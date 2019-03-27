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

/* see inner.h */
uint32_t
br_i32_decode_mod(uint32_t *x, const void *src, size_t len, const uint32_t *m)
{
	const unsigned char *buf;
	uint32_t r;
	size_t u, v, mlen;

	buf = src;

	/*
	 * First pass: determine whether the value fits. The 'r' value
	 * will contain the comparison result, as 0x00000000 (value is
	 * equal to the modulus), 0x00000001 (value is greater than the
	 * modulus), or 0xFFFFFFFF (value is lower than the modulus).
	 */
	mlen = (m[0] + 7) >> 3;
	r = 0;
	for (u = (mlen > len) ? mlen : len; u > 0; u --) {
		uint32_t mb, xb;

		v = u - 1;
		if (v >= mlen) {
			mb = 0;
		} else {
			mb = (m[1 + (v >> 2)] >> ((v & 3) << 3)) & 0xFF;
		}
		if (v >= len) {
			xb = 0;
		} else {
			xb = buf[len - u];
		}
		r = MUX(EQ(r, 0), (uint32_t)CMP(xb, mb), r);
	}

	/*
	 * Only r == 0xFFFFFFFF is acceptable. We want to set r to 0xFF if
	 * the value fits, 0x00 otherwise.
	 */
	r >>= 24;
	br_i32_zero(x, m[0]);
	u = (mlen > len) ? len : mlen;
	while (u > 0) {
		uint32_t xb;

		xb = buf[len - u] & r;
		u --;
		x[1 + (u >> 2)] |= xb << ((u & 3) << 3);
	}
	return r >> 7;
}
