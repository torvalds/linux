/****************************************************************

The author of this software is David M. Gay.

Copyright (C) 1998 by Lucent Technologies
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name of Lucent or any of its entities
not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.

****************************************************************/

/* Please send bug reports to David M. Gay (dmg at acm dot org,
 * with " at " changed at "@" and " dot " changed to ".").	*/

#include "gdtoaimp.h"

 Bigint *
#ifdef KR_headers
sum(a, b) Bigint *a; Bigint *b;
#else
sum(Bigint *a, Bigint *b)
#endif
{
	Bigint *c;
	ULong carry, *xc, *xa, *xb, *xe, y;
#ifdef Pack_32
	ULong z;
#endif

	if (a->wds < b->wds) {
		c = b; b = a; a = c;
		}
	c = Balloc(a->k);
	c->wds = a->wds;
	carry = 0;
	xa = a->x;
	xb = b->x;
	xc = c->x;
	xe = xc + b->wds;
#ifdef Pack_32
	do {
		y = (*xa & 0xffff) + (*xb & 0xffff) + carry;
		carry = (y & 0x10000) >> 16;
		z = (*xa++ >> 16) + (*xb++ >> 16) + carry;
		carry = (z & 0x10000) >> 16;
		Storeinc(xc, z, y);
		}
		while(xc < xe);
	xe += a->wds - b->wds;
	while(xc < xe) {
		y = (*xa & 0xffff) + carry;
		carry = (y & 0x10000) >> 16;
		z = (*xa++ >> 16) + carry;
		carry = (z & 0x10000) >> 16;
		Storeinc(xc, z, y);
		}
#else
	do {
		y = *xa++ + *xb++ + carry;
		carry = (y & 0x10000) >> 16;
		*xc++ = y & 0xffff;
		}
		while(xc < xe);
	xe += a->wds - b->wds;
	while(xc < xe) {
		y = *xa++ + carry;
		carry = (y & 0x10000) >> 16;
		*xc++ = y & 0xffff;
		}
#endif
	if (carry) {
		if (c->wds == c->maxwds) {
			b = Balloc(c->k + 1);
			Bcopy(b, c);
			Bfree(c);
			c = b;
			}
		c->x[c->wds++] = 1;
		}
	return c;
	}
