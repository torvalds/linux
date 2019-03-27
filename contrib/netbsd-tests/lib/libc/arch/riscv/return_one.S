/*	$NetBSD: return_one.S,v 1.2 2015/03/28 07:07:54 matt Exp $ */

#include <machine/asm.h>

	.globl	return_one_end

ENTRY_NP(return_one)
	li	a0, 1
	ret
return_one_end:
END(return_one)
