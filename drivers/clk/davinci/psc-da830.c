// SPDX-License-Identifier: GPL-2.0
/*
 * PSC clock descriptions for TI DA830/OMAP-L137/AM17XX
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

LPSC_CLKDEV1(spi0_clkdev,	NULL,	"spi_davinci.0");
LPSC_CLKDEV1(mmcsd_clkdev,	NULL,	"da830-mmc.0");
LPSC_CLKDEV1(uart0_clkdev,	NULL,	"serial8250.0");

static const struct davinci_lpsc_clk_info da830_psc0_info[] = {
	LPSC(0,  0, tpcc,     pll0_sysclk2, NULL,         LPSC_ALWAYS_ENABLED),
	LPSC(1,  0, tptc0,    pll0_sysclk2, NULL,         LPSC_ALWAYS_ENABLED),
	LPSC(2,  0, tptc1,    pll0_sysclk2, NULL,         LPSC_ALWAYS_ENABLED),
	LPSC(3,  0, aemif,    pll0_sysclk3, NULL,         LPSC_ALWAYS_ENABLED),
	LPSC(4,  0, spi0,     pll0_sysclk2, spi0_clkdev,  0),
	LPSC(5,  0, mmcsd,    pll0_sysclk2, mmcsd_clkdev, 0),
	LPSC(6,  0, aintc,    pll0_sysclk4, NULL,         LPSC_ALWAYS_ENABLED),
	LPSC(7,  0, arm_rom,  pll0_sysclk2, NULL,         LPSC_ALWAYS_ENABLED),
	LPSC(8,  0, secu_mgr, pll0_sysclk4, NULL,         LPSC_ALWAYS_ENABLED),
	LPSC(9,  0, uart0,    pll0_sysclk2, uart0_clkdev, 0),
	LPSC(10, 0, scr0_ss,  pll0_sysclk2, NULL,         LPSC_ALWAYS_ENABLED),
	LPSC(11, 0, scr1_ss,  pll0_sysclk2, NULL,         LPSC_ALWAYS_ENABLED),
	LPSC(12, 0, scr2_ss,  pll0_sysclk2, NULL,         LPSC_ALWAYS_ENABLED),
	LPSC(13, 0, pruss,    pll0_sysclk2, NULL,         LPSC_ALWAYS_ENABLED),
	LPSC(14, 0, arm,      pll0_sysclk6, NULL,         LPSC_ALWAYS_ENABLED),
	{ }
};

static int da830_psc0_init(struct device *dev, void __iomem *base)
{
	return davinci_psc_register_clocks(dev, da830_psc0_info, 16, base);
}

static struct clk_bulk_data da830_psc0_parent_clks[] = {
	{ .id = "pll0_sysclk2" },
	{ .id = "pll0_sysclk3" },
	{ .id = "pll0_sysclk4" },
	{ .id = "pll0_sysclk6" },
};

const struct davinci_psc_init_data da830_psc0_init_data = {
	.parent_clks		= da830_psc0_parent_clks,
	.num_parent_clks	= ARRAY_SIZE(da830_psc0_parent_clks),
	.psc_init		= &da830_psc0_init,
};

LPSC_CLKDEV3(usb0_clkdev,	"fck",	"da830-usb-phy-clks",
				NULL,	"musb-da8xx",
				NULL,	"cppi41-dmaengine");
LPSC_CLKDEV1(usb1_clkdev,	NULL,	"ohci-da8xx");
/* REVISIT: gpio-davinci.c should be modified to drop con_id */
LPSC_CLKDEV1(gpio_clkdev,	"gpio",	NULL);
LPSC_CLKDEV2(emac_clkdev,	NULL,	"davinci_emac.1",
				"fck",	"davinci_mdio.0");
LPSC_CLKDEV1(mcasp0_clkdev,	NULL,	"davinci-mcasp.0");
LPSC_CLKDEV1(mcasp1_clkdev,	NULL,	"davinci-mcasp.1");
LPSC_CLKDEV1(mcasp2_clkdev,	NULL,	"davinci-mcasp.2");
LPSC_CLKDEV1(spi1_clkdev,	NULL,	"spi_davinci.1");
LPSC_CLKDEV1(i2c1_clkdev,	NULL,	"i2c_davinci.2");
LPSC_CLKDEV1(uart1_clkdev,	NULL,	"serial8250.1");
LPSC_CLKDEV1(uart2_clkdev,	NULL,	"serial8250.2");
LPSC_CLKDEV1(lcdc_clkdev,	"fck",	"da8xx_lcdc.0");
LPSC_CLKDEV2(pwm_clkdev,	"fck",	"ehrpwm.0",
				"fck",	"ehrpwm.1");
LPSC_CLKDEV3(ecap_clkdev,	"fck",	"ecap.0",
				"fck",	"ecap.1",
				"fck",	"ecap.2");
LPSC_CLKDEV2(eqep_clkdev,	NULL,	"eqep.0",
				NULL,	"eqep.1");

static const struct davinci_lpsc_clk_info da830_psc1_info[] = {
	LPSC(1,  0, usb0,   pll0_sysclk2, usb0_clkdev,   0),
	LPSC(2,  0, usb1,   pll0_sysclk4, usb1_clkdev,   0),
	LPSC(3,  0, gpio,   pll0_sysclk4, gpio_clkdev,   0),
	LPSC(5,  0, emac,   pll0_sysclk4, emac_clkdev,   0),
	LPSC(6,  0, emif3,  pll0_sysclk5, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(7,  0, mcasp0, pll0_sysclk2, mcasp0_clkdev, 0),
	LPSC(8,  0, mcasp1, pll0_sysclk2, mcasp1_clkdev, 0),
	LPSC(9,  0, mcasp2, pll0_sysclk2, mcasp2_clkdev, 0),
	LPSC(10, 0, spi1,   pll0_sysclk2, spi1_clkdev,   0),
	LPSC(11, 0, i2c1,   pll0_sysclk4, i2c1_clkdev,   0),
	LPSC(12, 0, uart1,  pll0_sysclk2, uart1_clkdev,  0),
	LPSC(13, 0, uart2,  pll0_sysclk2, uart2_clkdev,  0),
	LPSC(16, 0, lcdc,   pll0_sysclk2, lcdc_clkdev,   0),
	LPSC(17, 0, pwm,    pll0_sysclk2, pwm_clkdev,    0),
	LPSC(20, 0, ecap,   pll0_sysclk2, ecap_clkdev,   0),
	LPSC(21, 0, eqep,   pll0_sysclk2, eqep_clkdev,   0),
	{ }
};

static int da830_psc1_init(struct device *dev, void __iomem *base)
{
	return davinci_psc_register_clocks(dev, da830_psc1_info, 32, base);
}

static struct clk_bulk_data da830_psc1_parent_clks[] = {
	{ .id = "pll0_sysclk2" },
	{ .id = "pll0_sysclk4" },
	{ .id = "pll0_sysclk5" },
};

const struct davinci_psc_init_data da830_psc1_init_data = {
	.parent_clks		= da830_psc1_parent_clks,
	.num_parent_clks	= ARRAY_SIZE(da830_psc1_parent_clks),
	.psc_init		= &da830_psc1_init,
};
