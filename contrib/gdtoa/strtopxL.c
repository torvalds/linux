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
#endif
#ifdef IEEE_8087
#define _0 2
#define _1 1
#define _2 0
#endif

 int
#ifdef KR_headers
strtopxL(s, sp, V) CONST char *s; char **sp; void *V;
#else
strtopxL(CONST char *s, char **sp, void *V)
#endif
{
	static FPI fpi0 = { 64, 1-16383-64+1, 32766 - 16383 - 64 + 1, 1, SI };
	ULong bits[2];
	Long exp;
	int k;
	ULong *L = (ULong*)V;
#ifdef Honor_FLT_ROUNDS
#include "gdtoa_fltrnds.h"
#else
#define fpi &fpi0
#endif

	k = strtodg(s, sp, fpi, &exp, bits);
	switch(k & STRTOG_Retmask) {
	  case STRTOG_NoNumber:
	  case STRTOG_Zero:
		L[0] = L[1] = L[2] = 0;
		break;

	  case STRTOG_Normal:
	  case STRTOG_Denormal:
	  case STRTOG_NaNbits:
		L[_2] = bits[0];
		L[_1] = bits[1];
		L[_0] = (exp + 0x3fff + 63) << 16;
		break;

	  case STRTOG_Infinite:
		L[_0] = 0x7fff << 16;
		L[_1] = 0x80000000;
		L[_2] = 0;
		break;

	  case STRTOG_NaN:
		L[0] = ld_QNAN0;
		L[1] = ld_QNAN1;
		L[2] = ld_QNAN2;
	  }
	if (k & STRTOG_Neg)
		L[_0] |= 0x80000000L;
	return k;
	}
