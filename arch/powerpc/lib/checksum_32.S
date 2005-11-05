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
#include <asm/errno.h>
#include <asm/ppc_asm.h>

	.text

/*
 * ip_fast_csum(buf, len) -- Optimized for IP header
 * len is in words and is always >= 5.
 */
_GLOBAL(ip_fast_csum)
	lwz	r0,0(r3)
	lwzu	r5,4(r3)
	addic.	r4,r4,-2
	addc	r0,r0,r5
	mtctr	r4
	blelr-
1:	lwzu	r4,4(r3)
	adde	r0,r0,r4
	bdnz	1b
	addze	r0,r0		/* add in final carry */
	rlwinm	r3,r0,16,0,31	/* fold two halves together */
	add	r3,r0,r3
	not	r3,r3
	srwi	r3,r3,16
	blr

/*
 * Compute checksum of TCP or UDP pseudo-header:
 *   csum_tcpudp_magic(saddr, daddr, len, proto, sum)
 */	
_GLOBAL(csum_tcpudp_magic)
	rlwimi	r5,r6,16,0,15	/* put proto in upper half of len */
	addc	r0,r3,r4	/* add 4 32-bit words together */
	adde	r0,r0,r5
	adde	r0,r0,r7
	addze	r0,r0		/* add in final carry */
	rlwinm	r3,r0,16,0,31	/* fold two halves together */
	add	r3,r0,r3
	not	r3,r3
	srwi	r3,r3,16
	blr

/*
 * computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit)
 *
 * csum_partial(buff, len, sum)
 */
_GLOBAL(csum_partial)
	addic	r0,r5,0
	subi	r3,r3,4
	srwi.	r6,r4,2
	beq	3f		/* if we're doing < 4 bytes */
	andi.	r5,r3,2		/* Align buffer to longword boundary */
	beq+	1f
	lhz	r5,4(r3)	/* do 2 bytes to get aligned */
	addi	r3,r3,2
	subi	r4,r4,2
	addc	r0,r0,r5
	srwi.	r6,r4,2		/* # words to do */
	beq	3f
1:	mtctr	r6
2:	lwzu	r5,4(r3)	/* the bdnz has zero overhead, so it should */
	adde	r0,r0,r5	/* be unnecessary to unroll this loop */
	bdnz	2b
	andi.	r4,r4,3
3:	cmpwi	0,r4,2
	blt+	4f
	lhz	r5,4(r3)
	addi	r3,r3,2
	subi	r4,r4,2
	adde	r0,r0,r5
4:	cmpwi	0,r4,1
	bne+	5f
	lbz	r5,4(r3)
	slwi	r5,r5,8		/* Upper byte of word */
	adde	r0,r0,r5
5:	addze	r3,r0		/* add in final carry */
	blr

/*
 * Computes the checksum of a memory block at src, length len,
 * and adds in "sum" (32-bit), while copying the block to dst.
 * If an access exception occurs on src or dst, it stores -EFAULT
 * to *src_err or *dst_err respectively, and (for an error on
 * src) zeroes the rest of dst.
 *
 * csum_partial_copy_generic(src, dst, len, sum, src_err, dst_err)
 */
_GLOBAL(csum_partial_copy_generic)
	addic	r0,r6,0
	subi	r3,r3,4
	subi	r4,r4,4
	srwi.	r6,r5,2
	beq	3f		/* if we're doing < 4 bytes */
	andi.	r9,r4,2		/* Align dst to longword boundary */
	beq+	1f
81:	lhz	r6,4(r3)	/* do 2 bytes to get aligned */
	addi	r3,r3,2
	subi	r5,r5,2
91:	sth	r6,4(r4)
	addi	r4,r4,2
	addc	r0,r0,r6
	srwi.	r6,r5,2		/* # words to do */
	beq	3f
1:	srwi.	r6,r5,4		/* # groups of 4 words to do */
	beq	10f
	mtctr	r6
71:	lwz	r6,4(r3)
72:	lwz	r9,8(r3)
73:	lwz	r10,12(r3)
74:	lwzu	r11,16(r3)
	adde	r0,r0,r6
75:	stw	r6,4(r4)
	adde	r0,r0,r9
76:	stw	r9,8(r4)
	adde	r0,r0,r10
77:	stw	r10,12(r4)
	adde	r0,r0,r11
78:	stwu	r11,16(r4)
	bdnz	71b
10:	rlwinm.	r6,r5,30,30,31	/* # words left to do */
	beq	13f
	mtctr	r6
82:	lwzu	r9,4(r3)
92:	stwu	r9,4(r4)
	adde	r0,r0,r9
	bdnz	82b
13:	andi.	r5,r5,3
3:	cmpwi	0,r5,2
	blt+	4f
83:	lhz	r6,4(r3)
	addi	r3,r3,2
	subi	r5,r5,2
93:	sth	r6,4(r4)
	addi	r4,r4,2
	adde	r0,r0,r6
4:	cmpwi	0,r5,1
	bne+	5f
84:	lbz	r6,4(r3)
94:	stb	r6,4(r4)
	slwi	r6,r6,8		/* Upper byte of word */
	adde	r0,r0,r6
5:	addze	r3,r0		/* add in final carry */
	blr

/* These shouldn't go in the fixup section, since that would
   cause the ex_table addresses to get out of order. */

src_error_4:
	mfctr	r6		/* update # bytes remaining from ctr */
	rlwimi	r5,r6,4,0,27
	b	79f
src_error_1:
	li	r6,0
	subi	r5,r5,2
95:	sth	r6,4(r4)
	addi	r4,r4,2
79:	srwi.	r6,r5,2
	beq	3f
	mtctr	r6
src_error_2:
	li	r6,0
96:	stwu	r6,4(r4)
	bdnz	96b
3:	andi.	r5,r5,3
	beq	src_error
src_error_3:
	li	r6,0
	mtctr	r5
	addi	r4,r4,3
97:	stbu	r6,1(r4)
	bdnz	97b
src_error:
	cmpwi	0,r7,0
	beq	1f
	li	r6,-EFAULT
	stw	r6,0(r7)
1:	addze	r3,r0
	blr

dst_error:
	cmpwi	0,r8,0
	beq	1f
	li	r6,-EFAULT
	stw	r6,0(r8)
1:	addze	r3,r0
	blr

.section __ex_table,"a"
	.long	81b,src_error_1
	.long	91b,dst_error
	.long	71b,src_error_4
	.long	72b,src_error_4
	.long	73b,src_error_4
	.long	74b,src_error_4
	.long	75b,dst_error
	.long	76b,dst_error
	.long	77b,dst_error
	.long	78b,dst_error
	.long	82b,src_error_2
	.long	92b,dst_error
	.long	83b,src_error_3
	.long	93b,dst_error
	.long	84b,src_error_3
	.long	94b,dst_error
	.long	95b,dst_error
	.long	96b,dst_error
	.long	97b,dst_error
