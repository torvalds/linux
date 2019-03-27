/*	$NetBSD: h_initfini_align.S,v 1.1 2013/12/11 17:31:56 matt Exp $	*/

#include <machine/asm.h>

RCSID("$NetBSD: h_initfini_align.S,v 1.1 2013/12/11 17:31:56 matt Exp $")

/*
 * LINTSTUB: bool check_stack_alignment(void);
 */

_ENTRY(check_stack_alignment, 0)
	clrl	%r0
	bitl	$3,%sp
	bneq	1f
	incl	%r0
1:	ret
END(check_stack_alignment)
