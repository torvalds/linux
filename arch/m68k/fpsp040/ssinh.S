|
|	ssinh.sa 3.1 12/10/90
|
|       The entry point sSinh computes the hyperbolic sine of
|       an input argument; sSinhd does the same except for denormalized
|       input.
|
|       Input: Double-extended number X in location pointed to
|		by address register a0.
|
|       Output: The value sinh(X) returned in floating-point register Fp0.
|
|       Accuracy and Monotonicity: The returned result is within 3 ulps in
|               64 significant bit, i.e. within 0.5001 ulp to 53 bits if the
|               result is subsequently rounded to double precision. The
|               result is provably monotonic in double precision.
|
|       Speed: The program sSINH takes approximately 280 cycles.
|
|       Algorithm:
|
|       SINH
|       1. If |X| > 16380 log2, go to 3.
|
|       2. (|X| <= 16380 log2) Sinh(X) is obtained by the formulae
|               y = |X|, sgn = sign(X), and z = expm1(Y),
|               sinh(X) = sgn*(1/2)*( z + z/(1+z) ).
|          Exit.
|
|       3. If |X| > 16480 log2, go to 5.
|
|       4. (16380 log2 < |X| <= 16480 log2)
|               sinh(X) = sign(X) * exp(|X|)/2.
|          However, invoking exp(|X|) may cause premature overflow.
|          Thus, we calculate sinh(X) as follows:
|             Y       := |X|
|             sgn     := sign(X)
|             sgnFact := sgn * 2**(16380)
|             Y'      := Y - 16381 log2
|             sinh(X) := sgnFact * exp(Y').
|          Exit.
|
|       5. (|X| > 16480 log2) sinh(X) must overflow. Return
|          sign(X)*Huge*Huge to generate overflow and an infinity with
|          the appropriate sign. Huge is the largest finite number in
|          extended format. Exit.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MOTOROLA
|	The copyright notice above does not evidence any
|	actual or intended publication of such source code.

|SSINH	idnt	2,1 | Motorola 040 Floating Point Software Package

	|section	8

T1:	.long 0x40C62D38,0xD3D64634 | ... 16381 LOG2 LEAD
T2:	.long 0x3D6F90AE,0xB1E75CC7 | ... 16381 LOG2 TRAIL

	|xref	t_frcinx
	|xref	t_ovfl
	|xref	t_extdnrm
	|xref	setox
	|xref	setoxm1

	.global	ssinhd
ssinhd:
|--SINH(X) = X FOR DENORMALIZED X

	bra	t_extdnrm

	.global	ssinh
ssinh:
	fmovex	(%a0),%fp0	| ...LOAD INPUT

	movel	(%a0),%d0
	movew	4(%a0),%d0
	movel	%d0,%a1		| save a copy of original (compacted) operand
	andl	#0x7FFFFFFF,%d0
	cmpl	#0x400CB167,%d0
	bgts	SINHBIG

|--THIS IS THE USUAL CASE, |X| < 16380 LOG2
|--Y = |X|, Z = EXPM1(Y), SINH(X) = SIGN(X)*(1/2)*( Z + Z/(1+Z) )

	fabsx	%fp0		| ...Y = |X|

	moveml	%a1/%d1,-(%sp)
	fmovemx %fp0-%fp0,(%a0)
	clrl	%d1
	bsr	setoxm1		| ...FP0 IS Z = EXPM1(Y)
	fmovel	#0,%fpcr
	moveml	(%sp)+,%a1/%d1

	fmovex	%fp0,%fp1
	fadds	#0x3F800000,%fp1	| ...1+Z
	fmovex	%fp0,-(%sp)
	fdivx	%fp1,%fp0		| ...Z/(1+Z)
	movel	%a1,%d0
	andl	#0x80000000,%d0
	orl	#0x3F000000,%d0
	faddx	(%sp)+,%fp0
	movel	%d0,-(%sp)

	fmovel	%d1,%fpcr
	fmuls	(%sp)+,%fp0	|last fp inst - possible exceptions set

	bra	t_frcinx

SINHBIG:
	cmpl	#0x400CB2B3,%d0
	bgt	t_ovfl
	fabsx	%fp0
	fsubd	T1(%pc),%fp0	| ...(|X|-16381LOG2_LEAD)
	movel	#0,-(%sp)
	movel	#0x80000000,-(%sp)
	movel	%a1,%d0
	andl	#0x80000000,%d0
	orl	#0x7FFB0000,%d0
	movel	%d0,-(%sp)	| ...EXTENDED FMT
	fsubd	T2(%pc),%fp0	| ...|X| - 16381 LOG2, ACCURATE

	movel	%d1,-(%sp)
	clrl	%d1
	fmovemx %fp0-%fp0,(%a0)
	bsr	setox
	fmovel	(%sp)+,%fpcr

	fmulx	(%sp)+,%fp0	|possible exception
	bra	t_frcinx

	|end
