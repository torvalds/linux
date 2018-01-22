// SPDX-License-Identifier: GPL-2.0
/*---------------------------------------------------------------------------+
 |  reg_divide.c                                                             |
 |                                                                           |
 | Divide one FPU_REG by another and put the result in a destination FPU_REG.|
 |                                                                           |
 | Copyright (C) 1996                                                        |
 |                  W. Metzenthen, 22 Parker St, Ormond, Vic 3163, Australia |
 |                  E-mail   billm@jacobi.maths.monash.edu.au                |
 |                                                                           |
 |    Return value is the tag of the answer, or-ed with FPU_Exception if     |
 |    one was raised, or -1 on internal error.                               |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 | The destination may be any FPU_REG, including one of the source FPU_REGs. |
 +---------------------------------------------------------------------------*/

#include "exception.h"
#include "reg_constant.h"
#include "fpu_emu.h"
#include "fpu_system.h"

/*
  Divide one register by another and put the result into a third register.
  */
int FPU_div(int flags, int rm, int control_w)
{
	FPU_REG x, y;
	FPU_REG const *a, *b, *st0_ptr, *st_ptr;
	FPU_REG *dest;
	u_char taga, tagb, signa, signb, sign, saved_sign;
	int tag, deststnr;

	if (flags & DEST_RM)
		deststnr = rm;
	else
		deststnr = 0;

	if (flags & REV) {
		b = &st(0);
		st0_ptr = b;
		tagb = FPU_gettag0();
		if (flags & LOADED) {
			a = (FPU_REG *) rm;
			taga = flags & 0x0f;
		} else {
			a = &st(rm);
			st_ptr = a;
			taga = FPU_gettagi(rm);
		}
	} else {
		a = &st(0);
		st0_ptr = a;
		taga = FPU_gettag0();
		if (flags & LOADED) {
			b = (FPU_REG *) rm;
			tagb = flags & 0x0f;
		} else {
			b = &st(rm);
			st_ptr = b;
			tagb = FPU_gettagi(rm);
		}
	}

	signa = getsign(a);
	signb = getsign(b);

	sign = signa ^ signb;

	dest = &st(deststnr);
	saved_sign = getsign(dest);

	if (!(taga | tagb)) {
		/* Both regs Valid, this should be the most common case. */
		reg_copy(a, &x);
		reg_copy(b, &y);
		setpositive(&x);
		setpositive(&y);
		tag = FPU_u_div(&x, &y, dest, control_w, sign);

		if (tag < 0)
			return tag;

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
		if (denormal_operand() < 0)
			return FPU_Exception;

		FPU_to_exp16(a, &x);
		FPU_to_exp16(b, &y);
		tag = FPU_u_div(&x, &y, dest, control_w, sign);
		if (tag < 0)
			return tag;

		FPU_settagi(deststnr, tag);
		return tag;
	} else if ((taga <= TW_Denormal) && (tagb <= TW_Denormal)) {
		if (tagb != TAG_Zero) {
			/* Want to find Zero/Valid */
			if (tagb == TW_Denormal) {
				if (denormal_operand() < 0)
					return FPU_Exception;
			}

			/* The result is zero. */
			FPU_copy_to_regi(&CONST_Z, TAG_Zero, deststnr);
			setsign(dest, sign);
			return TAG_Zero;
		}
		/* We have an exception condition, either 0/0 or Valid/Zero. */
		if (taga == TAG_Zero) {
			/* 0/0 */
			return arith_invalid(deststnr);
		}
		/* Valid/Zero */
		return FPU_divide_by_zero(deststnr, sign);
	}
	/* Must have infinities, NaNs, etc */
	else if ((taga == TW_NaN) || (tagb == TW_NaN)) {
		if (flags & LOADED)
			return real_2op_NaN((FPU_REG *) rm, flags & 0x0f, 0,
					    st0_ptr);

		if (flags & DEST_RM) {
			int tag;
			tag = FPU_gettag0();
			if (tag == TAG_Special)
				tag = FPU_Special(st0_ptr);
			return real_2op_NaN(st0_ptr, tag, rm,
					    (flags & REV) ? st0_ptr : &st(rm));
		} else {
			int tag;
			tag = FPU_gettagi(rm);
			if (tag == TAG_Special)
				tag = FPU_Special(&st(rm));
			return real_2op_NaN(&st(rm), tag, 0,
					    (flags & REV) ? st0_ptr : &st(rm));
		}
	} else if (taga == TW_Infinity) {
		if (tagb == TW_Infinity) {
			/* infinity/infinity */
			return arith_invalid(deststnr);
		} else {
			/* tagb must be Valid or Zero */
			if ((tagb == TW_Denormal) && (denormal_operand() < 0))
				return FPU_Exception;

			/* Infinity divided by Zero or Valid does
			   not raise and exception, but returns Infinity */
			FPU_copy_to_regi(a, TAG_Special, deststnr);
			setsign(dest, sign);
			return taga;
		}
	} else if (tagb == TW_Infinity) {
		if ((taga == TW_Denormal) && (denormal_operand() < 0))
			return FPU_Exception;

		/* The result is zero. */
		FPU_copy_to_regi(&CONST_Z, TAG_Zero, deststnr);
		setsign(dest, sign);
		return TAG_Zero;
	}
#ifdef PARANOID
	else {
		EXCEPTION(EX_INTERNAL | 0x102);
		return FPU_Exception;
	}
#endif /* PARANOID */

	return 0;
}
