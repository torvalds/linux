|
|	smovecr.sa 3.1 12/10/90
|
|	The entry point sMOVECR returns the constant at the
|	offset given in the instruction field.
|
|	Input: An offset in the instruction word.
|
|	Output:	The constant rounded to the user's rounding
|		mode unchecked for overflow.
|
|	Modified: fp0.
|
|
|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MOTOROLA
|	The copyright notice above does not evidence any
|	actual or intended publication of such source code.

|SMOVECR	idnt	2,1 | Motorola 040 Floating Point Software Package

	|section 8

#include "fpsp.h"

	|xref	nrm_set
	|xref	round
	|xref	PIRN
	|xref	PIRZRM
	|xref	PIRP
	|xref	SMALRN
	|xref	SMALRZRM
	|xref	SMALRP
	|xref	BIGRN
	|xref	BIGRZRM
	|xref	BIGRP

FZERO:	.long	00000000
|
|	FMOVECR
|
	.global	smovcr
smovcr:
	bfextu	CMDREG1B(%a6){#9:#7},%d0 |get offset
	bfextu	USER_FPCR(%a6){#26:#2},%d1 |get rmode
|
| check range of offset
|
	tstb	%d0		|if zero, offset is to pi
	beqs	PI_TBL		|it is pi
	cmpib	#0x0a,%d0		|check range $01 - $0a
	bles	Z_VAL		|if in this range, return zero
	cmpib	#0x0e,%d0		|check range $0b - $0e
	bles	SM_TBL		|valid constants in this range
	cmpib	#0x2f,%d0		|check range $10 - $2f
	bles	Z_VAL		|if in this range, return zero
	cmpib	#0x3f,%d0		|check range $30 - $3f
	ble	BG_TBL		|valid constants in this range
Z_VAL:
	fmoves	FZERO,%fp0
	rts
PI_TBL:
	tstb	%d1		|offset is zero, check for rmode
	beqs	PI_RN		|if zero, rn mode
	cmpib	#0x3,%d1		|check for rp
	beqs	PI_RP		|if 3, rp mode
PI_RZRM:
	leal	PIRZRM,%a0	|rmode is rz or rm, load PIRZRM in a0
	bra	set_finx
PI_RN:
	leal	PIRN,%a0		|rmode is rn, load PIRN in a0
	bra	set_finx
PI_RP:
	leal	PIRP,%a0		|rmode is rp, load PIRP in a0
	bra	set_finx
SM_TBL:
	subil	#0xb,%d0		|make offset in 0 - 4 range
	tstb	%d1		|check for rmode
	beqs	SM_RN		|if zero, rn mode
	cmpib	#0x3,%d1		|check for rp
	beqs	SM_RP		|if 3, rp mode
SM_RZRM:
	leal	SMALRZRM,%a0	|rmode is rz or rm, load SMRZRM in a0
	cmpib	#0x2,%d0		|check if result is inex
	ble	set_finx	|if 0 - 2, it is inexact
	bra	no_finx		|if 3, it is exact
SM_RN:
	leal	SMALRN,%a0	|rmode is rn, load SMRN in a0
	cmpib	#0x2,%d0		|check if result is inex
	ble	set_finx	|if 0 - 2, it is inexact
	bra	no_finx		|if 3, it is exact
SM_RP:
	leal	SMALRP,%a0	|rmode is rp, load SMRP in a0
	cmpib	#0x2,%d0		|check if result is inex
	ble	set_finx	|if 0 - 2, it is inexact
	bra	no_finx		|if 3, it is exact
BG_TBL:
	subil	#0x30,%d0		|make offset in 0 - f range
	tstb	%d1		|check for rmode
	beqs	BG_RN		|if zero, rn mode
	cmpib	#0x3,%d1		|check for rp
	beqs	BG_RP		|if 3, rp mode
BG_RZRM:
	leal	BIGRZRM,%a0	|rmode is rz or rm, load BGRZRM in a0
	cmpib	#0x1,%d0		|check if result is inex
	ble	set_finx	|if 0 - 1, it is inexact
	cmpib	#0x7,%d0		|second check
	ble	no_finx		|if 0 - 7, it is exact
	bra	set_finx	|if 8 - f, it is inexact
BG_RN:
	leal	BIGRN,%a0	|rmode is rn, load BGRN in a0
	cmpib	#0x1,%d0		|check if result is inex
	ble	set_finx	|if 0 - 1, it is inexact
	cmpib	#0x7,%d0		|second check
	ble	no_finx		|if 0 - 7, it is exact
	bra	set_finx	|if 8 - f, it is inexact
BG_RP:
	leal	BIGRP,%a0	|rmode is rp, load SMRP in a0
	cmpib	#0x1,%d0		|check if result is inex
	ble	set_finx	|if 0 - 1, it is inexact
	cmpib	#0x7,%d0		|second check
	ble	no_finx		|if 0 - 7, it is exact
|	bra	set_finx	;if 8 - f, it is inexact
set_finx:
	orl	#inx2a_mask,USER_FPSR(%a6) |set inex2/ainex
no_finx:
	mulul	#12,%d0			|use offset to point into tables
	movel	%d1,L_SCR1(%a6)		|load mode for round call
	bfextu	USER_FPCR(%a6){#24:#2},%d1	|get precision
	tstl	%d1			|check if extended precision
|
| Precision is extended
|
	bnes	not_ext			|if extended, do not call round
	fmovemx (%a0,%d0),%fp0-%fp0		|return result in fp0
	rts
|
| Precision is single or double
|
not_ext:
	swap	%d1			|rnd prec in upper word of d1
	addl	L_SCR1(%a6),%d1		|merge rmode in low word of d1
	movel	(%a0,%d0),FP_SCR1(%a6)	|load first word to temp storage
	movel	4(%a0,%d0),FP_SCR1+4(%a6)	|load second word
	movel	8(%a0,%d0),FP_SCR1+8(%a6)	|load third word
	clrl	%d0			|clear g,r,s
	lea	FP_SCR1(%a6),%a0
	btstb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)		|convert to internal ext. format

	bsr	round			|go round the mantissa

	bfclr	LOCAL_SGN(%a0){#0:#8}	|convert back to IEEE ext format
	beqs	fin_fcr
	bsetb	#sign_bit,LOCAL_EX(%a0)
fin_fcr:
	fmovemx (%a0),%fp0-%fp0
	rts

	|end
