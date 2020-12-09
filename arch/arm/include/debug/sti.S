/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/debug/sti.S
 *
 * Debugging macro include header
 * Copyright (C) 2013 STMicroelectronics (R&D) Limited.
 */

#define STIH41X_COMMS_BASE              0xfed00000
#define STIH41X_ASC2_BASE               (STIH41X_COMMS_BASE+0x32000)

#define STIH41X_SBC_LPM_BASE            0xfe400000
#define STIH41X_SBC_COMMS_BASE          (STIH41X_SBC_LPM_BASE + 0x100000)
#define STIH41X_SBC_ASC1_BASE           (STIH41X_SBC_COMMS_BASE + 0x31000)


#define VIRT_ADDRESS(x)		(x - 0x1000000)

#if IS_ENABLED(CONFIG_STIH41X_DEBUG_ASC2)
#define DEBUG_LL_UART_BASE	STIH41X_ASC2_BASE
#endif

#if IS_ENABLED(CONFIG_STIH41X_DEBUG_SBC_ASC1)
#define DEBUG_LL_UART_BASE	STIH41X_SBC_ASC1_BASE
#endif

#ifndef DEBUG_LL_UART_BASE
#error "DEBUG UART is not Configured"
#endif

#define ASC_TX_BUF_OFF  0x04
#define ASC_CTRL_OFF    0x0c
#define ASC_STA_OFF     0x14

#define ASC_STA_TX_FULL         (1<<9)
#define ASC_STA_TX_EMPTY        (1<<1)


		.macro	addruart, rp, rv, tmp
		ldr	\rp,      =DEBUG_LL_UART_BASE	@ physical base
		ldr	\rv,      =VIRT_ADDRESS(DEBUG_LL_UART_BASE) @ virt base
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
