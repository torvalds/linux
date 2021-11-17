|
|	scosh.sa 3.1 12/10/90
|
|	The entry point sCosh computes the hyperbolic cosine of
|	an input argument; sCoshd does the same except for denormalized
|	input.
|
|	Input: Double-extended number X in location pointed to
|		by address register a0.
|
|	Output: The value cosh(X) returned in floating-point register Fp0.
|
|	Accuracy and Monotonicity: The returned result is within 3 ulps in
|		64 significant bit, i.e. within 0.5001 ulp to 53 bits if the
|		result is subsequently rounded to double precision. The
|		result is provably monotonic in double precision.
|
|	Speed: The program sCOSH takes approximately 250 cycles.
|
|	Algorithm:
|
|	COSH
|	1. If |X| > 16380 log2, go to 3.
|
|	2. (|X| <= 16380 log2) Cosh(X) is obtained by the formulae
|		y = |X|, z = exp(Y), and
|		cosh(X) = (1/2)*( z + 1/z ).
|		Exit.
|
|	3. (|X| > 16380 log2). If |X| > 16480 log2, go to 5.
|
|	4. (16380 log2 < |X| <= 16480 log2)
|		cosh(X) = sign(X) * exp(|X|)/2.
|		However, invoking exp(|X|) may cause premature overflow.
|		Thus, we calculate sinh(X) as follows:
|		Y	:= |X|
|		Fact	:=	2**(16380)
|		Y'	:= Y - 16381 log2
|		cosh(X) := Fact * exp(Y').
|		Exit.
|
|	5. (|X| > 16480 log2) sinh(X) must overflow. Return
|		Huge*Huge to generate overflow and an infinity with
|		the appropriate sign. Huge is the largest finite number in
|		extended format. Exit.
|
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

|SCOSH	idnt	2,1 | Motorola 040 Floating Point Software Package

	|section	8

	|xref	t_ovfl
	|xref	t_frcinx
	|xref	setox

T1:	.long 0x40C62D38,0xD3D64634 | ... 16381 LOG2 LEAD
T2:	.long 0x3D6F90AE,0xB1E75CC7 | ... 16381 LOG2 TRAIL

TWO16380: .long 0x7FFB0000,0x80000000,0x00000000,0x00000000

	.global	scoshd
scoshd:
|--COSH(X) = 1 FOR DENORMALIZED X

	fmoves		#0x3F800000,%fp0

	fmovel		%d1,%FPCR
	fadds		#0x00800000,%fp0
	bra		t_frcinx

	.global	scosh
scosh:
	fmovex		(%a0),%fp0	| ...LOAD INPUT

	movel		(%a0),%d0
	movew		4(%a0),%d0
	andil		#0x7FFFFFFF,%d0
	cmpil		#0x400CB167,%d0
	bgts		COSHBIG

|--THIS IS THE USUAL CASE, |X| < 16380 LOG2
|--COSH(X) = (1/2) * ( EXP(X) + 1/EXP(X) )

	fabsx		%fp0		| ...|X|

	movel		%d1,-(%sp)
	clrl		%d1
	fmovemx	%fp0-%fp0,(%a0)	|pass parameter to setox
	bsr		setox		| ...FP0 IS EXP(|X|)
	fmuls		#0x3F000000,%fp0	| ...(1/2)EXP(|X|)
	movel		(%sp)+,%d1

	fmoves		#0x3E800000,%fp1	| ...(1/4)
	fdivx		%fp0,%fp1		| ...1/(2 EXP(|X|))

	fmovel		%d1,%FPCR
	faddx		%fp1,%fp0

	bra		t_frcinx

COSHBIG:
	cmpil		#0x400CB2B3,%d0
	bgts		COSHHUGE

	fabsx		%fp0
	fsubd		T1(%pc),%fp0		| ...(|X|-16381LOG2_LEAD)
	fsubd		T2(%pc),%fp0		| ...|X| - 16381 LOG2, ACCURATE

	movel		%d1,-(%sp)
	clrl		%d1
	fmovemx	%fp0-%fp0,(%a0)
	bsr		setox
	fmovel		(%sp)+,%fpcr

	fmulx		TWO16380(%pc),%fp0
	bra		t_frcinx

COSHHUGE:
	fmovel		#0,%fpsr		|clr N bit if set by source
	bclrb		#7,(%a0)		|always return positive value
	fmovemx	(%a0),%fp0-%fp0
	bra		t_ovfl

	|end
