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

                .macro  addruart, rx, tmp
                mrc     p15, 0, \rx, c1, c0
                tst     \rx, #1                 @ MMU enabled?
                moveq   \rx, #0xc8000000
                movne   \rx, #0xff000000
		orrne	\rx, \rx, #0x00b00000
#ifdef __ARMEB__
                add     \rx,\rx,#3              @ Uart regs are at off set of 3 if
						@ byte writes used - Big Endian.
#endif
                .endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>
