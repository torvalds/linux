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

 int
#ifdef KR_headers
strtoIxL(s, sp, a, b) CONST char *s; char **sp; void *a; void *b;
#else
strtoIxL(CONST char *s, char **sp, void *a, void *b)
#endif
{
	static FPI fpi = { 64, 1-16383-64+1, 32766 - 16383 - 64 + 1, 1, SI };
	Long exp[2];
	Bigint *B[2];
	int k, rv[2];
	ULong *L = (ULong *)a, *M = (ULong *)b;

	B[0] = Balloc(1);
	B[0]->wds = 2;
	k = strtoIg(s, sp, &fpi, exp, B, rv);
	ULtoxL(L, B[0]->x, exp[0], rv[0]);
	Bfree(B[0]);
	if (B[1]) {
		ULtoxL(M, B[1]->x, exp[1], rv[1]);
		Bfree(B[1]);
		}
	else {
		M[0] = L[0];
		M[1] = L[1];
		M[2] = L[2];
		}
	return k;
	}
