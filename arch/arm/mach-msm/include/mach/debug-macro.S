/*
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 * Author: Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <mach/hardware.h>
#include <mach/msm_iomap.h>

	.macro	addruart, rp, rv, tmp
#ifdef MSM_DEBUG_UART_PHYS
	ldr	\rp, =MSM_DEBUG_UART_PHYS
	ldr	\rv, =MSM_DEBUG_UART_BASE
#endif
	.endm

	.macro	senduart, rd, rx
#ifdef CONFIG_MSM_HAS_DEBUG_UART_HS
	@ Write the 1 character to UARTDM_TF
	str	\rd, [\rx, #0x70]
#else
	teq	\rx, #0
	strne	\rd, [\rx, #0x0C]
#endif
	.endm

	.macro	waituart, rd, rx
#ifdef CONFIG_MSM_HAS_DEBUG_UART_HS
	@ check for TX_EMT in UARTDM_SR
	ldr	\rd, [\rx, #0x08]
	tst	\rd, #0x08
	bne	1002f
	@ wait for TXREADY in UARTDM_ISR
1001:	ldr	\rd, [\rx, #0x14]
	tst	\rd, #0x80
	beq 	1001b
1002:
	@ Clear TX_READY by writing to the UARTDM_CR register
	mov	\rd, #0x300
	str	\rd, [\rx, #0x10]
	@ Write 0x1 to NCF register
	mov 	\rd, #0x1
	str	\rd, [\rx, #0x40]
	@ UARTDM reg. Read to induce delay
	ldr	\rd, [\rx, #0x08]
#else
	@ wait for TX_READY
1001:	ldr	\rd, [\rx, #0x08]
	tst	\rd, #0x04
	beq	1001b
#endif
	.endm

	.macro	busyuart, rd, rx
	.endm
