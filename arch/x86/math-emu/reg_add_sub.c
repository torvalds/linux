// SPDX-License-Identifier: GPL-2.0
/*---------------------------------------------------------------------------+
 |  reg_add_sub.c                                                            |
 |                                                                           |
 | Functions to add or subtract two registers and put the result in a third. |
 |                                                                           |
 | Copyright (C) 1992,1993,1997                                              |
 |                  W. Metzenthen, 22 Parker St, Ormond, Vic 3163, Australia |
 |                  E-mail   billm@suburbia.net                              |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 |  For each function, the destination may be any FPU_REG, including one of  |
 | the source FPU_REGs.                                                      |
 |  Each function returns 0 if the answer is o.k., otherwise a non-zero      |
 | value is returned, indicating either an exception condition or an         |
 | internal error.                                                           |
 +---------------------------------------------------------------------------*/

#include "exception.h"
#include "reg_constant.h"
#include "fpu_emu.h"
#include "control_w.h"
#include "fpu_system.h"

static
int add_sub_specials(FPU_REG const *a, u_char taga, u_char signa,
		     FPU_REG const *b, u_char tagb, u_char signb,
		     FPU_REG * dest, int deststnr, int control_w);

/*
  Operates on st(0) and st(n), or on st(0) and temporary data.
  The destination must be one of the source st(x).
  */
int FPU_add(FPU_REG const *b, u_char tagb, int deststnr, int control_w)
{
	FPU_REG *a = &st(0);
	FPU_REG *dest = &st(deststnr);
	u_char signb = getsign(b);
	u_char taga = FPU_gettag0();
	u_char signa = getsign(a);
	u_char saved_sign = getsign(dest);
	int diff, tag, expa, expb;

	if (!(taga | tagb)) {
		expa = exponent(a);
		expb = exponent(b);

	      valid_add:
		/* Both registers are valid */
		if (!(signa ^ signb)) {
			/* signs are the same */
			tag =
			    FPU_u_add(a, b, dest, control_w, signa, expa, expb);
		} else {
			/* The signs are different, so do a subtraction */
			diff = expa - expb;
			if (!diff) {
				diff = a->sigh - b->sigh;	/* This works only if the ms bits
								   are identical. */
				if (!diff) {
					diff = a->sigl > b->sigl;
					if (!diff)
						diff = -(a->sigl < b->sigl);
				}
			}

			if (diff > 0) {
				tag =
				    FPU_u_sub(a, b, dest, control_w, signa,
					      expa, expb);
			} else if (diff < 0) {
				tag =
				    FPU_u_sub(b, a, dest, control_w, signb,
					      expb, expa);
			} else {
				FPU_copy_to_regi(&CONST_Z, TAG_Zero, deststnr);
				/* sign depends upon rounding mode */
				setsign(dest, ((control_w & CW_RC) != RC_DOWN)
					? SIGN_POS : SIGN_NEG);
				return TAG_Zero;
			}
		}

		if (tag < 0) {
			setsign(dest, saved_sign);
			return tag;
		}
		FPU_settagi(deststnr, tag);
		return tag;
	}

	if (taga == TAG_Special)
		taga = FPU_Special(a);
	if (tagb == TAG_Special)
		tagb = FPU_Special(b);

	if (((taga == TAG_Valid) && (tagb == TW_Denormal))
	    || ((taga == TW_Denormal) && (tagb == TAG_Valid))
	    || ((taga == TW_Denormal) && (tagb == TW_Denormal))) {
		FPU_REG x, y;

		if (denormal_operand() < 0)
			return FPU_Exception;

		FPU_to_exp16(a, &x);
		FPU_to_exp16(b, &y);
		a = &x;
		b = &y;
		expa = exponent16(a);
		expb = exponent16(b);
		goto valid_add;
	}

	if ((taga == TW_NaN) || (tagb == TW_NaN)) {
		if (deststnr == 0)
			return real_2op_NaN(b, tagb, deststnr, a);
		else
			return real_2op_NaN(a, taga, deststnr, a);
	}

	return add_sub_specials(a, taga, signa, b, tagb, signb,
				dest, deststnr, control_w);
}

/* Subtract b from a.  (a-b) -> dest */
int FPU_sub(int flags, int rm, int control_w)
{
	FPU_REG const *a, *b;
	FPU_REG *dest;
	u_char taga, tagb, signa, signb, saved_sign, sign;
	int diff, tag = 0, expa, expb, deststnr;

	a = &st(0);
	taga = FPU_gettag0();

	deststnr = 0;
	if (flags & LOADED) {
		b = (FPU_REG *) rm;
		tagb = flags & 0x0f;
	} else {
		b = &st(rm);
		tagb = FPU_gettagi(rm);

		if (flags & DEST_RM)
			deststnr = rm;
	}

	signa = getsign(a);
	signb = getsign(b);

	if (flags & REV) {
		signa ^= SIGN_NEG;
		signb ^= SIGN_NEG;
	}

	dest = &st(deststnr);
	saved_sign = getsign(dest);

	if (!(taga | tagb)) {
		expa = exponent(a);
		expb = exponent(b);

	      valid_subtract:
		/* Both registers are valid */

		diff = expa - expb;

		if (!diff) {
			diff = a->sigh - b->sigh;	/* Works only if ms bits are identical */
			if (!diff) {
				diff = a->sigl > b->sigl;
				if (!diff)
					diff = -(a->sigl < b->sigl);
			}
		}

		switch ((((int)signa) * 2 + signb) / SIGN_NEG) {
		case 0:	/* P - P */
		case 3:	/* N - N */
			if (diff > 0) {
				/* |a| > |b| */
				tag =
				    FPU_u_sub(a, b, dest, control_w, signa,
					      expa, expb);
			} else if (diff == 0) {
				FPU_copy_to_regi(&CONST_Z, TAG_Zero, deststnr);

				/* sign depends upon rounding mode */
				setsign(dest, ((control_w & CW_RC) != RC_DOWN)
					? SIGN_POS : SIGN_NEG);
				return TAG_Zero;
			} else {
				sign = signa ^ SIGN_NEG;
				tag =
				    FPU_u_sub(b, a, dest, control_w, sign, expb,
					      expa);
			}
			break;
		case 1:	/* P - N */
			tag =
			    FPU_u_add(a, b, dest, control_w, SIGN_POS, expa,
				      expb);
			break;
		case 2:	/* N - P */
			tag =
			    FPU_u_add(a, b, dest, control_w, SIGN_NEG, expa,
				      expb);
			break;
#ifdef PARANOID
		default:
			EXCEPTION(EX_INTERNAL | 0x111);
			return -1;
#endif
		}
		if (tag < 0) {
			setsign(dest, saved_sign);
			return tag;
		}
		FPU_settagi(deststnr, tag);
		return tag;
	}

	if (taga == TAG_Special)
		taga = FPU_Special(a);
	if (tagb == TAG_Special)
		tagb = FPU_Special(b);

	if (((taga == TAG_Valid) && (tagb == TW_Denormal))
	    || ((taga == TW_Denormal) && (tagb == TAG_Valid))
	    || ((taga == TW_Denormal) && (tagb == TW_Denormal))) {
		FPU_REG x, y;

		if (denormal_operand() < 0)
			return FPU_Exception;

		FPU_to_exp16(a, &x);
		FPU_to_exp16(b, &y);
		a = &x;
		b = &y;
		expa = exponent16(a);
		expb = exponent16(b);

		goto valid_subtract;
	}

	if ((taga == TW_NaN) || (tagb == TW_NaN)) {
		FPU_REG const *d1, *d2;
		if (flags & REV) {
			d1 = b;
			d2 = a;
		} else {
			d1 = a;
			d2 = b;
		}
		if (flags & LOADED)
			return real_2op_NaN(b, tagb, deststnr, d1);
		if (flags & DEST_RM)
			return real_2op_NaN(a, taga, deststnr, d2);
		else
			return real_2op_NaN(b, tagb, deststnr, d2);
	}

	return add_sub_specials(a, taga, signa, b, tagb, signb ^ SIGN_NEG,
				dest, deststnr, control_w);
}

static
int add_sub_specials(FPU_REG const *a, u_char taga, u_char signa,
		     FPU_REG const *b, u_char tagb, u_char signb,
		     FPU_REG * dest, int deststnr, int control_w)
{
	if (((taga == TW_Denormal) || (tagb == TW_Denormal))
	    && (denormal_operand() < 0))
		return FPU_Exception;

	if (taga == TAG_Zero) {
		if (tagb == TAG_Zero) {
			/* Both are zero, result will be zero. */
			u_char different_signs = signa ^ signb;

			FPU_copy_to_regi(a, TAG_Zero, deststnr);
			if (different_signs) {
				/* Signs are different. */
				/* Sign of answer depends upon rounding mode. */
				setsign(dest, ((control_w & CW_RC) != RC_DOWN)
					? SIGN_POS : SIGN_NEG);
			} else
				setsign(dest, signa);	/* signa may differ from the sign of a. */
			return TAG_Zero;
		} else {
			reg_copy(b, dest);
			if ((tagb == TW_Denormal) && (b->sigh & 0x80000000)) {
				/* A pseudoDenormal, convert it. */
				addexponent(dest, 1);
				tagb = TAG_Valid;
			} else if (tagb > TAG_Empty)
				tagb = TAG_Special;
			setsign(dest, signb);	/* signb may differ from the sign of b. */
			FPU_settagi(deststnr, tagb);
			return tagb;
		}
	} else if (tagb == TAG_Zero) {
		reg_copy(a, dest);
		if ((taga == TW_Denormal) && (a->sigh & 0x80000000)) {
			/* A pseudoDenormal */
			addexponent(dest, 1);
			taga = TAG_Valid;
		} else if (taga > TAG_Empty)
			taga = TAG_Special;
		setsign(dest, signa);	/* signa may differ from the sign of a. */
		FPU_settagi(deststnr, taga);
		return taga;
	} else if (taga == TW_Infinity) {
		if ((tagb != TW_Infinity) || (signa == signb)) {
			FPU_copy_to_regi(a, TAG_Special, deststnr);
			setsign(dest, signa);	/* signa may differ from the sign of a. */
			return taga;
		}
		/* Infinity-Infinity is undefined. */
		return arith_invalid(deststnr);
	} else if (tagb == TW_Infinity) {
		FPU_copy_to_regi(b, TAG_Special, deststnr);
		setsign(dest, signb);	/* signb may differ from the sign of b. */
		return tagb;
	}
#ifdef PARANOID
	EXCEPTION(EX_INTERNAL | 0x101);
#endif

	return FPU_Exception;
}
