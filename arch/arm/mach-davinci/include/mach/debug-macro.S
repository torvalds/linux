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

#define davinci_uart_v2p(x)	((x) - PAGE_OFFSET + PLAT_PHYS_OFFSET)
#define davinci_uart_p2v(x)	((x) - PLAT_PHYS_OFFSET + PAGE_OFFSET)

		.pushsection .data
davinci_uart_phys:	.word	0
davinci_uart_virt:	.word	0
		.popsection

		.macro addruart, rp, rv

		/* Use davinci_uart_phys/virt if already configured */
10:		mrc	p15, 0, \rp, c1, c0
		tst	\rp, #1			@ MMU enabled?
		ldreq	\rp, =davinci_uart_v2p(davinci_uart_phys)
		ldrne	\rp, =davinci_uart_phys
		add	\rv, \rp, #4		@ davinci_uart_virt
		ldr	\rp, [\rp, #0]
		ldr	\rv, [\rv, #0]
		cmp	\rp, #0			@ is port configured?
		cmpne	\rv, #0
		bne	99f			@ already configured

		/* Check the debug UART address set in uncompress.h */
		mrc	p15, 0, \rp, c1, c0
		tst	\rp, #1			@ MMU enabled?

		/* Copy uart phys address from decompressor uart info */
		ldreq	\rv, =davinci_uart_v2p(davinci_uart_phys)
		ldrne	\rv, =davinci_uart_phys
		ldreq	\rp, =DAVINCI_UART_INFO
		ldrne	\rp, =davinci_uart_p2v(DAVINCI_UART_INFO)
		ldr	\rp, [\rp, #0]
		str	\rp, [\rv]

		/* Copy uart virt address from decompressor uart info */
		ldreq	\rv, =davinci_uart_v2p(davinci_uart_virt)
		ldrne	\rv, =davinci_uart_virt
		ldreq	\rp, =DAVINCI_UART_INFO
		ldrne	\rp, =davinci_uart_p2v(DAVINCI_UART_INFO)
		ldr	\rp, [\rp, #4]
		str	\rp, [\rv]

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

