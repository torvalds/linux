/* SPDX-License-Identifier: GPL-2.0 */
	.file	"div_small.S"
/*---------------------------------------------------------------------------+
 |  div_small.S                                                              |
 |                                                                           |
 | Divide a 64 bit integer by a 32 bit integer & return remainder.           |
 |                                                                           |
 | Copyright (C) 1992,1995                                                   |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail billm@jacobi.maths.monash.edu.au |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 |    unsigned long FPU_div_small(unsigned long long *x, unsigned long y)    |
 +---------------------------------------------------------------------------*/

#include "fpu_emu.h"

.text
SYM_FUNC_START(FPU_div_small)
	pushl	%ebp
	movl	%esp,%ebp

	pushl	%esi

	movl	PARAM1,%esi	/* pointer to num */
	movl	PARAM2,%ecx	/* The denominator */

	movl	4(%esi),%eax	/* Get the current num msw */
	xorl	%edx,%edx
	divl	%ecx

	movl	%eax,4(%esi)

	movl	(%esi),%eax	/* Get the num lsw */
	divl	%ecx

	movl	%eax,(%esi)

	movl	%edx,%eax	/* Return the remainder in eax */

	popl	%esi

	leave
	ret
SYM_FUNC_END(FPU_div_small)
