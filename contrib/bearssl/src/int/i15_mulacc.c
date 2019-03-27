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
br_i15_mulacc(uint16_t *d, const uint16_t *a, const uint16_t *b)
{
	size_t alen, blen, u;
	unsigned dl, dh;

	alen = (a[0] + 15) >> 4;
	blen = (b[0] + 15) >> 4;

	/*
	 * Announced bit length of d[] will be the sum of the announced
	 * bit lengths of a[] and b[]; but the lengths are encoded.
	 */
	dl = (a[0] & 15) + (b[0] & 15);
	dh = (a[0] >> 4) + (b[0] >> 4);
	d[0] = (dh << 4) + dl + (~(uint32_t)(dl - 15) >> 31);

	for (u = 0; u < blen; u ++) {
		uint32_t f;
		size_t v;
		uint32_t cc;

		f = b[1 + u];
		cc = 0;
		for (v = 0; v < alen; v ++) {
			uint32_t z;

			z = (uint32_t)d[1 + u + v] + MUL15(f, a[1 + v]) + cc;
			cc = z >> 15;
			d[1 + u + v] = z & 0x7FFF;
		}
		d[1 + u + alen] = cc;
	}
}
