/* SPDX-License-Identifier: GPL-2.0 */
	.file	"div_Xsig.S"
/*---------------------------------------------------------------------------+
 |  div_Xsig.S                                                               |
 |                                                                           |
 | Division subroutine for 96 bit quantities                                 |
 |                                                                           |
 | Copyright (C) 1994,1995                                                   |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail billm@jacobi.maths.monash.edu.au |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 | Divide the 96 bit quantity pointed to by a, by that pointed to by b, and  |
 | put the 96 bit result at the location d.                                  |
 |                                                                           |
 | The result may not be accurate to 96 bits. It is intended for use where   |
 | a result better than 64 bits is required. The result should usually be    |
 | good to at least 94 bits.                                                 |
 | The returned result is actually divided by one half. This is done to      |
 | prevent overflow.                                                         |
 |                                                                           |
 |  .aaaaaaaaaaaaaa / .bbbbbbbbbbbbb  ->  .dddddddddddd                      |
 |                                                                           |
 |  void div_Xsig(Xsig *a, Xsig *b, Xsig *dest)                              |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "exception.h"
#include "fpu_emu.h"


#define	XsigLL(x)	(x)
#define	XsigL(x)	4(x)
#define	XsigH(x)	8(x)


#ifndef NON_REENTRANT_FPU
/*
	Local storage on the stack:
	Accumulator:	FPU_accum_3:FPU_accum_2:FPU_accum_1:FPU_accum_0
 */
#define FPU_accum_3	-4(%ebp)
#define FPU_accum_2	-8(%ebp)
#define FPU_accum_1	-12(%ebp)
#define FPU_accum_0	-16(%ebp)
#define FPU_result_3	-20(%ebp)
#define FPU_result_2	-24(%ebp)
#define FPU_result_1	-28(%ebp)

#else
.data
/*
	Local storage in a static area:
	Accumulator:	FPU_accum_3:FPU_accum_2:FPU_accum_1:FPU_accum_0
 */
	.align 4,0
FPU_accum_3:
	.long	0
FPU_accum_2:
	.long	0
FPU_accum_1:
	.long	0
FPU_accum_0:
	.long	0
FPU_result_3:
	.long	0
FPU_result_2:
	.long	0
FPU_result_1:
	.long	0
#endif /* NON_REENTRANT_FPU */


.text
SYM_FUNC_START(div_Xsig)
	pushl	%ebp
	movl	%esp,%ebp
#ifndef NON_REENTRANT_FPU
	subl	$28,%esp
#endif /* NON_REENTRANT_FPU */ 

	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	PARAM1,%esi	/* pointer to num */
	movl	PARAM2,%ebx	/* pointer to denom */

#ifdef PARANOID
	testl	$0x80000000, XsigH(%ebx)	/* Divisor */
	je	L_bugged
#endif /* PARANOID */


/*---------------------------------------------------------------------------+
 |  Divide:   Return  arg1/arg2 to arg3.                                     |
 |                                                                           |
 |  The maximum returned value is (ignoring exponents)                       |
 |               .ffffffff ffffffff                                          |
 |               ------------------  =  1.ffffffff fffffffe                  |
 |               .80000000 00000000                                          |
 | and the minimum is                                                        |
 |               .80000000 00000000                                          |
 |               ------------------  =  .80000000 00000001   (rounded)       |
 |               .ffffffff ffffffff                                          |
 |                                                                           |
 +---------------------------------------------------------------------------*/

	/* Save extended dividend in local register */

	/* Divide by 2 to prevent overflow */
	clc
	movl	XsigH(%esi),%eax
	rcrl	%eax
	movl	%eax,FPU_accum_3
	movl	XsigL(%esi),%eax
	rcrl	%eax
	movl	%eax,FPU_accum_2
	movl	XsigLL(%esi),%eax
	rcrl	%eax
	movl	%eax,FPU_accum_1
	movl	$0,%eax
	rcrl	%eax
	movl	%eax,FPU_accum_0

	movl	FPU_accum_2,%eax	/* Get the current num */
	movl	FPU_accum_3,%edx

/*----------------------------------------------------------------------*/
/* Initialization done.
   Do the first 32 bits. */

	/* We will divide by a number which is too large */
	movl	XsigH(%ebx),%ecx
	addl	$1,%ecx
	jnc	LFirst_div_not_1

	/* here we need to divide by 100000000h,
	   i.e., no division at all.. */
	mov	%edx,%eax
	jmp	LFirst_div_done

LFirst_div_not_1:
	divl	%ecx		/* Divide the numerator by the augmented
				   denom ms dw */

LFirst_div_done:
	movl	%eax,FPU_result_3	/* Put the result in the answer */

	mull	XsigH(%ebx)	/* mul by the ms dw of the denom */

	subl	%eax,FPU_accum_2	/* Subtract from the num local reg */
	sbbl	%edx,FPU_accum_3

	movl	FPU_result_3,%eax	/* Get the result back */
	mull	XsigL(%ebx)	/* now mul the ls dw of the denom */

	subl	%eax,FPU_accum_1	/* Subtract from the num local reg */
	sbbl	%edx,FPU_accum_2
	sbbl	$0,FPU_accum_3
	je	LDo_2nd_32_bits		/* Must check for non-zero result here */

#ifdef PARANOID
	jb	L_bugged_1
#endif /* PARANOID */ 

	/* need to subtract another once of the denom */
	incl	FPU_result_3	/* Correct the answer */

	movl	XsigL(%ebx),%eax
	movl	XsigH(%ebx),%edx
	subl	%eax,FPU_accum_1	/* Subtract from the num local reg */
	sbbl	%edx,FPU_accum_2

#ifdef PARANOID
	sbbl	$0,FPU_accum_3
	jne	L_bugged_1	/* Must check for non-zero result here */
#endif /* PARANOID */ 

/*----------------------------------------------------------------------*/
/* Half of the main problem is done, there is just a reduced numerator
   to handle now.
   Work with the second 32 bits, FPU_accum_0 not used from now on */
LDo_2nd_32_bits:
	movl	FPU_accum_2,%edx	/* get the reduced num */
	movl	FPU_accum_1,%eax

	/* need to check for possible subsequent overflow */
	cmpl	XsigH(%ebx),%edx
	jb	LDo_2nd_div
	ja	LPrevent_2nd_overflow

	cmpl	XsigL(%ebx),%eax
	jb	LDo_2nd_div

LPrevent_2nd_overflow:
/* The numerator is greater or equal, would cause overflow */
	/* prevent overflow */
	subl	XsigL(%ebx),%eax
	sbbl	XsigH(%ebx),%edx
	movl	%edx,FPU_accum_2
	movl	%eax,FPU_accum_1

	incl	FPU_result_3	/* Reflect the subtraction in the answer */

#ifdef PARANOID
	je	L_bugged_2	/* Can't bump the result to 1.0 */
#endif /* PARANOID */ 

LDo_2nd_div:
	cmpl	$0,%ecx		/* augmented denom msw */
	jnz	LSecond_div_not_1

	/* %ecx == 0, we are dividing by 1.0 */
	mov	%edx,%eax
	jmp	LSecond_div_done

LSecond_div_not_1:
	divl	%ecx		/* Divide the numerator by the denom ms dw */

LSecond_div_done:
	movl	%eax,FPU_result_2	/* Put the result in the answer */

	mull	XsigH(%ebx)	/* mul by the ms dw of the denom */

	subl	%eax,FPU_accum_1	/* Subtract from the num local reg */
	sbbl	%edx,FPU_accum_2

#ifdef PARANOID
	jc	L_bugged_2
#endif /* PARANOID */

	movl	FPU_result_2,%eax	/* Get the result back */
	mull	XsigL(%ebx)	/* now mul the ls dw of the denom */

	subl	%eax,FPU_accum_0	/* Subtract from the num local reg */
	sbbl	%edx,FPU_accum_1	/* Subtract from the num local reg */
	sbbl	$0,FPU_accum_2

#ifdef PARANOID
	jc	L_bugged_2
#endif /* PARANOID */

	jz	LDo_3rd_32_bits

#ifdef PARANOID
	cmpl	$1,FPU_accum_2
	jne	L_bugged_2
#endif /* PARANOID */ 

	/* need to subtract another once of the denom */
	movl	XsigL(%ebx),%eax
	movl	XsigH(%ebx),%edx
	subl	%eax,FPU_accum_0	/* Subtract from the num local reg */
	sbbl	%edx,FPU_accum_1
	sbbl	$0,FPU_accum_2

#ifdef PARANOID
	jc	L_bugged_2
	jne	L_bugged_2
#endif /* PARANOID */ 

	addl	$1,FPU_result_2	/* Correct the answer */
	adcl	$0,FPU_result_3

#ifdef PARANOID
	jc	L_bugged_2	/* Must check for non-zero result here */
#endif /* PARANOID */ 

/*----------------------------------------------------------------------*/
/* The division is essentially finished here, we just need to perform
   tidying operations.
   Deal with the 3rd 32 bits */
LDo_3rd_32_bits:
	/* We use an approximation for the third 32 bits.
	To take account of the 3rd 32 bits of the divisor
	(call them del), we subtract  del * (a/b) */

	movl	FPU_result_3,%eax	/* a/b */
	mull	XsigLL(%ebx)		/* del */

	subl	%edx,FPU_accum_1

	/* A borrow indicates that the result is negative */
	jnb	LTest_over

	movl	XsigH(%ebx),%edx
	addl	%edx,FPU_accum_1

	subl	$1,FPU_result_2		/* Adjust the answer */
	sbbl	$0,FPU_result_3

	/* The above addition might not have been enough, check again. */
	movl	FPU_accum_1,%edx	/* get the reduced num */
	cmpl	XsigH(%ebx),%edx	/* denom */
	jb	LDo_3rd_div

	movl	XsigH(%ebx),%edx
	addl	%edx,FPU_accum_1

	subl	$1,FPU_result_2		/* Adjust the answer */
	sbbl	$0,FPU_result_3
	jmp	LDo_3rd_div

LTest_over:
	movl	FPU_accum_1,%edx	/* get the reduced num */

	/* need to check for possible subsequent overflow */
	cmpl	XsigH(%ebx),%edx	/* denom */
	jb	LDo_3rd_div

	/* prevent overflow */
	subl	XsigH(%ebx),%edx
	movl	%edx,FPU_accum_1

	addl	$1,FPU_result_2	/* Reflect the subtraction in the answer */
	adcl	$0,FPU_result_3

LDo_3rd_div:
	movl	FPU_accum_0,%eax
	movl	FPU_accum_1,%edx
	divl	XsigH(%ebx)

	movl    %eax,FPU_result_1       /* Rough estimate of third word */

	movl	PARAM3,%esi		/* pointer to answer */

	movl	FPU_result_1,%eax
	movl	%eax,XsigLL(%esi)
	movl	FPU_result_2,%eax
	movl	%eax,XsigL(%esi)
	movl	FPU_result_3,%eax
	movl	%eax,XsigH(%esi)

L_exit:
	popl	%ebx
	popl	%edi
	popl	%esi

	leave
	RET


#ifdef PARANOID
/* The logic is wrong if we got here */
L_bugged:
	pushl	EX_INTERNAL|0x240
	call	EXCEPTION
	pop	%ebx
	jmp	L_exit

L_bugged_1:
	pushl	EX_INTERNAL|0x241
	call	EXCEPTION
	pop	%ebx
	jmp	L_exit

L_bugged_2:
	pushl	EX_INTERNAL|0x242
	call	EXCEPTION
	pop	%ebx
	jmp	L_exit
#endif /* PARANOID */ 
SYM_FUNC_END(div_Xsig)
