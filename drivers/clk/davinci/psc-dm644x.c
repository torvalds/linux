// SPDX-License-Identifier: GPL-2.0
/*
 * PSC clock descriptions for TI DaVinci DM644x
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
LPSC_CLKDEV2(emac_clkdev,		NULL,		"davinci_emac.1",
					"fck",		"davinci_mdio.0");
LPSC_CLKDEV1(usb_clkdev,		"usb",		NULL);
LPSC_CLKDEV1(ide_clkdev,		NULL,		"palm_bk3710");
LPSC_CLKDEV1(aemif_clkdev,		"aemif",	NULL);
LPSC_CLKDEV1(mmcsd_clkdev,		NULL,		"dm6441-mmc.0");
LPSC_CLKDEV1(asp0_clkdev,		NULL,		"davinci-mcbsp");
LPSC_CLKDEV1(i2c_clkdev,		NULL,		"i2c_davinci.1");
LPSC_CLKDEV1(uart0_clkdev,		NULL,		"serial8250.0");
LPSC_CLKDEV1(uart1_clkdev,		NULL,		"serial8250.1");
LPSC_CLKDEV1(uart2_clkdev,		NULL,		"serial8250.2");
/* REVISIT: gpio-davinci.c should be modified to drop con_id */
LPSC_CLKDEV1(gpio_clkdev,		"gpio",		NULL);
LPSC_CLKDEV1(timer0_clkdev,		"timer0",	NULL);
LPSC_CLKDEV1(timer2_clkdev,		NULL,		"davinci-wdt");

static const struct davinci_lpsc_clk_info dm644x_psc_info[] = {
	LPSC(0,  0, vpss_master, pll1_sysclk3, vpss_master_clkdev, 0),
	LPSC(1,  0, vpss_slave,  pll1_sysclk3, vpss_slave_clkdev,  0),
	LPSC(6,  0, emac,        pll1_sysclk5, emac_clkdev,        0),
	LPSC(9,  0, usb,         pll1_sysclk5, usb_clkdev,         0),
	LPSC(10, 0, ide,         pll1_sysclk5, ide_clkdev,         0),
	LPSC(11, 0, vlynq,       pll1_sysclk5, NULL,               0),
	LPSC(14, 0, aemif,       pll1_sysclk5, aemif_clkdev,       0),
	LPSC(15, 0, mmcsd,       pll1_sysclk5, mmcsd_clkdev,       0),
	LPSC(17, 0, asp0,        pll1_sysclk5, asp0_clkdev,        0),
	LPSC(18, 0, i2c,         pll1_auxclk,  i2c_clkdev,         0),
	LPSC(19, 0, uart0,       pll1_auxclk,  uart0_clkdev,       0),
	LPSC(20, 0, uart1,       pll1_auxclk,  uart1_clkdev,       0),
	LPSC(21, 0, uart2,       pll1_auxclk,  uart2_clkdev,       0),
	LPSC(22, 0, spi,         pll1_sysclk5, NULL,               0),
	LPSC(23, 0, pwm0,        pll1_auxclk,  NULL,               0),
	LPSC(24, 0, pwm1,        pll1_auxclk,  NULL,               0),
	LPSC(25, 0, pwm2,        pll1_auxclk,  NULL,               0),
	LPSC(26, 0, gpio,        pll1_sysclk5, gpio_clkdev,        0),
	LPSC(27, 0, timer0,      pll1_auxclk,  timer0_clkdev,      LPSC_ALWAYS_ENABLED),
	LPSC(28, 0, timer1,      pll1_auxclk,  NULL,               0),
	/* REVISIT: why can't this be disabled? */
	LPSC(29, 0, timer2,      pll1_auxclk,  timer2_clkdev,      LPSC_ALWAYS_ENABLED),
	LPSC(31, 0, arm,         pll1_sysclk2, NULL,               LPSC_ALWAYS_ENABLED),
	/* REVISIT how to disable? */
	LPSC(39, 1, dsp,         pll1_sysclk1, NULL,               LPSC_ALWAYS_ENABLED),
	/* REVISIT how to disable? */
	LPSC(40, 1, vicp,        pll1_sysclk2, NULL,               LPSC_ALWAYS_ENABLED),
	{ }
};

static int dm644x_psc_init(struct device *dev, void __iomem *base)
{
	return davinci_psc_register_clocks(dev, dm644x_psc_info, 41, base);
}

static struct clk_bulk_data dm644x_psc_parent_clks[] = {
	{ .id = "pll1_sysclk1" },
	{ .id = "pll1_sysclk2" },
	{ .id = "pll1_sysclk3" },
	{ .id = "pll1_sysclk5" },
	{ .id = "pll1_auxclk"  },
};

const struct davinci_psc_init_data dm644x_psc_init_data = {
	.parent_clks		= dm644x_psc_parent_clks,
	.num_parent_clks	= ARRAY_SIZE(dm644x_psc_parent_clks),
	.psc_init		= &dm644x_psc_init,
};
