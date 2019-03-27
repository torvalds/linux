//===----------------------Hexagon builtin routine ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

/* Double Precision Multiply */
#define A r1:0
#define AH r1
#define AL r0
#define B r3:2
#define BH r3
#define BL r2

#define BTMP r5:4
#define BTMPH r5
#define BTMPL r4

#define PP_ODD r7:6
#define PP_ODD_H r7
#define PP_ODD_L r6

#define ONE r9:8
#define S_ONE r8
#define S_ZERO r9

#define PP_HH r11:10
#define PP_HH_H r11
#define PP_HH_L r10

#define ATMP r13:12
#define ATMPH r13
#define ATMPL r12

#define PP_LL r15:14
#define PP_LL_H r15
#define PP_LL_L r14

#define TMP r28

#define MANTBITS 52
#define HI_MANTBITS 20
#define EXPBITS 11
#define BIAS 1024
#define MANTISSA_TO_INT_BIAS 52

/* Some constant to adjust normalization amount in error code */
/* Amount to right shift the partial product to get to a denorm */
#define FUDGE 5

#define Q6_ALIAS(TAG) .global __qdsp_##TAG ; .set __qdsp_##TAG, __hexagon_##TAG
#define FAST_ALIAS(TAG) .global __hexagon_fast_##TAG ; .set __hexagon_fast_##TAG, __hexagon_##TAG
#define FAST2_ALIAS(TAG) .global __hexagon_fast2_##TAG ; .set __hexagon_fast2_##TAG, __hexagon_##TAG
#define END(TAG) .size TAG,.-TAG

#define SR_ROUND_OFF 22
	.text
	.global __hexagon_muldf3
	.type __hexagon_muldf3,@function
	Q6_ALIAS(muldf3)
  FAST_ALIAS(muldf3)
  FAST2_ALIAS(muldf3)
	.p2align 5
__hexagon_muldf3:
	{
		p0 = dfclass(A,#2)
		p0 = dfclass(B,#2)
		ATMP = combine(##0x40000000,#0)
	}
	{
		ATMP = insert(A,#MANTBITS,#EXPBITS-1)
		BTMP = asl(B,#EXPBITS-1)
		TMP = #-BIAS
		ONE = #1
	}
	{
		PP_ODD = mpyu(BTMPL,ATMPH)
		BTMP = insert(ONE,#2,#62)
	}
	/* since we know that the MSB of the H registers is zero, we should never carry */
	/* H <= 2^31-1.  L <= 2^32-1.  Therefore, HL <= 2^63-2^32-2^31+1 */
	/* Adding 2 HLs, we get 2^64-3*2^32+2 maximum.  */
	/* Therefore, we can add 3 2^32-1 values safely without carry.  We only need one. */
	{
		PP_LL = mpyu(ATMPL,BTMPL)
		PP_ODD += mpyu(ATMPL,BTMPH)
	}
	{
		PP_ODD += lsr(PP_LL,#32)
		PP_HH = mpyu(ATMPH,BTMPH)
		BTMP = combine(##BIAS+BIAS-4,#0)
	}
	{
		PP_HH += lsr(PP_ODD,#32)
		if (!p0) jump .Lmul_abnormal
		p1 = cmp.eq(PP_LL_L,#0)		// 64 lsb's 0?
		p1 = cmp.eq(PP_ODD_L,#0)	// 64 lsb's 0?
	}
	/*
	 * PP_HH can have a maximum of 0x3FFF_FFFF_FFFF_FFFF or thereabouts
	 * PP_HH can have a minimum of 0x1000_0000_0000_0000 or so
	 */
#undef PP_ODD
#undef PP_ODD_H
#undef PP_ODD_L
#define EXP10 r7:6
#define EXP1 r7
#define EXP0 r6
	{
		if (!p1) PP_HH_L = or(PP_HH_L,S_ONE)
		EXP0 = extractu(AH,#EXPBITS,#HI_MANTBITS)
		EXP1 = extractu(BH,#EXPBITS,#HI_MANTBITS)
	}
	{
		PP_LL = neg(PP_HH)
		EXP0 += add(TMP,EXP1)
		TMP = xor(AH,BH)
	}
	{
		if (!p2.new) PP_HH = PP_LL
		p2 = cmp.gt(TMP,#-1)
		p0 = !cmp.gt(EXP0,BTMPH)
		p0 = cmp.gt(EXP0,BTMPL)
		if (!p0.new) jump:nt .Lmul_ovf_unf
	}
	{
		A = convert_d2df(PP_HH)
		EXP0 = add(EXP0,#-BIAS-58)
	}
	{
		AH += asl(EXP0,#HI_MANTBITS)
		jumpr r31
	}

	.falign
.Lpossible_unf:
	/* We end up with a positive exponent */
	/* But we may have rounded up to an exponent of 1. */
	/* If the exponent is 1, if we rounded up to it
	 * we need to also raise underflow
	 * Fortunately, this is pretty easy to detect, we must have +/- 0x0010_0000_0000_0000
	 * And the PP should also have more than one bit set
	 */
	/* Note: ATMP should have abs(PP_HH) */
	/* Note: BTMPL should have 0x7FEFFFFF */
	{
		p0 = cmp.eq(AL,#0)
		p0 = bitsclr(AH,BTMPL)
		if (!p0.new) jumpr:t r31
		BTMPH = #0x7fff
	}
	{
		p0 = bitsset(ATMPH,BTMPH)
		BTMPL = USR
		BTMPH = #0x030
	}
	{
		if (p0) BTMPL = or(BTMPL,BTMPH)
	}
	{
		USR = BTMPL
	}
	{
		p0 = dfcmp.eq(A,A)
		jumpr r31
	}
	.falign
.Lmul_ovf_unf:
	{
		A = convert_d2df(PP_HH)
		ATMP = abs(PP_HH)			// take absolute value
		EXP1 = add(EXP0,#-BIAS-58)
	}
	{
		AH += asl(EXP1,#HI_MANTBITS)
		EXP1 = extractu(AH,#EXPBITS,#HI_MANTBITS)
		BTMPL = ##0x7FEFFFFF
	}
	{
		EXP1 += add(EXP0,##-BIAS-58)
		//BTMPH = add(clb(ATMP),#-2)
		BTMPH = #0
	}
	{
		p0 = cmp.gt(EXP1,##BIAS+BIAS-2)	// overflow
		if (p0.new) jump:nt .Lmul_ovf
	}
	{
		p0 = cmp.gt(EXP1,#0)
		if (p0.new) jump:nt .Lpossible_unf
		BTMPH = sub(EXP0,BTMPH)
		TMP = #63				// max amount to shift
	}
	/* Underflow */
	/*
	 * PP_HH has the partial product with sticky LSB.
	 * PP_HH can have a maximum of 0x3FFF_FFFF_FFFF_FFFF or thereabouts
	 * PP_HH can have a minimum of 0x1000_0000_0000_0000 or so
	 * The exponent of PP_HH is in  EXP1, which is non-positive (0 or negative)
	 * That's the exponent that happens after the normalization
	 *
	 * EXP0 has the exponent that, when added to the normalized value, is out of range.
	 *
	 * Strategy:
	 *
	 * * Shift down bits, with sticky bit, such that the bits are aligned according
	 *   to the LZ count and appropriate exponent, but not all the way to mantissa
	 *   field, keep around the last few bits.
	 * * Put a 1 near the MSB
	 * * Check the LSBs for inexact; if inexact also set underflow
	 * * Convert [u]d2df -- will correctly round according to rounding mode
	 * * Replace exponent field with zero
	 *
	 *
	 */


	{
		BTMPL = #0	 			// offset for extract
		BTMPH = sub(#FUDGE,BTMPH)		// amount to right shift
	}
	{
		p3 = cmp.gt(PP_HH_H,#-1)		// is it positive?
		BTMPH = min(BTMPH,TMP)			// Don't shift more than 63
		PP_HH = ATMP
	}
	{
		TMP = USR
		PP_LL = extractu(PP_HH,BTMP)
	}
	{
		PP_HH = asr(PP_HH,BTMPH)
		BTMPL = #0x0030					// underflow flag
		AH = insert(S_ZERO,#EXPBITS,#HI_MANTBITS)
	}
	{
		p0 = cmp.gtu(ONE,PP_LL)				// Did we extract all zeros?
		if (!p0.new) PP_HH_L = or(PP_HH_L,S_ONE)	// add sticky bit
		PP_HH_H = setbit(PP_HH_H,#HI_MANTBITS+3)	// Add back in a bit so we can use convert instruction
	}
	{
		PP_LL = neg(PP_HH)
		p1 = bitsclr(PP_HH_L,#0x7)		// Are the LSB's clear?
		if (!p1.new) TMP = or(BTMPL,TMP)	// If not, Inexact+Underflow
	}
	{
		if (!p3) PP_HH = PP_LL
		USR = TMP
	}
	{
		A = convert_d2df(PP_HH)			// Do rounding
		p0 = dfcmp.eq(A,A)			// realize exception
	}
	{
		AH = insert(S_ZERO,#EXPBITS-1,#HI_MANTBITS+1)		// Insert correct exponent
		jumpr r31
	}
	.falign
.Lmul_ovf:
	// We get either max finite value or infinity.  Either way, overflow+inexact
	{
		TMP = USR
		ATMP = combine(##0x7fefffff,#-1)	// positive max finite
		A = PP_HH
	}
	{
		PP_LL_L = extractu(TMP,#2,#SR_ROUND_OFF)	// rounding bits
		TMP = or(TMP,#0x28)			// inexact + overflow
		BTMP = combine(##0x7ff00000,#0)		// positive infinity
	}
	{
		USR = TMP
		PP_LL_L ^= lsr(AH,#31)			// Does sign match rounding?
		TMP = PP_LL_L				// unmodified rounding mode
	}
	{
		p0 = !cmp.eq(TMP,#1)			// If not round-to-zero and
		p0 = !cmp.eq(PP_LL_L,#2)		// Not rounding the other way,
		if (p0.new) ATMP = BTMP			// we should get infinity
		p0 = dfcmp.eq(A,A)			// Realize FP exception if enabled
	}
	{
		A = insert(ATMP,#63,#0)			// insert inf/maxfinite, leave sign
		jumpr r31
	}

.Lmul_abnormal:
	{
		ATMP = extractu(A,#63,#0)		// strip off sign
		BTMP = extractu(B,#63,#0)		// strip off sign
	}
	{
		p3 = cmp.gtu(ATMP,BTMP)
		if (!p3.new) A = B			// sort values
		if (!p3.new) B = A			// sort values
	}
	{
		// Any NaN --> NaN, possibly raise invalid if sNaN
		p0 = dfclass(A,#0x0f)		// A not NaN?
		if (!p0.new) jump:nt .Linvalid_nan
		if (!p3) ATMP = BTMP
		if (!p3) BTMP = ATMP
	}
	{
		// Infinity * nonzero number is infinity
		p1 = dfclass(A,#0x08)		// A is infinity
		p1 = dfclass(B,#0x0e)		// B is nonzero
	}
	{
		// Infinity * zero --> NaN, raise invalid
		// Other zeros return zero
		p0 = dfclass(A,#0x08)		// A is infinity
		p0 = dfclass(B,#0x01)		// B is zero
	}
	{
		if (p1) jump .Ltrue_inf
		p2 = dfclass(B,#0x01)
	}
	{
		if (p0) jump .Linvalid_zeroinf
		if (p2) jump .Ltrue_zero		// so return zero
		TMP = ##0x7c000000
	}
	// We are left with a normal or subnormal times a subnormal. A > B
	// If A and B are both very small (exp(a) < BIAS-MANTBITS),
	// we go to a single sticky bit, which we can round easily.
	// If A and B might multiply to something bigger, decrease A exponent and increase
	// B exponent and try again
	{
		p0 = bitsclr(AH,TMP)
		if (p0.new) jump:nt .Lmul_tiny
	}
	{
		TMP = cl0(BTMP)
	}
	{
		TMP = add(TMP,#-EXPBITS)
	}
	{
		BTMP = asl(BTMP,TMP)
	}
	{
		B = insert(BTMP,#63,#0)
		AH -= asl(TMP,#HI_MANTBITS)
	}
	jump __hexagon_muldf3
.Lmul_tiny:
	{
		TMP = USR
		A = xor(A,B)				// get sign bit
	}
	{
		TMP = or(TMP,#0x30)			// Inexact + Underflow
		A = insert(ONE,#63,#0)			// put in rounded up value
		BTMPH = extractu(TMP,#2,#SR_ROUND_OFF)	// get rounding mode
	}
	{
		USR = TMP
		p0 = cmp.gt(BTMPH,#1)			// Round towards pos/neg inf?
		if (!p0.new) AL = #0			// If not, zero
		BTMPH ^= lsr(AH,#31)			// rounding my way --> set LSB
	}
	{
		p0 = cmp.eq(BTMPH,#3)			// if rounding towards right inf
		if (!p0.new) AL = #0			// don't go to zero
		jumpr r31
	}
.Linvalid_zeroinf:
	{
		TMP = USR
	}
	{
		A = #-1
		TMP = or(TMP,#2)
	}
	{
		USR = TMP
	}
	{
		p0 = dfcmp.uo(A,A)			// force exception if enabled
		jumpr r31
	}
.Linvalid_nan:
	{
		p0 = dfclass(B,#0x0f)			// if B is not NaN
		TMP = convert_df2sf(A)			// will generate invalid if sNaN
		if (p0.new) B = A 			// make it whatever A is
	}
	{
		BL = convert_df2sf(B)			// will generate invalid if sNaN
		A = #-1
		jumpr r31
	}
	.falign
.Ltrue_zero:
	{
		A = B
		B = A
	}
.Ltrue_inf:
	{
		BH = extract(BH,#1,#31)
	}
	{
		AH ^= asl(BH,#31)
		jumpr r31
	}
END(__hexagon_muldf3)

#undef ATMP
#undef ATMPL
#undef ATMPH
#undef BTMP
#undef BTMPL
#undef BTMPH
