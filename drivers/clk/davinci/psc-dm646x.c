// SPDX-License-Identifier: GPL-2.0
/*
 * PSC clock descriptions for TI DaVinci DM646x
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

LPSC_CLKDEV1(ide_clkdev,	NULL,		"palm_bk3710");
LPSC_CLKDEV2(emac_clkdev,	NULL,		"davinci_emac.1",
				"fck",		"davinci_mdio.0");
LPSC_CLKDEV1(aemif_clkdev,	"aemif",	NULL);
LPSC_CLKDEV1(mcasp0_clkdev,	NULL,		"davinci-mcasp.0");
LPSC_CLKDEV1(mcasp1_clkdev,	NULL,		"davinci-mcasp.1");
LPSC_CLKDEV1(uart0_clkdev,	NULL,		"serial8250.0");
LPSC_CLKDEV1(uart1_clkdev,	NULL,		"serial8250.1");
LPSC_CLKDEV1(uart2_clkdev,	NULL,		"serial8250.2");
LPSC_CLKDEV1(i2c_clkdev,	NULL,		"i2c_davinci.1");
/* REVISIT: gpio-davinci.c should be modified to drop con_id */
LPSC_CLKDEV1(gpio_clkdev,	"gpio",		NULL);
LPSC_CLKDEV1(timer0_clkdev,	"timer0",	 NULL);

static const struct davinci_lpsc_clk_info dm646x_psc_info[] = {
	LPSC(0,  0, arm,      pll1_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	/* REVISIT how to disable? */
	LPSC(1,  0, dsp,      pll1_sysclk1, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(4,  0, edma_cc,  pll1_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(5,  0, edma_tc0, pll1_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(6,  0, edma_tc1, pll1_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(7,  0, edma_tc2, pll1_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(8,  0, edma_tc3, pll1_sysclk2, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(10, 0, ide,      pll1_sysclk4, ide_clkdev,    0),
	LPSC(14, 0, emac,     pll1_sysclk3, emac_clkdev,   0),
	LPSC(16, 0, vpif0,    ref_clk,      NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(17, 0, vpif1,    ref_clk,      NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(21, 0, aemif,    pll1_sysclk3, aemif_clkdev,  LPSC_ALWAYS_ENABLED),
	LPSC(22, 0, mcasp0,   pll1_sysclk3, mcasp0_clkdev, 0),
	LPSC(23, 0, mcasp1,   pll1_sysclk3, mcasp1_clkdev, 0),
	LPSC(26, 0, uart0,    aux_clkin,    uart0_clkdev,  0),
	LPSC(27, 0, uart1,    aux_clkin,    uart1_clkdev,  0),
	LPSC(28, 0, uart2,    aux_clkin,    uart2_clkdev,  0),
	/* REVIST: disabling hangs system */
	LPSC(29, 0, pwm0,     pll1_sysclk3, NULL,          LPSC_ALWAYS_ENABLED),
	/* REVIST: disabling hangs system */
	LPSC(30, 0, pwm1,     pll1_sysclk3, NULL,          LPSC_ALWAYS_ENABLED),
	LPSC(31, 0, i2c,      pll1_sysclk3, i2c_clkdev,    0),
	LPSC(33, 0, gpio,     pll1_sysclk3, gpio_clkdev,   0),
	LPSC(34, 0, timer0,   pll1_sysclk3, timer0_clkdev, LPSC_ALWAYS_ENABLED),
	LPSC(35, 0, timer1,   pll1_sysclk3, NULL,          0),
	{ }
};

int dm646x_psc_init(struct device *dev, void __iomem *base)
{
	return davinci_psc_register_clocks(dev, dm646x_psc_info, 46, base);
}

static struct clk_bulk_data dm646x_psc_parent_clks[] = {
	{ .id = "ref_clk"      },
	{ .id = "aux_clkin"    },
	{ .id = "pll1_sysclk1" },
	{ .id = "pll1_sysclk2" },
	{ .id = "pll1_sysclk3" },
	{ .id = "pll1_sysclk4" },
	{ .id = "pll1_sysclk5" },
};

const struct davinci_psc_init_data dm646x_psc_init_data = {
	.parent_clks		= dm646x_psc_parent_clks,
	.num_parent_clks	= ARRAY_SIZE(dm646x_psc_parent_clks),
	.psc_init		= &dm646x_psc_init,
};
