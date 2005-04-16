/* $Id: ashldi3.S,v 1.1 1998/04/06 16:09:28 jj Exp $
 * ashldi3.S:	Math-emu code creates all kinds of references to
 *              this little routine on the sparc with gcc.
 *
 * Copyright (C) 1998 Jakub Jelinek(jj@ultra.linux.cz)
 */

#include <asm/cprefix.h>

	.globl C_LABEL(__ashldi3)
C_LABEL(__ashldi3):
	tst	%o2
	be	3f
	 mov	32, %g2

	sub	%g2, %o2, %g2

	tst	%g2
	bg	1f
	 srl	%o1, %g2, %g3

	clr	%o5
	neg	%g2
	ba	2f
	 sll	%o1, %g2, %o4

1:
	sll	%o1, %o2, %o5
	srl	%o0, %o2, %g2
	or	%g2, %g3, %o4
2:
	mov	%o4, %o0
	mov	%o5, %o1
3:
	jmpl	%o7 + 8, %g0
	 nop
