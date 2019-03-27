/*	$NetBSD: h_initfini_align.S,v 1.2 2010/12/12 18:21:21 joerg Exp $	*/

#include <machine/asm.h>

RCSID("$NetBSD: h_initfini_align.S,v 1.2 2010/12/12 18:21:21 joerg Exp $")

_ENTRY(check_stack_alignment)
	movl	%esp, %eax
	andl	$15, %eax
	subl	$8, %eax
	sete	%al
	movzbl	%al, %eax
	ret
