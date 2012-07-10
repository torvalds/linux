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

		/* Check the debug UART configuration set in uncompress.h */
		mov	\rp, pc
		ldr	\rv, =OMAP_UART_INFO_OFS
		and	\rp, \rp, #0xff000000
		ldr	\rp, [\rp, \rv]

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
		cmp	\rp, #TI81XXUART1	@ ti81Xx UART offsets different
		beq	81f			@ configure UART1
		cmp	\rp, #TI81XXUART2	@ ti81Xx UART offsets different
		beq	82f			@ configure UART2
		cmp	\rp, #TI81XXUART3	@ ti81Xx UART offsets different
		beq	83f			@ configure UART3
		cmp	\rp, #AM33XXUART1	@ AM33XX UART offsets different
		beq	84f			@ configure UART1
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
81:		mov	\rp, #UART_OFFSET(TI81XX_UART1_BASE)
		b	98f
82:		mov	\rp, #UART_OFFSET(TI81XX_UART2_BASE)
		b	98f
83:		mov	\rp, #UART_OFFSET(TI81XX_UART3_BASE)
		b	98f
84:		ldr	\rp, =AM33XX_UART1_BASE
		and	\rp, \rp, #0x00ffffff
		b	97f
95:		ldr	\rp, =ZOOM_UART_BASE
		str	\rp, [\tmp, #0]		@ omap_uart_phys
		ldr	\rp, =ZOOM_UART_VIRT
		str	\rp, [\tmp, #4]		@ omap_uart_virt
		mov	\rp, #(UART_LSR << ZOOM_PORT_SHIFT)
		str	\rp, [\tmp, #8]		@ omap_uart_lsr
		b	10b

		/* AM33XX: Store both phys and virt address for the uart */
97:		add	\rp, \rp, #0x44000000	@ phys base
		str	\rp, [\tmp, #0]		@ omap_uart_phys
		sub	\rp, \rp, #0x44000000	@ phys base
		add	\rp, \rp, #0xf9000000	@ virt base
		str	\rp, [\tmp, #4]		@ omap_uart_virt
		mov	\rp, #(UART_LSR << OMAP_PORT_SHIFT)
		str	\rp, [\tmp, #8]		@ omap_uart_lsr

		b	10b

		/* Store both phys and virt address for the uart */
98:		add	\rp, \rp, #0x48000000	@ phys base
		str	\rp, [\tmp, #0]		@ omap_uart_phys
		sub	\rp, \rp, #0x48000000	@ phys base
		add	\rp, \rp, #0xfa000000	@ virt base
		str	\rp, [\tmp, #4]		@ omap_uart_virt
		mov	\rp, #(UART_LSR << OMAP_PORT_SHIFT)
		str	\rp, [\tmp, #8]		@ omap_uart_lsr

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
