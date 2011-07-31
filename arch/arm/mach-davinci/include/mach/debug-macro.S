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

#include <asm/memory.h>

#include <mach/serial.h>

#define UART_SHIFT	2

		.pushsection .data
davinci_uart_phys:	.word	0
davinci_uart_virt:	.word	0
		.popsection

		.macro addruart, rx, tmp

		/* Use davinci_uart_phys/virt if already configured */
10:		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		ldreq	\rx, =__virt_to_phys(davinci_uart_phys)
		ldrne	\rx, =davinci_uart_virt
		ldr	\rx, [\rx]
		cmp	\rx, #0			@ is port configured?
		bne	99f			@ already configured

		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?

		/* Copy uart phys address from decompressor uart info */
		ldreq	\tmp, =__virt_to_phys(davinci_uart_phys)
		ldrne	\tmp, =davinci_uart_phys
		ldreq	\rx, =DAVINCI_UART_INFO
		ldrne	\rx, =__phys_to_virt(DAVINCI_UART_INFO)
		ldr	\rx, [\rx, #0]
		str	\rx, [\tmp]

		/* Copy uart virt address from decompressor uart info */
		ldreq	\tmp, =__virt_to_phys(davinci_uart_virt)
		ldrne	\tmp, =davinci_uart_virt
		ldreq	\rx, =DAVINCI_UART_INFO
		ldrne	\rx, =__phys_to_virt(DAVINCI_UART_INFO)
		ldr	\rx, [\rx, #4]
		str	\rx, [\tmp]

		b	10b
99:
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

