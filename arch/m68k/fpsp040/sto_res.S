|
|	sto_res.sa 3.1 12/10/90
|
|	Takes the result and puts it in where the user expects it.
|	Library functions return result in fp0.	If fp0 is not the
|	users destination register then fp0 is moved to the
|	correct floating-point destination register.  fp0 and fp1
|	are then restored to the original contents.
|
|	Input:	result in fp0,fp1
|
|		d2 & a0 should be kept unmodified
|
|	Output:	moves the result to the true destination reg or mem
|
|	Modifies: destination floating point register
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MOTOROLA
|	The copyright notice above does not evidence any
|	actual or intended publication of such source code.

STO_RES:	|idnt	2,1 | Motorola 040 Floating Point Software Package


	|section	8

#include "fpsp.h"

	.global	sto_cos
sto_cos:
	bfextu		CMDREG1B(%a6){#13:#3},%d0	|extract cos destination
	cmpib		#3,%d0		|check for fp0/fp1 cases
	bles		c_fp0123
	fmovemx	%fp1-%fp1,-(%a7)
	moveql		#7,%d1
	subl		%d0,%d1		|d1 = 7- (dest. reg. no.)
	clrl		%d0
	bsetl		%d1,%d0		|d0 is dynamic register mask
	fmovemx	(%a7)+,%d0
	rts
c_fp0123:
	cmpib		#0,%d0
	beqs		c_is_fp0
	cmpib		#1,%d0
	beqs		c_is_fp1
	cmpib		#2,%d0
	beqs		c_is_fp2
c_is_fp3:
	fmovemx	%fp1-%fp1,USER_FP3(%a6)
	rts
c_is_fp2:
	fmovemx	%fp1-%fp1,USER_FP2(%a6)
	rts
c_is_fp1:
	fmovemx	%fp1-%fp1,USER_FP1(%a6)
	rts
c_is_fp0:
	fmovemx	%fp1-%fp1,USER_FP0(%a6)
	rts


	.global	sto_res
sto_res:
	bfextu		CMDREG1B(%a6){#6:#3},%d0	|extract destination register
	cmpib		#3,%d0		|check for fp0/fp1 cases
	bles		fp0123
	fmovemx	%fp0-%fp0,-(%a7)
	moveql		#7,%d1
	subl		%d0,%d1		|d1 = 7- (dest. reg. no.)
	clrl		%d0
	bsetl		%d1,%d0		|d0 is dynamic register mask
	fmovemx	(%a7)+,%d0
	rts
fp0123:
	cmpib		#0,%d0
	beqs		is_fp0
	cmpib		#1,%d0
	beqs		is_fp1
	cmpib		#2,%d0
	beqs		is_fp2
is_fp3:
	fmovemx	%fp0-%fp0,USER_FP3(%a6)
	rts
is_fp2:
	fmovemx	%fp0-%fp0,USER_FP2(%a6)
	rts
is_fp1:
	fmovemx	%fp0-%fp0,USER_FP1(%a6)
	rts
is_fp0:
	fmovemx	%fp0-%fp0,USER_FP0(%a6)
	rts

	|end
