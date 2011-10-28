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

#include <asm/memory.h>

#include <plat/serial.h>

#define UART_OFFSET(addr)	((addr) & 0x00ffffff)

#define omap_uart_v2p(x)	((x) - PAGE_OFFSET + PLAT_PHYS_OFFSET)
#define omap_uart_p2v(x)	((x) - PLAT_PHYS_OFFSET + PAGE_OFFSET)

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
		.macro	addruart, rp, rv

		/* Use omap_uart_phys/virt if already configured */
10:		mrc	p15, 0, \rp, c1, c0
		tst	\rp, #1			@ MMU enabled?
		ldreq	\rp, =omap_uart_v2p(omap_uart_phys)	@ MMU disabled
		ldrne	\rp, =omap_uart_phys	@ MMU enabled
		add	\rv, \rp, #4		@ omap_uart_virt
		ldr	\rp, [\rp, #0]
		ldr	\rv, [\rv, #0]
		cmp	\rp, #0			@ is port configured?
		cmpne	\rv, #0
		bne	99f			@ already configured

		/* Check the debug UART configuration set in uncompress.h */
		mrc	p15, 0, \rp, c1, c0
		tst	\rp, #1			@ MMU enabled?
		ldreq	\rp, =OMAP_UART_INFO	@ MMU not enabled
		ldrne	\rp, =omap_uart_p2v(OMAP_UART_INFO)	@ MMU enabled
		ldr	\rp, [\rp, #0]

		/* Select the UART to use based on the UART1 scratchpad value */
		cmp	\rp, #0			@ no port configured?
		beq	21f			@ if none, try to use UART1
		cmp	\rp, #OMAP2UART1	@ OMAP2/3/4UART1
		beq	21f			@ configure OMAP2/3/4UART1
		cmp	\rp, #OMAP2UART2	@ OMAP2/3/4UART2
		beq	22f			@ configure OMAP2/3/4UART2
		cmp	\rp, #OMAP2UART3	@ only on 24xx
		beq	23f			@ configure OMAP2UART3
		cmp	\rp, #OMAP3UART3	@ only on 34xx
		beq	33f			@ configure OMAP3UART3
		cmp	\rp, #OMAP4UART3	@ only on 44xx
		beq	43f			@ configure OMAP4UART3
		cmp	\rp, #OMAP3UART4	@ only on 36xx
		beq	34f			@ configure OMAP3UART4
		cmp	\rp, #OMAP4UART4	@ only on 44xx
		beq	44f			@ configure OMAP4UART4
		cmp	\rp, #TI816XUART1	@ ti816x UART offsets different
		beq	81f			@ configure UART1
		cmp	\rp, #TI816XUART2	@ ti816x UART offsets different
		beq	82f			@ configure UART2
		cmp	\rp, #TI816XUART3	@ ti816x UART offsets different
		beq	83f			@ configure UART3
		cmp	\rp, #ZOOM_UART		@ only on zoom2/3
		beq	95f			@ configure ZOOM_UART

		/* Configure the UART offset from the phys/virt base */
21:		mov	\rp, #UART_OFFSET(OMAP2_UART1_BASE)	@ omap2/3/4
		b	98f
22:		mov	\rp, #UART_OFFSET(OMAP2_UART2_BASE)	@ omap2/3/4
		b	98f
23:		mov	\rp, #UART_OFFSET(OMAP2_UART3_BASE)
		b	98f
33:		mov	\rp, #UART_OFFSET(OMAP3_UART1_BASE)
		add	\rp, \rp, #0x00fb0000
		add	\rp, \rp, #0x00006000		@ OMAP3_UART3_BASE
		b	98f
34:		mov	\rp, #UART_OFFSET(OMAP3_UART1_BASE)
		add	\rp, \rp, #0x00fb0000
		add	\rp, \rp, #0x00028000		@ OMAP3_UART4_BASE
		b	98f
43:		mov	\rp, #UART_OFFSET(OMAP4_UART3_BASE)
		b	98f
44:		mov	\rp, #UART_OFFSET(OMAP4_UART4_BASE)
		b	98f
81:		mov	\rp, #UART_OFFSET(TI816X_UART1_BASE)
		b	98f
82:		mov	\rp, #UART_OFFSET(TI816X_UART2_BASE)
		b	98f
83:		mov	\rp, #UART_OFFSET(TI816X_UART3_BASE)
		b	98f
95:		ldr	\rp, =ZOOM_UART_BASE
		mrc	p15, 0, \rv, c1, c0
		tst	\rv, #1			@ MMU enabled?
		ldreq	\rv, =omap_uart_v2p(omap_uart_phys)	@ MMU disabled
		ldrne	\rv, =omap_uart_phys	@ MMU enabled
		str	\rp, [\rv, #0]
		ldr	\rp, =ZOOM_UART_VIRT
		add	\rv, \rv, #4		@ omap_uart_virt
		str	\rp, [\rv, #0]
		mov	\rp, #(UART_LSR << ZOOM_PORT_SHIFT)
		add	\rv, \rv, #4		@ omap_uart_lsr
		str	\rp, [\rv, #0]
		b	10b

		/* Store both phys and virt address for the uart */
98:		add	\rp, \rp, #0x48000000	@ phys base
		mrc	p15, 0, \rv, c1, c0
		tst	\rv, #1			@ MMU enabled?
		ldreq	\rv, =omap_uart_v2p(omap_uart_phys)	@ MMU disabled
		ldrne	\rv, =omap_uart_phys	@ MMU enabled
		str	\rp, [\rv, #0]
		sub	\rp, \rp, #0x48000000	@ phys base
		add	\rp, \rp, #0xfa000000	@ virt base
		add	\rv, \rv, #4		@ omap_uart_virt
		str	\rp, [\rv, #0]
		mov	\rp, #(UART_LSR << OMAP_PORT_SHIFT)
		add	\rv, \rv, #4		@ omap_uart_lsr
		str	\rp, [\rv, #0]

		b	10b
99:
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx]
		.endm

		.macro	busyuart,rd,rx
1001:		mrc	p15, 0, \rd, c1, c0
		tst	\rd, #1			@ MMU enabled?
		ldreq	\rd, =omap_uart_v2p(omap_uart_lsr)	@ MMU disabled
		ldrne	\rd, =omap_uart_lsr	@ MMU enabled
		ldr	\rd, [\rd, #0]
		ldrb	\rd, [\rx, \rd]
		and	\rd, \rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		teq	\rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		bne	1001b
		.endm

		.macro	waituart,rd,rx
		.endm
