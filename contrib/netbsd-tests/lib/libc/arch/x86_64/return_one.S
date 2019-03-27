/*	$NetBSD: return_one.S,v 1.1 2011/07/18 23:16:11 jym Exp $ */

#include <machine/asm.h>

RCSID("$NetBSD: return_one.S,v 1.1 2011/07/18 23:16:11 jym Exp $");

_ENTRY(return_one)
	movq	$0x1, %rax
	retq
LABEL(return_one_end)
