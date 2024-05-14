|
|	x_bsun.sa 3.3 7/1/91
|
|	fpsp_bsun --- FPSP handler for branch/set on unordered exception
|
|	Copy the PC to FPIAR to maintain 881/882 compatibility
|
|	The real_bsun handler will need to perform further corrective
|	measures as outlined in the 040 User's Manual on pages
|	9-41f, section 9.8.3.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

X_BSUN:	|idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

	|xref	real_bsun

	.global	fpsp_bsun
fpsp_bsun:
|
	link		%a6,#-LOCAL_SIZE
	fsave		-(%a7)
	moveml		%d0-%d1/%a0-%a1,USER_DA(%a6)
	fmovemx	%fp0-%fp3,USER_FP0(%a6)
	fmoveml	%fpcr/%fpsr/%fpiar,USER_FPCR(%a6)

|
	movel		EXC_PC(%a6),USER_FPIAR(%a6)
|
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore	(%a7)+
	unlk		%a6
	bral		real_bsun
|
	|end
