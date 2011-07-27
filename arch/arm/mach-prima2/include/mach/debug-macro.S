/*
 * arch/arm/mach-prima2/include/mach/debug-macro.S
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <mach/hardware.h>
#include <mach/uart.h>

	.macro	addruart, rp, rv
	ldr	\rp, =SIRFSOC_UART1_PA_BASE		@ physical
	ldr	\rv, =SIRFSOC_UART1_VA_BASE		@ virtual
	.endm

	.macro	senduart,rd,rx
	str	\rd, [\rx, #SIRFSOC_UART_TXFIFO_DATA]
	.endm

	.macro	busyuart,rd,rx
	.endm

	.macro	waituart,rd,rx
1001:	ldr	\rd, [\rx, #SIRFSOC_UART_TXFIFO_STATUS]
	tst	\rd, #SIRFSOC_UART1_TXFIFO_EMPTY
	beq	1001b
	.endm

