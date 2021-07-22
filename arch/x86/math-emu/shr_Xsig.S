/* SPDX-License-Identifier: GPL-2.0 */
	.file	"shr_Xsig.S"
/*---------------------------------------------------------------------------+
 |  shr_Xsig.S                                                               |
 |                                                                           |
 | 12 byte right shift function                                              |
 |                                                                           |
 | Copyright (C) 1992,1994,1995                                              |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail billm@jacobi.maths.monash.edu.au |
 |                                                                           |
 | Call from C as:                                                           |
 |   void shr_Xsig(Xsig *arg, unsigned nr)                                   |
 |                                                                           |
 |   Extended shift right function.                                          |
 |   Fastest for small shifts.                                               |
 |   Shifts the 12 byte quantity pointed to by the first arg (arg)           |
 |   right by the number of bits specified by the second arg (nr).           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "fpu_emu.h"

.text
SYM_FUNC_START(shr_Xsig)
	push	%ebp
	movl	%esp,%ebp
	pushl	%esi
	movl	PARAM2,%ecx
	movl	PARAM1,%esi
	cmpl	$32,%ecx	/* shrd only works for 0..31 bits */
	jnc	L_more_than_31

/* less than 32 bits */
	pushl	%ebx
	movl	(%esi),%eax	/* lsl */
	movl	4(%esi),%ebx	/* midl */
	movl	8(%esi),%edx	/* msl */
	shrd	%cl,%ebx,%eax
	shrd	%cl,%edx,%ebx
	shr	%cl,%edx
	movl	%eax,(%esi)
	movl	%ebx,4(%esi)
	movl	%edx,8(%esi)
	popl	%ebx
	popl	%esi
	leave
	ret

L_more_than_31:
	cmpl	$64,%ecx
	jnc	L_more_than_63

	subb	$32,%cl
	movl	4(%esi),%eax	/* midl */
	movl	8(%esi),%edx	/* msl */
	shrd	%cl,%edx,%eax
	shr	%cl,%edx
	movl	%eax,(%esi)
	movl	%edx,4(%esi)
	movl	$0,8(%esi)
	popl	%esi
	leave
	ret

L_more_than_63:
	cmpl	$96,%ecx
	jnc	L_more_than_95

	subb	$64,%cl
	movl	8(%esi),%eax	/* msl */
	shr	%cl,%eax
	xorl	%edx,%edx
	movl	%eax,(%esi)
	movl	%edx,4(%esi)
	movl	%edx,8(%esi)
	popl	%esi
	leave
	ret

L_more_than_95:
	xorl	%eax,%eax
	movl	%eax,(%esi)
	movl	%eax,4(%esi)
	movl	%eax,8(%esi)
	popl	%esi
	leave
	ret
SYM_FUNC_END(shr_Xsig)
