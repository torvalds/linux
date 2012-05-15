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

#ifdef CONFIG_DEBUG_IMX1_UART
#define UART_PADDR	MX1_UART1_BASE_ADDR
#elif defined (CONFIG_DEBUG_IMX25_UART)
#define UART_PADDR	MX25_UART1_BASE_ADDR
#elif defined (CONFIG_DEBUG_IMX21_IMX27_UART)
#define UART_PADDR	MX2x_UART1_BASE_ADDR
#elif defined (CONFIG_DEBUG_IMX31_IMX35_UART)
#define UART_PADDR	MX3x_UART1_BASE_ADDR
#elif defined (CONFIG_DEBUG_IMX51_UART)
#define UART_PADDR	MX51_UART1_BASE_ADDR
#elif defined (CONFIG_DEBUG_IMX50_IMX53_UART)
#define UART_PADDR	MX53_UART1_BASE_ADDR
#elif defined (CONFIG_DEBUG_IMX6Q_UART4)
#define UART_PADDR	MX6Q_UART4_BASE_ADDR
#endif

#define UART_VADDR	IMX_IO_ADDRESS(UART_PADDR)

		.macro	addruart, rp, rv, tmp
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
