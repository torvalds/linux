/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef RKPM_URAT_H
#define RKPM_URAT_H

#include "rkpm_helpers.h"

/* UART16550 Registers */
#define UARTTX				0x0
#define UARTRX				0x0
#define UARTDLL				0x0
#define UARTIER				0x4
#define UARTDLLM			0x4
#define UARTIIR				0x8
#define UARTFCR				0x8
#define UARTLCR				0xc
#define UARTMCR				0x10
#define UARTLSR				0x14
#define UARTMSR				0x18
#define UARTSPR				0x1c
#define UARTCSR				0x20

#define UARTUSR				0x7c
#define UARTSRR				0x88
#define DIAGNOSTIC_MODE			BIT(4)
#define UART_RESET			BIT(0)
#define RCVR_FIFO_RESET			BIT(1)
#define XMIT_FIFO_RESET			BIT(2)

/* UART_USR bits */
#define UARTUSR_BUSY			BIT(0)
#define UARTUSR_TFIFO_N_FULL		BIT(1)
#define UARTUSR_TFIFO_EMPTY		BIT(2)
#define UARTUSR_RRIFO_N_EMPTY		BIT(3)
#define UARTUSR_RFIFO_FULL		BIT(4)

#define UARTFCR_FIFOEN			(1 << 0)	/* Enable the Tx/Rx FIFO */

#define UARTLCR_DLAB			(1 << 7)	/* Divisor Latch Access */

struct uart_debug_ctx {
	u32 uart_dll;
	u32 uart_dlh;
	u32 uart_ier;
	u32 uart_fcr;
	u32 uart_mcr;
	u32 uart_lcr;
};

void rkpm_uart_debug_init(void __iomem *base,
			  unsigned int uart_clk,
			  unsigned int baud_rate);
void rkpm_uart_debug_save(void __iomem *base,
			  struct uart_debug_ctx *ctx);
void rkpm_uart_debug_restore(void __iomem *base,
			     struct uart_debug_ctx *ctx);
#endif
