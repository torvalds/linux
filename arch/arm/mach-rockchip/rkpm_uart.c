// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#include <linux/io.h>

#include "rkpm_helpers.h"
#include "rkpm_uart.h"

#define UART_DEFAULT_BAUDRATE		115200

void rkpm_uart_debug_init(void __iomem *base,
			  unsigned int uart_clk,
			  unsigned int baud_rate)
{
	u32 uart_dll, uart_dlh;
	u32 div;

	if (!base || !uart_clk || !baud_rate)
		return;

	div = uart_clk / baud_rate / 16;
	uart_dll = div & 0xff;
	uart_dlh = (div >> 8) & 0xff;

	/* Reset uart */
	writel_relaxed(XMIT_FIFO_RESET | RCVR_FIFO_RESET | UART_RESET,
		       base + UARTSRR);
	rkpm_raw_udelay(10);

	writel_relaxed(DIAGNOSTIC_MODE, base + UARTMCR);
	writel_relaxed(0x83, base + UARTLCR);
	writel_relaxed(uart_dll, base + UARTDLL);
	writel_relaxed(uart_dlh, base + UARTDLLM);
	writel_relaxed(0x03, base + UARTLCR);
	writel_relaxed(0x01, base + UARTIER);
	writel_relaxed(UARTFCR_FIFOEN, base + UARTFCR);
	writel_relaxed(0, base + UARTMCR);
}

void rkpm_uart_debug_save(void __iomem *base,
			  struct uart_debug_ctx *ctx)
{
	u32 wait_cnt = 50000;

	while ((readl_relaxed(base + UARTUSR) & UARTUSR_BUSY) &&
	       --wait_cnt)
		rkpm_raw_udelay(10);

	/* Uart error! Unlikely to reach here */
	if (wait_cnt == 0)
		rkpm_uart_debug_init(base, 24000000, UART_DEFAULT_BAUDRATE);

	ctx->uart_lcr = readl_relaxed(base + UARTLCR);
	ctx->uart_ier = readl_relaxed(base + UARTIER);
	ctx->uart_mcr = readl_relaxed(base + UARTMCR);
	writel_relaxed(ctx->uart_lcr | UARTLCR_DLAB, base + UARTLCR);
	ctx->uart_dll = readl_relaxed(base + UARTDLL);
	ctx->uart_dlh = readl_relaxed(base + UARTDLLM);
	writel_relaxed(ctx->uart_lcr, base + UARTLCR);
}

void rkpm_uart_debug_restore(void __iomem *base,
			     struct uart_debug_ctx *ctx)
{
	u32 uart_lcr;
	u32 wait_cnt = 50000;

	while ((readl_relaxed(base + UARTUSR) & UARTUSR_BUSY) &&
	       --wait_cnt)
		rkpm_raw_udelay(10);

	writel_relaxed(XMIT_FIFO_RESET | RCVR_FIFO_RESET | UART_RESET,
		       base + UARTSRR);
	rkpm_raw_udelay(10);
	uart_lcr = readl_relaxed(base + UARTLCR);
	writel_relaxed(DIAGNOSTIC_MODE, base + UARTMCR);
	writel_relaxed(uart_lcr | UARTLCR_DLAB, base + UARTLCR);
	writel_relaxed(ctx->uart_dll, base + UARTDLL);
	writel_relaxed(ctx->uart_dlh, base + UARTDLLM);
	writel_relaxed(ctx->uart_lcr, base + UARTLCR);
	writel_relaxed(ctx->uart_ier, base + UARTIER);
	writel_relaxed(UARTFCR_FIFOEN, base + UARTFCR);
	writel_relaxed(ctx->uart_mcr, base + UARTMCR);
}

