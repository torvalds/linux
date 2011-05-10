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

#include <mach/mx23.h>
#include <mach/mx28.h>

#ifdef CONFIG_SOC_IMX23
#ifdef UART_PADDR
#error "CONFIG_DEBUG_LL is incompatible with multiple archs"
#endif
#define UART_PADDR	MX23_DUART_BASE_ADDR
#endif

#ifdef CONFIG_SOC_IMX28
#ifdef UART_PADDR
#error "CONFIG_DEBUG_LL is incompatible with multiple archs"
#endif
#define UART_PADDR	MX28_DUART_BASE_ADDR
#endif

#define UART_VADDR	MXS_IO_ADDRESS(UART_PADDR)

		.macro	addruart, rp, rv
		ldr	\rp, =UART_PADDR	@ physical
		ldr	\rv, =UART_VADDR	@ virtual
		.endm

#include <asm/hardware/debug-pl01x.S>
