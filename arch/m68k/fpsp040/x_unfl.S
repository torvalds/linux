|
|	x_unfl.sa 3.4 7/1/91
|
|	fpsp_unfl --- FPSP handler for underflow exception
|
| Trap disabled results
|	For 881/2 compatibility, sw must denormalize the intermediate
| result, then store the result.  Denormalization is accomplished
| by taking the intermediate result (which is always normalized) and
| shifting the mantissa right while incrementing the exponent until
| it is equal to the denormalized exponent for the destination
| format.  After denormalization, the result is rounded to the
| destination format.
|
| Trap enabled results
|	All trap disabled code applies.	In addition the exceptional
| operand needs to made available to the user with a bias of $6000
| added to the exponent.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MOTOROLA
|	The copyright notice above does not evidence any
|	actual or intended publication of such source code.

X_UNFL:	|idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

	|xref	denorm
	|xref	round
	|xref	store
	|xref	g_rndpr
	|xref	g_opcls
	|xref	g_dfmtou
	|xref	real_unfl
	|xref	real_inex
	|xref	fpsp_done
	|xref	b1238_fix

	.global	fpsp_unfl
fpsp_unfl:
	link		%a6,#-LOCAL_SIZE
	fsave		-(%a7)
	moveml		%d0-%d1/%a0-%a1,USER_DA(%a6)
	fmovemx	%fp0-%fp3,USER_FP0(%a6)
	fmoveml	%fpcr/%fpsr/%fpiar,USER_FPCR(%a6)

|
	bsrl		unf_res	|denormalize, round & store interm op
|
| If underflow exceptions are not enabled, check for inexact
| exception
|
	btstb		#unfl_bit,FPCR_ENABLE(%a6)
	beqs		ck_inex

	btstb		#E3,E_BYTE(%a6)
	beqs		no_e3_1
|
| Clear dirty bit on dest resister in the frame before branching
| to b1238_fix.
|
	bfextu		CMDREG3B(%a6){#6:#3},%d0	|get dest reg no
	bclrb		%d0,FPR_DIRTY_BITS(%a6)	|clr dest dirty bit
	bsrl		b1238_fix		|test for bug1238 case
	movel		USER_FPSR(%a6),FPSR_SHADOW(%a6)
	orl		#sx_mask,E_BYTE(%a6)
no_e3_1:
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore	(%a7)+
	unlk		%a6
	bral		real_unfl
|
| It is possible to have either inex2 or inex1 exceptions with the
| unfl.  If the inex enable bit is set in the FPCR, and either
| inex2 or inex1 occurred, we must clean up and branch to the
| real inex handler.
|
ck_inex:
	moveb		FPCR_ENABLE(%a6),%d0
	andb		FPSR_EXCEPT(%a6),%d0
	andib		#0x3,%d0
	beqs		unfl_done

|
| Inexact enabled and reported, and we must take an inexact exception
|
take_inex:
	btstb		#E3,E_BYTE(%a6)
	beqs		no_e3_2
|
| Clear dirty bit on dest resister in the frame before branching
| to b1238_fix.
|
	bfextu		CMDREG3B(%a6){#6:#3},%d0	|get dest reg no
	bclrb		%d0,FPR_DIRTY_BITS(%a6)	|clr dest dirty bit
	bsrl		b1238_fix		|test for bug1238 case
	movel		USER_FPSR(%a6),FPSR_SHADOW(%a6)
	orl		#sx_mask,E_BYTE(%a6)
no_e3_2:
	moveb		#INEX_VEC,EXC_VEC+1(%a6)
	moveml         USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx        USER_FP0(%a6),%fp0-%fp3
	fmoveml        USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore        (%a7)+
	unlk            %a6
	bral		real_inex

unfl_done:
	bclrb		#E3,E_BYTE(%a6)
	beqs		e1_set		|if set then branch
|
| Clear dirty bit on dest resister in the frame before branching
| to b1238_fix.
|
	bfextu		CMDREG3B(%a6){#6:#3},%d0		|get dest reg no
	bclrb		%d0,FPR_DIRTY_BITS(%a6)	|clr dest dirty bit
	bsrl		b1238_fix		|test for bug1238 case
	movel		USER_FPSR(%a6),FPSR_SHADOW(%a6)
	orl		#sx_mask,E_BYTE(%a6)
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore	(%a7)+
	unlk		%a6
	bral		fpsp_done
e1_set:
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	unlk		%a6
	bral		fpsp_done
|
|	unf_res --- underflow result calculation
|
unf_res:
	bsrl		g_rndpr		|returns RND_PREC in d0 0=ext,
|					;1=sgl, 2=dbl
|					;we need the RND_PREC in the
|					;upper word for round
	movew		#0,-(%a7)
	movew		%d0,-(%a7)	|copy RND_PREC to stack
|
|
| If the exception bit set is E3, the exceptional operand from the
| fpu is in WBTEMP; else it is in FPTEMP.
|
	btstb		#E3,E_BYTE(%a6)
	beqs		unf_E1
unf_E3:
	lea		WBTEMP(%a6),%a0	|a0 now points to operand
|
| Test for fsgldiv and fsglmul.  If the inst was one of these, then
| force the precision to extended for the denorm routine.  Use
| the user's precision for the round routine.
|
	movew		CMDREG3B(%a6),%d1	|check for fsgldiv or fsglmul
	andiw		#0x7f,%d1
	cmpiw		#0x30,%d1		|check for sgldiv
	beqs		unf_sgl
	cmpiw		#0x33,%d1		|check for sglmul
	bnes		unf_cont	|if not, use fpcr prec in round
unf_sgl:
	clrl		%d0
	movew		#0x1,(%a7)	|override g_rndpr precision
|					;force single
	bras		unf_cont
unf_E1:
	lea		FPTEMP(%a6),%a0	|a0 now points to operand
unf_cont:
	bclrb		#sign_bit,LOCAL_EX(%a0)	|clear sign bit
	sne		LOCAL_SGN(%a0)		|store sign

	bsrl		denorm		|returns denorm, a0 points to it
|
| WARNING:
|				;d0 has guard,round sticky bit
|				;make sure that it is not corrupted
|				;before it reaches the round subroutine
|				;also ensure that a0 isn't corrupted

|
| Set up d1 for round subroutine d1 contains the PREC/MODE
| information respectively on upper/lower register halves.
|
	bfextu		FPCR_MODE(%a6){#2:#2},%d1	|get mode from FPCR
|						;mode in lower d1
	addl		(%a7)+,%d1		|merge PREC/MODE
|
| WARNING: a0 and d0 are assumed to be intact between the denorm and
| round subroutines. All code between these two subroutines
| must not corrupt a0 and d0.
|
|
| Perform Round
|	Input:		a0 points to input operand
|			d0{31:29} has guard, round, sticky
|			d1{01:00} has rounding mode
|			d1{17:16} has rounding precision
|	Output:		a0 points to rounded operand
|

	bsrl		round		|returns rounded denorm at (a0)
|
| Differentiate between store to memory vs. store to register
|
unf_store:
	bsrl		g_opcls		|returns opclass in d0{2:0}
	cmpib		#0x3,%d0
	bnes		not_opc011
|
| At this point, a store to memory is pending
|
opc011:
	bsrl		g_dfmtou
	tstb		%d0
	beqs		ext_opc011	|If extended, do not subtract
|				;If destination format is sgl/dbl,
	tstb		LOCAL_HI(%a0)	|If rounded result is normal,don't
|					;subtract
	bmis		ext_opc011
	subqw		#1,LOCAL_EX(%a0)	|account for denorm bias vs.
|				;normalized bias
|				;          normalized   denormalized
|				;single       $7f           $7e
|				;double       $3ff          $3fe
|
ext_opc011:
	bsrl		store		|stores to memory
	bras		unf_done	|finish up

|
| At this point, a store to a float register is pending
|
not_opc011:
	bsrl		store	|stores to float register
|				;a0 is not corrupted on a store to a
|				;float register.
|
| Set the condition codes according to result
|
	tstl		LOCAL_HI(%a0)	|check upper mantissa
	bnes		ck_sgn
	tstl		LOCAL_LO(%a0)	|check lower mantissa
	bnes		ck_sgn
	bsetb		#z_bit,FPSR_CC(%a6) |set condition codes if zero
ck_sgn:
	btstb		#sign_bit,LOCAL_EX(%a0)	|check the sign bit
	beqs		unf_done
	bsetb		#neg_bit,FPSR_CC(%a6)

|
| Finish.
|
unf_done:
	btstb		#inex2_bit,FPSR_EXCEPT(%a6)
	beqs		no_aunfl
	bsetb		#aunfl_bit,FPSR_AEXCEPT(%a6)
no_aunfl:
	rts

	|end
