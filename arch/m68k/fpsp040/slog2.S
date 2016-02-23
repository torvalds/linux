|
|	slog2.sa 3.1 12/10/90
|
|       The entry point slog10 computes the base-10
|	logarithm of an input argument X.
|	slog10d does the same except the input value is a
|	denormalized number.
|	sLog2 and sLog2d are the base-2 analogues.
|
|       INPUT:	Double-extended value in memory location pointed to
|		by address register a0.
|
|       OUTPUT: log_10(X) or log_2(X) returned in floating-point
|		register fp0.
|
|       ACCURACY and MONOTONICITY: The returned result is within 1.7
|		ulps in 64 significant bit, i.e. within 0.5003 ulp
|		to 53 bits if the result is subsequently rounded
|		to double precision. The result is provably monotonic
|		in double precision.
|
|       SPEED:	Two timings are measured, both in the copy-back mode.
|		The first one is measured when the function is invoked
|		the first time (so the instructions and data are not
|		in cache), and the second one is measured when the
|		function is reinvoked at the same input argument.
|
|       ALGORITHM and IMPLEMENTATION NOTES:
|
|       slog10d:
|
|       Step 0.   If X < 0, create a NaN and raise the invalid operation
|                 flag. Otherwise, save FPCR in D1; set FpCR to default.
|       Notes:    Default means round-to-nearest mode, no floating-point
|                 traps, and precision control = double extended.
|
|       Step 1.   Call slognd to obtain Y = log(X), the natural log of X.
|       Notes:    Even if X is denormalized, log(X) is always normalized.
|
|       Step 2.   Compute log_10(X) = log(X) * (1/log(10)).
|            2.1  Restore the user FPCR
|            2.2  Return ans := Y * INV_L10.
|
|
|       slog10:
|
|       Step 0.   If X < 0, create a NaN and raise the invalid operation
|                 flag. Otherwise, save FPCR in D1; set FpCR to default.
|       Notes:    Default means round-to-nearest mode, no floating-point
|                 traps, and precision control = double extended.
|
|       Step 1.   Call sLogN to obtain Y = log(X), the natural log of X.
|
|       Step 2.   Compute log_10(X) = log(X) * (1/log(10)).
|            2.1  Restore the user FPCR
|            2.2  Return ans := Y * INV_L10.
|
|
|       sLog2d:
|
|       Step 0.   If X < 0, create a NaN and raise the invalid operation
|                 flag. Otherwise, save FPCR in D1; set FpCR to default.
|       Notes:    Default means round-to-nearest mode, no floating-point
|                 traps, and precision control = double extended.
|
|       Step 1.   Call slognd to obtain Y = log(X), the natural log of X.
|       Notes:    Even if X is denormalized, log(X) is always normalized.
|
|       Step 2.   Compute log_10(X) = log(X) * (1/log(2)).
|            2.1  Restore the user FPCR
|            2.2  Return ans := Y * INV_L2.
|
|
|       sLog2:
|
|       Step 0.   If X < 0, create a NaN and raise the invalid operation
|                 flag. Otherwise, save FPCR in D1; set FpCR to default.
|       Notes:    Default means round-to-nearest mode, no floating-point
|                 traps, and precision control = double extended.
|
|       Step 1.   If X is not an integer power of two, i.e., X != 2^k,
|                 go to Step 3.
|
|       Step 2.   Return k.
|            2.1  Get integer k, X = 2^k.
|            2.2  Restore the user FPCR.
|            2.3  Return ans := convert-to-double-extended(k).
|
|       Step 3.   Call sLogN to obtain Y = log(X), the natural log of X.
|
|       Step 4.   Compute log_2(X) = log(X) * (1/log(2)).
|            4.1  Restore the user FPCR
|            4.2  Return ans := Y * INV_L2.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

|SLOG2    idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

	|xref	t_frcinx
	|xref	t_operr
	|xref	slogn
	|xref	slognd

INV_L10:  .long 0x3FFD0000,0xDE5BD8A9,0x37287195,0x00000000

INV_L2:   .long 0x3FFF0000,0xB8AA3B29,0x5C17F0BC,0x00000000

	.global	slog10d
slog10d:
|--entry point for Log10(X), X is denormalized
	movel		(%a0),%d0
	blt		invalid
	movel		%d1,-(%sp)
	clrl		%d1
	bsr		slognd			| ...log(X), X denorm.
	fmovel		(%sp)+,%fpcr
	fmulx		INV_L10,%fp0
	bra		t_frcinx

	.global	slog10
slog10:
|--entry point for Log10(X), X is normalized

	movel		(%a0),%d0
	blt		invalid
	movel		%d1,-(%sp)
	clrl		%d1
	bsr		slogn			| ...log(X), X normal.
	fmovel		(%sp)+,%fpcr
	fmulx		INV_L10,%fp0
	bra		t_frcinx


	.global	slog2d
slog2d:
|--entry point for Log2(X), X is denormalized

	movel		(%a0),%d0
	blt		invalid
	movel		%d1,-(%sp)
	clrl		%d1
	bsr		slognd			| ...log(X), X denorm.
	fmovel		(%sp)+,%fpcr
	fmulx		INV_L2,%fp0
	bra		t_frcinx

	.global	slog2
slog2:
|--entry point for Log2(X), X is normalized
	movel		(%a0),%d0
	blt		invalid

	movel		8(%a0),%d0
	bnes		continue		| ...X is not 2^k

	movel		4(%a0),%d0
	andl		#0x7FFFFFFF,%d0
	tstl		%d0
	bnes		continue

|--X = 2^k.
	movew		(%a0),%d0
	andl		#0x00007FFF,%d0
	subl		#0x3FFF,%d0
	fmovel		%d1,%fpcr
	fmovel		%d0,%fp0
	bra		t_frcinx

continue:
	movel		%d1,-(%sp)
	clrl		%d1
	bsr		slogn			| ...log(X), X normal.
	fmovel		(%sp)+,%fpcr
	fmulx		INV_L2,%fp0
	bra		t_frcinx

invalid:
	bra		t_operr

	|end
