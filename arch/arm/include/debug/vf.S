/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =0x40028000	@ physical
	ldr	\rv, =0xfe028000	@ virtual
	.endm

	.macro	senduart, rd, rx
	strb	\rd, [\rx, #0x7]	@ Data Register
	.endm

	.macro	busyuart, rd, rx
1001:	ldrb	\rd, [\rx, #0x4]	@ Status Register 1
	tst	\rd, #1 << 6		@ TC
	beq	1001b			@ wait until transmit done
	.endm

	.macro	waituart,rd,rx
	.endm
