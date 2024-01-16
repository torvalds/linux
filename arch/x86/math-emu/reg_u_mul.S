/* SPDX-License-Identifier: GPL-2.0 */
	.file	"reg_u_mul.S"
/*---------------------------------------------------------------------------+
 |  reg_u_mul.S                                                              |
 |                                                                           |
 | Core multiplication routine                                               |
 |                                                                           |
 | Copyright (C) 1992,1993,1995,1997                                         |
 |                  W. Metzenthen, 22 Parker St, Ormond, Vic 3163, Australia |
 |                  E-mail   billm@suburbia.net                              |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 |   Basic multiplication routine.                                           |
 |   Does not check the resulting exponent for overflow/underflow            |
 |                                                                           |
 |   FPU_u_mul(FPU_REG *a, FPU_REG *b, FPU_REG *c, unsigned int cw);         |
 |                                                                           |
 |   Internal working is at approx 128 bits.                                 |
 |   Result is rounded to nearest 53 or 64 bits, using "nearest or even".    |
 +---------------------------------------------------------------------------*/

#include "exception.h"
#include "fpu_emu.h"
#include "control_w.h"



#ifndef NON_REENTRANT_FPU
/*  Local storage on the stack: */
#define FPU_accum_0	-4(%ebp)	/* ms word */
#define FPU_accum_1	-8(%ebp)

#else
/*  Local storage in a static area: */
.data
	.align 4,0
FPU_accum_0:
	.long	0
FPU_accum_1:
	.long	0
#endif /* NON_REENTRANT_FPU */


.text
SYM_FUNC_START(FPU_u_mul)
	pushl	%ebp
	movl	%esp,%ebp
#ifndef NON_REENTRANT_FPU
	subl	$8,%esp
#endif /* NON_REENTRANT_FPU */ 

	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	PARAM1,%esi
	movl	PARAM2,%edi

#ifdef PARANOID
	testl	$0x80000000,SIGH(%esi)
	jz	L_bugged
	testl	$0x80000000,SIGH(%edi)
	jz	L_bugged
#endif /* PARANOID */

	xorl	%ecx,%ecx
	xorl	%ebx,%ebx

	movl	SIGL(%esi),%eax
	mull	SIGL(%edi)
	movl	%eax,FPU_accum_0
	movl	%edx,FPU_accum_1

	movl	SIGL(%esi),%eax
	mull	SIGH(%edi)
	addl	%eax,FPU_accum_1
	adcl	%edx,%ebx
/*	adcl	$0,%ecx		// overflow here is not possible */

	movl	SIGH(%esi),%eax
	mull	SIGL(%edi)
	addl	%eax,FPU_accum_1
	adcl	%edx,%ebx
	adcl	$0,%ecx

	movl	SIGH(%esi),%eax
	mull	SIGH(%edi)
	addl	%eax,%ebx
	adcl	%edx,%ecx

	/* Get the sum of the exponents. */
	movl	PARAM6,%eax
	subl	EXP_BIAS-1,%eax

	/* Two denormals can cause an exponent underflow */
	cmpl	EXP_WAY_UNDER,%eax
	jg	Exp_not_underflow

	/* Set to a really low value allow correct handling */
	movl	EXP_WAY_UNDER,%eax

Exp_not_underflow:

/*  Have now finished with the sources */
	movl	PARAM3,%edi	/* Point to the destination */
	movw	%ax,EXP(%edi)

/*  Now make sure that the result is normalized */
	testl	$0x80000000,%ecx
	jnz	LResult_Normalised

	/* Normalize by shifting left one bit */
	shll	$1,FPU_accum_0
	rcll	$1,FPU_accum_1
	rcll	$1,%ebx
	rcll	$1,%ecx
	decw	EXP(%edi)

LResult_Normalised:
	movl	FPU_accum_0,%eax
	movl	FPU_accum_1,%edx
	orl	%eax,%eax
	jz	L_extent_zero

	orl	$1,%edx

L_extent_zero:
	movl	%ecx,%eax
	jmp	fpu_reg_round


#ifdef PARANOID
L_bugged:
	pushl	EX_INTERNAL|0x205
	call	EXCEPTION
	pop	%ebx
	jmp	L_exit

L_exit:
	popl	%ebx
	popl	%edi
	popl	%esi
	leave
	RET
#endif /* PARANOID */ 

SYM_FUNC_END(FPU_u_mul)
