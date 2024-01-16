|
|	sacos.sa 3.3 12/19/90
|
|	Description: The entry point sAcos computes the inverse cosine of
|		an input argument; sAcosd does the same except for denormalized
|		input.
|
|	Input: Double-extended number X in location pointed to
|		by address register a0.
|
|	Output: The value arccos(X) returned in floating-point register Fp0.
|
|	Accuracy and Monotonicity: The returned result is within 3 ulps in
|		64 significant bit, i.e. within 0.5001 ulp to 53 bits if the
|		result is subsequently rounded to double precision. The
|		result is provably monotonic in double precision.
|
|	Speed: The program sCOS takes approximately 310 cycles.
|
|	Algorithm:
|
|	ACOS
|	1. If |X| >= 1, go to 3.
|
|	2. (|X| < 1) Calculate acos(X) by
|		z := (1-X) / (1+X)
|		acos(X) = 2 * atan( sqrt(z) ).
|		Exit.
|
|	3. If |X| > 1, go to 5.
|
|	4. (|X| = 1) If X > 0, return 0. Otherwise, return Pi. Exit.
|
|	5. (|X| > 1) Generate an invalid operation by 0 * infinity.
|		Exit.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

|SACOS	idnt	2,1 | Motorola 040 Floating Point Software Package

	|section	8

PI:	.long 0x40000000,0xC90FDAA2,0x2168C235,0x00000000
PIBY2:	.long 0x3FFF0000,0xC90FDAA2,0x2168C235,0x00000000

	|xref	t_operr
	|xref	t_frcinx
	|xref	satan

	.global	sacosd
sacosd:
|--ACOS(X) = PI/2 FOR DENORMALIZED X
	fmovel		%d1,%fpcr		| ...load user's rounding mode/precision
	fmovex		PIBY2,%fp0
	bra		t_frcinx

	.global	sacos
sacos:
	fmovex		(%a0),%fp0	| ...LOAD INPUT

	movel		(%a0),%d0		| ...pack exponent with upper 16 fraction
	movew		4(%a0),%d0
	andil		#0x7FFFFFFF,%d0
	cmpil		#0x3FFF8000,%d0
	bges		ACOSBIG

|--THIS IS THE USUAL CASE, |X| < 1
|--ACOS(X) = 2 * ATAN(	SQRT( (1-X)/(1+X) )	)

	fmoves		#0x3F800000,%fp1
	faddx		%fp0,%fp1		| ...1+X
	fnegx		%fp0		| ... -X
	fadds		#0x3F800000,%fp0	| ...1-X
	fdivx		%fp1,%fp0		| ...(1-X)/(1+X)
	fsqrtx		%fp0		| ...SQRT((1-X)/(1+X))
	fmovemx	%fp0-%fp0,(%a0)	| ...overwrite input
	movel		%d1,-(%sp)	|save original users fpcr
	clrl		%d1
	bsr		satan		| ...ATAN(SQRT([1-X]/[1+X]))
	fmovel		(%sp)+,%fpcr	|restore users exceptions
	faddx		%fp0,%fp0		| ...2 * ATAN( STUFF )
	bra		t_frcinx

ACOSBIG:
	fabsx		%fp0
	fcmps		#0x3F800000,%fp0
	fbgt		t_operr		|cause an operr exception

|--|X| = 1, ACOS(X) = 0 OR PI
	movel		(%a0),%d0		| ...pack exponent with upper 16 fraction
	movew		4(%a0),%d0
	cmpl		#0,%d0		|D0 has original exponent+fraction
	bgts		ACOSP1

|--X = -1
|Returns PI and inexact exception
	fmovex		PI,%fp0
	fmovel		%d1,%FPCR
	fadds		#0x00800000,%fp0	|cause an inexact exception to be put
|					;into the 040 - will not trap until next
|					;fp inst.
	bra		t_frcinx

ACOSP1:
	fmovel		%d1,%FPCR
	fmoves		#0x00000000,%fp0
	rts				|Facos ; of +1 is exact

	|end
