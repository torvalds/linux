/* SPDX-License-Identifier: GPL-2.0 */
/* Sparc optimized memcmp code.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 2000, 2008 David S. Miller (davem@davemloft.net)
 */

#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/asm.h>

	.text
ENTRY(memcmp)
	cmp	%o2, 0
1:	BRANCH32(be, pn, 2f)
	 nop
	ldub	[%o0], %g7
	ldub	[%o1], %g3
	sub	%o2, 1, %o2
	add	%o0, 1, %o0
	add	%o1, 1, %o1
	subcc	%g7, %g3, %g3
	BRANCH32(be, pt, 1b)
	 cmp	%o2, 0
	retl
	 mov	%g3, %o0
2:	retl
	 mov	0, %o0
ENDPROC(memcmp)
EXPORT_SYMBOL(memcmp)
