// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Clock tree for CSR SiRFprimaII
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 */

#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>

#include "prima2.h"
#include "clk-common.c"

static struct clk_dmn clk_mmc01 = {
	.regofs = SIRFSOC_CLKC_MMC_CFG,
	.enable_bit = 59,
	.hw = {
		.init = &clk_mmc01_init,
	},
};

static struct clk_dmn clk_mmc23 = {
	.regofs = SIRFSOC_CLKC_MMC_CFG,
	.enable_bit = 60,
	.hw = {
		.init = &clk_mmc23_init,
	},
};

static struct clk_dmn clk_mmc45 = {
	.regofs = SIRFSOC_CLKC_MMC_CFG,
	.enable_bit = 61,
	.hw = {
		.init = &clk_mmc45_init,
	},
};

static const struct clk_init_data clk_nand_init = {
	.name = "nand",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_nand = {
	.enable_bit = 34,
	.hw = {
		.init = &clk_nand_init,
	},
};

enum prima2_clk_index {
	/* 0    1     2      3      4      5      6       7         8      9 */
	rtc,    osc,   pll1,  pll2,  pll3,  mem,   sys,   security, dsp,   gps,
	mf,     io,    cpu,   uart0, uart1, uart2, tsc,   i2c0,     i2c1,  spi0,
	spi1,   pwmc,  efuse, pulse, dmac0, dmac1, nand,  audio,    usp0,  usp1,
	usp2,   vip,   gfx,   mm,    lcd,   vpp,   mmc01, mmc23,    mmc45, usbpll,
	usb0,  usb1,   cphif, maxclk,
};

static __initdata struct clk_hw *prima2_clk_hw_array[maxclk] = {
	NULL, /* dummy */
	NULL,
	&clk_pll1.hw,
	&clk_pll2.hw,
	&clk_pll3.hw,
	&clk_mem.hw,
	&clk_sys.hw,
	&clk_security.hw,
	&clk_dsp.hw,
	&clk_gps.hw,
	&clk_mf.hw,
	&clk_io.hw,
	&clk_cpu.hw,
	&clk_uart0.hw,
	&clk_uart1.hw,
	&clk_uart2.hw,
	&clk_tsc.hw,
	&clk_i2c0.hw,
	&clk_i2c1.hw,
	&clk_spi0.hw,
	&clk_spi1.hw,
	&clk_pwmc.hw,
	&clk_efuse.hw,
	&clk_pulse.hw,
	&clk_dmac0.hw,
	&clk_dmac1.hw,
	&clk_nand.hw,
	&clk_audio.hw,
	&clk_usp0.hw,
	&clk_usp1.hw,
	&clk_usp2.hw,
	&clk_vip.hw,
	&clk_gfx.hw,
	&clk_mm.hw,
	&clk_lcd.hw,
	&clk_vpp.hw,
	&clk_mmc01.hw,
	&clk_mmc23.hw,
	&clk_mmc45.hw,
	&usb_pll_clk_hw,
	&clk_usb0.hw,
	&clk_usb1.hw,
	&clk_cphif.hw,
};

static struct clk *prima2_clks[maxclk];

static void __init prima2_clk_init(struct device_node *np)
{
	struct device_node *rscnp;
	int i;

	rscnp = of_find_compatible_node(NULL, NULL, "sirf,prima2-rsc");
	sirfsoc_rsc_vbase = of_iomap(rscnp, 0);
	if (!sirfsoc_rsc_vbase)
		panic("unable to map rsc registers\n");
	of_node_put(rscnp);

	sirfsoc_clk_vbase = of_iomap(np, 0);
	if (!sirfsoc_clk_vbase)
		panic("unable to map clkc registers\n");

	/* These are always available (RTC and 26MHz OSC)*/
	prima2_clks[rtc] = clk_register_fixed_rate(NULL, "rtc", NULL, 0, 32768);
	prima2_clks[osc] = clk_register_fixed_rate(NULL, "osc", NULL, 0,
						   26000000);

	for (i = pll1; i < maxclk; i++) {
		prima2_clks[i] = clk_register(NULL, prima2_clk_hw_array[i]);
		BUG_ON(IS_ERR(prima2_clks[i]));
	}
	clk_register_clkdev(prima2_clks[cpu], NULL, "cpu");
	clk_register_clkdev(prima2_clks[io],  NULL, "io");
	clk_register_clkdev(prima2_clks[mem],  NULL, "mem");
	clk_register_clkdev(prima2_clks[mem],  NULL, "osc");

	clk_data.clks = prima2_clks;
	clk_data.clk_num = maxclk;

	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}
CLK_OF_DECLARE(prima2_clk, "sirf,prima2-clkc", prima2_clk_init);
