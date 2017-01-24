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
#include <asm/export.h>

/*
 * Computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit).
 *
 * __csum_partial(r3=buff, r4=len, r5=sum)
 */
_GLOBAL(__csum_partial)
	addic	r0,r5,0			/* clear carry */

	srdi.	r6,r4,3			/* less than 8 bytes? */
	beq	.Lcsum_tail_word

	/*
	 * If only halfword aligned, align to a double word. Since odd
	 * aligned addresses should be rare and they would require more
	 * work to calculate the correct checksum, we ignore that case
	 * and take the potential slowdown of unaligned loads.
	 */
	rldicl. r6,r3,64-1,64-2		/* r6 = (r3 & 0x3) >> 1 */
	beq	.Lcsum_aligned

	li	r7,4
	sub	r6,r7,r6
	mtctr	r6

1:
	lhz	r6,0(r3)		/* align to doubleword */
	subi	r4,r4,2
	addi	r3,r3,2
	adde	r0,r0,r6
	bdnz	1b

.Lcsum_aligned:
	/*
	 * We unroll the loop such that each iteration is 64 bytes with an
	 * entry and exit limb of 64 bytes, meaning a minimum size of
	 * 128 bytes.
	 */
	srdi.	r6,r4,7
	beq	.Lcsum_tail_doublewords		/* len < 128 */

	srdi	r6,r4,6
	subi	r6,r6,1
	mtctr	r6

	stdu	r1,-STACKFRAMESIZE(r1)
	std	r14,STK_REG(R14)(r1)
	std	r15,STK_REG(R15)(r1)
	std	r16,STK_REG(R16)(r1)

	ld	r6,0(r3)
	ld	r9,8(r3)

	ld	r10,16(r3)
	ld	r11,24(r3)

	/*
	 * On POWER6 and POWER7 back to back adde instructions take 2 cycles
	 * because of the XER dependency. This means the fastest this loop can
	 * go is 16 cycles per iteration. The scheduling of the loop below has
	 * been shown to hit this on both POWER6 and POWER7.
	 */
	.align 5
2:
	adde	r0,r0,r6
	ld	r12,32(r3)
	ld	r14,40(r3)

	adde	r0,r0,r9
	ld	r15,48(r3)
	ld	r16,56(r3)
	addi	r3,r3,64

	adde	r0,r0,r10

	adde	r0,r0,r11

	adde	r0,r0,r12

	adde	r0,r0,r14

	adde	r0,r0,r15
	ld	r6,0(r3)
	ld	r9,8(r3)

	adde	r0,r0,r16
	ld	r10,16(r3)
	ld	r11,24(r3)
	bdnz	2b


	adde	r0,r0,r6
	ld	r12,32(r3)
	ld	r14,40(r3)

	adde	r0,r0,r9
	ld	r15,48(r3)
	ld	r16,56(r3)
	addi	r3,r3,64

	adde	r0,r0,r10
	adde	r0,r0,r11
	adde	r0,r0,r12
	adde	r0,r0,r14
	adde	r0,r0,r15
	adde	r0,r0,r16

	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)
	addi	r1,r1,STACKFRAMESIZE

	andi.	r4,r4,63

.Lcsum_tail_doublewords:		/* Up to 127 bytes to go */
	srdi.	r6,r4,3
	beq	.Lcsum_tail_word

	mtctr	r6
3:
	ld	r6,0(r3)
	addi	r3,r3,8
	adde	r0,r0,r6
	bdnz	3b

	andi.	r4,r4,7

.Lcsum_tail_word:			/* Up to 7 bytes to go */
	srdi.	r6,r4,2
	beq	.Lcsum_tail_halfword

	lwz	r6,0(r3)
	addi	r3,r3,4
	adde	r0,r0,r6
	subi	r4,r4,4

.Lcsum_tail_halfword:			/* Up to 3 bytes to go */
	srdi.	r6,r4,1
	beq	.Lcsum_tail_byte

	lhz	r6,0(r3)
	addi	r3,r3,2
	adde	r0,r0,r6
	subi	r4,r4,2

.Lcsum_tail_byte:			/* Up to 1 byte to go */
	andi.	r6,r4,1
	beq	.Lcsum_finish

	lbz	r6,0(r3)
	sldi	r9,r6,8			/* Pad the byte out to 16 bits */
	adde	r0,r0,r9

.Lcsum_finish:
	addze	r0,r0			/* add in final carry */
	rldicl	r4,r0,32,0		/* fold two 32 bit halves together */
	add	r3,r4,r0
	srdi	r3,r3,32
	blr
EXPORT_SYMBOL(__csum_partial)


	.macro srcnr
100:
	EX_TABLE(100b,.Lsrc_error_nr)
	.endm

	.macro source
150:
	EX_TABLE(150b,.Lsrc_error)
	.endm

	.macro dstnr
200:
	EX_TABLE(200b,.Ldest_error_nr)
	.endm

	.macro dest
250:
	EX_TABLE(250b,.Ldest_error)
	.endm

/*
 * Computes the checksum of a memory block at src, length len,
 * and adds in "sum" (32-bit), while copying the block to dst.
 * If an access exception occurs on src or dst, it stores -EFAULT
 * to *src_err or *dst_err respectively. The caller must take any action
 * required in this case (zeroing memory, recalculating partial checksum etc).
 *
 * csum_partial_copy_generic(r3=src, r4=dst, r5=len, r6=sum, r7=src_err, r8=dst_err)
 */
_GLOBAL(csum_partial_copy_generic)
	addic	r0,r6,0			/* clear carry */

	srdi.	r6,r5,3			/* less than 8 bytes? */
	beq	.Lcopy_tail_word

	/*
	 * If only halfword aligned, align to a double word. Since odd
	 * aligned addresses should be rare and they would require more
	 * work to calculate the correct checksum, we ignore that case
	 * and take the potential slowdown of unaligned loads.
	 *
	 * If the source and destination are relatively unaligned we only
	 * align the source. This keeps things simple.
	 */
	rldicl. r6,r3,64-1,64-2		/* r6 = (r3 & 0x3) >> 1 */
	beq	.Lcopy_aligned

	li	r9,4
	sub	r6,r9,r6
	mtctr	r6

1:
srcnr;	lhz	r6,0(r3)		/* align to doubleword */
	subi	r5,r5,2
	addi	r3,r3,2
	adde	r0,r0,r6
dstnr;	sth	r6,0(r4)
	addi	r4,r4,2
	bdnz	1b

.Lcopy_aligned:
	/*
	 * We unroll the loop such that each iteration is 64 bytes with an
	 * entry and exit limb of 64 bytes, meaning a minimum size of
	 * 128 bytes.
	 */
	srdi.	r6,r5,7
	beq	.Lcopy_tail_doublewords		/* len < 128 */

	srdi	r6,r5,6
	subi	r6,r6,1
	mtctr	r6

	stdu	r1,-STACKFRAMESIZE(r1)
	std	r14,STK_REG(R14)(r1)
	std	r15,STK_REG(R15)(r1)
	std	r16,STK_REG(R16)(r1)

source;	ld	r6,0(r3)
source;	ld	r9,8(r3)

source;	ld	r10,16(r3)
source;	ld	r11,24(r3)

	/*
	 * On POWER6 and POWER7 back to back adde instructions take 2 cycles
	 * because of the XER dependency. This means the fastest this loop can
	 * go is 16 cycles per iteration. The scheduling of the loop below has
	 * been shown to hit this on both POWER6 and POWER7.
	 */
	.align 5
2:
	adde	r0,r0,r6
source;	ld	r12,32(r3)
source;	ld	r14,40(r3)

	adde	r0,r0,r9
source;	ld	r15,48(r3)
source;	ld	r16,56(r3)
	addi	r3,r3,64

	adde	r0,r0,r10
dest;	std	r6,0(r4)
dest;	std	r9,8(r4)

	adde	r0,r0,r11
dest;	std	r10,16(r4)
dest;	std	r11,24(r4)

	adde	r0,r0,r12
dest;	std	r12,32(r4)
dest;	std	r14,40(r4)

	adde	r0,r0,r14
dest;	std	r15,48(r4)
dest;	std	r16,56(r4)
	addi	r4,r4,64

	adde	r0,r0,r15
source;	ld	r6,0(r3)
source;	ld	r9,8(r3)

	adde	r0,r0,r16
source;	ld	r10,16(r3)
source;	ld	r11,24(r3)
	bdnz	2b


	adde	r0,r0,r6
source;	ld	r12,32(r3)
source;	ld	r14,40(r3)

	adde	r0,r0,r9
source;	ld	r15,48(r3)
source;	ld	r16,56(r3)
	addi	r3,r3,64

	adde	r0,r0,r10
dest;	std	r6,0(r4)
dest;	std	r9,8(r4)

	adde	r0,r0,r11
dest;	std	r10,16(r4)
dest;	std	r11,24(r4)

	adde	r0,r0,r12
dest;	std	r12,32(r4)
dest;	std	r14,40(r4)

	adde	r0,r0,r14
dest;	std	r15,48(r4)
dest;	std	r16,56(r4)
	addi	r4,r4,64

	adde	r0,r0,r15
	adde	r0,r0,r16

	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)
	addi	r1,r1,STACKFRAMESIZE

	andi.	r5,r5,63

.Lcopy_tail_doublewords:		/* Up to 127 bytes to go */
	srdi.	r6,r5,3
	beq	.Lcopy_tail_word

	mtctr	r6
3:
srcnr;	ld	r6,0(r3)
	addi	r3,r3,8
	adde	r0,r0,r6
dstnr;	std	r6,0(r4)
	addi	r4,r4,8
	bdnz	3b

	andi.	r5,r5,7

.Lcopy_tail_word:			/* Up to 7 bytes to go */
	srdi.	r6,r5,2
	beq	.Lcopy_tail_halfword

srcnr;	lwz	r6,0(r3)
	addi	r3,r3,4
	adde	r0,r0,r6
dstnr;	stw	r6,0(r4)
	addi	r4,r4,4
	subi	r5,r5,4

.Lcopy_tail_halfword:			/* Up to 3 bytes to go */
	srdi.	r6,r5,1
	beq	.Lcopy_tail_byte

srcnr;	lhz	r6,0(r3)
	addi	r3,r3,2
	adde	r0,r0,r6
dstnr;	sth	r6,0(r4)
	addi	r4,r4,2
	subi	r5,r5,2

.Lcopy_tail_byte:			/* Up to 1 byte to go */
	andi.	r6,r5,1
	beq	.Lcopy_finish

srcnr;	lbz	r6,0(r3)
	sldi	r9,r6,8			/* Pad the byte out to 16 bits */
	adde	r0,r0,r9
dstnr;	stb	r6,0(r4)

.Lcopy_finish:
	addze	r0,r0			/* add in final carry */
	rldicl	r4,r0,32,0		/* fold two 32 bit halves together */
	add	r3,r4,r0
	srdi	r3,r3,32
	blr

.Lsrc_error:
	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)
	addi	r1,r1,STACKFRAMESIZE
.Lsrc_error_nr:
	cmpdi	0,r7,0
	beqlr
	li	r6,-EFAULT
	stw	r6,0(r7)
	blr

.Ldest_error:
	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)
	addi	r1,r1,STACKFRAMESIZE
.Ldest_error_nr:
	cmpdi	0,r8,0
	beqlr
	li	r6,-EFAULT
	stw	r6,0(r8)
	blr
EXPORT_SYMBOL(csum_partial_copy_generic)
