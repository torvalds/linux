/*
 *  linux/arch/arm/lib/delay.S
 *
 *  Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text

.LC0:		.word	loops_per_jiffy

/*
 * 0 <= r0 <= 2000
 */
ENTRY(__udelay)
		mov	r2,     #0x6800
		orr	r2, r2, #0x00db
		mul	r0, r2, r0
ENTRY(__const_udelay)				@ 0 <= r0 <= 0x01ffffff
		ldr	r2, .LC0
		ldr	r2, [r2]		@ max = 0x0fffffff
		mov	r0, r0, lsr #11		@ max = 0x00003fff
		mov	r2, r2, lsr #11		@ max = 0x0003ffff
		mul	r0, r2, r0		@ max = 2^32-1
		movs	r0, r0, lsr #6
		RETINSTR(moveq,pc,lr)

/*
 * loops = (r0 * 0x10c6 * 100 * loops_per_jiffy) / 2^32
 *
 * Oh, if only we had a cycle counter...
 */

@ Delay routine
ENTRY(__delay)
		subs	r0, r0, #1
#if 0
		RETINSTR(movls,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movls,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movls,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movls,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movls,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movls,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movls,pc,lr)
		subs	r0, r0, #1
#endif
		bhi	__delay
		RETINSTR(mov,pc,lr)
