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
void
br_i32_decode_reduce(uint32_t *x,
	const void *src, size_t len, const uint32_t *m)
{
	uint32_t m_bitlen;
	size_t mblen, k, q;
	const unsigned char *buf;

	m_bitlen = m[0];

	/*
	 * Special case for an invalid modulus.
	 */
	if (m_bitlen == 0) {
		x[0] = 0;
		return;
	}

	/*
	 * Clear the destination.
	 */
	br_i32_zero(x, m_bitlen);

	/*
	 * First decode directly as many bytes as possible without
	 * reduction, taking care to leave a number of bytes which
	 * is a multiple of 4.
	 */
	mblen = (m_bitlen + 7) >> 3;
	k = mblen - 1;

	/*
	 * Up to k bytes can be safely decoded.
	 */
	if (k >= len) {
		br_i32_decode(x, src, len);
		x[0] = m_bitlen;
		return;
	}

	/*
	 * We want to first inject some bytes with direct decoding,
	 * then extra bytes by whole 32-bit words. First compute
	 * the size that should be injected that way.
	 */
	buf = src;
	q = (len - k + 3) & ~(size_t)3;

	/*
	 * It may happen that this is more than what we already have
	 * (by at most 3 bytes). Such a case may happen only with
	 * a very short modulus. In that case, we must process the first
	 * bytes "manually".
	 */
	if (q > len) {
		int i;
		uint32_t w;

		w = 0;
		for (i = 0; i < 4; i ++) {
			w <<= 8;
			if (q <= len) {
				w |= buf[len - q];
			}
			q --;
		}
		br_i32_muladd_small(x, w, m);
	} else {
		br_i32_decode(x, buf, len - q);
		x[0] = m_bitlen;
	}

	/*
	 * At that point, we have exactly q bytes to inject, and q is
	 * a multiple of 4.
	 */
	for (k = len - q; k < len; k += 4) {
		br_i32_muladd_small(x, br_dec32be(buf + k), m);
	}
}
