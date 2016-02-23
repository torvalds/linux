/*---------------------------------------------------------------------------+
 |  poly_sin.c                                                               |
 |                                                                           |
 |  Computation of an approximation of the sin function and the cosine       |
 |  function by a polynomial.                                                |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1997,1999                                    |
 |                  W. Metzenthen, 22 Parker St, Ormond, Vic 3163, Australia |
 |                  E-mail   billm@melbpc.org.au                             |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "exception.h"
#include "reg_constant.h"
#include "fpu_emu.h"
#include "fpu_system.h"
#include "control_w.h"
#include "poly.h"

#define	N_COEFF_P	4
#define	N_COEFF_N	4

static const unsigned long long pos_terms_l[N_COEFF_P] = {
	0xaaaaaaaaaaaaaaabLL,
	0x00d00d00d00cf906LL,
	0x000006b99159a8bbLL,
	0x000000000d7392e6LL
};

static const unsigned long long neg_terms_l[N_COEFF_N] = {
	0x2222222222222167LL,
	0x0002e3bc74aab624LL,
	0x0000000b09229062LL,
	0x00000000000c7973LL
};

#define	N_COEFF_PH	4
#define	N_COEFF_NH	4
static const unsigned long long pos_terms_h[N_COEFF_PH] = {
	0x0000000000000000LL,
	0x05b05b05b05b0406LL,
	0x000049f93edd91a9LL,
	0x00000000c9c9ed62LL
};

static const unsigned long long neg_terms_h[N_COEFF_NH] = {
	0xaaaaaaaaaaaaaa98LL,
	0x001a01a01a019064LL,
	0x0000008f76c68a77LL,
	0x0000000000d58f5eLL
};

/*--- poly_sine() -----------------------------------------------------------+
 |                                                                           |
 +---------------------------------------------------------------------------*/
void poly_sine(FPU_REG *st0_ptr)
{
	int exponent, echange;
	Xsig accumulator, argSqrd, argTo4;
	unsigned long fix_up, adj;
	unsigned long long fixed_arg;
	FPU_REG result;

	exponent = exponent(st0_ptr);

	accumulator.lsw = accumulator.midw = accumulator.msw = 0;

	/* Split into two ranges, for arguments below and above 1.0 */
	/* The boundary between upper and lower is approx 0.88309101259 */
	if ((exponent < -1)
	    || ((exponent == -1) && (st0_ptr->sigh <= 0xe21240aa))) {
		/* The argument is <= 0.88309101259 */

		argSqrd.msw = st0_ptr->sigh;
		argSqrd.midw = st0_ptr->sigl;
		argSqrd.lsw = 0;
		mul64_Xsig(&argSqrd, &significand(st0_ptr));
		shr_Xsig(&argSqrd, 2 * (-1 - exponent));
		argTo4.msw = argSqrd.msw;
		argTo4.midw = argSqrd.midw;
		argTo4.lsw = argSqrd.lsw;
		mul_Xsig_Xsig(&argTo4, &argTo4);

		polynomial_Xsig(&accumulator, &XSIG_LL(argTo4), neg_terms_l,
				N_COEFF_N - 1);
		mul_Xsig_Xsig(&accumulator, &argSqrd);
		negate_Xsig(&accumulator);

		polynomial_Xsig(&accumulator, &XSIG_LL(argTo4), pos_terms_l,
				N_COEFF_P - 1);

		shr_Xsig(&accumulator, 2);	/* Divide by four */
		accumulator.msw |= 0x80000000;	/* Add 1.0 */

		mul64_Xsig(&accumulator, &significand(st0_ptr));
		mul64_Xsig(&accumulator, &significand(st0_ptr));
		mul64_Xsig(&accumulator, &significand(st0_ptr));

		/* Divide by four, FPU_REG compatible, etc */
		exponent = 3 * exponent;

		/* The minimum exponent difference is 3 */
		shr_Xsig(&accumulator, exponent(st0_ptr) - exponent);

		negate_Xsig(&accumulator);
		XSIG_LL(accumulator) += significand(st0_ptr);

		echange = round_Xsig(&accumulator);

		setexponentpos(&result, exponent(st0_ptr) + echange);
	} else {
		/* The argument is > 0.88309101259 */
		/* We use sin(st(0)) = cos(pi/2-st(0)) */

		fixed_arg = significand(st0_ptr);

		if (exponent == 0) {
			/* The argument is >= 1.0 */

			/* Put the binary point at the left. */
			fixed_arg <<= 1;
		}
		/* pi/2 in hex is: 1.921fb54442d18469 898CC51701B839A2 52049C1 */
		fixed_arg = 0x921fb54442d18469LL - fixed_arg;
		/* There is a special case which arises due to rounding, to fix here. */
		if (fixed_arg == 0xffffffffffffffffLL)
			fixed_arg = 0;

		XSIG_LL(argSqrd) = fixed_arg;
		argSqrd.lsw = 0;
		mul64_Xsig(&argSqrd, &fixed_arg);

		XSIG_LL(argTo4) = XSIG_LL(argSqrd);
		argTo4.lsw = argSqrd.lsw;
		mul_Xsig_Xsig(&argTo4, &argTo4);

		polynomial_Xsig(&accumulator, &XSIG_LL(argTo4), neg_terms_h,
				N_COEFF_NH - 1);
		mul_Xsig_Xsig(&accumulator, &argSqrd);
		negate_Xsig(&accumulator);

		polynomial_Xsig(&accumulator, &XSIG_LL(argTo4), pos_terms_h,
				N_COEFF_PH - 1);
		negate_Xsig(&accumulator);

		mul64_Xsig(&accumulator, &fixed_arg);
		mul64_Xsig(&accumulator, &fixed_arg);

		shr_Xsig(&accumulator, 3);
		negate_Xsig(&accumulator);

		add_Xsig_Xsig(&accumulator, &argSqrd);

		shr_Xsig(&accumulator, 1);

		accumulator.lsw |= 1;	/* A zero accumulator here would cause problems */
		negate_Xsig(&accumulator);

		/* The basic computation is complete. Now fix the answer to
		   compensate for the error due to the approximation used for
		   pi/2
		 */

		/* This has an exponent of -65 */
		fix_up = 0x898cc517;
		/* The fix-up needs to be improved for larger args */
		if (argSqrd.msw & 0xffc00000) {
			/* Get about 32 bit precision in these: */
			fix_up -= mul_32_32(0x898cc517, argSqrd.msw) / 6;
		}
		fix_up = mul_32_32(fix_up, LL_MSW(fixed_arg));

		adj = accumulator.lsw;	/* temp save */
		accumulator.lsw -= fix_up;
		if (accumulator.lsw > adj)
			XSIG_LL(accumulator)--;

		echange = round_Xsig(&accumulator);

		setexponentpos(&result, echange - 1);
	}

	significand(&result) = XSIG_LL(accumulator);
	setsign(&result, getsign(st0_ptr));
	FPU_copy_to_reg0(&result, TAG_Valid);

#ifdef PARANOID
	if ((exponent(&result) >= 0)
	    && (significand(&result) > 0x8000000000000000LL)) {
		EXCEPTION(EX_INTERNAL | 0x150);
	}
#endif /* PARANOID */

}

/*--- poly_cos() ------------------------------------------------------------+
 |                                                                           |
 +---------------------------------------------------------------------------*/
void poly_cos(FPU_REG *st0_ptr)
{
	FPU_REG result;
	long int exponent, exp2, echange;
	Xsig accumulator, argSqrd, fix_up, argTo4;
	unsigned long long fixed_arg;

#ifdef PARANOID
	if ((exponent(st0_ptr) > 0)
	    || ((exponent(st0_ptr) == 0)
		&& (significand(st0_ptr) > 0xc90fdaa22168c234LL))) {
		EXCEPTION(EX_Invalid);
		FPU_copy_to_reg0(&CONST_QNaN, TAG_Special);
		return;
	}
#endif /* PARANOID */

	exponent = exponent(st0_ptr);

	accumulator.lsw = accumulator.midw = accumulator.msw = 0;

	if ((exponent < -1)
	    || ((exponent == -1) && (st0_ptr->sigh <= 0xb00d6f54))) {
		/* arg is < 0.687705 */

		argSqrd.msw = st0_ptr->sigh;
		argSqrd.midw = st0_ptr->sigl;
		argSqrd.lsw = 0;
		mul64_Xsig(&argSqrd, &significand(st0_ptr));

		if (exponent < -1) {
			/* shift the argument right by the required places */
			shr_Xsig(&argSqrd, 2 * (-1 - exponent));
		}

		argTo4.msw = argSqrd.msw;
		argTo4.midw = argSqrd.midw;
		argTo4.lsw = argSqrd.lsw;
		mul_Xsig_Xsig(&argTo4, &argTo4);

		polynomial_Xsig(&accumulator, &XSIG_LL(argTo4), neg_terms_h,
				N_COEFF_NH - 1);
		mul_Xsig_Xsig(&accumulator, &argSqrd);
		negate_Xsig(&accumulator);

		polynomial_Xsig(&accumulator, &XSIG_LL(argTo4), pos_terms_h,
				N_COEFF_PH - 1);
		negate_Xsig(&accumulator);

		mul64_Xsig(&accumulator, &significand(st0_ptr));
		mul64_Xsig(&accumulator, &significand(st0_ptr));
		shr_Xsig(&accumulator, -2 * (1 + exponent));

		shr_Xsig(&accumulator, 3);
		negate_Xsig(&accumulator);

		add_Xsig_Xsig(&accumulator, &argSqrd);

		shr_Xsig(&accumulator, 1);

		/* It doesn't matter if accumulator is all zero here, the
		   following code will work ok */
		negate_Xsig(&accumulator);

		if (accumulator.lsw & 0x80000000)
			XSIG_LL(accumulator)++;
		if (accumulator.msw == 0) {
			/* The result is 1.0 */
			FPU_copy_to_reg0(&CONST_1, TAG_Valid);
			return;
		} else {
			significand(&result) = XSIG_LL(accumulator);

			/* will be a valid positive nr with expon = -1 */
			setexponentpos(&result, -1);
		}
	} else {
		fixed_arg = significand(st0_ptr);

		if (exponent == 0) {
			/* The argument is >= 1.0 */

			/* Put the binary point at the left. */
			fixed_arg <<= 1;
		}
		/* pi/2 in hex is: 1.921fb54442d18469 898CC51701B839A2 52049C1 */
		fixed_arg = 0x921fb54442d18469LL - fixed_arg;
		/* There is a special case which arises due to rounding, to fix here. */
		if (fixed_arg == 0xffffffffffffffffLL)
			fixed_arg = 0;

		exponent = -1;
		exp2 = -1;

		/* A shift is needed here only for a narrow range of arguments,
		   i.e. for fixed_arg approx 2^-32, but we pick up more... */
		if (!(LL_MSW(fixed_arg) & 0xffff0000)) {
			fixed_arg <<= 16;
			exponent -= 16;
			exp2 -= 16;
		}

		XSIG_LL(argSqrd) = fixed_arg;
		argSqrd.lsw = 0;
		mul64_Xsig(&argSqrd, &fixed_arg);

		if (exponent < -1) {
			/* shift the argument right by the required places */
			shr_Xsig(&argSqrd, 2 * (-1 - exponent));
		}

		argTo4.msw = argSqrd.msw;
		argTo4.midw = argSqrd.midw;
		argTo4.lsw = argSqrd.lsw;
		mul_Xsig_Xsig(&argTo4, &argTo4);

		polynomial_Xsig(&accumulator, &XSIG_LL(argTo4), neg_terms_l,
				N_COEFF_N - 1);
		mul_Xsig_Xsig(&accumulator, &argSqrd);
		negate_Xsig(&accumulator);

		polynomial_Xsig(&accumulator, &XSIG_LL(argTo4), pos_terms_l,
				N_COEFF_P - 1);

		shr_Xsig(&accumulator, 2);	/* Divide by four */
		accumulator.msw |= 0x80000000;	/* Add 1.0 */

		mul64_Xsig(&accumulator, &fixed_arg);
		mul64_Xsig(&accumulator, &fixed_arg);
		mul64_Xsig(&accumulator, &fixed_arg);

		/* Divide by four, FPU_REG compatible, etc */
		exponent = 3 * exponent;

		/* The minimum exponent difference is 3 */
		shr_Xsig(&accumulator, exp2 - exponent);

		negate_Xsig(&accumulator);
		XSIG_LL(accumulator) += fixed_arg;

		/* The basic computation is complete. Now fix the answer to
		   compensate for the error due to the approximation used for
		   pi/2
		 */

		/* This has an exponent of -65 */
		XSIG_LL(fix_up) = 0x898cc51701b839a2ll;
		fix_up.lsw = 0;

		/* The fix-up needs to be improved for larger args */
		if (argSqrd.msw & 0xffc00000) {
			/* Get about 32 bit precision in these: */
			fix_up.msw -= mul_32_32(0x898cc517, argSqrd.msw) / 2;
			fix_up.msw += mul_32_32(0x898cc517, argTo4.msw) / 24;
		}

		exp2 += norm_Xsig(&accumulator);
		shr_Xsig(&accumulator, 1);	/* Prevent overflow */
		exp2++;
		shr_Xsig(&fix_up, 65 + exp2);

		add_Xsig_Xsig(&accumulator, &fix_up);

		echange = round_Xsig(&accumulator);

		setexponentpos(&result, exp2 + echange);
		significand(&result) = XSIG_LL(accumulator);
	}

	FPU_copy_to_reg0(&result, TAG_Valid);

#ifdef PARANOID
	if ((exponent(&result) >= 0)
	    && (significand(&result) > 0x8000000000000000LL)) {
		EXCEPTION(EX_INTERNAL | 0x151);
	}
#endif /* PARANOID */

}
