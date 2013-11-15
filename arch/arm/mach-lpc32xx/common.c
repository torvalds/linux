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
#include <asm/system_info.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include "common.h"

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
static void lpc32xx_watchdog_reset(void)
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
		.virtual	= (unsigned long)IO_ADDRESS(LPC32XX_AHB0_START),
		.pfn		= __phys_to_pfn(LPC32XX_AHB0_START),
		.length		= LPC32XX_AHB0_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= (unsigned long)IO_ADDRESS(LPC32XX_AHB1_START),
		.pfn		= __phys_to_pfn(LPC32XX_AHB1_START),
		.length		= LPC32XX_AHB1_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= (unsigned long)IO_ADDRESS(LPC32XX_FABAPB_START),
		.pfn		= __phys_to_pfn(LPC32XX_FABAPB_START),
		.length		= LPC32XX_FABAPB_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= (unsigned long)IO_ADDRESS(LPC32XX_IRAM_BASE),
		.pfn		= __phys_to_pfn(LPC32XX_IRAM_BASE),
		.length		= (LPC32XX_IRAM_BANK_SIZE * 2),
		.type		= MT_DEVICE
	},
};

void __init lpc32xx_map_io(void)
{
	iotable_init(lpc32xx_io_desc, ARRAY_SIZE(lpc32xx_io_desc));
}

void lpc23xx_restart(enum reboot_mode mode, const char *cmd)
{
	switch (mode) {
	case REBOOT_SOFT:
	case REBOOT_HARD:
		lpc32xx_watchdog_reset();
		break;

	default:
		/* Do nothing */
		break;
	}

	/* Wait for watchdog to reset system */
	while (1)
		;
}

static int __init lpc32xx_check_uid(void)
{
	u32 uid[4];

	lpc32xx_get_uid(uid);

	printk(KERN_INFO "LPC32XX unique ID: %08x%08x%08x%08x\n",
		uid[3], uid[2], uid[1], uid[0]);

	if (!system_serial_low && !system_serial_high) {
		system_serial_low = uid[0];
		system_serial_high = uid[1];
	}

	return 1;
}
arch_initcall(lpc32xx_check_uid);
