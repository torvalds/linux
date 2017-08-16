/*
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

/* External port on Zoom2/3 */
#define ZOOM_UART_BASE		0x10000000
#define ZOOM_UART_VIRT		0xfa400000

#define OMAP_PORT_SHIFT		2
#define ZOOM_PORT_SHIFT		1

#define UART_OFFSET(addr)	((addr) & 0x00ffffff)

		.pushsection .data
omap_uart_phys:	.word	0
omap_uart_virt:	.word	0
omap_uart_lsr:	.word	0
		.popsection

		.macro	addruart, rp, rv, tmp

		/* Use omap_uart_phys/virt if already configured */
10:		adr	\rp, 99f		@ get effective addr of 99f
		ldr	\rv, [\rp]		@ get absolute addr of 99f
		sub	\rv, \rv, \rp		@ offset between the two
		ldr	\rp, [\rp, #4]		@ abs addr of omap_uart_phys
		sub	\tmp, \rp, \rv		@ make it effective
		ldr	\rp, [\tmp, #0]		@ omap_uart_phys
		ldr	\rv, [\tmp, #4]		@ omap_uart_virt
		cmp	\rp, #0			@ is port configured?
		cmpne	\rv, #0
		bne	100f			@ already configured

		/* Configure the UART offset from the phys/virt base */
#ifdef CONFIG_DEBUG_ZOOM_UART
		ldr	\rp, =ZOOM_UART_BASE
		str	\rp, [\tmp, #0]		@ omap_uart_phys
		ldr	\rp, =ZOOM_UART_VIRT
		str	\rp, [\tmp, #4]		@ omap_uart_virt
		mov	\rp, #(UART_LSR << ZOOM_PORT_SHIFT)
		str	\rp, [\tmp, #8]		@ omap_uart_lsr
#endif
		b	10b

		.align
99:		.word	.
		.word	omap_uart_phys
		.ltorg

100:		/* Pass the UART_LSR reg address */
		ldr	\tmp, [\tmp, #8]	@ omap_uart_lsr
		add	\rp, \rp, \tmp
		add	\rv, \rv, \tmp
		.endm

		.macro	senduart,rd,rx
		orr	\rd, \rd, \rx, lsl #24	@ preserve LSR reg offset
		bic	\rx, \rx, #0xff		@ get base (THR) reg address
		strb	\rd, [\rx]		@ send lower byte of rd
		orr	\rx, \rx, \rd, lsr #24	@ restore original rx (LSR)
		bic	\rd, \rd, #(0xff << 24)	@ restore original rd
		.endm

		.macro	busyuart,rd,rx
1001:		ldrb	\rd, [\rx]		@ rx contains UART_LSR address
		and	\rd, \rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		teq	\rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		bne	1001b
		.endm

		.macro	waituart,rd,rx
		.endm
