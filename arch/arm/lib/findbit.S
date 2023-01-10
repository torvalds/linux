/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/lib/findbit.S
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * 16th March 2001 - John Ripley <jripley@sonicblue.com>
 *   Fixed so that "size" is an exclusive not an inclusive quantity.
 *   All users of these functions expect exclusive sizes, and may
 *   also call with zero size.
 * Reworked by rmk.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/unwind.h>
                .text

#ifdef __ARMEB__
#define SWAB_ENDIAN le
#else
#define SWAB_ENDIAN be
#endif

		.macro	find_first, endian, set, name
ENTRY(_find_first_\name\()bit_\endian)
	UNWIND(	.fnstart)
		teq	r1, #0
		beq	3f
		mov	r2, #0
1:		ldr	r3, [r0], #4
		.ifeq \set
		mvns	r3, r3			@ invert/test bits
		.else
		movs	r3, r3			@ test bits
		.endif
		.ifc \endian, SWAB_ENDIAN
		bne	.L_found_swab
		.else
		bne	.L_found		@ found the bit?
		.endif
		add	r2, r2, #32		@ next index
2:		cmp	r2, r1			@ any more?
		blo	1b
3:		mov	r0, r1			@ no more bits
		ret	lr
	UNWIND(	.fnend)
ENDPROC(_find_first_\name\()bit_\endian)
		.endm

		.macro	find_next, endian, set, name
ENTRY(_find_next_\name\()bit_\endian)
	UNWIND(	.fnstart)
		cmp	r2, r1
		bhs	3b
		mov	ip, r2, lsr #5		@ word index
		add	r0, r0, ip, lsl #2
		ands	ip, r2, #31		@ bit position
		beq	1b
		ldr	r3, [r0], #4
		.ifeq \set
		mvn	r3, r3			@ invert bits
		.endif
		.ifc \endian, SWAB_ENDIAN
		rev_l	r3, ip
		.if	.Lrev_l_uses_tmp
		@ we need to recompute ip because rev_l will have overwritten
		@ it.
		and	ip, r2, #31		@ bit position
		.endif
		.endif
		movs	r3, r3, lsr ip		@ shift off unused bits
		bne	.L_found
		orr	r2, r2, #31		@ no zero bits
		add	r2, r2, #1		@ align bit pointer
		b	2b			@ loop for next bit
	UNWIND(	.fnend)
ENDPROC(_find_next_\name\()bit_\endian)
		.endm

		.macro	find_bit, endian, set, name
		find_first \endian, \set, \name
		find_next  \endian, \set, \name
		.endm

/* _find_first_zero_bit_le and _find_next_zero_bit_le */
		find_bit le, 0, zero_

/* _find_first_bit_le and _find_next_bit_le */
		find_bit le, 1

#ifdef __ARMEB__

/* _find_first_zero_bit_be and _find_next_zero_bit_be */
		find_bit be, 0, zero_

/* _find_first_bit_be and _find_next_bit_be */
		find_bit be, 1

#endif

/*
 * One or more bits in the LSB of r3 are assumed to be set.
 */
.L_found_swab:
	UNWIND(	.fnstart)
		rev_l	r3, ip
.L_found:
#if __LINUX_ARM_ARCH__ >= 7
		rbit	r3, r3			@ reverse bits
		clz	r3, r3			@ count high zero bits
		add	r0, r2, r3		@ add offset of first set bit
#elif __LINUX_ARM_ARCH__ >= 5
		rsb	r0, r3, #0
		and	r3, r3, r0		@ mask out lowest bit set
		clz	r3, r3			@ count high zero bits
		rsb	r3, r3, #31		@ offset of first set bit
		add	r0, r2, r3		@ add offset of first set bit
#else
		mov	ip, #~0
		tst	r3, ip, lsr #16		@ test bits 0-15
		addeq	r2, r2, #16
		moveq	r3, r3, lsr #16
		tst	r3, #0x00ff
		addeq	r2, r2, #8
		moveq	r3, r3, lsr #8
		tst	r3, #0x000f
		addeq	r2, r2, #4
		moveq	r3, r3, lsr #4
		tst	r3, #0x0003
		addeq	r2, r2, #2
		moveq	r3, r3, lsr #2
		tst	r3, #0x0001
		addeq	r2, r2, #1
		mov	r0, r2
#endif
		cmp	r1, r0			@ Clamp to maxbit
		movlo	r0, r1
		ret	lr
	UNWIND(	.fnend)
