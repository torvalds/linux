/* arch/arm/mach-omap2/include/mach/debug-macro.S
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

#define UART_OFFSET(addr)	((addr) & 0x00ffffff)

		.pushsection .data
omap_uart_phys:	.word	0
omap_uart_virt:	.word	0
omap_uart_lsr:	.word	0
		.popsection

		/*
		 * Note that this code won't work if the bootloader passes
		 * a wrong machine ID number in r1. To debug, just hardcode
		 * the desired UART phys and virt addresses temporarily into
		 * the omap_uart_phys and omap_uart_virt above.
		 */
		.macro	addruart, rx, tmp

		/* Use omap_uart_phys/virt if already configured */
10:		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		ldreq	\rx, =omap_uart_phys	@ physical base address
		ldrne	\rx, =omap_uart_virt	@ virtual base address
		ldr	\rx, [\rx, #0]
		cmp	\rx, #0			@ is port configured?
		bne	99f			@ already configured

		/* Check UART1 scratchpad register for uart to use */
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x48000000	@ physical base address
		movne	\rx, #0xfa000000	@ virtual base
		orr	\rx, \rx, #0x0006a000	@ uart1 on omap2/3/4
		ldrb	\rx, [\rx, #(UART_SCR << OMAP_PORT_SHIFT)] @ scratchpad

		/* Select the UART to use based on the UART1 scratchpad value */
		cmp	\rx, #0			@ no port configured?
		beq	21f			@ if none, try to use UART1
		cmp	\rx, #OMAP2UART1	@ OMAP2/3/4UART1
		beq	21f			@ configure OMAP2/3/4UART1
		cmp	\rx, #OMAP2UART2	@ OMAP2/3/4UART2
		beq	22f			@ configure OMAP2/3/4UART2
		cmp	\rx, #OMAP2UART3	@ only on 24xx
		beq	23f			@ configure OMAP2UART3
		cmp	\rx, #OMAP3UART3	@ only on 34xx
		beq	33f			@ configure OMAP3UART3
		cmp	\rx, #OMAP4UART3	@ only on 44xx
		beq	43f			@ configure OMAP4UART3
		cmp	\rx, #OMAP3UART4	@ only on 36xx
		beq	34f			@ configure OMAP3UART4
		cmp	\rx, #OMAP4UART4	@ only on 44xx
		beq	44f			@ configure OMAP4UART4
		cmp	\rx, #ZOOM_UART		@ only on zoom2/3
		beq	95f			@ configure ZOOM_UART

		/* Configure the UART offset from the phys/virt base */
21:		mov	\rx, #UART_OFFSET(OMAP2_UART1_BASE)	@ omap2/3/4
		b	98f
22:		mov	\rx, #UART_OFFSET(OMAP2_UART2_BASE)	@ omap2/3/4
		b	98f
23:		mov	\rx, #UART_OFFSET(OMAP2_UART3_BASE)
		b	98f
33:		mov	\rx, #UART_OFFSET(OMAP3_UART1_BASE)
		add	\rx, \rx, #0x00fb0000
		add	\rx, \rx, #0x00006000		@ OMAP3_UART3_BASE
		b	98f
34:		mov	\rx, #UART_OFFSET(OMAP3_UART1_BASE)
		add	\rx, \rx, #0x00fb0000
		add	\rx, \rx, #0x00028000		@ OMAP3_UART4_BASE
		b	98f
43:		mov	\rx, #UART_OFFSET(OMAP4_UART3_BASE)
		b	98f
44:		mov	\rx, #UART_OFFSET(OMAP4_UART4_BASE)
		b	98f
95:		mov	\rx, #ZOOM_UART_BASE
		ldr	\tmp, =omap_uart_phys
		str	\rx, [\tmp, #0]
		mov	\rx, #ZOOM_UART_VIRT
		ldr	\tmp, =omap_uart_virt
		str	\rx, [\tmp, #0]
		mov	\rx, #(UART_LSR << ZOOM_PORT_SHIFT)
		ldr	\tmp, =omap_uart_lsr
		str	\rx, [\tmp, #0]
		b	10b

		/* Store both phys and virt address for the uart */
98:		add	\rx, \rx, #0x48000000	@ phys base
		ldr	\tmp, =omap_uart_phys
		str	\rx, [\tmp, #0]
		sub	\rx, \rx, #0x48000000	@ phys base
		add	\rx, \rx, #0xfa000000	@ virt base
		ldr	\tmp, =omap_uart_virt
		str	\rx, [\tmp, #0]
		mov	\rx, #(UART_LSR << OMAP_PORT_SHIFT)
		ldr	\tmp, =omap_uart_lsr
		str	\rx, [\tmp, #0]

		b	10b
99:
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx]
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, =omap_uart_lsr
		ldr	\rd, [\rd, #0]
		ldrb	\rd, [\rx, \rd]
		and	\rd, \rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		teq	\rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		bne	1001b
		.endm

		.macro	waituart,rd,rx
		.endm
