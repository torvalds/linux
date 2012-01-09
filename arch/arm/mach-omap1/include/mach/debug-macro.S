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
		.macro	addruart, rp, rv, tmp

		/* Use omap_uart_phys/virt if already configured */
9:		adr	\rp, 99f		@ get effective addr of 99f
		ldr	\rv, [\rp]		@ get absolute addr of 99f
		sub	\rv, \rv, \rp		@ offset between the two
		ldr	\rp, [\rp, #4]		@ abs addr of omap_uart_phys
		sub	\tmp, \rp, \rv		@ make it effective
		ldr	\rp, [\tmp, #0]		@ omap_uart_phys
		ldr	\rv, [\tmp, #4]		@ omap_uart_virt
		cmp	\rp, #0			@ is port configured?
		cmpne	\rv, #0
		bne	100f			@ already configured

		/* Check the debug UART configuration set in uncompress.h */
		and	\rp, pc, #0xff000000
		ldr	\rv, =OMAP_UART_INFO_OFS
		ldr	\rp, [\rp, \rv]

		/* Select the UART to use based on the UART1 scratchpad value */
10:		cmp	\rp, #0			@ no port configured?
		beq	11f			@ if none, try to use UART1
		cmp	\rp, #OMAP1UART1
		beq	11f			@ configure OMAP1UART1
		cmp	\rp, #OMAP1UART2
		beq	12f			@ configure OMAP1UART2
		cmp	\rp, #OMAP1UART3
		beq	13f			@ configure OMAP2UART3

		/* Configure the UART offset from the phys/virt base */
11:		mov	\rp, #0x00fb0000	@ OMAP1UART1
		b	98f
12:		mov	\rp, #0x00fb0000	@ OMAP1UART1
		orr	\rp, \rp, #0x00000800	@ OMAP1UART2
		b	98f
13:		mov	\rp, #0x00fb0000	@ OMAP1UART1
		orr	\rp, \rp, #0x00000800	@ OMAP1UART2
		orr	\rp, \rp, #0x00009000	@ OMAP1UART3

		/* Store both phys and virt address for the uart */
98:		add	\rp, \rp, #0xff000000	@ phys base
		str	\rp, [\tmp, #0]		@ omap_uart_phys
		sub	\rp, \rp, #0xff000000	@ phys base
		add	\rp, \rp, #0xfe000000	@ virt base
		str	\rp, [\tmp, #4]		@ omap_uart_virt
		b	9b

		.align
99:		.word	.
		.word	omap_uart_phys
		.ltorg

100:
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
