/* SPDX-License-Identifier: GPL-2.0 */
/*---------------------------------------------------------------------------+
 |  reg_norm.S                                                               |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1995,1997                                    |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail billm@suburbia.net               |
 |                                                                           |
 | Normalize the value in a FPU_REG.                                         |
 |                                                                           |
 | Call from C as:                                                           |
 |    int FPU_normalize(FPU_REG *n)                                          |
 |                                                                           |
 |    int FPU_normalize_nuo(FPU_REG *n)                                      |
 |                                                                           |
 |    Return value is the tag of the answer, or-ed with FPU_Exception if     |
 |    one was raised, or -1 on internal error.                               |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "fpu_emu.h"


.text
SYM_FUNC_START(FPU_normalize)
	pushl	%ebp
	movl	%esp,%ebp
	pushl	%ebx

	movl	PARAM1,%ebx

	movl	SIGH(%ebx),%edx
	movl	SIGL(%ebx),%eax

	orl	%edx,%edx	/* ms bits */
	js	L_done		/* Already normalized */
	jnz	L_shift_1	/* Shift left 1 - 31 bits */

	orl	%eax,%eax
	jz	L_zero		/* The contents are zero */

	movl	%eax,%edx
	xorl	%eax,%eax
	subw	$32,EXP(%ebx)	/* This can cause an underflow */

/* We need to shift left by 1 - 31 bits */
L_shift_1:
	bsrl	%edx,%ecx	/* get the required shift in %ecx */
	subl	$31,%ecx
	negl	%ecx
	shld	%cl,%eax,%edx
	shl	%cl,%eax
	subw	%cx,EXP(%ebx)	/* This can cause an underflow */

	movl	%edx,SIGH(%ebx)
	movl	%eax,SIGL(%ebx)

L_done:
	cmpw	EXP_OVER,EXP(%ebx)
	jge	L_overflow

	cmpw	EXP_UNDER,EXP(%ebx)
	jle	L_underflow

L_exit_valid:
	movl	TAG_Valid,%eax

	/* Convert the exponent to 80x87 form. */
	addw	EXTENDED_Ebias,EXP(%ebx)
	andw	$0x7fff,EXP(%ebx)

L_exit:
	popl	%ebx
	leave
	RET


L_zero:
	movw	$0,EXP(%ebx)
	movl	TAG_Zero,%eax
	jmp	L_exit

L_underflow:
	/* Convert the exponent to 80x87 form. */
	addw	EXTENDED_Ebias,EXP(%ebx)
	push	%ebx
	call	arith_underflow
	pop	%ebx
	jmp	L_exit

L_overflow:
	/* Convert the exponent to 80x87 form. */
	addw	EXTENDED_Ebias,EXP(%ebx)
	push	%ebx
	call	arith_overflow
	pop	%ebx
	jmp	L_exit
SYM_FUNC_END(FPU_normalize)



/* Normalise without reporting underflow or overflow */
SYM_FUNC_START(FPU_normalize_nuo)
	pushl	%ebp
	movl	%esp,%ebp
	pushl	%ebx

	movl	PARAM1,%ebx

	movl	SIGH(%ebx),%edx
	movl	SIGL(%ebx),%eax

	orl	%edx,%edx	/* ms bits */
	js	L_exit_nuo_valid	/* Already normalized */
	jnz	L_nuo_shift_1	/* Shift left 1 - 31 bits */

	orl	%eax,%eax
	jz	L_exit_nuo_zero		/* The contents are zero */

	movl	%eax,%edx
	xorl	%eax,%eax
	subw	$32,EXP(%ebx)	/* This can cause an underflow */

/* We need to shift left by 1 - 31 bits */
L_nuo_shift_1:
	bsrl	%edx,%ecx	/* get the required shift in %ecx */
	subl	$31,%ecx
	negl	%ecx
	shld	%cl,%eax,%edx
	shl	%cl,%eax
	subw	%cx,EXP(%ebx)	/* This can cause an underflow */

	movl	%edx,SIGH(%ebx)
	movl	%eax,SIGL(%ebx)

L_exit_nuo_valid:
	movl	TAG_Valid,%eax

	popl	%ebx
	leave
	RET

L_exit_nuo_zero:
	movl	TAG_Zero,%eax
	movw	EXP_UNDER,EXP(%ebx)

	popl	%ebx
	leave
	RET
SYM_FUNC_END(FPU_normalize_nuo)
