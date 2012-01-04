/*
 * Copyright (C) 1994-1999 Russell King
 * Copyright (C) 2008-2009 Telechips
 * Copyright (C) 2009 Hans J. Koch <hjk@linutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

		.macro	addruart, rp, rv, tmp
		moveq	\rp, #0x90000000	@ physical base address
		movne	\rv, #0xF1000000	@ virtual base
		orr	\rp, \rp, #0x00007000	@ UART0
		orr	\rv, \rv, #0x00007000	@ UART0
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx, #0x44]
		.endm

		.macro	waituart,rd,rx
		.endm

		.macro	busyuart,rd,rx
1001:
		ldr \rd, [\rx, #0x14]
		tst \rd, #0x20

		beq 1001b
		.endm
