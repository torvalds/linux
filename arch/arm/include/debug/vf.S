/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define VF_UART0_BASE_ADDR	0x40027000
#define VF_UART1_BASE_ADDR	0x40028000
#define VF_UART2_BASE_ADDR	0x40029000
#define VF_UART3_BASE_ADDR	0x4002a000
#define VF_UART_BASE_ADDR(n)	VF_UART##n##_BASE_ADDR
#define VF_UART_BASE(n)		VF_UART_BASE_ADDR(n)
#define VF_UART_PHYSICAL_BASE	VF_UART_BASE(CONFIG_DEBUG_VF_UART_PORT)

#define VF_UART_VIRTUAL_BASE	0xfe000000

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =VF_UART_PHYSICAL_BASE 	@ physical
	and	\rv, \rp, #0xffffff		@ offset within 16MB section
	add	\rv, \rv, #VF_UART_VIRTUAL_BASE
	.endm

	.macro	senduart, rd, rx
	strb	\rd, [\rx, #0x7]	@ Data Register
	.endm

	.macro	busyuart, rd, rx
1001:	ldrb	\rd, [\rx, #0x4]	@ Status Register 1
	tst	\rd, #1 << 6		@ TC
	beq	1001b			@ wait until transmit done
	.endm

	.macro	waituart,rd,rx
	.endm
