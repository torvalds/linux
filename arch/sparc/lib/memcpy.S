/* memcpy.S: Sparc optimized memcpy and memmove code
 * Hand optimized from GNU libc's memcpy and memmove
 * Copyright (C) 1991,1996 Free Software Foundation
 * Copyright (C) 1995 Linus Torvalds (Linus.Torvalds@helsinki.fi)
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#define FUNC(x) 		\
	.globl	x;		\
	.type	x,@function;	\
	.align	4;		\
x:

/* Both these macros have to start with exactly the same insn */
#define MOVE_BIGCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7) \
	ldd	[%src + (offset) + 0x00], %t0; \
	ldd	[%src + (offset) + 0x08], %t2; \
	ldd	[%src + (offset) + 0x10], %t4; \
	ldd	[%src + (offset) + 0x18], %t6; \
	st	%t0, [%dst + (offset) + 0x00]; \
	st	%t1, [%dst + (offset) + 0x04]; \
	st	%t2, [%dst + (offset) + 0x08]; \
	st	%t3, [%dst + (offset) + 0x0c]; \
	st	%t4, [%dst + (offset) + 0x10]; \
	st	%t5, [%dst + (offset) + 0x14]; \
	st	%t6, [%dst + (offset) + 0x18]; \
	st	%t7, [%dst + (offset) + 0x1c];

#define MOVE_BIGALIGNCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7) \
	ldd	[%src + (offset) + 0x00], %t0; \
	ldd	[%src + (offset) + 0x08], %t2; \
	ldd	[%src + (offset) + 0x10], %t4; \
	ldd	[%src + (offset) + 0x18], %t6; \
	std	%t0, [%dst + (offset) + 0x00]; \
	std	%t2, [%dst + (offset) + 0x08]; \
	std	%t4, [%dst + (offset) + 0x10]; \
	std	%t6, [%dst + (offset) + 0x18];

#define MOVE_LASTCHUNK(src, dst, offset, t0, t1, t2, t3) \
	ldd	[%src - (offset) - 0x10], %t0; \
	ldd	[%src - (offset) - 0x08], %t2; \
	st	%t0, [%dst - (offset) - 0x10]; \
	st	%t1, [%dst - (offset) - 0x0c]; \
	st	%t2, [%dst - (offset) - 0x08]; \
	st	%t3, [%dst - (offset) - 0x04];

#define MOVE_LASTALIGNCHUNK(src, dst, offset, t0, t1, t2, t3) \
	ldd	[%src - (offset) - 0x10], %t0; \
	ldd	[%src - (offset) - 0x08], %t2; \
	std	%t0, [%dst - (offset) - 0x10]; \
	std	%t2, [%dst - (offset) - 0x08];

#define MOVE_SHORTCHUNK(src, dst, offset, t0, t1) \
	ldub	[%src - (offset) - 0x02], %t0; \
	ldub	[%src - (offset) - 0x01], %t1; \
	stb	%t0, [%dst - (offset) - 0x02]; \
	stb	%t1, [%dst - (offset) - 0x01];

/* Both these macros have to start with exactly the same insn */
#define RMOVE_BIGCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7) \
	ldd	[%src - (offset) - 0x20], %t0; \
	ldd	[%src - (offset) - 0x18], %t2; \
	ldd	[%src - (offset) - 0x10], %t4; \
	ldd	[%src - (offset) - 0x08], %t6; \
	st	%t0, [%dst - (offset) - 0x20]; \
	st	%t1, [%dst - (offset) - 0x1c]; \
	st	%t2, [%dst - (offset) - 0x18]; \
	st	%t3, [%dst - (offset) - 0x14]; \
	st	%t4, [%dst - (offset) - 0x10]; \
	st	%t5, [%dst - (offset) - 0x0c]; \
	st	%t6, [%dst - (offset) - 0x08]; \
	st	%t7, [%dst - (offset) - 0x04];

#define RMOVE_BIGALIGNCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7) \
	ldd	[%src - (offset) - 0x20], %t0; \
	ldd	[%src - (offset) - 0x18], %t2; \
	ldd	[%src - (offset) - 0x10], %t4; \
	ldd	[%src - (offset) - 0x08], %t6; \
	std	%t0, [%dst - (offset) - 0x20]; \
	std	%t2, [%dst - (offset) - 0x18]; \
	std	%t4, [%dst - (offset) - 0x10]; \
	std	%t6, [%dst - (offset) - 0x08];

#define RMOVE_LASTCHUNK(src, dst, offset, t0, t1, t2, t3) \
	ldd	[%src + (offset) + 0x00], %t0; \
	ldd	[%src + (offset) + 0x08], %t2; \
	st	%t0, [%dst + (offset) + 0x00]; \
	st	%t1, [%dst + (offset) + 0x04]; \
	st	%t2, [%dst + (offset) + 0x08]; \
	st	%t3, [%dst + (offset) + 0x0c];

#define RMOVE_SHORTCHUNK(src, dst, offset, t0, t1) \
	ldub	[%src + (offset) + 0x00], %t0; \
	ldub	[%src + (offset) + 0x01], %t1; \
	stb	%t0, [%dst + (offset) + 0x00]; \
	stb	%t1, [%dst + (offset) + 0x01];

#define SMOVE_CHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, prev, shil, shir, offset2) \
	ldd	[%src + (offset) + 0x00], %t0; \
	ldd	[%src + (offset) + 0x08], %t2; \
	srl	%t0, shir, %t5; \
	srl	%t1, shir, %t6; \
	sll	%t0, shil, %t0; \
	or	%t5, %prev, %t5; \
	sll	%t1, shil, %prev; \
	or	%t6, %t0, %t0; \
	srl	%t2, shir, %t1; \
	srl	%t3, shir, %t6; \
	sll	%t2, shil, %t2; \
	or	%t1, %prev, %t1; \
	std	%t4, [%dst + (offset) + (offset2) - 0x04]; \
	std	%t0, [%dst + (offset) + (offset2) + 0x04]; \
	sll	%t3, shil, %prev; \
	or	%t6, %t2, %t4;

#define SMOVE_ALIGNCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, prev, shil, shir, offset2) \
	ldd	[%src + (offset) + 0x00], %t0; \
	ldd	[%src + (offset) + 0x08], %t2; \
	srl	%t0, shir, %t4;	\
	srl	%t1, shir, %t5;	\
	sll	%t0, shil, %t6;	\
	or	%t4, %prev, %t0; \
	sll	%t1, shil, %prev; \
	or	%t5, %t6, %t1; \
	srl	%t2, shir, %t4;	\
	srl	%t3, shir, %t5;	\
	sll	%t2, shil, %t6; \
	or	%t4, %prev, %t2; \
	sll	%t3, shil, %prev; \
	or	%t5, %t6, %t3; \
	std	%t0, [%dst + (offset) + (offset2) + 0x00]; \
	std	%t2, [%dst + (offset) + (offset2) + 0x08];

	.text
	.align	4

0:
	retl
	 nop		! Only bcopy returns here and it retuns void...

#ifdef __KERNEL__
FUNC(amemmove)
FUNC(__memmove)
#endif
FUNC(memmove)
	cmp		%o0, %o1
	mov		%o0, %g7
	bleu		9f
	 sub		%o0, %o1, %o4

	add		%o1, %o2, %o3
	cmp		%o3, %o0
	bleu		0f
	 andcc		%o4, 3, %o5

	add		%o1, %o2, %o1
	add		%o0, %o2, %o0
	sub		%o1, 1, %o1
	sub		%o0, 1, %o0
	
1:	/* reverse_bytes */

	ldub		[%o1], %o4
	subcc		%o2, 1, %o2
	stb		%o4, [%o0]
	sub		%o1, 1, %o1
	bne		1b
	 sub		%o0, 1, %o0

	retl
	 mov		%g7, %o0

/* NOTE: This code is executed just for the cases,
         where %src (=%o1) & 3 is != 0.
	 We need to align it to 4. So, for (%src & 3)
	 1 we need to do ldub,lduh
	 2 lduh
	 3 just ldub
         so even if it looks weird, the branches
         are correct here. -jj
 */
78:	/* dword_align */

	andcc		%o1, 1, %g0
	be		4f
	 andcc		%o1, 2, %g0

	ldub		[%o1], %g2
	add		%o1, 1, %o1
	stb		%g2, [%o0]
	sub		%o2, 1, %o2
	bne		3f
	 add		%o0, 1, %o0
4:
	lduh		[%o1], %g2
	add		%o1, 2, %o1
	sth		%g2, [%o0]
	sub		%o2, 2, %o2
	b		3f
	 add		%o0, 2, %o0

FUNC(memcpy)	/* %o0=dst %o1=src %o2=len */

	sub		%o0, %o1, %o4
	mov		%o0, %g7
9:
	andcc		%o4, 3, %o5
0:
	bne		86f
	 cmp		%o2, 15

	bleu		90f
	 andcc		%o1, 3, %g0

	bne		78b
3:
	 andcc		%o1, 4, %g0

	be		2f
	 mov		%o2, %g1

	ld		[%o1], %o4
	sub		%g1, 4, %g1
	st		%o4, [%o0]
	add		%o1, 4, %o1
	add		%o0, 4, %o0
2:
	andcc		%g1, 0xffffff80, %g0
	be		3f
	 andcc		%o0, 4, %g0

	be		82f + 4
5:
	MOVE_BIGCHUNK(o1, o0, 0x00, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x20, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x40, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x60, o2, o3, o4, o5, g2, g3, g4, g5)
	sub		%g1, 128, %g1
	add		%o1, 128, %o1
	cmp		%g1, 128
	bge		5b
	 add		%o0, 128, %o0
3:
	andcc		%g1, 0x70, %g4
	be		80f
	 andcc		%g1, 8, %g0

	sethi		%hi(80f), %o5
	srl		%g4, 1, %o4
	add		%g4, %o4, %o4
	add		%o1, %g4, %o1
	sub		%o5, %o4, %o5
	jmpl		%o5 + %lo(80f), %g0
	 add		%o0, %g4, %o0

79:	/* memcpy_table */

	MOVE_LASTCHUNK(o1, o0, 0x60, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x50, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x40, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x30, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x20, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x10, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x00, g2, g3, g4, g5)

80:	/* memcpy_table_end */
	be		81f
	 andcc		%g1, 4, %g0

	ldd		[%o1], %g2
	add		%o0, 8, %o0
	st		%g2, [%o0 - 0x08]
	add		%o1, 8, %o1
	st		%g3, [%o0 - 0x04]

81:	/* memcpy_last7 */

	be		1f
	 andcc		%g1, 2, %g0

	ld		[%o1], %g2
	add		%o1, 4, %o1
	st		%g2, [%o0]
	add		%o0, 4, %o0
1:
	be		1f
	 andcc		%g1, 1, %g0

	lduh		[%o1], %g2
	add		%o1, 2, %o1
	sth		%g2, [%o0]
	add		%o0, 2, %o0
1:
	be		1f
	 nop

	ldub		[%o1], %g2
	stb		%g2, [%o0]
1:
	retl
	 mov		%g7, %o0

82:	/* ldd_std */
	MOVE_BIGALIGNCHUNK(o1, o0, 0x00, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x20, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x40, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x60, o2, o3, o4, o5, g2, g3, g4, g5)
	subcc		%g1, 128, %g1
	add		%o1, 128, %o1
	cmp		%g1, 128
	bge		82b
	 add		%o0, 128, %o0

	andcc		%g1, 0x70, %g4
	be		84f
	 andcc		%g1, 8, %g0

	sethi		%hi(84f), %o5
	add		%o1, %g4, %o1
	sub		%o5, %g4, %o5
	jmpl		%o5 + %lo(84f), %g0
	 add		%o0, %g4, %o0

83:	/* amemcpy_table */

	MOVE_LASTALIGNCHUNK(o1, o0, 0x60, g2, g3, g4, g5)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x50, g2, g3, g4, g5)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x40, g2, g3, g4, g5)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x30, g2, g3, g4, g5)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x20, g2, g3, g4, g5)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x10, g2, g3, g4, g5)
	MOVE_LASTALIGNCHUNK(o1, o0, 0x00, g2, g3, g4, g5)

84:	/* amemcpy_table_end */
	be		85f
	 andcc		%g1, 4, %g0

	ldd		[%o1], %g2
	add		%o0, 8, %o0
	std		%g2, [%o0 - 0x08]
	add		%o1, 8, %o1
85:	/* amemcpy_last7 */
	be		1f
	 andcc		%g1, 2, %g0

	ld		[%o1], %g2
	add		%o1, 4, %o1
	st		%g2, [%o0]
	add		%o0, 4, %o0
1:
	be		1f
	 andcc		%g1, 1, %g0

	lduh		[%o1], %g2
	add		%o1, 2, %o1
	sth		%g2, [%o0]
	add		%o0, 2, %o0
1:
	be		1f
	 nop

	ldub		[%o1], %g2
	stb		%g2, [%o0]
1:
	retl
	 mov		%g7, %o0

86:	/* non_aligned */
	cmp		%o2, 6
	bleu		88f
	 nop

	save		%sp, -96, %sp
	andcc		%i0, 3, %g0
	be		61f
	 andcc		%i0, 1, %g0
	be		60f
	 andcc		%i0, 2, %g0

	ldub		[%i1], %g5
	add		%i1, 1, %i1
	stb		%g5, [%i0]
	sub		%i2, 1, %i2
	bne		61f
	 add		%i0, 1, %i0
60:
	ldub		[%i1], %g3
	add		%i1, 2, %i1
	stb		%g3, [%i0]
	sub		%i2, 2, %i2
	ldub		[%i1 - 1], %g3
	add		%i0, 2, %i0
	stb		%g3, [%i0 - 1]
61:
	and		%i1, 3, %g2
	and		%i2, 0xc, %g3
	and		%i1, -4, %i1
	cmp		%g3, 4
	sll		%g2, 3, %g4
	mov		32, %g2
	be		4f
	 sub		%g2, %g4, %l0
	
	blu		3f
	 cmp		%g3, 0x8

	be		2f
	 srl		%i2, 2, %g3

	ld		[%i1], %i3
	add		%i0, -8, %i0
	ld		[%i1 + 4], %i4
	b		8f
	 add		%g3, 1, %g3
2:
	ld		[%i1], %i4
	add		%i0, -12, %i0
	ld		[%i1 + 4], %i5
	add		%g3, 2, %g3
	b		9f
	 add		%i1, -4, %i1
3:
	ld		[%i1], %g1
	add		%i0, -4, %i0
	ld		[%i1 + 4], %i3
	srl		%i2, 2, %g3
	b		7f
	 add		%i1, 4, %i1
4:
	ld		[%i1], %i5
	cmp		%i2, 7
	ld		[%i1 + 4], %g1
	srl		%i2, 2, %g3
	bleu		10f
	 add		%i1, 8, %i1

	ld		[%i1], %i3
	add		%g3, -1, %g3
5:
	sll		%i5, %g4, %g2
	srl		%g1, %l0, %g5
	or		%g2, %g5, %g2
	st		%g2, [%i0]
7:
	ld		[%i1 + 4], %i4
	sll		%g1, %g4, %g2
	srl		%i3, %l0, %g5
	or		%g2, %g5, %g2
	st		%g2, [%i0 + 4]
8:
	ld		[%i1 + 8], %i5
	sll		%i3, %g4, %g2
	srl		%i4, %l0, %g5
	or		%g2, %g5, %g2
	st		%g2, [%i0 + 8]
9:
	ld		[%i1 + 12], %g1
	sll		%i4, %g4, %g2
	srl		%i5, %l0, %g5
	addcc		%g3, -4, %g3
	or		%g2, %g5, %g2
	add		%i1, 16, %i1
	st		%g2, [%i0 + 12]
	add		%i0, 16, %i0
	bne,a		5b
	 ld		[%i1], %i3
10:
	sll		%i5, %g4, %g2
	srl		%g1, %l0, %g5
	srl		%l0, 3, %g3
	or		%g2, %g5, %g2
	sub		%i1, %g3, %i1
	andcc		%i2, 2, %g0
	st		%g2, [%i0]
	be		1f
	 andcc		%i2, 1, %g0

	ldub		[%i1], %g2
	add		%i1, 2, %i1
	stb		%g2, [%i0 + 4]
	add		%i0, 2, %i0
	ldub		[%i1 - 1], %g2
	stb		%g2, [%i0 + 3]
1:
	be		1f
	 nop
	ldub		[%i1], %g2
	stb		%g2, [%i0 + 4]
1:
	ret
	 restore	%g7, %g0, %o0

88:	/* short_end */

	and		%o2, 0xe, %o3
20:
	sethi		%hi(89f), %o5
	sll		%o3, 3, %o4
	add		%o0, %o3, %o0
	sub		%o5, %o4, %o5
	add		%o1, %o3, %o1
	jmpl		%o5 + %lo(89f), %g0
	 andcc		%o2, 1, %g0

	MOVE_SHORTCHUNK(o1, o0, 0x0c, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x0a, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x08, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x06, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x04, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x02, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, 0x00, g2, g3)

89:	/* short_table_end */

	be		1f
	 nop

	ldub		[%o1], %g2
	stb		%g2, [%o0]
1:
	retl
	 mov		%g7, %o0

90:	/* short_aligned_end */
	bne		88b
	 andcc		%o2, 8, %g0

	be		1f
	 andcc		%o2, 4, %g0

	ld		[%o1 + 0x00], %g2
	ld		[%o1 + 0x04], %g3
	add		%o1, 8, %o1
	st		%g2, [%o0 + 0x00]
	st		%g3, [%o0 + 0x04]
	add		%o0, 8, %o0
1:
	b		81b
	 mov		%o2, %g1
