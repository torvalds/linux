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

#if defined(CONFIG_ARCH_MSM7X00A) || defined(CONFIG_ARCH_QSD8X50)
#define MSM_UART1_PHYS        0xA9A00000
#define MSM_UART2_PHYS        0xA9B00000
#define MSM_UART3_PHYS        0xA9C00000
#elif defined(CONFIG_ARCH_MSM7X30)
#define MSM_UART1_PHYS        0xACA00000
#define MSM_UART2_PHYS        0xACB00000
#define MSM_UART3_PHYS        0xACC00000
#endif

#if defined(CONFIG_DEBUG_MSM_UART1)
#define MSM_DEBUG_UART_BASE	0xE1000000
#define MSM_DEBUG_UART_PHYS	MSM_UART1_PHYS
#elif defined(CONFIG_DEBUG_MSM_UART2)
#define MSM_DEBUG_UART_BASE	0xE1000000
#define MSM_DEBUG_UART_PHYS	MSM_UART2_PHYS
#elif defined(CONFIG_DEBUG_MSM_UART3)
#define MSM_DEBUG_UART_BASE	0xE1000000
#define MSM_DEBUG_UART_PHYS	MSM_UART3_PHYS
#endif

#ifdef CONFIG_DEBUG_MSM8660_UART
#define MSM_DEBUG_UART_BASE	0xF0040000
#define MSM_DEBUG_UART_PHYS	0x19C40000
#endif

#ifdef CONFIG_DEBUG_MSM8960_UART
#define MSM_DEBUG_UART_BASE	0xF0040000
#define MSM_DEBUG_UART_PHYS	0x16440000
#endif

#ifdef CONFIG_DEBUG_MSM8974_UART
#define MSM_DEBUG_UART_BASE	0xFA71E000
#define MSM_DEBUG_UART_PHYS	0xF991E000
#endif

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
	str	\rd, [\rx, #0x0C]
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
