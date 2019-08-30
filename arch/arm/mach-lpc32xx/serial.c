// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/mach-lpc32xx/serial.c
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include "common.h"

#define LPC32XX_SUART_FIFO_SIZE	64

struct uartinit {
	char *uart_ck_name;
	u32 ck_mode_mask;
	void __iomem *pdiv_clk_reg;
	resource_size_t mapbase;
};

static struct uartinit uartinit_data[] __initdata = {
	{
		.uart_ck_name = "uart5_ck",
		.ck_mode_mask =
			LPC32XX_UART_CLKMODE_LOAD(LPC32XX_UART_CLKMODE_ON, 5),
		.pdiv_clk_reg = LPC32XX_CLKPWR_UART5_CLK_CTRL,
		.mapbase = LPC32XX_UART5_BASE,
	},
	{
		.uart_ck_name = "uart3_ck",
		.ck_mode_mask =
			LPC32XX_UART_CLKMODE_LOAD(LPC32XX_UART_CLKMODE_ON, 3),
		.pdiv_clk_reg = LPC32XX_CLKPWR_UART3_CLK_CTRL,
		.mapbase = LPC32XX_UART3_BASE,
	},
	{
		.uart_ck_name = "uart4_ck",
		.ck_mode_mask =
			LPC32XX_UART_CLKMODE_LOAD(LPC32XX_UART_CLKMODE_ON, 4),
		.pdiv_clk_reg = LPC32XX_CLKPWR_UART4_CLK_CTRL,
		.mapbase = LPC32XX_UART4_BASE,
	},
	{
		.uart_ck_name = "uart6_ck",
		.ck_mode_mask =
			LPC32XX_UART_CLKMODE_LOAD(LPC32XX_UART_CLKMODE_ON, 6),
		.pdiv_clk_reg = LPC32XX_CLKPWR_UART6_CLK_CTRL,
		.mapbase = LPC32XX_UART6_BASE,
	},
};

void __init lpc32xx_serial_init(void)
{
	u32 tmp, clkmodes = 0;
	struct clk *clk;
	unsigned int puart;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(uartinit_data); i++) {
		clk = clk_get(NULL, uartinit_data[i].uart_ck_name);
		if (!IS_ERR(clk)) {
			clk_enable(clk);
		}

		/* Setup UART clock modes for all UARTs, disable autoclock */
		clkmodes |= uartinit_data[i].ck_mode_mask;

		/* pre-UART clock divider set to 1 */
		__raw_writel(0x0101, uartinit_data[i].pdiv_clk_reg);

		/*
		 * Force a flush of the RX FIFOs to work around a
		 * HW bug
		 */
		puart = uartinit_data[i].mapbase;
		__raw_writel(0xC1, LPC32XX_UART_IIR_FCR(puart));
		__raw_writel(0x00, LPC32XX_UART_DLL_FIFO(puart));
		j = LPC32XX_SUART_FIFO_SIZE;
		while (j--)
			tmp = __raw_readl(
				LPC32XX_UART_DLL_FIFO(puart));
		__raw_writel(0, LPC32XX_UART_IIR_FCR(puart));
	}

	/* This needs to be done after all UART clocks are setup */
	__raw_writel(clkmodes, LPC32XX_UARTCTL_CLKMODE);
	for (i = 0; i < ARRAY_SIZE(uartinit_data); i++) {
		/* Force a flush of the RX FIFOs to work around a HW bug */
		puart = uartinit_data[i].mapbase;
		__raw_writel(0xC1, LPC32XX_UART_IIR_FCR(puart));
		__raw_writel(0x00, LPC32XX_UART_DLL_FIFO(puart));
		j = LPC32XX_SUART_FIFO_SIZE;
		while (j--)
			tmp = __raw_readl(LPC32XX_UART_DLL_FIFO(puart));
		__raw_writel(0, LPC32XX_UART_IIR_FCR(puart));
	}

	/* Disable IrDA pulsing support on UART6 */
	tmp = __raw_readl(LPC32XX_UARTCTL_CTRL);
	tmp |= LPC32XX_UART_UART6_IRDAMOD_BYPASS;
	__raw_writel(tmp, LPC32XX_UARTCTL_CTRL);

	/* Disable UART5->USB transparent mode or USB won't work */
	tmp = __raw_readl(LPC32XX_UARTCTL_CTRL);
	tmp &= ~LPC32XX_UART_U5_ROUTE_TO_USB;
	__raw_writel(tmp, LPC32XX_UARTCTL_CTRL);
}
