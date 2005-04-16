|
|	bindec.sa 3.4 1/3/91
|
|	bindec
|
|	Description:
|		Converts an input in extended precision format
|		to bcd format.
|
|	Input:
|		a0 points to the input extended precision value
|		value in memory; d0 contains the k-factor sign-extended
|		to 32-bits.  The input may be either normalized,
|		unnormalized, or denormalized.
|
|	Output:	result in the FP_SCR1 space on the stack.
|
|	Saves and Modifies: D2-D7,A2,FP2
|
|	Algorithm:
|
|	A1.	Set RM and size ext;  Set SIGMA = sign of input.
|		The k-factor is saved for use in d7. Clear the
|		BINDEC_FLG for separating normalized/denormalized
|		input.  If input is unnormalized or denormalized,
|		normalize it.
|
|	A2.	Set X = abs(input).
|
|	A3.	Compute ILOG.
|		ILOG is the log base 10 of the input value.  It is
|		approximated by adding e + 0.f when the original
|		value is viewed as 2^^e * 1.f in extended precision.
|		This value is stored in d6.
|
|	A4.	Clr INEX bit.
|		The operation in A3 above may have set INEX2.
|
|	A5.	Set ICTR = 0;
|		ICTR is a flag used in A13.  It must be set before the
|		loop entry A6.
|
|	A6.	Calculate LEN.
|		LEN is the number of digits to be displayed.  The
|		k-factor can dictate either the total number of digits,
|		if it is a positive number, or the number of digits
|		after the decimal point which are to be included as
|		significant.  See the 68882 manual for examples.
|		If LEN is computed to be greater than 17, set OPERR in
|		USER_FPSR.  LEN is stored in d4.
|
|	A7.	Calculate SCALE.
|		SCALE is equal to 10^ISCALE, where ISCALE is the number
|		of decimal places needed to insure LEN integer digits
|		in the output before conversion to bcd. LAMBDA is the
|		sign of ISCALE, used in A9. Fp1 contains
|		10^^(abs(ISCALE)) using a rounding mode which is a
|		function of the original rounding mode and the signs
|		of ISCALE and X.  A table is given in the code.
|
|	A8.	Clr INEX; Force RZ.
|		The operation in A3 above may have set INEX2.
|		RZ mode is forced for the scaling operation to insure
|		only one rounding error.  The grs bits are collected in
|		the INEX flag for use in A10.
|
|	A9.	Scale X -> Y.
|		The mantissa is scaled to the desired number of
|		significant digits.  The excess digits are collected
|		in INEX2.
|
|	A10.	Or in INEX.
|		If INEX is set, round error occurred.  This is
|		compensated for by 'or-ing' in the INEX2 flag to
|		the lsb of Y.
|
|	A11.	Restore original FPCR; set size ext.
|		Perform FINT operation in the user's rounding mode.
|		Keep the size to extended.
|
|	A12.	Calculate YINT = FINT(Y) according to user's rounding
|		mode.  The FPSP routine sintd0 is used.  The output
|		is in fp0.
|
|	A13.	Check for LEN digits.
|		If the int operation results in more than LEN digits,
|		or less than LEN -1 digits, adjust ILOG and repeat from
|		A6.  This test occurs only on the first pass.  If the
|		result is exactly 10^LEN, decrement ILOG and divide
|		the mantissa by 10.
|
|	A14.	Convert the mantissa to bcd.
|		The binstr routine is used to convert the LEN digit
|		mantissa to bcd in memory.  The input to binstr is
|		to be a fraction; i.e. (mantissa)/10^LEN and adjusted
|		such that the decimal point is to the left of bit 63.
|		The bcd digits are stored in the correct position in
|		the final string area in memory.
|
|	A15.	Convert the exponent to bcd.
|		As in A14 above, the exp is converted to bcd and the
|		digits are stored in the final string.
|		Test the length of the final exponent string.  If the
|		length is 4, set operr.
|
|	A16.	Write sign bits to final string.
|
|	Implementation Notes:
|
|	The registers are used as follows:
|
|		d0: scratch; LEN input to binstr
|		d1: scratch
|		d2: upper 32-bits of mantissa for binstr
|		d3: scratch;lower 32-bits of mantissa for binstr
|		d4: LEN
|		d5: LAMBDA/ICTR
|		d6: ILOG
|		d7: k-factor
|		a0: ptr for original operand/final result
|		a1: scratch pointer
|		a2: pointer to FP_X; abs(original value) in ext
|		fp0: scratch
|		fp1: scratch
|		fp2: scratch
|		F_SCR1:
|		F_SCR2:
|		L_SCR1:
|		L_SCR2:

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MOTOROLA
|	The copyright notice above does not evidence any
|	actual or intended publication of such source code.

|BINDEC    idnt    2,1 | Motorola 040 Floating Point Software Package

#include "fpsp.h"

	|section	8

| Constants in extended precision
LOG2:	.long	0x3FFD0000,0x9A209A84,0xFBCFF798,0x00000000
LOG2UP1:	.long	0x3FFD0000,0x9A209A84,0xFBCFF799,0x00000000

| Constants in single precision
FONE:	.long	0x3F800000,0x00000000,0x00000000,0x00000000
FTWO:	.long	0x40000000,0x00000000,0x00000000,0x00000000
FTEN:	.long	0x41200000,0x00000000,0x00000000,0x00000000
F4933:	.long	0x459A2800,0x00000000,0x00000000,0x00000000

RBDTBL:	.byte	0,0,0,0
	.byte	3,3,2,2
	.byte	3,2,2,3
	.byte	2,3,3,2

	|xref	binstr
	|xref	sintdo
	|xref	ptenrn,ptenrm,ptenrp

	.global	bindec
	.global	sc_mul
bindec:
	moveml	%d2-%d7/%a2,-(%a7)
	fmovemx %fp0-%fp2,-(%a7)

| A1. Set RM and size ext. Set SIGMA = sign input;
|     The k-factor is saved for use in d7.  Clear BINDEC_FLG for
|     separating  normalized/denormalized input.  If the input
|     is a denormalized number, set the BINDEC_FLG memory word
|     to signal denorm.  If the input is unnormalized, normalize
|     the input and test for denormalized result.
|
	fmovel	#rm_mode,%FPCR	|set RM and ext
	movel	(%a0),L_SCR2(%a6)	|save exponent for sign check
	movel	%d0,%d7		|move k-factor to d7
	clrb	BINDEC_FLG(%a6)	|clr norm/denorm flag
	movew	STAG(%a6),%d0	|get stag
	andiw	#0xe000,%d0	|isolate stag bits
	beq	A2_str		|if zero, input is norm
|
| Normalize the denorm
|
un_de_norm:
	movew	(%a0),%d0
	andiw	#0x7fff,%d0	|strip sign of normalized exp
	movel	4(%a0),%d1
	movel	8(%a0),%d2
norm_loop:
	subw	#1,%d0
	lsll	#1,%d2
	roxll	#1,%d1
	tstl	%d1
	bges	norm_loop
|
| Test if the normalized input is denormalized
|
	tstw	%d0
	bgts	pos_exp		|if greater than zero, it is a norm
	st	BINDEC_FLG(%a6)	|set flag for denorm
pos_exp:
	andiw	#0x7fff,%d0	|strip sign of normalized exp
	movew	%d0,(%a0)
	movel	%d1,4(%a0)
	movel	%d2,8(%a0)

| A2. Set X = abs(input).
|
A2_str:
	movel	(%a0),FP_SCR2(%a6) | move input to work space
	movel	4(%a0),FP_SCR2+4(%a6) | move input to work space
	movel	8(%a0),FP_SCR2+8(%a6) | move input to work space
	andil	#0x7fffffff,FP_SCR2(%a6) |create abs(X)

| A3. Compute ILOG.
|     ILOG is the log base 10 of the input value.  It is approx-
|     imated by adding e + 0.f when the original value is viewed
|     as 2^^e * 1.f in extended precision.  This value is stored
|     in d6.
|
| Register usage:
|	Input/Output
|	d0: k-factor/exponent
|	d2: x/x
|	d3: x/x
|	d4: x/x
|	d5: x/x
|	d6: x/ILOG
|	d7: k-factor/Unchanged
|	a0: ptr for original operand/final result
|	a1: x/x
|	a2: x/x
|	fp0: x/float(ILOG)
|	fp1: x/x
|	fp2: x/x
|	F_SCR1:x/x
|	F_SCR2:Abs(X)/Abs(X) with $3fff exponent
|	L_SCR1:x/x
|	L_SCR2:first word of X packed/Unchanged

	tstb	BINDEC_FLG(%a6)	|check for denorm
	beqs	A3_cont		|if clr, continue with norm
	movel	#-4933,%d6	|force ILOG = -4933
	bras	A4_str
A3_cont:
	movew	FP_SCR2(%a6),%d0	|move exp to d0
	movew	#0x3fff,FP_SCR2(%a6) |replace exponent with 0x3fff
	fmovex	FP_SCR2(%a6),%fp0	|now fp0 has 1.f
	subw	#0x3fff,%d0	|strip off bias
	faddw	%d0,%fp0		|add in exp
	fsubs	FONE,%fp0	|subtract off 1.0
	fbge	pos_res		|if pos, branch
	fmulx	LOG2UP1,%fp0	|if neg, mul by LOG2UP1
	fmovel	%fp0,%d6		|put ILOG in d6 as a lword
	bras	A4_str		|go move out ILOG
pos_res:
	fmulx	LOG2,%fp0	|if pos, mul by LOG2
	fmovel	%fp0,%d6		|put ILOG in d6 as a lword


| A4. Clr INEX bit.
|     The operation in A3 above may have set INEX2.

A4_str:
	fmovel	#0,%FPSR		|zero all of fpsr - nothing needed


| A5. Set ICTR = 0;
|     ICTR is a flag used in A13.  It must be set before the
|     loop entry A6. The lower word of d5 is used for ICTR.

	clrw	%d5		|clear ICTR


| A6. Calculate LEN.
|     LEN is the number of digits to be displayed.  The k-factor
|     can dictate either the total number of digits, if it is
|     a positive number, or the number of digits after the
|     original decimal point which are to be included as
|     significant.  See the 68882 manual for examples.
|     If LEN is computed to be greater than 17, set OPERR in
|     USER_FPSR.  LEN is stored in d4.
|
| Register usage:
|	Input/Output
|	d0: exponent/Unchanged
|	d2: x/x/scratch
|	d3: x/x
|	d4: exc picture/LEN
|	d5: ICTR/Unchanged
|	d6: ILOG/Unchanged
|	d7: k-factor/Unchanged
|	a0: ptr for original operand/final result
|	a1: x/x
|	a2: x/x
|	fp0: float(ILOG)/Unchanged
|	fp1: x/x
|	fp2: x/x
|	F_SCR1:x/x
|	F_SCR2:Abs(X) with $3fff exponent/Unchanged
|	L_SCR1:x/x
|	L_SCR2:first word of X packed/Unchanged

A6_str:
	tstl	%d7		|branch on sign of k
	bles	k_neg		|if k <= 0, LEN = ILOG + 1 - k
	movel	%d7,%d4		|if k > 0, LEN = k
	bras	len_ck		|skip to LEN check
k_neg:
	movel	%d6,%d4		|first load ILOG to d4
	subl	%d7,%d4		|subtract off k
	addql	#1,%d4		|add in the 1
len_ck:
	tstl	%d4		|LEN check: branch on sign of LEN
	bles	LEN_ng		|if neg, set LEN = 1
	cmpl	#17,%d4		|test if LEN > 17
	bles	A7_str		|if not, forget it
	movel	#17,%d4		|set max LEN = 17
	tstl	%d7		|if negative, never set OPERR
	bles	A7_str		|if positive, continue
	orl	#opaop_mask,USER_FPSR(%a6) |set OPERR & AIOP in USER_FPSR
	bras	A7_str		|finished here
LEN_ng:
	moveql	#1,%d4		|min LEN is 1


| A7. Calculate SCALE.
|     SCALE is equal to 10^ISCALE, where ISCALE is the number
|     of decimal places needed to insure LEN integer digits
|     in the output before conversion to bcd. LAMBDA is the sign
|     of ISCALE, used in A9.  Fp1 contains 10^^(abs(ISCALE)) using
|     the rounding mode as given in the following table (see
|     Coonen, p. 7.23 as ref.; however, the SCALE variable is
|     of opposite sign in bindec.sa from Coonen).
|
|	Initial					USE
|	FPCR[6:5]	LAMBDA	SIGN(X)		FPCR[6:5]
|	----------------------------------------------
|	 RN	00	   0	   0		00/0	RN
|	 RN	00	   0	   1		00/0	RN
|	 RN	00	   1	   0		00/0	RN
|	 RN	00	   1	   1		00/0	RN
|	 RZ	01	   0	   0		11/3	RP
|	 RZ	01	   0	   1		11/3	RP
|	 RZ	01	   1	   0		10/2	RM
|	 RZ	01	   1	   1		10/2	RM
|	 RM	10	   0	   0		11/3	RP
|	 RM	10	   0	   1		10/2	RM
|	 RM	10	   1	   0		10/2	RM
|	 RM	10	   1	   1		11/3	RP
|	 RP	11	   0	   0		10/2	RM
|	 RP	11	   0	   1		11/3	RP
|	 RP	11	   1	   0		11/3	RP
|	 RP	11	   1	   1		10/2	RM
|
| Register usage:
|	Input/Output
|	d0: exponent/scratch - final is 0
|	d2: x/0 or 24 for A9
|	d3: x/scratch - offset ptr into PTENRM array
|	d4: LEN/Unchanged
|	d5: 0/ICTR:LAMBDA
|	d6: ILOG/ILOG or k if ((k<=0)&(ILOG<k))
|	d7: k-factor/Unchanged
|	a0: ptr for original operand/final result
|	a1: x/ptr to PTENRM array
|	a2: x/x
|	fp0: float(ILOG)/Unchanged
|	fp1: x/10^ISCALE
|	fp2: x/x
|	F_SCR1:x/x
|	F_SCR2:Abs(X) with $3fff exponent/Unchanged
|	L_SCR1:x/x
|	L_SCR2:first word of X packed/Unchanged

A7_str:
	tstl	%d7		|test sign of k
	bgts	k_pos		|if pos and > 0, skip this
	cmpl	%d6,%d7		|test k - ILOG
	blts	k_pos		|if ILOG >= k, skip this
	movel	%d7,%d6		|if ((k<0) & (ILOG < k)) ILOG = k
k_pos:
	movel	%d6,%d0		|calc ILOG + 1 - LEN in d0
	addql	#1,%d0		|add the 1
	subl	%d4,%d0		|sub off LEN
	swap	%d5		|use upper word of d5 for LAMBDA
	clrw	%d5		|set it zero initially
	clrw	%d2		|set up d2 for very small case
	tstl	%d0		|test sign of ISCALE
	bges	iscale		|if pos, skip next inst
	addqw	#1,%d5		|if neg, set LAMBDA true
	cmpl	#0xffffecd4,%d0	|test iscale <= -4908
	bgts	no_inf		|if false, skip rest
	addil	#24,%d0		|add in 24 to iscale
	movel	#24,%d2		|put 24 in d2 for A9
no_inf:
	negl	%d0		|and take abs of ISCALE
iscale:
	fmoves	FONE,%fp1	|init fp1 to 1
	bfextu	USER_FPCR(%a6){#26:#2},%d1 |get initial rmode bits
	lslw	#1,%d1		|put them in bits 2:1
	addw	%d5,%d1		|add in LAMBDA
	lslw	#1,%d1		|put them in bits 3:1
	tstl	L_SCR2(%a6)	|test sign of original x
	bges	x_pos		|if pos, don't set bit 0
	addql	#1,%d1		|if neg, set bit 0
x_pos:
	leal	RBDTBL,%a2	|load rbdtbl base
	moveb	(%a2,%d1),%d3	|load d3 with new rmode
	lsll	#4,%d3		|put bits in proper position
	fmovel	%d3,%fpcr		|load bits into fpu
	lsrl	#4,%d3		|put bits in proper position
	tstb	%d3		|decode new rmode for pten table
	bnes	not_rn		|if zero, it is RN
	leal	PTENRN,%a1	|load a1 with RN table base
	bras	rmode		|exit decode
not_rn:
	lsrb	#1,%d3		|get lsb in carry
	bccs	not_rp		|if carry clear, it is RM
	leal	PTENRP,%a1	|load a1 with RP table base
	bras	rmode		|exit decode
not_rp:
	leal	PTENRM,%a1	|load a1 with RM table base
rmode:
	clrl	%d3		|clr table index
e_loop:
	lsrl	#1,%d0		|shift next bit into carry
	bccs	e_next		|if zero, skip the mul
	fmulx	(%a1,%d3),%fp1	|mul by 10**(d3_bit_no)
e_next:
	addl	#12,%d3		|inc d3 to next pwrten table entry
	tstl	%d0		|test if ISCALE is zero
	bnes	e_loop		|if not, loop


| A8. Clr INEX; Force RZ.
|     The operation in A3 above may have set INEX2.
|     RZ mode is forced for the scaling operation to insure
|     only one rounding error.  The grs bits are collected in
|     the INEX flag for use in A10.
|
| Register usage:
|	Input/Output

	fmovel	#0,%FPSR		|clr INEX
	fmovel	#rz_mode,%FPCR	|set RZ rounding mode


| A9. Scale X -> Y.
|     The mantissa is scaled to the desired number of significant
|     digits.  The excess digits are collected in INEX2. If mul,
|     Check d2 for excess 10 exponential value.  If not zero,
|     the iscale value would have caused the pwrten calculation
|     to overflow.  Only a negative iscale can cause this, so
|     multiply by 10^(d2), which is now only allowed to be 24,
|     with a multiply by 10^8 and 10^16, which is exact since
|     10^24 is exact.  If the input was denormalized, we must
|     create a busy stack frame with the mul command and the
|     two operands, and allow the fpu to complete the multiply.
|
| Register usage:
|	Input/Output
|	d0: FPCR with RZ mode/Unchanged
|	d2: 0 or 24/unchanged
|	d3: x/x
|	d4: LEN/Unchanged
|	d5: ICTR:LAMBDA
|	d6: ILOG/Unchanged
|	d7: k-factor/Unchanged
|	a0: ptr for original operand/final result
|	a1: ptr to PTENRM array/Unchanged
|	a2: x/x
|	fp0: float(ILOG)/X adjusted for SCALE (Y)
|	fp1: 10^ISCALE/Unchanged
|	fp2: x/x
|	F_SCR1:x/x
|	F_SCR2:Abs(X) with $3fff exponent/Unchanged
|	L_SCR1:x/x
|	L_SCR2:first word of X packed/Unchanged

A9_str:
	fmovex	(%a0),%fp0	|load X from memory
	fabsx	%fp0		|use abs(X)
	tstw	%d5		|LAMBDA is in lower word of d5
	bne	sc_mul		|if neg (LAMBDA = 1), scale by mul
	fdivx	%fp1,%fp0		|calculate X / SCALE -> Y to fp0
	bras	A10_st		|branch to A10

sc_mul:
	tstb	BINDEC_FLG(%a6)	|check for denorm
	beqs	A9_norm		|if norm, continue with mul
	fmovemx %fp1-%fp1,-(%a7)	|load ETEMP with 10^ISCALE
	movel	8(%a0),-(%a7)	|load FPTEMP with input arg
	movel	4(%a0),-(%a7)
	movel	(%a0),-(%a7)
	movel	#18,%d3		|load count for busy stack
A9_loop:
	clrl	-(%a7)		|clear lword on stack
	dbf	%d3,A9_loop
	moveb	VER_TMP(%a6),(%a7) |write current version number
	moveb	#BUSY_SIZE-4,1(%a7) |write current busy size
	moveb	#0x10,0x44(%a7)	|set fcefpte[15] bit
	movew	#0x0023,0x40(%a7)	|load cmdreg1b with mul command
	moveb	#0xfe,0x8(%a7)	|load all 1s to cu savepc
	frestore (%a7)+		|restore frame to fpu for completion
	fmulx	36(%a1),%fp0	|multiply fp0 by 10^8
	fmulx	48(%a1),%fp0	|multiply fp0 by 10^16
	bras	A10_st
A9_norm:
	tstw	%d2		|test for small exp case
	beqs	A9_con		|if zero, continue as normal
	fmulx	36(%a1),%fp0	|multiply fp0 by 10^8
	fmulx	48(%a1),%fp0	|multiply fp0 by 10^16
A9_con:
	fmulx	%fp1,%fp0		|calculate X * SCALE -> Y to fp0


| A10. Or in INEX.
|      If INEX is set, round error occurred.  This is compensated
|      for by 'or-ing' in the INEX2 flag to the lsb of Y.
|
| Register usage:
|	Input/Output
|	d0: FPCR with RZ mode/FPSR with INEX2 isolated
|	d2: x/x
|	d3: x/x
|	d4: LEN/Unchanged
|	d5: ICTR:LAMBDA
|	d6: ILOG/Unchanged
|	d7: k-factor/Unchanged
|	a0: ptr for original operand/final result
|	a1: ptr to PTENxx array/Unchanged
|	a2: x/ptr to FP_SCR2(a6)
|	fp0: Y/Y with lsb adjusted
|	fp1: 10^ISCALE/Unchanged
|	fp2: x/x

A10_st:
	fmovel	%FPSR,%d0		|get FPSR
	fmovex	%fp0,FP_SCR2(%a6)	|move Y to memory
	leal	FP_SCR2(%a6),%a2	|load a2 with ptr to FP_SCR2
	btstl	#9,%d0		|check if INEX2 set
	beqs	A11_st		|if clear, skip rest
	oril	#1,8(%a2)	|or in 1 to lsb of mantissa
	fmovex	FP_SCR2(%a6),%fp0	|write adjusted Y back to fpu


| A11. Restore original FPCR; set size ext.
|      Perform FINT operation in the user's rounding mode.  Keep
|      the size to extended.  The sintdo entry point in the sint
|      routine expects the FPCR value to be in USER_FPCR for
|      mode and precision.  The original FPCR is saved in L_SCR1.

A11_st:
	movel	USER_FPCR(%a6),L_SCR1(%a6) |save it for later
	andil	#0x00000030,USER_FPCR(%a6) |set size to ext,
|					;block exceptions


| A12. Calculate YINT = FINT(Y) according to user's rounding mode.
|      The FPSP routine sintd0 is used.  The output is in fp0.
|
| Register usage:
|	Input/Output
|	d0: FPSR with AINEX cleared/FPCR with size set to ext
|	d2: x/x/scratch
|	d3: x/x
|	d4: LEN/Unchanged
|	d5: ICTR:LAMBDA/Unchanged
|	d6: ILOG/Unchanged
|	d7: k-factor/Unchanged
|	a0: ptr for original operand/src ptr for sintdo
|	a1: ptr to PTENxx array/Unchanged
|	a2: ptr to FP_SCR2(a6)/Unchanged
|	a6: temp pointer to FP_SCR2(a6) - orig value saved and restored
|	fp0: Y/YINT
|	fp1: 10^ISCALE/Unchanged
|	fp2: x/x
|	F_SCR1:x/x
|	F_SCR2:Y adjusted for inex/Y with original exponent
|	L_SCR1:x/original USER_FPCR
|	L_SCR2:first word of X packed/Unchanged

A12_st:
	moveml	%d0-%d1/%a0-%a1,-(%a7)	|save regs used by sintd0
	movel	L_SCR1(%a6),-(%a7)
	movel	L_SCR2(%a6),-(%a7)
	leal	FP_SCR2(%a6),%a0		|a0 is ptr to F_SCR2(a6)
	fmovex	%fp0,(%a0)		|move Y to memory at FP_SCR2(a6)
	tstl	L_SCR2(%a6)		|test sign of original operand
	bges	do_fint			|if pos, use Y
	orl	#0x80000000,(%a0)		|if neg, use -Y
do_fint:
	movel	USER_FPSR(%a6),-(%a7)
	bsr	sintdo			|sint routine returns int in fp0
	moveb	(%a7),USER_FPSR(%a6)
	addl	#4,%a7
	movel	(%a7)+,L_SCR2(%a6)
	movel	(%a7)+,L_SCR1(%a6)
	moveml	(%a7)+,%d0-%d1/%a0-%a1	|restore regs used by sint
	movel	L_SCR2(%a6),FP_SCR2(%a6)	|restore original exponent
	movel	L_SCR1(%a6),USER_FPCR(%a6) |restore user's FPCR


| A13. Check for LEN digits.
|      If the int operation results in more than LEN digits,
|      or less than LEN -1 digits, adjust ILOG and repeat from
|      A6.  This test occurs only on the first pass.  If the
|      result is exactly 10^LEN, decrement ILOG and divide
|      the mantissa by 10.  The calculation of 10^LEN cannot
|      be inexact, since all powers of ten upto 10^27 are exact
|      in extended precision, so the use of a previous power-of-ten
|      table will introduce no error.
|
|
| Register usage:
|	Input/Output
|	d0: FPCR with size set to ext/scratch final = 0
|	d2: x/x
|	d3: x/scratch final = x
|	d4: LEN/LEN adjusted
|	d5: ICTR:LAMBDA/LAMBDA:ICTR
|	d6: ILOG/ILOG adjusted
|	d7: k-factor/Unchanged
|	a0: pointer into memory for packed bcd string formation
|	a1: ptr to PTENxx array/Unchanged
|	a2: ptr to FP_SCR2(a6)/Unchanged
|	fp0: int portion of Y/abs(YINT) adjusted
|	fp1: 10^ISCALE/Unchanged
|	fp2: x/10^LEN
|	F_SCR1:x/x
|	F_SCR2:Y with original exponent/Unchanged
|	L_SCR1:original USER_FPCR/Unchanged
|	L_SCR2:first word of X packed/Unchanged

A13_st:
	swap	%d5		|put ICTR in lower word of d5
	tstw	%d5		|check if ICTR = 0
	bne	not_zr		|if non-zero, go to second test
|
| Compute 10^(LEN-1)
|
	fmoves	FONE,%fp2	|init fp2 to 1.0
	movel	%d4,%d0		|put LEN in d0
	subql	#1,%d0		|d0 = LEN -1
	clrl	%d3		|clr table index
l_loop:
	lsrl	#1,%d0		|shift next bit into carry
	bccs	l_next		|if zero, skip the mul
	fmulx	(%a1,%d3),%fp2	|mul by 10**(d3_bit_no)
l_next:
	addl	#12,%d3		|inc d3 to next pwrten table entry
	tstl	%d0		|test if LEN is zero
	bnes	l_loop		|if not, loop
|
| 10^LEN-1 is computed for this test and A14.  If the input was
| denormalized, check only the case in which YINT > 10^LEN.
|
	tstb	BINDEC_FLG(%a6)	|check if input was norm
	beqs	A13_con		|if norm, continue with checking
	fabsx	%fp0		|take abs of YINT
	bra	test_2
|
| Compare abs(YINT) to 10^(LEN-1) and 10^LEN
|
A13_con:
	fabsx	%fp0		|take abs of YINT
	fcmpx	%fp2,%fp0		|compare abs(YINT) with 10^(LEN-1)
	fbge	test_2		|if greater, do next test
	subql	#1,%d6		|subtract 1 from ILOG
	movew	#1,%d5		|set ICTR
	fmovel	#rm_mode,%FPCR	|set rmode to RM
	fmuls	FTEN,%fp2	|compute 10^LEN
	bra	A6_str		|return to A6 and recompute YINT
test_2:
	fmuls	FTEN,%fp2	|compute 10^LEN
	fcmpx	%fp2,%fp0		|compare abs(YINT) with 10^LEN
	fblt	A14_st		|if less, all is ok, go to A14
	fbgt	fix_ex		|if greater, fix and redo
	fdivs	FTEN,%fp0	|if equal, divide by 10
	addql	#1,%d6		| and inc ILOG
	bras	A14_st		| and continue elsewhere
fix_ex:
	addql	#1,%d6		|increment ILOG by 1
	movew	#1,%d5		|set ICTR
	fmovel	#rm_mode,%FPCR	|set rmode to RM
	bra	A6_str		|return to A6 and recompute YINT
|
| Since ICTR <> 0, we have already been through one adjustment,
| and shouldn't have another; this is to check if abs(YINT) = 10^LEN
| 10^LEN is again computed using whatever table is in a1 since the
| value calculated cannot be inexact.
|
not_zr:
	fmoves	FONE,%fp2	|init fp2 to 1.0
	movel	%d4,%d0		|put LEN in d0
	clrl	%d3		|clr table index
z_loop:
	lsrl	#1,%d0		|shift next bit into carry
	bccs	z_next		|if zero, skip the mul
	fmulx	(%a1,%d3),%fp2	|mul by 10**(d3_bit_no)
z_next:
	addl	#12,%d3		|inc d3 to next pwrten table entry
	tstl	%d0		|test if LEN is zero
	bnes	z_loop		|if not, loop
	fabsx	%fp0		|get abs(YINT)
	fcmpx	%fp2,%fp0		|check if abs(YINT) = 10^LEN
	fbne	A14_st		|if not, skip this
	fdivs	FTEN,%fp0	|divide abs(YINT) by 10
	addql	#1,%d6		|and inc ILOG by 1
	addql	#1,%d4		| and inc LEN
	fmuls	FTEN,%fp2	| if LEN++, the get 10^^LEN


| A14. Convert the mantissa to bcd.
|      The binstr routine is used to convert the LEN digit
|      mantissa to bcd in memory.  The input to binstr is
|      to be a fraction; i.e. (mantissa)/10^LEN and adjusted
|      such that the decimal point is to the left of bit 63.
|      The bcd digits are stored in the correct position in
|      the final string area in memory.
|
|
| Register usage:
|	Input/Output
|	d0: x/LEN call to binstr - final is 0
|	d1: x/0
|	d2: x/ms 32-bits of mant of abs(YINT)
|	d3: x/ls 32-bits of mant of abs(YINT)
|	d4: LEN/Unchanged
|	d5: ICTR:LAMBDA/LAMBDA:ICTR
|	d6: ILOG
|	d7: k-factor/Unchanged
|	a0: pointer into memory for packed bcd string formation
|	    /ptr to first mantissa byte in result string
|	a1: ptr to PTENxx array/Unchanged
|	a2: ptr to FP_SCR2(a6)/Unchanged
|	fp0: int portion of Y/abs(YINT) adjusted
|	fp1: 10^ISCALE/Unchanged
|	fp2: 10^LEN/Unchanged
|	F_SCR1:x/Work area for final result
|	F_SCR2:Y with original exponent/Unchanged
|	L_SCR1:original USER_FPCR/Unchanged
|	L_SCR2:first word of X packed/Unchanged

A14_st:
	fmovel	#rz_mode,%FPCR	|force rz for conversion
	fdivx	%fp2,%fp0		|divide abs(YINT) by 10^LEN
	leal	FP_SCR1(%a6),%a0
	fmovex	%fp0,(%a0)	|move abs(YINT)/10^LEN to memory
	movel	4(%a0),%d2	|move 2nd word of FP_RES to d2
	movel	8(%a0),%d3	|move 3rd word of FP_RES to d3
	clrl	4(%a0)		|zero word 2 of FP_RES
	clrl	8(%a0)		|zero word 3 of FP_RES
	movel	(%a0),%d0		|move exponent to d0
	swap	%d0		|put exponent in lower word
	beqs	no_sft		|if zero, don't shift
	subil	#0x3ffd,%d0	|sub bias less 2 to make fract
	tstl	%d0		|check if > 1
	bgts	no_sft		|if so, don't shift
	negl	%d0		|make exp positive
m_loop:
	lsrl	#1,%d2		|shift d2:d3 right, add 0s
	roxrl	#1,%d3		|the number of places
	dbf	%d0,m_loop	|given in d0
no_sft:
	tstl	%d2		|check for mantissa of zero
	bnes	no_zr		|if not, go on
	tstl	%d3		|continue zero check
	beqs	zer_m		|if zero, go directly to binstr
no_zr:
	clrl	%d1		|put zero in d1 for addx
	addil	#0x00000080,%d3	|inc at bit 7
	addxl	%d1,%d2		|continue inc
	andil	#0xffffff80,%d3	|strip off lsb not used by 882
zer_m:
	movel	%d4,%d0		|put LEN in d0 for binstr call
	addql	#3,%a0		|a0 points to M16 byte in result
	bsr	binstr		|call binstr to convert mant


| A15. Convert the exponent to bcd.
|      As in A14 above, the exp is converted to bcd and the
|      digits are stored in the final string.
|
|      Digits are stored in L_SCR1(a6) on return from BINDEC as:
|
|	 32               16 15                0
|	-----------------------------------------
|	|  0 | e3 | e2 | e1 | e4 |  X |  X |  X |
|	-----------------------------------------
|
| And are moved into their proper places in FP_SCR1.  If digit e4
| is non-zero, OPERR is signaled.  In all cases, all 4 digits are
| written as specified in the 881/882 manual for packed decimal.
|
| Register usage:
|	Input/Output
|	d0: x/LEN call to binstr - final is 0
|	d1: x/scratch (0);shift count for final exponent packing
|	d2: x/ms 32-bits of exp fraction/scratch
|	d3: x/ls 32-bits of exp fraction
|	d4: LEN/Unchanged
|	d5: ICTR:LAMBDA/LAMBDA:ICTR
|	d6: ILOG
|	d7: k-factor/Unchanged
|	a0: ptr to result string/ptr to L_SCR1(a6)
|	a1: ptr to PTENxx array/Unchanged
|	a2: ptr to FP_SCR2(a6)/Unchanged
|	fp0: abs(YINT) adjusted/float(ILOG)
|	fp1: 10^ISCALE/Unchanged
|	fp2: 10^LEN/Unchanged
|	F_SCR1:Work area for final result/BCD result
|	F_SCR2:Y with original exponent/ILOG/10^4
|	L_SCR1:original USER_FPCR/Exponent digits on return from binstr
|	L_SCR2:first word of X packed/Unchanged

A15_st:
	tstb	BINDEC_FLG(%a6)	|check for denorm
	beqs	not_denorm
	ftstx	%fp0		|test for zero
	fbeq	den_zero	|if zero, use k-factor or 4933
	fmovel	%d6,%fp0		|float ILOG
	fabsx	%fp0		|get abs of ILOG
	bras	convrt
den_zero:
	tstl	%d7		|check sign of the k-factor
	blts	use_ilog	|if negative, use ILOG
	fmoves	F4933,%fp0	|force exponent to 4933
	bras	convrt		|do it
use_ilog:
	fmovel	%d6,%fp0		|float ILOG
	fabsx	%fp0		|get abs of ILOG
	bras	convrt
not_denorm:
	ftstx	%fp0		|test for zero
	fbne	not_zero	|if zero, force exponent
	fmoves	FONE,%fp0	|force exponent to 1
	bras	convrt		|do it
not_zero:
	fmovel	%d6,%fp0		|float ILOG
	fabsx	%fp0		|get abs of ILOG
convrt:
	fdivx	24(%a1),%fp0	|compute ILOG/10^4
	fmovex	%fp0,FP_SCR2(%a6)	|store fp0 in memory
	movel	4(%a2),%d2	|move word 2 to d2
	movel	8(%a2),%d3	|move word 3 to d3
	movew	(%a2),%d0		|move exp to d0
	beqs	x_loop_fin	|if zero, skip the shift
	subiw	#0x3ffd,%d0	|subtract off bias
	negw	%d0		|make exp positive
x_loop:
	lsrl	#1,%d2		|shift d2:d3 right
	roxrl	#1,%d3		|the number of places
	dbf	%d0,x_loop	|given in d0
x_loop_fin:
	clrl	%d1		|put zero in d1 for addx
	addil	#0x00000080,%d3	|inc at bit 6
	addxl	%d1,%d2		|continue inc
	andil	#0xffffff80,%d3	|strip off lsb not used by 882
	movel	#4,%d0		|put 4 in d0 for binstr call
	leal	L_SCR1(%a6),%a0	|a0 is ptr to L_SCR1 for exp digits
	bsr	binstr		|call binstr to convert exp
	movel	L_SCR1(%a6),%d0	|load L_SCR1 lword to d0
	movel	#12,%d1		|use d1 for shift count
	lsrl	%d1,%d0		|shift d0 right by 12
	bfins	%d0,FP_SCR1(%a6){#4:#12} |put e3:e2:e1 in FP_SCR1
	lsrl	%d1,%d0		|shift d0 right by 12
	bfins	%d0,FP_SCR1(%a6){#16:#4} |put e4 in FP_SCR1
	tstb	%d0		|check if e4 is zero
	beqs	A16_st		|if zero, skip rest
	orl	#opaop_mask,USER_FPSR(%a6) |set OPERR & AIOP in USER_FPSR


| A16. Write sign bits to final string.
|	   Sigma is bit 31 of initial value; RHO is bit 31 of d6 (ILOG).
|
| Register usage:
|	Input/Output
|	d0: x/scratch - final is x
|	d2: x/x
|	d3: x/x
|	d4: LEN/Unchanged
|	d5: ICTR:LAMBDA/LAMBDA:ICTR
|	d6: ILOG/ILOG adjusted
|	d7: k-factor/Unchanged
|	a0: ptr to L_SCR1(a6)/Unchanged
|	a1: ptr to PTENxx array/Unchanged
|	a2: ptr to FP_SCR2(a6)/Unchanged
|	fp0: float(ILOG)/Unchanged
|	fp1: 10^ISCALE/Unchanged
|	fp2: 10^LEN/Unchanged
|	F_SCR1:BCD result with correct signs
|	F_SCR2:ILOG/10^4
|	L_SCR1:Exponent digits on return from binstr
|	L_SCR2:first word of X packed/Unchanged

A16_st:
	clrl	%d0		|clr d0 for collection of signs
	andib	#0x0f,FP_SCR1(%a6) |clear first nibble of FP_SCR1
	tstl	L_SCR2(%a6)	|check sign of original mantissa
	bges	mant_p		|if pos, don't set SM
	moveql	#2,%d0		|move 2 in to d0 for SM
mant_p:
	tstl	%d6		|check sign of ILOG
	bges	wr_sgn		|if pos, don't set SE
	addql	#1,%d0		|set bit 0 in d0 for SE
wr_sgn:
	bfins	%d0,FP_SCR1(%a6){#0:#2} |insert SM and SE into FP_SCR1

| Clean up and restore all registers used.

	fmovel	#0,%FPSR		|clear possible inex2/ainex bits
	fmovemx (%a7)+,%fp0-%fp2
	moveml	(%a7)+,%d2-%d7/%a2
	rts

	|end
