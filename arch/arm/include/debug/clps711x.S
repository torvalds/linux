/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

	.macro	waituart,rd,rx
	.endm

	.macro	senduart,rd,rx
	str	\rd, [\rx, #UARTDR]
	.endm

	.macro	busyuart,rd,rx
1001:	ldr	\rd, [\rx, #SYSFLG]
	tst	\rd, #SYSFLG_UBUSY
	bne	1001b
	.endm
