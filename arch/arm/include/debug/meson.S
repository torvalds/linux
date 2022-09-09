/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Carlo Caione
 * Carlo Caione <carlo@caione.org>
 */

#define MESON_AO_UART_WFIFO		0x0
#define MESON_AO_UART_STATUS		0xc

#define MESON_AO_UART_TX_FIFO_EMPTY	(1 << 22)
#define MESON_AO_UART_TX_FIFO_FULL	(1 << 21)

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =(CONFIG_DEBUG_UART_PHYS)		@ physical
	ldr	\rv, =(CONFIG_DEBUG_UART_VIRT)		@ virtual
	.endm

	.macro	senduart,rd,rx
	str	\rd, [\rx, #MESON_AO_UART_WFIFO]
	.endm

	.macro	busyuart,rd,rx
1002:	ldr	\rd, [\rx, #MESON_AO_UART_STATUS]
	tst	\rd, #MESON_AO_UART_TX_FIFO_EMPTY
	beq	1002b
	.endm

	.macro	waituartcts,rd,rx
	.endm

	.macro	waituarttxrdy,rd,rx
1001:	ldr	\rd, [\rx, #MESON_AO_UART_STATUS]
	tst	\rd, #MESON_AO_UART_TX_FIFO_FULL
	bne	1001b
	.endm
