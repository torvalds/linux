/* arch/arm/mach-mxs/include/mach/debug-macro.S
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

#ifdef CONFIG_DEBUG_IMX23_UART
#define UART_PADDR	0x80070000
#elif defined (CONFIG_DEBUG_IMX28_UART)
#define UART_PADDR	0x80074000
#endif

#define UART_VADDR	0xfe100000

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =UART_PADDR	@ physical
		ldr	\rv, =UART_VADDR	@ virtual
		.endm

#include <asm/hardware/debug-pl01x.S>
