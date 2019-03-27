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

 void
#ifdef KR_headers
ULtodd(L, bits, exp, k) ULong *L; ULong *bits; Long exp; int k;
#else
ULtodd(ULong *L, ULong *bits, Long exp, int k)
#endif
{
	int i, j;

	switch(k & STRTOG_Retmask) {
	  case STRTOG_NoNumber:
	  case STRTOG_Zero:
		L[0] = L[1] = L[2] = L[3] = 0;
		break;

	  case STRTOG_Normal:
		L[_1] = (bits[1] >> 21 | bits[2] << 11) & (ULong)0xffffffffL;
		L[_0] = (bits[2] >> 21) | (bits[3] << 11 & 0xfffff)
			  | ((exp + 0x3ff + 105) << 20);
		exp += 0x3ff + 52;
		if (bits[1] &= 0x1fffff) {
			i = hi0bits(bits[1]) - 11;
			if (i >= exp) {
				i = exp - 1;
				exp = 0;
				}
			else
				exp -= i;
			if (i > 0) {
				bits[1] = bits[1] << i | bits[0] >> (32-i);
				bits[0] = bits[0] << i & (ULong)0xffffffffL;
				}
			}
		else if (bits[0]) {
			i = hi0bits(bits[0]) + 21;
			if (i >= exp) {
				i = exp - 1;
				exp = 0;
				}
			else
				exp -= i;
			if (i < 32) {
				bits[1] = bits[0] >> (32 - i);
				bits[0] = bits[0] << i & (ULong)0xffffffffL;
				}
			else {
				bits[1] = bits[0] << (i - 32);
				bits[0] = 0;
				}
			}
		else {
			L[2] = L[3] = 0;
			break;
			}
		L[2+_1] = bits[0];
		L[2+_0] = (bits[1] & 0xfffff) | (exp << 20);
		break;

	  case STRTOG_Denormal:
		if (bits[3])
			goto nearly_normal;
		if (bits[2])
			goto partly_normal;
		if (bits[1] & 0xffe00000)
			goto hardly_normal;
		/* completely denormal */
		L[2] = L[3] = 0;
		L[_1] = bits[0];
		L[_0] = bits[1];
		break;

	  nearly_normal:
		i = hi0bits(bits[3]) - 11;	/* i >= 12 */
		j = 32 - i;
		L[_0] = ((bits[3] << i | bits[2] >> j) & 0xfffff)
			| ((65 - i) << 20);
		L[_1] = (bits[2] << i | bits[1] >> j) & 0xffffffffL;
		L[2+_0] = bits[1] & (((ULong)1L << j) - 1);
		L[2+_1] = bits[0];
		break;

	  partly_normal:
		i = hi0bits(bits[2]) - 11;
		if (i < 0) {
			j = -i;
			i += 32;
			L[_0] = (bits[2] >> j & 0xfffff) | ((33 + j) << 20);
			L[_1] = (bits[2] << i | bits[1] >> j) & 0xffffffffL;
			L[2+_0] = bits[1] & (((ULong)1L << j) - 1);
			L[2+_1] = bits[0];
			break;
			}
		if (i == 0) {
			L[_0] = (bits[2] & 0xfffff) | (33 << 20);
			L[_1] = bits[1];
			L[2+_0] = 0;
			L[2+_1] = bits[0];
			break;
			}
		j = 32 - i;
		L[_0] = (((bits[2] << i) | (bits[1] >> j)) & 0xfffff)
				| ((j + 1) << 20);
		L[_1] = (bits[1] << i | bits[0] >> j) & 0xffffffffL;
		L[2+_0] = 0;
		L[2+_1] = bits[0] & ((1L << j) - 1);
		break;

	  hardly_normal:
		j = 11 - hi0bits(bits[1]);
		i = 32 - j;
		L[_0] = (bits[1] >> j & 0xfffff) | ((j + 1) << 20);
		L[_1] = (bits[1] << i | bits[0] >> j) & 0xffffffffL;
		L[2+_0] = 0;
		L[2+_1] = bits[0] & (((ULong)1L << j) - 1);
		break;

	  case STRTOG_Infinite:
		L[_0] = L[2+_0] = 0x7ff00000;
		L[_1] = L[2+_1] = 0;
		break;

	  case STRTOG_NaN:
		L[0] = L[2] = d_QNAN0;
		L[1] = L[3] = d_QNAN1;
		break;

	  case STRTOG_NaNbits:
		L[_1] = (bits[1] >> 21 | bits[2] << 11) & (ULong)0xffffffffL;
		L[_0] = bits[2] >> 21 | bits[3] << 11
			  | (ULong)0x7ff00000L;
		L[2+_1] = bits[0];
		L[2+_0] = bits[1] | (ULong)0x7ff00000L;
	  }
	if (k & STRTOG_Neg) {
		L[_0] |= 0x80000000L;
		L[2+_0] |= 0x80000000L;
		}
	}

 int
#ifdef KR_headers
strtordd(s, sp, rounding, dd) CONST char *s; char **sp; int rounding; double *dd;
#else
strtordd(CONST char *s, char **sp, int rounding, double *dd)
#endif
{
#ifdef Sudden_Underflow
	static FPI fpi0 = { 106, 1-1023, 2046-1023-106+1, 1, 1 };
#else
	static FPI fpi0 = { 106, 1-1023-53+1, 2046-1023-106+1, 1, 0 };
#endif
	FPI *fpi, fpi1;
	ULong bits[4];
	Long exp;
	int k;

	fpi = &fpi0;
	if (rounding != FPI_Round_near) {
		fpi1 = fpi0;
		fpi1.rounding = rounding;
		fpi = &fpi1;
		}
	k = strtodg(s, sp, fpi, &exp, bits);
	ULtodd((ULong*)dd, bits, exp, k);
	return k;
	}
