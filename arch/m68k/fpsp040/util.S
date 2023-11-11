|
|	util.sa 3.7 7/29/91
|
|	This file contains routines used by other programs.
|
|	ovf_res: used by overflow to force the correct
|		 result. ovf_r_k, ovf_r_x2, ovf_r_x3 are
|		 derivatives of this routine.
|	get_fline: get user's opcode word
|	g_dfmtou: returns the destination format.
|	g_opcls: returns the opclass of the float instruction.
|	g_rndpr: returns the rounding precision.
|	reg_dest: write byte, word, or long data to Dn
|
|
|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

|UTIL	idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

	|xref	mem_read

	.global	g_dfmtou
	.global	g_opcls
	.global	g_rndpr
	.global	get_fline
	.global	reg_dest

|
| Final result table for ovf_res. Note that the negative counterparts
| are unnecessary as ovf_res always returns the sign separately from
| the exponent.
|					;+inf
EXT_PINF:	.long	0x7fff0000,0x00000000,0x00000000,0x00000000
|					;largest +ext
EXT_PLRG:	.long	0x7ffe0000,0xffffffff,0xffffffff,0x00000000
|					;largest magnitude +sgl in ext
SGL_PLRG:	.long	0x407e0000,0xffffff00,0x00000000,0x00000000
|					;largest magnitude +dbl in ext
DBL_PLRG:	.long	0x43fe0000,0xffffffff,0xfffff800,0x00000000
|					;largest -ext

tblovfl:
	.long	EXT_RN
	.long	EXT_RZ
	.long	EXT_RM
	.long	EXT_RP
	.long	SGL_RN
	.long	SGL_RZ
	.long	SGL_RM
	.long	SGL_RP
	.long	DBL_RN
	.long	DBL_RZ
	.long	DBL_RM
	.long	DBL_RP
	.long	error
	.long	error
	.long	error
	.long	error


|
|	ovf_r_k --- overflow result calculation
|
| This entry point is used by kernel_ex.
|
| This forces the destination precision to be extended
|
| Input:	operand in ETEMP
| Output:	a result is in ETEMP (internal extended format)
|
	.global	ovf_r_k
ovf_r_k:
	lea	ETEMP(%a6),%a0	|a0 points to source operand
	bclrb	#sign_bit,ETEMP_EX(%a6)
	sne	ETEMP_SGN(%a6)	|convert to internal IEEE format

|
|	ovf_r_x2 --- overflow result calculation
|
| This entry point used by x_ovfl.  (opclass 0 and 2)
|
| Input		a0  points to an operand in the internal extended format
| Output	a0  points to the result in the internal extended format
|
| This sets the round precision according to the user's FPCR unless the
| instruction is fsgldiv or fsglmul or fsadd, fdadd, fsub, fdsub, fsmul,
| fdmul, fsdiv, fddiv, fssqrt, fsmove, fdmove, fsabs, fdabs, fsneg, fdneg.
| If the instruction is fsgldiv of fsglmul, the rounding precision must be
| extended.  If the instruction is not fsgldiv or fsglmul but a force-
| precision instruction, the rounding precision is then set to the force
| precision.

	.global	ovf_r_x2
ovf_r_x2:
	btstb	#E3,E_BYTE(%a6)		|check for nu exception
	beql	ovf_e1_exc		|it is cu exception
ovf_e3_exc:
	movew	CMDREG3B(%a6),%d0		|get the command word
	andiw	#0x00000060,%d0		|clear all bits except 6 and 5
	cmpil	#0x00000040,%d0
	beql	ovff_sgl		|force precision is single
	cmpil	#0x00000060,%d0
	beql	ovff_dbl		|force precision is double
	movew	CMDREG3B(%a6),%d0		|get the command word again
	andil	#0x7f,%d0			|clear all except operation
	cmpil	#0x33,%d0
	beql	ovf_fsgl		|fsglmul or fsgldiv
	cmpil	#0x30,%d0
	beql	ovf_fsgl
	bra	ovf_fpcr		|instruction is none of the above
|					;use FPCR
ovf_e1_exc:
	movew	CMDREG1B(%a6),%d0		|get command word
	andil	#0x00000044,%d0		|clear all bits except 6 and 2
	cmpil	#0x00000040,%d0
	beql	ovff_sgl		|the instruction is force single
	cmpil	#0x00000044,%d0
	beql	ovff_dbl		|the instruction is force double
	movew	CMDREG1B(%a6),%d0		|again get the command word
	andil	#0x0000007f,%d0		|clear all except the op code
	cmpil	#0x00000027,%d0
	beql	ovf_fsgl		|fsglmul
	cmpil	#0x00000024,%d0
	beql	ovf_fsgl		|fsgldiv
	bra	ovf_fpcr		|none of the above, use FPCR
|
|
| Inst is either fsgldiv or fsglmul.  Force extended precision.
|
ovf_fsgl:
	clrl	%d0
	bra	ovf_res

ovff_sgl:
	movel	#0x00000001,%d0		|set single
	bra	ovf_res
ovff_dbl:
	movel	#0x00000002,%d0		|set double
	bra	ovf_res
|
| The precision is in the fpcr.
|
ovf_fpcr:
	bfextu	FPCR_MODE(%a6){#0:#2},%d0 |set round precision
	bra	ovf_res

|
|
|	ovf_r_x3 --- overflow result calculation
|
| This entry point used by x_ovfl. (opclass 3 only)
|
| Input		a0  points to an operand in the internal extended format
| Output	a0  points to the result in the internal extended format
|
| This sets the round precision according to the destination size.
|
	.global	ovf_r_x3
ovf_r_x3:
	bsr	g_dfmtou	|get dest fmt in d0{1:0}
|				;for fmovout, the destination format
|				;is the rounding precision

|
|	ovf_res --- overflow result calculation
|
| Input:
|	a0	points to operand in internal extended format
| Output:
|	a0	points to result in internal extended format
|
	.global	ovf_res
ovf_res:
	lsll	#2,%d0		|move round precision to d0{3:2}
	bfextu	FPCR_MODE(%a6){#2:#2},%d1 |set round mode
	orl	%d1,%d0		|index is fmt:mode in d0{3:0}
	leal	tblovfl,%a1	|load a1 with table address
	movel	%a1@(%d0:l:4),%a1	|use d0 as index to the table
	jmp	(%a1)		|go to the correct routine
|
|case DEST_FMT = EXT
|
EXT_RN:
	leal	EXT_PINF,%a1	|answer is +/- infinity
	bsetb	#inf_bit,FPSR_CC(%a6)
	bra	set_sign	|now go set the sign
EXT_RZ:
	leal	EXT_PLRG,%a1	|answer is +/- large number
	bra	set_sign	|now go set the sign
EXT_RM:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	e_rm_pos
e_rm_neg:
	leal	EXT_PINF,%a1	|answer is negative infinity
	orl	#neginf_mask,USER_FPSR(%a6)
	bra	end_ovfr
e_rm_pos:
	leal	EXT_PLRG,%a1	|answer is large positive number
	bra	end_ovfr
EXT_RP:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	e_rp_pos
e_rp_neg:
	leal	EXT_PLRG,%a1	|answer is large negative number
	bsetb	#neg_bit,FPSR_CC(%a6)
	bra	end_ovfr
e_rp_pos:
	leal	EXT_PINF,%a1	|answer is positive infinity
	bsetb	#inf_bit,FPSR_CC(%a6)
	bra	end_ovfr
|
|case DEST_FMT = DBL
|
DBL_RN:
	leal	EXT_PINF,%a1	|answer is +/- infinity
	bsetb	#inf_bit,FPSR_CC(%a6)
	bra	set_sign
DBL_RZ:
	leal	DBL_PLRG,%a1	|answer is +/- large number
	bra	set_sign	|now go set the sign
DBL_RM:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	d_rm_pos
d_rm_neg:
	leal	EXT_PINF,%a1	|answer is negative infinity
	orl	#neginf_mask,USER_FPSR(%a6)
	bra	end_ovfr	|inf is same for all precisions (ext,dbl,sgl)
d_rm_pos:
	leal	DBL_PLRG,%a1	|answer is large positive number
	bra	end_ovfr
DBL_RP:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	d_rp_pos
d_rp_neg:
	leal	DBL_PLRG,%a1	|answer is large negative number
	bsetb	#neg_bit,FPSR_CC(%a6)
	bra	end_ovfr
d_rp_pos:
	leal	EXT_PINF,%a1	|answer is positive infinity
	bsetb	#inf_bit,FPSR_CC(%a6)
	bra	end_ovfr
|
|case DEST_FMT = SGL
|
SGL_RN:
	leal	EXT_PINF,%a1	|answer is +/-  infinity
	bsetb	#inf_bit,FPSR_CC(%a6)
	bras	set_sign
SGL_RZ:
	leal	SGL_PLRG,%a1	|answer is +/- large number
	bras	set_sign
SGL_RM:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	s_rm_pos
s_rm_neg:
	leal	EXT_PINF,%a1	|answer is negative infinity
	orl	#neginf_mask,USER_FPSR(%a6)
	bras	end_ovfr
s_rm_pos:
	leal	SGL_PLRG,%a1	|answer is large positive number
	bras	end_ovfr
SGL_RP:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	s_rp_pos
s_rp_neg:
	leal	SGL_PLRG,%a1	|answer is large negative number
	bsetb	#neg_bit,FPSR_CC(%a6)
	bras	end_ovfr
s_rp_pos:
	leal	EXT_PINF,%a1	|answer is positive infinity
	bsetb	#inf_bit,FPSR_CC(%a6)
	bras	end_ovfr

set_sign:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	end_ovfr
neg_sign:
	bsetb	#neg_bit,FPSR_CC(%a6)

end_ovfr:
	movew	LOCAL_EX(%a1),LOCAL_EX(%a0) |do not overwrite sign
	movel	LOCAL_HI(%a1),LOCAL_HI(%a0)
	movel	LOCAL_LO(%a1),LOCAL_LO(%a0)
	rts


|
|	ERROR
|
error:
	rts
|
|	get_fline --- get f-line opcode of interrupted instruction
|
|	Returns opcode in the low word of d0.
|
get_fline:
	movel	USER_FPIAR(%a6),%a0	|opcode address
	movel	#0,-(%a7)	|reserve a word on the stack
	leal	2(%a7),%a1	|point to low word of temporary
	movel	#2,%d0		|count
	bsrl	mem_read
	movel	(%a7)+,%d0
	rts
|
|	g_rndpr --- put rounding precision in d0{1:0}
|
|	valid return codes are:
|		00 - extended
|		01 - single
|		10 - double
|
| begin
| get rounding precision (cmdreg3b{6:5})
| begin
|  case	opclass = 011 (move out)
|	get destination format - this is the also the rounding precision
|
|  case	opclass = 0x0
|	if E3
|	    *case RndPr(from cmdreg3b{6:5} = 11  then RND_PREC = DBL
|	    *case RndPr(from cmdreg3b{6:5} = 10  then RND_PREC = SGL
|	     case RndPr(from cmdreg3b{6:5} = 00 | 01
|		use precision from FPCR{7:6}
|			case 00 then RND_PREC = EXT
|			case 01 then RND_PREC = SGL
|			case 10 then RND_PREC = DBL
|	else E1
|	     use precision in FPCR{7:6}
|	     case 00 then RND_PREC = EXT
|	     case 01 then RND_PREC = SGL
|	     case 10 then RND_PREC = DBL
| end
|
g_rndpr:
	bsr	g_opcls		|get opclass in d0{2:0}
	cmpw	#0x0003,%d0	|check for opclass 011
	bnes	op_0x0

|
| For move out instructions (opclass 011) the destination format
| is the same as the rounding precision.  Pass results from g_dfmtou.
|
	bsr	g_dfmtou
	rts
op_0x0:
	btstb	#E3,E_BYTE(%a6)
	beql	unf_e1_exc	|branch to e1 underflow
unf_e3_exc:
	movel	CMDREG3B(%a6),%d0	|rounding precision in d0{10:9}
	bfextu	%d0{#9:#2},%d0	|move the rounding prec bits to d0{1:0}
	cmpil	#0x2,%d0
	beql	unff_sgl	|force precision is single
	cmpil	#0x3,%d0		|force precision is double
	beql	unff_dbl
	movew	CMDREG3B(%a6),%d0	|get the command word again
	andil	#0x7f,%d0		|clear all except operation
	cmpil	#0x33,%d0
	beql	unf_fsgl	|fsglmul or fsgldiv
	cmpil	#0x30,%d0
	beql	unf_fsgl	|fsgldiv or fsglmul
	bra	unf_fpcr
unf_e1_exc:
	movel	CMDREG1B(%a6),%d0	|get 32 bits off the stack, 1st 16 bits
|				;are the command word
	andil	#0x00440000,%d0	|clear all bits except bits 6 and 2
	cmpil	#0x00400000,%d0
	beql	unff_sgl	|force single
	cmpil	#0x00440000,%d0	|force double
	beql	unff_dbl
	movel	CMDREG1B(%a6),%d0	|get the command word again
	andil	#0x007f0000,%d0	|clear all bits except the operation
	cmpil	#0x00270000,%d0
	beql	unf_fsgl	|fsglmul
	cmpil	#0x00240000,%d0
	beql	unf_fsgl	|fsgldiv
	bra	unf_fpcr

|
| Convert to return format.  The values from cmdreg3b and the return
| values are:
|	cmdreg3b	return	     precision
|	--------	------	     ---------
|	  00,01		  0		ext
|	   10		  1		sgl
|	   11		  2		dbl
| Force single
|
unff_sgl:
	movel	#1,%d0		|return 1
	rts
|
| Force double
|
unff_dbl:
	movel	#2,%d0		|return 2
	rts
|
| Force extended
|
unf_fsgl:
	movel	#0,%d0
	rts
|
| Get rounding precision set in FPCR{7:6}.
|
unf_fpcr:
	movel	USER_FPCR(%a6),%d0 |rounding precision bits in d0{7:6}
	bfextu	%d0{#24:#2},%d0	|move the rounding prec bits to d0{1:0}
	rts
|
|	g_opcls --- put opclass in d0{2:0}
|
g_opcls:
	btstb	#E3,E_BYTE(%a6)
	beqs	opc_1b		|if set, go to cmdreg1b
opc_3b:
	clrl	%d0		|if E3, only opclass 0x0 is possible
	rts
opc_1b:
	movel	CMDREG1B(%a6),%d0
	bfextu	%d0{#0:#3},%d0	|shift opclass bits d0{31:29} to d0{2:0}
	rts
|
|	g_dfmtou --- put destination format in d0{1:0}
|
|	If E1, the format is from cmdreg1b{12:10}
|	If E3, the format is extended.
|
|	Dest. Fmt.
|		extended  010 -> 00
|		single    001 -> 01
|		double    101 -> 10
|
g_dfmtou:
	btstb	#E3,E_BYTE(%a6)
	beqs	op011
	clrl	%d0		|if E1, size is always ext
	rts
op011:
	movel	CMDREG1B(%a6),%d0
	bfextu	%d0{#3:#3},%d0	|dest fmt from cmdreg1b{12:10}
	cmpb	#1,%d0		|check for single
	bnes	not_sgl
	movel	#1,%d0
	rts
not_sgl:
	cmpb	#5,%d0		|check for double
	bnes	not_dbl
	movel	#2,%d0
	rts
not_dbl:
	clrl	%d0		|must be extended
	rts

|
|
| Final result table for unf_sub. Note that the negative counterparts
| are unnecessary as unf_sub always returns the sign separately from
| the exponent.
|					;+zero
EXT_PZRO:	.long	0x00000000,0x00000000,0x00000000,0x00000000
|					;+zero
SGL_PZRO:	.long	0x3f810000,0x00000000,0x00000000,0x00000000
|					;+zero
DBL_PZRO:	.long	0x3c010000,0x00000000,0x00000000,0x00000000
|					;smallest +ext denorm
EXT_PSML:	.long	0x00000000,0x00000000,0x00000001,0x00000000
|					;smallest +sgl denorm
SGL_PSML:	.long	0x3f810000,0x00000100,0x00000000,0x00000000
|					;smallest +dbl denorm
DBL_PSML:	.long	0x3c010000,0x00000000,0x00000800,0x00000000
|
|	UNF_SUB --- underflow result calculation
|
| Input:
|	d0	contains round precision
|	a0	points to input operand in the internal extended format
|
| Output:
|	a0	points to correct internal extended precision result.
|

tblunf:
	.long	uEXT_RN
	.long	uEXT_RZ
	.long	uEXT_RM
	.long	uEXT_RP
	.long	uSGL_RN
	.long	uSGL_RZ
	.long	uSGL_RM
	.long	uSGL_RP
	.long	uDBL_RN
	.long	uDBL_RZ
	.long	uDBL_RM
	.long	uDBL_RP
	.long	uDBL_RN
	.long	uDBL_RZ
	.long	uDBL_RM
	.long	uDBL_RP

	.global	unf_sub
unf_sub:
	lsll	#2,%d0		|move round precision to d0{3:2}
	bfextu	FPCR_MODE(%a6){#2:#2},%d1 |set round mode
	orl	%d1,%d0		|index is fmt:mode in d0{3:0}
	leal	tblunf,%a1	|load a1 with table address
	movel	%a1@(%d0:l:4),%a1	|use d0 as index to the table
	jmp	(%a1)		|go to the correct routine
|
|case DEST_FMT = EXT
|
uEXT_RN:
	leal	EXT_PZRO,%a1	|answer is +/- zero
	bsetb	#z_bit,FPSR_CC(%a6)
	bra	uset_sign	|now go set the sign
uEXT_RZ:
	leal	EXT_PZRO,%a1	|answer is +/- zero
	bsetb	#z_bit,FPSR_CC(%a6)
	bra	uset_sign	|now go set the sign
uEXT_RM:
	tstb	LOCAL_SGN(%a0)	|if negative underflow
	beqs	ue_rm_pos
ue_rm_neg:
	leal	EXT_PSML,%a1	|answer is negative smallest denorm
	bsetb	#neg_bit,FPSR_CC(%a6)
	bra	end_unfr
ue_rm_pos:
	leal	EXT_PZRO,%a1	|answer is positive zero
	bsetb	#z_bit,FPSR_CC(%a6)
	bra	end_unfr
uEXT_RP:
	tstb	LOCAL_SGN(%a0)	|if negative underflow
	beqs	ue_rp_pos
ue_rp_neg:
	leal	EXT_PZRO,%a1	|answer is negative zero
	oril	#negz_mask,USER_FPSR(%a6)
	bra	end_unfr
ue_rp_pos:
	leal	EXT_PSML,%a1	|answer is positive smallest denorm
	bra	end_unfr
|
|case DEST_FMT = DBL
|
uDBL_RN:
	leal	DBL_PZRO,%a1	|answer is +/- zero
	bsetb	#z_bit,FPSR_CC(%a6)
	bra	uset_sign
uDBL_RZ:
	leal	DBL_PZRO,%a1	|answer is +/- zero
	bsetb	#z_bit,FPSR_CC(%a6)
	bra	uset_sign	|now go set the sign
uDBL_RM:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	ud_rm_pos
ud_rm_neg:
	leal	DBL_PSML,%a1	|answer is smallest denormalized negative
	bsetb	#neg_bit,FPSR_CC(%a6)
	bra	end_unfr
ud_rm_pos:
	leal	DBL_PZRO,%a1	|answer is positive zero
	bsetb	#z_bit,FPSR_CC(%a6)
	bra	end_unfr
uDBL_RP:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	ud_rp_pos
ud_rp_neg:
	leal	DBL_PZRO,%a1	|answer is negative zero
	oril	#negz_mask,USER_FPSR(%a6)
	bra	end_unfr
ud_rp_pos:
	leal	DBL_PSML,%a1	|answer is smallest denormalized negative
	bra	end_unfr
|
|case DEST_FMT = SGL
|
uSGL_RN:
	leal	SGL_PZRO,%a1	|answer is +/- zero
	bsetb	#z_bit,FPSR_CC(%a6)
	bras	uset_sign
uSGL_RZ:
	leal	SGL_PZRO,%a1	|answer is +/- zero
	bsetb	#z_bit,FPSR_CC(%a6)
	bras	uset_sign
uSGL_RM:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	us_rm_pos
us_rm_neg:
	leal	SGL_PSML,%a1	|answer is smallest denormalized negative
	bsetb	#neg_bit,FPSR_CC(%a6)
	bras	end_unfr
us_rm_pos:
	leal	SGL_PZRO,%a1	|answer is positive zero
	bsetb	#z_bit,FPSR_CC(%a6)
	bras	end_unfr
uSGL_RP:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	us_rp_pos
us_rp_neg:
	leal	SGL_PZRO,%a1	|answer is negative zero
	oril	#negz_mask,USER_FPSR(%a6)
	bras	end_unfr
us_rp_pos:
	leal	SGL_PSML,%a1	|answer is smallest denormalized positive
	bras	end_unfr

uset_sign:
	tstb	LOCAL_SGN(%a0)	|if negative overflow
	beqs	end_unfr
uneg_sign:
	bsetb	#neg_bit,FPSR_CC(%a6)

end_unfr:
	movew	LOCAL_EX(%a1),LOCAL_EX(%a0) |be careful not to overwrite sign
	movel	LOCAL_HI(%a1),LOCAL_HI(%a0)
	movel	LOCAL_LO(%a1),LOCAL_LO(%a0)
	rts
|
|	reg_dest --- write byte, word, or long data to Dn
|
|
| Input:
|	L_SCR1: Data
|	d1:     data size and dest register number formatted as:
|
|	32		5    4     3     2     1     0
|       -----------------------------------------------
|       |        0        |    Size   |  Dest Reg #   |
|       -----------------------------------------------
|
|	Size is:
|		0 - Byte
|		1 - Word
|		2 - Long/Single
|
pregdst:
	.long	byte_d0
	.long	byte_d1
	.long	byte_d2
	.long	byte_d3
	.long	byte_d4
	.long	byte_d5
	.long	byte_d6
	.long	byte_d7
	.long	word_d0
	.long	word_d1
	.long	word_d2
	.long	word_d3
	.long	word_d4
	.long	word_d5
	.long	word_d6
	.long	word_d7
	.long	long_d0
	.long	long_d1
	.long	long_d2
	.long	long_d3
	.long	long_d4
	.long	long_d5
	.long	long_d6
	.long	long_d7

reg_dest:
	leal	pregdst,%a0
	movel	%a0@(%d1:l:4),%a0
	jmp	(%a0)

byte_d0:
	moveb	L_SCR1(%a6),USER_D0+3(%a6)
	rts
byte_d1:
	moveb	L_SCR1(%a6),USER_D1+3(%a6)
	rts
byte_d2:
	moveb	L_SCR1(%a6),%d2
	rts
byte_d3:
	moveb	L_SCR1(%a6),%d3
	rts
byte_d4:
	moveb	L_SCR1(%a6),%d4
	rts
byte_d5:
	moveb	L_SCR1(%a6),%d5
	rts
byte_d6:
	moveb	L_SCR1(%a6),%d6
	rts
byte_d7:
	moveb	L_SCR1(%a6),%d7
	rts
word_d0:
	movew	L_SCR1(%a6),USER_D0+2(%a6)
	rts
word_d1:
	movew	L_SCR1(%a6),USER_D1+2(%a6)
	rts
word_d2:
	movew	L_SCR1(%a6),%d2
	rts
word_d3:
	movew	L_SCR1(%a6),%d3
	rts
word_d4:
	movew	L_SCR1(%a6),%d4
	rts
word_d5:
	movew	L_SCR1(%a6),%d5
	rts
word_d6:
	movew	L_SCR1(%a6),%d6
	rts
word_d7:
	movew	L_SCR1(%a6),%d7
	rts
long_d0:
	movel	L_SCR1(%a6),USER_D0(%a6)
	rts
long_d1:
	movel	L_SCR1(%a6),USER_D1(%a6)
	rts
long_d2:
	movel	L_SCR1(%a6),%d2
	rts
long_d3:
	movel	L_SCR1(%a6),%d3
	rts
long_d4:
	movel	L_SCR1(%a6),%d4
	rts
long_d5:
	movel	L_SCR1(%a6),%d5
	rts
long_d6:
	movel	L_SCR1(%a6),%d6
	rts
long_d7:
	movel	L_SCR1(%a6),%d7
	rts
	|end
