/*
 * arch/arm/mach-prima2/include/mach/debug-macro.S
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#if defined(CONFIG_DEBUG_SIRFPRIMA2_UART1)
#define SIRFSOC_UART1_PA_BASE          0xb0060000
#elif defined(CONFIG_DEBUG_SIRFMARCO_UART1)
#define SIRFSOC_UART1_PA_BASE          0xcc060000
#else
#define SIRFSOC_UART1_PA_BASE          0
#endif

#define SIRFSOC_UART1_VA_BASE		0xFEC60000

#define SIRFSOC_UART_TXFIFO_STATUS	0x0114
#define SIRFSOC_UART_TXFIFO_DATA	0x0118

#define SIRFSOC_UART1_TXFIFO_FULL                       (1 << 5)
#define SIRFSOC_UART1_TXFIFO_EMPTY			(1 << 6)

	.macro	addruart, rp, rv, tmp
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

