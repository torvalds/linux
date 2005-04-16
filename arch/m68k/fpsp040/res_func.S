|
|	res_func.sa 3.9 7/29/91
|
| Normalizes denormalized numbers if necessary and updates the
| stack frame.  The function is then restored back into the
| machine and the 040 completes the operation.  This routine
| is only used by the unsupported data type/format handler.
| (Exception vector 55).
|
| For packed move out (fmove.p fpm,<ea>) the operation is
| completed here; data is packed and moved to user memory.
| The stack is restored to the 040 only in the case of a
| reportable exception in the conversion.
|
|
|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MOTOROLA
|	The copyright notice above does not evidence any
|	actual or intended publication of such source code.

RES_FUNC:    |idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

sp_bnds:	.short	0x3f81,0x407e
		.short	0x3f6a,0x0000
dp_bnds:	.short	0x3c01,0x43fe
		.short	0x3bcd,0x0000

	|xref	mem_write
	|xref	bindec
	|xref	get_fline
	|xref	round
	|xref	denorm
	|xref	dest_ext
	|xref	dest_dbl
	|xref	dest_sgl
	|xref	unf_sub
	|xref	nrm_set
	|xref	dnrm_lp
	|xref	ovf_res
	|xref	reg_dest
	|xref	t_ovfl
	|xref	t_unfl

	.global	res_func
	.global	p_move

res_func:
	clrb	DNRM_FLG(%a6)
	clrb	RES_FLG(%a6)
	clrb	CU_ONLY(%a6)
	tstb	DY_MO_FLG(%a6)
	beqs	monadic
dyadic:
	btstb	#7,DTAG(%a6)	|if dop = norm=000, zero=001,
|				;inf=010 or nan=011
	beqs	monadic		|then branch
|				;else denorm
| HANDLE DESTINATION DENORM HERE
|				;set dtag to norm
|				;write the tag & fpte15 to the fstack
	leal	FPTEMP(%a6),%a0

	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)

	bsr	nrm_set		|normalize number (exp will go negative)
	bclrb	#sign_bit,LOCAL_EX(%a0) |get rid of false sign
	bfclr	LOCAL_SGN(%a0){#0:#8}	|change back to IEEE ext format
	beqs	dpos
	bsetb	#sign_bit,LOCAL_EX(%a0)
dpos:
	bfclr	DTAG(%a6){#0:#4}	|set tag to normalized, FPTE15 = 0
	bsetb	#4,DTAG(%a6)	|set FPTE15
	orb	#0x0f,DNRM_FLG(%a6)
monadic:
	leal	ETEMP(%a6),%a0
	btstb	#direction_bit,CMDREG1B(%a6)	|check direction
	bne	opclass3			|it is a mv out
|
| At this point, only opclass 0 and 2 possible
|
	btstb	#7,STAG(%a6)	|if sop = norm=000, zero=001,
|				;inf=010 or nan=011
	bne	mon_dnrm	|else denorm
	tstb	DY_MO_FLG(%a6)	|all cases of dyadic instructions would
	bne	normal		|require normalization of denorm

| At this point:
|	monadic instructions:	fabs  = $18  fneg   = $1a  ftst   = $3a
|				fmove = $00  fsmove = $40  fdmove = $44
|				fsqrt = $05* fssqrt = $41  fdsqrt = $45
|				(*fsqrt reencoded to $05)
|
	movew	CMDREG1B(%a6),%d0	|get command register
	andil	#0x7f,%d0			|strip to only command word
|
| At this point, fabs, fneg, fsmove, fdmove, ftst, fsqrt, fssqrt, and
| fdsqrt are possible.
| For cases fabs, fneg, fsmove, and fdmove goto spos (do not normalize)
| For cases fsqrt, fssqrt, and fdsqrt goto nrm_src (do normalize)
|
	btstl	#0,%d0
	bne	normal			|weed out fsqrt instructions
|
| cu_norm handles fmove in instructions with normalized inputs.
| The routine round is used to correctly round the input for the
| destination precision and mode.
|
cu_norm:
	st	CU_ONLY(%a6)		|set cu-only inst flag
	movew	CMDREG1B(%a6),%d0
	andib	#0x3b,%d0		|isolate bits to select inst
	tstb	%d0
	beql	cu_nmove	|if zero, it is an fmove
	cmpib	#0x18,%d0
	beql	cu_nabs		|if $18, it is fabs
	cmpib	#0x1a,%d0
	beql	cu_nneg		|if $1a, it is fneg
|
| Inst is ftst.  Check the source operand and set the cc's accordingly.
| No write is done, so simply rts.
|
cu_ntst:
	movew	LOCAL_EX(%a0),%d0
	bclrl	#15,%d0
	sne	LOCAL_SGN(%a0)
	beqs	cu_ntpo
	orl	#neg_mask,USER_FPSR(%a6) |set N
cu_ntpo:
	cmpiw	#0x7fff,%d0	|test for inf/nan
	bnes	cu_ntcz
	tstl	LOCAL_HI(%a0)
	bnes	cu_ntn
	tstl	LOCAL_LO(%a0)
	bnes	cu_ntn
	orl	#inf_mask,USER_FPSR(%a6)
	rts
cu_ntn:
	orl	#nan_mask,USER_FPSR(%a6)
	movel	ETEMP_EX(%a6),FPTEMP_EX(%a6)	|set up fptemp sign for
|						;snan handler

	rts
cu_ntcz:
	tstl	LOCAL_HI(%a0)
	bnel	cu_ntsx
	tstl	LOCAL_LO(%a0)
	bnel	cu_ntsx
	orl	#z_mask,USER_FPSR(%a6)
cu_ntsx:
	rts
|
| Inst is fabs.  Execute the absolute value function on the input.
| Branch to the fmove code.  If the operand is NaN, do nothing.
|
cu_nabs:
	moveb	STAG(%a6),%d0
	btstl	#5,%d0			|test for NaN or zero
	bne	wr_etemp		|if either, simply write it
	bclrb	#7,LOCAL_EX(%a0)		|do abs
	bras	cu_nmove		|fmove code will finish
|
| Inst is fneg.  Execute the negate value function on the input.
| Fall though to the fmove code.  If the operand is NaN, do nothing.
|
cu_nneg:
	moveb	STAG(%a6),%d0
	btstl	#5,%d0			|test for NaN or zero
	bne	wr_etemp		|if either, simply write it
	bchgb	#7,LOCAL_EX(%a0)		|do neg
|
| Inst is fmove.  This code also handles all result writes.
| If bit 2 is set, round is forced to double.  If it is clear,
| and bit 6 is set, round is forced to single.  If both are clear,
| the round precision is found in the fpcr.  If the rounding precision
| is double or single, round the result before the write.
|
cu_nmove:
	moveb	STAG(%a6),%d0
	andib	#0xe0,%d0			|isolate stag bits
	bne	wr_etemp		|if not norm, simply write it
	btstb	#2,CMDREG1B+1(%a6)	|check for rd
	bne	cu_nmrd
	btstb	#6,CMDREG1B+1(%a6)	|check for rs
	bne	cu_nmrs
|
| The move or operation is not with forced precision.  Test for
| nan or inf as the input; if so, simply write it to FPn.  Use the
| FPCR_MODE byte to get rounding on norms and zeros.
|
cu_nmnr:
	bfextu	FPCR_MODE(%a6){#0:#2},%d0
	tstb	%d0			|check for extended
	beq	cu_wrexn		|if so, just write result
	cmpib	#1,%d0			|check for single
	beq	cu_nmrs			|fall through to double
|
| The move is fdmove or round precision is double.
|
cu_nmrd:
	movel	#2,%d0			|set up the size for denorm
	movew	LOCAL_EX(%a0),%d1		|compare exponent to double threshold
	andw	#0x7fff,%d1
	cmpw	#0x3c01,%d1
	bls	cu_nunfl
	bfextu	FPCR_MODE(%a6){#2:#2},%d1	|get rmode
	orl	#0x00020000,%d1		|or in rprec (double)
	clrl	%d0			|clear g,r,s for round
	bclrb	#sign_bit,LOCAL_EX(%a0)	|convert to internal format
	sne	LOCAL_SGN(%a0)
	bsrl	round
	bfclr	LOCAL_SGN(%a0){#0:#8}
	beqs	cu_nmrdc
	bsetb	#sign_bit,LOCAL_EX(%a0)
cu_nmrdc:
	movew	LOCAL_EX(%a0),%d1		|check for overflow
	andw	#0x7fff,%d1
	cmpw	#0x43ff,%d1
	bge	cu_novfl		|take care of overflow case
	bra	cu_wrexn
|
| The move is fsmove or round precision is single.
|
cu_nmrs:
	movel	#1,%d0
	movew	LOCAL_EX(%a0),%d1
	andw	#0x7fff,%d1
	cmpw	#0x3f81,%d1
	bls	cu_nunfl
	bfextu	FPCR_MODE(%a6){#2:#2},%d1
	orl	#0x00010000,%d1
	clrl	%d0
	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)
	bsrl	round
	bfclr	LOCAL_SGN(%a0){#0:#8}
	beqs	cu_nmrsc
	bsetb	#sign_bit,LOCAL_EX(%a0)
cu_nmrsc:
	movew	LOCAL_EX(%a0),%d1
	andw	#0x7FFF,%d1
	cmpw	#0x407f,%d1
	blt	cu_wrexn
|
| The operand is above precision boundaries.  Use t_ovfl to
| generate the correct value.
|
cu_novfl:
	bsr	t_ovfl
	bra	cu_wrexn
|
| The operand is below precision boundaries.  Use denorm to
| generate the correct value.
|
cu_nunfl:
	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)
	bsr	denorm
	bfclr	LOCAL_SGN(%a0){#0:#8}	|change back to IEEE ext format
	beqs	cu_nucont
	bsetb	#sign_bit,LOCAL_EX(%a0)
cu_nucont:
	bfextu	FPCR_MODE(%a6){#2:#2},%d1
	btstb	#2,CMDREG1B+1(%a6)	|check for rd
	bne	inst_d
	btstb	#6,CMDREG1B+1(%a6)	|check for rs
	bne	inst_s
	swap	%d1
	moveb	FPCR_MODE(%a6),%d1
	lsrb	#6,%d1
	swap	%d1
	bra	inst_sd
inst_d:
	orl	#0x00020000,%d1
	bra	inst_sd
inst_s:
	orl	#0x00010000,%d1
inst_sd:
	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)
	bsrl	round
	bfclr	LOCAL_SGN(%a0){#0:#8}
	beqs	cu_nuflp
	bsetb	#sign_bit,LOCAL_EX(%a0)
cu_nuflp:
	btstb	#inex2_bit,FPSR_EXCEPT(%a6)
	beqs	cu_nuninx
	orl	#aunfl_mask,USER_FPSR(%a6) |if the round was inex, set AUNFL
cu_nuninx:
	tstl	LOCAL_HI(%a0)		|test for zero
	bnes	cu_nunzro
	tstl	LOCAL_LO(%a0)
	bnes	cu_nunzro
|
| The mantissa is zero from the denorm loop.  Check sign and rmode
| to see if rounding should have occurred which would leave the lsb.
|
	movel	USER_FPCR(%a6),%d0
	andil	#0x30,%d0		|isolate rmode
	cmpil	#0x20,%d0
	blts	cu_nzro
	bnes	cu_nrp
cu_nrm:
	tstw	LOCAL_EX(%a0)	|if positive, set lsb
	bges	cu_nzro
	btstb	#7,FPCR_MODE(%a6) |check for double
	beqs	cu_nincs
	bras	cu_nincd
cu_nrp:
	tstw	LOCAL_EX(%a0)	|if positive, set lsb
	blts	cu_nzro
	btstb	#7,FPCR_MODE(%a6) |check for double
	beqs	cu_nincs
cu_nincd:
	orl	#0x800,LOCAL_LO(%a0) |inc for double
	bra	cu_nunzro
cu_nincs:
	orl	#0x100,LOCAL_HI(%a0) |inc for single
	bra	cu_nunzro
cu_nzro:
	orl	#z_mask,USER_FPSR(%a6)
	moveb	STAG(%a6),%d0
	andib	#0xe0,%d0
	cmpib	#0x40,%d0		|check if input was tagged zero
	beqs	cu_numv
cu_nunzro:
	orl	#unfl_mask,USER_FPSR(%a6) |set unfl
cu_numv:
	movel	(%a0),ETEMP(%a6)
	movel	4(%a0),ETEMP_HI(%a6)
	movel	8(%a0),ETEMP_LO(%a6)
|
| Write the result to memory, setting the fpsr cc bits.  NaN and Inf
| bypass cu_wrexn.
|
cu_wrexn:
	tstw	LOCAL_EX(%a0)		|test for zero
	beqs	cu_wrzero
	cmpw	#0x8000,LOCAL_EX(%a0)	|test for zero
	bnes	cu_wreon
cu_wrzero:
	orl	#z_mask,USER_FPSR(%a6)	|set Z bit
cu_wreon:
	tstw	LOCAL_EX(%a0)
	bpl	wr_etemp
	orl	#neg_mask,USER_FPSR(%a6)
	bra	wr_etemp

|
| HANDLE SOURCE DENORM HERE
|
|				;clear denorm stag to norm
|				;write the new tag & ete15 to the fstack
mon_dnrm:
|
| At this point, check for the cases in which normalizing the
| denorm produces incorrect results.
|
	tstb	DY_MO_FLG(%a6)	|all cases of dyadic instructions would
	bnes	nrm_src		|require normalization of denorm

| At this point:
|	monadic instructions:	fabs  = $18  fneg   = $1a  ftst   = $3a
|				fmove = $00  fsmove = $40  fdmove = $44
|				fsqrt = $05* fssqrt = $41  fdsqrt = $45
|				(*fsqrt reencoded to $05)
|
	movew	CMDREG1B(%a6),%d0	|get command register
	andil	#0x7f,%d0			|strip to only command word
|
| At this point, fabs, fneg, fsmove, fdmove, ftst, fsqrt, fssqrt, and
| fdsqrt are possible.
| For cases fabs, fneg, fsmove, and fdmove goto spos (do not normalize)
| For cases fsqrt, fssqrt, and fdsqrt goto nrm_src (do normalize)
|
	btstl	#0,%d0
	bnes	nrm_src		|weed out fsqrt instructions
	st	CU_ONLY(%a6)	|set cu-only inst flag
	bra	cu_dnrm		|fmove, fabs, fneg, ftst
|				;cases go to cu_dnrm
nrm_src:
	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)
	bsr	nrm_set		|normalize number (exponent will go
|				; negative)
	bclrb	#sign_bit,LOCAL_EX(%a0) |get rid of false sign

	bfclr	LOCAL_SGN(%a0){#0:#8}	|change back to IEEE ext format
	beqs	spos
	bsetb	#sign_bit,LOCAL_EX(%a0)
spos:
	bfclr	STAG(%a6){#0:#4}	|set tag to normalized, FPTE15 = 0
	bsetb	#4,STAG(%a6)	|set ETE15
	orb	#0xf0,DNRM_FLG(%a6)
normal:
	tstb	DNRM_FLG(%a6)	|check if any of the ops were denorms
	bne	ck_wrap		|if so, check if it is a potential
|				;wrap-around case
fix_stk:
	moveb	#0xfe,CU_SAVEPC(%a6)
	bclrb	#E1,E_BYTE(%a6)

	clrw	NMNEXC(%a6)

	st	RES_FLG(%a6)	|indicate that a restore is needed
	rts

|
| cu_dnrm handles all cu-only instructions (fmove, fabs, fneg, and
| ftst) completely in software without an frestore to the 040.
|
cu_dnrm:
	st	CU_ONLY(%a6)
	movew	CMDREG1B(%a6),%d0
	andib	#0x3b,%d0		|isolate bits to select inst
	tstb	%d0
	beql	cu_dmove	|if zero, it is an fmove
	cmpib	#0x18,%d0
	beql	cu_dabs		|if $18, it is fabs
	cmpib	#0x1a,%d0
	beql	cu_dneg		|if $1a, it is fneg
|
| Inst is ftst.  Check the source operand and set the cc's accordingly.
| No write is done, so simply rts.
|
cu_dtst:
	movew	LOCAL_EX(%a0),%d0
	bclrl	#15,%d0
	sne	LOCAL_SGN(%a0)
	beqs	cu_dtpo
	orl	#neg_mask,USER_FPSR(%a6) |set N
cu_dtpo:
	cmpiw	#0x7fff,%d0	|test for inf/nan
	bnes	cu_dtcz
	tstl	LOCAL_HI(%a0)
	bnes	cu_dtn
	tstl	LOCAL_LO(%a0)
	bnes	cu_dtn
	orl	#inf_mask,USER_FPSR(%a6)
	rts
cu_dtn:
	orl	#nan_mask,USER_FPSR(%a6)
	movel	ETEMP_EX(%a6),FPTEMP_EX(%a6)	|set up fptemp sign for
|						;snan handler
	rts
cu_dtcz:
	tstl	LOCAL_HI(%a0)
	bnel	cu_dtsx
	tstl	LOCAL_LO(%a0)
	bnel	cu_dtsx
	orl	#z_mask,USER_FPSR(%a6)
cu_dtsx:
	rts
|
| Inst is fabs.  Execute the absolute value function on the input.
| Branch to the fmove code.
|
cu_dabs:
	bclrb	#7,LOCAL_EX(%a0)		|do abs
	bras	cu_dmove		|fmove code will finish
|
| Inst is fneg.  Execute the negate value function on the input.
| Fall though to the fmove code.
|
cu_dneg:
	bchgb	#7,LOCAL_EX(%a0)		|do neg
|
| Inst is fmove.  This code also handles all result writes.
| If bit 2 is set, round is forced to double.  If it is clear,
| and bit 6 is set, round is forced to single.  If both are clear,
| the round precision is found in the fpcr.  If the rounding precision
| is double or single, the result is zero, and the mode is checked
| to determine if the lsb of the result should be set.
|
cu_dmove:
	btstb	#2,CMDREG1B+1(%a6)	|check for rd
	bne	cu_dmrd
	btstb	#6,CMDREG1B+1(%a6)	|check for rs
	bne	cu_dmrs
|
| The move or operation is not with forced precision.  Use the
| FPCR_MODE byte to get rounding.
|
cu_dmnr:
	bfextu	FPCR_MODE(%a6){#0:#2},%d0
	tstb	%d0			|check for extended
	beq	cu_wrexd		|if so, just write result
	cmpib	#1,%d0			|check for single
	beq	cu_dmrs			|fall through to double
|
| The move is fdmove or round precision is double.  Result is zero.
| Check rmode for rp or rm and set lsb accordingly.
|
cu_dmrd:
	bfextu	FPCR_MODE(%a6){#2:#2},%d1	|get rmode
	tstw	LOCAL_EX(%a0)		|check sign
	blts	cu_dmdn
	cmpib	#3,%d1			|check for rp
	bne	cu_dpd			|load double pos zero
	bra	cu_dpdr			|load double pos zero w/lsb
cu_dmdn:
	cmpib	#2,%d1			|check for rm
	bne	cu_dnd			|load double neg zero
	bra	cu_dndr			|load double neg zero w/lsb
|
| The move is fsmove or round precision is single.  Result is zero.
| Check for rp or rm and set lsb accordingly.
|
cu_dmrs:
	bfextu	FPCR_MODE(%a6){#2:#2},%d1	|get rmode
	tstw	LOCAL_EX(%a0)		|check sign
	blts	cu_dmsn
	cmpib	#3,%d1			|check for rp
	bne	cu_spd			|load single pos zero
	bra	cu_spdr			|load single pos zero w/lsb
cu_dmsn:
	cmpib	#2,%d1			|check for rm
	bne	cu_snd			|load single neg zero
	bra	cu_sndr			|load single neg zero w/lsb
|
| The precision is extended, so the result in etemp is correct.
| Simply set unfl (not inex2 or aunfl) and write the result to
| the correct fp register.
cu_wrexd:
	orl	#unfl_mask,USER_FPSR(%a6)
	tstw	LOCAL_EX(%a0)
	beq	wr_etemp
	orl	#neg_mask,USER_FPSR(%a6)
	bra	wr_etemp
|
| These routines write +/- zero in double format.  The routines
| cu_dpdr and cu_dndr set the double lsb.
|
cu_dpd:
	movel	#0x3c010000,LOCAL_EX(%a0)	|force pos double zero
	clrl	LOCAL_HI(%a0)
	clrl	LOCAL_LO(%a0)
	orl	#z_mask,USER_FPSR(%a6)
	orl	#unfinx_mask,USER_FPSR(%a6)
	bra	wr_etemp
cu_dpdr:
	movel	#0x3c010000,LOCAL_EX(%a0)	|force pos double zero
	clrl	LOCAL_HI(%a0)
	movel	#0x800,LOCAL_LO(%a0)	|with lsb set
	orl	#unfinx_mask,USER_FPSR(%a6)
	bra	wr_etemp
cu_dnd:
	movel	#0xbc010000,LOCAL_EX(%a0)	|force pos double zero
	clrl	LOCAL_HI(%a0)
	clrl	LOCAL_LO(%a0)
	orl	#z_mask,USER_FPSR(%a6)
	orl	#neg_mask,USER_FPSR(%a6)
	orl	#unfinx_mask,USER_FPSR(%a6)
	bra	wr_etemp
cu_dndr:
	movel	#0xbc010000,LOCAL_EX(%a0)	|force pos double zero
	clrl	LOCAL_HI(%a0)
	movel	#0x800,LOCAL_LO(%a0)	|with lsb set
	orl	#neg_mask,USER_FPSR(%a6)
	orl	#unfinx_mask,USER_FPSR(%a6)
	bra	wr_etemp
|
| These routines write +/- zero in single format.  The routines
| cu_dpdr and cu_dndr set the single lsb.
|
cu_spd:
	movel	#0x3f810000,LOCAL_EX(%a0)	|force pos single zero
	clrl	LOCAL_HI(%a0)
	clrl	LOCAL_LO(%a0)
	orl	#z_mask,USER_FPSR(%a6)
	orl	#unfinx_mask,USER_FPSR(%a6)
	bra	wr_etemp
cu_spdr:
	movel	#0x3f810000,LOCAL_EX(%a0)	|force pos single zero
	movel	#0x100,LOCAL_HI(%a0)	|with lsb set
	clrl	LOCAL_LO(%a0)
	orl	#unfinx_mask,USER_FPSR(%a6)
	bra	wr_etemp
cu_snd:
	movel	#0xbf810000,LOCAL_EX(%a0)	|force pos single zero
	clrl	LOCAL_HI(%a0)
	clrl	LOCAL_LO(%a0)
	orl	#z_mask,USER_FPSR(%a6)
	orl	#neg_mask,USER_FPSR(%a6)
	orl	#unfinx_mask,USER_FPSR(%a6)
	bra	wr_etemp
cu_sndr:
	movel	#0xbf810000,LOCAL_EX(%a0)	|force pos single zero
	movel	#0x100,LOCAL_HI(%a0)	|with lsb set
	clrl	LOCAL_LO(%a0)
	orl	#neg_mask,USER_FPSR(%a6)
	orl	#unfinx_mask,USER_FPSR(%a6)
	bra	wr_etemp

|
| This code checks for 16-bit overflow conditions on dyadic
| operations which are not restorable into the floating-point
| unit and must be completed in software.  Basically, this
| condition exists with a very large norm and a denorm.  One
| of the operands must be denormalized to enter this code.
|
| Flags used:
|	DY_MO_FLG contains 0 for monadic op, $ff for dyadic
|	DNRM_FLG contains $00 for neither op denormalized
|	                  $0f for the destination op denormalized
|	                  $f0 for the source op denormalized
|	                  $ff for both ops denormalized
|
| The wrap-around condition occurs for add, sub, div, and cmp
| when
|
|	abs(dest_exp - src_exp) >= $8000
|
| and for mul when
|
|	(dest_exp + src_exp) < $0
|
| we must process the operation here if this case is true.
|
| The rts following the frcfpn routine is the exit from res_func
| for this condition.  The restore flag (RES_FLG) is left clear.
| No frestore is done unless an exception is to be reported.
|
| For fadd:
|	if(sign_of(dest) != sign_of(src))
|		replace exponent of src with $3fff (keep sign)
|		use fpu to perform dest+new_src (user's rmode and X)
|		clr sticky
|	else
|		set sticky
|	call round with user's precision and mode
|	move result to fpn and wbtemp
|
| For fsub:
|	if(sign_of(dest) == sign_of(src))
|		replace exponent of src with $3fff (keep sign)
|		use fpu to perform dest+new_src (user's rmode and X)
|		clr sticky
|	else
|		set sticky
|	call round with user's precision and mode
|	move result to fpn and wbtemp
|
| For fdiv/fsgldiv:
|	if(both operands are denorm)
|		restore_to_fpu;
|	if(dest is norm)
|		force_ovf;
|	else(dest is denorm)
|		force_unf:
|
| For fcmp:
|	if(dest is norm)
|		N = sign_of(dest);
|	else(dest is denorm)
|		N = sign_of(src);
|
| For fmul:
|	if(both operands are denorm)
|		force_unf;
|	if((dest_exp + src_exp) < 0)
|		force_unf:
|	else
|		restore_to_fpu;
|
| local equates:
	.set	addcode,0x22
	.set	subcode,0x28
	.set	mulcode,0x23
	.set	divcode,0x20
	.set	cmpcode,0x38
ck_wrap:
	| tstb	DY_MO_FLG(%a6)	;check for fsqrt
	beq	fix_stk		|if zero, it is fsqrt
	movew	CMDREG1B(%a6),%d0
	andiw	#0x3b,%d0		|strip to command bits
	cmpiw	#addcode,%d0
	beq	wrap_add
	cmpiw	#subcode,%d0
	beq	wrap_sub
	cmpiw	#mulcode,%d0
	beq	wrap_mul
	cmpiw	#cmpcode,%d0
	beq	wrap_cmp
|
| Inst is fdiv.
|
wrap_div:
	cmpb	#0xff,DNRM_FLG(%a6) |if both ops denorm,
	beq	fix_stk		 |restore to fpu
|
| One of the ops is denormalized.  Test for wrap condition
| and force the result.
|
	cmpb	#0x0f,DNRM_FLG(%a6) |check for dest denorm
	bnes	div_srcd
div_destd:
	bsrl	ckinf_ns
	bne	fix_stk
	bfextu	ETEMP_EX(%a6){#1:#15},%d0	|get src exp (always pos)
	bfexts	FPTEMP_EX(%a6){#1:#15},%d1	|get dest exp (always neg)
	subl	%d1,%d0			|subtract dest from src
	cmpl	#0x7fff,%d0
	blt	fix_stk			|if less, not wrap case
	clrb	WBTEMP_SGN(%a6)
	movew	ETEMP_EX(%a6),%d0		|find the sign of the result
	movew	FPTEMP_EX(%a6),%d1
	eorw	%d1,%d0
	andiw	#0x8000,%d0
	beq	force_unf
	st	WBTEMP_SGN(%a6)
	bra	force_unf

ckinf_ns:
	moveb	STAG(%a6),%d0		|check source tag for inf or nan
	bra	ck_in_com
ckinf_nd:
	moveb	DTAG(%a6),%d0		|check destination tag for inf or nan
ck_in_com:
	andib	#0x60,%d0			|isolate tag bits
	cmpb	#0x40,%d0			|is it inf?
	beq	nan_or_inf		|not wrap case
	cmpb	#0x60,%d0			|is it nan?
	beq	nan_or_inf		|yes, not wrap case?
	cmpb	#0x20,%d0			|is it a zero?
	beq	nan_or_inf		|yes
	clrl	%d0
	rts				|then ; it is either a zero of norm,
|					;check wrap case
nan_or_inf:
	moveql	#-1,%d0
	rts



div_srcd:
	bsrl	ckinf_nd
	bne	fix_stk
	bfextu	FPTEMP_EX(%a6){#1:#15},%d0	|get dest exp (always pos)
	bfexts	ETEMP_EX(%a6){#1:#15},%d1	|get src exp (always neg)
	subl	%d1,%d0			|subtract src from dest
	cmpl	#0x8000,%d0
	blt	fix_stk			|if less, not wrap case
	clrb	WBTEMP_SGN(%a6)
	movew	ETEMP_EX(%a6),%d0		|find the sign of the result
	movew	FPTEMP_EX(%a6),%d1
	eorw	%d1,%d0
	andiw	#0x8000,%d0
	beqs	force_ovf
	st	WBTEMP_SGN(%a6)
|
| This code handles the case of the instruction resulting in
| an overflow condition.
|
force_ovf:
	bclrb	#E1,E_BYTE(%a6)
	orl	#ovfl_inx_mask,USER_FPSR(%a6)
	clrw	NMNEXC(%a6)
	leal	WBTEMP(%a6),%a0		|point a0 to memory location
	movew	CMDREG1B(%a6),%d0
	btstl	#6,%d0			|test for forced precision
	beqs	frcovf_fpcr
	btstl	#2,%d0			|check for double
	bnes	frcovf_dbl
	movel	#0x1,%d0			|inst is forced single
	bras	frcovf_rnd
frcovf_dbl:
	movel	#0x2,%d0			|inst is forced double
	bras	frcovf_rnd
frcovf_fpcr:
	bfextu	FPCR_MODE(%a6){#0:#2},%d0	|inst not forced - use fpcr prec
frcovf_rnd:

| The 881/882 does not set inex2 for the following case, so the
| line is commented out to be compatible with 881/882
|	tst.b	%d0
|	beq.b	frcovf_x
|	or.l	#inex2_mask,USER_FPSR(%a6) ;if prec is s or d, set inex2

|frcovf_x:
	bsrl	ovf_res			|get correct result based on
|					;round precision/mode.  This
|					;sets FPSR_CC correctly
|					;returns in external format
	bfclr	WBTEMP_SGN(%a6){#0:#8}
	beq	frcfpn
	bsetb	#sign_bit,WBTEMP_EX(%a6)
	bra	frcfpn
|
| Inst is fadd.
|
wrap_add:
	cmpb	#0xff,DNRM_FLG(%a6) |if both ops denorm,
	beq	fix_stk		 |restore to fpu
|
| One of the ops is denormalized.  Test for wrap condition
| and complete the instruction.
|
	cmpb	#0x0f,DNRM_FLG(%a6) |check for dest denorm
	bnes	add_srcd
add_destd:
	bsrl	ckinf_ns
	bne	fix_stk
	bfextu	ETEMP_EX(%a6){#1:#15},%d0	|get src exp (always pos)
	bfexts	FPTEMP_EX(%a6){#1:#15},%d1	|get dest exp (always neg)
	subl	%d1,%d0			|subtract dest from src
	cmpl	#0x8000,%d0
	blt	fix_stk			|if less, not wrap case
	bra	add_wrap
add_srcd:
	bsrl	ckinf_nd
	bne	fix_stk
	bfextu	FPTEMP_EX(%a6){#1:#15},%d0	|get dest exp (always pos)
	bfexts	ETEMP_EX(%a6){#1:#15},%d1	|get src exp (always neg)
	subl	%d1,%d0			|subtract src from dest
	cmpl	#0x8000,%d0
	blt	fix_stk			|if less, not wrap case
|
| Check the signs of the operands.  If they are unlike, the fpu
| can be used to add the norm and 1.0 with the sign of the
| denorm and it will correctly generate the result in extended
| precision.  We can then call round with no sticky and the result
| will be correct for the user's rounding mode and precision.  If
| the signs are the same, we call round with the sticky bit set
| and the result will be correct for the user's rounding mode and
| precision.
|
add_wrap:
	movew	ETEMP_EX(%a6),%d0
	movew	FPTEMP_EX(%a6),%d1
	eorw	%d1,%d0
	andiw	#0x8000,%d0
	beq	add_same
|
| The signs are unlike.
|
	cmpb	#0x0f,DNRM_FLG(%a6) |is dest the denorm?
	bnes	add_u_srcd
	movew	FPTEMP_EX(%a6),%d0
	andiw	#0x8000,%d0
	orw	#0x3fff,%d0	|force the exponent to +/- 1
	movew	%d0,FPTEMP_EX(%a6) |in the denorm
	movel	USER_FPCR(%a6),%d0
	andil	#0x30,%d0
	fmovel	%d0,%fpcr		|set up users rmode and X
	fmovex	ETEMP(%a6),%fp0
	faddx	FPTEMP(%a6),%fp0
	leal	WBTEMP(%a6),%a0	|point a0 to wbtemp in frame
	fmovel	%fpsr,%d1
	orl	%d1,USER_FPSR(%a6) |capture cc's and inex from fadd
	fmovex	%fp0,WBTEMP(%a6)	|write result to memory
	lsrl	#4,%d0		|put rmode in lower 2 bits
	movel	USER_FPCR(%a6),%d1
	andil	#0xc0,%d1
	lsrl	#6,%d1		|put precision in upper word
	swap	%d1
	orl	%d0,%d1		|set up for round call
	clrl	%d0		|force sticky to zero
	bclrb	#sign_bit,WBTEMP_EX(%a6)
	sne	WBTEMP_SGN(%a6)
	bsrl	round		|round result to users rmode & prec
	bfclr	WBTEMP_SGN(%a6){#0:#8}	|convert back to IEEE ext format
	beq	frcfpnr
	bsetb	#sign_bit,WBTEMP_EX(%a6)
	bra	frcfpnr
add_u_srcd:
	movew	ETEMP_EX(%a6),%d0
	andiw	#0x8000,%d0
	orw	#0x3fff,%d0	|force the exponent to +/- 1
	movew	%d0,ETEMP_EX(%a6) |in the denorm
	movel	USER_FPCR(%a6),%d0
	andil	#0x30,%d0
	fmovel	%d0,%fpcr		|set up users rmode and X
	fmovex	ETEMP(%a6),%fp0
	faddx	FPTEMP(%a6),%fp0
	fmovel	%fpsr,%d1
	orl	%d1,USER_FPSR(%a6) |capture cc's and inex from fadd
	leal	WBTEMP(%a6),%a0	|point a0 to wbtemp in frame
	fmovex	%fp0,WBTEMP(%a6)	|write result to memory
	lsrl	#4,%d0		|put rmode in lower 2 bits
	movel	USER_FPCR(%a6),%d1
	andil	#0xc0,%d1
	lsrl	#6,%d1		|put precision in upper word
	swap	%d1
	orl	%d0,%d1		|set up for round call
	clrl	%d0		|force sticky to zero
	bclrb	#sign_bit,WBTEMP_EX(%a6)
	sne	WBTEMP_SGN(%a6)	|use internal format for round
	bsrl	round		|round result to users rmode & prec
	bfclr	WBTEMP_SGN(%a6){#0:#8}	|convert back to IEEE ext format
	beq	frcfpnr
	bsetb	#sign_bit,WBTEMP_EX(%a6)
	bra	frcfpnr
|
| Signs are alike:
|
add_same:
	cmpb	#0x0f,DNRM_FLG(%a6) |is dest the denorm?
	bnes	add_s_srcd
add_s_destd:
	leal	ETEMP(%a6),%a0
	movel	USER_FPCR(%a6),%d0
	andil	#0x30,%d0
	lsrl	#4,%d0		|put rmode in lower 2 bits
	movel	USER_FPCR(%a6),%d1
	andil	#0xc0,%d1
	lsrl	#6,%d1		|put precision in upper word
	swap	%d1
	orl	%d0,%d1		|set up for round call
	movel	#0x20000000,%d0	|set sticky for round
	bclrb	#sign_bit,ETEMP_EX(%a6)
	sne	ETEMP_SGN(%a6)
	bsrl	round		|round result to users rmode & prec
	bfclr	ETEMP_SGN(%a6){#0:#8}	|convert back to IEEE ext format
	beqs	add_s_dclr
	bsetb	#sign_bit,ETEMP_EX(%a6)
add_s_dclr:
	leal	WBTEMP(%a6),%a0
	movel	ETEMP(%a6),(%a0)	|write result to wbtemp
	movel	ETEMP_HI(%a6),4(%a0)
	movel	ETEMP_LO(%a6),8(%a0)
	tstw	ETEMP_EX(%a6)
	bgt	add_ckovf
	orl	#neg_mask,USER_FPSR(%a6)
	bra	add_ckovf
add_s_srcd:
	leal	FPTEMP(%a6),%a0
	movel	USER_FPCR(%a6),%d0
	andil	#0x30,%d0
	lsrl	#4,%d0		|put rmode in lower 2 bits
	movel	USER_FPCR(%a6),%d1
	andil	#0xc0,%d1
	lsrl	#6,%d1		|put precision in upper word
	swap	%d1
	orl	%d0,%d1		|set up for round call
	movel	#0x20000000,%d0	|set sticky for round
	bclrb	#sign_bit,FPTEMP_EX(%a6)
	sne	FPTEMP_SGN(%a6)
	bsrl	round		|round result to users rmode & prec
	bfclr	FPTEMP_SGN(%a6){#0:#8}	|convert back to IEEE ext format
	beqs	add_s_sclr
	bsetb	#sign_bit,FPTEMP_EX(%a6)
add_s_sclr:
	leal	WBTEMP(%a6),%a0
	movel	FPTEMP(%a6),(%a0)	|write result to wbtemp
	movel	FPTEMP_HI(%a6),4(%a0)
	movel	FPTEMP_LO(%a6),8(%a0)
	tstw	FPTEMP_EX(%a6)
	bgt	add_ckovf
	orl	#neg_mask,USER_FPSR(%a6)
add_ckovf:
	movew	WBTEMP_EX(%a6),%d0
	andiw	#0x7fff,%d0
	cmpiw	#0x7fff,%d0
	bne	frcfpnr
|
| The result has overflowed to $7fff exponent.  Set I, ovfl,
| and aovfl, and clr the mantissa (incorrectly set by the
| round routine.)
|
	orl	#inf_mask+ovfl_inx_mask,USER_FPSR(%a6)
	clrl	4(%a0)
	bra	frcfpnr
|
| Inst is fsub.
|
wrap_sub:
	cmpb	#0xff,DNRM_FLG(%a6) |if both ops denorm,
	beq	fix_stk		 |restore to fpu
|
| One of the ops is denormalized.  Test for wrap condition
| and complete the instruction.
|
	cmpb	#0x0f,DNRM_FLG(%a6) |check for dest denorm
	bnes	sub_srcd
sub_destd:
	bsrl	ckinf_ns
	bne	fix_stk
	bfextu	ETEMP_EX(%a6){#1:#15},%d0	|get src exp (always pos)
	bfexts	FPTEMP_EX(%a6){#1:#15},%d1	|get dest exp (always neg)
	subl	%d1,%d0			|subtract src from dest
	cmpl	#0x8000,%d0
	blt	fix_stk			|if less, not wrap case
	bra	sub_wrap
sub_srcd:
	bsrl	ckinf_nd
	bne	fix_stk
	bfextu	FPTEMP_EX(%a6){#1:#15},%d0	|get dest exp (always pos)
	bfexts	ETEMP_EX(%a6){#1:#15},%d1	|get src exp (always neg)
	subl	%d1,%d0			|subtract dest from src
	cmpl	#0x8000,%d0
	blt	fix_stk			|if less, not wrap case
|
| Check the signs of the operands.  If they are alike, the fpu
| can be used to subtract from the norm 1.0 with the sign of the
| denorm and it will correctly generate the result in extended
| precision.  We can then call round with no sticky and the result
| will be correct for the user's rounding mode and precision.  If
| the signs are unlike, we call round with the sticky bit set
| and the result will be correct for the user's rounding mode and
| precision.
|
sub_wrap:
	movew	ETEMP_EX(%a6),%d0
	movew	FPTEMP_EX(%a6),%d1
	eorw	%d1,%d0
	andiw	#0x8000,%d0
	bne	sub_diff
|
| The signs are alike.
|
	cmpb	#0x0f,DNRM_FLG(%a6) |is dest the denorm?
	bnes	sub_u_srcd
	movew	FPTEMP_EX(%a6),%d0
	andiw	#0x8000,%d0
	orw	#0x3fff,%d0	|force the exponent to +/- 1
	movew	%d0,FPTEMP_EX(%a6) |in the denorm
	movel	USER_FPCR(%a6),%d0
	andil	#0x30,%d0
	fmovel	%d0,%fpcr		|set up users rmode and X
	fmovex	FPTEMP(%a6),%fp0
	fsubx	ETEMP(%a6),%fp0
	fmovel	%fpsr,%d1
	orl	%d1,USER_FPSR(%a6) |capture cc's and inex from fadd
	leal	WBTEMP(%a6),%a0	|point a0 to wbtemp in frame
	fmovex	%fp0,WBTEMP(%a6)	|write result to memory
	lsrl	#4,%d0		|put rmode in lower 2 bits
	movel	USER_FPCR(%a6),%d1
	andil	#0xc0,%d1
	lsrl	#6,%d1		|put precision in upper word
	swap	%d1
	orl	%d0,%d1		|set up for round call
	clrl	%d0		|force sticky to zero
	bclrb	#sign_bit,WBTEMP_EX(%a6)
	sne	WBTEMP_SGN(%a6)
	bsrl	round		|round result to users rmode & prec
	bfclr	WBTEMP_SGN(%a6){#0:#8}	|convert back to IEEE ext format
	beq	frcfpnr
	bsetb	#sign_bit,WBTEMP_EX(%a6)
	bra	frcfpnr
sub_u_srcd:
	movew	ETEMP_EX(%a6),%d0
	andiw	#0x8000,%d0
	orw	#0x3fff,%d0	|force the exponent to +/- 1
	movew	%d0,ETEMP_EX(%a6) |in the denorm
	movel	USER_FPCR(%a6),%d0
	andil	#0x30,%d0
	fmovel	%d0,%fpcr		|set up users rmode and X
	fmovex	FPTEMP(%a6),%fp0
	fsubx	ETEMP(%a6),%fp0
	fmovel	%fpsr,%d1
	orl	%d1,USER_FPSR(%a6) |capture cc's and inex from fadd
	leal	WBTEMP(%a6),%a0	|point a0 to wbtemp in frame
	fmovex	%fp0,WBTEMP(%a6)	|write result to memory
	lsrl	#4,%d0		|put rmode in lower 2 bits
	movel	USER_FPCR(%a6),%d1
	andil	#0xc0,%d1
	lsrl	#6,%d1		|put precision in upper word
	swap	%d1
	orl	%d0,%d1		|set up for round call
	clrl	%d0		|force sticky to zero
	bclrb	#sign_bit,WBTEMP_EX(%a6)
	sne	WBTEMP_SGN(%a6)
	bsrl	round		|round result to users rmode & prec
	bfclr	WBTEMP_SGN(%a6){#0:#8}	|convert back to IEEE ext format
	beq	frcfpnr
	bsetb	#sign_bit,WBTEMP_EX(%a6)
	bra	frcfpnr
|
| Signs are unlike:
|
sub_diff:
	cmpb	#0x0f,DNRM_FLG(%a6) |is dest the denorm?
	bnes	sub_s_srcd
sub_s_destd:
	leal	ETEMP(%a6),%a0
	movel	USER_FPCR(%a6),%d0
	andil	#0x30,%d0
	lsrl	#4,%d0		|put rmode in lower 2 bits
	movel	USER_FPCR(%a6),%d1
	andil	#0xc0,%d1
	lsrl	#6,%d1		|put precision in upper word
	swap	%d1
	orl	%d0,%d1		|set up for round call
	movel	#0x20000000,%d0	|set sticky for round
|
| Since the dest is the denorm, the sign is the opposite of the
| norm sign.
|
	eoriw	#0x8000,ETEMP_EX(%a6)	|flip sign on result
	tstw	ETEMP_EX(%a6)
	bgts	sub_s_dwr
	orl	#neg_mask,USER_FPSR(%a6)
sub_s_dwr:
	bclrb	#sign_bit,ETEMP_EX(%a6)
	sne	ETEMP_SGN(%a6)
	bsrl	round		|round result to users rmode & prec
	bfclr	ETEMP_SGN(%a6){#0:#8}	|convert back to IEEE ext format
	beqs	sub_s_dclr
	bsetb	#sign_bit,ETEMP_EX(%a6)
sub_s_dclr:
	leal	WBTEMP(%a6),%a0
	movel	ETEMP(%a6),(%a0)	|write result to wbtemp
	movel	ETEMP_HI(%a6),4(%a0)
	movel	ETEMP_LO(%a6),8(%a0)
	bra	sub_ckovf
sub_s_srcd:
	leal	FPTEMP(%a6),%a0
	movel	USER_FPCR(%a6),%d0
	andil	#0x30,%d0
	lsrl	#4,%d0		|put rmode in lower 2 bits
	movel	USER_FPCR(%a6),%d1
	andil	#0xc0,%d1
	lsrl	#6,%d1		|put precision in upper word
	swap	%d1
	orl	%d0,%d1		|set up for round call
	movel	#0x20000000,%d0	|set sticky for round
	bclrb	#sign_bit,FPTEMP_EX(%a6)
	sne	FPTEMP_SGN(%a6)
	bsrl	round		|round result to users rmode & prec
	bfclr	FPTEMP_SGN(%a6){#0:#8}	|convert back to IEEE ext format
	beqs	sub_s_sclr
	bsetb	#sign_bit,FPTEMP_EX(%a6)
sub_s_sclr:
	leal	WBTEMP(%a6),%a0
	movel	FPTEMP(%a6),(%a0)	|write result to wbtemp
	movel	FPTEMP_HI(%a6),4(%a0)
	movel	FPTEMP_LO(%a6),8(%a0)
	tstw	FPTEMP_EX(%a6)
	bgt	sub_ckovf
	orl	#neg_mask,USER_FPSR(%a6)
sub_ckovf:
	movew	WBTEMP_EX(%a6),%d0
	andiw	#0x7fff,%d0
	cmpiw	#0x7fff,%d0
	bne	frcfpnr
|
| The result has overflowed to $7fff exponent.  Set I, ovfl,
| and aovfl, and clr the mantissa (incorrectly set by the
| round routine.)
|
	orl	#inf_mask+ovfl_inx_mask,USER_FPSR(%a6)
	clrl	4(%a0)
	bra	frcfpnr
|
| Inst is fcmp.
|
wrap_cmp:
	cmpb	#0xff,DNRM_FLG(%a6) |if both ops denorm,
	beq	fix_stk		 |restore to fpu
|
| One of the ops is denormalized.  Test for wrap condition
| and complete the instruction.
|
	cmpb	#0x0f,DNRM_FLG(%a6) |check for dest denorm
	bnes	cmp_srcd
cmp_destd:
	bsrl	ckinf_ns
	bne	fix_stk
	bfextu	ETEMP_EX(%a6){#1:#15},%d0	|get src exp (always pos)
	bfexts	FPTEMP_EX(%a6){#1:#15},%d1	|get dest exp (always neg)
	subl	%d1,%d0			|subtract dest from src
	cmpl	#0x8000,%d0
	blt	fix_stk			|if less, not wrap case
	tstw	ETEMP_EX(%a6)		|set N to ~sign_of(src)
	bge	cmp_setn
	rts
cmp_srcd:
	bsrl	ckinf_nd
	bne	fix_stk
	bfextu	FPTEMP_EX(%a6){#1:#15},%d0	|get dest exp (always pos)
	bfexts	ETEMP_EX(%a6){#1:#15},%d1	|get src exp (always neg)
	subl	%d1,%d0			|subtract src from dest
	cmpl	#0x8000,%d0
	blt	fix_stk			|if less, not wrap case
	tstw	FPTEMP_EX(%a6)		|set N to sign_of(dest)
	blt	cmp_setn
	rts
cmp_setn:
	orl	#neg_mask,USER_FPSR(%a6)
	rts

|
| Inst is fmul.
|
wrap_mul:
	cmpb	#0xff,DNRM_FLG(%a6) |if both ops denorm,
	beq	force_unf	|force an underflow (really!)
|
| One of the ops is denormalized.  Test for wrap condition
| and complete the instruction.
|
	cmpb	#0x0f,DNRM_FLG(%a6) |check for dest denorm
	bnes	mul_srcd
mul_destd:
	bsrl	ckinf_ns
	bne	fix_stk
	bfextu	ETEMP_EX(%a6){#1:#15},%d0	|get src exp (always pos)
	bfexts	FPTEMP_EX(%a6){#1:#15},%d1	|get dest exp (always neg)
	addl	%d1,%d0			|subtract dest from src
	bgt	fix_stk
	bra	force_unf
mul_srcd:
	bsrl	ckinf_nd
	bne	fix_stk
	bfextu	FPTEMP_EX(%a6){#1:#15},%d0	|get dest exp (always pos)
	bfexts	ETEMP_EX(%a6){#1:#15},%d1	|get src exp (always neg)
	addl	%d1,%d0			|subtract src from dest
	bgt	fix_stk

|
| This code handles the case of the instruction resulting in
| an underflow condition.
|
force_unf:
	bclrb	#E1,E_BYTE(%a6)
	orl	#unfinx_mask,USER_FPSR(%a6)
	clrw	NMNEXC(%a6)
	clrb	WBTEMP_SGN(%a6)
	movew	ETEMP_EX(%a6),%d0		|find the sign of the result
	movew	FPTEMP_EX(%a6),%d1
	eorw	%d1,%d0
	andiw	#0x8000,%d0
	beqs	frcunfcont
	st	WBTEMP_SGN(%a6)
frcunfcont:
	lea	WBTEMP(%a6),%a0		|point a0 to memory location
	movew	CMDREG1B(%a6),%d0
	btstl	#6,%d0			|test for forced precision
	beqs	frcunf_fpcr
	btstl	#2,%d0			|check for double
	bnes	frcunf_dbl
	movel	#0x1,%d0			|inst is forced single
	bras	frcunf_rnd
frcunf_dbl:
	movel	#0x2,%d0			|inst is forced double
	bras	frcunf_rnd
frcunf_fpcr:
	bfextu	FPCR_MODE(%a6){#0:#2},%d0	|inst not forced - use fpcr prec
frcunf_rnd:
	bsrl	unf_sub			|get correct result based on
|					;round precision/mode.  This
|					;sets FPSR_CC correctly
	bfclr	WBTEMP_SGN(%a6){#0:#8}	|convert back to IEEE ext format
	beqs	frcfpn
	bsetb	#sign_bit,WBTEMP_EX(%a6)
	bra	frcfpn

|
| Write the result to the user's fpn.  All results must be HUGE to be
| written; otherwise the results would have overflowed or underflowed.
| If the rounding precision is single or double, the ovf_res routine
| is needed to correctly supply the max value.
|
frcfpnr:
	movew	CMDREG1B(%a6),%d0
	btstl	#6,%d0			|test for forced precision
	beqs	frcfpn_fpcr
	btstl	#2,%d0			|check for double
	bnes	frcfpn_dbl
	movel	#0x1,%d0			|inst is forced single
	bras	frcfpn_rnd
frcfpn_dbl:
	movel	#0x2,%d0			|inst is forced double
	bras	frcfpn_rnd
frcfpn_fpcr:
	bfextu	FPCR_MODE(%a6){#0:#2},%d0	|inst not forced - use fpcr prec
	tstb	%d0
	beqs	frcfpn			|if extended, write what you got
frcfpn_rnd:
	bclrb	#sign_bit,WBTEMP_EX(%a6)
	sne	WBTEMP_SGN(%a6)
	bsrl	ovf_res			|get correct result based on
|					;round precision/mode.  This
|					;sets FPSR_CC correctly
	bfclr	WBTEMP_SGN(%a6){#0:#8}	|convert back to IEEE ext format
	beqs	frcfpn_clr
	bsetb	#sign_bit,WBTEMP_EX(%a6)
frcfpn_clr:
	orl	#ovfinx_mask,USER_FPSR(%a6)
|
| Perform the write.
|
frcfpn:
	bfextu	CMDREG1B(%a6){#6:#3},%d0	|extract fp destination register
	cmpib	#3,%d0
	bles	frc0123			|check if dest is fp0-fp3
	movel	#7,%d1
	subl	%d0,%d1
	clrl	%d0
	bsetl	%d1,%d0
	fmovemx WBTEMP(%a6),%d0
	rts
frc0123:
	cmpib	#0,%d0
	beqs	frc0_dst
	cmpib	#1,%d0
	beqs	frc1_dst
	cmpib	#2,%d0
	beqs	frc2_dst
frc3_dst:
	movel	WBTEMP_EX(%a6),USER_FP3(%a6)
	movel	WBTEMP_HI(%a6),USER_FP3+4(%a6)
	movel	WBTEMP_LO(%a6),USER_FP3+8(%a6)
	rts
frc2_dst:
	movel	WBTEMP_EX(%a6),USER_FP2(%a6)
	movel	WBTEMP_HI(%a6),USER_FP2+4(%a6)
	movel	WBTEMP_LO(%a6),USER_FP2+8(%a6)
	rts
frc1_dst:
	movel	WBTEMP_EX(%a6),USER_FP1(%a6)
	movel	WBTEMP_HI(%a6),USER_FP1+4(%a6)
	movel	WBTEMP_LO(%a6),USER_FP1+8(%a6)
	rts
frc0_dst:
	movel	WBTEMP_EX(%a6),USER_FP0(%a6)
	movel	WBTEMP_HI(%a6),USER_FP0+4(%a6)
	movel	WBTEMP_LO(%a6),USER_FP0+8(%a6)
	rts

|
| Write etemp to fpn.
| A check is made on enabled and signalled snan exceptions,
| and the destination is not overwritten if this condition exists.
| This code is designed to make fmoveins of unsupported data types
| faster.
|
wr_etemp:
	btstb	#snan_bit,FPSR_EXCEPT(%a6)	|if snan is set, and
	beqs	fmoveinc		|enabled, force restore
	btstb	#snan_bit,FPCR_ENABLE(%a6) |and don't overwrite
	beqs	fmoveinc		|the dest
	movel	ETEMP_EX(%a6),FPTEMP_EX(%a6)	|set up fptemp sign for
|						;snan handler
	tstb	ETEMP(%a6)		|check for negative
	blts	snan_neg
	rts
snan_neg:
	orl	#neg_bit,USER_FPSR(%a6)	|snan is negative; set N
	rts
fmoveinc:
	clrw	NMNEXC(%a6)
	bclrb	#E1,E_BYTE(%a6)
	moveb	STAG(%a6),%d0		|check if stag is inf
	andib	#0xe0,%d0
	cmpib	#0x40,%d0
	bnes	fminc_cnan
	orl	#inf_mask,USER_FPSR(%a6) |if inf, nothing yet has set I
	tstw	LOCAL_EX(%a0)		|check sign
	bges	fminc_con
	orl	#neg_mask,USER_FPSR(%a6)
	bra	fminc_con
fminc_cnan:
	cmpib	#0x60,%d0			|check if stag is NaN
	bnes	fminc_czero
	orl	#nan_mask,USER_FPSR(%a6) |if nan, nothing yet has set NaN
	movel	ETEMP_EX(%a6),FPTEMP_EX(%a6)	|set up fptemp sign for
|						;snan handler
	tstw	LOCAL_EX(%a0)		|check sign
	bges	fminc_con
	orl	#neg_mask,USER_FPSR(%a6)
	bra	fminc_con
fminc_czero:
	cmpib	#0x20,%d0			|check if zero
	bnes	fminc_con
	orl	#z_mask,USER_FPSR(%a6)	|if zero, set Z
	tstw	LOCAL_EX(%a0)		|check sign
	bges	fminc_con
	orl	#neg_mask,USER_FPSR(%a6)
fminc_con:
	bfextu	CMDREG1B(%a6){#6:#3},%d0	|extract fp destination register
	cmpib	#3,%d0
	bles	fp0123			|check if dest is fp0-fp3
	movel	#7,%d1
	subl	%d0,%d1
	clrl	%d0
	bsetl	%d1,%d0
	fmovemx ETEMP(%a6),%d0
	rts

fp0123:
	cmpib	#0,%d0
	beqs	fp0_dst
	cmpib	#1,%d0
	beqs	fp1_dst
	cmpib	#2,%d0
	beqs	fp2_dst
fp3_dst:
	movel	ETEMP_EX(%a6),USER_FP3(%a6)
	movel	ETEMP_HI(%a6),USER_FP3+4(%a6)
	movel	ETEMP_LO(%a6),USER_FP3+8(%a6)
	rts
fp2_dst:
	movel	ETEMP_EX(%a6),USER_FP2(%a6)
	movel	ETEMP_HI(%a6),USER_FP2+4(%a6)
	movel	ETEMP_LO(%a6),USER_FP2+8(%a6)
	rts
fp1_dst:
	movel	ETEMP_EX(%a6),USER_FP1(%a6)
	movel	ETEMP_HI(%a6),USER_FP1+4(%a6)
	movel	ETEMP_LO(%a6),USER_FP1+8(%a6)
	rts
fp0_dst:
	movel	ETEMP_EX(%a6),USER_FP0(%a6)
	movel	ETEMP_HI(%a6),USER_FP0+4(%a6)
	movel	ETEMP_LO(%a6),USER_FP0+8(%a6)
	rts

opclass3:
	st	CU_ONLY(%a6)
	movew	CMDREG1B(%a6),%d0	|check if packed moveout
	andiw	#0x0c00,%d0	|isolate last 2 bits of size field
	cmpiw	#0x0c00,%d0	|if size is 011 or 111, it is packed
	beq	pack_out	|else it is norm or denorm
	bra	mv_out


|
|	MOVE OUT
|

mv_tbl:
	.long	li
	.long	sgp
	.long	xp
	.long	mvout_end	|should never be taken
	.long	wi
	.long	dp
	.long	bi
	.long	mvout_end	|should never be taken
mv_out:
	bfextu	CMDREG1B(%a6){#3:#3},%d1	|put source specifier in d1
	leal	mv_tbl,%a0
	movel	%a0@(%d1:l:4),%a0
	jmp	(%a0)

|
| This exit is for move-out to memory.  The aunfl bit is
| set if the result is inex and unfl is signalled.
|
mvout_end:
	btstb	#inex2_bit,FPSR_EXCEPT(%a6)
	beqs	no_aufl
	btstb	#unfl_bit,FPSR_EXCEPT(%a6)
	beqs	no_aufl
	bsetb	#aunfl_bit,FPSR_AEXCEPT(%a6)
no_aufl:
	clrw	NMNEXC(%a6)
	bclrb	#E1,E_BYTE(%a6)
	fmovel	#0,%FPSR			|clear any cc bits from res_func
|
| Return ETEMP to extended format from internal extended format so
| that gen_except will have a correctly signed value for ovfl/unfl
| handlers.
|
	bfclr	ETEMP_SGN(%a6){#0:#8}
	beqs	mvout_con
	bsetb	#sign_bit,ETEMP_EX(%a6)
mvout_con:
	rts
|
| This exit is for move-out to int register.  The aunfl bit is
| not set in any case for this move.
|
mvouti_end:
	clrw	NMNEXC(%a6)
	bclrb	#E1,E_BYTE(%a6)
	fmovel	#0,%FPSR			|clear any cc bits from res_func
|
| Return ETEMP to extended format from internal extended format so
| that gen_except will have a correctly signed value for ovfl/unfl
| handlers.
|
	bfclr	ETEMP_SGN(%a6){#0:#8}
	beqs	mvouti_con
	bsetb	#sign_bit,ETEMP_EX(%a6)
mvouti_con:
	rts
|
| li is used to handle a long integer source specifier
|

li:
	moveql	#4,%d0		|set byte count

	btstb	#7,STAG(%a6)	|check for extended denorm
	bne	int_dnrm	|if so, branch

	fmovemx ETEMP(%a6),%fp0-%fp0
	fcmpd	#0x41dfffffffc00000,%fp0
| 41dfffffffc00000 in dbl prec = 401d0000fffffffe00000000 in ext prec
	fbge	lo_plrg
	fcmpd	#0xc1e0000000000000,%fp0
| c1e0000000000000 in dbl prec = c01e00008000000000000000 in ext prec
	fble	lo_nlrg
|
| at this point, the answer is between the largest pos and neg values
|
	movel	USER_FPCR(%a6),%d1	|use user's rounding mode
	andil	#0x30,%d1
	fmovel	%d1,%fpcr
	fmovel	%fp0,L_SCR1(%a6)	|let the 040 perform conversion
	fmovel %fpsr,%d1
	orl	%d1,USER_FPSR(%a6)	|capture inex2/ainex if set
	bra	int_wrt


lo_plrg:
	movel	#0x7fffffff,L_SCR1(%a6)	|answer is largest positive int
	fbeq	int_wrt			|exact answer
	fcmpd	#0x41dfffffffe00000,%fp0
| 41dfffffffe00000 in dbl prec = 401d0000ffffffff00000000 in ext prec
	fbge	int_operr		|set operr
	bra	int_inx			|set inexact

lo_nlrg:
	movel	#0x80000000,L_SCR1(%a6)
	fbeq	int_wrt			|exact answer
	fcmpd	#0xc1e0000000100000,%fp0
| c1e0000000100000 in dbl prec = c01e00008000000080000000 in ext prec
	fblt	int_operr		|set operr
	bra	int_inx			|set inexact

|
| wi is used to handle a word integer source specifier
|

wi:
	moveql	#2,%d0		|set byte count

	btstb	#7,STAG(%a6)	|check for extended denorm
	bne	int_dnrm	|branch if so

	fmovemx ETEMP(%a6),%fp0-%fp0
	fcmps	#0x46fffe00,%fp0
| 46fffe00 in sgl prec = 400d0000fffe000000000000 in ext prec
	fbge	wo_plrg
	fcmps	#0xc7000000,%fp0
| c7000000 in sgl prec = c00e00008000000000000000 in ext prec
	fble	wo_nlrg

|
| at this point, the answer is between the largest pos and neg values
|
	movel	USER_FPCR(%a6),%d1	|use user's rounding mode
	andil	#0x30,%d1
	fmovel	%d1,%fpcr
	fmovew	%fp0,L_SCR1(%a6)	|let the 040 perform conversion
	fmovel %fpsr,%d1
	orl	%d1,USER_FPSR(%a6)	|capture inex2/ainex if set
	bra	int_wrt

wo_plrg:
	movew	#0x7fff,L_SCR1(%a6)	|answer is largest positive int
	fbeq	int_wrt			|exact answer
	fcmps	#0x46ffff00,%fp0
| 46ffff00 in sgl prec = 400d0000ffff000000000000 in ext prec
	fbge	int_operr		|set operr
	bra	int_inx			|set inexact

wo_nlrg:
	movew	#0x8000,L_SCR1(%a6)
	fbeq	int_wrt			|exact answer
	fcmps	#0xc7000080,%fp0
| c7000080 in sgl prec = c00e00008000800000000000 in ext prec
	fblt	int_operr		|set operr
	bra	int_inx			|set inexact

|
| bi is used to handle a byte integer source specifier
|

bi:
	moveql	#1,%d0		|set byte count

	btstb	#7,STAG(%a6)	|check for extended denorm
	bne	int_dnrm	|branch if so

	fmovemx ETEMP(%a6),%fp0-%fp0
	fcmps	#0x42fe0000,%fp0
| 42fe0000 in sgl prec = 40050000fe00000000000000 in ext prec
	fbge	by_plrg
	fcmps	#0xc3000000,%fp0
| c3000000 in sgl prec = c00600008000000000000000 in ext prec
	fble	by_nlrg

|
| at this point, the answer is between the largest pos and neg values
|
	movel	USER_FPCR(%a6),%d1	|use user's rounding mode
	andil	#0x30,%d1
	fmovel	%d1,%fpcr
	fmoveb	%fp0,L_SCR1(%a6)	|let the 040 perform conversion
	fmovel %fpsr,%d1
	orl	%d1,USER_FPSR(%a6)	|capture inex2/ainex if set
	bra	int_wrt

by_plrg:
	moveb	#0x7f,L_SCR1(%a6)		|answer is largest positive int
	fbeq	int_wrt			|exact answer
	fcmps	#0x42ff0000,%fp0
| 42ff0000 in sgl prec = 40050000ff00000000000000 in ext prec
	fbge	int_operr		|set operr
	bra	int_inx			|set inexact

by_nlrg:
	moveb	#0x80,L_SCR1(%a6)
	fbeq	int_wrt			|exact answer
	fcmps	#0xc3008000,%fp0
| c3008000 in sgl prec = c00600008080000000000000 in ext prec
	fblt	int_operr		|set operr
	bra	int_inx			|set inexact

|
| Common integer routines
|
| int_drnrm---account for possible nonzero result for round up with positive
| operand and round down for negative answer.  In the first case (result = 1)
| byte-width (store in d0) of result must be honored.  In the second case,
| -1 in L_SCR1(a6) will cover all contingencies (FMOVE.B/W/L out).

int_dnrm:
	movel	#0,L_SCR1(%a6)	| initialize result to 0
	bfextu	FPCR_MODE(%a6){#2:#2},%d1	| d1 is the rounding mode
	cmpb	#2,%d1
	bmis	int_inx		| if RN or RZ, done
	bnes	int_rp		| if RP, continue below
	tstw	ETEMP(%a6)	| RM: store -1 in L_SCR1 if src is negative
	bpls	int_inx		| otherwise result is 0
	movel	#-1,L_SCR1(%a6)
	bras	int_inx
int_rp:
	tstw	ETEMP(%a6)	| RP: store +1 of proper width in L_SCR1 if
|				; source is greater than 0
	bmis	int_inx		| otherwise, result is 0
	lea	L_SCR1(%a6),%a1	| a1 is address of L_SCR1
	addal	%d0,%a1		| offset by destination width -1
	subal	#1,%a1
	bsetb	#0,(%a1)		| set low bit at a1 address
int_inx:
	oril	#inx2a_mask,USER_FPSR(%a6)
	bras	int_wrt
int_operr:
	fmovemx %fp0-%fp0,FPTEMP(%a6)	|FPTEMP must contain the extended
|				;precision source that needs to be
|				;converted to integer this is required
|				;if the operr exception is enabled.
|				;set operr/aiop (no inex2 on int ovfl)

	oril	#opaop_mask,USER_FPSR(%a6)
|				;fall through to perform int_wrt
int_wrt:
	movel	EXC_EA(%a6),%a1	|load destination address
	tstl	%a1		|check to see if it is a dest register
	beqs	wrt_dn		|write data register
	lea	L_SCR1(%a6),%a0	|point to supervisor source address
	bsrl	mem_write
	bra	mvouti_end

wrt_dn:
	movel	%d0,-(%sp)	|d0 currently contains the size to write
	bsrl	get_fline	|get_fline returns Dn in d0
	andiw	#0x7,%d0		|isolate register
	movel	(%sp)+,%d1	|get size
	cmpil	#4,%d1		|most frequent case
	beqs	sz_long
	cmpil	#2,%d1
	bnes	sz_con
	orl	#8,%d0		|add 'word' size to register#
	bras	sz_con
sz_long:
	orl	#0x10,%d0		|add 'long' size to register#
sz_con:
	movel	%d0,%d1		|reg_dest expects size:reg in d1
	bsrl	reg_dest	|load proper data register
	bra	mvouti_end
xp:
	lea	ETEMP(%a6),%a0
	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)
	btstb	#7,STAG(%a6)	|check for extended denorm
	bne	xdnrm
	clrl	%d0
	bras	do_fp		|do normal case
sgp:
	lea	ETEMP(%a6),%a0
	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)
	btstb	#7,STAG(%a6)	|check for extended denorm
	bne	sp_catas	|branch if so
	movew	LOCAL_EX(%a0),%d0
	lea	sp_bnds,%a1
	cmpw	(%a1),%d0
	blt	sp_under
	cmpw	2(%a1),%d0
	bgt	sp_over
	movel	#1,%d0		|set destination format to single
	bras	do_fp		|do normal case
dp:
	lea	ETEMP(%a6),%a0
	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)

	btstb	#7,STAG(%a6)	|check for extended denorm
	bne	dp_catas	|branch if so

	movew	LOCAL_EX(%a0),%d0
	lea	dp_bnds,%a1

	cmpw	(%a1),%d0
	blt	dp_under
	cmpw	2(%a1),%d0
	bgt	dp_over

	movel	#2,%d0		|set destination format to double
|				;fall through to do_fp
|
do_fp:
	bfextu	FPCR_MODE(%a6){#2:#2},%d1	|rnd mode in d1
	swap	%d0			|rnd prec in upper word
	addl	%d0,%d1			|d1 has PREC/MODE info

	clrl	%d0			|clear g,r,s

	bsrl	round			|round

	movel	%a0,%a1
	movel	EXC_EA(%a6),%a0

	bfextu	CMDREG1B(%a6){#3:#3},%d1	|extract destination format
|					;at this point only the dest
|					;formats sgl, dbl, ext are
|					;possible
	cmpb	#2,%d1
	bgts	ddbl			|double=5, extended=2, single=1
	bnes	dsgl
|					;fall through to dext
dext:
	bsrl	dest_ext
	bra	mvout_end
dsgl:
	bsrl	dest_sgl
	bra	mvout_end
ddbl:
	bsrl	dest_dbl
	bra	mvout_end

|
| Handle possible denorm or catastrophic underflow cases here
|
xdnrm:
	bsr	set_xop		|initialize WBTEMP
	bsetb	#wbtemp15_bit,WB_BYTE(%a6) |set wbtemp15

	movel	%a0,%a1
	movel	EXC_EA(%a6),%a0	|a0 has the destination pointer
	bsrl	dest_ext	|store to memory
	bsetb	#unfl_bit,FPSR_EXCEPT(%a6)
	bra	mvout_end

sp_under:
	bsetb	#etemp15_bit,STAG(%a6)

	cmpw	4(%a1),%d0
	blts	sp_catas	|catastrophic underflow case

	movel	#1,%d0		|load in round precision
	movel	#sgl_thresh,%d1	|load in single denorm threshold
	bsrl	dpspdnrm	|expects d1 to have the proper
|				;denorm threshold
	bsrl	dest_sgl	|stores value to destination
	bsetb	#unfl_bit,FPSR_EXCEPT(%a6)
	bra	mvout_end	|exit

dp_under:
	bsetb	#etemp15_bit,STAG(%a6)

	cmpw	4(%a1),%d0
	blts	dp_catas	|catastrophic underflow case

	movel	#dbl_thresh,%d1	|load in double precision threshold
	movel	#2,%d0
	bsrl	dpspdnrm	|expects d1 to have proper
|				;denorm threshold
|				;expects d0 to have round precision
	bsrl	dest_dbl	|store value to destination
	bsetb	#unfl_bit,FPSR_EXCEPT(%a6)
	bra	mvout_end	|exit

|
| Handle catastrophic underflow cases here
|
sp_catas:
| Temp fix for z bit set in unf_sub
	movel	USER_FPSR(%a6),-(%a7)

	movel	#1,%d0		|set round precision to sgl

	bsrl	unf_sub		|a0 points to result

	movel	(%a7)+,USER_FPSR(%a6)

	movel	#1,%d0
	subw	%d0,LOCAL_EX(%a0) |account for difference between
|				;denorm/norm bias

	movel	%a0,%a1		|a1 has the operand input
	movel	EXC_EA(%a6),%a0	|a0 has the destination pointer

	bsrl	dest_sgl	|store the result
	oril	#unfinx_mask,USER_FPSR(%a6)
	bra	mvout_end

dp_catas:
| Temp fix for z bit set in unf_sub
	movel	USER_FPSR(%a6),-(%a7)

	movel	#2,%d0		|set round precision to dbl
	bsrl	unf_sub		|a0 points to result

	movel	(%a7)+,USER_FPSR(%a6)

	movel	#1,%d0
	subw	%d0,LOCAL_EX(%a0) |account for difference between
|				;denorm/norm bias

	movel	%a0,%a1		|a1 has the operand input
	movel	EXC_EA(%a6),%a0	|a0 has the destination pointer

	bsrl	dest_dbl	|store the result
	oril	#unfinx_mask,USER_FPSR(%a6)
	bra	mvout_end

|
| Handle catastrophic overflow cases here
|
sp_over:
| Temp fix for z bit set in unf_sub
	movel	USER_FPSR(%a6),-(%a7)

	movel	#1,%d0
	leal	FP_SCR1(%a6),%a0	|use FP_SCR1 for creating result
	movel	ETEMP_EX(%a6),(%a0)
	movel	ETEMP_HI(%a6),4(%a0)
	movel	ETEMP_LO(%a6),8(%a0)
	bsrl	ovf_res

	movel	(%a7)+,USER_FPSR(%a6)

	movel	%a0,%a1
	movel	EXC_EA(%a6),%a0
	bsrl	dest_sgl
	orl	#ovfinx_mask,USER_FPSR(%a6)
	bra	mvout_end

dp_over:
| Temp fix for z bit set in ovf_res
	movel	USER_FPSR(%a6),-(%a7)

	movel	#2,%d0
	leal	FP_SCR1(%a6),%a0	|use FP_SCR1 for creating result
	movel	ETEMP_EX(%a6),(%a0)
	movel	ETEMP_HI(%a6),4(%a0)
	movel	ETEMP_LO(%a6),8(%a0)
	bsrl	ovf_res

	movel	(%a7)+,USER_FPSR(%a6)

	movel	%a0,%a1
	movel	EXC_EA(%a6),%a0
	bsrl	dest_dbl
	orl	#ovfinx_mask,USER_FPSR(%a6)
	bra	mvout_end

|
|	DPSPDNRM
|
| This subroutine takes an extended normalized number and denormalizes
| it to the given round precision. This subroutine also decrements
| the input operand's exponent by 1 to account for the fact that
| dest_sgl or dest_dbl expects a normalized number's bias.
|
| Input: a0  points to a normalized number in internal extended format
|	 d0  is the round precision (=1 for sgl; =2 for dbl)
|	 d1  is the single precision or double precision
|	     denorm threshold
|
| Output: (In the format for dest_sgl or dest_dbl)
|	 a0   points to the destination
|	 a1   points to the operand
|
| Exceptions: Reports inexact 2 exception by setting USER_FPSR bits
|
dpspdnrm:
	movel	%d0,-(%a7)	|save round precision
	clrl	%d0		|clear initial g,r,s
	bsrl	dnrm_lp		|careful with d0, it's needed by round

	bfextu	FPCR_MODE(%a6){#2:#2},%d1 |get rounding mode
	swap	%d1
	movew	2(%a7),%d1	|set rounding precision
	swap	%d1		|at this point d1 has PREC/MODE info
	bsrl	round		|round result, sets the inex bit in
|				;USER_FPSR if needed

	movew	#1,%d0
	subw	%d0,LOCAL_EX(%a0) |account for difference in denorm
|				;vs norm bias

	movel	%a0,%a1		|a1 has the operand input
	movel	EXC_EA(%a6),%a0	|a0 has the destination pointer
	addw	#4,%a7		|pop stack
	rts
|
| SET_XOP initialized WBTEMP with the value pointed to by a0
| input: a0 points to input operand in the internal extended format
|
set_xop:
	movel	LOCAL_EX(%a0),WBTEMP_EX(%a6)
	movel	LOCAL_HI(%a0),WBTEMP_HI(%a6)
	movel	LOCAL_LO(%a0),WBTEMP_LO(%a6)
	bfclr	WBTEMP_SGN(%a6){#0:#8}
	beqs	sxop
	bsetb	#sign_bit,WBTEMP_EX(%a6)
sxop:
	bfclr	STAG(%a6){#5:#4}	|clear wbtm66,wbtm1,wbtm0,sbit
	rts
|
|	P_MOVE
|
p_movet:
	.long	p_move
	.long	p_movez
	.long	p_movei
	.long	p_moven
	.long	p_move
p_regd:
	.long	p_dyd0
	.long	p_dyd1
	.long	p_dyd2
	.long	p_dyd3
	.long	p_dyd4
	.long	p_dyd5
	.long	p_dyd6
	.long	p_dyd7

pack_out:
	leal	p_movet,%a0	|load jmp table address
	movew	STAG(%a6),%d0	|get source tag
	bfextu	%d0{#16:#3},%d0	|isolate source bits
	movel	(%a0,%d0.w*4),%a0	|load a0 with routine label for tag
	jmp	(%a0)		|go to the routine

p_write:
	movel	#0x0c,%d0	|get byte count
	movel	EXC_EA(%a6),%a1	|get the destination address
	bsr	mem_write	|write the user's destination
	moveb	#0,CU_SAVEPC(%a6) |set the cu save pc to all 0's

|
| Also note that the dtag must be set to norm here - this is because
| the 040 uses the dtag to execute the correct microcode.
|
        bfclr    DTAG(%a6){#0:#3}  |set dtag to norm

	rts

| Notes on handling of special case (zero, inf, and nan) inputs:
|	1. Operr is not signalled if the k-factor is greater than 18.
|	2. Per the manual, status bits are not set.
|

p_move:
	movew	CMDREG1B(%a6),%d0
	btstl	#kfact_bit,%d0	|test for dynamic k-factor
	beqs	statick		|if clear, k-factor is static
dynamick:
	bfextu	%d0{#25:#3},%d0	|isolate register for dynamic k-factor
	lea	p_regd,%a0
	movel	%a0@(%d0:l:4),%a0
	jmp	(%a0)
statick:
	andiw	#0x007f,%d0	|get k-factor
	bfexts	%d0{#25:#7},%d0	|sign extend d0 for bindec
	leal	ETEMP(%a6),%a0	|a0 will point to the packed decimal
	bsrl	bindec		|perform the convert; data at a6
	leal	FP_SCR1(%a6),%a0	|load a0 with result address
	bral	p_write
p_movez:
	leal	ETEMP(%a6),%a0	|a0 will point to the packed decimal
	clrw	2(%a0)		|clear lower word of exp
	clrl	4(%a0)		|load second lword of ZERO
	clrl	8(%a0)		|load third lword of ZERO
	bra	p_write		|go write results
p_movei:
	fmovel	#0,%FPSR		|clear aiop
	leal	ETEMP(%a6),%a0	|a0 will point to the packed decimal
	clrw	2(%a0)		|clear lower word of exp
	bra	p_write		|go write the result
p_moven:
	leal	ETEMP(%a6),%a0	|a0 will point to the packed decimal
	clrw	2(%a0)		|clear lower word of exp
	bra	p_write		|go write the result

|
| Routines to read the dynamic k-factor from Dn.
|
p_dyd0:
	movel	USER_D0(%a6),%d0
	bras	statick
p_dyd1:
	movel	USER_D1(%a6),%d0
	bras	statick
p_dyd2:
	movel	%d2,%d0
	bras	statick
p_dyd3:
	movel	%d3,%d0
	bras	statick
p_dyd4:
	movel	%d4,%d0
	bras	statick
p_dyd5:
	movel	%d5,%d0
	bras	statick
p_dyd6:
	movel	%d6,%d0
	bra	statick
p_dyd7:
	movel	%d7,%d0
	bra	statick

	|end
