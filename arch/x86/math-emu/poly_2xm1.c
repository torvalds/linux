// SPDX-License-Identifier: GPL-2.0
/*---------------------------------------------------------------------------+
 |  poly_2xm1.c                                                              |
 |                                                                           |
 | Function to compute 2^x-1 by a polynomial approximation.                  |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1997                                         |
 |                  W. Metzenthen, 22 Parker St, Ormond, Vic 3163, Australia |
 |                  E-mail   billm@suburbia.net                              |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "exception.h"
#include "reg_constant.h"
#include "fpu_emu.h"
#include "fpu_system.h"
#include "control_w.h"
#include "poly.h"

#define	HIPOWER	11
static const unsigned long long lterms[HIPOWER] = {
	0x0000000000000000LL,	/* This term done separately as 12 bytes */
	0xf5fdeffc162c7543LL,
	0x1c6b08d704a0bfa6LL,
	0x0276556df749cc21LL,
	0x002bb0ffcf14f6b8LL,
	0x0002861225ef751cLL,
	0x00001ffcbfcd5422LL,
	0x00000162c005d5f1LL,
	0x0000000da96ccb1bLL,
	0x0000000078d1b897LL,
	0x000000000422b029LL
};

static const Xsig hiterm = MK_XSIG(0xb17217f7, 0xd1cf79ab, 0xc8a39194);

/* Four slices: 0.0 : 0.25 : 0.50 : 0.75 : 1.0,
   These numbers are 2^(1/4), 2^(1/2), and 2^(3/4)
 */
static const Xsig shiftterm0 = MK_XSIG(0, 0, 0);
static const Xsig shiftterm1 = MK_XSIG(0x9837f051, 0x8db8a96f, 0x46ad2318);
static const Xsig shiftterm2 = MK_XSIG(0xb504f333, 0xf9de6484, 0x597d89b3);
static const Xsig shiftterm3 = MK_XSIG(0xd744fcca, 0xd69d6af4, 0x39a68bb9);

static const Xsig *shiftterm[] = { &shiftterm0, &shiftterm1,
	&shiftterm2, &shiftterm3
};

/*--- poly_2xm1() -----------------------------------------------------------+
 | Requires st(0) which is TAG_Valid and < 1.                                |
 +---------------------------------------------------------------------------*/
int poly_2xm1(u_char sign, FPU_REG *arg, FPU_REG *result)
{
	long int exponent, shift;
	unsigned long long Xll;
	Xsig accumulator, Denom, argSignif;
	u_char tag;

	exponent = exponent16(arg);

#ifdef PARANOID
	if (exponent >= 0) {	/* Don't want a |number| >= 1.0 */
		/* Number negative, too large, or not Valid. */
		EXCEPTION(EX_INTERNAL | 0x127);
		return 1;
	}
#endif /* PARANOID */

	argSignif.lsw = 0;
	XSIG_LL(argSignif) = Xll = significand(arg);

	if (exponent == -1) {
		shift = (argSignif.msw & 0x40000000) ? 3 : 2;
		/* subtract 0.5 or 0.75 */
		exponent -= 2;
		XSIG_LL(argSignif) <<= 2;
		Xll <<= 2;
	} else if (exponent == -2) {
		shift = 1;
		/* subtract 0.25 */
		exponent--;
		XSIG_LL(argSignif) <<= 1;
		Xll <<= 1;
	} else
		shift = 0;

	if (exponent < -2) {
		/* Shift the argument right by the required places. */
		if (FPU_shrx(&Xll, -2 - exponent) >= 0x80000000U)
			Xll++;	/* round up */
	}

	accumulator.lsw = accumulator.midw = accumulator.msw = 0;
	polynomial_Xsig(&accumulator, &Xll, lterms, HIPOWER - 1);
	mul_Xsig_Xsig(&accumulator, &argSignif);
	shr_Xsig(&accumulator, 3);

	mul_Xsig_Xsig(&argSignif, &hiterm);	/* The leading term */
	add_two_Xsig(&accumulator, &argSignif, &exponent);

	if (shift) {
		/* The argument is large, use the identity:
		   f(x+a) = f(a) * (f(x) + 1) - 1;
		 */
		shr_Xsig(&accumulator, -exponent);
		accumulator.msw |= 0x80000000;	/* add 1.0 */
		mul_Xsig_Xsig(&accumulator, shiftterm[shift]);
		accumulator.msw &= 0x3fffffff;	/* subtract 1.0 */
		exponent = 1;
	}

	if (sign != SIGN_POS) {
		/* The argument is negative, use the identity:
		   f(-x) = -f(x) / (1 + f(x))
		 */
		Denom.lsw = accumulator.lsw;
		XSIG_LL(Denom) = XSIG_LL(accumulator);
		if (exponent < 0)
			shr_Xsig(&Denom, -exponent);
		else if (exponent > 0) {
			/* exponent must be 1 here */
			XSIG_LL(Denom) <<= 1;
			if (Denom.lsw & 0x80000000)
				XSIG_LL(Denom) |= 1;
			(Denom.lsw) <<= 1;
		}
		Denom.msw |= 0x80000000;	/* add 1.0 */
		div_Xsig(&accumulator, &Denom, &accumulator);
	}

	/* Convert to 64 bit signed-compatible */
	exponent += round_Xsig(&accumulator);

	result = &st(0);
	significand(result) = XSIG_LL(accumulator);
	setexponent16(result, exponent);

	tag = FPU_round(result, 1, 0, FULL_PRECISION, sign);

	setsign(result, sign);
	FPU_settag0(tag);

	return 0;

}
