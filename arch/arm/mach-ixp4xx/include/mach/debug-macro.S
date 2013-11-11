/* arch/arm/mach-ixp4xx/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

                .macro  addruart, rp, rv, tmp
#ifdef __ARMEB__
                mov     \rp, #3         @ Uart regs are at off set of 3 if
					@ byte writes used - Big Endian.
#else
		mov	\rp, #0
#endif
		orr	\rv, \rp, #0xfe000000	@ virtual
		orr	\rv, \rv, #0x00f00000
                orr     \rp, \rp, #0xc8000000	@ physical
                .endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>
