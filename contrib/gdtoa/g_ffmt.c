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

 char*
#ifdef KR_headers
g_ffmt(buf, f, ndig, bufsize) char *buf; float *f; int ndig; size_t bufsize;
#else
g_ffmt(char *buf, float *f, int ndig, size_t bufsize)
#endif
{
	static FPI fpi0 = { 24, 1-127-24+1,  254-127-24+1, 1, 0 };
	char *b, *s, *se;
	ULong bits[1], *L, sign;
	int decpt, ex, i, mode;
#ifdef Honor_FLT_ROUNDS
#include "gdtoa_fltrnds.h"
#else
#define fpi &fpi0
#endif

	if (ndig < 0)
		ndig = 0;
	if (bufsize < ndig + 10)
		return 0;

	L = (ULong*)f;
	sign = L[0] & 0x80000000L;
	if ((L[0] & 0x7f800000) == 0x7f800000) {
		/* Infinity or NaN */
		if (L[0] & 0x7fffff) {
			return strcp(buf, "NaN");
			}
		b = buf;
		if (sign)
			*b++ = '-';
		return strcp(b, "Infinity");
		}
	if (*f == 0.) {
		b = buf;
#ifndef IGNORE_ZERO_SIGN
		if (L[0] & 0x80000000L)
			*b++ = '-';
#endif
		*b++ = '0';
		*b = 0;
		return b;
		}
	bits[0] = L[0] & 0x7fffff;
	if ( (ex = (L[0] >> 23) & 0xff) !=0)
		bits[0] |= 0x800000;
	else
		ex = 1;
	ex -= 0x7f + 23;
	mode = 2;
	if (ndig <= 0) {
		if (bufsize < 16)
			return 0;
		mode = 0;
		}
	i = STRTOG_Normal;
	s = gdtoa(fpi, ex, bits, &i, mode, ndig, &decpt, &se);
	return g__fmt(buf, s, se, decpt, sign, bufsize);
	}
