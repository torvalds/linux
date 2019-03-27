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
br_i31_montymul(uint32_t *d, const uint32_t *x, const uint32_t *y,
	const uint32_t *m, uint32_t m0i)
{
	/*
	 * Each outer loop iteration computes:
	 *   d <- (d + xu*y + f*m) / 2^31
	 * We have xu <= 2^31-1 and f <= 2^31-1.
	 * Thus, if d <= 2*m-1 on input, then:
	 *   2*m-1 + 2*(2^31-1)*m <= (2^32)*m-1
	 * and the new d value is less than 2*m.
	 *
	 * We represent d over 31-bit words, with an extra word 'dh'
	 * which can thus be only 0 or 1.
	 */
	size_t len, len4, u, v;
	uint32_t dh;

	len = (m[0] + 31) >> 5;
	len4 = len & ~(size_t)3;
	br_i31_zero(d, m[0]);
	dh = 0;
	for (u = 0; u < len; u ++) {
		/*
		 * The carry for each operation fits on 32 bits:
		 *   d[v+1] <= 2^31-1
		 *   xu*y[v+1] <= (2^31-1)*(2^31-1)
		 *   f*m[v+1] <= (2^31-1)*(2^31-1)
		 *   r <= 2^32-1
		 *   (2^31-1) + 2*(2^31-1)*(2^31-1) + (2^32-1) = 2^63 - 2^31
		 * After division by 2^31, the new r is then at most 2^32-1
		 *
		 * Using a 32-bit carry has performance benefits on 32-bit
		 * systems; however, on 64-bit architectures, we prefer to
		 * keep the carry (r) in a 64-bit register, thus avoiding some
		 * "clear high bits" operations.
		 */
		uint32_t f, xu;
#if BR_64
		uint64_t r;
#else
		uint32_t r;
#endif

		xu = x[u + 1];
		f = MUL31_lo((d[1] + MUL31_lo(x[u + 1], y[1])), m0i);

		r = 0;
		for (v = 0; v < len4; v += 4) {
			uint64_t z;

			z = (uint64_t)d[v + 1] + MUL31(xu, y[v + 1])
				+ MUL31(f, m[v + 1]) + r;
			r = z >> 31;
			d[v + 0] = (uint32_t)z & 0x7FFFFFFF;
			z = (uint64_t)d[v + 2] + MUL31(xu, y[v + 2])
				+ MUL31(f, m[v + 2]) + r;
			r = z >> 31;
			d[v + 1] = (uint32_t)z & 0x7FFFFFFF;
			z = (uint64_t)d[v + 3] + MUL31(xu, y[v + 3])
				+ MUL31(f, m[v + 3]) + r;
			r = z >> 31;
			d[v + 2] = (uint32_t)z & 0x7FFFFFFF;
			z = (uint64_t)d[v + 4] + MUL31(xu, y[v + 4])
				+ MUL31(f, m[v + 4]) + r;
			r = z >> 31;
			d[v + 3] = (uint32_t)z & 0x7FFFFFFF;
		}
		for (; v < len; v ++) {
			uint64_t z;

			z = (uint64_t)d[v + 1] + MUL31(xu, y[v + 1])
				+ MUL31(f, m[v + 1]) + r;
			r = z >> 31;
			d[v] = (uint32_t)z & 0x7FFFFFFF;
		}

		/*
		 * Since the new dh can only be 0 or 1, the addition of
		 * the old dh with the carry MUST fit on 32 bits, and
		 * thus can be done into dh itself.
		 */
		dh += r;
		d[len] = dh & 0x7FFFFFFF;
		dh >>= 31;
	}

	/*
	 * We must write back the bit length because it was overwritten in
	 * the loop (not overwriting it would require a test in the loop,
	 * which would yield bigger and slower code).
	 */
	d[0] = m[0];

	/*
	 * d[] may still be greater than m[] at that point; notably, the
	 * 'dh' word may be non-zero.
	 */
	br_i31_sub(d, m, NEQ(dh, 0) | NOT(br_i31_sub(d, m, 0)));
}
