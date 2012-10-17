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

#if defined(CONFIG_DEBUG_MMP_UART2)
#define MMP_UART_OFFSET	0x00017000
#elif defined(CONFIG_DEBUG_MMP_UART3)
#define MMP_UART_OFFSET	0x00018000
#else
#error "Select uart for DEBUG_LL"
#endif

#include <mach/addr-map.h>

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =APB_PHYS_BASE		@ physical
		ldr	\rv, =APB_VIRT_BASE		@ virtual
		orr	\rp, \rp, #MMP_UART_OFFSET
		orr	\rv, \rv, #MMP_UART_OFFSET
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>
