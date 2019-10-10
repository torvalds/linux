/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arch/arm/mach-prima2/include/mach/debug-macro.S
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 */

#define SIRF_LLUART_TXFIFO_STATUS	0x0114
#define SIRF_LLUART_TXFIFO_DATA	0x0118

#define SIRF_LLUART_TXFIFO_FULL                       (1 << 5)

#ifdef CONFIG_DEBUG_SIRFATLAS7_UART0
#define SIRF_LLUART_TXFIFO_EMPTY			(1 << 8)
#else
#define SIRF_LLUART_TXFIFO_EMPTY			(1 << 6)
#endif


	.macro	addruart, rp, rv, tmp
	ldr	\rp, =CONFIG_DEBUG_UART_PHYS		@ physical
	ldr	\rv, =CONFIG_DEBUG_UART_VIRT		@ virtual
	.endm

	.macro	senduart,rd,rx
	str	\rd, [\rx, #SIRF_LLUART_TXFIFO_DATA]
	.endm

	.macro	busyuart,rd,rx
	.endm

	.macro	waituart,rd,rx
1001:	ldr	\rd, [\rx, #SIRF_LLUART_TXFIFO_STATUS]
	tst	\rd, #SIRF_LLUART_TXFIFO_EMPTY
	beq	1001b
	.endm

