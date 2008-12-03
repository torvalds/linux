/* strlen.S: Sparc optimized strlen code
 * Hand optimized from GNU libc's strlen
 * Copyright (C) 1991,1996 Free Software Foundation
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#define LO_MAGIC 0x01010101
#define HI_MAGIC 0x80808080

0:
	ldub	[%o0], %o5
	cmp	%o5, 0
	be	1f
	 add	%o0, 1, %o0
	andcc	%o0, 3, %g0
	be	4f
	 or	%o4, %lo(HI_MAGIC), %o3
	ldub	[%o0], %o5
	cmp	%o5, 0
	be	2f
	 add	%o0, 1, %o0
	andcc	%o0, 3, %g0
	be	5f
	 sethi	%hi(LO_MAGIC), %o4
	ldub	[%o0], %o5
	cmp	%o5, 0
	be	3f
	 add	%o0, 1, %o0
	b	8f
	 or	%o4, %lo(LO_MAGIC), %o2
1:
	retl
	 mov	0, %o0
2:
	retl
	 mov	1, %o0
3:
	retl
	 mov	2, %o0

	.align 4
	.global strlen
strlen:
	mov	%o0, %o1
	andcc	%o0, 3, %g0
	bne	0b
	 sethi	%hi(HI_MAGIC), %o4
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
	be	8b
	 add	%o0, 4, %o0

	/* Check every byte. */
	srl	%o5, 24, %g5
	andcc	%g5, 0xff, %g0
	be	1f
	 add	%o0, -4, %o4
	srl	%o5, 16, %g5
	andcc	%g5, 0xff, %g0
	be	1f
	 add	%o4, 1, %o4
	srl	%o5, 8, %g5
	andcc	%g5, 0xff, %g0
	be	1f
	 add	%o4, 1, %o4
	andcc	%o5, 0xff, %g0
	bne,a	2b
	 ld	[%o0], %o5
	add	%o4, 1, %o4
1:
	retl
	 sub	%o4, %o1, %o0
