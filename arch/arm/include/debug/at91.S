/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2003-2005 SAN People
 *
 * Debugging macro include header
*/

#define AT91_DBGU_SR		(0x14)	/* Status Register */
#define AT91_DBGU_THR		(0x1c)	/* Transmitter Holding Register */
#define AT91_DBGU_TXRDY		(1 << 1)	/* Transmitter Ready */
#define AT91_DBGU_TXEMPTY	(1 << 9)	/* Transmitter Empty */

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =CONFIG_DEBUG_UART_PHYS		@ System peripherals (phys address)
	ldr	\rv, =CONFIG_DEBUG_UART_VIRT		@ System peripherals (virt address)
	.endm

	.macro	senduart,rd,rx
	strb	\rd, [\rx, #(AT91_DBGU_THR)]		@ Write to Transmitter Holding Register
	.endm

	.macro	waituarttxrdy,rd,rx
1001:	ldr	\rd, [\rx, #(AT91_DBGU_SR)]		@ Read Status Register
	tst	\rd, #AT91_DBGU_TXRDY			@ DBGU_TXRDY = 1 when ready to transmit
	beq	1001b
	.endm

	.macro	waituartcts,rd,rx
	.endm

	.macro	busyuart,rd,rx
1001:	ldr	\rd, [\rx, #(AT91_DBGU_SR)]		@ Read Status Register
	tst	\rd, #AT91_DBGU_TXEMPTY			@ DBGU_TXEMPTY = 1 when transmission complete
	beq	1001b
	.endm

