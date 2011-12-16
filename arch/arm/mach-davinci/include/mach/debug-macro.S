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

		.pushsection .data
davinci_uart_phys:	.word	0
davinci_uart_virt:	.word	0
		.popsection

		.macro addruart, rp, rv, tmp

		/* Use davinci_uart_phys/virt if already configured */
10:		adr	\rp, 99f		@ get effective addr of 99f
		ldr	\rv, [\rp]		@ get absolute addr of 99f
		sub	\rv, \rv, \rp		@ offset between the two
		ldr	\rp, [\rp, #4]		@ abs addr of omap_uart_phys
		sub	\tmp, \rp, \rv		@ make it effective
		ldr	\rp, [\tmp, #0]		@ davinci_uart_phys
		ldr	\rv, [\tmp, #4]		@ davinci_uart_virt
		cmp	\rp, #0			@ is port configured?
		cmpne	\rv, #0
		bne	100f			@ already configured

		/* Check the debug UART address set in uncompress.h */
		and	\rp, pc, #0xff000000
		ldr	\rv, =DAVINCI_UART_INFO_OFS
		add	\rp, \rp, \rv

		/* Copy uart phys address from decompressor uart info */
		ldr	\rv, [\rp, #0]
		str	\rv, [\tmp, #0]

		/* Copy uart virt address from decompressor uart info */
		ldr	\rv, [\rp, #4]
		str	\rv, [\tmp, #4]

		b	10b

		.align
99:		.word	.
		.word	davinci_uart_phys
		.ltorg

100:
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

