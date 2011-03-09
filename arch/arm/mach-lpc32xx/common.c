/*
 * arch/arm/mach-lpc32xx/common.c
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

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/i2c-pnx.h>
#include <linux/io.h>

#include <asm/mach/map.h>

#include <mach/i2c.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include "common.h"

/*
 * Watchdog timer
 */
static struct resource watchdog_resources[] = {
	[0] = {
		.start = LPC32XX_WDTIM_BASE,
		.end = LPC32XX_WDTIM_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device lpc32xx_watchdog_device = {
	.name = "pnx4008-watchdog",
	.id = -1,
	.num_resources = ARRAY_SIZE(watchdog_resources),
	.resource = watchdog_resources,
};

/*
 * I2C busses
 */
static struct i2c_pnx_data i2c0_data = {
	.name = I2C_CHIP_NAME "1",
	.base = LPC32XX_I2C1_BASE,
	.irq = IRQ_LPC32XX_I2C_1,
};

static struct i2c_pnx_data i2c1_data = {
	.name = I2C_CHIP_NAME "2",
	.base = LPC32XX_I2C2_BASE,
	.irq = IRQ_LPC32XX_I2C_2,
};

static struct i2c_pnx_data i2c2_data = {
	.name = "USB-I2C",
	.base = LPC32XX_OTG_I2C_BASE,
	.irq = IRQ_LPC32XX_USB_I2C,
};

struct platform_device lpc32xx_i2c0_device = {
	.name = "pnx-i2c",
	.id = 0,
	.dev = {
		.platform_data = &i2c0_data,
	},
};

struct platform_device lpc32xx_i2c1_device = {
	.name = "pnx-i2c",
	.id = 1,
	.dev = {
		.platform_data = &i2c1_data,
	},
};

struct platform_device lpc32xx_i2c2_device = {
	.name = "pnx-i2c",
	.id = 2,
	.dev = {
		.platform_data = &i2c2_data,
	},
};

/*
 * Returns the unique ID for the device
 */
void lpc32xx_get_uid(u32 devid[4])
{
	int i;

	for (i = 0; i < 4; i++)
		devid[i] = __raw_readl(LPC32XX_CLKPWR_DEVID(i << 2));
}

/*
 * Returns SYSCLK source
 * 0 = PLL397, 1 = main oscillator
 */
int clk_is_sysclk_mainosc(void)
{
	if ((__raw_readl(LPC32XX_CLKPWR_SYSCLK_CTRL) &
		LPC32XX_CLKPWR_SYSCTRL_SYSCLKMUX) == 0)
		return 1;

	return 0;
}

/*
 * System reset via the watchdog timer
 */
void lpc32xx_watchdog_reset(void)
{
	/* Make sure WDT clocks are enabled */
	__raw_writel(LPC32XX_CLKPWR_PWMCLK_WDOG_EN,
		LPC32XX_CLKPWR_TIMER_CLK_CTRL);

	/* Instant assert of RESETOUT_N with pulse length 1mS */
	__raw_writel(13000, io_p2v(LPC32XX_WDTIM_BASE + 0x18));
	__raw_writel(0x70, io_p2v(LPC32XX_WDTIM_BASE + 0xC));
}

/*
 * Detects and returns IRAM size for the device variation
 */
#define LPC32XX_IRAM_BANK_SIZE SZ_128K
static u32 iram_size;
u32 lpc32xx_return_iram_size(void)
{
	if (iram_size == 0) {
		u32 savedval1, savedval2;
		void __iomem *iramptr1, *iramptr2;

		iramptr1 = io_p2v(LPC32XX_IRAM_BASE);
		iramptr2 = io_p2v(LPC32XX_IRAM_BASE + LPC32XX_IRAM_BANK_SIZE);
		savedval1 = __raw_readl(iramptr1);
		savedval2 = __raw_readl(iramptr2);

		if (savedval1 == savedval2) {
			__raw_writel(savedval2 + 1, iramptr2);
			if (__raw_readl(iramptr1) == savedval2 + 1)
				iram_size = LPC32XX_IRAM_BANK_SIZE;
			else
				iram_size = LPC32XX_IRAM_BANK_SIZE * 2;
			__raw_writel(savedval2, iramptr2);
		} else
			iram_size = LPC32XX_IRAM_BANK_SIZE * 2;
	}

	return iram_size;
}

/*
 * Computes PLL rate from PLL register and input clock
 */
u32 clk_check_pll_setup(u32 ifreq, struct clk_pll_setup *pllsetup)
{
	u32 ilfreq, p, m, n, fcco, fref, cfreq;
	int mode;

	/*
	 * PLL requirements
	 * ifreq must be >= 1MHz and <= 20MHz
	 * FCCO must be >= 156MHz and <= 320MHz
	 * FREF must be >= 1MHz and <= 27MHz
	 * Assume the passed input data is not valid
	 */

	ilfreq = ifreq;
	m = pllsetup->pll_m;
	n = pllsetup->pll_n;
	p = pllsetup->pll_p;

	mode = (pllsetup->cco_bypass_b15 << 2) |
		(pllsetup->direct_output_b14 << 1) |
	pllsetup->fdbk_div_ctrl_b13;

	switch (mode) {
	case 0x0: /* Non-integer mode */
		cfreq = (m * ilfreq) / (2 * p * n);
		fcco = (m * ilfreq) / n;
		fref = ilfreq / n;
		break;

	case 0x1: /* integer mode */
		cfreq = (m * ilfreq) / n;
		fcco = (m * ilfreq) / (n * 2 * p);
		fref = ilfreq / n;
		break;

	case 0x2:
	case 0x3: /* Direct mode */
		cfreq = (m * ilfreq) / n;
		fcco = cfreq;
		fref = ilfreq / n;
		break;

	case 0x4:
	case 0x5: /* Bypass mode */
		cfreq = ilfreq / (2 * p);
		fcco = 156000000;
		fref = 1000000;
		break;

	case 0x6:
	case 0x7: /* Direct bypass mode */
	default:
		cfreq = ilfreq;
		fcco = 156000000;
		fref = 1000000;
		break;
	}

	if (fcco < 156000000 || fcco > 320000000)
		cfreq = 0;

	if (fref < 1000000 || fref > 27000000)
		cfreq = 0;

	return (u32) cfreq;
}

u32 clk_get_pclk_div(void)
{
	return 1 + ((__raw_readl(LPC32XX_CLKPWR_HCLK_DIV) >> 2) & 0x1F);
}

static struct map_desc lpc32xx_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(LPC32XX_AHB0_START),
		.pfn		= __phys_to_pfn(LPC32XX_AHB0_START),
		.length		= LPC32XX_AHB0_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= IO_ADDRESS(LPC32XX_AHB1_START),
		.pfn		= __phys_to_pfn(LPC32XX_AHB1_START),
		.length		= LPC32XX_AHB1_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= IO_ADDRESS(LPC32XX_FABAPB_START),
		.pfn		= __phys_to_pfn(LPC32XX_FABAPB_START),
		.length		= LPC32XX_FABAPB_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= IO_ADDRESS(LPC32XX_IRAM_BASE),
		.pfn		= __phys_to_pfn(LPC32XX_IRAM_BASE),
		.length		= (LPC32XX_IRAM_BANK_SIZE * 2),
		.type		= MT_DEVICE
	},
};

void __init lpc32xx_map_io(void)
{
	iotable_init(lpc32xx_io_desc, ARRAY_SIZE(lpc32xx_io_desc));
}
