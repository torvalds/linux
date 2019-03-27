/*	$NetBSD: return_one.S,v 1.3 2015/03/29 00:38:36 matt Exp $ */

#include <machine/asm.h>

.globl	return_one_start, return_one_end

_ENTRY(return_one)
return_one_start:
	li	%r3, 1
	blr
return_one_end:
END(return_one)
