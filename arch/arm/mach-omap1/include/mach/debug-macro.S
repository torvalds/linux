/* arch/arm/mach-omap1/include/mach/debug-macro.S
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

#include <linux/serial_reg.h>

#include <asm/memory.h>

#include <plat/serial.h>

		.pushsection .data
omap_uart_phys:	.word	0x0
omap_uart_virt:	.word	0x0
		.popsection

		/*
		 * Note that this code won't work if the bootloader passes
		 * a wrong machine ID number in r1. To debug, just hardcode
		 * the desired UART phys and virt addresses temporarily into
		 * the omap_uart_phys and omap_uart_virt above.
		 */
		.macro	addruart, rx, tmp

		/* Use omap_uart_phys/virt if already configured */
9:		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		ldreq	\rx, =__virt_to_phys(omap_uart_phys)	@ physical base address
		ldrne	\rx, =omap_uart_virt	@ virtual base
		ldr	\rx, [\rx, #0]
		cmp	\rx, #0			@ is port configured?
		bne	99f			@ already configured

		/* Check the debug UART configuration set in uncompress.h */
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		ldreq	\rx, =OMAP_UART_INFO
		ldrne	\rx, =__phys_to_virt(OMAP_UART_INFO)
		ldr	\rx, [\rx, #0]

		/* Select the UART to use based on the UART1 scratchpad value */
10:		cmp	\rx, #0			@ no port configured?
		beq	11f			@ if none, try to use UART1
		cmp	\rx, #OMAP1UART1
		beq	11f			@ configure OMAP1UART1
		cmp	\rx, #OMAP1UART2
		beq	12f			@ configure OMAP1UART2
		cmp	\rx, #OMAP1UART3
		beq	13f			@ configure OMAP2UART3

		/* Configure the UART offset from the phys/virt base */
11:		mov	\rx, #0x00fb0000	@ OMAP1UART1
		b	98f
12:		mov	\rx, #0x00fb0000	@ OMAP1UART1
		orr	\rx, \rx, #0x00000800	@ OMAP1UART2
		b	98f
13:		mov	\rx, #0x00fb0000	@ OMAP1UART1
		orr	\rx, \rx, #0x00000800	@ OMAP1UART2
		orr	\rx, \rx, #0x00009000	@ OMAP1UART3

		/* Store both phys and virt address for the uart */
98:		add	\rx, \rx, #0xff000000	@ phys base
		mrc	p15, 0, \tmp, c1, c0
		tst	\tmp, #1		@ MMU enabled?
		ldreq	\tmp, =__virt_to_phys(omap_uart_phys)
		ldrne	\tmp, =omap_uart_phys
		str	\rx, [\tmp, #0]
		sub	\rx, \rx, #0xff000000	@ phys base
		add	\rx, \rx, #0xfe000000	@ virt base
		ldreq	\tmp, =__virt_to_phys(omap_uart_virt)
		ldrne	\tmp, =omap_uart_virt
		str	\rx, [\tmp, #0]
		b	9b
99:
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx]
		.endm

		.macro	busyuart,rd,rx
1001:		ldrb	\rd, [\rx, #(UART_LSR << OMAP_PORT_SHIFT)]
		and	\rd, \rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		teq	\rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		beq	1002f
		ldrb	\rd, [\rx, #(UART_LSR << OMAP7XX_PORT_SHIFT)]
		and	\rd, \rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		teq	\rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		bne	1001b
1002:
		.endm

		.macro	waituart,rd,rx
		.endm
