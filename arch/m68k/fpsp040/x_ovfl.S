|
|	x_ovfl.sa 3.5 7/1/91
|
|	fpsp_ovfl --- FPSP handler for overflow exception
|
|	Overflow occurs when a floating-point intermediate result is
|	too large to be represented in a floating-point data register,
|	or when storing to memory, the contents of a floating-point
|	data register are too large to be represented in the
|	destination format.
|
| Trap disabled results
|
| If the instruction is move_out, then garbage is stored in the
| destination.  If the instruction is not move_out, then the
| destination is not affected.  For 68881 compatibility, the
| following values should be stored at the destination, based
| on the current rounding mode:
|
|  RN	Infinity with the sign of the intermediate result.
|  RZ	Largest magnitude number, with the sign of the
|	intermediate result.
|  RM   For pos overflow, the largest pos number. For neg overflow,
|	-infinity
|  RP   For pos overflow, +infinity. For neg overflow, the largest
|	neg number
|
| Trap enabled results
| All trap disabled code applies.  In addition the exceptional
| operand needs to be made available to the users exception handler
| with a bias of $6000 subtracted from the exponent.
|
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

X_OVFL:	|idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

	|xref	ovf_r_x2
	|xref	ovf_r_x3
	|xref	store
	|xref	real_ovfl
	|xref	real_inex
	|xref	fpsp_done
	|xref	g_opcls
	|xref	b1238_fix

	.global	fpsp_ovfl
fpsp_ovfl:
	link		%a6,#-LOCAL_SIZE
	fsave		-(%a7)
	moveml		%d0-%d1/%a0-%a1,USER_DA(%a6)
	fmovemx	%fp0-%fp3,USER_FP0(%a6)
	fmoveml	%fpcr/%fpsr/%fpiar,USER_FPCR(%a6)

|
|	The 040 doesn't set the AINEX bit in the FPSR, the following
|	line temporarily rectifies this error.
|
	bsetb	#ainex_bit,FPSR_AEXCEPT(%a6)
|
	bsrl	ovf_adj		|denormalize, round & store interm op
|
|	if overflow traps not enabled check for inexact exception
|
	btstb	#ovfl_bit,FPCR_ENABLE(%a6)
	beqs	ck_inex
|
	btstb		#E3,E_BYTE(%a6)
	beqs		no_e3_1
	bfextu		CMDREG3B(%a6){#6:#3},%d0	|get dest reg no
	bclrb		%d0,FPR_DIRTY_BITS(%a6)	|clr dest dirty bit
	bsrl		b1238_fix
	movel		USER_FPSR(%a6),FPSR_SHADOW(%a6)
	orl		#sx_mask,E_BYTE(%a6)
no_e3_1:
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore	(%a7)+
	unlk		%a6
	bral		real_ovfl
|
| It is possible to have either inex2 or inex1 exceptions with the
| ovfl.  If the inex enable bit is set in the FPCR, and either
| inex2 or inex1 occurred, we must clean up and branch to the
| real inex handler.
|
ck_inex:
|	move.b		FPCR_ENABLE(%a6),%d0
|	and.b		FPSR_EXCEPT(%a6),%d0
|	andi.b		#$3,%d0
	btstb		#inex2_bit,FPCR_ENABLE(%a6)
	beqs		ovfl_exit
|
| Inexact enabled and reported, and we must take an inexact exception.
|
take_inex:
	btstb		#E3,E_BYTE(%a6)
	beqs		no_e3_2
	bfextu		CMDREG3B(%a6){#6:#3},%d0	|get dest reg no
	bclrb		%d0,FPR_DIRTY_BITS(%a6)	|clr dest dirty bit
	bsrl		b1238_fix
	movel		USER_FPSR(%a6),FPSR_SHADOW(%a6)
	orl		#sx_mask,E_BYTE(%a6)
no_e3_2:
	moveb		#INEX_VEC,EXC_VEC+1(%a6)
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore	(%a7)+
	unlk		%a6
	bral		real_inex

ovfl_exit:
	bclrb	#E3,E_BYTE(%a6)	|test and clear E3 bit
	beqs	e1_set
|
| Clear dirty bit on dest resister in the frame before branching
| to b1238_fix.
|
	bfextu		CMDREG3B(%a6){#6:#3},%d0	|get dest reg no
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
|	ovf_adj
|
ovf_adj:
|
| Have a0 point to the correct operand.
|
	btstb	#E3,E_BYTE(%a6)	|test E3 bit
	beqs	ovf_e1

	lea	WBTEMP(%a6),%a0
	bras	ovf_com
ovf_e1:
	lea	ETEMP(%a6),%a0

ovf_com:
	bclrb	#sign_bit,LOCAL_EX(%a0)
	sne	LOCAL_SGN(%a0)

	bsrl	g_opcls		|returns opclass in d0
	cmpiw	#3,%d0		|check for opclass3
	bnes	not_opc011

|
| FPSR_CC is saved and restored because ovf_r_x3 affects it. The
| CCs are defined to be 'not affected' for the opclass3 instruction.
|
	moveb	FPSR_CC(%a6),L_SCR1(%a6)
	bsrl	ovf_r_x3	|returns a0 pointing to result
	moveb	L_SCR1(%a6),FPSR_CC(%a6)
	bral	store		|stores to memory or register

not_opc011:
	bsrl	ovf_r_x2	|returns a0 pointing to result
	bral	store		|stores to memory or register

	|end
