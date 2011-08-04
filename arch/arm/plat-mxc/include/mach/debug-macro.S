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
#include <mach/hardware.h>

#ifdef CONFIG_SOC_IMX1
#define UART_PADDR	MX1_UART1_BASE_ADDR
#endif

#ifdef CONFIG_SOC_IMX25
#ifdef UART_PADDR
#error "CONFIG_DEBUG_LL is incompatible with multiple archs"
#endif
#define UART_PADDR	MX25_UART1_BASE_ADDR
#endif

#if defined(CONFIG_SOC_IMX21) || defined (CONFIG_SOC_IMX27)
#ifdef UART_PADDR
#error "CONFIG_DEBUG_LL is incompatible with multiple archs"
#endif
#define UART_PADDR	MX2x_UART1_BASE_ADDR
#endif

#if defined(CONFIG_SOC_IMX31) || defined(CONFIG_SOC_IMX35)
#ifdef UART_PADDR
#error "CONFIG_DEBUG_LL is incompatible with multiple archs"
#endif
#define UART_PADDR	MX3x_UART1_BASE_ADDR
#endif

#ifdef CONFIG_SOC_IMX51
#ifdef UART_PADDR
#error "CONFIG_DEBUG_LL is incompatible with multiple archs"
#endif
#define UART_PADDR	MX51_UART1_BASE_ADDR
#endif

#define UART_VADDR	IMX_IO_ADDRESS(UART_PADDR)

		.macro	addruart, rp, rv
		ldr	\rp, =UART_PADDR	@ physical
		ldr	\rv, =UART_VADDR	@ virtual
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
