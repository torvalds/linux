/* arch/arm/mach-imx/include/mach/debug-macro.S
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
#define IMX_NEEDS_DEPRECATED_SYMBOLS

#ifdef CONFIG_ARCH_MX1
#include <mach/mx1.h>
#define UART_PADDR	UART1_BASE_ADDR
#define UART_VADDR	IO_ADDRESS(UART1_BASE_ADDR)
#endif

#ifdef CONFIG_ARCH_MX25
#ifdef UART_PADDR
#error "CONFIG_DEBUG_LL is incompatible with multiple archs"
#endif
#include <mach/mx25.h>
#define UART_PADDR	MX25_UART1_BASE_ADDR
#define UART_VADDR	MX25_AIPS1_IO_ADDRESS(MX25_UART1_BASE_ADDR)
#endif

#ifdef CONFIG_ARCH_MX2
#ifdef UART_PADDR
#error "CONFIG_DEBUG_LL is incompatible with multiple archs"
#endif
#include <mach/mx2x.h>
#define UART_PADDR	UART1_BASE_ADDR
#define UART_VADDR	AIPI_IO_ADDRESS(UART1_BASE_ADDR)
#endif

#ifdef CONFIG_ARCH_MX3
#ifdef UART_PADDR
#error "CONFIG_DEBUG_LL is incompatible with multiple archs"
#endif
#include <mach/mx3x.h>
#define UART_PADDR	UART1_BASE_ADDR
#define UART_VADDR	AIPS1_IO_ADDRESS(UART1_BASE_ADDR)
#endif

#ifdef CONFIG_ARCH_MX5
#ifdef UART_PADDR
#error "CONFIG_DEBUG_LL is incompatible with multiple archs"
#endif
#include <mach/mx51.h>
#define UART_PADDR	MX51_UART1_BASE_ADDR
#define UART_VADDR	MX51_AIPS1_IO_ADDRESS(MX51_UART1_BASE_ADDR)
#endif

#ifdef CONFIG_ARCH_MXC91231
#ifdef UART_PADDR
#error "CONFIG_DEBUG_LL is incompatible with multiple archs"
#endif
#include <mach/mxc91231.h>
#define UART_PADDR	MXC91231_UART2_BASE_ADDR
#define UART_VADDR	MXC91231_IO_ADDRESS(MXC91231_UART2_BASE_ADDR)
#endif
		.macro	addruart, rx, tmp
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		ldreq	\rx, =UART_PADDR	@ physical
		ldrne	\rx, =UART_VADDR	@ virtual
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0x40]	@ TXDATA
		.endm

		.macro	waituart,rd,rx
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #0x98]	@ SR2
		tst	\rd, #1 << 3		@ TXDC
		beq	1002b			@ wait until transmit done
		.endm
