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
br_i32_encode(void *dst, size_t len, const uint32_t *x)
{
	unsigned char *buf;
	size_t k;

	buf = dst;

	/*
	 * Compute the announced size of x in bytes; extra bytes are
	 * filled with zeros.
	 */
	k = (x[0] + 7) >> 3;
	while (len > k) {
		*buf ++ = 0;
		len --;
	}

	/*
	 * Now we use k as index within x[]. That index starts at 1;
	 * we initialize it to the topmost complete word, and process
	 * any remaining incomplete word.
	 */
	k = (len + 3) >> 2;
	switch (len & 3) {
	case 3:
		*buf ++ = x[k] >> 16;
		/* fall through */
	case 2:
		*buf ++ = x[k] >> 8;
		/* fall through */
	case 1:
		*buf ++ = x[k];
		k --;
	}

	/*
	 * Encode all complete words.
	 */
	while (k > 0) {
		br_enc32be(buf, x[k]);
		k --;
		buf += 4;
	}
}
