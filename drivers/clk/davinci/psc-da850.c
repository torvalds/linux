// SPDX-License-Identifier: GPL-2.0
/*
 * PSC clock descriptions for TI DA850/OMAP-L138/AM18XX
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#include <linux/clk-provider.h>
#include <linux/reset-controller.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/types.h>

#include "psc.h"

LPSC_CLKDEV2(emifa_clkdev,	NULL,		"ti-aemif",
				"aemif",	"davinci_nand.0");
LPSC_CLKDEV1(spi0_clkdev,	NULL,		"spi_davinci.0");
LPSC_CLKDEV1(mmcsd0_clkdev,	NULL,		"da830-mmc.0");
LPSC_CLKDEV1(uart0_clkdev,	NULL,		"serial8250.0");
/* REVISIT: used dev_id instead of con_id */
LPSC_CLKDEV1(arm_clkdev,	"arm",		NULL);
LPSC_CLKDEV1(dsp_clkdev,	NULL,		"davinci-rproc.0");

static const struct davinci_lpsc_clk_info da850_psc0_info[] = {
	LPSC(0,  0, tpcc0,   pll0_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(1,  0, tptc0,   pll0_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(2,  0, tptc1,   pll0_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(3,  0, emifa,   async1,       emifa_clkdev,  0),
	LPSC(4,  0, spi0,    pll0_sysclk2, spi0_clkdev,   0),
	LPSC(5,  0, mmcsd0,  pll0_sysclk2, mmcsd0_clkdev, 0),
	LPSC(6,  0, aintc,   pll0_sysclk4, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(7,  0, arm_rom, pll0_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(9,  0, uart0,   pll0_sysclk2, uart0_clkdev,  0),
	LPSC(13, 0, pruss,   pll0_sysclk2, NULL,          0),
	LPSC(14, 0, arm,     pll0_sysclk6, arm_clkdev,    LPSC_ALWAYS_ENABLED | LPSC_SET_RATE_PARENT),
	LPSC(15, 1, dsp,     pll0_sysclk1, dsp_clkdev,    LPSC_FORCE | LPSC_LOCAL_RESET),
	{ }
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
LPSC_CLKDEV1(sata_clkdev,	"fck",	"ahci_da850");
LPSC_CLKDEV1(vpif_clkdev,	NULL,	"vpif");
LPSC_CLKDEV1(spi1_clkdev,	NULL,	"spi_davinci.1");
LPSC_CLKDEV1(i2c1_clkdev,	NULL,	"i2c_davinci.2");
LPSC_CLKDEV1(uart1_clkdev,	NULL,	"serial8250.1");
LPSC_CLKDEV1(uart2_clkdev,	NULL,	"serial8250.2");
LPSC_CLKDEV1(mcbsp0_clkdev,	NULL,	"davinci-mcbsp.0");
LPSC_CLKDEV1(mcbsp1_clkdev,	NULL,	"davinci-mcbsp.1");
LPSC_CLKDEV1(lcdc_clkdev,	"fck",	"da8xx_lcdc.0");
LPSC_CLKDEV3(ehrpwm_clkdev,	"fck",	"ehrpwm.0",
				"fck",	"ehrpwm.1",
				NULL,	"da830-tbclksync");
LPSC_CLKDEV1(mmcsd1_clkdev,	NULL,	"da830-mmc.1");
LPSC_CLKDEV3(ecap_clkdev,	"fck",	"ecap.0",
				"fck",	"ecap.1",
				"fck",	"ecap.2");

static struct reset_control_lookup da850_psc0_reset_lookup_table[] = {
	RESET_LOOKUP("da850-psc0", 15, "davinci-rproc.0", NULL),
};

static int da850_psc0_init(struct device *dev, void __iomem *base)
{
	reset_controller_add_lookup(da850_psc0_reset_lookup_table,
				    ARRAY_SIZE(da850_psc0_reset_lookup_table));
	return davinci_psc_register_clocks(dev, da850_psc0_info, 16, base);
}

static int of_da850_psc0_init(struct device *dev, void __iomem *base)
{
	return of_davinci_psc_clk_init(dev, da850_psc0_info, 16, base);
}

static struct clk_bulk_data da850_psc0_parent_clks[] = {
	{ .id = "pll0_sysclk1" },
	{ .id = "pll0_sysclk2" },
	{ .id = "pll0_sysclk4" },
	{ .id = "pll0_sysclk6" },
	{ .id = "async1"       },
};

const struct davinci_psc_init_data da850_psc0_init_data = {
	.parent_clks		= da850_psc0_parent_clks,
	.num_parent_clks	= ARRAY_SIZE(da850_psc0_parent_clks),
	.psc_init		= &da850_psc0_init,
};

const struct davinci_psc_init_data of_da850_psc0_init_data = {
	.parent_clks		= da850_psc0_parent_clks,
	.num_parent_clks	= ARRAY_SIZE(da850_psc0_parent_clks),
	.psc_init		= &of_da850_psc0_init,
};

static const struct davinci_lpsc_clk_info da850_psc1_info[] = {
	LPSC(0,  0, tpcc1,  pll0_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(1,  0, usb0,   pll0_sysclk2, usb0_clkdev,   0),
	LPSC(2,  0, usb1,   pll0_sysclk4, usb1_clkdev,   0),
	LPSC(3,  0, gpio,   pll0_sysclk4, gpio_clkdev,   0),
	LPSC(5,  0, emac,   pll0_sysclk4, emac_clkdev,   0),
	LPSC(6,  0, ddr,    pll0_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(7,  0, mcasp0, async3,       mcasp0_clkdev, 0),
	LPSC(8,  0, sata,   pll0_sysclk2, sata_clkdev,   LPSC_FORCE),
	LPSC(9,  0, vpif,   pll0_sysclk2, vpif_clkdev,   0),
	LPSC(10, 0, spi1,   async3,       spi1_clkdev,   0),
	LPSC(11, 0, i2c1,   pll0_sysclk4, i2c1_clkdev,   0),
	LPSC(12, 0, uart1,  async3,       uart1_clkdev,  0),
	LPSC(13, 0, uart2,  async3,       uart2_clkdev,  0),
	LPSC(14, 0, mcbsp0, async3,       mcbsp0_clkdev, 0),
	LPSC(15, 0, mcbsp1, async3,       mcbsp1_clkdev, 0),
	LPSC(16, 0, lcdc,   pll0_sysclk2, lcdc_clkdev,   0),
	LPSC(17, 0, ehrpwm, async3,       ehrpwm_clkdev, 0),
	LPSC(18, 0, mmcsd1, pll0_sysclk2, mmcsd1_clkdev, 0),
	LPSC(20, 0, ecap,   async3,       ecap_clkdev,   0),
	LPSC(21, 0, tptc2,  pll0_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	{ }
};

static int da850_psc1_init(struct device *dev, void __iomem *base)
{
	return davinci_psc_register_clocks(dev, da850_psc1_info, 32, base);
}

static int of_da850_psc1_init(struct device *dev, void __iomem *base)
{
	return of_davinci_psc_clk_init(dev, da850_psc1_info, 32, base);
}

static struct clk_bulk_data da850_psc1_parent_clks[] = {
	{ .id = "pll0_sysclk2" },
	{ .id = "pll0_sysclk4" },
	{ .id = "async3"       },
};

const struct davinci_psc_init_data da850_psc1_init_data = {
	.parent_clks		= da850_psc1_parent_clks,
	.num_parent_clks	= ARRAY_SIZE(da850_psc1_parent_clks),
	.psc_init		= &da850_psc1_init,
};

const struct davinci_psc_init_data of_da850_psc1_init_data = {
	.parent_clks		= da850_psc1_parent_clks,
	.num_parent_clks	= ARRAY_SIZE(da850_psc1_parent_clks),
	.psc_init		= &of_da850_psc1_init,
};
