/*
 *  linux/arch/m32r/mm/page.S
 *
 *  Clear/Copy page with CPU
 *
 *  Copyright (C) 2004  The Free Software Initiative of Japan
 *
 *  Written by Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
	.text
	.global	copy_page
	/*
	 * copy_page (to, from)
	 *
	 * PAGE_SIZE = 4096-byte
	 * Cache line = 16-byte
	 * 16 * 256
	 */
	.align	4
copy_page:
	ldi	r2, #255
	ld	r3, @r0		/* cache line allocate */
	ld	r4, @r1+
	ld	r5, @r1+
	ld	r6, @r1+
	ld	r7, @r1+
	.fillinsn
0:
	st	r4, @r0
	st	r5, @+r0
	st	r6, @+r0
	st	r7, @+r0
	ld	r4, @r1+
	addi	r0, #4
	ld	r5, @r1+
	ld	r6, @r1+
	ld	r7, @r1+
	ld	r3, @r0		/* cache line allocate */
	addi	r2, #-1
	bnez	r2, 0b

	st	r4, @r0
	st	r5, @+r0
	st	r6, @+r0
	st	r7, @+r0
	jmp	r14

	.text
	.global	clear_page
	/*
	 * clear_page (to)
	 *
	 * PAGE_SIZE = 4096-byte
	 * Cache line = 16-byte
	 * 16 * 256
	 */
	.align	4
clear_page:
	ldi	r2, #255
	ldi	r4, #0
	ld	r3, @r0		/* cache line allocate */
	.fillinsn
0:
	st	r4, @r0
	st	r4, @+r0
	st	r4, @+r0
	st	r4, @+r0
	addi	r0, #4
	ld	r3, @r0		/* cache line allocate */
	addi	r2, #-1
	bnez	r2, 0b

	st	r4, @r0
	st	r4, @+r0
	st	r4, @+r0
	st	r4, @+r0
	jmp	r14
