/*
 * arch/arm/plat-spear/include/plat/debug-macro.S
 *
 * Debugging macro include header for spear platform
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/amba/serial.h>
#include <mach/hardware.h>

		.macro	addruart, rp, rv
		mov	\rp, #SPEAR_DBG_UART_BASE		@ Physical base
		mov	\rv, #VA_SPEAR_DBG_UART_BASE		@ Virtual base
		.endm

		.macro	senduart, rd, rx
		strb	\rd, [\rx, #UART01x_DR]			@ ASC_TX_BUFFER
		.endm

		.macro	waituart, rd, rx
1001:		ldr	\rd, [\rx, #UART01x_FR]			@ FLAG REGISTER
		tst	\rd, #UART01x_FR_TXFF			@ TX_FULL
		bne	1001b
		.endm

		.macro	busyuart, rd, rx
1002:		ldr	\rd, [\rx, #UART01x_FR]			@ FLAG REGISTER
		tst	\rd, #UART011_FR_TXFE			@ TX_EMPTY
		beq	1002b
		.endm
