// SPDX-License-Identifier: GPL-2.0
/*
 * PSC clock descriptions for TI DaVinci DM365
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk/davinci.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "psc.h"

LPSC_CLKDEV1(vpss_slave_clkdev,		"slave",	"vpss");
LPSC_CLKDEV1(spi1_clkdev,		NULL,		"spi_davinci.1");
LPSC_CLKDEV1(mmcsd1_clkdev,		NULL,		"da830-mmc.1");
LPSC_CLKDEV1(asp0_clkdev,		NULL,		"davinci-mcbsp");
LPSC_CLKDEV1(usb_clkdev,		"usb",		NULL);
LPSC_CLKDEV1(spi2_clkdev,		NULL,		"spi_davinci.2");
LPSC_CLKDEV1(aemif_clkdev,		"aemif",	NULL);
LPSC_CLKDEV1(mmcsd0_clkdev,		NULL,		"da830-mmc.0");
LPSC_CLKDEV1(i2c_clkdev,		NULL,		"i2c_davinci.1");
LPSC_CLKDEV1(uart0_clkdev,		NULL,		"serial8250.0");
LPSC_CLKDEV1(uart1_clkdev,		NULL,		"serial8250.1");
LPSC_CLKDEV1(spi0_clkdev,		NULL,		"spi_davinci.0");
/* REVISIT: gpio-davinci.c should be modified to drop con_id */
LPSC_CLKDEV1(gpio_clkdev,		"gpio",		NULL);
LPSC_CLKDEV1(timer0_clkdev,		"timer0",	NULL);
LPSC_CLKDEV1(timer2_clkdev,		NULL,		"davinci-wdt");
LPSC_CLKDEV1(spi3_clkdev,		NULL,		"spi_davinci.3");
LPSC_CLKDEV1(spi4_clkdev,		NULL,		"spi_davinci.4");
LPSC_CLKDEV2(emac_clkdev,		NULL,		"davinci_emac.1",
					"fck",		"davinci_mdio.0");
LPSC_CLKDEV1(voice_codec_clkdev,	NULL,		"davinci_voicecodec");
LPSC_CLKDEV1(vpss_dac_clkdev,		"vpss_dac",	NULL);
LPSC_CLKDEV1(vpss_master_clkdev,	"master",	"vpss");

static const struct davinci_lpsc_clk_info dm365_psc_info[] = {
	LPSC(1,  0, vpss_slave,  pll1_sysclk5, vpss_slave_clkdev,  0),
	LPSC(5,  0, timer3,      pll1_auxclk,  NULL,               0),
	LPSC(6,  0, spi1,        pll1_sysclk4, spi1_clkdev,        0),
	LPSC(7,  0, mmcsd1,      pll1_sysclk4, mmcsd1_clkdev,      0),
	LPSC(8,  0, asp0,        pll1_sysclk4, asp0_clkdev,        0),
	LPSC(9,  0, usb,         pll1_auxclk,  usb_clkdev,         0),
	LPSC(10, 0, pwm3,        pll1_auxclk,  NULL,               0),
	LPSC(11, 0, spi2,        pll1_sysclk4, spi2_clkdev,        0),
	LPSC(12, 0, rto,         pll1_sysclk4, NULL,               0),
	LPSC(14, 0, aemif,       pll1_sysclk4, aemif_clkdev,       0),
	LPSC(15, 0, mmcsd0,      pll1_sysclk8, mmcsd0_clkdev,      0),
	LPSC(18, 0, i2c,         pll1_auxclk,  i2c_clkdev,         0),
	LPSC(19, 0, uart0,       pll1_auxclk,  uart0_clkdev,       0),
	LPSC(20, 0, uart1,       pll1_sysclk4, uart1_clkdev,       0),
	LPSC(22, 0, spi0,        pll1_sysclk4, spi0_clkdev,        0),
	LPSC(23, 0, pwm0,        pll1_auxclk,  NULL,               0),
	LPSC(24, 0, pwm1,        pll1_auxclk,  NULL,               0),
	LPSC(25, 0, pwm2,        pll1_auxclk,  NULL,               0),
	LPSC(26, 0, gpio,        pll1_sysclk4, gpio_clkdev,        0),
	LPSC(27, 0, timer0,      pll1_auxclk,  timer0_clkdev,      LPSC_ALWAYS_ENABLED),
	LPSC(28, 0, timer1,      pll1_auxclk,  NULL,               0),
	/* REVISIT: why can't this be disabled? */
	LPSC(29, 0, timer2,      pll1_auxclk,  timer2_clkdev,      LPSC_ALWAYS_ENABLED),
	LPSC(31, 0, arm,         pll2_sysclk2, NULL,               LPSC_ALWAYS_ENABLED),
	LPSC(38, 0, spi3,        pll1_sysclk4, spi3_clkdev,        0),
	LPSC(39, 0, spi4,        pll1_auxclk,  spi4_clkdev,        0),
	LPSC(40, 0, emac,        pll1_sysclk4, emac_clkdev,        0),
	/*
	 * The TRM (ARM Subsystem User's Guide) shows two clocks input into
	 * voice codec module (PLL2 SYSCLK4 with a DIV2 and PLL1 SYSCLK4). Its
	 * not fully clear from documentation which clock should be considered
	 * as parent for PSC. The clock chosen here is to maintain
	 * compatibility with existing code in arch/arm/mach-davinci/dm365.c
	 */
	LPSC(44, 0, voice_codec, pll2_sysclk4, voice_codec_clkdev, 0),
	/*
	 * Its not fully clear from TRM (ARM Subsystem User's Guide) as to what
	 * the parent of VPSS DAC LPSC should actually be. PLL1 SYSCLK3 feeds
	 * into HDVICP and MJCP. The clock chosen here is to remain compatible
	 * with code existing in arch/arm/mach-davinci/dm365.c
	 */
	LPSC(46, 0, vpss_dac,    pll1_sysclk3, vpss_dac_clkdev,    0),
	LPSC(47, 0, vpss_master, pll1_sysclk5, vpss_master_clkdev, 0),
	LPSC(50, 0, mjcp,        pll1_sysclk3, NULL,               0),
	{ }
};

int dm365_psc_init(struct device *dev, void __iomem *base)
{
	return davinci_psc_register_clocks(dev, dm365_psc_info, 52, base);
}

static struct clk_bulk_data dm365_psc_parent_clks[] = {
	{ .id = "pll1_sysclk1" },
	{ .id = "pll1_sysclk3" },
	{ .id = "pll1_sysclk4" },
	{ .id = "pll1_sysclk5" },
	{ .id = "pll1_sysclk8" },
	{ .id = "pll2_sysclk2" },
	{ .id = "pll2_sysclk4" },
	{ .id = "pll1_auxclk"  },
};

const struct davinci_psc_init_data dm365_psc_init_data = {
	.parent_clks		= dm365_psc_parent_clks,
	.num_parent_clks	= ARRAY_SIZE(dm365_psc_parent_clks),
	.psc_init		= &dm365_psc_init,
};
