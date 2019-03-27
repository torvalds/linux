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
br_i32_montymul(uint32_t *d, const uint32_t *x, const uint32_t *y,
	const uint32_t *m, uint32_t m0i)
{
	size_t len, u, v;
	uint64_t dh;

	len = (m[0] + 31) >> 5;
	br_i32_zero(d, m[0]);
	dh = 0;
	for (u = 0; u < len; u ++) {
		uint32_t f, xu;
		uint64_t r1, r2, zh;

		xu = x[u + 1];
		f = (d[1] + x[u + 1] * y[1]) * m0i;
		r1 = 0;
		r2 = 0;
		for (v = 0; v < len; v ++) {
			uint64_t z;
			uint32_t t;

			z = (uint64_t)d[v + 1] + MUL(xu, y[v + 1]) + r1;
			r1 = z >> 32;
			t = (uint32_t)z;
			z = (uint64_t)t + MUL(f, m[v + 1]) + r2;
			r2 = z >> 32;
			if (v != 0) {
				d[v] = (uint32_t)z;
			}
		}
		zh = dh + r1 + r2;
		d[len] = (uint32_t)zh;
		dh = zh >> 32;
	}

	/*
	 * d[] may still be greater than m[] at that point; notably, the
	 * 'dh' word may be non-zero.
	 */
	br_i32_sub(d, m, NEQ(dh, 0) | NOT(br_i32_sub(d, m, 0)));
}
