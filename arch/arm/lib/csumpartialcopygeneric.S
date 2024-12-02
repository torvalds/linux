/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/lib/csumpartialcopygeneric.S
 *
 *  Copyright (C) 1995-2001 Russell King
 */
#include <asm/assembler.h>

/*
 * unsigned int
 * csum_partial_copy_xxx(const char *src, char *dst, int len, int sum, )
 *  r0 = src, r1 = dst, r2 = len, r3 = sum
 *  Returns : r0 = checksum
 *
 * Note that 'tst' and 'teq' preserve the carry flag.
 */

src	.req	r0
dst	.req	r1
len	.req	r2
sum	.req	r3

.Lzero:		mov	r0, sum
		load_regs

		/*
		 * Align an unaligned destination pointer.  We know that
		 * we have >= 8 bytes here, so we don't need to check
		 * the length.  Note that the source pointer hasn't been
		 * aligned yet.
		 */
.Ldst_unaligned:
		tst	dst, #1
		beq	.Ldst_16bit

		load1b	ip
		sub	len, len, #1
		adcs	sum, sum, ip, put_byte_1	@ update checksum
		strb	ip, [dst], #1
		tst	dst, #2
		reteq	lr			@ dst is now 32bit aligned

.Ldst_16bit:	load2b	r8, ip
		sub	len, len, #2
		adcs	sum, sum, r8, put_byte_0
		strb	r8, [dst], #1
		adcs	sum, sum, ip, put_byte_1
		strb	ip, [dst], #1
		ret	lr			@ dst is now 32bit aligned

		/*
		 * Handle 0 to 7 bytes, with any alignment of source and
		 * destination pointers.  Note that when we get here, C = 0
		 */
.Lless8:	teq	len, #0			@ check for zero count
		beq	.Lzero

		/* we must have at least one byte. */
		tst	dst, #1			@ dst 16-bit aligned
		beq	.Lless8_aligned

		/* Align dst */
		load1b	ip
		sub	len, len, #1
		adcs	sum, sum, ip, put_byte_1	@ update checksum
		strb	ip, [dst], #1
		tst	len, #6
		beq	.Lless8_byteonly

1:		load2b	r8, ip
		sub	len, len, #2
		adcs	sum, sum, r8, put_byte_0
		strb	r8, [dst], #1
		adcs	sum, sum, ip, put_byte_1
		strb	ip, [dst], #1
.Lless8_aligned:
		tst	len, #6
		bne	1b
.Lless8_byteonly:
		tst	len, #1
		beq	.Ldone
		load1b	r8
		adcs	sum, sum, r8, put_byte_0	@ update checksum
		strb	r8, [dst], #1
		b	.Ldone

FN_ENTRY
		save_regs
		mov	sum, #-1

		cmp	len, #8			@ Ensure that we have at least
		blo	.Lless8			@ 8 bytes to copy.

		adds	sum, sum, #0		@ C = 0
		tst	dst, #3			@ Test destination alignment
		blne	.Ldst_unaligned		@ align destination, return here

		/*
		 * Ok, the dst pointer is now 32bit aligned, and we know
		 * that we must have more than 4 bytes to copy.  Note
		 * that C contains the carry from the dst alignment above.
		 */

		tst	src, #3			@ Test source alignment
		bne	.Lsrc_not_aligned

		/* Routine for src & dst aligned */

		bics	ip, len, #15
		beq	2f

1:		load4l	r4, r5, r6, r7
		stmia	dst!, {r4, r5, r6, r7}
		adcs	sum, sum, r4
		adcs	sum, sum, r5
		adcs	sum, sum, r6
		adcs	sum, sum, r7
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b

2:		ands	ip, len, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r4, r5
		stmia	dst!, {r4, r5}
		adcs	sum, sum, r4
		adcs	sum, sum, r5
		tst	ip, #4
		beq	4f

3:		load1l	r4
		str	r4, [dst], #4
		adcs	sum, sum, r4

4:		ands	len, len, #3
		beq	.Ldone
		load1l	r4
		tst	len, #2
		mov	r5, r4, get_byte_0
		beq	.Lexit
		adcs	sum, sum, r4, lspush #16
		strb	r5, [dst], #1
		mov	r5, r4, get_byte_1
		strb	r5, [dst], #1
		mov	r5, r4, get_byte_2
.Lexit:		tst	len, #1
		strbne	r5, [dst], #1
		andne	r5, r5, #255
		adcsne	sum, sum, r5, put_byte_0

		/*
		 * If the dst pointer was not 16-bit aligned, we
		 * need to rotate the checksum here to get around
		 * the inefficient byte manipulations in the
		 * architecture independent code.
		 */
.Ldone:		adc	r0, sum, #0
		ldr	sum, [sp, #0]		@ dst
		tst	sum, #1
		movne	r0, r0, ror #8
		load_regs

.Lsrc_not_aligned:
		adc	sum, sum, #0		@ include C from dst alignment
		and	ip, src, #3
		bic	src, src, #3
		load1l	r5
		cmp	ip, #2
		beq	.Lsrc2_aligned
		bhi	.Lsrc3_aligned
		mov	r4, r5, lspull #8		@ C = 0
		bics	ip, len, #15
		beq	2f
1:		load4l	r5, r6, r7, r8
		orr	r4, r4, r5, lspush #24
		mov	r5, r5, lspull #8
		orr	r5, r5, r6, lspush #24
		mov	r6, r6, lspull #8
		orr	r6, r6, r7, lspush #24
		mov	r7, r7, lspull #8
		orr	r7, r7, r8, lspush #24
		stmia	dst!, {r4, r5, r6, r7}
		adcs	sum, sum, r4
		adcs	sum, sum, r5
		adcs	sum, sum, r6
		adcs	sum, sum, r7
		mov	r4, r8, lspull #8
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, len, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r5, r6
		orr	r4, r4, r5, lspush #24
		mov	r5, r5, lspull #8
		orr	r5, r5, r6, lspush #24
		stmia	dst!, {r4, r5}
		adcs	sum, sum, r4
		adcs	sum, sum, r5
		mov	r4, r6, lspull #8
		tst	ip, #4
		beq	4f
3:		load1l	r5
		orr	r4, r4, r5, lspush #24
		str	r4, [dst], #4
		adcs	sum, sum, r4
		mov	r4, r5, lspull #8
4:		ands	len, len, #3
		beq	.Ldone
		mov	r5, r4, get_byte_0
		tst	len, #2
		beq	.Lexit
		adcs	sum, sum, r4, lspush #16
		strb	r5, [dst], #1
		mov	r5, r4, get_byte_1
		strb	r5, [dst], #1
		mov	r5, r4, get_byte_2
		b	.Lexit

.Lsrc2_aligned:	mov	r4, r5, lspull #16
		adds	sum, sum, #0
		bics	ip, len, #15
		beq	2f
1:		load4l	r5, r6, r7, r8
		orr	r4, r4, r5, lspush #16
		mov	r5, r5, lspull #16
		orr	r5, r5, r6, lspush #16
		mov	r6, r6, lspull #16
		orr	r6, r6, r7, lspush #16
		mov	r7, r7, lspull #16
		orr	r7, r7, r8, lspush #16
		stmia	dst!, {r4, r5, r6, r7}
		adcs	sum, sum, r4
		adcs	sum, sum, r5
		adcs	sum, sum, r6
		adcs	sum, sum, r7
		mov	r4, r8, lspull #16
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, len, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r5, r6
		orr	r4, r4, r5, lspush #16
		mov	r5, r5, lspull #16
		orr	r5, r5, r6, lspush #16
		stmia	dst!, {r4, r5}
		adcs	sum, sum, r4
		adcs	sum, sum, r5
		mov	r4, r6, lspull #16
		tst	ip, #4
		beq	4f
3:		load1l	r5
		orr	r4, r4, r5, lspush #16
		str	r4, [dst], #4
		adcs	sum, sum, r4
		mov	r4, r5, lspull #16
4:		ands	len, len, #3
		beq	.Ldone
		mov	r5, r4, get_byte_0
		tst	len, #2
		beq	.Lexit
		adcs	sum, sum, r4
		strb	r5, [dst], #1
		mov	r5, r4, get_byte_1
		strb	r5, [dst], #1
		tst	len, #1
		beq	.Ldone
		load1b	r5
		b	.Lexit

.Lsrc3_aligned:	mov	r4, r5, lspull #24
		adds	sum, sum, #0
		bics	ip, len, #15
		beq	2f
1:		load4l	r5, r6, r7, r8
		orr	r4, r4, r5, lspush #8
		mov	r5, r5, lspull #24
		orr	r5, r5, r6, lspush #8
		mov	r6, r6, lspull #24
		orr	r6, r6, r7, lspush #8
		mov	r7, r7, lspull #24
		orr	r7, r7, r8, lspush #8
		stmia	dst!, {r4, r5, r6, r7}
		adcs	sum, sum, r4
		adcs	sum, sum, r5
		adcs	sum, sum, r6
		adcs	sum, sum, r7
		mov	r4, r8, lspull #24
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, len, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r5, r6
		orr	r4, r4, r5, lspush #8
		mov	r5, r5, lspull #24
		orr	r5, r5, r6, lspush #8
		stmia	dst!, {r4, r5}
		adcs	sum, sum, r4
		adcs	sum, sum, r5
		mov	r4, r6, lspull #24
		tst	ip, #4
		beq	4f
3:		load1l	r5
		orr	r4, r4, r5, lspush #8
		str	r4, [dst], #4
		adcs	sum, sum, r4
		mov	r4, r5, lspull #24
4:		ands	len, len, #3
		beq	.Ldone
		mov	r5, r4, get_byte_0
		tst	len, #2
		beq	.Lexit
		strb	r5, [dst], #1
		adcs	sum, sum, r4
		load1l	r4
		mov	r5, r4, get_byte_0
		strb	r5, [dst], #1
		adcs	sum, sum, r4, lspush #24
		mov	r5, r4, get_byte_1
		b	.Lexit
FN_EXIT
