/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 * Author: Brian Swetland <swetland@google.com>
 */

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =CONFIG_DEBUG_UART_PHYS
	ldr	\rv, =CONFIG_DEBUG_UART_VIRT
	.endm

	.macro	senduart, rd, rx
ARM_BE8(rev	\rd, \rd )
	@ Write the 1 character to UARTDM_TF
	str	\rd, [\rx, #0x70]
	.endm

	.macro	waituart, rd, rx
	@ check for TX_EMT in UARTDM_SR
	ldr	\rd, [\rx, #0x08]
ARM_BE8(rev     \rd, \rd )
	tst	\rd, #0x08
	bne	1002f
	@ wait for TXREADY in UARTDM_ISR
1001:	ldr	\rd, [\rx, #0x14]
ARM_BE8(rev     \rd, \rd )
	tst	\rd, #0x80
	beq 	1001b
1002:
	@ Clear TX_READY by writing to the UARTDM_CR register
	mov	\rd, #0x300
ARM_BE8(rev     \rd, \rd )
	str	\rd, [\rx, #0x10]
	@ Write 0x1 to NCF register
	mov 	\rd, #0x1
ARM_BE8(rev     \rd, \rd )
	str	\rd, [\rx, #0x40]
	@ UARTDM reg. Read to induce delay
	ldr	\rd, [\rx, #0x08]
	.endm

	.macro	busyuart, rd, rx
	.endm
