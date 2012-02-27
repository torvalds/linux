/*
 * arch/arm/mach-lpc32xx/serial.c
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

/* Standard 8250/16550 compatible serial ports */
static struct plat_serial8250_port serial_std_platform_data[] = {
#ifdef CONFIG_ARCH_LPC32XX_UART5_SELECT
	{
		.membase        = io_p2v(LPC32XX_UART5_BASE),
		.mapbase        = LPC32XX_UART5_BASE,
		.irq		= IRQ_LPC32XX_UART_IIR5,
		.uartclk	= LPC32XX_MAIN_OSC_FREQ,
		.regshift	= 2,
		.iotype		= UPIO_MEM32,
		.flags		= UPF_BOOT_AUTOCONF | UPF_BUGGY_UART |
					UPF_SKIP_TEST,
	},
#endif
#ifdef CONFIG_ARCH_LPC32XX_UART3_SELECT
	{
		.membase	= io_p2v(LPC32XX_UART3_BASE),
		.mapbase        = LPC32XX_UART3_BASE,
		.irq		= IRQ_LPC32XX_UART_IIR3,
		.uartclk	= LPC32XX_MAIN_OSC_FREQ,
		.regshift	= 2,
		.iotype		= UPIO_MEM32,
		.flags		= UPF_BOOT_AUTOCONF | UPF_BUGGY_UART |
					UPF_SKIP_TEST,
	},
#endif
#ifdef CONFIG_ARCH_LPC32XX_UART4_SELECT
	{
		.membase	= io_p2v(LPC32XX_UART4_BASE),
		.mapbase        = LPC32XX_UART4_BASE,
		.irq		= IRQ_LPC32XX_UART_IIR4,
		.uartclk	= LPC32XX_MAIN_OSC_FREQ,
		.regshift	= 2,
		.iotype		= UPIO_MEM32,
		.flags		= UPF_BOOT_AUTOCONF | UPF_BUGGY_UART |
					UPF_SKIP_TEST,
	},
#endif
#ifdef CONFIG_ARCH_LPC32XX_UART6_SELECT
	{
		.membase	= io_p2v(LPC32XX_UART6_BASE),
		.mapbase        = LPC32XX_UART6_BASE,
		.irq		= IRQ_LPC32XX_UART_IIR6,
		.uartclk	= LPC32XX_MAIN_OSC_FREQ,
		.regshift	= 2,
		.iotype		= UPIO_MEM32,
		.flags		= UPF_BOOT_AUTOCONF | UPF_BUGGY_UART |
					UPF_SKIP_TEST,
	},
#endif
	{ },
};

struct uartinit {
	char *uart_ck_name;
	u32 ck_mode_mask;
	void __iomem *pdiv_clk_reg;
	resource_size_t mapbase;
};

static struct uartinit uartinit_data[] __initdata = {
#ifdef CONFIG_ARCH_LPC32XX_UART5_SELECT
	{
		.uart_ck_name = "uart5_ck",
		.ck_mode_mask =
			LPC32XX_UART_CLKMODE_LOAD(LPC32XX_UART_CLKMODE_ON, 5),
		.pdiv_clk_reg = LPC32XX_CLKPWR_UART5_CLK_CTRL,
		.mapbase = LPC32XX_UART5_BASE,
	},
#endif
#ifdef CONFIG_ARCH_LPC32XX_UART3_SELECT
	{
		.uart_ck_name = "uart3_ck",
		.ck_mode_mask =
			LPC32XX_UART_CLKMODE_LOAD(LPC32XX_UART_CLKMODE_ON, 3),
		.pdiv_clk_reg = LPC32XX_CLKPWR_UART3_CLK_CTRL,
		.mapbase = LPC32XX_UART3_BASE,
	},
#endif
#ifdef CONFIG_ARCH_LPC32XX_UART4_SELECT
	{
		.uart_ck_name = "uart4_ck",
		.ck_mode_mask =
			LPC32XX_UART_CLKMODE_LOAD(LPC32XX_UART_CLKMODE_ON, 4),
		.pdiv_clk_reg = LPC32XX_CLKPWR_UART4_CLK_CTRL,
		.mapbase = LPC32XX_UART4_BASE,
	},
#endif
#ifdef CONFIG_ARCH_LPC32XX_UART6_SELECT
	{
		.uart_ck_name = "uart6_ck",
		.ck_mode_mask =
			LPC32XX_UART_CLKMODE_LOAD(LPC32XX_UART_CLKMODE_ON, 6),
		.pdiv_clk_reg = LPC32XX_CLKPWR_UART6_CLK_CTRL,
		.mapbase = LPC32XX_UART6_BASE,
	},
#endif
};

static struct platform_device serial_std_platform_device = {
	.name			= "serial8250",
	.id			= 0,
	.dev			= {
		.platform_data	= serial_std_platform_data,
	},
};

static struct platform_device *lpc32xx_serial_devs[] __initdata = {
	&serial_std_platform_device,
};

void __init lpc32xx_serial_init(void)
{
	u32 tmp, clkmodes = 0;
	struct clk *clk;
	unsigned int puart;
	int i, j;

	/* UART clocks are off, let clock driver manage them */
	__raw_writel(0, LPC32XX_CLKPWR_UART_CLK_CTRL);

	for (i = 0; i < ARRAY_SIZE(uartinit_data); i++) {
		clk = clk_get(NULL, uartinit_data[i].uart_ck_name);
		if (!IS_ERR(clk)) {
			clk_enable(clk);
			serial_std_platform_data[i].uartclk =
				clk_get_rate(clk);
		}

		/* Fall back on main osc rate if clock rate return fails */
		if (serial_std_platform_data[i].uartclk == 0)
			serial_std_platform_data[i].uartclk =
				LPC32XX_MAIN_OSC_FREQ;

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
	for (i = 0; i < ARRAY_SIZE(uartinit_data) - 1; i++) {
		/* Force a flush of the RX FIFOs to work around a HW bug */
		puart = serial_std_platform_data[i].mapbase;
		__raw_writel(0xC1, LPC32XX_UART_IIR_FCR(puart));
		__raw_writel(0x00, LPC32XX_UART_DLL_FIFO(puart));
		j = LPC32XX_SUART_FIFO_SIZE;
		while (j--)
			tmp = __raw_readl(LPC32XX_UART_DLL_FIFO(puart));
		__raw_writel(0, LPC32XX_UART_IIR_FCR(puart));
	}

	/* Disable UART5->USB transparent mode or USB won't work */
	tmp = __raw_readl(LPC32XX_UARTCTL_CTRL);
	tmp &= ~LPC32XX_UART_U5_ROUTE_TO_USB;
	__raw_writel(tmp, LPC32XX_UARTCTL_CTRL);

	platform_add_devices(lpc32xx_serial_devs,
		ARRAY_SIZE(lpc32xx_serial_devs));
}
