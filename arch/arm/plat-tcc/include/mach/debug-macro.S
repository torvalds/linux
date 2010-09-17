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

		.macro	addruart,rx,tmp
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x90000000	@ physical base address
		movne	\rx, #0xF1000000	@ virtual base
		orr	\rx, \rx, #0x00007000	@ UART0
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
