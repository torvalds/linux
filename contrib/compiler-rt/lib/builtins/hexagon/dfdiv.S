//===----------------------Hexagon builtin routine ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

/* Double Precision Divide */

#define A r1:0
#define AH r1
#define AL r0

#define B r3:2
#define BH r3
#define BL r2

#define Q r5:4
#define QH r5
#define QL r4

#define PROD r7:6
#define PRODHI r7
#define PRODLO r6

#define SFONE r8
#define SFDEN r9
#define SFERROR r10
#define SFRECIP r11

#define EXPBA r13:12
#define EXPB r13
#define EXPA r12

#define REMSUB2 r15:14



#define SIGN r28

#define Q_POSITIVE p3
#define NORMAL p2
#define NO_OVF_UNF p1
#define P_TMP p0

#define RECIPEST_SHIFT 3
#define QADJ 61

#define DFCLASS_NORMAL 0x02
#define DFCLASS_NUMBER 0x0F
#define DFCLASS_INFINITE 0x08
#define DFCLASS_ZERO 0x01
#define DFCLASS_NONZERO (DFCLASS_NUMBER ^ DFCLASS_ZERO)
#define DFCLASS_NONINFINITE (DFCLASS_NUMBER ^ DFCLASS_INFINITE)

#define DF_MANTBITS 52
#define DF_EXPBITS 11
#define SF_MANTBITS 23
#define SF_EXPBITS 8
#define DF_BIAS 0x3ff

#define SR_ROUND_OFF 22

#define Q6_ALIAS(TAG) .global __qdsp_##TAG ; .set __qdsp_##TAG, __hexagon_##TAG
#define FAST_ALIAS(TAG) .global __hexagon_fast_##TAG ; .set __hexagon_fast_##TAG, __hexagon_##TAG
#define FAST2_ALIAS(TAG) .global __hexagon_fast2_##TAG ; .set __hexagon_fast2_##TAG, __hexagon_##TAG
#define END(TAG) .size TAG,.-TAG

	.text
	.global __hexagon_divdf3
	.type __hexagon_divdf3,@function
	Q6_ALIAS(divdf3)
        FAST_ALIAS(divdf3)
        FAST2_ALIAS(divdf3)
	.p2align 5
__hexagon_divdf3:
	{
		NORMAL = dfclass(A,#DFCLASS_NORMAL)
		NORMAL = dfclass(B,#DFCLASS_NORMAL)
		EXPBA = combine(BH,AH)
		SIGN = xor(AH,BH)
	}
#undef A
#undef AH
#undef AL
#undef B
#undef BH
#undef BL
#define REM r1:0
#define REMHI r1
#define REMLO r0
#define DENOM r3:2
#define DENOMHI r3
#define DENOMLO r2
	{
		if (!NORMAL) jump .Ldiv_abnormal
		PROD = extractu(DENOM,#SF_MANTBITS,#DF_MANTBITS-SF_MANTBITS)
		SFONE = ##0x3f800001
	}
	{
		SFDEN = or(SFONE,PRODLO)
		EXPB = extractu(EXPB,#DF_EXPBITS,#DF_MANTBITS-32)
		EXPA = extractu(EXPA,#DF_EXPBITS,#DF_MANTBITS-32)
		Q_POSITIVE = cmp.gt(SIGN,#-1)
	}
#undef SIGN
#define ONE r28
.Ldenorm_continue:
	{
		SFRECIP,P_TMP = sfrecipa(SFONE,SFDEN)
		SFERROR = and(SFONE,#-2)
		ONE = #1
		EXPA = sub(EXPA,EXPB)
	}
#undef EXPB
#define RECIPEST r13
	{
		SFERROR -= sfmpy(SFRECIP,SFDEN):lib
		REMHI = insert(ONE,#DF_EXPBITS+1,#DF_MANTBITS-32)
		RECIPEST = ##0x00800000 << RECIPEST_SHIFT
	}
	{
		SFRECIP += sfmpy(SFRECIP,SFERROR):lib
		DENOMHI = insert(ONE,#DF_EXPBITS+1,#DF_MANTBITS-32)
		SFERROR = and(SFONE,#-2)
	}
	{
		SFERROR -= sfmpy(SFRECIP,SFDEN):lib
		QH = #-DF_BIAS+1
		QL = #DF_BIAS-1
	}
	{
		SFRECIP += sfmpy(SFRECIP,SFERROR):lib
		NO_OVF_UNF = cmp.gt(EXPA,QH)
		NO_OVF_UNF = !cmp.gt(EXPA,QL)
	}
	{
		RECIPEST = insert(SFRECIP,#SF_MANTBITS,#RECIPEST_SHIFT)
		Q = #0
		EXPA = add(EXPA,#-QADJ)
	}
#undef SFERROR
#undef SFRECIP
#define TMP r10
#define TMP1 r11
	{
		RECIPEST = add(RECIPEST,#((-3) << RECIPEST_SHIFT))
	}

#define DIV_ITER1B(QSHIFTINSN,QSHIFT,REMSHIFT,EXTRA) \
	{ \
		PROD = mpyu(RECIPEST,REMHI); \
		REM = asl(REM,# ## ( REMSHIFT )); \
	}; \
	{ \
		PRODLO = # ## 0; \
		REM -= mpyu(PRODHI,DENOMLO); \
		REMSUB2 = mpyu(PRODHI,DENOMHI); \
	}; \
	{ \
		Q += QSHIFTINSN(PROD, # ## ( QSHIFT )); \
		REM -= asl(REMSUB2, # ## 32); \
		EXTRA \
	}


	DIV_ITER1B(ASL,14,15,)
	DIV_ITER1B(ASR,1,15,)
	DIV_ITER1B(ASR,16,15,)
	DIV_ITER1B(ASR,31,15,PROD=# ( 0 );)

#undef REMSUB2
#define TMPPAIR r15:14
#define TMPPAIRHI r15
#define TMPPAIRLO r14
#undef RECIPEST
#define EXPB r13
	{
		// compare or sub with carry
		TMPPAIR = sub(REM,DENOM)
		P_TMP = cmp.gtu(DENOM,REM)
		// set up amt to add to q
		if (!P_TMP.new) PRODLO  = #2
	}
	{
		Q = add(Q,PROD)
		if (!P_TMP) REM = TMPPAIR
		TMPPAIR = #0
	}
	{
		P_TMP = cmp.eq(REM,TMPPAIR)
		if (!P_TMP.new) QL = or(QL,ONE)
	}
	{
		PROD = neg(Q)
	}
	{
		if (!Q_POSITIVE) Q = PROD
	}
#undef REM
#undef REMHI
#undef REMLO
#undef DENOM
#undef DENOMLO
#undef DENOMHI
#define A r1:0
#define AH r1
#define AL r0
#define B r3:2
#define BH r3
#define BL r2
	{
		A = convert_d2df(Q)
		if (!NO_OVF_UNF) jump .Ldiv_ovf_unf
	}
	{
		AH += asl(EXPA,#DF_MANTBITS-32)
		jumpr r31
	}

.Ldiv_ovf_unf:
	{
		AH += asl(EXPA,#DF_MANTBITS-32)
		EXPB = extractu(AH,#DF_EXPBITS,#DF_MANTBITS-32)
	}
	{
		PROD = abs(Q)
		EXPA = add(EXPA,EXPB)
	}
	{
		P_TMP = cmp.gt(EXPA,##DF_BIAS+DF_BIAS)		// overflow
		if (P_TMP.new) jump:nt .Ldiv_ovf
	}
	{
		P_TMP = cmp.gt(EXPA,#0)
		if (P_TMP.new) jump:nt .Lpossible_unf		// round up to normal possible...
	}
	/* Underflow */
	/* We know what the infinite range exponent should be (EXPA) */
	/* Q is 2's complement, PROD is abs(Q) */
	/* Normalize Q, shift right, add a high bit, convert, change exponent */

#define FUDGE1 7	// how much to shift right
#define FUDGE2 4	// how many guard/round to keep at lsbs

	{
		EXPB = add(clb(PROD),#-1)			// doesn't need to be added in since
		EXPA = sub(#FUDGE1,EXPA)			// we extract post-converted exponent
		TMP = USR
		TMP1 = #63
	}
	{
		EXPB = min(EXPA,TMP1)
		TMP1 = or(TMP,#0x030)
		PROD = asl(PROD,EXPB)
		EXPA = #0
	}
	{
		TMPPAIR = extractu(PROD,EXPBA)				// bits that will get shifted out
		PROD = lsr(PROD,EXPB)					// shift out bits
		B = #1
	}
	{
		P_TMP = cmp.gtu(B,TMPPAIR)
		if (!P_TMP.new) PRODLO = or(BL,PRODLO)
		PRODHI = setbit(PRODHI,#DF_MANTBITS-32+FUDGE2)
	}
	{
		Q = neg(PROD)
		P_TMP = bitsclr(PRODLO,#(1<<FUDGE2)-1)
		if (!P_TMP.new) TMP = TMP1
	}
	{
		USR = TMP
		if (Q_POSITIVE) Q = PROD
		TMP = #-DF_BIAS-(DF_MANTBITS+FUDGE2)
	}
	{
		A = convert_d2df(Q)
	}
	{
		AH += asl(TMP,#DF_MANTBITS-32)
		jumpr r31
	}


.Lpossible_unf:
	/* If upper parts of Q were all F's, but abs(A) == 0x00100000_00000000, we rounded up to min_normal */
	/* The answer is correct, but we need to raise Underflow */
	{
		B = extractu(A,#63,#0)
		TMPPAIR = combine(##0x00100000,#0)		// min normal
		TMP = #0x7FFF
	}
	{
		P_TMP = dfcmp.eq(TMPPAIR,B)		// Is everything zero in the rounded value...
		P_TMP = bitsset(PRODHI,TMP)		// but a bunch of bits set in the unrounded abs(quotient)?
	}

#if (__HEXAGON_ARCH__ == 60)
		TMP = USR		// If not, just return
		if (!P_TMP) jumpr r31   // Else, we want to set Unf+Inexact
					// Note that inexact is already set...
#else
	{
		if (!P_TMP) jumpr r31			// If not, just return
		TMP = USR				// Else, we want to set Unf+Inexact
	}						// Note that inexact is already set...
#endif
	{
		TMP = or(TMP,#0x30)
	}
	{
		USR = TMP
	}
	{
		p0 = dfcmp.eq(A,A)
		jumpr r31
	}

.Ldiv_ovf:
	/*
	 * Raise Overflow, and choose the correct overflow value (saturated normal or infinity)
	 */
	{
		TMP = USR
		B = combine(##0x7fefffff,#-1)
		AH = mux(Q_POSITIVE,#0,#-1)
	}
	{
		PROD = combine(##0x7ff00000,#0)
		QH = extractu(TMP,#2,#SR_ROUND_OFF)
		TMP = or(TMP,#0x28)
	}
	{
		USR = TMP
		QH ^= lsr(AH,#31)
		QL = QH
	}
	{
		p0 = !cmp.eq(QL,#1)		// if not round-to-zero
		p0 = !cmp.eq(QH,#2)		// and not rounding the other way
		if (p0.new) B = PROD		// go to inf
		p0 = dfcmp.eq(B,B)		// get exceptions
	}
	{
		A = insert(B,#63,#0)
		jumpr r31
	}

#undef ONE
#define SIGN r28
#undef NORMAL
#undef NO_OVF_UNF
#define P_INF p1
#define P_ZERO p2
.Ldiv_abnormal:
	{
		P_TMP = dfclass(A,#DFCLASS_NUMBER)
		P_TMP = dfclass(B,#DFCLASS_NUMBER)
		Q_POSITIVE = cmp.gt(SIGN,#-1)
	}
	{
		P_INF = dfclass(A,#DFCLASS_INFINITE)
		P_INF = dfclass(B,#DFCLASS_INFINITE)
	}
	{
		P_ZERO = dfclass(A,#DFCLASS_ZERO)
		P_ZERO = dfclass(B,#DFCLASS_ZERO)
	}
	{
		if (!P_TMP) jump .Ldiv_nan
		if (P_INF) jump .Ldiv_invalid
	}
	{
		if (P_ZERO) jump .Ldiv_invalid
	}
	{
		P_ZERO = dfclass(A,#DFCLASS_NONZERO)		// nonzero
		P_ZERO = dfclass(B,#DFCLASS_NONINFINITE)	// non-infinite
	}
	{
		P_INF = dfclass(A,#DFCLASS_NONINFINITE)	// non-infinite
		P_INF = dfclass(B,#DFCLASS_NONZERO)	// nonzero
	}
	{
		if (!P_ZERO) jump .Ldiv_zero_result
		if (!P_INF) jump .Ldiv_inf_result
	}
	/* Now we've narrowed it down to (de)normal / (de)normal */
	/* Set up A/EXPA B/EXPB and go back */
#undef P_ZERO
#undef P_INF
#define P_TMP2 p1
	{
		P_TMP = dfclass(A,#DFCLASS_NORMAL)
		P_TMP2 = dfclass(B,#DFCLASS_NORMAL)
		TMP = ##0x00100000
	}
	{
		EXPBA = combine(BH,AH)
		AH = insert(TMP,#DF_EXPBITS+1,#DF_MANTBITS-32)		// clear out hidden bit, sign bit
		BH = insert(TMP,#DF_EXPBITS+1,#DF_MANTBITS-32)		// clear out hidden bit, sign bit
	}
	{
		if (P_TMP) AH = or(AH,TMP)				// if normal, add back in hidden bit
		if (P_TMP2) BH = or(BH,TMP)				// if normal, add back in hidden bit
	}
	{
		QH = add(clb(A),#-DF_EXPBITS)
		QL = add(clb(B),#-DF_EXPBITS)
		TMP = #1
	}
	{
		EXPA = extractu(EXPA,#DF_EXPBITS,#DF_MANTBITS-32)
		EXPB = extractu(EXPB,#DF_EXPBITS,#DF_MANTBITS-32)
	}
	{
		A = asl(A,QH)
		B = asl(B,QL)
		if (!P_TMP) EXPA = sub(TMP,QH)
		if (!P_TMP2) EXPB = sub(TMP,QL)
	}	// recreate values needed by resume coke
	{
		PROD = extractu(B,#SF_MANTBITS,#DF_MANTBITS-SF_MANTBITS)
	}
	{
		SFDEN = or(SFONE,PRODLO)
		jump .Ldenorm_continue
	}

.Ldiv_zero_result:
	{
		AH = xor(AH,BH)
		B = #0
	}
	{
		A = insert(B,#63,#0)
		jumpr r31
	}
.Ldiv_inf_result:
	{
		p2 = dfclass(B,#DFCLASS_ZERO)
		p2 = dfclass(A,#DFCLASS_NONINFINITE)
	}
	{
		TMP = USR
		if (!p2) jump 1f
		AH = xor(AH,BH)
	}
	{
		TMP = or(TMP,#0x04)		// DBZ
	}
	{
		USR = TMP
	}
1:
	{
		B = combine(##0x7ff00000,#0)
		p0 = dfcmp.uo(B,B)		// take possible exception
	}
	{
		A = insert(B,#63,#0)
		jumpr r31
	}
.Ldiv_nan:
	{
		p0 = dfclass(A,#0x10)
		p1 = dfclass(B,#0x10)
		if (!p0.new) A = B
		if (!p1.new) B = A
	}
	{
		QH = convert_df2sf(A)	// get possible invalid exceptions
		QL = convert_df2sf(B)
	}
	{
		A = #-1
		jumpr r31
	}

.Ldiv_invalid:
	{
		TMP = ##0x7f800001
	}
	{
		A = convert_sf2df(TMP)		// get invalid, get DF qNaN
		jumpr r31
	}
END(__hexagon_divdf3)
