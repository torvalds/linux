/*	$NetBSD: h_initfini_align.S,v 1.1 2013/11/15 20:00:20 martin Exp $	*/

#include <machine/asm.h>

RCSID("$NetBSD: h_initfini_align.S,v 1.1 2013/11/15 20:00:20 martin Exp $")

_ENTRY(check_stack_alignment)
	add	%sp, BIAS, %o1
	and	%o1, 15, %o2
	retl
	 not	%o2, %o0
