/*
 *  linux/arch/arm26/lib/delay.S
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

LC0:		.word	loops_per_jiffy

/*
 * 0 <= r0 <= 2000
 */
ENTRY(udelay)
		mov	r2,     #0x6800
		orr	r2, r2, #0x00db
		mul	r1, r0, r2
		ldr	r2, LC0
		ldr	r2, [r2]
		mov	r1, r1, lsr #11
		mov	r2, r2, lsr #11
		mul	r0, r1, r2
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
