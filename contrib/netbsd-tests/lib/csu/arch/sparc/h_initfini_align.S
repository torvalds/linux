/*	$NetBSD: h_initfini_align.S,v 1.1 2013/12/12 11:22:26 martin Exp $	*/

#include <machine/asm.h>

RCSID("$NetBSD: h_initfini_align.S,v 1.1 2013/12/12 11:22:26 martin Exp $")

_ENTRY(check_stack_alignment)
	and	%sp, 7, %o1
	retl
	 not	%o1, %o0
