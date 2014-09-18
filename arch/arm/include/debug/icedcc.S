/*
 *  arch/arm/include/debug/icedcc.S
 *
 *  Copyright (C) 1994-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

		@@ debug using ARM EmbeddedICE DCC channel

		.macro	addruart, rp, rv, tmp
		.endm

#if defined(CONFIG_CPU_V6) || defined(CONFIG_CPU_V6K) || defined(CONFIG_CPU_V7)

		.macro	senduart, rd, rx
		mcr	p14, 0, \rd, c0, c5, 0
		.endm

		.macro	busyuart, rd, rx
1001:
		mrc	p14, 0, \rx, c0, c1, 0
		tst	\rx, #0x20000000
		beq	1001b
		.endm

		.macro	waituart, rd, rx
		mov	\rd, #0x2000000
1001:
		subs	\rd, \rd, #1
		bmi	1002f
		mrc	p14, 0, \rx, c0, c1, 0
		tst	\rx, #0x20000000
		bne	1001b
1002:
		.endm

#elif defined(CONFIG_CPU_XSCALE)

		.macro	senduart, rd, rx
		mcr	p14, 0, \rd, c8, c0, 0
		.endm

		.macro	busyuart, rd, rx
1001:
		mrc	p14, 0, \rx, c14, c0, 0
		tst	\rx, #0x10000000
		beq	1001b
		.endm

		.macro	waituart, rd, rx
		mov	\rd, #0x10000000
1001:
		subs	\rd, \rd, #1
		bmi	1002f
		mrc	p14, 0, \rx, c14, c0, 0
		tst	\rx, #0x10000000
		bne	1001b
1002:
		.endm

#else

		.macro	senduart, rd, rx
		mcr	p14, 0, \rd, c1, c0, 0
		.endm

		.macro	busyuart, rd, rx
1001:
		mrc	p14, 0, \rx, c0, c0, 0
		tst	\rx, #2
		beq	1001b

		.endm

		.macro	waituart, rd, rx
		mov	\rd, #0x2000000
1001:
		subs	\rd, \rd, #1
		bmi	1002f
		mrc	p14, 0, \rx, c0, c0, 0
		tst	\rx, #2
		bne	1001b
1002:
		.endm

#endif	/* CONFIG_CPU_V6 */
