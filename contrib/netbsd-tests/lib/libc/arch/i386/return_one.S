/*	$NetBSD: return_one.S,v 1.1 2011/07/18 23:16:09 jym Exp $ */

#include <machine/asm.h>

RCSID("$NetBSD: return_one.S,v 1.1 2011/07/18 23:16:09 jym Exp $");

_ENTRY(return_one)
	movl	$0x1,%eax
	ret
LABEL(return_one_end)
