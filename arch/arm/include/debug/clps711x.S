/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 */

#ifndef CONFIG_DEBUG_CLPS711X_UART2
#define CLPS711X_UART_PADDR	(0x80000000 + 0x0000)
#define CLPS711X_UART_VADDR	(0xfeff4000 + 0x0000)
#else
#define CLPS711X_UART_PADDR	(0x80000000 + 0x1000)
#define CLPS711X_UART_VADDR	(0xfeff4000 + 0x1000)
#endif

#define SYSFLG		(0x0140)
#define SYSFLG_UBUSY	(1 << 11)
#define UARTDR		(0x0480)

	.macro	addruart, rp, rv, tmp
	ldr	\rv, =CLPS711X_UART_VADDR
	ldr	\rp, =CLPS711X_UART_PADDR
	.endm

	.macro	waituartcts,rd,rx
	.endm

	.macro	waituarttxrdy,rd,rx
	.endm

	.macro	senduart,rd,rx
	str	\rd, [\rx, #UARTDR]
	.endm

	.macro	busyuart,rd,rx
1001:	ldr	\rd, [\rx, #SYSFLG]
	tst	\rd, #SYSFLG_UBUSY
	bne	1001b
	.endm
