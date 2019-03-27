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
 * Constant-time division. The divisor must not be larger than 16 bits,
 * and the quotient must fit on 17 bits.
 */
static uint32_t
divrem16(uint32_t x, uint32_t d, uint32_t *r)
{
	int i;
	uint32_t q;

	q = 0;
	d <<= 16;
	for (i = 16; i >= 0; i --) {
		uint32_t ctl;

		ctl = LE(d, x);
		q |= ctl << i;
		x -= (-ctl) & d;
		d >>= 1;
	}
	if (r != NULL) {
		*r = x;
	}
	return q;
}

/* see inner.h */
void
br_i15_muladd_small(uint16_t *x, uint16_t z, const uint16_t *m)
{
	/*
	 * Constant-time: we accept to leak the exact bit length of the
	 * modulus m.
	 */
	unsigned m_bitlen, mblr;
	size_t u, mlen;
	uint32_t hi, a0, a, b, q;
	uint32_t cc, tb, over, under;

	/*
	 * Simple case: the modulus fits on one word.
	 */
	m_bitlen = m[0];
	if (m_bitlen == 0) {
		return;
	}
	if (m_bitlen <= 15) {
		uint32_t rem;

		divrem16(((uint32_t)x[1] << 15) | z, m[1], &rem);
		x[1] = rem;
		return;
	}
	mlen = (m_bitlen + 15) >> 4;
	mblr = m_bitlen & 15;

	/*
	 * Principle: we estimate the quotient (x*2^15+z)/m by
	 * doing a 30/15 division with the high words.
	 *
	 * Let:
	 *   w = 2^15
	 *   a = (w*a0 + a1) * w^N + a2
	 *   b = b0 * w^N + b2
	 * such that:
	 *   0 <= a0 < w
	 *   0 <= a1 < w
	 *   0 <= a2 < w^N
	 *   w/2 <= b0 < w
	 *   0 <= b2 < w^N
	 *   a < w*b
	 * I.e. the two top words of a are a0:a1, the top word of b is
	 * b0, we ensured that b0 is "full" (high bit set), and a is
	 * such that the quotient q = a/b fits on one word (0 <= q < w).
	 *
	 * If a = b*q + r (with 0 <= r < q), then we can estimate q by
	 * using a division on the top words:
	 *   a0*w + a1 = b0*u + v (with 0 <= v < b0)
	 * Then the following holds:
	 *   0 <= u <= w
	 *   u-2 <= q <= u
	 */
	hi = x[mlen];
	if (mblr == 0) {
		a0 = x[mlen];
		memmove(x + 2, x + 1, (mlen - 1) * sizeof *x);
		x[1] = z;
		a = (a0 << 15) + x[mlen];
		b = m[mlen];
	} else {
		a0 = (x[mlen] << (15 - mblr)) | (x[mlen - 1] >> mblr);
		memmove(x + 2, x + 1, (mlen - 1) * sizeof *x);
		x[1] = z;
		a = (a0 << 15) | (((x[mlen] << (15 - mblr))
			| (x[mlen - 1] >> mblr)) & 0x7FFF);
		b = (m[mlen] << (15 - mblr)) | (m[mlen - 1] >> mblr);
	}
	q = divrem16(a, b, NULL);

	/*
	 * We computed an estimate for q, but the real one may be q,
	 * q-1 or q-2; moreover, the division may have returned a value
	 * 8000 or even 8001 if the two high words were identical, and
	 * we want to avoid values beyond 7FFF. We thus adjust q so
	 * that the "true" multiplier will be q+1, q or q-1, and q is
	 * in the 0000..7FFF range.
	 */
	q = MUX(EQ(b, a0), 0x7FFF, q - 1 + ((q - 1) >> 31));

	/*
	 * We subtract q*m from x (x has an extra high word of value 'hi').
	 * Since q may be off by 1 (in either direction), we may have to
	 * add or subtract m afterwards.
	 *
	 * The 'tb' flag will be true (1) at the end of the loop if the
	 * result is greater than or equal to the modulus (not counting
	 * 'hi' or the carry).
	 */
	cc = 0;
	tb = 1;
	for (u = 1; u <= mlen; u ++) {
		uint32_t mw, zl, xw, nxw;

		mw = m[u];
		zl = MUL15(mw, q) + cc;
		cc = zl >> 15;
		zl &= 0x7FFF;
		xw = x[u];
		nxw = xw - zl;
		cc += nxw >> 31;
		nxw &= 0x7FFF;
		x[u] = nxw;
		tb = MUX(EQ(nxw, mw), tb, GT(nxw, mw));
	}

	/*
	 * If we underestimated q, then either cc < hi (one extra bit
	 * beyond the top array word), or cc == hi and tb is true (no
	 * extra bit, but the result is not lower than the modulus).
	 *
	 * If we overestimated q, then cc > hi.
	 */
	over = GT(cc, hi);
	under = ~over & (tb | LT(cc, hi));
	br_i15_add(x, m, over);
	br_i15_sub(x, m, under);
}
