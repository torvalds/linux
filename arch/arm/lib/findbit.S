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
                .text

		.macro	find_first, endian, set, name
ENTRY(_find_first_\name\()bit_\endian)
		teq	r1, #0
		beq	3f
		mov	r2, #0
1:
		.ifc	\endian, be
		eor	r3, r2, #0x18
 ARM(		ldrb	r3, [r0, r3, lsr #3]	)
 THUMB(		lsr	r3, #3			)
 THUMB(		ldrb	r3, [r0, r3]		)
		.else
 ARM(		ldrb	r3, [r0, r2, lsr #3]	)
 THUMB(		lsr	r3, r2, #3		)
 THUMB(		ldrb	r3, [r0, r3]		)
		.endif
		.ifeq	\set
		eors	r3, r3, #0xff		@ invert bits
		.else
		movs	r3, r3
		.endif
		bne	.L_found		@ any now set - found zero bit
		add	r2, r2, #8		@ next bit pointer
2:		cmp	r2, r1			@ any more?
		blo	1b
3:		mov	r0, r1			@ no free bits
		ret	lr
ENDPROC(_find_first_\name\()bit_\endian)
		.endm

		.macro	find_next, endian, set, name
ENTRY(_find_next_\name\()bit_\endian)
		cmp	r2, r1
		bhs	3b
		ands	ip, r2, #7
		beq	1b			@ If new byte, goto old routine
		.ifc	\endian, be
		eor	r3, r2, #0x18
 ARM(		ldrb	r3, [r0, r3, lsr #3]	)
 THUMB(		lsr	r3, #3			)
 THUMB(		ldrb	r3, [r0, r3]		)
		.else
 ARM(		ldrb	r3, [r0, r2, lsr #3]	)
 THUMB(		lsr	r3, r2, #3		)
 THUMB(		ldrb	r3, [r0, r3]		)
		.endif
		.ifeq	\set
		eor	r3, r3, #0xff		@ now looking for a 1 bit
		.endif
		movs	r3, r3, lsr ip		@ shift off unused bits
		bne	.L_found
		orr	r2, r2, #7		@ if zero, then no bits here
		add	r2, r2, #1		@ align bit pointer
		b	2b			@ loop for next bit
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
		tst	r3, #0x0f
		addeq	r2, r2, #4
		movne	r3, r3, lsl #4
		tst	r3, #0x30
		addeq	r2, r2, #2
		movne	r3, r3, lsl #2
		tst	r3, #0x40
		addeq	r2, r2, #1
		mov	r0, r2
#endif
		cmp	r1, r0			@ Clamp to maxbit
		movlo	r0, r1
		ret	lr

