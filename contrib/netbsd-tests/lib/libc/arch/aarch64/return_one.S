/* $NetBSD: return_one.S,v 1.1 2014/08/10 05:47:38 matt Exp $ */

#include <machine/asm.h>

ENTRY_NP(return_one)
	mov	x0, #1
	ret
	.globl	return_one_end
return_one_end:
END(return_one)
