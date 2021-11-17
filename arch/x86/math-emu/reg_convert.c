// SPDX-License-Identifier: GPL-2.0
/*---------------------------------------------------------------------------+
 |  reg_convert.c                                                            |
 |                                                                           |
 |  Convert register representation.                                         |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1996,1997                                    |
 |                  W. Metzenthen, 22 Parker St, Ormond, Vic 3163, Australia |
 |                  E-mail   billm@suburbia.net                              |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "exception.h"
#include "fpu_emu.h"

int FPU_to_exp16(FPU_REG const *a, FPU_REG *x)
{
	int sign = getsign(a);

	*(long long *)&(x->sigl) = *(const long long *)&(a->sigl);

	/* Set up the exponent as a 16 bit quantity. */
	setexponent16(x, exponent(a));

	if (exponent16(x) == EXP_UNDER) {
		/* The number is a de-normal or pseudodenormal. */
		/* We only deal with the significand and exponent. */

		if (x->sigh & 0x80000000) {
			/* Is a pseudodenormal. */
			/* This is non-80486 behaviour because the number
			   loses its 'denormal' identity. */
			addexponent(x, 1);
		} else {
			/* Is a denormal. */
			addexponent(x, 1);
			FPU_normalize_nuo(x);
		}
	}

	if (!(x->sigh & 0x80000000)) {
		EXCEPTION(EX_INTERNAL | 0x180);
	}

	return sign;
}
