/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 */

#define UARTn_CMD		0x000c
#define UARTn_CMD_TXEN			0x0004

#define	UARTn_STATUS		0x0010
#define	UARTn_STATUS_TXC		0x0020
#define	UARTn_STATUS_TXBL		0x0040

#define	UARTn_TXDATA		0x0034

		.macro	addruart, rx, tmp, tmp2
		ldr	\rx, =(CONFIG_DEBUG_UART_PHYS)

		/*
		 * enable TX. The driver might disable it to save energy. We
		 * don't care about disabling at the end as during debug power
		 * consumption isn't that important.
		 */
		ldr	\tmp, =(UARTn_CMD_TXEN)
		str	\tmp, [\rx, #UARTn_CMD]
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx, #UARTn_TXDATA]
		.endm

		.macro	waituartcts,rd,rx
		.endm

		.macro	waituarttxrdy,rd,rx
1001:		ldr	\rd, [\rx, #UARTn_STATUS]
		tst	\rd, #UARTn_STATUS_TXBL
		beq	1001b
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, UARTn_STATUS]
		tst	\rd, #UARTn_STATUS_TXC
		bne	1001b
		.endm
