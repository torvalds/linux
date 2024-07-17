// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Amlogic S4 PLL Clock Controller Driver
 *
 * Copyright (c) 2022-2023 Amlogic, inc. All rights reserved
 * Author: Yu Tu <yu.tu@amlogic.com>
 */

#include <linux/clk-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mpll.h"
#include "clk-pll.h"
#include "clk-regmap.h"
#include "s4-pll.h"
#include "meson-clkc-utils.h"
#include <dt-bindings/clock/amlogic,s4-pll-clkc.h>

static DEFINE_SPINLOCK(meson_clk_lock);

/*
 * These clock are a fixed value (fixed_pll is 2GHz) that is initialized by ROMcode.
 * The chip was changed fixed pll for security reasons. Fixed PLL registers are not writable
 * in the kernel phase. Write of fixed PLL-related register will cause the system to crash.
 * Meanwhile, these clock won't ever change at runtime.
 * For the above reasons, we can only use ro_ops for fixed PLL related clocks.
 */
static struct clk_regmap s4_fixed_pll_dco = {
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
		.l = {
			.reg_off = ANACTRL_FIXPLL_CTRL0,
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
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", }
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fixed_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = ANACTRL_FIXPLL_CTRL0,
		.shift = 16,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fixed_pll",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fixed_pll_dco.hw
		},
		.num_parents = 1,
		/*
		 * This clock won't ever change at runtime so
		 * CLK_SET_RATE_PARENT is not required
		 */
	},
};

static struct clk_fixed_factor s4_fclk_div2_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div2_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor s4_fclk_div3_div = {
	.mult = 1,
	.div = 3,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div3 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 20,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div3_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor s4_fclk_div4_div = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div4 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 21,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div4_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor s4_fclk_div5_div = {
	.mult = 1,
	.div = 5,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div5 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 22,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div5_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor s4_fclk_div7_div = {
	.mult = 1,
	.div = 7,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div7 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 23,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div7_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor s4_fclk_div2p5_div = {
	.mult = 2,
	.div = 5,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2p5_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_fclk_div2p5 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_FIXPLL_CTRL1,
		.bit_idx = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2p5",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fclk_div2p5_div.hw
		},
		.num_parents = 1,
	},
};

static const struct pll_mult_range s4_gp0_pll_mult_range = {
	.min = 125,
	.max = 250,
};

/*
 * Internal gp0 pll emulation configuration parameters
 */
static const struct reg_sequence s4_gp0_init_regs[] = {
	{ .reg = ANACTRL_GP0PLL_CTRL1,	.def = 0x00000000 },
	{ .reg = ANACTRL_GP0PLL_CTRL2,	.def = 0x00000000 },
	{ .reg = ANACTRL_GP0PLL_CTRL3,	.def = 0x48681c00 },
	{ .reg = ANACTRL_GP0PLL_CTRL4,	.def = 0x88770290 },
	{ .reg = ANACTRL_GP0PLL_CTRL5,	.def = 0x39272000 },
	{ .reg = ANACTRL_GP0PLL_CTRL6,	.def = 0x56540000 }
};

static struct clk_regmap s4_gp0_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = ANACTRL_GP0PLL_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.m = {
			.reg_off = ANACTRL_GP0PLL_CTRL0,
			.shift   = 0,
			.width   = 8,
		},
		.n = {
			.reg_off = ANACTRL_GP0PLL_CTRL0,
			.shift   = 10,
			.width   = 5,
		},
		.l = {
			.reg_off = ANACTRL_GP0PLL_CTRL0,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = ANACTRL_GP0PLL_CTRL0,
			.shift   = 29,
			.width   = 1,
		},
		.range = &s4_gp0_pll_mult_range,
		.init_regs = s4_gp0_init_regs,
		.init_count = ARRAY_SIZE(s4_gp0_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "gp0_pll_dco",
		.ops = &meson_clk_pll_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", }
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_gp0_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = ANACTRL_GP0PLL_CTRL0,
		.shift = 16,
		.width = 3,
		.flags = (CLK_DIVIDER_POWER_OF_TWO |
			  CLK_DIVIDER_ROUND_CLOSEST),
	},
	.hw.init = &(struct clk_init_data){
		.name = "gp0_pll",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_gp0_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * Internal hifi pll emulation configuration parameters
 */
static const struct reg_sequence s4_hifi_init_regs[] = {
	{ .reg = ANACTRL_HIFIPLL_CTRL1,	.def = 0x00010e56 },
	{ .reg = ANACTRL_HIFIPLL_CTRL2,	.def = 0x00000000 },
	{ .reg = ANACTRL_HIFIPLL_CTRL3,	.def = 0x6a285c00 },
	{ .reg = ANACTRL_HIFIPLL_CTRL4,	.def = 0x65771290 },
	{ .reg = ANACTRL_HIFIPLL_CTRL5,	.def = 0x39272000 },
	{ .reg = ANACTRL_HIFIPLL_CTRL6,	.def = 0x56540000 }
};

static struct clk_regmap s4_hifi_pll_dco = {
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
		.l = {
			.reg_off = ANACTRL_HIFIPLL_CTRL0,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = ANACTRL_HIFIPLL_CTRL0,
			.shift   = 29,
			.width   = 1,
		},
		.range = &s4_gp0_pll_mult_range,
		.init_regs = s4_hifi_init_regs,
		.init_count = ARRAY_SIZE(s4_hifi_init_regs),
		.flags = CLK_MESON_PLL_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hifi_pll_dco",
		.ops = &meson_clk_pll_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", }
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_hifi_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = ANACTRL_HIFIPLL_CTRL0,
		.shift = 16,
		.width = 2,
		.flags = (CLK_DIVIDER_POWER_OF_TWO |
			  CLK_DIVIDER_ROUND_CLOSEST),
	},
	.hw.init = &(struct clk_init_data){
		.name = "hifi_pll",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_hifi_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap s4_hdmi_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = ANACTRL_HDMIPLL_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.m = {
			.reg_off = ANACTRL_HDMIPLL_CTRL0,
			.shift   = 0,
			.width   = 8,
		},
		.n = {
			.reg_off = ANACTRL_HDMIPLL_CTRL0,
			.shift   = 10,
			.width   = 5,
		},
		.l = {
			.reg_off = ANACTRL_HDMIPLL_CTRL0,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = ANACTRL_HDMIPLL_CTRL0,
			.shift   = 29,
			.width   = 1,
		},
		.range = &s4_gp0_pll_mult_range,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_dco",
		.ops = &meson_clk_pll_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", }
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_hdmi_pll_od = {
	.data = &(struct clk_regmap_div_data){
		.offset = ANACTRL_HDMIPLL_CTRL0,
		.shift = 16,
		.width = 4,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_od",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_hdmi_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap s4_hdmi_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = ANACTRL_HDMIPLL_CTRL0,
		.shift = 20,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_hdmi_pll_od.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor s4_mpll_50m_div = {
	.mult = 1,
	.div = 80,
	.hw.init = &(struct clk_init_data){
		.name = "mpll_50m_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fixed_pll_dco.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_mpll_50m = {
	.data = &(struct clk_regmap_mux_data){
		.offset = ANACTRL_FIXPLL_CTRL3,
		.mask = 0x1,
		.shift = 5,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll_50m",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", },
			{ .hw = &s4_mpll_50m_div.hw },
		},
		.num_parents = 2,
	},
};

static struct clk_fixed_factor s4_mpll_prediv = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "mpll_prediv",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_fixed_pll_dco.hw
		},
		.num_parents = 1,
	},
};

static const struct reg_sequence s4_mpll0_init_regs[] = {
	{ .reg = ANACTRL_MPLL_CTRL2, .def = 0x40000033 }
};

static struct clk_regmap s4_mpll0_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = ANACTRL_MPLL_CTRL1,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = ANACTRL_MPLL_CTRL1,
			.shift   = 30,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = ANACTRL_MPLL_CTRL1,
			.shift   = 20,
			.width   = 9,
		},
		.ssen = {
			.reg_off = ANACTRL_MPLL_CTRL1,
			.shift   = 29,
			.width	 = 1,
		},
		.lock = &meson_clk_lock,
		.init_regs = s4_mpll0_init_regs,
		.init_count = ARRAY_SIZE(s4_mpll0_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll0_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_mpll0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_MPLL_CTRL1,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_mpll0_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct reg_sequence s4_mpll1_init_regs[] = {
	{ .reg = ANACTRL_MPLL_CTRL4,	.def = 0x40000033 }
};

static struct clk_regmap s4_mpll1_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = ANACTRL_MPLL_CTRL3,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = ANACTRL_MPLL_CTRL3,
			.shift   = 30,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = ANACTRL_MPLL_CTRL3,
			.shift   = 20,
			.width   = 9,
		},
		.ssen = {
			.reg_off = ANACTRL_MPLL_CTRL3,
			.shift   = 29,
			.width	 = 1,
		},
		.lock = &meson_clk_lock,
		.init_regs = s4_mpll1_init_regs,
		.init_count = ARRAY_SIZE(s4_mpll1_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll1_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_mpll1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_MPLL_CTRL3,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_mpll1_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct reg_sequence s4_mpll2_init_regs[] = {
	{ .reg = ANACTRL_MPLL_CTRL6, .def = 0x40000033 }
};

static struct clk_regmap s4_mpll2_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = ANACTRL_MPLL_CTRL5,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = ANACTRL_MPLL_CTRL5,
			.shift   = 30,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = ANACTRL_MPLL_CTRL5,
			.shift   = 20,
			.width   = 9,
		},
		.ssen = {
			.reg_off = ANACTRL_MPLL_CTRL5,
			.shift   = 29,
			.width	 = 1,
		},
		.lock = &meson_clk_lock,
		.init_regs = s4_mpll2_init_regs,
		.init_count = ARRAY_SIZE(s4_mpll2_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll2_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_mpll2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_MPLL_CTRL5,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_mpll2_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct reg_sequence s4_mpll3_init_regs[] = {
	{ .reg = ANACTRL_MPLL_CTRL8, .def = 0x40000033 }
};

static struct clk_regmap s4_mpll3_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = ANACTRL_MPLL_CTRL7,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = ANACTRL_MPLL_CTRL7,
			.shift   = 30,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = ANACTRL_MPLL_CTRL7,
			.shift   = 20,
			.width   = 9,
		},
		.ssen = {
			.reg_off = ANACTRL_MPLL_CTRL7,
			.shift   = 29,
			.width	 = 1,
		},
		.lock = &meson_clk_lock,
		.init_regs = s4_mpll3_init_regs,
		.init_count = ARRAY_SIZE(s4_mpll3_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll3_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&s4_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap s4_mpll3 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = ANACTRL_MPLL_CTRL7,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll3",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &s4_mpll3_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* Array of all clocks provided by this provider */
static struct clk_hw *s4_pll_hw_clks[] = {
	[CLKID_FIXED_PLL_DCO]		= &s4_fixed_pll_dco.hw,
	[CLKID_FIXED_PLL]		= &s4_fixed_pll.hw,
	[CLKID_FCLK_DIV2_DIV]		= &s4_fclk_div2_div.hw,
	[CLKID_FCLK_DIV2]		= &s4_fclk_div2.hw,
	[CLKID_FCLK_DIV3_DIV]		= &s4_fclk_div3_div.hw,
	[CLKID_FCLK_DIV3]		= &s4_fclk_div3.hw,
	[CLKID_FCLK_DIV4_DIV]		= &s4_fclk_div4_div.hw,
	[CLKID_FCLK_DIV4]		= &s4_fclk_div4.hw,
	[CLKID_FCLK_DIV5_DIV]		= &s4_fclk_div5_div.hw,
	[CLKID_FCLK_DIV5]		= &s4_fclk_div5.hw,
	[CLKID_FCLK_DIV7_DIV]		= &s4_fclk_div7_div.hw,
	[CLKID_FCLK_DIV7]		= &s4_fclk_div7.hw,
	[CLKID_FCLK_DIV2P5_DIV]		= &s4_fclk_div2p5_div.hw,
	[CLKID_FCLK_DIV2P5]		= &s4_fclk_div2p5.hw,
	[CLKID_GP0_PLL_DCO]		= &s4_gp0_pll_dco.hw,
	[CLKID_GP0_PLL]			= &s4_gp0_pll.hw,
	[CLKID_HIFI_PLL_DCO]		= &s4_hifi_pll_dco.hw,
	[CLKID_HIFI_PLL]		= &s4_hifi_pll.hw,
	[CLKID_HDMI_PLL_DCO]		= &s4_hdmi_pll_dco.hw,
	[CLKID_HDMI_PLL_OD]		= &s4_hdmi_pll_od.hw,
	[CLKID_HDMI_PLL]		= &s4_hdmi_pll.hw,
	[CLKID_MPLL_50M_DIV]		= &s4_mpll_50m_div.hw,
	[CLKID_MPLL_50M]		= &s4_mpll_50m.hw,
	[CLKID_MPLL_PREDIV]		= &s4_mpll_prediv.hw,
	[CLKID_MPLL0_DIV]		= &s4_mpll0_div.hw,
	[CLKID_MPLL0]			= &s4_mpll0.hw,
	[CLKID_MPLL1_DIV]		= &s4_mpll1_div.hw,
	[CLKID_MPLL1]			= &s4_mpll1.hw,
	[CLKID_MPLL2_DIV]		= &s4_mpll2_div.hw,
	[CLKID_MPLL2]			= &s4_mpll2.hw,
	[CLKID_MPLL3_DIV]		= &s4_mpll3_div.hw,
	[CLKID_MPLL3]			= &s4_mpll3.hw,
};

static struct clk_regmap *const s4_pll_clk_regmaps[] = {
	&s4_fixed_pll_dco,
	&s4_fixed_pll,
	&s4_fclk_div2,
	&s4_fclk_div3,
	&s4_fclk_div4,
	&s4_fclk_div5,
	&s4_fclk_div7,
	&s4_fclk_div2p5,
	&s4_gp0_pll_dco,
	&s4_gp0_pll,
	&s4_hifi_pll_dco,
	&s4_hifi_pll,
	&s4_hdmi_pll_dco,
	&s4_hdmi_pll_od,
	&s4_hdmi_pll,
	&s4_mpll_50m,
	&s4_mpll0_div,
	&s4_mpll0,
	&s4_mpll1_div,
	&s4_mpll1,
	&s4_mpll2_div,
	&s4_mpll2,
	&s4_mpll3_div,
	&s4_mpll3,
};

static const struct reg_sequence s4_init_regs[] = {
	{ .reg = ANACTRL_MPLL_CTRL0,	.def = 0x00000543 },
};

static struct regmap_config clkc_regmap_config = {
	.reg_bits       = 32,
	.val_bits       = 32,
	.reg_stride     = 4,
	.max_register   = ANACTRL_HDMIPLL_CTRL0,
};

static struct meson_clk_hw_data s4_pll_clks = {
	.hws = s4_pll_hw_clks,
	.num = ARRAY_SIZE(s4_pll_hw_clks),
};

static int meson_s4_pll_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	void __iomem *base;
	int ret, i;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base),
				     "can't ioremap resource\n");

	regmap = devm_regmap_init_mmio(dev, base, &clkc_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "can't init regmap mmio region\n");

	ret = regmap_multi_reg_write(regmap, s4_init_regs, ARRAY_SIZE(s4_init_regs));
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to init registers\n");

	/* Populate regmap for the regmap backed clocks */
	for (i = 0; i < ARRAY_SIZE(s4_pll_clk_regmaps); i++)
		s4_pll_clk_regmaps[i]->map = regmap;

	/* Register clocks */
	for (i = 0; i < s4_pll_clks.num; i++) {
		/* array might be sparse */
		if (!s4_pll_clks.hws[i])
			continue;

		ret = devm_clk_hw_register(dev, s4_pll_clks.hws[i]);
		if (ret)
			return dev_err_probe(dev, ret,
					     "clock[%d] registration failed\n", i);
	}

	return devm_of_clk_add_hw_provider(dev, meson_clk_hw_get,
					   &s4_pll_clks);
}

static const struct of_device_id clkc_match_table[] = {
	{
		.compatible = "amlogic,s4-pll-clkc",
	},
	{}
};
MODULE_DEVICE_TABLE(of, clkc_match_table);

static struct platform_driver s4_driver = {
	.probe		= meson_s4_pll_probe,
	.driver		= {
		.name	= "s4-pll-clkc",
		.of_match_table = clkc_match_table,
	},
};

module_platform_driver(s4_driver);
MODULE_AUTHOR("Yu Tu <yu.tu@amlogic.com>");
MODULE_LICENSE("GPL");
