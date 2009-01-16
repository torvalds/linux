/* strlen.S: Sparc optimized strlen code
 * Hand optimized from GNU libc's strlen
 * Copyright (C) 1991,1996 Free Software Foundation
 * Copyright (C) 1996,2008 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996, 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/linkage.h>
#include <asm/asm.h>

#define LO_MAGIC 0x01010101
#define HI_MAGIC 0x80808080

	.text
ENTRY(strlen)
	mov	%o0, %o1
	andcc	%o0, 3, %g0
	BRANCH32(be, pt, 9f)
	 sethi	%hi(HI_MAGIC), %o4
	ldub	[%o0], %o5
	BRANCH_REG_ZERO(pn, %o5, 11f)
	 add	%o0, 1, %o0
	andcc	%o0, 3, %g0
	BRANCH32(be, pn, 4f)
	 or	%o4, %lo(HI_MAGIC), %o3
	ldub	[%o0], %o5
	BRANCH_REG_ZERO(pn, %o5, 12f)
	 add	%o0, 1, %o0
	andcc	%o0, 3, %g0
	BRANCH32(be, pt, 5f)
	 sethi	%hi(LO_MAGIC), %o4
	ldub	[%o0], %o5
	BRANCH_REG_ZERO(pn, %o5, 13f)
	 add	%o0, 1, %o0
	BRANCH32(ba, pt, 8f)
	 or	%o4, %lo(LO_MAGIC), %o2
9:
	or	%o4, %lo(HI_MAGIC), %o3
4:
	sethi	%hi(LO_MAGIC), %o4
5:
	or	%o4, %lo(LO_MAGIC), %o2
8:
	ld	[%o0], %o5
2:
	sub	%o5, %o2, %o4
	andcc	%o4, %o3, %g0
	BRANCH32(be, pt, 8b)
	 add	%o0, 4, %o0

	/* Check every byte. */
	srl	%o5, 24, %g7
	andcc	%g7, 0xff, %g0
	BRANCH32(be, pn, 1f)
	 add	%o0, -4, %o4
	srl	%o5, 16, %g7
	andcc	%g7, 0xff, %g0
	BRANCH32(be, pn, 1f)
	 add	%o4, 1, %o4
	srl	%o5, 8, %g7
	andcc	%g7, 0xff, %g0
	BRANCH32(be, pn, 1f)
	 add	%o4, 1, %o4
	andcc	%o5, 0xff, %g0
	BRANCH32_ANNUL(bne, pt, 2b)
	 ld	[%o0], %o5
	add	%o4, 1, %o4
1:
	retl
	 sub	%o4, %o1, %o0
11:
	retl
	 mov	0, %o0
12:
	retl
	 mov	1, %o0
13:
	retl
	 mov	2, %o0
ENDPROC(strlen)
