/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/debug/sti.S
 *
 * Debugging macro include header
 * Copyright (C) 2013 STMicroelectronics (R&D) Limited.
 */

#define ASC_TX_BUF_OFF  0x04
#define ASC_CTRL_OFF    0x0c
#define ASC_STA_OFF     0x14

#define ASC_STA_TX_FULL         (1<<9)
#define ASC_STA_TX_EMPTY        (1<<1)


		.macro	addruart, rp, rv, tmp
		ldr	\rp,      =CONFIG_DEBUG_UART_PHYS	@ physical base
		ldr	\rv,      =CONFIG_DEBUG_UART_VIRT	@ virt base
		.endm

                .macro  senduart,rd,rx
                strb    \rd, [\rx, #ASC_TX_BUF_OFF]
                .endm

		.macro	waituartcts,rd,rx
		.endm

                .macro  waituarttxrdy,rd,rx
1001:           ldr     \rd, [\rx, #ASC_STA_OFF]
                tst     \rd, #ASC_STA_TX_FULL
                bne     1001b
                .endm

                .macro  busyuart,rd,rx
1001:           ldr     \rd, [\rx, #ASC_STA_OFF]
                tst     \rd, #ASC_STA_TX_EMPTY
                beq     1001b
                .endm
