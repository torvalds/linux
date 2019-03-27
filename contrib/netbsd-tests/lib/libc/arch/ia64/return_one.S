/*	$NetBSD: return_one.S,v 1.2 2016/08/05 15:02:29 scole Exp $ */

#include <machine/asm.h>

.globl	return_one_end	

ENTRY(return_one,0)
	mov	ret0=1
	br.ret.sptk.few  rp
return_one_end:	
END(return_one)
