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

/* see inner.h */
void
br_i15_reduce(uint16_t *x, const uint16_t *a, const uint16_t *m)
{
	uint32_t m_bitlen, a_bitlen;
	size_t mlen, alen, u;

	m_bitlen = m[0];
	mlen = (m_bitlen + 15) >> 4;

	x[0] = m_bitlen;
	if (m_bitlen == 0) {
		return;
	}

	/*
	 * If the source is shorter, then simply copy all words from a[]
	 * and zero out the upper words.
	 */
	a_bitlen = a[0];
	alen = (a_bitlen + 15) >> 4;
	if (a_bitlen < m_bitlen) {
		memcpy(x + 1, a + 1, alen * sizeof *a);
		for (u = alen; u < mlen; u ++) {
			x[u + 1] = 0;
		}
		return;
	}

	/*
	 * The source length is at least equal to that of the modulus.
	 * We must thus copy N-1 words, and input the remaining words
	 * one by one.
	 */
	memcpy(x + 1, a + 2 + (alen - mlen), (mlen - 1) * sizeof *a);
	x[mlen] = 0;
	for (u = 1 + alen - mlen; u > 0; u --) {
		br_i15_muladd_small(x, a[u], m);
	}
}
