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

#include "brssl.h"

/*
 * Prepare a vector buffer for adding 'extra' elements.
 *   buf      current buffer
 *   esize    size of a vector element
 *   ptr      pointer to the 'ptr' vector field
 *   len      pointer to the 'len' vector field
 *   extra    number of elements to add
 *
 * If the buffer must be enlarged, then this function allocates the new
 * buffer and releases the old one. The new buffer address is then returned.
 * If the buffer needs not be enlarged, then the buffer address is returned.
 *
 * In case of enlargement, the 'len' field is adjusted accordingly. The
 * 'ptr' field is not modified.
 */
void *
vector_expand(void *buf,
	size_t esize, size_t *ptr, size_t *len, size_t extra)
{
	size_t nlen;
	void *nbuf;

	if (*len - *ptr >= extra) {
		return buf;
	}
	nlen = (*len << 1);
	if (nlen - *ptr < extra) {
		nlen = extra + *ptr;
		if (nlen < 8) {
			nlen = 8;
		}
	}
	nbuf = xmalloc(nlen * esize);
	if (buf != NULL) {
		memcpy(nbuf, buf, *len * esize);
		xfree(buf);
	}
	*len = nlen;
	return nbuf;
}
