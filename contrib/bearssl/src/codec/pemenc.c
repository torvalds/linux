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

/*
 * Get the appropriate Base64 character for a numeric value in the
 * 0..63 range. This is constant-time.
 */
static char
b64char(uint32_t x)
{
	/*
	 * Values 0 to 25 map to 0x41..0x5A ('A' to 'Z')
	 * Values 26 to 51 map to 0x61..0x7A ('a' to 'z')
	 * Values 52 to 61 map to 0x30..0x39 ('0' to '9')
	 * Value 62 maps to 0x2B ('+')
	 * Value 63 maps to 0x2F ('/')
	 */
	uint32_t a, b, c;

	a = x - 26;
	b = x - 52;
	c = x - 62;

	/*
	 * Looking at bits 8..15 of values a, b and c:
	 *
	 *     x       a   b   c
	 *  ---------------------
	 *   0..25    FF  FF  FF
	 *   26..51   00  FF  FF
	 *   52..61   00  00  FF
	 *   62..63   00  00  00
	 */
	return (char)(((x + 0x41) & ((a & b & c) >> 8))
		| ((x + (0x61 - 26)) & ((~a & b & c) >> 8))
		| ((x - (52 - 0x30)) & ((~a & ~b & c) >> 8))
		| ((0x2B + ((x & 1) << 2)) & (~(a | b | c) >> 8)));
}

/* see bearssl_pem.h */
size_t
br_pem_encode(void *dest, const void *data, size_t len,
	const char *banner, unsigned flags)
{
	size_t dlen, banner_len, lines;
	char *d;
	unsigned char *buf;
	size_t u;
	int off, lim;

	banner_len = strlen(banner);
	/* FIXME: try to avoid divisions here, as they may pull
	   an extra libc function. */
	if ((flags & BR_PEM_LINE64) != 0) {
		lines = (len + 47) / 48;
	} else {
		lines = (len + 56) / 57;
	}
	dlen = (banner_len << 1) + 30 + (((len + 2) / 3) << 2)
		+ lines + 2;
	if ((flags & BR_PEM_CRLF) != 0) {
		dlen += lines + 2;
	}

	if (dest == NULL) {
		return dlen;
	}

	d = dest;

	/*
	 * We always move the source data to the end of output buffer;
	 * the encoding process never "catches up" except at the very
	 * end. This also handles all conditions of partial or total
	 * overlap.
	 */
	buf = (unsigned char *)d + dlen - len;
	memmove(buf, data, len);

	memcpy(d, "-----BEGIN ", 11);
	d += 11;
	memcpy(d, banner, banner_len);
	d += banner_len;
	memcpy(d, "-----", 5);
	d += 5;
	if ((flags & BR_PEM_CRLF) != 0) {
		*d ++ = 0x0D;
	}
	*d ++ = 0x0A;

	off = 0;
	lim = (flags & BR_PEM_LINE64) != 0 ? 16 : 19;
	for (u = 0; (u + 2) < len; u += 3) {
		uint32_t w;

		w = ((uint32_t)buf[u] << 16)
			| ((uint32_t)buf[u + 1] << 8)
			| (uint32_t)buf[u + 2];
		*d ++ = b64char(w >> 18);
		*d ++ = b64char((w >> 12) & 0x3F);
		*d ++ = b64char((w >> 6) & 0x3F);
		*d ++ = b64char(w & 0x3F);
		if (++ off == lim) {
			off = 0;
			if ((flags & BR_PEM_CRLF) != 0) {
				*d ++ = 0x0D;
			}
			*d ++ = 0x0A;
		}
	}
	if (u < len) {
		uint32_t w;

		w = (uint32_t)buf[u] << 16;
		if (u + 1 < len) {
			w |= (uint32_t)buf[u + 1] << 8;
		}
		*d ++ = b64char(w >> 18);
		*d ++ = b64char((w >> 12) & 0x3F);
		if (u + 1 < len) {
			*d ++ = b64char((w >> 6) & 0x3F);
		} else {
			*d ++ = 0x3D;
		}
		*d ++ = 0x3D;
		off ++;
	}
	if (off != 0) {
		if ((flags & BR_PEM_CRLF) != 0) {
			*d ++ = 0x0D;
		}
		*d ++ = 0x0A;
	}

	memcpy(d, "-----END ", 9);
	d += 9;
	memcpy(d, banner, banner_len);
	d += banner_len;
	memcpy(d, "-----", 5);
	d += 5;
	if ((flags & BR_PEM_CRLF) != 0) {
		*d ++ = 0x0D;
	}
	*d ++ = 0x0A;

	/* Final zero, not counted in returned length. */
	*d ++ = 0x00;

	return dlen;
}
