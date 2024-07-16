// SPDX-License-Identifier: GPL-2.0
/*---------------------------------------------------------------------------+
 |  poly_l2.c                                                                |
 |                                                                           |
 | Compute the base 2 log of a FPU_REG, using a polynomial approximation.    |
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

static void log2_kernel(FPU_REG const *arg, u_char argsign,
			Xsig * accum_result, long int *expon);

/*--- poly_l2() -------------------------------------------------------------+
 |   Base 2 logarithm by a polynomial approximation.                         |
 +---------------------------------------------------------------------------*/
void poly_l2(FPU_REG *st0_ptr, FPU_REG *st1_ptr, u_char st1_sign)
{
	long int exponent, expon, expon_expon;
	Xsig accumulator, expon_accum, yaccum;
	u_char sign, argsign;
	FPU_REG x;
	int tag;

	exponent = exponent16(st0_ptr);

	/* From st0_ptr, make a number > sqrt(2)/2 and < sqrt(2) */
	if (st0_ptr->sigh > (unsigned)0xb504f334) {
		/* Treat as  sqrt(2)/2 < st0_ptr < 1 */
		significand(&x) = -significand(st0_ptr);
		setexponent16(&x, -1);
		exponent++;
		argsign = SIGN_NEG;
	} else {
		/* Treat as  1 <= st0_ptr < sqrt(2) */
		x.sigh = st0_ptr->sigh - 0x80000000;
		x.sigl = st0_ptr->sigl;
		setexponent16(&x, 0);
		argsign = SIGN_POS;
	}
	tag = FPU_normalize_nuo(&x);

	if (tag == TAG_Zero) {
		expon = 0;
		accumulator.msw = accumulator.midw = accumulator.lsw = 0;
	} else {
		log2_kernel(&x, argsign, &accumulator, &expon);
	}

	if (exponent < 0) {
		sign = SIGN_NEG;
		exponent = -exponent;
	} else
		sign = SIGN_POS;
	expon_accum.msw = exponent;
	expon_accum.midw = expon_accum.lsw = 0;
	if (exponent) {
		expon_expon = 31 + norm_Xsig(&expon_accum);
		shr_Xsig(&accumulator, expon_expon - expon);

		if (sign ^ argsign)
			negate_Xsig(&accumulator);
		add_Xsig_Xsig(&accumulator, &expon_accum);
	} else {
		expon_expon = expon;
		sign = argsign;
	}

	yaccum.lsw = 0;
	XSIG_LL(yaccum) = significand(st1_ptr);
	mul_Xsig_Xsig(&accumulator, &yaccum);

	expon_expon += round_Xsig(&accumulator);

	if (accumulator.msw == 0) {
		FPU_copy_to_reg1(&CONST_Z, TAG_Zero);
		return;
	}

	significand(st1_ptr) = XSIG_LL(accumulator);
	setexponent16(st1_ptr, expon_expon + exponent16(st1_ptr) + 1);

	tag = FPU_round(st1_ptr, 1, 0, FULL_PRECISION, sign ^ st1_sign);
	FPU_settagi(1, tag);

	set_precision_flag_up();	/* 80486 appears to always do this */

	return;

}

/*--- poly_l2p1() -----------------------------------------------------------+
 |   Base 2 logarithm by a polynomial approximation.                         |
 |   log2(x+1)                                                               |
 +---------------------------------------------------------------------------*/
int poly_l2p1(u_char sign0, u_char sign1,
	      FPU_REG * st0_ptr, FPU_REG * st1_ptr, FPU_REG * dest)
{
	u_char tag;
	long int exponent;
	Xsig accumulator, yaccum;

	if (exponent16(st0_ptr) < 0) {
		log2_kernel(st0_ptr, sign0, &accumulator, &exponent);

		yaccum.lsw = 0;
		XSIG_LL(yaccum) = significand(st1_ptr);
		mul_Xsig_Xsig(&accumulator, &yaccum);

		exponent += round_Xsig(&accumulator);

		exponent += exponent16(st1_ptr) + 1;
		if (exponent < EXP_WAY_UNDER)
			exponent = EXP_WAY_UNDER;

		significand(dest) = XSIG_LL(accumulator);
		setexponent16(dest, exponent);

		tag = FPU_round(dest, 1, 0, FULL_PRECISION, sign0 ^ sign1);
		FPU_settagi(1, tag);

		if (tag == TAG_Valid)
			set_precision_flag_up();	/* 80486 appears to always do this */
	} else {
		/* The magnitude of st0_ptr is far too large. */

		if (sign0 != SIGN_POS) {
			/* Trying to get the log of a negative number. */
#ifdef PECULIAR_486		/* Stupid 80486 doesn't worry about log(negative). */
			changesign(st1_ptr);
#else
			if (arith_invalid(1) < 0)
				return 1;
#endif /* PECULIAR_486 */
		}

		/* 80486 appears to do this */
		if (sign0 == SIGN_NEG)
			set_precision_flag_down();
		else
			set_precision_flag_up();
	}

	if (exponent(dest) <= EXP_UNDER)
		EXCEPTION(EX_Underflow);

	return 0;

}

#undef HIPOWER
#define	HIPOWER	10
static const unsigned long long logterms[HIPOWER] = {
	0x2a8eca5705fc2ef0LL,
	0xf6384ee1d01febceLL,
	0x093bb62877cdf642LL,
	0x006985d8a9ec439bLL,
	0x0005212c4f55a9c8LL,
	0x00004326a16927f0LL,
	0x0000038d1d80a0e7LL,
	0x0000003141cc80c6LL,
	0x00000002b1668c9fLL,
	0x000000002c7a46aaLL
};

static const unsigned long leadterm = 0xb8000000;

/*--- log2_kernel() ---------------------------------------------------------+
 |   Base 2 logarithm by a polynomial approximation.                         |
 |   log2(x+1)                                                               |
 +---------------------------------------------------------------------------*/
static void log2_kernel(FPU_REG const *arg, u_char argsign, Xsig *accum_result,
			long int *expon)
{
	long int exponent, adj;
	unsigned long long Xsq;
	Xsig accumulator, Numer, Denom, argSignif, arg_signif;

	exponent = exponent16(arg);
	Numer.lsw = Denom.lsw = 0;
	XSIG_LL(Numer) = XSIG_LL(Denom) = significand(arg);
	if (argsign == SIGN_POS) {
		shr_Xsig(&Denom, 2 - (1 + exponent));
		Denom.msw |= 0x80000000;
		div_Xsig(&Numer, &Denom, &argSignif);
	} else {
		shr_Xsig(&Denom, 1 - (1 + exponent));
		negate_Xsig(&Denom);
		if (Denom.msw & 0x80000000) {
			div_Xsig(&Numer, &Denom, &argSignif);
			exponent++;
		} else {
			/* Denom must be 1.0 */
			argSignif.lsw = Numer.lsw;
			argSignif.midw = Numer.midw;
			argSignif.msw = Numer.msw;
		}
	}

#ifndef PECULIAR_486
	/* Should check here that  |local_arg|  is within the valid range */
	if (exponent >= -2) {
		if ((exponent > -2) || (argSignif.msw > (unsigned)0xafb0ccc0)) {
			/* The argument is too large */
		}
	}
#endif /* PECULIAR_486 */

	arg_signif.lsw = argSignif.lsw;
	XSIG_LL(arg_signif) = XSIG_LL(argSignif);
	adj = norm_Xsig(&argSignif);
	accumulator.lsw = argSignif.lsw;
	XSIG_LL(accumulator) = XSIG_LL(argSignif);
	mul_Xsig_Xsig(&accumulator, &accumulator);
	shr_Xsig(&accumulator, 2 * (-1 - (1 + exponent + adj)));
	Xsq = XSIG_LL(accumulator);
	if (accumulator.lsw & 0x80000000)
		Xsq++;

	accumulator.msw = accumulator.midw = accumulator.lsw = 0;
	/* Do the basic fixed point polynomial evaluation */
	polynomial_Xsig(&accumulator, &Xsq, logterms, HIPOWER - 1);

	mul_Xsig_Xsig(&accumulator, &argSignif);
	shr_Xsig(&accumulator, 6 - adj);

	mul32_Xsig(&arg_signif, leadterm);
	add_two_Xsig(&accumulator, &arg_signif, &exponent);

	*expon = exponent + 1;
	accum_result->lsw = accumulator.lsw;
	accum_result->midw = accumulator.midw;
	accum_result->msw = accumulator.msw;

}
