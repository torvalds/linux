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

/* OMAP2 serial ports */
#define OMAP2_UART1_BASE	0x4806a000
#define OMAP2_UART2_BASE	0x4806c000
#define OMAP2_UART3_BASE	0x4806e000

/* OMAP3 serial ports */
#define OMAP3_UART1_BASE	OMAP2_UART1_BASE
#define OMAP3_UART2_BASE	OMAP2_UART2_BASE
#define OMAP3_UART3_BASE	0x49020000
#define OMAP3_UART4_BASE	0x49042000	/* Only on 36xx */
#define OMAP3_UART4_AM35XX_BASE	0x4809E000	/* Only on AM35xx */

/* OMAP4 serial ports */
#define OMAP4_UART1_BASE	OMAP2_UART1_BASE
#define OMAP4_UART2_BASE	OMAP2_UART2_BASE
#define OMAP4_UART3_BASE	0x48020000
#define OMAP4_UART4_BASE	0x4806e000

/* TI81XX serial ports */
#define TI81XX_UART1_BASE	0x48020000
#define TI81XX_UART2_BASE	0x48022000
#define TI81XX_UART3_BASE	0x48024000

/* AM3505/3517 UART4 */
#define AM35XX_UART4_BASE	0x4809E000	/* Only on AM3505/3517 */

/* AM33XX serial port */
#define AM33XX_UART1_BASE	0x44E09000

/* OMAP5 serial ports */
#define OMAP5_UART1_BASE	OMAP2_UART1_BASE
#define OMAP5_UART2_BASE	OMAP2_UART2_BASE
#define OMAP5_UART3_BASE	OMAP4_UART3_BASE
#define OMAP5_UART4_BASE	OMAP4_UART4_BASE
#define OMAP5_UART5_BASE	0x48066000
#define OMAP5_UART6_BASE	0x48068000

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
#ifdef CONFIG_DEBUG_OMAP2UART1
		mov	\rp, #UART_OFFSET(OMAP2_UART1_BASE)	@ omap2/3/4
		b	98f
#endif
#ifdef CONFIG_DEBUG_OMAP2UART2
		mov	\rp, #UART_OFFSET(OMAP2_UART2_BASE)	@ omap2/3/4
		b	98f
#endif
#ifdef CONFIG_DEBUG_OMAP2UART3
		mov	\rp, #UART_OFFSET(OMAP2_UART3_BASE)
		b	98f
#endif
#ifdef CONFIG_DEBUG_OMAP3UART3
		mov	\rp, #UART_OFFSET(OMAP3_UART1_BASE)
		add	\rp, \rp, #0x00fb0000
		add	\rp, \rp, #0x00006000		@ OMAP3_UART3_BASE
		b	98f
#endif
#ifdef CONFIG_DEBUG_OMAP4UART3
		mov	\rp, #UART_OFFSET(OMAP4_UART3_BASE)
		b	98f
#endif
#ifdef CONFIG_DEBUG_OMAP3UART4
		mov	\rp, #UART_OFFSET(OMAP3_UART1_BASE)
		add	\rp, \rp, #0x00fb0000
		add	\rp, \rp, #0x00028000		@ OMAP3_UART4_BASE
		b	98f
#endif
#ifdef CONFIG_DEBUG_OMAP4UART4
		mov	\rp, #UART_OFFSET(OMAP4_UART4_BASE)
		b	98f
#endif
#ifdef CONFIG_DEBUG_TI81XXUART1
		mov	\rp, #UART_OFFSET(TI81XX_UART1_BASE)
		b	98f
#endif
#ifdef CONFIG_DEBUG_TI81XXUART2
		mov	\rp, #UART_OFFSET(TI81XX_UART2_BASE)
		b	98f
#endif
#ifdef CONFIG_DEBUG_TI81XXUART3
		mov	\rp, #UART_OFFSET(TI81XX_UART3_BASE)
		b	98f
#endif
#ifdef CONFIG_DEBUG_AM33XXUART1
		ldr	\rp, =AM33XX_UART1_BASE
		and	\rp, \rp, #0x00ffffff
		b	97f
#endif
#ifdef CONFIG_DEBUG_ZOOM_UART
		ldr	\rp, =ZOOM_UART_BASE
		str	\rp, [\tmp, #0]		@ omap_uart_phys
		ldr	\rp, =ZOOM_UART_VIRT
		str	\rp, [\tmp, #4]		@ omap_uart_virt
		mov	\rp, #(UART_LSR << ZOOM_PORT_SHIFT)
		str	\rp, [\tmp, #8]		@ omap_uart_lsr
#endif
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
