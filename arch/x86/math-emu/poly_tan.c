// SPDX-License-Identifier: GPL-2.0
/*---------------------------------------------------------------------------+
 |  poly_tan.c                                                               |
 |                                                                           |
 | Compute the tan of a FPU_REG, using a polynomial approximation.           |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1997,1999                                    |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail   billm@melbpc.org.au            |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "exception.h"
#include "reg_constant.h"
#include "fpu_emu.h"
#include "fpu_system.h"
#include "control_w.h"
#include "poly.h"

#define	HiPOWERop	3	/* odd poly, positive terms */
static const unsigned long long oddplterm[HiPOWERop] = {
	0x0000000000000000LL,
	0x0051a1cf08fca228LL,
	0x0000000071284ff7LL
};

#define	HiPOWERon	2	/* odd poly, negative terms */
static const unsigned long long oddnegterm[HiPOWERon] = {
	0x1291a9a184244e80LL,
	0x0000583245819c21LL
};

#define	HiPOWERep	2	/* even poly, positive terms */
static const unsigned long long evenplterm[HiPOWERep] = {
	0x0e848884b539e888LL,
	0x00003c7f18b887daLL
};

#define	HiPOWERen	2	/* even poly, negative terms */
static const unsigned long long evennegterm[HiPOWERen] = {
	0xf1f0200fd51569ccLL,
	0x003afb46105c4432LL
};

static const unsigned long long twothirds = 0xaaaaaaaaaaaaaaabLL;

/*--- poly_tan() ------------------------------------------------------------+
 |                                                                           |
 +---------------------------------------------------------------------------*/
void poly_tan(FPU_REG *st0_ptr)
{
	long int exponent;
	int invert;
	Xsig argSq, argSqSq, accumulatoro, accumulatore, accum,
	    argSignif, fix_up;
	unsigned long adj;

	exponent = exponent(st0_ptr);

#ifdef PARANOID
	if (signnegative(st0_ptr)) {	/* Can't hack a number < 0.0 */
		arith_invalid(0);
		return;
	}			/* Need a positive number */
#endif /* PARANOID */

	/* Split the problem into two domains, smaller and larger than pi/4 */
	if ((exponent == 0)
	    || ((exponent == -1) && (st0_ptr->sigh > 0xc90fdaa2))) {
		/* The argument is greater than (approx) pi/4 */
		invert = 1;
		accum.lsw = 0;
		XSIG_LL(accum) = significand(st0_ptr);

		if (exponent == 0) {
			/* The argument is >= 1.0 */
			/* Put the binary point at the left. */
			XSIG_LL(accum) <<= 1;
		}
		/* pi/2 in hex is: 1.921fb54442d18469 898CC51701B839A2 52049C1 */
		XSIG_LL(accum) = 0x921fb54442d18469LL - XSIG_LL(accum);
		/* This is a special case which arises due to rounding. */
		if (XSIG_LL(accum) == 0xffffffffffffffffLL) {
			FPU_settag0(TAG_Valid);
			significand(st0_ptr) = 0x8a51e04daabda360LL;
			setexponent16(st0_ptr,
				      (0x41 + EXTENDED_Ebias) | SIGN_Negative);
			return;
		}

		argSignif.lsw = accum.lsw;
		XSIG_LL(argSignif) = XSIG_LL(accum);
		exponent = -1 + norm_Xsig(&argSignif);
	} else {
		invert = 0;
		argSignif.lsw = 0;
		XSIG_LL(accum) = XSIG_LL(argSignif) = significand(st0_ptr);

		if (exponent < -1) {
			/* shift the argument right by the required places */
			if (FPU_shrx(&XSIG_LL(accum), -1 - exponent) >=
			    0x80000000U)
				XSIG_LL(accum)++;	/* round up */
		}
	}

	XSIG_LL(argSq) = XSIG_LL(accum);
	argSq.lsw = accum.lsw;
	mul_Xsig_Xsig(&argSq, &argSq);
	XSIG_LL(argSqSq) = XSIG_LL(argSq);
	argSqSq.lsw = argSq.lsw;
	mul_Xsig_Xsig(&argSqSq, &argSqSq);

	/* Compute the negative terms for the numerator polynomial */
	accumulatoro.msw = accumulatoro.midw = accumulatoro.lsw = 0;
	polynomial_Xsig(&accumulatoro, &XSIG_LL(argSqSq), oddnegterm,
			HiPOWERon - 1);
	mul_Xsig_Xsig(&accumulatoro, &argSq);
	negate_Xsig(&accumulatoro);
	/* Add the positive terms */
	polynomial_Xsig(&accumulatoro, &XSIG_LL(argSqSq), oddplterm,
			HiPOWERop - 1);

	/* Compute the positive terms for the denominator polynomial */
	accumulatore.msw = accumulatore.midw = accumulatore.lsw = 0;
	polynomial_Xsig(&accumulatore, &XSIG_LL(argSqSq), evenplterm,
			HiPOWERep - 1);
	mul_Xsig_Xsig(&accumulatore, &argSq);
	negate_Xsig(&accumulatore);
	/* Add the negative terms */
	polynomial_Xsig(&accumulatore, &XSIG_LL(argSqSq), evennegterm,
			HiPOWERen - 1);
	/* Multiply by arg^2 */
	mul64_Xsig(&accumulatore, &XSIG_LL(argSignif));
	mul64_Xsig(&accumulatore, &XSIG_LL(argSignif));
	/* de-normalize and divide by 2 */
	shr_Xsig(&accumulatore, -2 * (1 + exponent) + 1);
	negate_Xsig(&accumulatore);	/* This does 1 - accumulator */

	/* Now find the ratio. */
	if (accumulatore.msw == 0) {
		/* accumulatoro must contain 1.0 here, (actually, 0) but it
		   really doesn't matter what value we use because it will
		   have negligible effect in later calculations
		 */
		XSIG_LL(accum) = 0x8000000000000000LL;
		accum.lsw = 0;
	} else {
		div_Xsig(&accumulatoro, &accumulatore, &accum);
	}

	/* Multiply by 1/3 * arg^3 */
	mul64_Xsig(&accum, &XSIG_LL(argSignif));
	mul64_Xsig(&accum, &XSIG_LL(argSignif));
	mul64_Xsig(&accum, &XSIG_LL(argSignif));
	mul64_Xsig(&accum, &twothirds);
	shr_Xsig(&accum, -2 * (exponent + 1));

	/* tan(arg) = arg + accum */
	add_two_Xsig(&accum, &argSignif, &exponent);

	if (invert) {
		/* We now have the value of tan(pi_2 - arg) where pi_2 is an
		   approximation for pi/2
		 */
		/* The next step is to fix the answer to compensate for the
		   error due to the approximation used for pi/2
		 */

		/* This is (approx) delta, the error in our approx for pi/2
		   (see above). It has an exponent of -65
		 */
		XSIG_LL(fix_up) = 0x898cc51701b839a2LL;
		fix_up.lsw = 0;

		if (exponent == 0)
			adj = 0xffffffff;	/* We want approx 1.0 here, but
						   this is close enough. */
		else if (exponent > -30) {
			adj = accum.msw >> -(exponent + 1);	/* tan */
			adj = mul_32_32(adj, adj);	/* tan^2 */
		} else
			adj = 0;
		adj = mul_32_32(0x898cc517, adj);	/* delta * tan^2 */

		fix_up.msw += adj;
		if (!(fix_up.msw & 0x80000000)) {	/* did fix_up overflow ? */
			/* Yes, we need to add an msb */
			shr_Xsig(&fix_up, 1);
			fix_up.msw |= 0x80000000;
			shr_Xsig(&fix_up, 64 + exponent);
		} else
			shr_Xsig(&fix_up, 65 + exponent);

		add_two_Xsig(&accum, &fix_up, &exponent);

		/* accum now contains tan(pi/2 - arg).
		   Use tan(arg) = 1.0 / tan(pi/2 - arg)
		 */
		accumulatoro.lsw = accumulatoro.midw = 0;
		accumulatoro.msw = 0x80000000;
		div_Xsig(&accumulatoro, &accum, &accum);
		exponent = -exponent - 1;
	}

	/* Transfer the result */
	round_Xsig(&accum);
	FPU_settag0(TAG_Valid);
	significand(st0_ptr) = XSIG_LL(accum);
	setexponent16(st0_ptr, exponent + EXTENDED_Ebias);	/* Result is positive. */

}
