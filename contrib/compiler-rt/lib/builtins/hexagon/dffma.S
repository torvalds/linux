//===----------------------Hexagon builtin routine ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define Q6_ALIAS(TAG) .global __qdsp_##TAG ; .set __qdsp_##TAG, __hexagon_##TAG
#define END(TAG) .size TAG,.-TAG

/* Double Precision Multiply */


#define A r1:0
#define AH r1
#define AL r0
#define B r3:2
#define BH r3
#define BL r2
#define C r5:4
#define CH r5
#define CL r4



#define BTMP r15:14
#define BTMPH r15
#define BTMPL r14

#define ATMP r13:12
#define ATMPH r13
#define ATMPL r12

#define CTMP r11:10
#define CTMPH r11
#define CTMPL r10

#define PP_LL r9:8
#define PP_LL_H r9
#define PP_LL_L r8

#define PP_ODD r7:6
#define PP_ODD_H r7
#define PP_ODD_L r6


#define PP_HH r17:16
#define PP_HH_H r17
#define PP_HH_L r16

#define EXPA r18
#define EXPB r19
#define EXPBA r19:18

#define TMP r28

#define P_TMP p0
#define PROD_NEG p3
#define EXACT p2
#define SWAP p1

#define MANTBITS 52
#define HI_MANTBITS 20
#define EXPBITS 11
#define BIAS 1023
#define STACKSPACE 32

#define ADJUST 4

#define FUDGE 7
#define FUDGE2 3

#ifndef SR_ROUND_OFF
#define SR_ROUND_OFF 22
#endif

	/*
	 * First, classify for normal values, and abort if abnormal
	 *
	 * Next, unpack mantissa into 0x1000_0000_0000_0000 + mant<<8
	 *
	 * Since we know that the 2 MSBs of the H registers is zero, we should never carry
	 * the partial products that involve the H registers
	 *
	 * Try to buy X slots, at the expense of latency if needed
	 *
	 * We will have PP_HH with the upper bits of the product, PP_LL with the lower
	 * PP_HH can have a maximum of 0x03FF_FFFF_FFFF_FFFF or thereabouts
	 * PP_HH can have a minimum of 0x0100_0000_0000_0000
	 *
	 * 0x0100_0000_0000_0000 has EXP of EXPA+EXPB-BIAS
	 *
	 * We need to align CTMP.
	 * If CTMP >> PP, convert PP to 64 bit with sticky, align CTMP, and follow normal add
	 * If CTMP << PP align CTMP and add 128 bits.  Then compute sticky
	 * If CTMP ~= PP, align CTMP and add 128 bits.  May have massive cancellation.
	 *
	 * Convert partial product and CTMP to 2's complement prior to addition
	 *
	 * After we add, we need to normalize into upper 64 bits, then compute sticky.
	 *
	 *
	 */

	.text
	.global __hexagon_fmadf4
        .type __hexagon_fmadf4,@function
	.global __hexagon_fmadf5
        .type __hexagon_fmadf5,@function
	.global fma
	.type fma,@function
	Q6_ALIAS(fmadf5)
	.p2align 5
__hexagon_fmadf4:
__hexagon_fmadf5:
fma:
	{
		P_TMP = dfclass(A,#2)
		P_TMP = dfclass(B,#2)
		ATMP = #0
		BTMP = #0
	}
	{
		ATMP = insert(A,#MANTBITS,#EXPBITS-3)
		BTMP = insert(B,#MANTBITS,#EXPBITS-3)
		PP_ODD_H = ##0x10000000
		allocframe(#STACKSPACE)
	}
	{
		PP_LL = mpyu(ATMPL,BTMPL)
		if (!P_TMP) jump .Lfma_abnormal_ab
		ATMPH = or(ATMPH,PP_ODD_H)
		BTMPH = or(BTMPH,PP_ODD_H)
	}
	{
		P_TMP = dfclass(C,#2)
		if (!P_TMP.new) jump:nt .Lfma_abnormal_c
		CTMP = combine(PP_ODD_H,#0)
		PP_ODD = combine(#0,PP_LL_H)
	}
.Lfma_abnormal_c_restart:
	{
		PP_ODD += mpyu(BTMPL,ATMPH)
		CTMP = insert(C,#MANTBITS,#EXPBITS-3)
		memd(r29+#0) = PP_HH
		memd(r29+#8) = EXPBA
	}
	{
		PP_ODD += mpyu(ATMPL,BTMPH)
		EXPBA = neg(CTMP)
		P_TMP = cmp.gt(CH,#-1)
		TMP = xor(AH,BH)
	}
	{
		EXPA = extractu(AH,#EXPBITS,#HI_MANTBITS)
		EXPB = extractu(BH,#EXPBITS,#HI_MANTBITS)
		PP_HH = combine(#0,PP_ODD_H)
		if (!P_TMP) CTMP = EXPBA
	}
	{
		PP_HH += mpyu(ATMPH,BTMPH)
		PP_LL = combine(PP_ODD_L,PP_LL_L)
#undef PP_ODD
#undef PP_ODD_H
#undef PP_ODD_L
#undef ATMP
#undef ATMPL
#undef ATMPH
#undef BTMP
#undef BTMPL
#undef BTMPH
#define RIGHTLEFTSHIFT r13:12
#define RIGHTSHIFT r13
#define LEFTSHIFT r12

		EXPA = add(EXPA,EXPB)
#undef EXPB
#undef EXPBA
#define EXPC r19
#define EXPCA r19:18
		EXPC = extractu(CH,#EXPBITS,#HI_MANTBITS)
	}
	/* PP_HH:PP_LL now has product */
	/* CTMP is negated */
	/* EXPA,B,C are extracted */
	/*
	 * We need to negate PP
	 * Since we will be adding with carry later, if we need to negate,
	 * just invert all bits now, which we can do conditionally and in parallel
	 */
#define PP_HH_TMP r15:14
#define PP_LL_TMP r7:6
	{
		EXPA = add(EXPA,#-BIAS+(ADJUST))
		PROD_NEG = !cmp.gt(TMP,#-1)
		PP_LL_TMP = #0
		PP_HH_TMP = #0
	}
	{
		PP_LL_TMP = sub(PP_LL_TMP,PP_LL,PROD_NEG):carry
		P_TMP = !cmp.gt(TMP,#-1)
		SWAP = cmp.gt(EXPC,EXPA)	// If C >> PP
		if (SWAP.new) EXPCA = combine(EXPA,EXPC)
	}
	{
		PP_HH_TMP = sub(PP_HH_TMP,PP_HH,PROD_NEG):carry
		if (P_TMP) PP_LL = PP_LL_TMP
#undef PP_LL_TMP
#define CTMP2 r7:6
#define CTMP2H r7
#define CTMP2L r6
		CTMP2 = #0
		EXPC = sub(EXPA,EXPC)
	}
	{
		if (P_TMP) PP_HH = PP_HH_TMP
		P_TMP = cmp.gt(EXPC,#63)
		if (SWAP) PP_LL = CTMP2
		if (SWAP) CTMP2 = PP_LL
	}
#undef PP_HH_TMP
//#define ONE r15:14
//#define S_ONE r14
#define ZERO r15:14
#define S_ZERO r15
#undef PROD_NEG
#define P_CARRY p3
	{
		if (SWAP) PP_HH = CTMP	// Swap C and PP
		if (SWAP) CTMP = PP_HH
		if (P_TMP) EXPC = add(EXPC,#-64)
		TMP = #63
	}
	{
		// If diff > 63, pre-shift-right by 64...
		if (P_TMP) CTMP2 = CTMP
		TMP = asr(CTMPH,#31)
		RIGHTSHIFT = min(EXPC,TMP)
		LEFTSHIFT = #0
	}
#undef C
#undef CH
#undef CL
#define STICKIES r5:4
#define STICKIESH r5
#define STICKIESL r4
	{
		if (P_TMP) CTMP = combine(TMP,TMP)	// sign extension of pre-shift-right-64
		STICKIES = extract(CTMP2,RIGHTLEFTSHIFT)
		CTMP2 = lsr(CTMP2,RIGHTSHIFT)
		LEFTSHIFT = sub(#64,RIGHTSHIFT)
	}
	{
		ZERO = #0
		TMP = #-2
		CTMP2 |= lsl(CTMP,LEFTSHIFT)
		CTMP = asr(CTMP,RIGHTSHIFT)
	}
	{
		P_CARRY = cmp.gtu(STICKIES,ZERO)	// If we have sticky bits from C shift
		if (P_CARRY.new) CTMP2L = and(CTMP2L,TMP) // make sure adding 1 == OR
#undef ZERO
#define ONE r15:14
#define S_ONE r14
		ONE = #1
		STICKIES = #0
	}
	{
		PP_LL = add(CTMP2,PP_LL,P_CARRY):carry	// use the carry to add the sticky
	}
	{
		PP_HH = add(CTMP,PP_HH,P_CARRY):carry
		TMP = #62
	}
	/*
	 * PP_HH:PP_LL now holds the sum
	 * We may need to normalize left, up to ??? bits.
	 *
	 * I think that if we have massive cancellation, the range we normalize by
	 * is still limited
	 */
	{
		LEFTSHIFT = add(clb(PP_HH),#-2)
		if (!cmp.eq(LEFTSHIFT.new,TMP)) jump:t 1f	// all sign bits?
	}
	/* We had all sign bits, shift left by 62. */
	{
		CTMP = extractu(PP_LL,#62,#2)
		PP_LL = asl(PP_LL,#62)
		EXPA = add(EXPA,#-62)			// And adjust exponent of result
	}
	{
		PP_HH = insert(CTMP,#62,#0)		// Then shift 63
	}
	{
		LEFTSHIFT = add(clb(PP_HH),#-2)
	}
	.falign
1:
	{
		CTMP = asl(PP_HH,LEFTSHIFT)
		STICKIES |= asl(PP_LL,LEFTSHIFT)
		RIGHTSHIFT = sub(#64,LEFTSHIFT)
		EXPA = sub(EXPA,LEFTSHIFT)
	}
	{
		CTMP |= lsr(PP_LL,RIGHTSHIFT)
		EXACT = cmp.gtu(ONE,STICKIES)
		TMP = #BIAS+BIAS-2
	}
	{
		if (!EXACT) CTMPL = or(CTMPL,S_ONE)
		// If EXPA is overflow/underflow, jump to ovf_unf
		P_TMP = !cmp.gt(EXPA,TMP)
		P_TMP = cmp.gt(EXPA,#1)
		if (!P_TMP.new) jump:nt .Lfma_ovf_unf
	}
	{
		// XXX: FIXME: should PP_HH for check of zero be CTMP?
		P_TMP = cmp.gtu(ONE,CTMP)		// is result true zero?
		A = convert_d2df(CTMP)
		EXPA = add(EXPA,#-BIAS-60)
		PP_HH = memd(r29+#0)
	}
	{
		AH += asl(EXPA,#HI_MANTBITS)
		EXPCA = memd(r29+#8)
		if (!P_TMP) dealloc_return		// not zero, return
	}
.Ladd_yields_zero:
	/* We had full cancellation.  Return +/- zero (-0 when round-down) */
	{
		TMP = USR
		A = #0
	}
	{
		TMP = extractu(TMP,#2,#SR_ROUND_OFF)
		PP_HH = memd(r29+#0)
		EXPCA = memd(r29+#8)
	}
	{
		p0 = cmp.eq(TMP,#2)
		if (p0.new) AH = ##0x80000000
		dealloc_return
	}

#undef RIGHTLEFTSHIFT
#undef RIGHTSHIFT
#undef LEFTSHIFT
#undef CTMP2
#undef CTMP2H
#undef CTMP2L

.Lfma_ovf_unf:
	{
		p0 = cmp.gtu(ONE,CTMP)
		if (p0.new) jump:nt .Ladd_yields_zero
	}
	{
		A = convert_d2df(CTMP)
		EXPA = add(EXPA,#-BIAS-60)
		TMP = EXPA
	}
#define NEW_EXPB r7
#define NEW_EXPA r6
	{
		AH += asl(EXPA,#HI_MANTBITS)
		NEW_EXPB = extractu(AH,#EXPBITS,#HI_MANTBITS)
	}
	{
		NEW_EXPA = add(EXPA,NEW_EXPB)
		PP_HH = memd(r29+#0)
		EXPCA = memd(r29+#8)
#undef PP_HH
#undef PP_HH_H
#undef PP_HH_L
#undef EXPCA
#undef EXPC
#undef EXPA
#undef PP_LL
#undef PP_LL_H
#undef PP_LL_L
#define EXPA r6
#define EXPB r7
#define EXPBA r7:6
#define ATMP r9:8
#define ATMPH r9
#define ATMPL r8
#undef NEW_EXPB
#undef NEW_EXPA
		ATMP = abs(CTMP)
	}
	{
		p0 = cmp.gt(EXPA,##BIAS+BIAS)
		if (p0.new) jump:nt .Lfma_ovf
	}
	{
		p0 = cmp.gt(EXPA,#0)
		if (p0.new) jump:nt .Lpossible_unf
	}
	{
		// TMP has original EXPA.
		// ATMP is corresponding value
		// Normalize ATMP and shift right to correct location
		EXPB = add(clb(ATMP),#-2)		// Amount to left shift to normalize
		EXPA = sub(#1+5,TMP)			// Amount to right shift to denormalize
		p3 = cmp.gt(CTMPH,#-1)
	}
	/* Underflow */
	/* We know that the infinte range exponent should be EXPA */
	/* CTMP is 2's complement, ATMP is abs(CTMP) */
	{
		EXPA = add(EXPA,EXPB)		// how much to shift back right
		ATMP = asl(ATMP,EXPB)		// shift left
		AH = USR
		TMP = #63
	}
	{
		EXPB = min(EXPA,TMP)
		EXPA = #0
		AL = #0x0030
	}
	{
		B = extractu(ATMP,EXPBA)
		ATMP = asr(ATMP,EXPB)
	}
	{
		p0 = cmp.gtu(ONE,B)
		if (!p0.new) ATMPL = or(ATMPL,S_ONE)
		ATMPH = setbit(ATMPH,#HI_MANTBITS+FUDGE2)
	}
	{
		CTMP = neg(ATMP)
		p1 = bitsclr(ATMPL,#(1<<FUDGE2)-1)
		if (!p1.new) AH = or(AH,AL)
		B = #0
	}
	{
		if (p3) CTMP = ATMP
		USR = AH
		TMP = #-BIAS-(MANTBITS+FUDGE2)
	}
	{
		A = convert_d2df(CTMP)
	}
	{
		AH += asl(TMP,#HI_MANTBITS)
		dealloc_return
	}
.Lpossible_unf:
	{
		TMP = ##0x7fefffff
		ATMP = abs(CTMP)
	}
	{
		p0 = cmp.eq(AL,#0)
		p0 = bitsclr(AH,TMP)
		if (!p0.new) dealloc_return:t
		TMP = #0x7fff
	}
	{
		p0 = bitsset(ATMPH,TMP)
		BH = USR
		BL = #0x0030
	}
	{
		if (p0) BH = or(BH,BL)
	}
	{
		USR = BH
	}
	{
		p0 = dfcmp.eq(A,A)
		dealloc_return
	}
.Lfma_ovf:
	{
		TMP = USR
		CTMP = combine(##0x7fefffff,#-1)
		A = CTMP
	}
	{
		ATMP = combine(##0x7ff00000,#0)
		BH = extractu(TMP,#2,#SR_ROUND_OFF)
		TMP = or(TMP,#0x28)
	}
	{
		USR = TMP
		BH ^= lsr(AH,#31)
		BL = BH
	}
	{
		p0 = !cmp.eq(BL,#1)
		p0 = !cmp.eq(BH,#2)
	}
	{
		p0 = dfcmp.eq(ATMP,ATMP)
		if (p0.new) CTMP = ATMP
	}
	{
		A = insert(CTMP,#63,#0)
		dealloc_return
	}
#undef CTMP
#undef CTMPH
#undef CTMPL
#define BTMP r11:10
#define BTMPH r11
#define BTMPL r10

#undef STICKIES
#undef STICKIESH
#undef STICKIESL
#define C r5:4
#define CH r5
#define CL r4

.Lfma_abnormal_ab:
	{
		ATMP = extractu(A,#63,#0)
		BTMP = extractu(B,#63,#0)
		deallocframe
	}
	{
		p3 = cmp.gtu(ATMP,BTMP)
		if (!p3.new) A = B		// sort values
		if (!p3.new) B = A
	}
	{
		p0 = dfclass(A,#0x0f)		// A NaN?
		if (!p0.new) jump:nt .Lnan
		if (!p3) ATMP = BTMP
		if (!p3) BTMP = ATMP
	}
	{
		p1 = dfclass(A,#0x08)		// A is infinity
		p1 = dfclass(B,#0x0e)		// B is nonzero
	}
	{
		p0 = dfclass(A,#0x08)		// a is inf
		p0 = dfclass(B,#0x01)		// b is zero
	}
	{
		if (p1) jump .Lab_inf
		p2 = dfclass(B,#0x01)
	}
	{
		if (p0) jump .Linvalid
		if (p2) jump .Lab_true_zero
		TMP = ##0x7c000000
	}
	// We are left with a normal or subnormal times a subnormal, A > B
	// If A and B are both very small, we will go to a single sticky bit; replace
	// A and B lower 63 bits with 0x0010_0000_0000_0000, which yields equivalent results
	// if A and B might multiply to something bigger, decrease A exp and increase B exp
	// and start over
	{
		p0 = bitsclr(AH,TMP)
		if (p0.new) jump:nt .Lfma_ab_tiny
	}
	{
		TMP = add(clb(BTMP),#-EXPBITS)
	}
	{
		BTMP = asl(BTMP,TMP)
	}
	{
		B = insert(BTMP,#63,#0)
		AH -= asl(TMP,#HI_MANTBITS)
	}
	jump fma

.Lfma_ab_tiny:
	ATMP = combine(##0x00100000,#0)
	{
		A = insert(ATMP,#63,#0)
		B = insert(ATMP,#63,#0)
	}
	jump fma

.Lab_inf:
	{
		B = lsr(B,#63)
		p0 = dfclass(C,#0x10)
	}
	{
		A ^= asl(B,#63)
		if (p0) jump .Lnan
	}
	{
		p1 = dfclass(C,#0x08)
		if (p1.new) jump:nt .Lfma_inf_plus_inf
	}
	/* A*B is +/- inf, C is finite.  Return A */
	{
		jumpr r31
	}
	.falign
.Lfma_inf_plus_inf:
	{	// adding infinities of different signs is invalid
		p0 = dfcmp.eq(A,C)
		if (!p0.new) jump:nt .Linvalid
	}
	{
		jumpr r31
	}

.Lnan:
	{
		p0 = dfclass(B,#0x10)
		p1 = dfclass(C,#0x10)
		if (!p0.new) B = A
		if (!p1.new) C = A
	}
	{	// find sNaNs
		BH = convert_df2sf(B)
		BL = convert_df2sf(C)
	}
	{
		BH = convert_df2sf(A)
		A = #-1
		jumpr r31
	}

.Linvalid:
	{
		TMP = ##0x7f800001		// sp snan
	}
	{
		A = convert_sf2df(TMP)
		jumpr r31
	}

.Lab_true_zero:
	// B is zero, A is finite number
	{
		p0 = dfclass(C,#0x10)
		if (p0.new) jump:nt .Lnan
		if (p0.new) A = C
	}
	{
		p0 = dfcmp.eq(B,C)		// is C also zero?
		AH = lsr(AH,#31)		// get sign
	}
	{
		BH ^= asl(AH,#31)		// form correctly signed zero in B
		if (!p0) A = C			// If C is not zero, return C
		if (!p0) jumpr r31
	}
	/* B has correctly signed zero, C is also zero */
.Lzero_plus_zero:
	{
		p0 = cmp.eq(B,C)		// yes, scalar equals.  +0++0 or -0+-0
		if (p0.new) jumpr:t r31
		A = B
	}
	{
		TMP = USR
	}
	{
		TMP = extractu(TMP,#2,#SR_ROUND_OFF)
		A = #0
	}
	{
		p0 = cmp.eq(TMP,#2)
		if (p0.new) AH = ##0x80000000
		jumpr r31
	}
#undef BTMP
#undef BTMPH
#undef BTMPL
#define CTMP r11:10
	.falign
.Lfma_abnormal_c:
	/* We know that AB is normal * normal */
	/* C is not normal: zero, subnormal, inf, or NaN. */
	{
		p0 = dfclass(C,#0x10)		// is C NaN?
		if (p0.new) jump:nt .Lnan
		if (p0.new) A = C		// move NaN to A
		deallocframe
	}
	{
		p0 = dfclass(C,#0x08)		// is C inf?
		if (p0.new) A = C		// return C
		if (p0.new) jumpr:nt r31
	}
	// zero or subnormal
	// If we have a zero, and we know AB is normal*normal, we can just call normal multiply
	{
		p0 = dfclass(C,#0x01)		// is C zero?
		if (p0.new) jump:nt __hexagon_muldf3
		TMP = #1
	}
	// Left with: subnormal
	// Adjust C and jump back to restart
	{
		allocframe(#STACKSPACE)		// oops, deallocated above, re-allocate frame
		CTMP = #0
		CH = insert(TMP,#EXPBITS,#HI_MANTBITS)
		jump .Lfma_abnormal_c_restart
	}
END(fma)
