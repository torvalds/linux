/*
 * Debugging macro for DaVinci
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/* Modifications
 * Jan 2009	Chaithrika U S	Added senduart, busyuart, waituart
 *				macros, based on debug-8250.S file
 *				but using 32-bit accesses required for
 *                              some davinci devices.
 */

#include <linux/serial_reg.h>

#include <mach/serial.h>

#define UART_SHIFT	2

#if defined(CONFIG_DEBUG_DAVINCI_DMx_UART0)
#define UART_BASE	DAVINCI_UART0_BASE
#elif defined(CONFIG_DEBUG_DAVINCI_DA8XX_UART1)
#define UART_BASE	DA8XX_UART1_BASE
#elif defined(CONFIG_DEBUG_DAVINCI_DA8XX_UART2)
#define UART_BASE	DA8XX_UART2_BASE
#elif defined(CONFIG_DEBUG_DAVINCI_TNETV107X_UART1)
#define UART_BASE	TNETV107X_UART2_BASE
#define UART_VIRTBASE	TNETV107X_UART2_VIRT
#else
#error "Select a specifc port for DEBUG_LL"
#endif

#ifndef UART_VIRTBASE
#define UART_VIRTBASE	IO_ADDRESS(UART_BASE)
#endif

		.macro addruart, rp, rv, tmp
		ldr	\rp, =UART_BASE
		ldr	\rv, =UART_VIRTBASE
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #UART_TX << UART_SHIFT]
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #UART_LSR << UART_SHIFT]
		and	\rd, \rd, #UART_LSR_TEMT | UART_LSR_THRE
		teq	\rd, #UART_LSR_TEMT | UART_LSR_THRE
		bne	1002b
		.endm

		.macro	waituart,rd,rx
#ifdef FLOW_CONTROL
1001:		ldr	\rd, [\rx, #UART_MSR << UART_SHIFT]
		tst	\rd, #UART_MSR_CTS
		beq	1001b
#endif
		.endm

