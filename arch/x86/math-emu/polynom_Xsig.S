/* SPDX-License-Identifier: GPL-2.0 */
/*---------------------------------------------------------------------------+
 |  polynomial_Xsig.S                                                        |
 |                                                                           |
 | Fixed point arithmetic polynomial evaluation.                             |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1995                                         |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail billm@jacobi.maths.monash.edu.au |
 |                                                                           |
 | Call from C as:                                                           |
 |   void polynomial_Xsig(Xsig *accum, unsigned long long x,                 |
 |                        unsigned long long terms[], int n)                 |
 |                                                                           |
 | Computes:                                                                 |
 | terms[0] + (terms[1] + (terms[2] + ... + (terms[n-1]*x)*x)*x)*x) ... )*x  |
 | and adds the result to the 12 byte Xsig.                                  |
 | The terms[] are each 8 bytes, but all computation is performed to 12 byte |
 | precision.                                                                |
 |                                                                           |
 | This function must be used carefully: most overflow of intermediate       |
 | results is controlled, but overflow of the result is not.                 |
 |                                                                           |
 +---------------------------------------------------------------------------*/
	.file	"polynomial_Xsig.S"

#include "fpu_emu.h"


#define	TERM_SIZE	$8
#define	SUM_MS		-20(%ebp)	/* sum ms long */
#define SUM_MIDDLE	-24(%ebp)	/* sum middle long */
#define	SUM_LS		-28(%ebp)	/* sum ls long */
#define	ACCUM_MS	-4(%ebp)	/* accum ms long */
#define	ACCUM_MIDDLE	-8(%ebp)	/* accum middle long */
#define	ACCUM_LS	-12(%ebp)	/* accum ls long */
#define OVERFLOWED      -16(%ebp)	/* addition overflow flag */

.text
ENTRY(polynomial_Xsig)
	pushl	%ebp
	movl	%esp,%ebp
	subl	$32,%esp
	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	PARAM2,%esi		/* x */
	movl	PARAM3,%edi		/* terms */

	movl	TERM_SIZE,%eax
	mull	PARAM4			/* n */
	addl	%eax,%edi

	movl	4(%edi),%edx		/* terms[n] */
	movl	%edx,SUM_MS
	movl	(%edi),%edx		/* terms[n] */
	movl	%edx,SUM_MIDDLE
	xor	%eax,%eax
	movl	%eax,SUM_LS
	movb	%al,OVERFLOWED

	subl	TERM_SIZE,%edi
	decl	PARAM4
	js	L_accum_done

L_accum_loop:
	xor	%eax,%eax
	movl	%eax,ACCUM_MS
	movl	%eax,ACCUM_MIDDLE

	movl	SUM_MIDDLE,%eax
	mull	(%esi)			/* x ls long */
	movl	%edx,ACCUM_LS

	movl	SUM_MIDDLE,%eax
	mull	4(%esi)			/* x ms long */
	addl	%eax,ACCUM_LS
	adcl	%edx,ACCUM_MIDDLE
	adcl	$0,ACCUM_MS

	movl	SUM_MS,%eax
	mull	(%esi)			/* x ls long */
	addl	%eax,ACCUM_LS
	adcl	%edx,ACCUM_MIDDLE
	adcl	$0,ACCUM_MS

	movl	SUM_MS,%eax
	mull	4(%esi)			/* x ms long */
	addl	%eax,ACCUM_MIDDLE
	adcl	%edx,ACCUM_MS

	testb	$0xff,OVERFLOWED
	jz	L_no_overflow

	movl	(%esi),%eax
	addl	%eax,ACCUM_MIDDLE
	movl	4(%esi),%eax
	adcl	%eax,ACCUM_MS		/* This could overflow too */

L_no_overflow:

/*
 * Now put the sum of next term and the accumulator
 * into the sum register
 */
	movl	ACCUM_LS,%eax
	addl	(%edi),%eax		/* term ls long */
	movl	%eax,SUM_LS
	movl	ACCUM_MIDDLE,%eax
	adcl	(%edi),%eax		/* term ls long */
	movl	%eax,SUM_MIDDLE
	movl	ACCUM_MS,%eax
	adcl	4(%edi),%eax		/* term ms long */
	movl	%eax,SUM_MS
	sbbb	%al,%al
	movb	%al,OVERFLOWED		/* Used in the next iteration */

	subl	TERM_SIZE,%edi
	decl	PARAM4
	jns	L_accum_loop

L_accum_done:
	movl	PARAM1,%edi		/* accum */
	movl	SUM_LS,%eax
	addl	%eax,(%edi)
	movl	SUM_MIDDLE,%eax
	adcl	%eax,4(%edi)
	movl	SUM_MS,%eax
	adcl	%eax,8(%edi)

	popl	%ebx
	popl	%edi
	popl	%esi
	leave
	ret
ENDPROC(polynomial_Xsig)
