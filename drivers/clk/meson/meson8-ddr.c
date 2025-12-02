// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic Meson8 DDR clock controller
 *
 * Copyright (C) 2019 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <dt-bindings/clock/meson8-ddr-clkc.h>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-regmap.h"
#include "clk-pll.h"
#include "meson-clkc-utils.h"

#define AM_DDR_PLL_CNTL			0x00
#define AM_DDR_PLL_CNTL1		0x04
#define AM_DDR_PLL_CNTL2		0x08
#define AM_DDR_PLL_CNTL3		0x0c
#define AM_DDR_PLL_CNTL4		0x10
#define AM_DDR_PLL_STS			0x14
#define DDR_CLK_CNTL			0x18
#define DDR_CLK_STS			0x1c

static struct clk_regmap meson8_ddr_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = AM_DDR_PLL_CNTL,
			.shift   = 30,
			.width   = 1,
		},
		.m = {
			.reg_off = AM_DDR_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = AM_DDR_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.l = {
			.reg_off = AM_DDR_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = AM_DDR_PLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "ddr_pll_dco",
		.ops = &meson_clk_pll_ro_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8_ddr_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = AM_DDR_PLL_CNTL,
		.shift = 16,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ddr_pll",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8_ddr_pll_dco.hw
		},
		.num_parents = 1,
	},
};

static struct clk_hw *meson8_ddr_hw_clks[] = {
	[DDR_CLKID_DDR_PLL_DCO]		= &meson8_ddr_pll_dco.hw,
	[DDR_CLKID_DDR_PLL]		= &meson8_ddr_pll.hw,
};

static const struct meson_clkc_data meson8_ddr_clkc_data = {
	.hw_clks = {
		.hws = meson8_ddr_hw_clks,
		.num = ARRAY_SIZE(meson8_ddr_hw_clks),
	},
};

static const struct of_device_id meson8_ddr_clkc_match_table[] = {
	{
		.compatible = "amlogic,meson8-ddr-clkc",
		.data = &meson8_ddr_clkc_data,
	}, {
		.compatible = "amlogic,meson8b-ddr-clkc",
		.data = &meson8_ddr_clkc_data,
	},
	{ /* sentinel */ }
};

static struct platform_driver meson8_ddr_clkc_driver = {
	.probe		= meson_clkc_mmio_probe,
	.driver		= {
		.name	= "meson8-ddr-clkc",
		.of_match_table = meson8_ddr_clkc_match_table,
	},
};

builtin_platform_driver(meson8_ddr_clkc_driver);
