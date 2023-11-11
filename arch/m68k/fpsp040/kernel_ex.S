|
|	kernel_ex.sa 3.3 12/19/90
|
| This file contains routines to force exception status in the
| fpu for exceptional cases detected or reported within the
| transcendental functions.  Typically, the t_xx routine will
| set the appropriate bits in the USER_FPSR word on the stack.
| The bits are tested in gen_except.sa to determine if an exceptional
| situation needs to be created on return from the FPSP.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

KERNEL_EX:    |idnt    2,1 | Motorola 040 Floating Point Software Package

	|section    8

#include "fpsp.h"

mns_inf:  .long 0xffff0000,0x00000000,0x00000000
pls_inf:  .long 0x7fff0000,0x00000000,0x00000000
nan:      .long 0x7fff0000,0xffffffff,0xffffffff
huge:     .long 0x7ffe0000,0xffffffff,0xffffffff

	|xref	  ovf_r_k
	|xref	  unf_sub
	|xref	  nrm_set

	.global	  t_dz
	.global      t_dz2
	.global      t_operr
	.global      t_unfl
	.global      t_ovfl
	.global      t_ovfl2
	.global      t_inx2
	.global	  t_frcinx
	.global	  t_extdnrm
	.global	  t_resdnrm
	.global	  dst_nan
	.global	  src_nan
|
|	DZ exception
|
|
|	if dz trap disabled
|		store properly signed inf (use sign of etemp) into fp0
|		set FPSR exception status dz bit, condition code
|		inf bit, and accrued dz bit
|		return
|		frestore the frame into the machine (done by unimp_hd)
|
|	else dz trap enabled
|		set exception status bit & accrued bits in FPSR
|		set flag to disable sto_res from corrupting fp register
|		return
|		frestore the frame into the machine (done by unimp_hd)
|
| t_dz2 is used by monadic functions such as flogn (from do_func).
| t_dz is used by monadic functions such as satanh (from the
| transcendental function).
|
t_dz2:
	bsetb	#neg_bit,FPSR_CC(%a6)	|set neg bit in FPSR
	fmovel	#0,%FPSR			|clr status bits (Z set)
	btstb	#dz_bit,FPCR_ENABLE(%a6)	|test FPCR for dz exc enabled
	bnes	dz_ena_end
	bras	m_inf			|flogx always returns -inf
t_dz:
	fmovel	#0,%FPSR			|clr status bits (Z set)
	btstb	#dz_bit,FPCR_ENABLE(%a6)	|test FPCR for dz exc enabled
	bnes	dz_ena
|
|	dz disabled
|
	btstb	#sign_bit,ETEMP_EX(%a6)	|check sign for neg or pos
	beqs	p_inf			|branch if pos sign

m_inf:
	fmovemx mns_inf,%fp0-%fp0		|load -inf
	bsetb	#neg_bit,FPSR_CC(%a6)	|set neg bit in FPSR
	bras	set_fpsr
p_inf:
	fmovemx pls_inf,%fp0-%fp0		|load +inf
set_fpsr:
	orl	#dzinf_mask,USER_FPSR(%a6) |set I,DZ,ADZ
	rts
|
|	dz enabled
|
dz_ena:
	btstb	#sign_bit,ETEMP_EX(%a6)	|check sign for neg or pos
	beqs	dz_ena_end
	bsetb	#neg_bit,FPSR_CC(%a6)	|set neg bit in FPSR
dz_ena_end:
	orl	#dzinf_mask,USER_FPSR(%a6) |set I,DZ,ADZ
	st	STORE_FLG(%a6)
	rts
|
|	OPERR exception
|
|	if (operr trap disabled)
|		set FPSR exception status operr bit, condition code
|		nan bit; Store default NAN into fp0
|		frestore the frame into the machine (done by unimp_hd)
|
|	else (operr trap enabled)
|		set FPSR exception status operr bit, accrued operr bit
|		set flag to disable sto_res from corrupting fp register
|		frestore the frame into the machine (done by unimp_hd)
|
t_operr:
	orl	#opnan_mask,USER_FPSR(%a6) |set NaN, OPERR, AIOP

	btstb	#operr_bit,FPCR_ENABLE(%a6) |test FPCR for operr enabled
	bnes	op_ena

	fmovemx nan,%fp0-%fp0		|load default nan
	rts
op_ena:
	st	STORE_FLG(%a6)		|do not corrupt destination
	rts

|
|	t_unfl --- UNFL exception
|
| This entry point is used by all routines requiring unfl, inex2,
| aunfl, and ainex to be set on exit.
|
| On entry, a0 points to the exceptional operand.  The final exceptional
| operand is built in FP_SCR1 and only the sign from the original operand
| is used.
|
t_unfl:
	clrl	FP_SCR1(%a6)		|set exceptional operand to zero
	clrl	FP_SCR1+4(%a6)
	clrl	FP_SCR1+8(%a6)
	tstb	(%a0)			|extract sign from caller's exop
	bpls	unfl_signok
	bset	#sign_bit,FP_SCR1(%a6)
unfl_signok:
	leal	FP_SCR1(%a6),%a0
	orl	#unfinx_mask,USER_FPSR(%a6)
|					;set UNFL, INEX2, AUNFL, AINEX
unfl_con:
	btstb	#unfl_bit,FPCR_ENABLE(%a6)
	beqs	unfl_dis

unfl_ena:
	bfclr	STAG(%a6){#5:#3}		|clear wbtm66,wbtm1,wbtm0
	bsetb	#wbtemp15_bit,WB_BYTE(%a6) |set wbtemp15
	bsetb	#sticky_bit,STICKY(%a6)	|set sticky bit

	bclrb	#E1,E_BYTE(%a6)

unfl_dis:
	bfextu	FPCR_MODE(%a6){#0:#2},%d0	|get round precision

	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)		|convert to internal ext format

	bsr	unf_sub			|returns IEEE result at a0
|					;and sets FPSR_CC accordingly

	bfclr	LOCAL_SGN(%a0){#0:#8}	|convert back to IEEE ext format
	beqs	unfl_fin

	bsetb	#sign_bit,LOCAL_EX(%a0)
	bsetb	#sign_bit,FP_SCR1(%a6)	|set sign bit of exc operand

unfl_fin:
	fmovemx (%a0),%fp0-%fp0		|store result in fp0
	rts


|
|	t_ovfl2 --- OVFL exception (without inex2 returned)
|
| This entry is used by scale to force catastrophic overflow.  The
| ovfl, aovfl, and ainex bits are set, but not the inex2 bit.
|
t_ovfl2:
	orl	#ovfl_inx_mask,USER_FPSR(%a6)
	movel	ETEMP(%a6),FP_SCR1(%a6)
	movel	ETEMP_HI(%a6),FP_SCR1+4(%a6)
	movel	ETEMP_LO(%a6),FP_SCR1+8(%a6)
|
| Check for single or double round precision.  If single, check if
| the lower 40 bits of ETEMP are zero; if not, set inex2.  If double,
| check if the lower 21 bits are zero; if not, set inex2.
|
	moveb	FPCR_MODE(%a6),%d0
	andib	#0xc0,%d0
	beq	t_work		|if extended, finish ovfl processing
	cmpib	#0x40,%d0		|test for single
	bnes	t_dbl
t_sgl:
	tstb	ETEMP_LO(%a6)
	bnes	t_setinx2
	movel	ETEMP_HI(%a6),%d0
	andil	#0xff,%d0		|look at only lower 8 bits
	bnes	t_setinx2
	bra	t_work
t_dbl:
	movel	ETEMP_LO(%a6),%d0
	andil	#0x7ff,%d0	|look at only lower 11 bits
	beq	t_work
t_setinx2:
	orl	#inex2_mask,USER_FPSR(%a6)
	bras	t_work
|
|	t_ovfl --- OVFL exception
|
|** Note: the exc operand is returned in ETEMP.
|
t_ovfl:
	orl	#ovfinx_mask,USER_FPSR(%a6)
t_work:
	btstb	#ovfl_bit,FPCR_ENABLE(%a6) |test FPCR for ovfl enabled
	beqs	ovf_dis

ovf_ena:
	clrl	FP_SCR1(%a6)		|set exceptional operand
	clrl	FP_SCR1+4(%a6)
	clrl	FP_SCR1+8(%a6)

	bfclr	STAG(%a6){#5:#3}		|clear wbtm66,wbtm1,wbtm0
	bclrb	#wbtemp15_bit,WB_BYTE(%a6) |clear wbtemp15
	bsetb	#sticky_bit,STICKY(%a6)	|set sticky bit

	bclrb	#E1,E_BYTE(%a6)
|					;fall through to disabled case

| For disabled overflow call 'ovf_r_k'.  This routine loads the
| correct result based on the rounding precision, destination
| format, rounding mode and sign.
|
ovf_dis:
	bsr	ovf_r_k			|returns unsigned ETEMP_EX
|					;and sets FPSR_CC accordingly.
	bfclr	ETEMP_SGN(%a6){#0:#8}	|fix sign
	beqs	ovf_pos
	bsetb	#sign_bit,ETEMP_EX(%a6)
	bsetb	#sign_bit,FP_SCR1(%a6)	|set exceptional operand sign
ovf_pos:
	fmovemx ETEMP(%a6),%fp0-%fp0		|move the result to fp0
	rts


|
|	INEX2 exception
|
| The inex2 and ainex bits are set.
|
t_inx2:
	orl	#inx2a_mask,USER_FPSR(%a6) |set INEX2, AINEX
	rts

|
|	Force Inex2
|
| This routine is called by the transcendental routines to force
| the inex2 exception bits set in the FPSR.  If the underflow bit
| is set, but the underflow trap was not taken, the aunfl bit in
| the FPSR must be set.
|
t_frcinx:
	orl	#inx2a_mask,USER_FPSR(%a6) |set INEX2, AINEX
	btstb	#unfl_bit,FPSR_EXCEPT(%a6) |test for unfl bit set
	beqs	no_uacc1		|if clear, do not set aunfl
	bsetb	#aunfl_bit,FPSR_AEXCEPT(%a6)
no_uacc1:
	rts

|
|	DST_NAN
|
| Determine if the destination nan is signalling or non-signalling,
| and set the FPSR bits accordingly.  See the MC68040 User's Manual
| section 3.2.2.5 NOT-A-NUMBERS.
|
dst_nan:
	btstb	#sign_bit,FPTEMP_EX(%a6) |test sign of nan
	beqs	dst_pos			|if clr, it was positive
	bsetb	#neg_bit,FPSR_CC(%a6)	|set N bit
dst_pos:
	btstb	#signan_bit,FPTEMP_HI(%a6) |check if signalling
	beqs	dst_snan		|branch if signalling

	fmovel	%d1,%fpcr			|restore user's rmode/prec
	fmovex FPTEMP(%a6),%fp0		|return the non-signalling nan
|
| Check the source nan.  If it is signalling, snan will be reported.
|
	moveb	STAG(%a6),%d0
	andib	#0xe0,%d0
	cmpib	#0x60,%d0
	bnes	no_snan
	btstb	#signan_bit,ETEMP_HI(%a6) |check if signalling
	bnes	no_snan
	orl	#snaniop_mask,USER_FPSR(%a6) |set NAN, SNAN, AIOP
no_snan:
	rts

dst_snan:
	btstb	#snan_bit,FPCR_ENABLE(%a6) |check if trap enabled
	beqs	dst_dis			|branch if disabled

	orb	#nan_tag,DTAG(%a6)	|set up dtag for nan
	st	STORE_FLG(%a6)		|do not store a result
	orl	#snaniop_mask,USER_FPSR(%a6) |set NAN, SNAN, AIOP
	rts

dst_dis:
	bsetb	#signan_bit,FPTEMP_HI(%a6) |set SNAN bit in sop
	fmovel	%d1,%fpcr			|restore user's rmode/prec
	fmovex FPTEMP(%a6),%fp0		|load non-sign. nan
	orl	#snaniop_mask,USER_FPSR(%a6) |set NAN, SNAN, AIOP
	rts

|
|	SRC_NAN
|
| Determine if the source nan is signalling or non-signalling,
| and set the FPSR bits accordingly.  See the MC68040 User's Manual
| section 3.2.2.5 NOT-A-NUMBERS.
|
src_nan:
	btstb	#sign_bit,ETEMP_EX(%a6) |test sign of nan
	beqs	src_pos			|if clr, it was positive
	bsetb	#neg_bit,FPSR_CC(%a6)	|set N bit
src_pos:
	btstb	#signan_bit,ETEMP_HI(%a6) |check if signalling
	beqs	src_snan		|branch if signalling
	fmovel	%d1,%fpcr			|restore user's rmode/prec
	fmovex ETEMP(%a6),%fp0		|return the non-signalling nan
	rts

src_snan:
	btstb	#snan_bit,FPCR_ENABLE(%a6) |check if trap enabled
	beqs	src_dis			|branch if disabled
	bsetb	#signan_bit,ETEMP_HI(%a6) |set SNAN bit in sop
	orb	#norm_tag,DTAG(%a6)	|set up dtag for norm
	orb	#nan_tag,STAG(%a6)	|set up stag for nan
	st	STORE_FLG(%a6)		|do not store a result
	orl	#snaniop_mask,USER_FPSR(%a6) |set NAN, SNAN, AIOP
	rts

src_dis:
	bsetb	#signan_bit,ETEMP_HI(%a6) |set SNAN bit in sop
	fmovel	%d1,%fpcr			|restore user's rmode/prec
	fmovex ETEMP(%a6),%fp0		|load non-sign. nan
	orl	#snaniop_mask,USER_FPSR(%a6) |set NAN, SNAN, AIOP
	rts

|
| For all functions that have a denormalized input and that f(x)=x,
| this is the entry point
|
t_extdnrm:
	orl	#unfinx_mask,USER_FPSR(%a6)
|					;set UNFL, INEX2, AUNFL, AINEX
	bras	xdnrm_con
|
| Entry point for scale with extended denorm.  The function does
| not set inex2, aunfl, or ainex.
|
t_resdnrm:
	orl	#unfl_mask,USER_FPSR(%a6)

xdnrm_con:
	btstb	#unfl_bit,FPCR_ENABLE(%a6)
	beqs	xdnrm_dis

|
| If exceptions are enabled, the additional task of setting up WBTEMP
| is needed so that when the underflow exception handler is entered,
| the user perceives no difference between what the 040 provides vs.
| what the FPSP provides.
|
xdnrm_ena:
	movel	%a0,-(%a7)

	movel	LOCAL_EX(%a0),FP_SCR1(%a6)
	movel	LOCAL_HI(%a0),FP_SCR1+4(%a6)
	movel	LOCAL_LO(%a0),FP_SCR1+8(%a6)

	lea	FP_SCR1(%a6),%a0

	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)		|convert to internal ext format
	tstw	LOCAL_EX(%a0)		|check if input is denorm
	beqs	xdnrm_dn		|if so, skip nrm_set
	bsr	nrm_set			|normalize the result (exponent
|					;will be negative
xdnrm_dn:
	bclrb	#sign_bit,LOCAL_EX(%a0)	|take off false sign
	bfclr	LOCAL_SGN(%a0){#0:#8}	|change back to IEEE ext format
	beqs	xdep
	bsetb	#sign_bit,LOCAL_EX(%a0)
xdep:
	bfclr	STAG(%a6){#5:#3}		|clear wbtm66,wbtm1,wbtm0
	bsetb	#wbtemp15_bit,WB_BYTE(%a6) |set wbtemp15
	bclrb	#sticky_bit,STICKY(%a6)	|clear sticky bit
	bclrb	#E1,E_BYTE(%a6)
	movel	(%a7)+,%a0
xdnrm_dis:
	bfextu	FPCR_MODE(%a6){#0:#2},%d0	|get round precision
	bnes	not_ext			|if not round extended, store
|					;IEEE defaults
is_ext:
	btstb	#sign_bit,LOCAL_EX(%a0)
	beqs	xdnrm_store

	bsetb	#neg_bit,FPSR_CC(%a6)	|set N bit in FPSR_CC

	bras	xdnrm_store

not_ext:
	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)		|convert to internal ext format
	bsr	unf_sub			|returns IEEE result pointed by
|					;a0; sets FPSR_CC accordingly
	bfclr	LOCAL_SGN(%a0){#0:#8}	|convert back to IEEE ext format
	beqs	xdnrm_store
	bsetb	#sign_bit,LOCAL_EX(%a0)
xdnrm_store:
	fmovemx (%a0),%fp0-%fp0		|store result in fp0
	rts

|
| This subroutine is used for dyadic operations that use an extended
| denorm within the kernel. The approach used is to capture the frame,
| fix/restore.
|
	.global	t_avoid_unsupp
t_avoid_unsupp:
	link	%a2,#-LOCAL_SIZE		|so that a2 fpsp.h negative
|					;offsets may be used
	fsave	-(%a7)
	tstb	1(%a7)			|check if idle, exit if so
	beq	idle_end
	btstb	#E1,E_BYTE(%a2)		|check for an E1 exception if
|					;enabled, there is an unsupp
	beq	end_avun		|else, exit
	btstb	#7,DTAG(%a2)		|check for denorm destination
	beqs	src_den			|else, must be a source denorm
|
| handle destination denorm
|
	lea	FPTEMP(%a2),%a0
	btstb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)		|convert to internal ext format
	bclrb	#7,DTAG(%a2)		|set DTAG to norm
	bsr	nrm_set			|normalize result, exponent
|					;will become negative
	bclrb	#sign_bit,LOCAL_EX(%a0)	|get rid of fake sign
	bfclr	LOCAL_SGN(%a0){#0:#8}	|convert back to IEEE ext format
	beqs	ck_src_den		|check if source is also denorm
	bsetb	#sign_bit,LOCAL_EX(%a0)
ck_src_den:
	btstb	#7,STAG(%a2)
	beqs	end_avun
src_den:
	lea	ETEMP(%a2),%a0
	btstb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)		|convert to internal ext format
	bclrb	#7,STAG(%a2)		|set STAG to norm
	bsr	nrm_set			|normalize result, exponent
|					;will become negative
	bclrb	#sign_bit,LOCAL_EX(%a0)	|get rid of fake sign
	bfclr	LOCAL_SGN(%a0){#0:#8}	|convert back to IEEE ext format
	beqs	den_com
	bsetb	#sign_bit,LOCAL_EX(%a0)
den_com:
	moveb	#0xfe,CU_SAVEPC(%a2)	|set continue frame
	clrw	NMNEXC(%a2)		|clear NMNEXC
	bclrb	#E1,E_BYTE(%a2)
|	fmove.l	%FPSR,FPSR_SHADOW(%a2)
|	bset.b	#SFLAG,E_BYTE(%a2)
|	bset.b	#XFLAG,T_BYTE(%a2)
end_avun:
	frestore (%a7)+
	unlk	%a2
	rts
idle_end:
	addl	#4,%a7
	unlk	%a2
	rts
	|end
