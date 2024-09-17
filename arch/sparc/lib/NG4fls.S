/* NG4fls.S: SPARC optimized fls and __fls for T4 and above.
 *
 * Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
 */

#include <linux/linkage.h>

#define LZCNT_O0_G2	\
	.word	0x85b002e8

	.text
	.register	%g2, #scratch
	.register	%g3, #scratch

ENTRY(NG4fls)
	LZCNT_O0_G2	!lzcnt	%o0, %g2
	mov	64, %g3
	retl
	 sub	%g3, %g2, %o0
ENDPROC(NG4fls)

ENTRY(__NG4fls)
	brz,pn	%o0, 1f
	LZCNT_O0_G2	!lzcnt	%o0, %g2
	mov	63, %g3
	sub	%g3, %g2, %o0
1:
	retl
	 nop
ENDPROC(__NG4fls)
