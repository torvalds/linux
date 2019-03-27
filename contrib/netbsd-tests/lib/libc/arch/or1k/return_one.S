/*	$NetBSD: return_one.S,v 1.1 2014/09/03 19:34:26 matt Exp $ */

#include <machine/asm.h>

.globl	return_one, return_one_end

ENTRY_NP(return_one)
	l.addi	r11, r0, 1
	l.jr	lr
	l.nop
return_one_end:
END(return_one)
