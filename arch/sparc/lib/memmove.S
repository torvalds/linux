/* SPDX-License-Identifier: GPL-2.0 */
/* memmove.S: Simple memmove implementation.
 *
 * Copyright (C) 1997, 2004 David S. Miller (davem@redhat.com)
 * Copyright (C) 1996, 1997, 1998, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

#include <linux/linkage.h>
#include <asm/export.h>

	.text
ENTRY(memmove) /* o0=dst o1=src o2=len */
	brz,pn		%o2, 99f
	 mov		%o0, %g1

	cmp		%o0, %o1
	bleu,pt		%xcc, 2f
	 add		%o1, %o2, %g7
	cmp		%g7, %o0
	bleu,pt		%xcc, memcpy
	 add		%o0, %o2, %o5
	sub		%g7, 1, %o1

	sub		%o5, 1, %o0
1:	ldub		[%o1], %g7
	subcc		%o2, 1, %o2
	sub		%o1, 1, %o1
	stb		%g7, [%o0]
	bne,pt		%icc, 1b
	 sub		%o0, 1, %o0
99:
	retl
	 mov		%g1, %o0

	/* We can't just call memcpy for these memmove cases.  On some
	 * chips the memcpy uses cache initializing stores and when dst
	 * and src are close enough, those can clobber the source data
	 * before we've loaded it in.
	 */
2:	or		%o0, %o1, %g7
	or		%o2, %g7, %g7
	andcc		%g7, 0x7, %g0
	bne,pn		%xcc, 4f
	 nop

3:	ldx		[%o1], %g7
	add		%o1, 8, %o1
	subcc		%o2, 8, %o2
	add		%o0, 8, %o0
	bne,pt		%icc, 3b
	 stx		%g7, [%o0 - 0x8]
	ba,a,pt		%xcc, 99b

4:	ldub		[%o1], %g7
	add		%o1, 1, %o1
	subcc		%o2, 1, %o2
	add		%o0, 1, %o0
	bne,pt		%icc, 4b
	 stb		%g7, [%o0 - 0x1]
	ba,a,pt		%xcc, 99b
ENDPROC(memmove)
EXPORT_SYMBOL(memmove)
