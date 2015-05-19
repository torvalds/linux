/*
 * Memory copy functions for 32-bit PowerPC.
 *
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/ppc_asm.h>

#define COPY_16_BYTES		\
	lwz	r7,4(r4);	\
	lwz	r8,8(r4);	\
	lwz	r9,12(r4);	\
	lwzu	r10,16(r4);	\
	stw	r7,4(r6);	\
	stw	r8,8(r6);	\
	stw	r9,12(r6);	\
	stwu	r10,16(r6)

#define COPY_16_BYTES_WITHEX(n)	\
8 ## n ## 0:			\
	lwz	r7,4(r4);	\
8 ## n ## 1:			\
	lwz	r8,8(r4);	\
8 ## n ## 2:			\
	lwz	r9,12(r4);	\
8 ## n ## 3:			\
	lwzu	r10,16(r4);	\
8 ## n ## 4:			\
	stw	r7,4(r6);	\
8 ## n ## 5:			\
	stw	r8,8(r6);	\
8 ## n ## 6:			\
	stw	r9,12(r6);	\
8 ## n ## 7:			\
	stwu	r10,16(r6)

#define COPY_16_BYTES_EXCODE(n)			\
9 ## n ## 0:					\
	addi	r5,r5,-(16 * n);		\
	b	104f;				\
9 ## n ## 1:					\
	addi	r5,r5,-(16 * n);		\
	b	105f;				\
.section __ex_table,"a";			\
	.align	2;				\
	.long	8 ## n ## 0b,9 ## n ## 0b;	\
	.long	8 ## n ## 1b,9 ## n ## 0b;	\
	.long	8 ## n ## 2b,9 ## n ## 0b;	\
	.long	8 ## n ## 3b,9 ## n ## 0b;	\
	.long	8 ## n ## 4b,9 ## n ## 1b;	\
	.long	8 ## n ## 5b,9 ## n ## 1b;	\
	.long	8 ## n ## 6b,9 ## n ## 1b;	\
	.long	8 ## n ## 7b,9 ## n ## 1b;	\
	.text

	.text
	.stabs	"arch/powerpc/lib/",N_SO,0,0,0f
	.stabs	"copy_32.S",N_SO,0,0,0f
0:

CACHELINE_BYTES = L1_CACHE_BYTES
LG_CACHELINE_BYTES = L1_CACHE_SHIFT
CACHELINE_MASK = (L1_CACHE_BYTES-1)

/*
 * Use dcbz on the complete cache lines in the destination
 * to set them to zero.  This requires that the destination
 * area is cacheable.  -- paulus
 */
_GLOBAL(cacheable_memzero)
	mr	r5,r4
	li	r4,0
	addi	r6,r3,-4
	cmplwi	0,r5,4
	blt	7f
	stwu	r4,4(r6)
	beqlr
	andi.	r0,r6,3
	add	r5,r0,r5
	subf	r6,r0,r6
	clrlwi	r7,r6,32-LG_CACHELINE_BYTES
	add	r8,r7,r5
	srwi	r9,r8,LG_CACHELINE_BYTES
	addic.	r9,r9,-1	/* total number of complete cachelines */
	ble	2f
	xori	r0,r7,CACHELINE_MASK & ~3
	srwi.	r0,r0,2
	beq	3f
	mtctr	r0
4:	stwu	r4,4(r6)
	bdnz	4b
3:	mtctr	r9
	li	r7,4
10:	dcbz	r7,r6
	addi	r6,r6,CACHELINE_BYTES
	bdnz	10b
	clrlwi	r5,r8,32-LG_CACHELINE_BYTES
	addi	r5,r5,4
2:	srwi	r0,r5,2
	mtctr	r0
	bdz	6f
1:	stwu	r4,4(r6)
	bdnz	1b
6:	andi.	r5,r5,3
7:	cmpwi	0,r5,0
	beqlr
	mtctr	r5
	addi	r6,r6,3
8:	stbu	r4,1(r6)
	bdnz	8b
	blr

_GLOBAL(memset)
	rlwimi	r4,r4,8,16,23
	rlwimi	r4,r4,16,0,15
	addi	r6,r3,-4
	cmplwi	0,r5,4
	blt	7f
	stwu	r4,4(r6)
	beqlr
	andi.	r0,r6,3
	add	r5,r0,r5
	subf	r6,r0,r6
	srwi	r0,r5,2
	mtctr	r0
	bdz	6f
1:	stwu	r4,4(r6)
	bdnz	1b
6:	andi.	r5,r5,3
7:	cmpwi	0,r5,0
	beqlr
	mtctr	r5
	addi	r6,r6,3
8:	stbu	r4,1(r6)
	bdnz	8b
	blr

/*
 * This version uses dcbz on the complete cache lines in the
 * destination area to reduce memory traffic.  This requires that
 * the destination area is cacheable.
 * We only use this version if the source and dest don't overlap.
 * -- paulus.
 */
_GLOBAL(cacheable_memcpy)
	add	r7,r3,r5		/* test if the src & dst overlap */
	add	r8,r4,r5
	cmplw	0,r4,r7
	cmplw	1,r3,r8
	crand	0,0,4			/* cr0.lt &= cr1.lt */
	blt	memcpy			/* if regions overlap */

	addi	r4,r4,-4
	addi	r6,r3,-4
	neg	r0,r3
	andi.	r0,r0,CACHELINE_MASK	/* # bytes to start of cache line */
	beq	58f

	cmplw	0,r5,r0			/* is this more than total to do? */
	blt	63f			/* if not much to do */
	andi.	r8,r0,3			/* get it word-aligned first */
	subf	r5,r0,r5
	mtctr	r8
	beq+	61f
70:	lbz	r9,4(r4)		/* do some bytes */
	stb	r9,4(r6)
	addi	r4,r4,1
	addi	r6,r6,1
	bdnz	70b
61:	srwi.	r0,r0,2
	mtctr	r0
	beq	58f
72:	lwzu	r9,4(r4)		/* do some words */
	stwu	r9,4(r6)
	bdnz	72b

58:	srwi.	r0,r5,LG_CACHELINE_BYTES /* # complete cachelines */
	clrlwi	r5,r5,32-LG_CACHELINE_BYTES
	li	r11,4
	mtctr	r0
	beq	63f
53:
	dcbz	r11,r6
	COPY_16_BYTES
#if L1_CACHE_BYTES >= 32
	COPY_16_BYTES
#if L1_CACHE_BYTES >= 64
	COPY_16_BYTES
	COPY_16_BYTES
#if L1_CACHE_BYTES >= 128
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
#endif
#endif
#endif
	bdnz	53b

63:	srwi.	r0,r5,2
	mtctr	r0
	beq	64f
30:	lwzu	r0,4(r4)
	stwu	r0,4(r6)
	bdnz	30b

64:	andi.	r0,r5,3
	mtctr	r0
	beq+	65f
40:	lbz	r0,4(r4)
	stb	r0,4(r6)
	addi	r4,r4,1
	addi	r6,r6,1
	bdnz	40b
65:	blr

_GLOBAL(memmove)
	cmplw	0,r3,r4
	bgt	backwards_memcpy
	/* fall through */

_GLOBAL(memcpy)
	srwi.	r7,r5,3
	addi	r6,r3,-4
	addi	r4,r4,-4
	beq	2f			/* if less than 8 bytes to do */
	andi.	r0,r6,3			/* get dest word aligned */
	mtctr	r7
	bne	5f
1:	lwz	r7,4(r4)
	lwzu	r8,8(r4)
	stw	r7,4(r6)
	stwu	r8,8(r6)
	bdnz	1b
	andi.	r5,r5,7
2:	cmplwi	0,r5,4
	blt	3f
	lwzu	r0,4(r4)
	addi	r5,r5,-4
	stwu	r0,4(r6)
3:	cmpwi	0,r5,0
	beqlr
	mtctr	r5
	addi	r4,r4,3
	addi	r6,r6,3
4:	lbzu	r0,1(r4)
	stbu	r0,1(r6)
	bdnz	4b
	blr
5:	subfic	r0,r0,4
	mtctr	r0
6:	lbz	r7,4(r4)
	addi	r4,r4,1
	stb	r7,4(r6)
	addi	r6,r6,1
	bdnz	6b
	subf	r5,r0,r5
	rlwinm.	r7,r5,32-3,3,31
	beq	2b
	mtctr	r7
	b	1b

_GLOBAL(backwards_memcpy)
	rlwinm.	r7,r5,32-3,3,31		/* r0 = r5 >> 3 */
	add	r6,r3,r5
	add	r4,r4,r5
	beq	2f
	andi.	r0,r6,3
	mtctr	r7
	bne	5f
1:	lwz	r7,-4(r4)
	lwzu	r8,-8(r4)
	stw	r7,-4(r6)
	stwu	r8,-8(r6)
	bdnz	1b
	andi.	r5,r5,7
2:	cmplwi	0,r5,4
	blt	3f
	lwzu	r0,-4(r4)
	subi	r5,r5,4
	stwu	r0,-4(r6)
3:	cmpwi	0,r5,0
	beqlr
	mtctr	r5
4:	lbzu	r0,-1(r4)
	stbu	r0,-1(r6)
	bdnz	4b
	blr
5:	mtctr	r0
6:	lbzu	r7,-1(r4)
	stbu	r7,-1(r6)
	bdnz	6b
	subf	r5,r0,r5
	rlwinm.	r7,r5,32-3,3,31
	beq	2b
	mtctr	r7
	b	1b

_GLOBAL(__copy_tofrom_user)
	addi	r4,r4,-4
	addi	r6,r3,-4
	neg	r0,r3
	andi.	r0,r0,CACHELINE_MASK	/* # bytes to start of cache line */
	beq	58f

	cmplw	0,r5,r0			/* is this more than total to do? */
	blt	63f			/* if not much to do */
	andi.	r8,r0,3			/* get it word-aligned first */
	mtctr	r8
	beq+	61f
70:	lbz	r9,4(r4)		/* do some bytes */
71:	stb	r9,4(r6)
	addi	r4,r4,1
	addi	r6,r6,1
	bdnz	70b
61:	subf	r5,r0,r5
	srwi.	r0,r0,2
	mtctr	r0
	beq	58f
72:	lwzu	r9,4(r4)		/* do some words */
73:	stwu	r9,4(r6)
	bdnz	72b

	.section __ex_table,"a"
	.align	2
	.long	70b,100f
	.long	71b,101f
	.long	72b,102f
	.long	73b,103f
	.text

58:	srwi.	r0,r5,LG_CACHELINE_BYTES /* # complete cachelines */
	clrlwi	r5,r5,32-LG_CACHELINE_BYTES
	li	r11,4
	beq	63f

	/* Here we decide how far ahead to prefetch the source */
	li	r3,4
	cmpwi	r0,1
	li	r7,0
	ble	114f
	li	r7,1
#if MAX_COPY_PREFETCH > 1
	/* Heuristically, for large transfers we prefetch
	   MAX_COPY_PREFETCH cachelines ahead.  For small transfers
	   we prefetch 1 cacheline ahead. */
	cmpwi	r0,MAX_COPY_PREFETCH
	ble	112f
	li	r7,MAX_COPY_PREFETCH
112:	mtctr	r7
111:	dcbt	r3,r4
	addi	r3,r3,CACHELINE_BYTES
	bdnz	111b
#else
	dcbt	r3,r4
	addi	r3,r3,CACHELINE_BYTES
#endif /* MAX_COPY_PREFETCH > 1 */

114:	subf	r8,r7,r0
	mr	r0,r7
	mtctr	r8

53:	dcbt	r3,r4
54:	dcbz	r11,r6
	.section __ex_table,"a"
	.align	2
	.long	54b,105f
	.text
/* the main body of the cacheline loop */
	COPY_16_BYTES_WITHEX(0)
#if L1_CACHE_BYTES >= 32
	COPY_16_BYTES_WITHEX(1)
#if L1_CACHE_BYTES >= 64
	COPY_16_BYTES_WITHEX(2)
	COPY_16_BYTES_WITHEX(3)
#if L1_CACHE_BYTES >= 128
	COPY_16_BYTES_WITHEX(4)
	COPY_16_BYTES_WITHEX(5)
	COPY_16_BYTES_WITHEX(6)
	COPY_16_BYTES_WITHEX(7)
#endif
#endif
#endif
	bdnz	53b
	cmpwi	r0,0
	li	r3,4
	li	r7,0
	bne	114b

63:	srwi.	r0,r5,2
	mtctr	r0
	beq	64f
30:	lwzu	r0,4(r4)
31:	stwu	r0,4(r6)
	bdnz	30b

64:	andi.	r0,r5,3
	mtctr	r0
	beq+	65f
40:	lbz	r0,4(r4)
41:	stb	r0,4(r6)
	addi	r4,r4,1
	addi	r6,r6,1
	bdnz	40b
65:	li	r3,0
	blr

/* read fault, initial single-byte copy */
100:	li	r9,0
	b	90f
/* write fault, initial single-byte copy */
101:	li	r9,1
90:	subf	r5,r8,r5
	li	r3,0
	b	99f
/* read fault, initial word copy */
102:	li	r9,0
	b	91f
/* write fault, initial word copy */
103:	li	r9,1
91:	li	r3,2
	b	99f

/*
 * this stuff handles faults in the cacheline loop and branches to either
 * 104f (if in read part) or 105f (if in write part), after updating r5
 */
	COPY_16_BYTES_EXCODE(0)
#if L1_CACHE_BYTES >= 32
	COPY_16_BYTES_EXCODE(1)
#if L1_CACHE_BYTES >= 64
	COPY_16_BYTES_EXCODE(2)
	COPY_16_BYTES_EXCODE(3)
#if L1_CACHE_BYTES >= 128
	COPY_16_BYTES_EXCODE(4)
	COPY_16_BYTES_EXCODE(5)
	COPY_16_BYTES_EXCODE(6)
	COPY_16_BYTES_EXCODE(7)
#endif
#endif
#endif

/* read fault in cacheline loop */
104:	li	r9,0
	b	92f
/* fault on dcbz (effectively a write fault) */
/* or write fault in cacheline loop */
105:	li	r9,1
92:	li	r3,LG_CACHELINE_BYTES
	mfctr	r8
	add	r0,r0,r8
	b	106f
/* read fault in final word loop */
108:	li	r9,0
	b	93f
/* write fault in final word loop */
109:	li	r9,1
93:	andi.	r5,r5,3
	li	r3,2
	b	99f
/* read fault in final byte loop */
110:	li	r9,0
	b	94f
/* write fault in final byte loop */
111:	li	r9,1
94:	li	r5,0
	li	r3,0
/*
 * At this stage the number of bytes not copied is
 * r5 + (ctr << r3), and r9 is 0 for read or 1 for write.
 */
99:	mfctr	r0
106:	slw	r3,r0,r3
	add.	r3,r3,r5
	beq	120f			/* shouldn't happen */
	cmpwi	0,r9,0
	bne	120f
/* for a read fault, first try to continue the copy one byte at a time */
	mtctr	r3
130:	lbz	r0,4(r4)
131:	stb	r0,4(r6)
	addi	r4,r4,1
	addi	r6,r6,1
	bdnz	130b
/* then clear out the destination: r3 bytes starting at 4(r6) */
132:	mfctr	r3
	srwi.	r0,r3,2
	li	r9,0
	mtctr	r0
	beq	113f
112:	stwu	r9,4(r6)
	bdnz	112b
113:	andi.	r0,r3,3
	mtctr	r0
	beq	120f
114:	stb	r9,4(r6)
	addi	r6,r6,1
	bdnz	114b
120:	blr

	.section __ex_table,"a"
	.align	2
	.long	30b,108b
	.long	31b,109b
	.long	40b,110b
	.long	41b,111b
	.long	130b,132b
	.long	131b,120b
	.long	112b,120b
	.long	114b,120b
	.text
