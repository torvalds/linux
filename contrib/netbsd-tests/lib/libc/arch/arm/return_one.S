/*	$NetBSD: return_one.S,v 1.2 2014/01/26 20:42:06 matt Exp $ */

#include <machine/asm.h>

ENTRY_NP(return_one)
	mov	r0, #1
	RET
	.align	0
	.globl	return_one_end
return_one_end:
END(return_one)
