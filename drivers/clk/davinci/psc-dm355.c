// SPDX-License-Identifier: GPL-2.0
/*
 * PSC clock descriptions for TI DaVinci DM355
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "psc.h"

LPSC_CLKDEV1(vpss_master_clkdev,	"master",	"vpss");
LPSC_CLKDEV1(vpss_slave_clkdev,		"slave",	"vpss");
LPSC_CLKDEV1(spi1_clkdev,		NULL,		"spi_davinci.1");
LPSC_CLKDEV1(mmcsd1_clkdev,		NULL,		"dm6441-mmc.1");
LPSC_CLKDEV1(mcbsp1_clkdev,		NULL,		"davinci-mcbsp.1");
LPSC_CLKDEV1(usb_clkdev,		"usb",		NULL);
LPSC_CLKDEV1(spi2_clkdev,		NULL,		"spi_davinci.2");
LPSC_CLKDEV1(aemif_clkdev,		"aemif",	NULL);
LPSC_CLKDEV1(mmcsd0_clkdev,		NULL,		"dm6441-mmc.0");
LPSC_CLKDEV1(mcbsp0_clkdev,		NULL,		"davinci-mcbsp.0");
LPSC_CLKDEV1(i2c_clkdev,		NULL,		"i2c_davinci.1");
LPSC_CLKDEV1(uart0_clkdev,		NULL,		"serial8250.0");
LPSC_CLKDEV1(uart1_clkdev,		NULL,		"serial8250.1");
LPSC_CLKDEV1(uart2_clkdev,		NULL,		"serial8250.2");
LPSC_CLKDEV1(spi0_clkdev,		NULL,		"spi_davinci.0");
/* REVISIT: gpio-davinci.c should be modified to drop con_id */
LPSC_CLKDEV1(gpio_clkdev,		"gpio",		NULL);
LPSC_CLKDEV1(timer0_clkdev,		"timer0",	NULL);
LPSC_CLKDEV1(timer2_clkdev,		NULL,		"davinci-wdt");
LPSC_CLKDEV1(vpss_dac_clkdev,		"vpss_dac",	NULL);

static const struct davinci_lpsc_clk_info dm355_psc_info[] = {
	LPSC(0,  0, vpss_master, pll1_sysclk4, vpss_master_clkdev, 0),
	LPSC(1,  0, vpss_slave,  pll1_sysclk4, vpss_slave_clkdev,  0),
	LPSC(5,  0, timer3,      pll1_auxclk,  NULL,               0),
	LPSC(6,  0, spi1,        pll1_sysclk2, spi1_clkdev,        0),
	LPSC(7,  0, mmcsd1,      pll1_sysclk2, mmcsd1_clkdev,      0),
	LPSC(8,  0, asp1,        pll1_sysclk2, NULL,               0),
	LPSC(9,  0, usb,         pll1_sysclk2, usb_clkdev,         0),
	LPSC(10, 0, pwm3,        pll1_auxclk,  NULL,               0),
	LPSC(11, 0, spi2,        pll1_sysclk2, spi2_clkdev,        0),
	LPSC(12, 0, rto,         pll1_auxclk,  NULL,               0),
	LPSC(14, 0, aemif,       pll1_sysclk2, aemif_clkdev,       0),
	LPSC(15, 0, mmcsd0,      pll1_sysclk2, mmcsd0_clkdev,      0),
	LPSC(17, 0, asp0,        pll1_sysclk2, NULL,               0),
	LPSC(18, 0, i2c,         pll1_auxclk,  i2c_clkdev,         0),
	LPSC(19, 0, uart0,       pll1_auxclk,  uart0_clkdev,       0),
	LPSC(20, 0, uart1,       pll1_auxclk,  uart1_clkdev,       0),
	LPSC(21, 0, uart2,       pll1_sysclk2, uart2_clkdev,       0),
	LPSC(22, 0, spi0,        pll1_sysclk2, spi0_clkdev,        0),
	LPSC(23, 0, pwm0,        pll1_auxclk,  NULL,               0),
	LPSC(24, 0, pwm1,        pll1_auxclk,  NULL,               0),
	LPSC(25, 0, pwm2,        pll1_auxclk,  NULL,               0),
	LPSC(26, 0, gpio,        pll1_sysclk2, gpio_clkdev,        0),
	LPSC(27, 0, timer0,      pll1_auxclk,  timer0_clkdev,      LPSC_ALWAYS_ENABLED),
	LPSC(28, 0, timer1,      pll1_auxclk,  NULL,               0),
	/* REVISIT: why can't this be disabled? */
	LPSC(29, 0, timer2,      pll1_auxclk,  timer2_clkdev,      LPSC_ALWAYS_ENABLED),
	LPSC(31, 0, arm,         pll1_sysclk1, NULL,               LPSC_ALWAYS_ENABLED),
	LPSC(40, 0, mjcp,        pll1_sysclk1, NULL,               0),
	LPSC(41, 0, vpss_dac,    pll1_sysclk3, vpss_dac_clkdev,    0),
	{ }
};

static int dm355_psc_init(struct device *dev, void __iomem *base)
{
	return davinci_psc_register_clocks(dev, dm355_psc_info, 42, base);
}

static struct clk_bulk_data dm355_psc_parent_clks[] = {
	{ .id = "pll1_sysclk1" },
	{ .id = "pll1_sysclk2" },
	{ .id = "pll1_sysclk3" },
	{ .id = "pll1_sysclk4" },
	{ .id = "pll1_auxclk"  },
};

const struct davinci_psc_init_data dm355_psc_init_data = {
	.parent_clks		= dm355_psc_parent_clks,
	.num_parent_clks	= ARRAY_SIZE(dm355_psc_parent_clks),
	.psc_init		= &dm355_psc_init,
};
