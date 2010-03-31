/* arch/arm/mach-sa1100/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/
#include <mach/hardware.h>

		.macro	addruart, rx, tmp
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x80000000	@ physical base address
		movne	\rx, #0xf8000000	@ virtual address

		@ We probe for the active serial port here, coherently with
		@ the comment in arch/arm/mach-sa1100/include/mach/uncompress.h.
		@ We assume r1 can be clobbered.

		@ see if Ser3 is active
		add	\rx, \rx, #0x00050000
		ldr	r1, [\rx, #UTCR3]
		tst	r1, #UTCR3_TXE

		@ if Ser3 is inactive, then try Ser1
		addeq	\rx, \rx, #(0x00010000 - 0x00050000)
		ldreq	r1, [\rx, #UTCR3]
		tsteq	r1, #UTCR3_TXE

		@ if Ser1 is inactive, then try Ser2
		addeq	\rx, \rx, #(0x00030000 - 0x00010000)
		ldreq	r1, [\rx, #UTCR3]
		tsteq	r1, #UTCR3_TXE

		@ if all ports are inactive, then there is nothing we can do
		moveq	pc, lr
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #UTDR]
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #UTSR1]
		tst	\rd, #UTSR1_TNF
		beq	1001b
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #UTSR1]
		tst	\rd, #UTSR1_TBY
		bne	1001b
		.endm
