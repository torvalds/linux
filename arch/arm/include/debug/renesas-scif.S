/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Renesas SCIF(A) debugging macro include header
 *
 * Based on r8a7790.S
 *
 * Copyright (C) 2012-2013 Renesas Electronics Corporation
 * Copyright (C) 1994-1999 Russell King
 */

#define SCIF_PHYS	CONFIG_DEBUG_UART_PHYS
#define SCIF_VIRT	((SCIF_PHYS & 0x00ffffff) | 0xfd000000)

#if defined(CONFIG_DEBUG_R7S9210_SCIF2) || defined(CONFIG_DEBUG_R7S9210_SCIF4)
/* RZ/A2 SCIFA */
#define FTDR		0x06
#define FSR		0x08
#elif CONFIG_DEBUG_UART_PHYS < 0xe6e00000
/* SCIFA */
#define FTDR		0x20
#define FSR		0x14
#else
/* SCIF */
#define FTDR		0x0c
#define FSR		0x10
#endif

#define TDFE	(1 << 5)
#define TEND	(1 << 6)

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =SCIF_PHYS
	ldr	\rv, =SCIF_VIRT
	.endm

	.macro	waituart, rd, rx
1001:	ldrh	\rd, [\rx, #FSR]
	tst	\rd, #TDFE
	beq	1001b
	.endm

	.macro	senduart, rd, rx
	strb	\rd, [\rx, #FTDR]
	ldrh	\rd, [\rx, #FSR]
	bic	\rd, \rd, #TEND
	strh	\rd, [\rx, #FSR]
	.endm

	.macro	busyuart, rd, rx
1001:	ldrh	\rd, [\rx, #FSR]
	tst	\rd, #TEND
	beq	1001b
	.endm
