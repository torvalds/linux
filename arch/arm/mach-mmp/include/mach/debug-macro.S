/* arch/arm/mach-mmp/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copied from arch/arm/mach-pxa/include/mach/debug.S
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <mach/addr-map.h>

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =APB_PHYS_BASE		@ physical
		ldr	\rv, =APB_VIRT_BASE		@ virtual
		orr	\rp, \rp, #0x00017000
		orr	\rv, \rv, #0x00017000
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>
