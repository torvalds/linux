// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 * Author: Jian Hu <jian.hu@amlogic.com>
 *
 * Copyright (c) 2023, SberDevices. All Rights Reserved.
 * Author: Dmitry Rokosov <ddrokosov@sberdevices.ru>
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include "clk-pll.h"
#include "clk-regmap.h"
#include "meson-clkc-utils.h"

#define ANACTRL_FIXPLL_CTRL0	0x0
#define ANACTRL_FIXPLL_CTRL1	0x4
#define ANACTRL_FIXPLL_STS	0x14
#define ANACTRL_HIFIPLL_CTRL0	0xc0
#define ANACTRL_HIFIPLL_CTRL1	0xc4
#define ANACTRL_HIFIPLL_CTRL2	0xc8
#define ANACTRL_HIFIPLL_CTRL3	0xcc
#define ANACTRL_HIFIPLL_CTRL4	0xd0
#define ANACTRL_HIFIPLL_STS	0xd4

#include <dt-bindings/clock/amlogic,a1-pll-clkc.h>

static struct clk_regmap a1_fixed_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = ANACTRL_FIXPLL_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.m = {
			.reg_off = ANACTRL_FIXPLL_CTRL0,
			.shift   = 0,
			.width   = 8,
		},
		.n = {
			.reg_off = ANACTRL_FIXPLL_CTRL0,
			.shift   = 10,
			.width   = 5,
		},
		.frac = {
			.reg_off = ANACTRL_FIXPLL_CTRL1,
			.shift   = 0,
			.width   = 19,
		},
		.l = {
			.reg_off = ANACTRL_FIXPLL_STS,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = ANACTRL_FIXPLL_CTRL0,
			.shift   = 29,
			.width   = 1,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "fixed_pll_dco",
		.ops = &meson_clk_pll_ro_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "fixpll_in",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_fixed_pll = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL0,
		.bit_idx = 20,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "fixed_pll",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_fixed_pll_dco.hw
		},
		.num_parents = 1,
	},
};

static const struct pll_mult_range a1_hifi_pll_range = {
	.min = 32,
	.max = 64,
};

static const struct reg_sequence a1_hifi_pll_init_regs[] = {
	{ .reg = ANACTRL_HIFIPLL_CTRL1, .def = 0x01800000 },
	{ .reg = ANACTRL_HIFIPLL_CTRL2, .def = 0x00001100 },
	{ .reg = ANACTRL_HIFIPLL_CTRL3, .def = 0x100a1100 },
	{ .reg = ANACTRL_HIFIPLL_CTRL4, .def = 0x00302000 },
	{ .reg = ANACTRL_HIFIPLL_CTRL0, .def = 0x01f18000 },
};

static struct clk_regmap a1_hifi_pll = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = ANACTRL_HIFIPLL_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.m = {
			.reg_off = ANACTRL_HIFIPLL_CTRL0,
			.shift   = 0,
			.width   = 8,
		},
		.n = {
			.reg_off = ANACTRL_HIFIPLL_CTRL0,
			.shift   = 10,
			.width   = 5,
		},
		.frac = {
			.reg_off = ANACTRL_HIFIPLL_CTRL1,
			.shift   = 0,
			.width   = 19,
		},
		.l = {
			.reg_off = ANACTRL_HIFIPLL_STS,
			.shift   = 31,
			.width   = 1,
		},
		.current_en = {
			.reg_off = ANACTRL_HIFIPLL_CTRL0,
			.shift   = 26,
			.width   = 1,
		},
		.l_detect = {
			.reg_off = ANACTRL_HIFIPLL_CTRL2,
			.shift   = 6,
			.width   = 1,
		},
		.range = &a1_hifi_pll_range,
		.init_regs = a1_hifi_pll_init_regs,
		.init_count = ARRAY_SIZE(a1_hifi_pll_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "hifi_pll",
		.ops = &meson_clk_pll_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "hifipll_in",
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor a1_fclk_div2_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_fclk_div2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL0,
		.bit_idx = 21,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_fclk_div2_div.hw
		},
		.num_parents = 1,
		/*
		 * This clock is used by DDR clock in BL2 firmware
		 * and is required by the platform to operate correctly.
		 * Until the following condition are met, we need this clock to
		 * be marked as critical:
		 * a) Mark the clock used by a firmware resource, if possible
		 * b) CCF has a clock hand-off mechanism to make the sure the
		 *    clock stays on until the proper driver comes along
		 */
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_fixed_factor a1_fclk_div3_div = {
	.mult = 1,
	.div = 3,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_fclk_div3 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL0,
		.bit_idx = 22,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_fclk_div3_div.hw
		},
		.num_parents = 1,
		/*
		 * This clock is used by APB bus which is set in boot ROM code
		 * and is required by the platform to operate correctly.
		 */
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_fixed_factor a1_fclk_div5_div = {
	.mult = 1,
	.div = 5,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_fclk_div5 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL0,
		.bit_idx = 23,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_fclk_div5_div.hw
		},
		.num_parents = 1,
		/*
		 * This clock is used by AXI bus which setted in Romcode
		 * and is required by the platform to operate correctly.
		 */
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_fixed_factor a1_fclk_div7_div = {
	.mult = 1,
	.div = 7,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a1_fclk_div7 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL0,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a1_fclk_div7_div.hw
		},
		.num_parents = 1,
	},
};

/* Array of all clocks registered by this provider */
static struct clk_hw *a1_pll_hw_clks[] = {
	[CLKID_FIXED_PLL_DCO]	= &a1_fixed_pll_dco.hw,
	[CLKID_FIXED_PLL]	= &a1_fixed_pll.hw,
	[CLKID_FCLK_DIV2_DIV]	= &a1_fclk_div2_div.hw,
	[CLKID_FCLK_DIV3_DIV]	= &a1_fclk_div3_div.hw,
	[CLKID_FCLK_DIV5_DIV]	= &a1_fclk_div5_div.hw,
	[CLKID_FCLK_DIV7_DIV]	= &a1_fclk_div7_div.hw,
	[CLKID_FCLK_DIV2]	= &a1_fclk_div2.hw,
	[CLKID_FCLK_DIV3]	= &a1_fclk_div3.hw,
	[CLKID_FCLK_DIV5]	= &a1_fclk_div5.hw,
	[CLKID_FCLK_DIV7]	= &a1_fclk_div7.hw,
	[CLKID_HIFI_PLL]	= &a1_hifi_pll.hw,
};

static const struct meson_clkc_data a1_pll_clkc_data = {
	.hw_clks = {
		.hws = a1_pll_hw_clks,
		.num = ARRAY_SIZE(a1_pll_hw_clks),
	},
};

static const struct of_device_id a1_pll_clkc_match_table[] = {
	{
		.compatible = "amlogic,a1-pll-clkc",
		.data = &a1_pll_clkc_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, a1_pll_clkc_match_table);

static struct platform_driver a1_pll_clkc_driver = {
	.probe = meson_clkc_mmio_probe,
	.driver = {
		.name = "a1-pll-clkc",
		.of_match_table = a1_pll_clkc_match_table,
	},
};
module_platform_driver(a1_pll_clkc_driver);

MODULE_DESCRIPTION("Amlogic A1 PLL Clock Controller driver");
MODULE_AUTHOR("Jian Hu <jian.hu@amlogic.com>");
MODULE_AUTHOR("Dmitry Rokosov <ddrokosov@sberdevices.ru>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
