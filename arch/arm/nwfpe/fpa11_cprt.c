/*
    NetWinder Floating Point Emulator
    (c) Rebel.COM, 1998,1999
    (c) Philip Blundell, 1999, 2001

    Direct questions, comments to Scott Bambrough <scottb@netwinder.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "fpa11.h"
#include "fpopcode.h"
#include "fpa11.inl"
#include "fpmodule.h"
#include "fpmodule.inl"
#include "softfloat.h"

unsigned int PerformFLT(const unsigned int opcode);
unsigned int PerformFIX(const unsigned int opcode);

static unsigned int PerformComparison(const unsigned int opcode);

unsigned int EmulateCPRT(const unsigned int opcode)
{

	if (opcode & 0x800000) {
		/* This is some variant of a comparison (PerformComparison
		   will sort out which one).  Since most of the other CPRT
		   instructions are oddball cases of some sort or other it
		   makes sense to pull this out into a fast path.  */
		return PerformComparison(opcode);
	}

	/* Hint to GCC that we'd like a jump table rather than a load of CMPs */
	switch ((opcode & 0x700000) >> 20) {
	case FLT_CODE >> 20:
		return PerformFLT(opcode);
		break;
	case FIX_CODE >> 20:
		return PerformFIX(opcode);
		break;

	case WFS_CODE >> 20:
		writeFPSR(readRegister(getRd(opcode)));
		break;
	case RFS_CODE >> 20:
		writeRegister(getRd(opcode), readFPSR());
		break;

	default:
		return 0;
	}

	return 1;
}

unsigned int PerformFLT(const unsigned int opcode)
{
	FPA11 *fpa11 = GET_FPA11();
	struct roundingData roundData;

	roundData.mode = SetRoundingMode(opcode);
	roundData.precision = SetRoundingPrecision(opcode);
	roundData.exception = 0;

	switch (opcode & MASK_ROUNDING_PRECISION) {
	case ROUND_SINGLE:
		{
			fpa11->fType[getFn(opcode)] = typeSingle;
			fpa11->fpreg[getFn(opcode)].fSingle = int32_to_float32(&roundData, readRegister(getRd(opcode)));
		}
		break;

	case ROUND_DOUBLE:
		{
			fpa11->fType[getFn(opcode)] = typeDouble;
			fpa11->fpreg[getFn(opcode)].fDouble = int32_to_float64(readRegister(getRd(opcode)));
		}
		break;

#ifdef CONFIG_FPE_NWFPE_XP
	case ROUND_EXTENDED:
		{
			fpa11->fType[getFn(opcode)] = typeExtended;
			fpa11->fpreg[getFn(opcode)].fExtended = int32_to_floatx80(readRegister(getRd(opcode)));
		}
		break;
#endif

	default:
		return 0;
	}

	if (roundData.exception)
		float_raise(roundData.exception);

	return 1;
}

unsigned int PerformFIX(const unsigned int opcode)
{
	FPA11 *fpa11 = GET_FPA11();
	unsigned int Fn = getFm(opcode);
	struct roundingData roundData;

	roundData.mode = SetRoundingMode(opcode);
	roundData.precision = SetRoundingPrecision(opcode);
	roundData.exception = 0;

	switch (fpa11->fType[Fn]) {
	case typeSingle:
		{
			writeRegister(getRd(opcode), float32_to_int32(&roundData, fpa11->fpreg[Fn].fSingle));
		}
		break;

	case typeDouble:
		{
			writeRegister(getRd(opcode), float64_to_int32(&roundData, fpa11->fpreg[Fn].fDouble));
		}
		break;

#ifdef CONFIG_FPE_NWFPE_XP
	case typeExtended:
		{
			writeRegister(getRd(opcode), floatx80_to_int32(&roundData, fpa11->fpreg[Fn].fExtended));
		}
		break;
#endif

	default:
		return 0;
	}

	if (roundData.exception)
		float_raise(roundData.exception);

	return 1;
}

/* This instruction sets the flags N, Z, C, V in the FPSR. */
static unsigned int PerformComparison(const unsigned int opcode)
{
	FPA11 *fpa11 = GET_FPA11();
	unsigned int Fn = getFn(opcode), Fm = getFm(opcode);
	int e_flag = opcode & 0x400000;	/* 1 if CxFE */
	int n_flag = opcode & 0x200000;	/* 1 if CNxx */
	unsigned int flags = 0;

#ifdef CONFIG_FPE_NWFPE_XP
	floatx80 rFn, rFm;

	/* Check for unordered condition and convert all operands to 80-bit
	   format.
	   ?? Might be some mileage in avoiding this conversion if possible.
	   Eg, if both operands are 32-bit, detect this and do a 32-bit
	   comparison (cheaper than an 80-bit one).  */
	switch (fpa11->fType[Fn]) {
	case typeSingle:
		//printk("single.\n");
		if (float32_is_nan(fpa11->fpreg[Fn].fSingle))
			goto unordered;
		rFn = float32_to_floatx80(fpa11->fpreg[Fn].fSingle);
		break;

	case typeDouble:
		//printk("double.\n");
		if (float64_is_nan(fpa11->fpreg[Fn].fDouble))
			goto unordered;
		rFn = float64_to_floatx80(fpa11->fpreg[Fn].fDouble);
		break;

	case typeExtended:
		//printk("extended.\n");
		if (floatx80_is_nan(fpa11->fpreg[Fn].fExtended))
			goto unordered;
		rFn = fpa11->fpreg[Fn].fExtended;
		break;

	default:
		return 0;
	}

	if (CONSTANT_FM(opcode)) {
		//printk("Fm is a constant: #%d.\n",Fm);
		rFm = getExtendedConstant(Fm);
		if (floatx80_is_nan(rFm))
			goto unordered;
	} else {
		//printk("Fm = r%d which contains a ",Fm);
		switch (fpa11->fType[Fm]) {
		case typeSingle:
			//printk("single.\n");
			if (float32_is_nan(fpa11->fpreg[Fm].fSingle))
				goto unordered;
			rFm = float32_to_floatx80(fpa11->fpreg[Fm].fSingle);
			break;

		case typeDouble:
			//printk("double.\n");
			if (float64_is_nan(fpa11->fpreg[Fm].fDouble))
				goto unordered;
			rFm = float64_to_floatx80(fpa11->fpreg[Fm].fDouble);
			break;

		case typeExtended:
			//printk("extended.\n");
			if (floatx80_is_nan(fpa11->fpreg[Fm].fExtended))
				goto unordered;
			rFm = fpa11->fpreg[Fm].fExtended;
			break;

		default:
			return 0;
		}
	}

	if (n_flag)
		rFm.high ^= 0x8000;

	/* test for less than condition */
	if (floatx80_lt(rFn, rFm))
		flags |= CC_NEGATIVE;

	/* test for equal condition */
	if (floatx80_eq(rFn, rFm))
		flags |= CC_ZERO;

	/* test for greater than or equal condition */
	if (floatx80_lt(rFm, rFn))
		flags |= CC_CARRY;

#else
	if (CONSTANT_FM(opcode)) {
		/* Fm is a constant.  Do the comparison in whatever precision
		   Fn happens to be stored in.  */
		if (fpa11->fType[Fn] == typeSingle) {
			float32 rFm = getSingleConstant(Fm);
			float32 rFn = fpa11->fpreg[Fn].fSingle;

			if (float32_is_nan(rFn))
				goto unordered;

			if (n_flag)
				rFm ^= 0x80000000;

			/* test for less than condition */
			if (float32_lt_nocheck(rFn, rFm))
				flags |= CC_NEGATIVE;

			/* test for equal condition */
			if (float32_eq_nocheck(rFn, rFm))
				flags |= CC_ZERO;

			/* test for greater than or equal condition */
			if (float32_lt_nocheck(rFm, rFn))
				flags |= CC_CARRY;
		} else {
			float64 rFm = getDoubleConstant(Fm);
			float64 rFn = fpa11->fpreg[Fn].fDouble;

			if (float64_is_nan(rFn))
				goto unordered;

			if (n_flag)
				rFm ^= 0x8000000000000000ULL;

			/* test for less than condition */
			if (float64_lt_nocheck(rFn, rFm))
				flags |= CC_NEGATIVE;

			/* test for equal condition */
			if (float64_eq_nocheck(rFn, rFm))
				flags |= CC_ZERO;

			/* test for greater than or equal condition */
			if (float64_lt_nocheck(rFm, rFn))
				flags |= CC_CARRY;
		}
	} else {
		/* Both operands are in registers.  */
		if (fpa11->fType[Fn] == typeSingle
		    && fpa11->fType[Fm] == typeSingle) {
			float32 rFm = fpa11->fpreg[Fm].fSingle;
			float32 rFn = fpa11->fpreg[Fn].fSingle;

			if (float32_is_nan(rFn)
			    || float32_is_nan(rFm))
				goto unordered;

			if (n_flag)
				rFm ^= 0x80000000;

			/* test for less than condition */
			if (float32_lt_nocheck(rFn, rFm))
				flags |= CC_NEGATIVE;

			/* test for equal condition */
			if (float32_eq_nocheck(rFn, rFm))
				flags |= CC_ZERO;

			/* test for greater than or equal condition */
			if (float32_lt_nocheck(rFm, rFn))
				flags |= CC_CARRY;
		} else {
			/* Promote 32-bit operand to 64 bits.  */
			float64 rFm, rFn;

			rFm = (fpa11->fType[Fm] == typeSingle) ?
			    float32_to_float64(fpa11->fpreg[Fm].fSingle)
			    : fpa11->fpreg[Fm].fDouble;

			rFn = (fpa11->fType[Fn] == typeSingle) ?
			    float32_to_float64(fpa11->fpreg[Fn].fSingle)
			    : fpa11->fpreg[Fn].fDouble;

			if (float64_is_nan(rFn)
			    || float64_is_nan(rFm))
				goto unordered;

			if (n_flag)
				rFm ^= 0x8000000000000000ULL;

			/* test for less than condition */
			if (float64_lt_nocheck(rFn, rFm))
				flags |= CC_NEGATIVE;

			/* test for equal condition */
			if (float64_eq_nocheck(rFn, rFm))
				flags |= CC_ZERO;

			/* test for greater than or equal condition */
			if (float64_lt_nocheck(rFm, rFn))
				flags |= CC_CARRY;
		}
	}

#endif

	writeConditionCodes(flags);

	return 1;

      unordered:
	/* ?? The FPA data sheet is pretty vague about this, in particular
	   about whether the non-E comparisons can ever raise exceptions.
	   This implementation is based on a combination of what it says in
	   the data sheet, observation of how the Acorn emulator actually
	   behaves (and how programs expect it to) and guesswork.  */
	flags |= CC_OVERFLOW;
	flags &= ~(CC_ZERO | CC_NEGATIVE);

	if (BIT_AC & readFPSR())
		flags |= CC_CARRY;

	if (e_flag)
		float_raise(float_flag_invalid);

	writeConditionCodes(flags);
	return 1;
}
