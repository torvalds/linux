/****************************************************************

The author of this software is David M. Gay.

Copyright (C) 2004 by David M. Gay.
All Rights Reserved
Based on material in the rest of /netlib/fp/gdota.tar.gz,
which is copyright (C) 1998, 2000 by Lucent Technologies.

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

/* This is a variant of strtod that works on Intel ia32 systems */
/* with the default extended-precision arithmetic -- it does not */
/* require setting the precision control to 53 bits.  */

/* Please send bug reports to David M. Gay (dmg at acm dot org,
 * with " at " changed at "@" and " dot " changed to ".").	*/

#include "gdtoaimp.h"

 double
#ifdef KR_headers
strtod(s, sp) CONST char *s; char **sp;
#else
strtod(CONST char *s, char **sp)
#endif
{
	static FPI fpi = { 53, 1-1023-53+1, 2046-1023-53+1, 1, SI };
	ULong bits[2];
	Long exp;
	int k;
	union { ULong L[2]; double d; } u;

	k = strtodg(s, sp, &fpi, &exp, bits);
	switch(k & STRTOG_Retmask) {
	  case STRTOG_NoNumber:
	  case STRTOG_Zero:
		u.L[0] = u.L[1] = 0;
		break;

	  case STRTOG_Normal:
		u.L[_1] = bits[0];
		u.L[_0] = (bits[1] & ~0x100000) | ((exp + 0x3ff + 52) << 20);
		break;

	  case STRTOG_Denormal:
		u.L[_1] = bits[0];
		u.L[_0] = bits[1];
		break;

	  case STRTOG_Infinite:
		u.L[_0] = 0x7ff00000;
		u.L[_1] = 0;
		break;

	  case STRTOG_NaN:
		u.L[0] = d_QNAN0;
		u.L[1] = d_QNAN1;
		break;

	  case STRTOG_NaNbits:
		u.L[_0] = 0x7ff00000 | bits[1];
		u.L[_1] = bits[0];
	  }
	if (k & STRTOG_Neg)
		u.L[_0] |= 0x80000000L;
	return u.d;
	}
