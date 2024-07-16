|
|	scale.sa 3.3 7/30/91
|
|	The entry point sSCALE computes the destination operand
|	scaled by the source operand.  If the absolute value of
|	the source operand is (>= 2^14) an overflow or underflow
|	is returned.
|
|	The entry point sscale is called from do_func to emulate
|	the fscale unimplemented instruction.
|
|	Input: Double-extended destination operand in FPTEMP,
|		double-extended source operand in ETEMP.
|
|	Output: The function returns scale(X,Y) to fp0.
|
|	Modifies: fp0.
|
|	Algorithm:
|
|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

|SCALE    idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

	|xref	t_ovfl2
	|xref	t_unfl
	|xref	round
	|xref	t_resdnrm

SRC_BNDS: .short	0x3fff,0x400c

|
| This entry point is used by the unimplemented instruction exception
| handler.
|
|
|
|	FSCALE
|
	.global	sscale
sscale:
	fmovel		#0,%fpcr		|clr user enabled exc
	clrl		%d1
	movew		FPTEMP(%a6),%d1	|get dest exponent
	smi		L_SCR1(%a6)	|use L_SCR1 to hold sign
	andil		#0x7fff,%d1	|strip sign
	movew		ETEMP(%a6),%d0	|check src bounds
	andiw		#0x7fff,%d0	|clr sign bit
	cmp2w		SRC_BNDS,%d0
	bccs		src_in
	cmpiw		#0x400c,%d0	|test for too large
	bge		src_out
|
| The source input is below 1, so we check for denormalized numbers
| and set unfl.
|
src_small:
	moveb		DTAG(%a6),%d0
	andib		#0xe0,%d0
	tstb		%d0
	beqs		no_denorm
	st		STORE_FLG(%a6)	|dest already contains result
	orl		#unfl_mask,USER_FPSR(%a6) |set UNFL
den_done:
	leal		FPTEMP(%a6),%a0
	bra		t_resdnrm
no_denorm:
	fmovel		USER_FPCR(%a6),%FPCR
	fmovex		FPTEMP(%a6),%fp0	|simply return dest
	rts


|
| Source is within 2^14 range.  To perform the int operation,
| move it to d0.
|
src_in:
	fmovex		ETEMP(%a6),%fp0	|move in src for int
	fmovel		#rz_mode,%fpcr	|force rz for src conversion
	fmovel		%fp0,%d0		|int src to d0
	fmovel		#0,%FPSR		|clr status from above
	tstw		ETEMP(%a6)	|check src sign
	blt		src_neg
|
| Source is positive.  Add the src to the dest exponent.
| The result can be denormalized, if src = 0, or overflow,
| if the result of the add sets a bit in the upper word.
|
src_pos:
	tstw		%d1		|check for denorm
	beq		dst_dnrm
	addl		%d0,%d1		|add src to dest exp
	beqs		denorm		|if zero, result is denorm
	cmpil		#0x7fff,%d1	|test for overflow
	bges		ovfl
	tstb		L_SCR1(%a6)
	beqs		spos_pos
	orw		#0x8000,%d1
spos_pos:
	movew		%d1,FPTEMP(%a6)	|result in FPTEMP
	fmovel		USER_FPCR(%a6),%FPCR
	fmovex		FPTEMP(%a6),%fp0	|write result to fp0
	rts
ovfl:
	tstb		L_SCR1(%a6)
	beqs		sovl_pos
	orw		#0x8000,%d1
sovl_pos:
	movew		FPTEMP(%a6),ETEMP(%a6)	|result in ETEMP
	movel		FPTEMP_HI(%a6),ETEMP_HI(%a6)
	movel		FPTEMP_LO(%a6),ETEMP_LO(%a6)
	bra		t_ovfl2

denorm:
	tstb		L_SCR1(%a6)
	beqs		den_pos
	orw		#0x8000,%d1
den_pos:
	tstl		FPTEMP_HI(%a6)	|check j bit
	blts		nden_exit	|if set, not denorm
	movew		%d1,ETEMP(%a6)	|input expected in ETEMP
	movel		FPTEMP_HI(%a6),ETEMP_HI(%a6)
	movel		FPTEMP_LO(%a6),ETEMP_LO(%a6)
	orl		#unfl_bit,USER_FPSR(%a6)	|set unfl
	leal		ETEMP(%a6),%a0
	bra		t_resdnrm
nden_exit:
	movew		%d1,FPTEMP(%a6)	|result in FPTEMP
	fmovel		USER_FPCR(%a6),%FPCR
	fmovex		FPTEMP(%a6),%fp0	|write result to fp0
	rts

|
| Source is negative.  Add the src to the dest exponent.
| (The result exponent will be reduced).  The result can be
| denormalized.
|
src_neg:
	addl		%d0,%d1		|add src to dest
	beqs		denorm		|if zero, result is denorm
	blts		fix_dnrm	|if negative, result is
|					;needing denormalization
	tstb		L_SCR1(%a6)
	beqs		sneg_pos
	orw		#0x8000,%d1
sneg_pos:
	movew		%d1,FPTEMP(%a6)	|result in FPTEMP
	fmovel		USER_FPCR(%a6),%FPCR
	fmovex		FPTEMP(%a6),%fp0	|write result to fp0
	rts


|
| The result exponent is below denorm value.  Test for catastrophic
| underflow and force zero if true.  If not, try to shift the
| mantissa right until a zero exponent exists.
|
fix_dnrm:
	cmpiw		#0xffc0,%d1	|lower bound for normalization
	blt		fix_unfl	|if lower, catastrophic unfl
	movew		%d1,%d0		|use d0 for exp
	movel		%d2,-(%a7)	|free d2 for norm
	movel		FPTEMP_HI(%a6),%d1
	movel		FPTEMP_LO(%a6),%d2
	clrl		L_SCR2(%a6)
fix_loop:
	addw		#1,%d0		|drive d0 to 0
	lsrl		#1,%d1		|while shifting the
	roxrl		#1,%d2		|mantissa to the right
	bccs		no_carry
	st		L_SCR2(%a6)	|use L_SCR2 to capture inex
no_carry:
	tstw		%d0		|it is finished when
	blts		fix_loop	|d0 is zero or the mantissa
	tstb		L_SCR2(%a6)
	beqs		tst_zero
	orl		#unfl_inx_mask,USER_FPSR(%a6)
|					;set unfl, aunfl, ainex
|
| Test for zero. If zero, simply use fmove to return +/- zero
| to the fpu.
|
tst_zero:
	clrw		FPTEMP_EX(%a6)
	tstb		L_SCR1(%a6)	|test for sign
	beqs		tst_con
	orw		#0x8000,FPTEMP_EX(%a6) |set sign bit
tst_con:
	movel		%d1,FPTEMP_HI(%a6)
	movel		%d2,FPTEMP_LO(%a6)
	movel		(%a7)+,%d2
	tstl		%d1
	bnes		not_zero
	tstl		FPTEMP_LO(%a6)
	bnes		not_zero
|
| Result is zero.  Check for rounding mode to set lsb.  If the
| mode is rp, and the zero is positive, return smallest denorm.
| If the mode is rm, and the zero is negative, return smallest
| negative denorm.
|
	btstb		#5,FPCR_MODE(%a6) |test if rm or rp
	beqs		no_dir
	btstb		#4,FPCR_MODE(%a6) |check which one
	beqs		zer_rm
zer_rp:
	tstb		L_SCR1(%a6)	|check sign
	bnes		no_dir		|if set, neg op, no inc
	movel		#1,FPTEMP_LO(%a6) |set lsb
	bras		sm_dnrm
zer_rm:
	tstb		L_SCR1(%a6)	|check sign
	beqs		no_dir		|if clr, neg op, no inc
	movel		#1,FPTEMP_LO(%a6) |set lsb
	orl		#neg_mask,USER_FPSR(%a6) |set N
	bras		sm_dnrm
no_dir:
	fmovel		USER_FPCR(%a6),%FPCR
	fmovex		FPTEMP(%a6),%fp0	|use fmove to set cc's
	rts

|
| The rounding mode changed the zero to a smallest denorm. Call
| t_resdnrm with exceptional operand in ETEMP.
|
sm_dnrm:
	movel		FPTEMP_EX(%a6),ETEMP_EX(%a6)
	movel		FPTEMP_HI(%a6),ETEMP_HI(%a6)
	movel		FPTEMP_LO(%a6),ETEMP_LO(%a6)
	leal		ETEMP(%a6),%a0
	bra		t_resdnrm

|
| Result is still denormalized.
|
not_zero:
	orl		#unfl_mask,USER_FPSR(%a6) |set unfl
	tstb		L_SCR1(%a6)	|check for sign
	beqs		fix_exit
	orl		#neg_mask,USER_FPSR(%a6) |set N
fix_exit:
	bras		sm_dnrm


|
| The result has underflowed to zero. Return zero and set
| unfl, aunfl, and ainex.
|
fix_unfl:
	orl		#unfl_inx_mask,USER_FPSR(%a6)
	btstb		#5,FPCR_MODE(%a6) |test if rm or rp
	beqs		no_dir2
	btstb		#4,FPCR_MODE(%a6) |check which one
	beqs		zer_rm2
zer_rp2:
	tstb		L_SCR1(%a6)	|check sign
	bnes		no_dir2		|if set, neg op, no inc
	clrl		FPTEMP_EX(%a6)
	clrl		FPTEMP_HI(%a6)
	movel		#1,FPTEMP_LO(%a6) |set lsb
	bras		sm_dnrm		|return smallest denorm
zer_rm2:
	tstb		L_SCR1(%a6)	|check sign
	beqs		no_dir2		|if clr, neg op, no inc
	movew		#0x8000,FPTEMP_EX(%a6)
	clrl		FPTEMP_HI(%a6)
	movel		#1,FPTEMP_LO(%a6) |set lsb
	orl		#neg_mask,USER_FPSR(%a6) |set N
	bra		sm_dnrm		|return smallest denorm

no_dir2:
	tstb		L_SCR1(%a6)
	bges		pos_zero
neg_zero:
	clrl		FP_SCR1(%a6)	|clear the exceptional operand
	clrl		FP_SCR1+4(%a6)	|for gen_except.
	clrl		FP_SCR1+8(%a6)
	fmoves		#0x80000000,%fp0
	rts
pos_zero:
	clrl		FP_SCR1(%a6)	|clear the exceptional operand
	clrl		FP_SCR1+4(%a6)	|for gen_except.
	clrl		FP_SCR1+8(%a6)
	fmoves		#0x00000000,%fp0
	rts

|
| The destination is a denormalized number.  It must be handled
| by first shifting the bits in the mantissa until it is normalized,
| then adding the remainder of the source to the exponent.
|
dst_dnrm:
	moveml		%d2/%d3,-(%a7)
	movew		FPTEMP_EX(%a6),%d1
	movel		FPTEMP_HI(%a6),%d2
	movel		FPTEMP_LO(%a6),%d3
dst_loop:
	tstl		%d2		|test for normalized result
	blts		dst_norm	|exit loop if so
	tstl		%d0		|otherwise, test shift count
	beqs		dst_fin		|if zero, shifting is done
	subil		#1,%d0		|dec src
	lsll		#1,%d3
	roxll		#1,%d2
	bras		dst_loop
|
| Destination became normalized.  Simply add the remaining
| portion of the src to the exponent.
|
dst_norm:
	addw		%d0,%d1		|dst is normalized; add src
	tstb		L_SCR1(%a6)
	beqs		dnrm_pos
	orl		#0x8000,%d1
dnrm_pos:
	movemw		%d1,FPTEMP_EX(%a6)
	moveml		%d2,FPTEMP_HI(%a6)
	moveml		%d3,FPTEMP_LO(%a6)
	fmovel		USER_FPCR(%a6),%FPCR
	fmovex		FPTEMP(%a6),%fp0
	moveml		(%a7)+,%d2/%d3
	rts

|
| Destination remained denormalized.  Call t_excdnrm with
| exceptional operand in ETEMP.
|
dst_fin:
	tstb		L_SCR1(%a6)	|check for sign
	beqs		dst_exit
	orl		#neg_mask,USER_FPSR(%a6) |set N
	orl		#0x8000,%d1
dst_exit:
	movemw		%d1,ETEMP_EX(%a6)
	moveml		%d2,ETEMP_HI(%a6)
	moveml		%d3,ETEMP_LO(%a6)
	orl		#unfl_mask,USER_FPSR(%a6) |set unfl
	moveml		(%a7)+,%d2/%d3
	leal		ETEMP(%a6),%a0
	bra		t_resdnrm

|
| Source is outside of 2^14 range.  Test the sign and branch
| to the appropriate exception handler.
|
src_out:
	tstb		L_SCR1(%a6)
	beqs		scro_pos
	orl		#0x8000,%d1
scro_pos:
	movel		FPTEMP_HI(%a6),ETEMP_HI(%a6)
	movel		FPTEMP_LO(%a6),ETEMP_LO(%a6)
	tstw		ETEMP(%a6)
	blts		res_neg
res_pos:
	movew		%d1,ETEMP(%a6)	|result in ETEMP
	bra		t_ovfl2
res_neg:
	movew		%d1,ETEMP(%a6)	|result in ETEMP
	leal		ETEMP(%a6),%a0
	bra		t_unfl
	|end
