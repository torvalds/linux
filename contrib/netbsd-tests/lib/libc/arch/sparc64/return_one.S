/*	$NetBSD: return_one.S,v 1.2 2016/12/31 11:51:20 martin Exp $ */

#include <machine/asm.h>

.global	return_one_end

ENTRY(return_one)
return_one:
	retl
	 mov 1, %o0
return_one_end:
