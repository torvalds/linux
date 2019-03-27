/****************************************************************

The author of this software is David M. Gay.

Copyright (C) 1998, 2000 by Lucent Technologies
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

#undef _0
#undef _1

/* one or the other of IEEE_MC68k or IEEE_8087 should be #defined */

#ifdef IEEE_MC68k
#define _0 0
#define _1 1
#define _2 2
#define _3 3
#endif
#ifdef IEEE_8087
#define _0 3
#define _1 2
#define _2 1
#define _3 0
#endif

 char*
#ifdef KR_headers
g_Qfmt(buf, V, ndig, bufsize) char *buf; char *V; int ndig; size_t bufsize;
#else
g_Qfmt(char *buf, void *V, int ndig, size_t bufsize)
#endif
{
	static FPI fpi0 = { 113, 1-16383-113+1, 32766 - 16383 - 113 + 1, 1, 0 };
	char *b, *s, *se;
	ULong bits[4], *L, sign;
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

	L = (ULong*)V;
	sign = L[_0] & 0x80000000L;
	bits[3] = L[_0] & 0xffff;
	bits[2] = L[_1];
	bits[1] = L[_2];
	bits[0] = L[_3];
	b = buf;
	if ( (ex = (L[_0] & 0x7fff0000L) >> 16) !=0) {
		if (ex == 0x7fff) {
			/* Infinity or NaN */
			if (bits[0] | bits[1] | bits[2] | bits[3])
				b = strcp(b, "NaN");
			else {
				b = buf;
				if (sign)
					*b++ = '-';
				b = strcp(b, "Infinity");
				}
			return b;
			}
		i = STRTOG_Normal;
		bits[3] |= 0x10000;
		}
	else if (bits[0] | bits[1] | bits[2] | bits[3]) {
		i = STRTOG_Denormal;
		ex = 1;
		}
	else {
#ifndef IGNORE_ZERO_SIGN
		if (sign)
			*b++ = '-';
#endif
		*b++ = '0';
		*b = 0;
		return b;
		}
	ex -= 0x3fff + 112;
	mode = 2;
	if (ndig <= 0) {
		if (bufsize < 48)
			return 0;
		mode = 0;
		}
	s = gdtoa(fpi, ex, bits, &i, mode, ndig, &decpt, &se);
	return g__fmt(buf, s, se, decpt, sign, bufsize);
	}
