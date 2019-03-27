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
uint32_t
br_i15_decode_mod(uint16_t *x, const void *src, size_t len, const uint16_t *m)
{
	/*
	 * Two-pass algorithm: in the first pass, we determine whether the
	 * value fits; in the second pass, we do the actual write.
	 *
	 * During the first pass, 'r' contains the comparison result so
	 * far:
	 *  0x00000000   value is equal to the modulus
	 *  0x00000001   value is greater than the modulus
	 *  0xFFFFFFFF   value is lower than the modulus
	 *
	 * Since we iterate starting with the least significant bytes (at
	 * the end of src[]), each new comparison overrides the previous
	 * except when the comparison yields 0 (equal).
	 *
	 * During the second pass, 'r' is either 0xFFFFFFFF (value fits)
	 * or 0x00000000 (value does not fit).
	 *
	 * We must iterate over all bytes of the source, _and_ possibly
	 * some extra virtual bytes (with value 0) so as to cover the
	 * complete modulus as well. We also add 4 such extra bytes beyond
	 * the modulus length because it then guarantees that no accumulated
	 * partial word remains to be processed.
	 */
	const unsigned char *buf;
	size_t mlen, tlen;
	int pass;
	uint32_t r;

	buf = src;
	mlen = (m[0] + 15) >> 4;
	tlen = (mlen << 1);
	if (tlen < len) {
		tlen = len;
	}
	tlen += 4;
	r = 0;
	for (pass = 0; pass < 2; pass ++) {
		size_t u, v;
		uint32_t acc;
		int acc_len;

		v = 1;
		acc = 0;
		acc_len = 0;
		for (u = 0; u < tlen; u ++) {
			uint32_t b;

			if (u < len) {
				b = buf[len - 1 - u];
			} else {
				b = 0;
			}
			acc |= (b << acc_len);
			acc_len += 8;
			if (acc_len >= 15) {
				uint32_t xw;

				xw = acc & (uint32_t)0x7FFF;
				acc_len -= 15;
				acc = b >> (8 - acc_len);
				if (v <= mlen) {
					if (pass) {
						x[v] = r & xw;
					} else {
						uint32_t cc;

						cc = (uint32_t)CMP(xw, m[v]);
						r = MUX(EQ(cc, 0), r, cc);
					}
				} else {
					if (!pass) {
						r = MUX(EQ(xw, 0), r, 1);
					}
				}
				v ++;
			}
		}

		/*
		 * When we reach this point at the end of the first pass:
		 * r is either 0, 1 or -1; we want to set r to 0 if it
		 * is equal to 0 or 1, and leave it to -1 otherwise.
		 *
		 * When we reach this point at the end of the second pass:
		 * r is either 0 or -1; we want to leave that value
		 * untouched. This is a subcase of the previous.
		 */
		r >>= 1;
		r |= (r << 1);
	}

	x[0] = m[0];
	return r & (uint32_t)1;
}
