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
br_i31_decode_reduce(uint32_t *x,
	const void *src, size_t len, const uint32_t *m)
{
	uint32_t m_ebitlen, m_rbitlen;
	size_t mblen, k;
	const unsigned char *buf;
	uint32_t acc;
	int acc_len;

	/*
	 * Get the encoded bit length.
	 */
	m_ebitlen = m[0];

	/*
	 * Special case for an invalid (null) modulus.
	 */
	if (m_ebitlen == 0) {
		x[0] = 0;
		return;
	}

	/*
	 * Clear the destination.
	 */
	br_i31_zero(x, m_ebitlen);

	/*
	 * First decode directly as many bytes as possible. This requires
	 * computing the actual bit length.
	 */
	m_rbitlen = m_ebitlen >> 5;
	m_rbitlen = (m_ebitlen & 31) + (m_rbitlen << 5) - m_rbitlen;
	mblen = (m_rbitlen + 7) >> 3;
	k = mblen - 1;
	if (k >= len) {
		br_i31_decode(x, src, len);
		x[0] = m_ebitlen;
		return;
	}
	buf = src;
	br_i31_decode(x, buf, k);
	x[0] = m_ebitlen;

	/*
	 * Input remaining bytes, using 31-bit words.
	 */
	acc = 0;
	acc_len = 0;
	while (k < len) {
		uint32_t v;

		v = buf[k ++];
		if (acc_len >= 23) {
			acc_len -= 23;
			acc <<= (8 - acc_len);
			acc |= v >> acc_len;
			br_i31_muladd_small(x, acc, m);
			acc = v & (0xFF >> (8 - acc_len));
		} else {
			acc = (acc << 8) | v;
			acc_len += 8;
		}
	}

	/*
	 * We may have some bits accumulated. We then perform a shift to
	 * be able to inject these bits as a full 31-bit word.
	 */
	if (acc_len != 0) {
		acc = (acc | (x[1] << acc_len)) & 0x7FFFFFFF;
		br_i31_rshift(x, 31 - acc_len);
		br_i31_muladd_small(x, acc, m);
	}
}
