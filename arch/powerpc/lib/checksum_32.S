/*
 * This file contains assembly-language implementations
 * of IP-style 1's complement checksum routines.
 *	
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * Severely hacked about by Paul Mackerras (paulus@cs.anu.edu.au).
 */

#include <linux/sys.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/ppc_asm.h>
#include <asm/export.h>

	.text

/*
 * computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit)
 *
 * __csum_partial(buff, len, sum)
 */
_GLOBAL(__csum_partial)
	subi	r3,r3,4
	srawi.	r6,r4,2		/* Divide len by 4 and also clear carry */
	beq	3f		/* if we're doing < 4 bytes */
	andi.	r0,r3,2		/* Align buffer to longword boundary */
	beq+	1f
	lhz	r0,4(r3)	/* do 2 bytes to get aligned */
	subi	r4,r4,2
	addi	r3,r3,2
	srwi.	r6,r4,2		/* # words to do */
	adde	r5,r5,r0
	beq	3f
1:	andi.	r6,r6,3		/* Prepare to handle words 4 by 4 */
	beq	21f
	mtctr	r6
2:	lwzu	r0,4(r3)
	adde	r5,r5,r0
	bdnz	2b
21:	srwi.	r6,r4,4		/* # blocks of 4 words to do */
	beq	3f
	mtctr	r6
22:	lwz	r0,4(r3)
	lwz	r6,8(r3)
	lwz	r7,12(r3)
	lwzu	r8,16(r3)
	adde	r5,r5,r0
	adde	r5,r5,r6
	adde	r5,r5,r7
	adde	r5,r5,r8
	bdnz	22b
3:	andi.	r0,r4,2
	beq+	4f
	lhz	r0,4(r3)
	addi	r3,r3,2
	adde	r5,r5,r0
4:	andi.	r0,r4,1
	beq+	5f
	lbz	r0,4(r3)
	slwi	r0,r0,8		/* Upper byte of word */
	adde	r5,r5,r0
5:	addze	r3,r5		/* add in final carry */
	blr
EXPORT_SYMBOL(__csum_partial)

/*
 * Computes the checksum of a memory block at src, length len,
 * and adds in "sum" (32-bit), while copying the block to dst.
 * If an access exception occurs on src or dst, it stores -EFAULT
 * to *src_err or *dst_err respectively, and (for an error on
 * src) zeroes the rest of dst.
 *
 * csum_partial_copy_generic(src, dst, len, sum, src_err, dst_err)
 */
#define CSUM_COPY_16_BYTES_WITHEX(n)	\
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
	adde	r12,r12,r7;	\
8 ## n ## 5:			\
	stw	r8,8(r6);	\
	adde	r12,r12,r8;	\
8 ## n ## 6:			\
	stw	r9,12(r6);	\
	adde	r12,r12,r9;	\
8 ## n ## 7:			\
	stwu	r10,16(r6);	\
	adde	r12,r12,r10

#define CSUM_COPY_16_BYTES_EXCODE(n)		\
.section __ex_table,"a";		\
	.align	2;			\
	.long	8 ## n ## 0b,src_error;	\
	.long	8 ## n ## 1b,src_error;	\
	.long	8 ## n ## 2b,src_error;	\
	.long	8 ## n ## 3b,src_error;	\
	.long	8 ## n ## 4b,dst_error;	\
	.long	8 ## n ## 5b,dst_error;	\
	.long	8 ## n ## 6b,dst_error;	\
	.long	8 ## n ## 7b,dst_error;	\
	.text

	.text
	.stabs	"arch/powerpc/lib/",N_SO,0,0,0f
	.stabs	"checksum_32.S",N_SO,0,0,0f
0:

CACHELINE_BYTES = L1_CACHE_BYTES
LG_CACHELINE_BYTES = L1_CACHE_SHIFT
CACHELINE_MASK = (L1_CACHE_BYTES-1)

_GLOBAL(csum_partial_copy_generic)
	stwu	r1,-16(r1)
	stw	r7,12(r1)
	stw	r8,8(r1)

	addic	r12,r6,0
	addi	r6,r4,-4
	neg	r0,r4
	addi	r4,r3,-4
	andi.	r0,r0,CACHELINE_MASK	/* # bytes to start of cache line */
	crset	4*cr7+eq
	beq	58f

	cmplw	0,r5,r0			/* is this more than total to do? */
	blt	63f			/* if not much to do */
	rlwinm	r7,r6,3,0x8
	rlwnm	r12,r12,r7,0,31	/* odd destination address: rotate one byte */
	cmplwi	cr7,r7,0	/* is destination address even ? */
	andi.	r8,r0,3			/* get it word-aligned first */
	mtctr	r8
	beq+	61f
	li	r3,0
70:	lbz	r9,4(r4)		/* do some bytes */
	addi	r4,r4,1
	slwi	r3,r3,8
	rlwimi	r3,r9,0,24,31
71:	stb	r9,4(r6)
	addi	r6,r6,1
	bdnz	70b
	adde	r12,r12,r3
61:	subf	r5,r0,r5
	srwi.	r0,r0,2
	mtctr	r0
	beq	58f
72:	lwzu	r9,4(r4)		/* do some words */
	adde	r12,r12,r9
73:	stwu	r9,4(r6)
	bdnz	72b

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
/* the main body of the cacheline loop */
	CSUM_COPY_16_BYTES_WITHEX(0)
#if L1_CACHE_BYTES >= 32
	CSUM_COPY_16_BYTES_WITHEX(1)
#if L1_CACHE_BYTES >= 64
	CSUM_COPY_16_BYTES_WITHEX(2)
	CSUM_COPY_16_BYTES_WITHEX(3)
#if L1_CACHE_BYTES >= 128
	CSUM_COPY_16_BYTES_WITHEX(4)
	CSUM_COPY_16_BYTES_WITHEX(5)
	CSUM_COPY_16_BYTES_WITHEX(6)
	CSUM_COPY_16_BYTES_WITHEX(7)
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
	adde	r12,r12,r0
31:	stwu	r0,4(r6)
	bdnz	30b

64:	andi.	r0,r5,2
	beq+	65f
40:	lhz	r0,4(r4)
	addi	r4,r4,2
41:	sth	r0,4(r6)
	adde	r12,r12,r0
	addi	r6,r6,2
65:	andi.	r0,r5,1
	beq+	66f
50:	lbz	r0,4(r4)
51:	stb	r0,4(r6)
	slwi	r0,r0,8
	adde	r12,r12,r0
66:	addze	r3,r12
	addi	r1,r1,16
	beqlr+	cr7
	rlwinm	r3,r3,8,0,31	/* odd destination address: rotate one byte */
	blr

/* read fault */
src_error:
	lwz	r7,12(r1)
	addi	r1,r1,16
	cmpwi	cr0,r7,0
	beqlr
	li	r0,-EFAULT
	stw	r0,0(r7)
	blr
/* write fault */
dst_error:
	lwz	r8,8(r1)
	addi	r1,r1,16
	cmpwi	cr0,r8,0
	beqlr
	li	r0,-EFAULT
	stw	r0,0(r8)
	blr

	.section __ex_table,"a"
	.align	2
	.long	70b,src_error
	.long	71b,dst_error
	.long	72b,src_error
	.long	73b,dst_error
	.long	54b,dst_error
	.text

/*
 * this stuff handles faults in the cacheline loop and branches to either
 * src_error (if in read part) or dst_error (if in write part)
 */
	CSUM_COPY_16_BYTES_EXCODE(0)
#if L1_CACHE_BYTES >= 32
	CSUM_COPY_16_BYTES_EXCODE(1)
#if L1_CACHE_BYTES >= 64
	CSUM_COPY_16_BYTES_EXCODE(2)
	CSUM_COPY_16_BYTES_EXCODE(3)
#if L1_CACHE_BYTES >= 128
	CSUM_COPY_16_BYTES_EXCODE(4)
	CSUM_COPY_16_BYTES_EXCODE(5)
	CSUM_COPY_16_BYTES_EXCODE(6)
	CSUM_COPY_16_BYTES_EXCODE(7)
#endif
#endif
#endif

	.section __ex_table,"a"
	.align	2
	.long	30b,src_error
	.long	31b,dst_error
	.long	40b,src_error
	.long	41b,dst_error
	.long	50b,src_error
	.long	51b,dst_error
EXPORT_SYMBOL(csum_partial_copy_generic)
