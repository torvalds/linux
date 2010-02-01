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
#define UART_SHIFT	2

		.macro addruart, rx, tmp
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x01000000	@ physical base address
		movne	\rx, #0xfe000000	@ virtual base
#if defined(CONFIG_ARCH_DAVINCI_DA8XX) && defined(CONFIG_ARCH_DAVINCI_DMx)
#error Cannot enable DaVinci and DA8XX platforms concurrently
#elif defined(CONFIG_MACH_DAVINCI_DA830_EVM) || \
	defined(CONFIG_MACH_DAVINCI_DA850_EVM)
		orr	\rx, \rx, #0x00d00000	@ physical base address
		orr	\rx, \rx, #0x0000d000	@ of UART 2
#else
		orr	\rx, \rx, #0x00c20000   @ UART 0
#endif
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

