/* arch/arm/mach-clps711x/include/mach/debug-macro.S
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
#include <asm/hardware/clps7111.h>

		.macro	addruart, rp, rv, tmp
#ifndef CONFIG_DEBUG_CLPS711X_UART2
		mov	\rp, #0x0000	@ UART1
#else
		mov	\rp, #0x1000	@ UART2
#endif
		orr	\rv, \rp, #CLPS7111_VIRT_BASE
		orr	\rp, \rp, #CLPS7111_PHYS_BASE
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0x0480]	@ UARTDR
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #0x0140]	@ SYSFLGx
		tst	\rd, #1 << 11		@ UBUSYx
		bne	1001b
		.endm

		.macro	busyuart,rd,rx
		tst	\rx, #0x1000		@ UART2 does not have CTS here
		bne	1002f
1001:		ldr	\rd, [\rx, #0x0140]	@ SYSFLGx
		tst	\rd, #1 << 8		@ CTS
		bne	1001b
1002:
		.endm

