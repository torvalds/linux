|
|	decbin.sa 3.3 12/19/90
|
|	Description: Converts normalized packed bcd value pointed to by
|	register A6 to extended-precision value in FP0.
|
|	Input: Normalized packed bcd value in ETEMP(a6).
|
|	Output:	Exact floating-point representation of the packed bcd value.
|
|	Saves and Modifies: D2-D5
|
|	Speed: The program decbin takes ??? cycles to execute.
|
|	Object Size:
|
|	External Reference(s): None.
|
|	Algorithm:
|	Expected is a normal bcd (i.e. non-exceptional; all inf, zero,
|	and NaN operands are dispatched without entering this routine)
|	value in 68881/882 format at location ETEMP(A6).
|
|	A1.	Convert the bcd exponent to binary by successive adds and muls.
|	Set the sign according to SE. Subtract 16 to compensate
|	for the mantissa which is to be interpreted as 17 integer
|	digits, rather than 1 integer and 16 fraction digits.
|	Note: this operation can never overflow.
|
|	A2. Convert the bcd mantissa to binary by successive
|	adds and muls in FP0. Set the sign according to SM.
|	The mantissa digits will be converted with the decimal point
|	assumed following the least-significant digit.
|	Note: this operation can never overflow.
|
|	A3. Count the number of leading/trailing zeros in the
|	bcd string.  If SE is positive, count the leading zeros;
|	if negative, count the trailing zeros.  Set the adjusted
|	exponent equal to the exponent from A1 and the zero count
|	added if SM = 1 and subtracted if SM = 0.  Scale the
|	mantissa the equivalent of forcing in the bcd value:
|
|	SM = 0	a non-zero digit in the integer position
|	SM = 1	a non-zero digit in Mant0, lsd of the fraction
|
|	this will insure that any value, regardless of its
|	representation (ex. 0.1E2, 1E1, 10E0, 100E-1), is converted
|	consistently.
|
|	A4. Calculate the factor 10^exp in FP1 using a table of
|	10^(2^n) values.  To reduce the error in forming factors
|	greater than 10^27, a directed rounding scheme is used with
|	tables rounded to RN, RM, and RP, according to the table
|	in the comments of the pwrten section.
|
|	A5. Form the final binary number by scaling the mantissa by
|	the exponent factor.  This is done by multiplying the
|	mantissa in FP0 by the factor in FP1 if the adjusted
|	exponent sign is positive, and dividing FP0 by FP1 if
|	it is negative.
|
|	Clean up and return.  Check if the final mul or div resulted
|	in an inex2 exception.  If so, set inex1 in the fpsr and
|	check if the inex1 exception is enabled.  If so, set d7 upper
|	word to $0100.  This will signal unimp.sa that an enabled inex1
|	exception occurred.  Unimp will fix the stack.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

|DECBIN    idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

|
|	PTENRN, PTENRM, and PTENRP are arrays of powers of 10 rounded
|	to nearest, minus, and plus, respectively.  The tables include
|	10**{1,2,4,8,16,32,64,128,256,512,1024,2048,4096}.  No rounding
|	is required until the power is greater than 27, however, all
|	tables include the first 5 for ease of indexing.
|
	|xref	PTENRN
	|xref	PTENRM
	|xref	PTENRP

RTABLE:	.byte	0,0,0,0
	.byte	2,3,2,3
	.byte	2,3,3,2
	.byte	3,2,2,3

	.global	decbin
	.global	calc_e
	.global	pwrten
	.global	calc_m
	.global	norm
	.global	ap_st_z
	.global	ap_st_n
|
	.set	FNIBS,7
	.set	FSTRT,0
|
	.set	ESTRT,4
	.set	EDIGITS,2	|
|
| Constants in single precision
FZERO:	.long	0x00000000
FONE:	.long	0x3F800000
FTEN:	.long	0x41200000

	.set	TEN,10

|
decbin:
	| fmovel	#0,FPCR		;clr real fpcr
	moveml	%d2-%d5,-(%a7)
|
| Calculate exponent:
|  1. Copy bcd value in memory for use as a working copy.
|  2. Calculate absolute value of exponent in d1 by mul and add.
|  3. Correct for exponent sign.
|  4. Subtract 16 to compensate for interpreting the mant as all integer digits.
|     (i.e., all digits assumed left of the decimal point.)
|
| Register usage:
|
|  calc_e:
|	(*)  d0: temp digit storage
|	(*)  d1: accumulator for binary exponent
|	(*)  d2: digit count
|	(*)  d3: offset pointer
|	( )  d4: first word of bcd
|	( )  a0: pointer to working bcd value
|	( )  a6: pointer to original bcd value
|	(*)  FP_SCR1: working copy of original bcd value
|	(*)  L_SCR1: copy of original exponent word
|
calc_e:
	movel	#EDIGITS,%d2	|# of nibbles (digits) in fraction part
	moveql	#ESTRT,%d3	|counter to pick up digits
	leal	FP_SCR1(%a6),%a0	|load tmp bcd storage address
	movel	ETEMP(%a6),(%a0)	|save input bcd value
	movel	ETEMP_HI(%a6),4(%a0) |save words 2 and 3
	movel	ETEMP_LO(%a6),8(%a0) |and work with these
	movel	(%a0),%d4	|get first word of bcd
	clrl	%d1		|zero d1 for accumulator
e_gd:
	mulul	#TEN,%d1	|mul partial product by one digit place
	bfextu	%d4{%d3:#4},%d0	|get the digit and zero extend into d0
	addl	%d0,%d1		|d1 = d1 + d0
	addqb	#4,%d3		|advance d3 to the next digit
	dbf	%d2,e_gd	|if we have used all 3 digits, exit loop
	btst	#30,%d4		|get SE
	beqs	e_pos		|don't negate if pos
	negl	%d1		|negate before subtracting
e_pos:
	subl	#16,%d1		|sub to compensate for shift of mant
	bges	e_save		|if still pos, do not neg
	negl	%d1		|now negative, make pos and set SE
	orl	#0x40000000,%d4	|set SE in d4,
	orl	#0x40000000,(%a0)	|and in working bcd
e_save:
	movel	%d1,L_SCR1(%a6)	|save exp in memory
|
|
| Calculate mantissa:
|  1. Calculate absolute value of mantissa in fp0 by mul and add.
|  2. Correct for mantissa sign.
|     (i.e., all digits assumed left of the decimal point.)
|
| Register usage:
|
|  calc_m:
|	(*)  d0: temp digit storage
|	(*)  d1: lword counter
|	(*)  d2: digit count
|	(*)  d3: offset pointer
|	( )  d4: words 2 and 3 of bcd
|	( )  a0: pointer to working bcd value
|	( )  a6: pointer to original bcd value
|	(*) fp0: mantissa accumulator
|	( )  FP_SCR1: working copy of original bcd value
|	( )  L_SCR1: copy of original exponent word
|
calc_m:
	moveql	#1,%d1		|word counter, init to 1
	fmoves	FZERO,%fp0	|accumulator
|
|
|  Since the packed number has a long word between the first & second parts,
|  get the integer digit then skip down & get the rest of the
|  mantissa.  We will unroll the loop once.
|
	bfextu	(%a0){#28:#4},%d0	|integer part is ls digit in long word
	faddb	%d0,%fp0		|add digit to sum in fp0
|
|
|  Get the rest of the mantissa.
|
loadlw:
	movel	(%a0,%d1.L*4),%d4	|load mantissa longword into d4
	moveql	#FSTRT,%d3	|counter to pick up digits
	moveql	#FNIBS,%d2	|reset number of digits per a0 ptr
md2b:
	fmuls	FTEN,%fp0	|fp0 = fp0 * 10
	bfextu	%d4{%d3:#4},%d0	|get the digit and zero extend
	faddb	%d0,%fp0	|fp0 = fp0 + digit
|
|
|  If all the digits (8) in that long word have been converted (d2=0),
|  then inc d1 (=2) to point to the next long word and reset d3 to 0
|  to initialize the digit offset, and set d2 to 7 for the digit count;
|  else continue with this long word.
|
	addqb	#4,%d3		|advance d3 to the next digit
	dbf	%d2,md2b		|check for last digit in this lw
nextlw:
	addql	#1,%d1		|inc lw pointer in mantissa
	cmpl	#2,%d1		|test for last lw
	ble	loadlw		|if not, get last one

|
|  Check the sign of the mant and make the value in fp0 the same sign.
|
m_sign:
	btst	#31,(%a0)	|test sign of the mantissa
	beq	ap_st_z		|if clear, go to append/strip zeros
	fnegx	%fp0		|if set, negate fp0

|
| Append/strip zeros:
|
|  For adjusted exponents which have an absolute value greater than 27*,
|  this routine calculates the amount needed to normalize the mantissa
|  for the adjusted exponent.  That number is subtracted from the exp
|  if the exp was positive, and added if it was negative.  The purpose
|  of this is to reduce the value of the exponent and the possibility
|  of error in calculation of pwrten.
|
|  1. Branch on the sign of the adjusted exponent.
|  2p.(positive exp)
|   2. Check M16 and the digits in lwords 2 and 3 in descending order.
|   3. Add one for each zero encountered until a non-zero digit.
|   4. Subtract the count from the exp.
|   5. Check if the exp has crossed zero in #3 above; make the exp abs
|	   and set SE.
|	6. Multiply the mantissa by 10**count.
|  2n.(negative exp)
|   2. Check the digits in lwords 3 and 2 in descending order.
|   3. Add one for each zero encountered until a non-zero digit.
|   4. Add the count to the exp.
|   5. Check if the exp has crossed zero in #3 above; clear SE.
|   6. Divide the mantissa by 10**count.
|
|  *Why 27?  If the adjusted exponent is within -28 < expA < 28, than
|   any adjustment due to append/strip zeros will drive the resultant
|   exponent towards zero.  Since all pwrten constants with a power
|   of 27 or less are exact, there is no need to use this routine to
|   attempt to lessen the resultant exponent.
|
| Register usage:
|
|  ap_st_z:
|	(*)  d0: temp digit storage
|	(*)  d1: zero count
|	(*)  d2: digit count
|	(*)  d3: offset pointer
|	( )  d4: first word of bcd
|	(*)  d5: lword counter
|	( )  a0: pointer to working bcd value
|	( )  FP_SCR1: working copy of original bcd value
|	( )  L_SCR1: copy of original exponent word
|
|
| First check the absolute value of the exponent to see if this
| routine is necessary.  If so, then check the sign of the exponent
| and do append (+) or strip (-) zeros accordingly.
| This section handles a positive adjusted exponent.
|
ap_st_z:
	movel	L_SCR1(%a6),%d1	|load expA for range test
	cmpl	#27,%d1		|test is with 27
	ble	pwrten		|if abs(expA) <28, skip ap/st zeros
	btst	#30,(%a0)	|check sign of exp
	bne	ap_st_n		|if neg, go to neg side
	clrl	%d1		|zero count reg
	movel	(%a0),%d4		|load lword 1 to d4
	bfextu	%d4{#28:#4},%d0	|get M16 in d0
	bnes	ap_p_fx		|if M16 is non-zero, go fix exp
	addql	#1,%d1		|inc zero count
	moveql	#1,%d5		|init lword counter
	movel	(%a0,%d5.L*4),%d4	|get lword 2 to d4
	bnes	ap_p_cl		|if lw 2 is zero, skip it
	addql	#8,%d1		|and inc count by 8
	addql	#1,%d5		|inc lword counter
	movel	(%a0,%d5.L*4),%d4	|get lword 3 to d4
ap_p_cl:
	clrl	%d3		|init offset reg
	moveql	#7,%d2		|init digit counter
ap_p_gd:
	bfextu	%d4{%d3:#4},%d0	|get digit
	bnes	ap_p_fx		|if non-zero, go to fix exp
	addql	#4,%d3		|point to next digit
	addql	#1,%d1		|inc digit counter
	dbf	%d2,ap_p_gd	|get next digit
ap_p_fx:
	movel	%d1,%d0		|copy counter to d2
	movel	L_SCR1(%a6),%d1	|get adjusted exp from memory
	subl	%d0,%d1		|subtract count from exp
	bges	ap_p_fm		|if still pos, go to pwrten
	negl	%d1		|now its neg; get abs
	movel	(%a0),%d4		|load lword 1 to d4
	orl	#0x40000000,%d4	| and set SE in d4
	orl	#0x40000000,(%a0)	| and in memory
|
| Calculate the mantissa multiplier to compensate for the striping of
| zeros from the mantissa.
|
ap_p_fm:
	movel	#PTENRN,%a1	|get address of power-of-ten table
	clrl	%d3		|init table index
	fmoves	FONE,%fp1	|init fp1 to 1
	moveql	#3,%d2		|init d2 to count bits in counter
ap_p_el:
	asrl	#1,%d0		|shift lsb into carry
	bccs	ap_p_en		|if 1, mul fp1 by pwrten factor
	fmulx	(%a1,%d3),%fp1	|mul by 10**(d3_bit_no)
ap_p_en:
	addl	#12,%d3		|inc d3 to next rtable entry
	tstl	%d0		|check if d0 is zero
	bnes	ap_p_el		|if not, get next bit
	fmulx	%fp1,%fp0		|mul mantissa by 10**(no_bits_shifted)
	bra	pwrten		|go calc pwrten
|
| This section handles a negative adjusted exponent.
|
ap_st_n:
	clrl	%d1		|clr counter
	moveql	#2,%d5		|set up d5 to point to lword 3
	movel	(%a0,%d5.L*4),%d4	|get lword 3
	bnes	ap_n_cl		|if not zero, check digits
	subl	#1,%d5		|dec d5 to point to lword 2
	addql	#8,%d1		|inc counter by 8
	movel	(%a0,%d5.L*4),%d4	|get lword 2
ap_n_cl:
	movel	#28,%d3		|point to last digit
	moveql	#7,%d2		|init digit counter
ap_n_gd:
	bfextu	%d4{%d3:#4},%d0	|get digit
	bnes	ap_n_fx		|if non-zero, go to exp fix
	subql	#4,%d3		|point to previous digit
	addql	#1,%d1		|inc digit counter
	dbf	%d2,ap_n_gd	|get next digit
ap_n_fx:
	movel	%d1,%d0		|copy counter to d0
	movel	L_SCR1(%a6),%d1	|get adjusted exp from memory
	subl	%d0,%d1		|subtract count from exp
	bgts	ap_n_fm		|if still pos, go fix mantissa
	negl	%d1		|take abs of exp and clr SE
	movel	(%a0),%d4		|load lword 1 to d4
	andl	#0xbfffffff,%d4	| and clr SE in d4
	andl	#0xbfffffff,(%a0)	| and in memory
|
| Calculate the mantissa multiplier to compensate for the appending of
| zeros to the mantissa.
|
ap_n_fm:
	movel	#PTENRN,%a1	|get address of power-of-ten table
	clrl	%d3		|init table index
	fmoves	FONE,%fp1	|init fp1 to 1
	moveql	#3,%d2		|init d2 to count bits in counter
ap_n_el:
	asrl	#1,%d0		|shift lsb into carry
	bccs	ap_n_en		|if 1, mul fp1 by pwrten factor
	fmulx	(%a1,%d3),%fp1	|mul by 10**(d3_bit_no)
ap_n_en:
	addl	#12,%d3		|inc d3 to next rtable entry
	tstl	%d0		|check if d0 is zero
	bnes	ap_n_el		|if not, get next bit
	fdivx	%fp1,%fp0		|div mantissa by 10**(no_bits_shifted)
|
|
| Calculate power-of-ten factor from adjusted and shifted exponent.
|
| Register usage:
|
|  pwrten:
|	(*)  d0: temp
|	( )  d1: exponent
|	(*)  d2: {FPCR[6:5],SM,SE} as index in RTABLE; temp
|	(*)  d3: FPCR work copy
|	( )  d4: first word of bcd
|	(*)  a1: RTABLE pointer
|  calc_p:
|	(*)  d0: temp
|	( )  d1: exponent
|	(*)  d3: PWRTxx table index
|	( )  a0: pointer to working copy of bcd
|	(*)  a1: PWRTxx pointer
|	(*) fp1: power-of-ten accumulator
|
| Pwrten calculates the exponent factor in the selected rounding mode
| according to the following table:
|
|	Sign of Mant  Sign of Exp  Rounding Mode  PWRTEN Rounding Mode
|
|	ANY	  ANY	RN	RN
|
|	 +	   +	RP	RP
|	 -	   +	RP	RM
|	 +	   -	RP	RM
|	 -	   -	RP	RP
|
|	 +	   +	RM	RM
|	 -	   +	RM	RP
|	 +	   -	RM	RP
|	 -	   -	RM	RM
|
|	 +	   +	RZ	RM
|	 -	   +	RZ	RM
|	 +	   -	RZ	RP
|	 -	   -	RZ	RP
|
|
pwrten:
	movel	USER_FPCR(%a6),%d3 |get user's FPCR
	bfextu	%d3{#26:#2},%d2	|isolate rounding mode bits
	movel	(%a0),%d4		|reload 1st bcd word to d4
	asll	#2,%d2		|format d2 to be
	bfextu	%d4{#0:#2},%d0	| {FPCR[6],FPCR[5],SM,SE}
	addl	%d0,%d2		|in d2 as index into RTABLE
	leal	RTABLE,%a1	|load rtable base
	moveb	(%a1,%d2),%d0	|load new rounding bits from table
	clrl	%d3			|clear d3 to force no exc and extended
	bfins	%d0,%d3{#26:#2}	|stuff new rounding bits in FPCR
	fmovel	%d3,%FPCR		|write new FPCR
	asrl	#1,%d0		|write correct PTENxx table
	bccs	not_rp		|to a1
	leal	PTENRP,%a1	|it is RP
	bras	calc_p		|go to init section
not_rp:
	asrl	#1,%d0		|keep checking
	bccs	not_rm
	leal	PTENRM,%a1	|it is RM
	bras	calc_p		|go to init section
not_rm:
	leal	PTENRN,%a1	|it is RN
calc_p:
	movel	%d1,%d0		|copy exp to d0;use d0
	bpls	no_neg		|if exp is negative,
	negl	%d0		|invert it
	orl	#0x40000000,(%a0)	|and set SE bit
no_neg:
	clrl	%d3		|table index
	fmoves	FONE,%fp1	|init fp1 to 1
e_loop:
	asrl	#1,%d0		|shift next bit into carry
	bccs	e_next		|if zero, skip the mul
	fmulx	(%a1,%d3),%fp1	|mul by 10**(d3_bit_no)
e_next:
	addl	#12,%d3		|inc d3 to next rtable entry
	tstl	%d0		|check if d0 is zero
	bnes	e_loop		|not zero, continue shifting
|
|
|  Check the sign of the adjusted exp and make the value in fp0 the
|  same sign. If the exp was pos then multiply fp1*fp0;
|  else divide fp0/fp1.
|
| Register Usage:
|  norm:
|	( )  a0: pointer to working bcd value
|	(*) fp0: mantissa accumulator
|	( ) fp1: scaling factor - 10**(abs(exp))
|
norm:
	btst	#30,(%a0)	|test the sign of the exponent
	beqs	mul		|if clear, go to multiply
div:
	fdivx	%fp1,%fp0		|exp is negative, so divide mant by exp
	bras	end_dec
mul:
	fmulx	%fp1,%fp0		|exp is positive, so multiply by exp
|
|
| Clean up and return with result in fp0.
|
| If the final mul/div in decbin incurred an inex exception,
| it will be inex2, but will be reported as inex1 by get_op.
|
end_dec:
	fmovel	%FPSR,%d0		|get status register
	bclrl	#inex2_bit+8,%d0	|test for inex2 and clear it
	fmovel	%d0,%FPSR		|return status reg w/o inex2
	beqs	no_exc		|skip this if no exc
	orl	#inx1a_mask,USER_FPSR(%a6) |set inex1/ainex
no_exc:
	moveml	(%a7)+,%d2-%d5
	rts
	|end
