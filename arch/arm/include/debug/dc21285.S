/* SPDX-License-Identifier: GPL-2.0-only */
/* arch/arm/mach-footbridge/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
*/

#include <asm/hardware/dec21285.h>

#include <mach/hardware.h>
	/* For EBSA285 debugging */
		.equ	dc21285_high, ARMCSR_BASE & 0xff000000
		.equ	dc21285_low,  ARMCSR_BASE & 0x00ffffff

		.macro	addruart, rp, rv, tmp
		.if	dc21285_low
		mov	\rp, #dc21285_low
		.else
		mov	\rp, #0
		.endif
		orr	\rv, \rp, #dc21285_high
		orr	\rp, \rp, #0x42000000
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0x160]	@ UARTDR
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #0x178]	@ UARTFLG
		tst	\rd, #1 << 3
		bne	1001b
		.endm

		.macro	waituartcts,rd,rx
		.endm

		.macro	waituarttxrdy,rd,rx
		.endm
