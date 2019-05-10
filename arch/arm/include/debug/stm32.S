/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics SA 2017 - All Rights Reserved
 * Author:   Gerald Baeza <gerald.baeza@st.com> for STMicroelectronics.
 */

#define STM32_UART_BASE			0x40011000	/* USART1 */

#ifdef CONFIG_STM32F4_DEBUG_UART
#define STM32_USART_SR_OFF		0x00
#define STM32_USART_TDR_OFF		0x04
#endif

#ifdef CONFIG_STM32F7_DEBUG_UART
#define STM32_USART_SR_OFF		0x1C
#define STM32_USART_TDR_OFF		0x28
#endif

#define STM32_USART_TC			(1 << 6)	/* Tx complete       */
#define STM32_USART_TXE			(1 << 7)	/* Tx data reg empty */

.macro	addruart, rp, rv, tmp
	ldr	\rp,      =STM32_UART_BASE	@ physical base
	ldr	\rv,      =STM32_UART_BASE      @ virt base /* NoMMU */
.endm

.macro  senduart,rd,rx
	strb    \rd, [\rx, #STM32_USART_TDR_OFF]
.endm

.macro  waituart,rd,rx
1001:	ldr	\rd, [\rx, #(STM32_USART_SR_OFF)]	@ Read Status Register
	tst	\rd, #STM32_USART_TXE			@ TXE = 1 = tx empty
	beq	1001b
.endm

.macro  busyuart,rd,rx
1001:	ldr	\rd, [\rx, #(STM32_USART_SR_OFF)]	@ Read Status Register
	tst	\rd, #STM32_USART_TC			@ TC = 1 = tx complete
	beq	1001b
.endm
