|
|	sasin.sa 3.3 12/19/90
|
|	Description: The entry point sAsin computes the inverse sine of
|		an input argument; sAsind does the same except for denormalized
|		input.
|
|	Input: Double-extended number X in location pointed to
|		by address register a0.
|
|	Output: The value arcsin(X) returned in floating-point register Fp0.
|
|	Accuracy and Monotonicity: The returned result is within 3 ulps in
|		64 significant bit, i.e. within 0.5001 ulp to 53 bits if the
|		result is subsequently rounded to double precision. The
|		result is provably monotonic in double precision.
|
|	Speed: The program sASIN takes approximately 310 cycles.
|
|	Algorithm:
|
|	ASIN
|	1. If |X| >= 1, go to 3.
|
|	2. (|X| < 1) Calculate asin(X) by
|		z := sqrt( [1-X][1+X] )
|		asin(X) = atan( x / z ).
|		Exit.
|
|	3. If |X| > 1, go to 5.
|
|	4. (|X| = 1) sgn := sign(X), return asin(X) := sgn * Pi/2. Exit.
|
|	5. (|X| > 1) Generate an invalid operation by 0 * infinity.
|		Exit.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

|SASIN	idnt	2,1 | Motorola 040 Floating Point Software Package

	|section	8

PIBY2:	.long 0x3FFF0000,0xC90FDAA2,0x2168C235,0x00000000

	|xref	t_operr
	|xref	t_frcinx
	|xref	t_extdnrm
	|xref	satan

	.global	sasind
sasind:
|--ASIN(X) = X FOR DENORMALIZED X

	bra		t_extdnrm

	.global	sasin
sasin:
	fmovex		(%a0),%fp0	| ...LOAD INPUT

	movel		(%a0),%d0
	movew		4(%a0),%d0
	andil		#0x7FFFFFFF,%d0
	cmpil		#0x3FFF8000,%d0
	bges		asinbig

|--THIS IS THE USUAL CASE, |X| < 1
|--ASIN(X) = ATAN( X / SQRT( (1-X)(1+X) ) )

	fmoves		#0x3F800000,%fp1
	fsubx		%fp0,%fp1		| ...1-X
	fmovemx	%fp2-%fp2,-(%a7)
	fmoves		#0x3F800000,%fp2
	faddx		%fp0,%fp2		| ...1+X
	fmulx		%fp2,%fp1		| ...(1+X)(1-X)
	fmovemx	(%a7)+,%fp2-%fp2
	fsqrtx		%fp1		| ...SQRT([1-X][1+X])
	fdivx		%fp1,%fp0		| ...X/SQRT([1-X][1+X])
	fmovemx	%fp0-%fp0,(%a0)
	bsr		satan
	bra		t_frcinx

asinbig:
	fabsx		%fp0	 | ...|X|
	fcmps		#0x3F800000,%fp0
	fbgt		t_operr		|cause an operr exception

|--|X| = 1, ASIN(X) = +- PI/2.

	fmovex		PIBY2,%fp0
	movel		(%a0),%d0
	andil		#0x80000000,%d0	| ...SIGN BIT OF X
	oril		#0x3F800000,%d0	| ...+-1 IN SGL FORMAT
	movel		%d0,-(%sp)	| ...push SIGN(X) IN SGL-FMT
	fmovel		%d1,%FPCR
	fmuls		(%sp)+,%fp0
	bra		t_frcinx

	|end
