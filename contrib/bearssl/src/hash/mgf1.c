/*
 * Copyright (c) 2018 Thomas Pornin <pornin@bolet.org>
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
br_mgf1_xor(void *data, size_t len,
	const br_hash_class *dig, const void *seed, size_t seed_len)
{
	unsigned char *buf;
	size_t u, hlen;
	uint32_t c;

	buf = data;
	hlen = br_digest_size(dig);
	for (u = 0, c = 0; u < len; u += hlen, c ++) {
		br_hash_compat_context hc;
		unsigned char tmp[64];
		size_t v;

		hc.vtable = dig;
		dig->init(&hc.vtable);
		dig->update(&hc.vtable, seed, seed_len);
		br_enc32be(tmp, c);
		dig->update(&hc.vtable, tmp, 4);
		dig->out(&hc.vtable, tmp);
		for (v = 0; v < hlen; v ++) {
			if ((u + v) >= len) {
				break;
			}
			buf[u + v] ^= tmp[v];
		}
	}
}
