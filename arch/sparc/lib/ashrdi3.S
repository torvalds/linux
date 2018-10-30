/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ashrdi3.S:	The filesystem code creates all kinds of references to
 *              this little routine on the sparc with gcc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/linkage.h>
#include <asm/export.h>

	.text
ENTRY(__ashrdi3)
	tst	%o2
	be	3f
	 or	%g0, 32, %g2

	sub	%g2, %o2, %g2

	tst	%g2
	bg	1f
	 sra	%o0, %o2, %o4

	sra	%o0, 31, %o4
	sub	%g0, %g2, %g2
	ba	2f
	 sra	%o0, %g2, %o5

1:
	sll	%o0, %g2, %g3
	srl	%o1, %o2, %g2
	or	%g2, %g3, %o5
2:
	or	%g0, %o4, %o0
	or	%g0, %o5, %o1
3:
	jmpl	%o7 + 8, %g0
	 nop
ENDPROC(__ashrdi3)
EXPORT_SYMBOL(__ashrdi3)
