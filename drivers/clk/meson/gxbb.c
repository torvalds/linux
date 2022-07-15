// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 AmLogic, Inc.
 * Michael Turquette <mturquette@baylibre.com>
 */

#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/module.h>

#include "gxbb.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-mpll.h"
#include "meson-eeclk.h"
#include "vid-pll-div.h"

static DEFINE_SPINLOCK(meson_clk_lock);

static const struct pll_params_table gxbb_gp0_pll_params_table[] = {
	PLL_PARAMS(32, 1),
	PLL_PARAMS(33, 1),
	PLL_PARAMS(34, 1),
	PLL_PARAMS(35, 1),
	PLL_PARAMS(36, 1),
	PLL_PARAMS(37, 1),
	PLL_PARAMS(38, 1),
	PLL_PARAMS(39, 1),
	PLL_PARAMS(40, 1),
	PLL_PARAMS(41, 1),
	PLL_PARAMS(42, 1),
	PLL_PARAMS(43, 1),
	PLL_PARAMS(44, 1),
	PLL_PARAMS(45, 1),
	PLL_PARAMS(46, 1),
	PLL_PARAMS(47, 1),
	PLL_PARAMS(48, 1),
	PLL_PARAMS(49, 1),
	PLL_PARAMS(50, 1),
	PLL_PARAMS(51, 1),
	PLL_PARAMS(52, 1),
	PLL_PARAMS(53, 1),
	PLL_PARAMS(54, 1),
	PLL_PARAMS(55, 1),
	PLL_PARAMS(56, 1),
	PLL_PARAMS(57, 1),
	PLL_PARAMS(58, 1),
	PLL_PARAMS(59, 1),
	PLL_PARAMS(60, 1),
	PLL_PARAMS(61, 1),
	PLL_PARAMS(62, 1),
	{ /* sentinel */ },
};

static const struct pll_params_table gxl_gp0_pll_params_table[] = {
	PLL_PARAMS(42, 1),
	PLL_PARAMS(43, 1),
	PLL_PARAMS(44, 1),
	PLL_PARAMS(45, 1),
	PLL_PARAMS(46, 1),
	PLL_PARAMS(47, 1),
	PLL_PARAMS(48, 1),
	PLL_PARAMS(49, 1),
	PLL_PARAMS(50, 1),
	PLL_PARAMS(51, 1),
	PLL_PARAMS(52, 1),
	PLL_PARAMS(53, 1),
	PLL_PARAMS(54, 1),
	PLL_PARAMS(55, 1),
	PLL_PARAMS(56, 1),
	PLL_PARAMS(57, 1),
	PLL_PARAMS(58, 1),
	PLL_PARAMS(59, 1),
	PLL_PARAMS(60, 1),
	PLL_PARAMS(61, 1),
	PLL_PARAMS(62, 1),
	PLL_PARAMS(63, 1),
	PLL_PARAMS(64, 1),
	PLL_PARAMS(65, 1),
	PLL_PARAMS(66, 1),
	{ /* sentinel */ },
};

static struct clk_regmap gxbb_fixed_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 30,
			.width   = 1,
		},
		.m = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.frac = {
			.reg_off = HHI_MPLL_CNTL2,
			.shift   = 0,
			.width   = 12,
		},
		.l = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "fixed_pll_dco",
		.ops = &meson_clk_pll_ro_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_fixed_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_MPLL_CNTL,
		.shift = 16,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fixed_pll",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_fixed_pll_dco.hw
		},
		.num_parents = 1,
		/*
		 * This clock won't ever change at runtime so
		 * CLK_SET_RATE_PARENT is not required
		 */
	},
};

static struct clk_fixed_factor gxbb_hdmi_pll_pre_mult = {
	.mult = 2,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_pre_mult",
		.ops = &clk_fixed_factor_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_hdmi_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = HHI_HDMI_PLL_CNTL,
			.shift   = 30,
			.width   = 1,
		},
		.m = {
			.reg_off = HHI_HDMI_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_HDMI_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.frac = {
			.reg_off = HHI_HDMI_PLL_CNTL2,
			.shift   = 0,
			.width   = 12,
		},
		.l = {
			.reg_off = HHI_HDMI_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_HDMI_PLL_CNTL,
			.shift   = 28,
			.width   = 1,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_dco",
		.ops = &meson_clk_pll_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_hdmi_pll_pre_mult.hw
		},
		.num_parents = 1,
		/*
		 * Display directly handle hdmi pll registers ATM, we need
		 * NOCACHE to keep our view of the clock as accurate as possible
		 */
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxl_hdmi_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = HHI_HDMI_PLL_CNTL,
			.shift   = 30,
			.width   = 1,
		},
		.m = {
			.reg_off = HHI_HDMI_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_HDMI_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		/*
		 * On gxl, there is a register shift due to
		 * HHI_HDMI_PLL_CNTL1 which does not exist on gxbb,
		 * so we use the HHI_HDMI_PLL_CNTL2 define from GXBB
		 * instead which is defined at the same offset.
		 */
		.frac = {
			.reg_off = HHI_HDMI_PLL_CNTL2,
			.shift   = 0,
			.width   = 10,
		},
		.l = {
			.reg_off = HHI_HDMI_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_HDMI_PLL_CNTL,
			.shift   = 28,
			.width   = 1,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_dco",
		.ops = &meson_clk_pll_ro_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
		/*
		 * Display directly handle hdmi pll registers ATM, we need
		 * NOCACHE to keep our view of the clock as accurate as possible
		 */
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxbb_hdmi_pll_od = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_HDMI_PLL_CNTL2,
		.shift = 16,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_od",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_hdmi_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE | CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_hdmi_pll_od2 = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_HDMI_PLL_CNTL2,
		.shift = 22,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_od2",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_hdmi_pll_od.hw
		},
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE | CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_hdmi_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_HDMI_PLL_CNTL2,
		.shift = 18,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_hdmi_pll_od2.hw
		},
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE | CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxl_hdmi_pll_od = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_HDMI_PLL_CNTL + 8,
		.shift = 21,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_od",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxl_hdmi_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE | CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxl_hdmi_pll_od2 = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_HDMI_PLL_CNTL + 8,
		.shift = 23,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_od2",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxl_hdmi_pll_od.hw
		},
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE | CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxl_hdmi_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_HDMI_PLL_CNTL + 8,
		.shift = 19,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxl_hdmi_pll_od2.hw
		},
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE | CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_sys_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = HHI_SYS_PLL_CNTL,
			.shift   = 30,
			.width   = 1,
		},
		.m = {
			.reg_off = HHI_SYS_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_SYS_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.l = {
			.reg_off = HHI_SYS_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_SYS_PLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_pll_dco",
		.ops = &meson_clk_pll_ro_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_sys_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_SYS_PLL_CNTL,
		.shift = 10,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_pll",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_sys_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct reg_sequence gxbb_gp0_init_regs[] = {
	{ .reg = HHI_GP0_PLL_CNTL2,	.def = 0x69c80000 },
	{ .reg = HHI_GP0_PLL_CNTL3,	.def = 0x0a5590c4 },
	{ .reg = HHI_GP0_PLL_CNTL4,	.def = 0x0000500d },
};

static struct clk_regmap gxbb_gp0_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 30,
			.width   = 1,
		},
		.m = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.l = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
		.table = gxbb_gp0_pll_params_table,
		.init_regs = gxbb_gp0_init_regs,
		.init_count = ARRAY_SIZE(gxbb_gp0_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "gp0_pll_dco",
		.ops = &meson_clk_pll_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static const struct reg_sequence gxl_gp0_init_regs[] = {
	{ .reg = HHI_GP0_PLL_CNTL1,	.def = 0xc084b000 },
	{ .reg = HHI_GP0_PLL_CNTL2,	.def = 0xb75020be },
	{ .reg = HHI_GP0_PLL_CNTL3,	.def = 0x0a59a288 },
	{ .reg = HHI_GP0_PLL_CNTL4,	.def = 0xc000004d },
	{ .reg = HHI_GP0_PLL_CNTL5,	.def = 0x00078000 },
};

static struct clk_regmap gxl_gp0_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 30,
			.width   = 1,
		},
		.m = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.frac = {
			.reg_off = HHI_GP0_PLL_CNTL1,
			.shift   = 0,
			.width   = 10,
		},
		.l = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
		.table = gxl_gp0_pll_params_table,
		.init_regs = gxl_gp0_init_regs,
		.init_count = ARRAY_SIZE(gxl_gp0_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "gp0_pll_dco",
		.ops = &meson_clk_pll_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_gp0_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_GP0_PLL_CNTL,
		.shift = 16,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gp0_pll",
		.ops = &clk_regmap_divider_ops,
		.parent_data = &(const struct clk_parent_data) {
			/*
			 * Note:
			 * GXL and GXBB have different gp0_pll_dco (with
			 * different struct clk_hw). We fallback to the global
			 * naming string mechanism so gp0_pll picks up the
			 * appropriate one.
			 */
			.name = "gp0_pll_dco",
			.index = -1,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor gxbb_fclk_div2_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_fclk_div2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 27,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_fclk_div2_div.hw
		},
		.num_parents = 1,
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_fixed_factor gxbb_fclk_div3_div = {
	.mult = 1,
	.div = 3,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_fclk_div3 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 28,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_fclk_div3_div.hw
		},
		.num_parents = 1,
		/*
		 * FIXME:
		 * This clock, as fdiv2, is used by the SCPI FW and is required
		 * by the platform to operate correctly.
		 * Until the following condition are met, we need this clock to
		 * be marked as critical:
		 * a) The SCPI generic driver claims and enable all the clocks
		 *    it needs
		 * b) CCF has a clock hand-off mechanism to make the sure the
		 *    clock stays on until the proper driver comes along
		 */
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_fixed_factor gxbb_fclk_div4_div = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_fclk_div4 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 29,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_fclk_div4_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_fclk_div5_div = {
	.mult = 1,
	.div = 5,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_fclk_div5 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_fclk_div5_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_fclk_div7_div = {
	.mult = 1,
	.div = 7,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_fclk_div7 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_fclk_div7_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_mpll_prediv = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_MPLL_CNTL5,
		.shift = 12,
		.width = 1,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll_prediv",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_fixed_pll.hw },
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_mpll0_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = HHI_MPLL_CNTL7,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 25,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = HHI_MPLL_CNTL7,
			.shift   = 16,
			.width   = 9,
		},
		.lock = &meson_clk_lock,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll0_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap gxl_mpll0_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = HHI_MPLL_CNTL7,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = HHI_MPLL_CNTL7,
			.shift   = 15,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = HHI_MPLL_CNTL7,
			.shift   = 16,
			.width   = 9,
		},
		.lock = &meson_clk_lock,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll0_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_mpll0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL7,
		.bit_idx = 14,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll0",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			/*
			 * Note:
			 * GXL and GXBB have different SDM_EN registers. We
			 * fallback to the global naming string mechanism so
			 * mpll0_div picks up the appropriate one.
			 */
			.name = "mpll0_div",
			.index = -1,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_mpll1_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = HHI_MPLL_CNTL8,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = HHI_MPLL_CNTL8,
			.shift   = 15,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = HHI_MPLL_CNTL8,
			.shift   = 16,
			.width   = 9,
		},
		.lock = &meson_clk_lock,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll1_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_mpll1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL8,
		.bit_idx = 14,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_mpll1_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_mpll2_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = HHI_MPLL_CNTL9,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = HHI_MPLL_CNTL9,
			.shift   = 15,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = HHI_MPLL_CNTL9,
			.shift   = 16,
			.width   = 9,
		},
		.lock = &meson_clk_lock,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll2_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap gxbb_mpll2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL9,
		.bit_idx = 14,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_mpll2_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static u32 mux_table_clk81[]	= { 0, 2, 3, 4, 5, 6, 7 };
static const struct clk_parent_data clk81_parent_data[] = {
	{ .fw_name = "xtal", },
	{ .hw = &gxbb_fclk_div7.hw },
	{ .hw = &gxbb_mpll1.hw },
	{ .hw = &gxbb_mpll2.hw },
	{ .hw = &gxbb_fclk_div4.hw },
	{ .hw = &gxbb_fclk_div3.hw },
	{ .hw = &gxbb_fclk_div5.hw },
};

static struct clk_regmap gxbb_mpeg_clk_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_MPEG_CLK_CNTL,
		.mask = 0x7,
		.shift = 12,
		.table = mux_table_clk81,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpeg_clk_sel",
		.ops = &clk_regmap_mux_ro_ops,
		/*
		 * bits 14:12 selects from 8 possible parents:
		 * xtal, 1'b0 (wtf), fclk_div7, mpll_clkout1, mpll_clkout2,
		 * fclk_div4, fclk_div3, fclk_div5
		 */
		.parent_data = clk81_parent_data,
		.num_parents = ARRAY_SIZE(clk81_parent_data),
	},
};

static struct clk_regmap gxbb_mpeg_clk_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_MPEG_CLK_CNTL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpeg_clk_div",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mpeg_clk_sel.hw
		},
		.num_parents = 1,
	},
};

/* the mother of dragons gates */
static struct clk_regmap gxbb_clk81 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPEG_CLK_CNTL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "clk81",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mpeg_clk_div.hw
		},
		.num_parents = 1,
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_regmap gxbb_sar_adc_clk_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_SAR_CLK_CNTL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sar_adc_clk_sel",
		.ops = &clk_regmap_mux_ops,
		/* NOTE: The datasheet doesn't list the parents for bit 10 */
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "xtal", },
			{ .hw = &gxbb_clk81.hw },
		},
		.num_parents = 2,
	},
};

static struct clk_regmap gxbb_sar_adc_clk_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_SAR_CLK_CNTL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sar_adc_clk_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_sar_adc_clk_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_sar_adc_clk = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_SAR_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sar_adc_clk",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_sar_adc_clk_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * The MALI IP is clocked by two identical clocks (mali_0 and mali_1)
 * muxed by a glitch-free switch. The CCF can manage this glitch-free
 * mux because it does top-to-bottom updates the each clock tree and
 * switches to the "inactive" one when CLK_SET_RATE_GATE is set.
 */

static const struct clk_parent_data gxbb_mali_0_1_parent_data[] = {
	{ .fw_name = "xtal", },
	{ .hw = &gxbb_gp0_pll.hw },
	{ .hw = &gxbb_mpll2.hw },
	{ .hw = &gxbb_mpll1.hw },
	{ .hw = &gxbb_fclk_div7.hw },
	{ .hw = &gxbb_fclk_div4.hw },
	{ .hw = &gxbb_fclk_div3.hw },
	{ .hw = &gxbb_fclk_div5.hw },
};

static struct clk_regmap gxbb_mali_0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_MALI_CLK_CNTL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = gxbb_mali_0_1_parent_data,
		.num_parents = 8,
		/*
		 * Don't request the parent to change the rate because
		 * all GPU frequencies can be derived from the fclk_*
		 * clocks and one special GP0_PLL setting. This is
		 * important because we need the MPLL clocks for audio.
		 */
		.flags = 0,
	},
};

static struct clk_regmap gxbb_mali_0_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_MALI_CLK_CNTL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mali_0_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_mali_0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MALI_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mali_0_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_GATE | CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_mali_1_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_MALI_CLK_CNTL,
		.mask = 0x7,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_1_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = gxbb_mali_0_1_parent_data,
		.num_parents = 8,
		/*
		 * Don't request the parent to change the rate because
		 * all GPU frequencies can be derived from the fclk_*
		 * clocks and one special GP0_PLL setting. This is
		 * important because we need the MPLL clocks for audio.
		 */
		.flags = 0,
	},
};

static struct clk_regmap gxbb_mali_1_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_MALI_CLK_CNTL,
		.shift = 16,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_1_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mali_1_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_mali_1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MALI_CLK_CNTL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mali_1_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_GATE | CLK_SET_RATE_PARENT,
	},
};

static const struct clk_hw *gxbb_mali_parent_hws[] = {
	&gxbb_mali_0.hw,
	&gxbb_mali_1.hw,
};

static struct clk_regmap gxbb_mali = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_MALI_CLK_CNTL,
		.mask = 1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = gxbb_mali_parent_hws,
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_cts_amclk_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_AUD_CLK_CNTL,
		.mask = 0x3,
		.shift = 9,
		.table = (u32[]){ 1, 2, 3 },
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_amclk_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mpll0.hw,
			&gxbb_mpll1.hw,
			&gxbb_mpll2.hw,
		},
		.num_parents = 3,
	},
};

static struct clk_regmap gxbb_cts_amclk_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = HHI_AUD_CLK_CNTL,
		.shift = 0,
		.width = 8,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_amclk_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_cts_amclk_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_cts_amclk = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_AUD_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_amclk",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_cts_amclk_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_cts_mclk_i958_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_AUD_CLK_CNTL2,
		.mask = 0x3,
		.shift = 25,
		.table = (u32[]){ 1, 2, 3 },
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "cts_mclk_i958_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_mpll0.hw,
			&gxbb_mpll1.hw,
			&gxbb_mpll2.hw,
		},
		.num_parents = 3,
	},
};

static struct clk_regmap gxbb_cts_mclk_i958_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_AUD_CLK_CNTL2,
		.shift = 16,
		.width = 8,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "cts_mclk_i958_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_cts_mclk_i958_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_cts_mclk_i958 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_AUD_CLK_CNTL2,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_mclk_i958",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_cts_mclk_i958_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_cts_i958 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_AUD_CLK_CNTL2,
		.mask = 0x1,
		.shift = 27,
		},
	.hw.init = &(struct clk_init_data){
		.name = "cts_i958",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_cts_amclk.hw,
			&gxbb_cts_mclk_i958.hw
		},
		.num_parents = 2,
		/*
		 *The parent is specific to origin of the audio data. Let the
		 * consumer choose the appropriate parent
		 */
		.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
	},
};

static const struct clk_parent_data gxbb_32k_clk_parent_data[] = {
	{ .fw_name = "xtal", },
	/*
	 * FIXME: This clock is provided by the ao clock controller but the
	 * clock is not yet part of the binding of this controller, so string
	 * name must be use to set this parent.
	 */
	{ .name = "cts_slow_oscin", .index = -1 },
	{ .hw = &gxbb_fclk_div3.hw },
	{ .hw = &gxbb_fclk_div5.hw },
};

static struct clk_regmap gxbb_32k_clk_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_32K_CLK_CNTL,
		.mask = 0x3,
		.shift = 16,
		},
	.hw.init = &(struct clk_init_data){
		.name = "32k_clk_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = gxbb_32k_clk_parent_data,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_32k_clk_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_32K_CLK_CNTL,
		.shift = 0,
		.width = 14,
	},
	.hw.init = &(struct clk_init_data){
		.name = "32k_clk_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_32k_clk_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_DIVIDER_ROUND_CLOSEST,
	},
};

static struct clk_regmap gxbb_32k_clk = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_32K_CLK_CNTL,
		.bit_idx = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "32k_clk",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_32k_clk_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data gxbb_sd_emmc_clk0_parent_data[] = {
	{ .fw_name = "xtal", },
	{ .hw = &gxbb_fclk_div2.hw },
	{ .hw = &gxbb_fclk_div3.hw },
	{ .hw = &gxbb_fclk_div5.hw },
	{ .hw = &gxbb_fclk_div7.hw },
	/*
	 * Following these parent clocks, we should also have had mpll2, mpll3
	 * and gp0_pll but these clocks are too precious to be used here. All
	 * the necessary rates for MMC and NAND operation can be acheived using
	 * xtal or fclk_div clocks
	 */
};

/* SDIO clock */
static struct clk_regmap gxbb_sd_emmc_a_clk0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_SD_EMMC_CLK_CNTL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_a_clk0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = gxbb_sd_emmc_clk0_parent_data,
		.num_parents = ARRAY_SIZE(gxbb_sd_emmc_clk0_parent_data),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_sd_emmc_a_clk0_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_SD_EMMC_CLK_CNTL,
		.shift = 0,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_a_clk0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_sd_emmc_a_clk0_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_sd_emmc_a_clk0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_SD_EMMC_CLK_CNTL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sd_emmc_a_clk0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_sd_emmc_a_clk0_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* SDcard clock */
static struct clk_regmap gxbb_sd_emmc_b_clk0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_SD_EMMC_CLK_CNTL,
		.mask = 0x7,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_b_clk0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = gxbb_sd_emmc_clk0_parent_data,
		.num_parents = ARRAY_SIZE(gxbb_sd_emmc_clk0_parent_data),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_sd_emmc_b_clk0_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_SD_EMMC_CLK_CNTL,
		.shift = 16,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_b_clk0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_sd_emmc_b_clk0_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_sd_emmc_b_clk0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_SD_EMMC_CLK_CNTL,
		.bit_idx = 23,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sd_emmc_b_clk0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_sd_emmc_b_clk0_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* EMMC/NAND clock */
static struct clk_regmap gxbb_sd_emmc_c_clk0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_NAND_CLK_CNTL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_c_clk0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = gxbb_sd_emmc_clk0_parent_data,
		.num_parents = ARRAY_SIZE(gxbb_sd_emmc_clk0_parent_data),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_sd_emmc_c_clk0_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_NAND_CLK_CNTL,
		.shift = 0,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_c_clk0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_sd_emmc_c_clk0_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_sd_emmc_c_clk0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_NAND_CLK_CNTL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sd_emmc_c_clk0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_sd_emmc_c_clk0_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* VPU Clock */

static const struct clk_hw *gxbb_vpu_parent_hws[] = {
	&gxbb_fclk_div4.hw,
	&gxbb_fclk_div3.hw,
	&gxbb_fclk_div5.hw,
	&gxbb_fclk_div7.hw,
};

static struct clk_regmap gxbb_vpu_0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VPU_CLK_CNTL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu_0_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bits 9:10 selects from 4 possible parents:
		 * fclk_div4, fclk_div3, fclk_div5, fclk_div7,
		 */
		.parent_hws = gxbb_vpu_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_vpu_parent_hws),
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap gxbb_vpu_0_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VPU_CLK_CNTL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu_0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vpu_0_sel.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_vpu_0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VPU_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vpu_0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vpu_0_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vpu_1_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VPU_CLK_CNTL,
		.mask = 0x3,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu_1_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bits 25:26 selects from 4 possible parents:
		 * fclk_div4, fclk_div3, fclk_div5, fclk_div7,
		 */
		.parent_hws = gxbb_vpu_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_vpu_parent_hws),
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap gxbb_vpu_1_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VPU_CLK_CNTL,
		.shift = 16,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu_1_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vpu_1_sel.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_vpu_1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VPU_CLK_CNTL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vpu_1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vpu_1_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vpu = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VPU_CLK_CNTL,
		.mask = 1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bit 31 selects from 2 possible parents:
		 * vpu_0 or vpu_1
		 */
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vpu_0.hw,
			&gxbb_vpu_1.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

/* VAPB Clock */

static const struct clk_hw *gxbb_vapb_parent_hws[] = {
	&gxbb_fclk_div4.hw,
	&gxbb_fclk_div3.hw,
	&gxbb_fclk_div5.hw,
	&gxbb_fclk_div7.hw,
};

static struct clk_regmap gxbb_vapb_0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VAPBCLK_CNTL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vapb_0_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bits 9:10 selects from 4 possible parents:
		 * fclk_div4, fclk_div3, fclk_div5, fclk_div7,
		 */
		.parent_hws = gxbb_vapb_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_vapb_parent_hws),
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap gxbb_vapb_0_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VAPBCLK_CNTL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vapb_0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vapb_0_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_vapb_0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VAPBCLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vapb_0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vapb_0_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vapb_1_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VAPBCLK_CNTL,
		.mask = 0x3,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vapb_1_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bits 25:26 selects from 4 possible parents:
		 * fclk_div4, fclk_div3, fclk_div5, fclk_div7,
		 */
		.parent_hws = gxbb_vapb_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_vapb_parent_hws),
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap gxbb_vapb_1_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VAPBCLK_CNTL,
		.shift = 16,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vapb_1_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vapb_1_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_vapb_1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VAPBCLK_CNTL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vapb_1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vapb_1_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vapb_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VAPBCLK_CNTL,
		.mask = 1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vapb_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bit 31 selects from 2 possible parents:
		 * vapb_0 or vapb_1
		 */
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vapb_0.hw,
			&gxbb_vapb_1.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap gxbb_vapb = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VAPBCLK_CNTL,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vapb",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vapb_sel.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

/* Video Clocks */

static struct clk_regmap gxbb_vid_pll_div = {
	.data = &(struct meson_vid_pll_div_data){
		.val = {
			.reg_off = HHI_VID_PLL_CLK_DIV,
			.shift   = 0,
			.width   = 15,
		},
		.sel = {
			.reg_off = HHI_VID_PLL_CLK_DIV,
			.shift   = 16,
			.width   = 2,
		},
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vid_pll_div",
		.ops = &meson_vid_pll_div_ro_ops,
		.parent_data = &(const struct clk_parent_data) {
			/*
			 * Note:
			 * GXL and GXBB have different hdmi_plls (with
			 * different struct clk_hw). We fallback to the global
			 * naming string mechanism so vid_pll_div picks up the
			 * appropriate one.
			 */
			.name = "hdmi_pll",
			.index = -1,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
	},
};

static const struct clk_parent_data gxbb_vid_pll_parent_data[] = {
	{ .hw = &gxbb_vid_pll_div.hw },
	/*
	 * Note:
	 * GXL and GXBB have different hdmi_plls (with
	 * different struct clk_hw). We fallback to the global
	 * naming string mechanism so vid_pll_div picks up the
	 * appropriate one.
	 */
	{ .name = "hdmi_pll", .index = -1 },
};

static struct clk_regmap gxbb_vid_pll_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VID_PLL_CLK_DIV,
		.mask = 0x1,
		.shift = 18,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vid_pll_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bit 18 selects from 2 possible parents:
		 * vid_pll_div or hdmi_pll
		 */
		.parent_data = gxbb_vid_pll_parent_data,
		.num_parents = ARRAY_SIZE(gxbb_vid_pll_parent_data),
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxbb_vid_pll = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_PLL_CLK_DIV,
		.bit_idx = 19,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vid_pll",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vid_pll_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static const struct clk_hw *gxbb_vclk_parent_hws[] = {
	&gxbb_vid_pll.hw,
	&gxbb_fclk_div4.hw,
	&gxbb_fclk_div3.hw,
	&gxbb_fclk_div5.hw,
	&gxbb_vid_pll.hw,
	&gxbb_fclk_div7.hw,
	&gxbb_mpll1.hw,
};

static struct clk_regmap gxbb_vclk_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VID_CLK_CNTL,
		.mask = 0x7,
		.shift = 16,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bits 16:18 selects from 8 possible parents:
		 * vid_pll, fclk_div4, fclk_div3, fclk_div5,
		 * vid_pll, fclk_div7, mp1
		 */
		.parent_hws = gxbb_vclk_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_vclk_parent_hws),
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxbb_vclk2_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VIID_CLK_CNTL,
		.mask = 0x7,
		.shift = 16,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bits 16:18 selects from 8 possible parents:
		 * vid_pll, fclk_div4, fclk_div3, fclk_div5,
		 * vid_pll, fclk_div7, mp1
		 */
		.parent_hws = gxbb_vclk_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_vclk_parent_hws),
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxbb_vclk_input = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_DIV,
		.bit_idx = 16,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk_input",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk_sel.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk2_input = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_DIV,
		.bit_idx = 16,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk2_input",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk2_sel.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VID_CLK_DIV,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vclk_input.hw
		},
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxbb_vclk2_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VIID_CLK_DIV,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vclk2_input.hw
		},
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxbb_vclk = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 19,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_CNTL,
		.bit_idx = 19,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk2_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk_div1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk_div1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk_div2_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 1,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk_div2_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk_div4_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 2,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk_div4_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk_div6_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 3,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk_div6_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk_div12_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 4,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk_div12_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk2_div1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_CNTL,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk2_div1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk2.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk2_div2_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_CNTL,
		.bit_idx = 1,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk2_div2_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk2.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk2_div4_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_CNTL,
		.bit_idx = 2,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk2_div4_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk2.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk2_div6_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_CNTL,
		.bit_idx = 3,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk2_div6_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk2.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_vclk2_div12_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_CNTL,
		.bit_idx = 4,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk2_div12_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_vclk2.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_fixed_factor gxbb_vclk_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div2",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vclk_div2_en.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_vclk_div4 = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div4",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vclk_div4_en.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_vclk_div6 = {
	.mult = 1,
	.div = 6,
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div6",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vclk_div6_en.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_vclk_div12 = {
	.mult = 1,
	.div = 12,
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div12",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vclk_div12_en.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_vclk2_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div2",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vclk2_div2_en.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_vclk2_div4 = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div4",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vclk2_div4_en.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_vclk2_div6 = {
	.mult = 1,
	.div = 6,
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div6",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vclk2_div6_en.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_vclk2_div12 = {
	.mult = 1,
	.div = 12,
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div12",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vclk2_div12_en.hw
		},
		.num_parents = 1,
	},
};

static u32 mux_table_cts_sel[] = { 0, 1, 2, 3, 4, 8, 9, 10, 11, 12 };
static const struct clk_hw *gxbb_cts_parent_hws[] = {
	&gxbb_vclk_div1.hw,
	&gxbb_vclk_div2.hw,
	&gxbb_vclk_div4.hw,
	&gxbb_vclk_div6.hw,
	&gxbb_vclk_div12.hw,
	&gxbb_vclk2_div1.hw,
	&gxbb_vclk2_div2.hw,
	&gxbb_vclk2_div4.hw,
	&gxbb_vclk2_div6.hw,
	&gxbb_vclk2_div12.hw,
};

static struct clk_regmap gxbb_cts_enci_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VID_CLK_DIV,
		.mask = 0xf,
		.shift = 28,
		.table = mux_table_cts_sel,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_enci_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = gxbb_cts_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_cts_parent_hws),
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxbb_cts_encp_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VID_CLK_DIV,
		.mask = 0xf,
		.shift = 20,
		.table = mux_table_cts_sel,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_encp_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = gxbb_cts_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_cts_parent_hws),
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxbb_cts_vdac_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VIID_CLK_DIV,
		.mask = 0xf,
		.shift = 28,
		.table = mux_table_cts_sel,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_vdac_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = gxbb_cts_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_cts_parent_hws),
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_GET_RATE_NOCACHE,
	},
};

/* TOFIX: add support for cts_tcon */
static u32 mux_table_hdmi_tx_sel[] = { 0, 1, 2, 3, 4, 8, 9, 10, 11, 12 };
static const struct clk_hw *gxbb_cts_hdmi_tx_parent_hws[] = {
	&gxbb_vclk_div1.hw,
	&gxbb_vclk_div2.hw,
	&gxbb_vclk_div4.hw,
	&gxbb_vclk_div6.hw,
	&gxbb_vclk_div12.hw,
	&gxbb_vclk2_div1.hw,
	&gxbb_vclk2_div2.hw,
	&gxbb_vclk2_div4.hw,
	&gxbb_vclk2_div6.hw,
	&gxbb_vclk2_div12.hw,
};

static struct clk_regmap gxbb_hdmi_tx_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_HDMI_CLK_CNTL,
		.mask = 0xf,
		.shift = 16,
		.table = mux_table_hdmi_tx_sel,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_tx_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bits 31:28 selects from 12 possible parents:
		 * vclk_div1, vclk_div2, vclk_div4, vclk_div6, vclk_div12
		 * vclk2_div1, vclk2_div2, vclk2_div4, vclk2_div6, vclk2_div12,
		 * cts_tcon
		 */
		.parent_hws = gxbb_cts_hdmi_tx_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_cts_hdmi_tx_parent_hws),
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxbb_cts_enci = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL2,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "cts_enci",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_cts_enci_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_cts_encp = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL2,
		.bit_idx = 2,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "cts_encp",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_cts_encp_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_cts_vdac = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL2,
		.bit_idx = 4,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "cts_vdac",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_cts_vdac_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

static struct clk_regmap gxbb_hdmi_tx = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL2,
		.bit_idx = 5,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hdmi_tx",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_hdmi_tx_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

/* HDMI Clocks */

static const struct clk_parent_data gxbb_hdmi_parent_data[] = {
	{ .fw_name = "xtal", },
	{ .hw = &gxbb_fclk_div4.hw },
	{ .hw = &gxbb_fclk_div3.hw },
	{ .hw = &gxbb_fclk_div5.hw },
};

static struct clk_regmap gxbb_hdmi_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_HDMI_CLK_CNTL,
		.mask = 0x3,
		.shift = 9,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = gxbb_hdmi_parent_data,
		.num_parents = ARRAY_SIZE(gxbb_hdmi_parent_data),
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxbb_hdmi_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_HDMI_CLK_CNTL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_hdmi_sel.hw },
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_regmap gxbb_hdmi = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_HDMI_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hdmi",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &gxbb_hdmi_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
	},
};

/* VDEC clocks */

static const struct clk_hw *gxbb_vdec_parent_hws[] = {
	&gxbb_fclk_div4.hw,
	&gxbb_fclk_div3.hw,
	&gxbb_fclk_div5.hw,
	&gxbb_fclk_div7.hw,
};

static struct clk_regmap gxbb_vdec_1_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VDEC_CLK_CNTL,
		.mask = 0x3,
		.shift = 9,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_1_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = gxbb_vdec_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_vdec_parent_hws),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_vdec_1_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VDEC_CLK_CNTL,
		.shift = 0,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_1_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vdec_1_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_vdec_1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VDEC_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vdec_1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vdec_1_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_vdec_hevc_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VDEC2_CLK_CNTL,
		.mask = 0x3,
		.shift = 25,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_hevc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = gxbb_vdec_parent_hws,
		.num_parents = ARRAY_SIZE(gxbb_vdec_parent_hws),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_vdec_hevc_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VDEC2_CLK_CNTL,
		.shift = 16,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_hevc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vdec_hevc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_vdec_hevc = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VDEC2_CLK_CNTL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vdec_hevc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_vdec_hevc_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static u32 mux_table_gen_clk[]	= { 0, 4, 5, 6, 7, 8,
				    9, 10, 11, 13, 14, };
static const struct clk_parent_data gen_clk_parent_data[] = {
	{ .fw_name = "xtal", },
	{ .hw = &gxbb_vdec_1.hw },
	{ .hw = &gxbb_vdec_hevc.hw },
	{ .hw = &gxbb_mpll0.hw },
	{ .hw = &gxbb_mpll1.hw },
	{ .hw = &gxbb_mpll2.hw },
	{ .hw = &gxbb_fclk_div4.hw },
	{ .hw = &gxbb_fclk_div3.hw },
	{ .hw = &gxbb_fclk_div5.hw },
	{ .hw = &gxbb_fclk_div7.hw },
	{ .hw = &gxbb_gp0_pll.hw },
};

static struct clk_regmap gxbb_gen_clk_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_GEN_CLK_CNTL,
		.mask = 0xf,
		.shift = 12,
		.table = mux_table_gen_clk,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gen_clk_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bits 15:12 selects from 14 possible parents:
		 * xtal, [rtc_oscin_i], [sys_cpu_div16], [ddr_dpll_pt],
		 * vid_pll, vid2_pll (hevc), mpll0, mpll1, mpll2, fdiv4,
		 * fdiv3, fdiv5, [cts_msr_clk], fdiv7, gp0_pll
		 */
		.parent_data = gen_clk_parent_data,
		.num_parents = ARRAY_SIZE(gen_clk_parent_data),
	},
};

static struct clk_regmap gxbb_gen_clk_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_GEN_CLK_CNTL,
		.shift = 0,
		.width = 11,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gen_clk_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_gen_clk_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gxbb_gen_clk = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_GEN_CLK_CNTL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gen_clk",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gxbb_gen_clk_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

#define MESON_GATE(_name, _reg, _bit) \
	MESON_PCLK(_name, _reg, _bit, &gxbb_clk81.hw)

/* Everything Else (EE) domain gates */
static MESON_GATE(gxbb_ddr, HHI_GCLK_MPEG0, 0);
static MESON_GATE(gxbb_dos, HHI_GCLK_MPEG0, 1);
static MESON_GATE(gxbb_isa, HHI_GCLK_MPEG0, 5);
static MESON_GATE(gxbb_pl301, HHI_GCLK_MPEG0, 6);
static MESON_GATE(gxbb_periphs, HHI_GCLK_MPEG0, 7);
static MESON_GATE(gxbb_spicc, HHI_GCLK_MPEG0, 8);
static MESON_GATE(gxbb_i2c, HHI_GCLK_MPEG0, 9);
static MESON_GATE(gxbb_sana, HHI_GCLK_MPEG0, 10);
static MESON_GATE(gxbb_smart_card, HHI_GCLK_MPEG0, 11);
static MESON_GATE(gxbb_rng0, HHI_GCLK_MPEG0, 12);
static MESON_GATE(gxbb_uart0, HHI_GCLK_MPEG0, 13);
static MESON_GATE(gxbb_sdhc, HHI_GCLK_MPEG0, 14);
static MESON_GATE(gxbb_stream, HHI_GCLK_MPEG0, 15);
static MESON_GATE(gxbb_async_fifo, HHI_GCLK_MPEG0, 16);
static MESON_GATE(gxbb_sdio, HHI_GCLK_MPEG0, 17);
static MESON_GATE(gxbb_abuf, HHI_GCLK_MPEG0, 18);
static MESON_GATE(gxbb_hiu_iface, HHI_GCLK_MPEG0, 19);
static MESON_GATE(gxbb_assist_misc, HHI_GCLK_MPEG0, 23);
static MESON_GATE(gxbb_emmc_a, HHI_GCLK_MPEG0, 24);
static MESON_GATE(gxbb_emmc_b, HHI_GCLK_MPEG0, 25);
static MESON_GATE(gxbb_emmc_c, HHI_GCLK_MPEG0, 26);
static MESON_GATE(gxl_acodec, HHI_GCLK_MPEG0, 28);
static MESON_GATE(gxbb_spi, HHI_GCLK_MPEG0, 30);

static MESON_GATE(gxbb_i2s_spdif, HHI_GCLK_MPEG1, 2);
static MESON_GATE(gxbb_eth, HHI_GCLK_MPEG1, 3);
static MESON_GATE(gxbb_demux, HHI_GCLK_MPEG1, 4);
static MESON_GATE(gxbb_blkmv, HHI_GCLK_MPEG1, 14);
static MESON_GATE(gxbb_aiu, HHI_GCLK_MPEG1, 15);
static MESON_GATE(gxbb_uart1, HHI_GCLK_MPEG1, 16);
static MESON_GATE(gxbb_g2d, HHI_GCLK_MPEG1, 20);
static MESON_GATE(gxbb_usb0, HHI_GCLK_MPEG1, 21);
static MESON_GATE(gxbb_usb1, HHI_GCLK_MPEG1, 22);
static MESON_GATE(gxbb_reset, HHI_GCLK_MPEG1, 23);
static MESON_GATE(gxbb_nand, HHI_GCLK_MPEG1, 24);
static MESON_GATE(gxbb_dos_parser, HHI_GCLK_MPEG1, 25);
static MESON_GATE(gxbb_usb, HHI_GCLK_MPEG1, 26);
static MESON_GATE(gxbb_vdin1, HHI_GCLK_MPEG1, 28);
static MESON_GATE(gxbb_ahb_arb0, HHI_GCLK_MPEG1, 29);
static MESON_GATE(gxbb_efuse, HHI_GCLK_MPEG1, 30);
static MESON_GATE(gxbb_boot_rom, HHI_GCLK_MPEG1, 31);

static MESON_GATE(gxbb_ahb_data_bus, HHI_GCLK_MPEG2, 1);
static MESON_GATE(gxbb_ahb_ctrl_bus, HHI_GCLK_MPEG2, 2);
static MESON_GATE(gxbb_hdmi_intr_sync, HHI_GCLK_MPEG2, 3);
static MESON_GATE(gxbb_hdmi_pclk, HHI_GCLK_MPEG2, 4);
static MESON_GATE(gxbb_usb1_ddr_bridge, HHI_GCLK_MPEG2, 8);
static MESON_GATE(gxbb_usb0_ddr_bridge, HHI_GCLK_MPEG2, 9);
static MESON_GATE(gxbb_mmc_pclk, HHI_GCLK_MPEG2, 11);
static MESON_GATE(gxbb_dvin, HHI_GCLK_MPEG2, 12);
static MESON_GATE(gxbb_uart2, HHI_GCLK_MPEG2, 15);
static MESON_GATE(gxbb_sar_adc, HHI_GCLK_MPEG2, 22);
static MESON_GATE(gxbb_vpu_intr, HHI_GCLK_MPEG2, 25);
static MESON_GATE(gxbb_sec_ahb_ahb3_bridge, HHI_GCLK_MPEG2, 26);
static MESON_GATE(gxbb_clk81_a53, HHI_GCLK_MPEG2, 29);

static MESON_GATE(gxbb_vclk2_venci0, HHI_GCLK_OTHER, 1);
static MESON_GATE(gxbb_vclk2_venci1, HHI_GCLK_OTHER, 2);
static MESON_GATE(gxbb_vclk2_vencp0, HHI_GCLK_OTHER, 3);
static MESON_GATE(gxbb_vclk2_vencp1, HHI_GCLK_OTHER, 4);
static MESON_GATE(gxbb_gclk_venci_int0, HHI_GCLK_OTHER, 8);
static MESON_GATE(gxbb_gclk_vencp_int, HHI_GCLK_OTHER, 9);
static MESON_GATE(gxbb_dac_clk, HHI_GCLK_OTHER, 10);
static MESON_GATE(gxbb_aoclk_gate, HHI_GCLK_OTHER, 14);
static MESON_GATE(gxbb_iec958_gate, HHI_GCLK_OTHER, 16);
static MESON_GATE(gxbb_enc480p, HHI_GCLK_OTHER, 20);
static MESON_GATE(gxbb_rng1, HHI_GCLK_OTHER, 21);
static MESON_GATE(gxbb_gclk_venci_int1, HHI_GCLK_OTHER, 22);
static MESON_GATE(gxbb_vclk2_venclmcc, HHI_GCLK_OTHER, 24);
static MESON_GATE(gxbb_vclk2_vencl, HHI_GCLK_OTHER, 25);
static MESON_GATE(gxbb_vclk_other, HHI_GCLK_OTHER, 26);
static MESON_GATE(gxbb_edp, HHI_GCLK_OTHER, 31);

/* Always On (AO) domain gates */

static MESON_GATE(gxbb_ao_media_cpu, HHI_GCLK_AO, 0);
static MESON_GATE(gxbb_ao_ahb_sram, HHI_GCLK_AO, 1);
static MESON_GATE(gxbb_ao_ahb_bus, HHI_GCLK_AO, 2);
static MESON_GATE(gxbb_ao_iface, HHI_GCLK_AO, 3);
static MESON_GATE(gxbb_ao_i2c, HHI_GCLK_AO, 4);

/* AIU gates */
static MESON_PCLK(gxbb_aiu_glue, HHI_GCLK_MPEG1, 6, &gxbb_aiu.hw);
static MESON_PCLK(gxbb_iec958, HHI_GCLK_MPEG1, 7, &gxbb_aiu_glue.hw);
static MESON_PCLK(gxbb_i2s_out, HHI_GCLK_MPEG1, 8, &gxbb_aiu_glue.hw);
static MESON_PCLK(gxbb_amclk, HHI_GCLK_MPEG1, 9, &gxbb_aiu_glue.hw);
static MESON_PCLK(gxbb_aififo2, HHI_GCLK_MPEG1, 10, &gxbb_aiu_glue.hw);
static MESON_PCLK(gxbb_mixer, HHI_GCLK_MPEG1, 11, &gxbb_aiu_glue.hw);
static MESON_PCLK(gxbb_mixer_iface, HHI_GCLK_MPEG1, 12, &gxbb_aiu_glue.hw);
static MESON_PCLK(gxbb_adc, HHI_GCLK_MPEG1, 13, &gxbb_aiu_glue.hw);

/* Array of all clocks provided by this provider */

static struct clk_hw_onecell_data gxbb_hw_onecell_data = {
	.hws = {
		[CLKID_SYS_PLL]		    = &gxbb_sys_pll.hw,
		[CLKID_HDMI_PLL]	    = &gxbb_hdmi_pll.hw,
		[CLKID_FIXED_PLL]	    = &gxbb_fixed_pll.hw,
		[CLKID_FCLK_DIV2]	    = &gxbb_fclk_div2.hw,
		[CLKID_FCLK_DIV3]	    = &gxbb_fclk_div3.hw,
		[CLKID_FCLK_DIV4]	    = &gxbb_fclk_div4.hw,
		[CLKID_FCLK_DIV5]	    = &gxbb_fclk_div5.hw,
		[CLKID_FCLK_DIV7]	    = &gxbb_fclk_div7.hw,
		[CLKID_GP0_PLL]		    = &gxbb_gp0_pll.hw,
		[CLKID_MPEG_SEL]	    = &gxbb_mpeg_clk_sel.hw,
		[CLKID_MPEG_DIV]	    = &gxbb_mpeg_clk_div.hw,
		[CLKID_CLK81]		    = &gxbb_clk81.hw,
		[CLKID_MPLL0]		    = &gxbb_mpll0.hw,
		[CLKID_MPLL1]		    = &gxbb_mpll1.hw,
		[CLKID_MPLL2]		    = &gxbb_mpll2.hw,
		[CLKID_DDR]		    = &gxbb_ddr.hw,
		[CLKID_DOS]		    = &gxbb_dos.hw,
		[CLKID_ISA]		    = &gxbb_isa.hw,
		[CLKID_PL301]		    = &gxbb_pl301.hw,
		[CLKID_PERIPHS]		    = &gxbb_periphs.hw,
		[CLKID_SPICC]		    = &gxbb_spicc.hw,
		[CLKID_I2C]		    = &gxbb_i2c.hw,
		[CLKID_SAR_ADC]		    = &gxbb_sar_adc.hw,
		[CLKID_SMART_CARD]	    = &gxbb_smart_card.hw,
		[CLKID_RNG0]		    = &gxbb_rng0.hw,
		[CLKID_UART0]		    = &gxbb_uart0.hw,
		[CLKID_SDHC]		    = &gxbb_sdhc.hw,
		[CLKID_STREAM]		    = &gxbb_stream.hw,
		[CLKID_ASYNC_FIFO]	    = &gxbb_async_fifo.hw,
		[CLKID_SDIO]		    = &gxbb_sdio.hw,
		[CLKID_ABUF]		    = &gxbb_abuf.hw,
		[CLKID_HIU_IFACE]	    = &gxbb_hiu_iface.hw,
		[CLKID_ASSIST_MISC]	    = &gxbb_assist_misc.hw,
		[CLKID_SPI]		    = &gxbb_spi.hw,
		[CLKID_I2S_SPDIF]	    = &gxbb_i2s_spdif.hw,
		[CLKID_ETH]		    = &gxbb_eth.hw,
		[CLKID_DEMUX]		    = &gxbb_demux.hw,
		[CLKID_AIU_GLUE]	    = &gxbb_aiu_glue.hw,
		[CLKID_IEC958]		    = &gxbb_iec958.hw,
		[CLKID_I2S_OUT]		    = &gxbb_i2s_out.hw,
		[CLKID_AMCLK]		    = &gxbb_amclk.hw,
		[CLKID_AIFIFO2]		    = &gxbb_aififo2.hw,
		[CLKID_MIXER]		    = &gxbb_mixer.hw,
		[CLKID_MIXER_IFACE]	    = &gxbb_mixer_iface.hw,
		[CLKID_ADC]		    = &gxbb_adc.hw,
		[CLKID_BLKMV]		    = &gxbb_blkmv.hw,
		[CLKID_AIU]		    = &gxbb_aiu.hw,
		[CLKID_UART1]		    = &gxbb_uart1.hw,
		[CLKID_G2D]		    = &gxbb_g2d.hw,
		[CLKID_USB0]		    = &gxbb_usb0.hw,
		[CLKID_USB1]		    = &gxbb_usb1.hw,
		[CLKID_RESET]		    = &gxbb_reset.hw,
		[CLKID_NAND]		    = &gxbb_nand.hw,
		[CLKID_DOS_PARSER]	    = &gxbb_dos_parser.hw,
		[CLKID_USB]		    = &gxbb_usb.hw,
		[CLKID_VDIN1]		    = &gxbb_vdin1.hw,
		[CLKID_AHB_ARB0]	    = &gxbb_ahb_arb0.hw,
		[CLKID_EFUSE]		    = &gxbb_efuse.hw,
		[CLKID_BOOT_ROM]	    = &gxbb_boot_rom.hw,
		[CLKID_AHB_DATA_BUS]	    = &gxbb_ahb_data_bus.hw,
		[CLKID_AHB_CTRL_BUS]	    = &gxbb_ahb_ctrl_bus.hw,
		[CLKID_HDMI_INTR_SYNC]	    = &gxbb_hdmi_intr_sync.hw,
		[CLKID_HDMI_PCLK]	    = &gxbb_hdmi_pclk.hw,
		[CLKID_USB1_DDR_BRIDGE]	    = &gxbb_usb1_ddr_bridge.hw,
		[CLKID_USB0_DDR_BRIDGE]	    = &gxbb_usb0_ddr_bridge.hw,
		[CLKID_MMC_PCLK]	    = &gxbb_mmc_pclk.hw,
		[CLKID_DVIN]		    = &gxbb_dvin.hw,
		[CLKID_UART2]		    = &gxbb_uart2.hw,
		[CLKID_SANA]		    = &gxbb_sana.hw,
		[CLKID_VPU_INTR]	    = &gxbb_vpu_intr.hw,
		[CLKID_SEC_AHB_AHB3_BRIDGE] = &gxbb_sec_ahb_ahb3_bridge.hw,
		[CLKID_CLK81_A53]	    = &gxbb_clk81_a53.hw,
		[CLKID_VCLK2_VENCI0]	    = &gxbb_vclk2_venci0.hw,
		[CLKID_VCLK2_VENCI1]	    = &gxbb_vclk2_venci1.hw,
		[CLKID_VCLK2_VENCP0]	    = &gxbb_vclk2_vencp0.hw,
		[CLKID_VCLK2_VENCP1]	    = &gxbb_vclk2_vencp1.hw,
		[CLKID_GCLK_VENCI_INT0]	    = &gxbb_gclk_venci_int0.hw,
		[CLKID_GCLK_VENCI_INT]	    = &gxbb_gclk_vencp_int.hw,
		[CLKID_DAC_CLK]		    = &gxbb_dac_clk.hw,
		[CLKID_AOCLK_GATE]	    = &gxbb_aoclk_gate.hw,
		[CLKID_IEC958_GATE]	    = &gxbb_iec958_gate.hw,
		[CLKID_ENC480P]		    = &gxbb_enc480p.hw,
		[CLKID_RNG1]		    = &gxbb_rng1.hw,
		[CLKID_GCLK_VENCI_INT1]	    = &gxbb_gclk_venci_int1.hw,
		[CLKID_VCLK2_VENCLMCC]	    = &gxbb_vclk2_venclmcc.hw,
		[CLKID_VCLK2_VENCL]	    = &gxbb_vclk2_vencl.hw,
		[CLKID_VCLK_OTHER]	    = &gxbb_vclk_other.hw,
		[CLKID_EDP]		    = &gxbb_edp.hw,
		[CLKID_AO_MEDIA_CPU]	    = &gxbb_ao_media_cpu.hw,
		[CLKID_AO_AHB_SRAM]	    = &gxbb_ao_ahb_sram.hw,
		[CLKID_AO_AHB_BUS]	    = &gxbb_ao_ahb_bus.hw,
		[CLKID_AO_IFACE]	    = &gxbb_ao_iface.hw,
		[CLKID_AO_I2C]		    = &gxbb_ao_i2c.hw,
		[CLKID_SD_EMMC_A]	    = &gxbb_emmc_a.hw,
		[CLKID_SD_EMMC_B]	    = &gxbb_emmc_b.hw,
		[CLKID_SD_EMMC_C]	    = &gxbb_emmc_c.hw,
		[CLKID_SAR_ADC_CLK]	    = &gxbb_sar_adc_clk.hw,
		[CLKID_SAR_ADC_SEL]	    = &gxbb_sar_adc_clk_sel.hw,
		[CLKID_SAR_ADC_DIV]	    = &gxbb_sar_adc_clk_div.hw,
		[CLKID_MALI_0_SEL]	    = &gxbb_mali_0_sel.hw,
		[CLKID_MALI_0_DIV]	    = &gxbb_mali_0_div.hw,
		[CLKID_MALI_0]		    = &gxbb_mali_0.hw,
		[CLKID_MALI_1_SEL]	    = &gxbb_mali_1_sel.hw,
		[CLKID_MALI_1_DIV]	    = &gxbb_mali_1_div.hw,
		[CLKID_MALI_1]		    = &gxbb_mali_1.hw,
		[CLKID_MALI]		    = &gxbb_mali.hw,
		[CLKID_CTS_AMCLK]	    = &gxbb_cts_amclk.hw,
		[CLKID_CTS_AMCLK_SEL]	    = &gxbb_cts_amclk_sel.hw,
		[CLKID_CTS_AMCLK_DIV]	    = &gxbb_cts_amclk_div.hw,
		[CLKID_CTS_MCLK_I958]	    = &gxbb_cts_mclk_i958.hw,
		[CLKID_CTS_MCLK_I958_SEL]   = &gxbb_cts_mclk_i958_sel.hw,
		[CLKID_CTS_MCLK_I958_DIV]   = &gxbb_cts_mclk_i958_div.hw,
		[CLKID_CTS_I958]	    = &gxbb_cts_i958.hw,
		[CLKID_32K_CLK]		    = &gxbb_32k_clk.hw,
		[CLKID_32K_CLK_SEL]	    = &gxbb_32k_clk_sel.hw,
		[CLKID_32K_CLK_DIV]	    = &gxbb_32k_clk_div.hw,
		[CLKID_SD_EMMC_A_CLK0_SEL]  = &gxbb_sd_emmc_a_clk0_sel.hw,
		[CLKID_SD_EMMC_A_CLK0_DIV]  = &gxbb_sd_emmc_a_clk0_div.hw,
		[CLKID_SD_EMMC_A_CLK0]	    = &gxbb_sd_emmc_a_clk0.hw,
		[CLKID_SD_EMMC_B_CLK0_SEL]  = &gxbb_sd_emmc_b_clk0_sel.hw,
		[CLKID_SD_EMMC_B_CLK0_DIV]  = &gxbb_sd_emmc_b_clk0_div.hw,
		[CLKID_SD_EMMC_B_CLK0]	    = &gxbb_sd_emmc_b_clk0.hw,
		[CLKID_SD_EMMC_C_CLK0_SEL]  = &gxbb_sd_emmc_c_clk0_sel.hw,
		[CLKID_SD_EMMC_C_CLK0_DIV]  = &gxbb_sd_emmc_c_clk0_div.hw,
		[CLKID_SD_EMMC_C_CLK0]	    = &gxbb_sd_emmc_c_clk0.hw,
		[CLKID_VPU_0_SEL]	    = &gxbb_vpu_0_sel.hw,
		[CLKID_VPU_0_DIV]	    = &gxbb_vpu_0_div.hw,
		[CLKID_VPU_0]		    = &gxbb_vpu_0.hw,
		[CLKID_VPU_1_SEL]	    = &gxbb_vpu_1_sel.hw,
		[CLKID_VPU_1_DIV]	    = &gxbb_vpu_1_div.hw,
		[CLKID_VPU_1]		    = &gxbb_vpu_1.hw,
		[CLKID_VPU]		    = &gxbb_vpu.hw,
		[CLKID_VAPB_0_SEL]	    = &gxbb_vapb_0_sel.hw,
		[CLKID_VAPB_0_DIV]	    = &gxbb_vapb_0_div.hw,
		[CLKID_VAPB_0]		    = &gxbb_vapb_0.hw,
		[CLKID_VAPB_1_SEL]	    = &gxbb_vapb_1_sel.hw,
		[CLKID_VAPB_1_DIV]	    = &gxbb_vapb_1_div.hw,
		[CLKID_VAPB_1]		    = &gxbb_vapb_1.hw,
		[CLKID_VAPB_SEL]	    = &gxbb_vapb_sel.hw,
		[CLKID_VAPB]		    = &gxbb_vapb.hw,
		[CLKID_HDMI_PLL_PRE_MULT]   = &gxbb_hdmi_pll_pre_mult.hw,
		[CLKID_MPLL0_DIV]	    = &gxbb_mpll0_div.hw,
		[CLKID_MPLL1_DIV]	    = &gxbb_mpll1_div.hw,
		[CLKID_MPLL2_DIV]	    = &gxbb_mpll2_div.hw,
		[CLKID_MPLL_PREDIV]	    = &gxbb_mpll_prediv.hw,
		[CLKID_FCLK_DIV2_DIV]	    = &gxbb_fclk_div2_div.hw,
		[CLKID_FCLK_DIV3_DIV]	    = &gxbb_fclk_div3_div.hw,
		[CLKID_FCLK_DIV4_DIV]	    = &gxbb_fclk_div4_div.hw,
		[CLKID_FCLK_DIV5_DIV]	    = &gxbb_fclk_div5_div.hw,
		[CLKID_FCLK_DIV7_DIV]	    = &gxbb_fclk_div7_div.hw,
		[CLKID_VDEC_1_SEL]	    = &gxbb_vdec_1_sel.hw,
		[CLKID_VDEC_1_DIV]	    = &gxbb_vdec_1_div.hw,
		[CLKID_VDEC_1]		    = &gxbb_vdec_1.hw,
		[CLKID_VDEC_HEVC_SEL]	    = &gxbb_vdec_hevc_sel.hw,
		[CLKID_VDEC_HEVC_DIV]	    = &gxbb_vdec_hevc_div.hw,
		[CLKID_VDEC_HEVC]	    = &gxbb_vdec_hevc.hw,
		[CLKID_GEN_CLK_SEL]	    = &gxbb_gen_clk_sel.hw,
		[CLKID_GEN_CLK_DIV]	    = &gxbb_gen_clk_div.hw,
		[CLKID_GEN_CLK]		    = &gxbb_gen_clk.hw,
		[CLKID_FIXED_PLL_DCO]	    = &gxbb_fixed_pll_dco.hw,
		[CLKID_HDMI_PLL_DCO]	    = &gxbb_hdmi_pll_dco.hw,
		[CLKID_HDMI_PLL_OD]	    = &gxbb_hdmi_pll_od.hw,
		[CLKID_HDMI_PLL_OD2]	    = &gxbb_hdmi_pll_od2.hw,
		[CLKID_SYS_PLL_DCO]	    = &gxbb_sys_pll_dco.hw,
		[CLKID_GP0_PLL_DCO]	    = &gxbb_gp0_pll_dco.hw,
		[CLKID_VID_PLL_DIV]	    = &gxbb_vid_pll_div.hw,
		[CLKID_VID_PLL_SEL]	    = &gxbb_vid_pll_sel.hw,
		[CLKID_VID_PLL]		    = &gxbb_vid_pll.hw,
		[CLKID_VCLK_SEL]	    = &gxbb_vclk_sel.hw,
		[CLKID_VCLK2_SEL]	    = &gxbb_vclk2_sel.hw,
		[CLKID_VCLK_INPUT]	    = &gxbb_vclk_input.hw,
		[CLKID_VCLK2_INPUT]	    = &gxbb_vclk2_input.hw,
		[CLKID_VCLK_DIV]	    = &gxbb_vclk_div.hw,
		[CLKID_VCLK2_DIV]	    = &gxbb_vclk2_div.hw,
		[CLKID_VCLK]		    = &gxbb_vclk.hw,
		[CLKID_VCLK2]		    = &gxbb_vclk2.hw,
		[CLKID_VCLK_DIV1]	    = &gxbb_vclk_div1.hw,
		[CLKID_VCLK_DIV2_EN]	    = &gxbb_vclk_div2_en.hw,
		[CLKID_VCLK_DIV2]	    = &gxbb_vclk_div2.hw,
		[CLKID_VCLK_DIV4_EN]	    = &gxbb_vclk_div4_en.hw,
		[CLKID_VCLK_DIV4]	    = &gxbb_vclk_div4.hw,
		[CLKID_VCLK_DIV6_EN]	    = &gxbb_vclk_div6_en.hw,
		[CLKID_VCLK_DIV6]	    = &gxbb_vclk_div6.hw,
		[CLKID_VCLK_DIV12_EN]	    = &gxbb_vclk_div12_en.hw,
		[CLKID_VCLK_DIV12]	    = &gxbb_vclk_div12.hw,
		[CLKID_VCLK2_DIV1]	    = &gxbb_vclk2_div1.hw,
		[CLKID_VCLK2_DIV2_EN]	    = &gxbb_vclk2_div2_en.hw,
		[CLKID_VCLK2_DIV2]	    = &gxbb_vclk2_div2.hw,
		[CLKID_VCLK2_DIV4_EN]	    = &gxbb_vclk2_div4_en.hw,
		[CLKID_VCLK2_DIV4]	    = &gxbb_vclk2_div4.hw,
		[CLKID_VCLK2_DIV6_EN]	    = &gxbb_vclk2_div6_en.hw,
		[CLKID_VCLK2_DIV6]	    = &gxbb_vclk2_div6.hw,
		[CLKID_VCLK2_DIV12_EN]	    = &gxbb_vclk2_div12_en.hw,
		[CLKID_VCLK2_DIV12]	    = &gxbb_vclk2_div12.hw,
		[CLKID_CTS_ENCI_SEL]	    = &gxbb_cts_enci_sel.hw,
		[CLKID_CTS_ENCP_SEL]	    = &gxbb_cts_encp_sel.hw,
		[CLKID_CTS_VDAC_SEL]	    = &gxbb_cts_vdac_sel.hw,
		[CLKID_HDMI_TX_SEL]	    = &gxbb_hdmi_tx_sel.hw,
		[CLKID_CTS_ENCI]	    = &gxbb_cts_enci.hw,
		[CLKID_CTS_ENCP]	    = &gxbb_cts_encp.hw,
		[CLKID_CTS_VDAC]	    = &gxbb_cts_vdac.hw,
		[CLKID_HDMI_TX]		    = &gxbb_hdmi_tx.hw,
		[CLKID_HDMI_SEL]	    = &gxbb_hdmi_sel.hw,
		[CLKID_HDMI_DIV]	    = &gxbb_hdmi_div.hw,
		[CLKID_HDMI]		    = &gxbb_hdmi.hw,
		[NR_CLKS]		    = NULL,
	},
	.num = NR_CLKS,
};

static struct clk_hw_onecell_data gxl_hw_onecell_data = {
	.hws = {
		[CLKID_SYS_PLL]		    = &gxbb_sys_pll.hw,
		[CLKID_HDMI_PLL]	    = &gxl_hdmi_pll.hw,
		[CLKID_FIXED_PLL]	    = &gxbb_fixed_pll.hw,
		[CLKID_FCLK_DIV2]	    = &gxbb_fclk_div2.hw,
		[CLKID_FCLK_DIV3]	    = &gxbb_fclk_div3.hw,
		[CLKID_FCLK_DIV4]	    = &gxbb_fclk_div4.hw,
		[CLKID_FCLK_DIV5]	    = &gxbb_fclk_div5.hw,
		[CLKID_FCLK_DIV7]	    = &gxbb_fclk_div7.hw,
		[CLKID_GP0_PLL]		    = &gxbb_gp0_pll.hw,
		[CLKID_MPEG_SEL]	    = &gxbb_mpeg_clk_sel.hw,
		[CLKID_MPEG_DIV]	    = &gxbb_mpeg_clk_div.hw,
		[CLKID_CLK81]		    = &gxbb_clk81.hw,
		[CLKID_MPLL0]		    = &gxbb_mpll0.hw,
		[CLKID_MPLL1]		    = &gxbb_mpll1.hw,
		[CLKID_MPLL2]		    = &gxbb_mpll2.hw,
		[CLKID_DDR]		    = &gxbb_ddr.hw,
		[CLKID_DOS]		    = &gxbb_dos.hw,
		[CLKID_ISA]		    = &gxbb_isa.hw,
		[CLKID_PL301]		    = &gxbb_pl301.hw,
		[CLKID_PERIPHS]		    = &gxbb_periphs.hw,
		[CLKID_SPICC]		    = &gxbb_spicc.hw,
		[CLKID_I2C]		    = &gxbb_i2c.hw,
		[CLKID_SAR_ADC]		    = &gxbb_sar_adc.hw,
		[CLKID_SMART_CARD]	    = &gxbb_smart_card.hw,
		[CLKID_RNG0]		    = &gxbb_rng0.hw,
		[CLKID_UART0]		    = &gxbb_uart0.hw,
		[CLKID_SDHC]		    = &gxbb_sdhc.hw,
		[CLKID_STREAM]		    = &gxbb_stream.hw,
		[CLKID_ASYNC_FIFO]	    = &gxbb_async_fifo.hw,
		[CLKID_SDIO]		    = &gxbb_sdio.hw,
		[CLKID_ABUF]		    = &gxbb_abuf.hw,
		[CLKID_HIU_IFACE]	    = &gxbb_hiu_iface.hw,
		[CLKID_ASSIST_MISC]	    = &gxbb_assist_misc.hw,
		[CLKID_SPI]		    = &gxbb_spi.hw,
		[CLKID_I2S_SPDIF]	    = &gxbb_i2s_spdif.hw,
		[CLKID_ETH]		    = &gxbb_eth.hw,
		[CLKID_DEMUX]		    = &gxbb_demux.hw,
		[CLKID_AIU_GLUE]	    = &gxbb_aiu_glue.hw,
		[CLKID_IEC958]		    = &gxbb_iec958.hw,
		[CLKID_I2S_OUT]		    = &gxbb_i2s_out.hw,
		[CLKID_AMCLK]		    = &gxbb_amclk.hw,
		[CLKID_AIFIFO2]		    = &gxbb_aififo2.hw,
		[CLKID_MIXER]		    = &gxbb_mixer.hw,
		[CLKID_MIXER_IFACE]	    = &gxbb_mixer_iface.hw,
		[CLKID_ADC]		    = &gxbb_adc.hw,
		[CLKID_BLKMV]		    = &gxbb_blkmv.hw,
		[CLKID_AIU]		    = &gxbb_aiu.hw,
		[CLKID_UART1]		    = &gxbb_uart1.hw,
		[CLKID_G2D]		    = &gxbb_g2d.hw,
		[CLKID_USB0]		    = &gxbb_usb0.hw,
		[CLKID_USB1]		    = &gxbb_usb1.hw,
		[CLKID_RESET]		    = &gxbb_reset.hw,
		[CLKID_NAND]		    = &gxbb_nand.hw,
		[CLKID_DOS_PARSER]	    = &gxbb_dos_parser.hw,
		[CLKID_USB]		    = &gxbb_usb.hw,
		[CLKID_VDIN1]		    = &gxbb_vdin1.hw,
		[CLKID_AHB_ARB0]	    = &gxbb_ahb_arb0.hw,
		[CLKID_EFUSE]		    = &gxbb_efuse.hw,
		[CLKID_BOOT_ROM]	    = &gxbb_boot_rom.hw,
		[CLKID_AHB_DATA_BUS]	    = &gxbb_ahb_data_bus.hw,
		[CLKID_AHB_CTRL_BUS]	    = &gxbb_ahb_ctrl_bus.hw,
		[CLKID_HDMI_INTR_SYNC]	    = &gxbb_hdmi_intr_sync.hw,
		[CLKID_HDMI_PCLK]	    = &gxbb_hdmi_pclk.hw,
		[CLKID_USB1_DDR_BRIDGE]	    = &gxbb_usb1_ddr_bridge.hw,
		[CLKID_USB0_DDR_BRIDGE]	    = &gxbb_usb0_ddr_bridge.hw,
		[CLKID_MMC_PCLK]	    = &gxbb_mmc_pclk.hw,
		[CLKID_DVIN]		    = &gxbb_dvin.hw,
		[CLKID_UART2]		    = &gxbb_uart2.hw,
		[CLKID_SANA]		    = &gxbb_sana.hw,
		[CLKID_VPU_INTR]	    = &gxbb_vpu_intr.hw,
		[CLKID_SEC_AHB_AHB3_BRIDGE] = &gxbb_sec_ahb_ahb3_bridge.hw,
		[CLKID_CLK81_A53]	    = &gxbb_clk81_a53.hw,
		[CLKID_VCLK2_VENCI0]	    = &gxbb_vclk2_venci0.hw,
		[CLKID_VCLK2_VENCI1]	    = &gxbb_vclk2_venci1.hw,
		[CLKID_VCLK2_VENCP0]	    = &gxbb_vclk2_vencp0.hw,
		[CLKID_VCLK2_VENCP1]	    = &gxbb_vclk2_vencp1.hw,
		[CLKID_GCLK_VENCI_INT0]	    = &gxbb_gclk_venci_int0.hw,
		[CLKID_GCLK_VENCI_INT]	    = &gxbb_gclk_vencp_int.hw,
		[CLKID_DAC_CLK]		    = &gxbb_dac_clk.hw,
		[CLKID_AOCLK_GATE]	    = &gxbb_aoclk_gate.hw,
		[CLKID_IEC958_GATE]	    = &gxbb_iec958_gate.hw,
		[CLKID_ENC480P]		    = &gxbb_enc480p.hw,
		[CLKID_RNG1]		    = &gxbb_rng1.hw,
		[CLKID_GCLK_VENCI_INT1]	    = &gxbb_gclk_venci_int1.hw,
		[CLKID_VCLK2_VENCLMCC]	    = &gxbb_vclk2_venclmcc.hw,
		[CLKID_VCLK2_VENCL]	    = &gxbb_vclk2_vencl.hw,
		[CLKID_VCLK_OTHER]	    = &gxbb_vclk_other.hw,
		[CLKID_EDP]		    = &gxbb_edp.hw,
		[CLKID_AO_MEDIA_CPU]	    = &gxbb_ao_media_cpu.hw,
		[CLKID_AO_AHB_SRAM]	    = &gxbb_ao_ahb_sram.hw,
		[CLKID_AO_AHB_BUS]	    = &gxbb_ao_ahb_bus.hw,
		[CLKID_AO_IFACE]	    = &gxbb_ao_iface.hw,
		[CLKID_AO_I2C]		    = &gxbb_ao_i2c.hw,
		[CLKID_SD_EMMC_A]	    = &gxbb_emmc_a.hw,
		[CLKID_SD_EMMC_B]	    = &gxbb_emmc_b.hw,
		[CLKID_SD_EMMC_C]	    = &gxbb_emmc_c.hw,
		[CLKID_SAR_ADC_CLK]	    = &gxbb_sar_adc_clk.hw,
		[CLKID_SAR_ADC_SEL]	    = &gxbb_sar_adc_clk_sel.hw,
		[CLKID_SAR_ADC_DIV]	    = &gxbb_sar_adc_clk_div.hw,
		[CLKID_MALI_0_SEL]	    = &gxbb_mali_0_sel.hw,
		[CLKID_MALI_0_DIV]	    = &gxbb_mali_0_div.hw,
		[CLKID_MALI_0]		    = &gxbb_mali_0.hw,
		[CLKID_MALI_1_SEL]	    = &gxbb_mali_1_sel.hw,
		[CLKID_MALI_1_DIV]	    = &gxbb_mali_1_div.hw,
		[CLKID_MALI_1]		    = &gxbb_mali_1.hw,
		[CLKID_MALI]		    = &gxbb_mali.hw,
		[CLKID_CTS_AMCLK]	    = &gxbb_cts_amclk.hw,
		[CLKID_CTS_AMCLK_SEL]	    = &gxbb_cts_amclk_sel.hw,
		[CLKID_CTS_AMCLK_DIV]	    = &gxbb_cts_amclk_div.hw,
		[CLKID_CTS_MCLK_I958]	    = &gxbb_cts_mclk_i958.hw,
		[CLKID_CTS_MCLK_I958_SEL]   = &gxbb_cts_mclk_i958_sel.hw,
		[CLKID_CTS_MCLK_I958_DIV]   = &gxbb_cts_mclk_i958_div.hw,
		[CLKID_CTS_I958]	    = &gxbb_cts_i958.hw,
		[CLKID_32K_CLK]		    = &gxbb_32k_clk.hw,
		[CLKID_32K_CLK_SEL]	    = &gxbb_32k_clk_sel.hw,
		[CLKID_32K_CLK_DIV]	    = &gxbb_32k_clk_div.hw,
		[CLKID_SD_EMMC_A_CLK0_SEL]  = &gxbb_sd_emmc_a_clk0_sel.hw,
		[CLKID_SD_EMMC_A_CLK0_DIV]  = &gxbb_sd_emmc_a_clk0_div.hw,
		[CLKID_SD_EMMC_A_CLK0]	    = &gxbb_sd_emmc_a_clk0.hw,
		[CLKID_SD_EMMC_B_CLK0_SEL]  = &gxbb_sd_emmc_b_clk0_sel.hw,
		[CLKID_SD_EMMC_B_CLK0_DIV]  = &gxbb_sd_emmc_b_clk0_div.hw,
		[CLKID_SD_EMMC_B_CLK0]	    = &gxbb_sd_emmc_b_clk0.hw,
		[CLKID_SD_EMMC_C_CLK0_SEL]  = &gxbb_sd_emmc_c_clk0_sel.hw,
		[CLKID_SD_EMMC_C_CLK0_DIV]  = &gxbb_sd_emmc_c_clk0_div.hw,
		[CLKID_SD_EMMC_C_CLK0]	    = &gxbb_sd_emmc_c_clk0.hw,
		[CLKID_VPU_0_SEL]	    = &gxbb_vpu_0_sel.hw,
		[CLKID_VPU_0_DIV]	    = &gxbb_vpu_0_div.hw,
		[CLKID_VPU_0]		    = &gxbb_vpu_0.hw,
		[CLKID_VPU_1_SEL]	    = &gxbb_vpu_1_sel.hw,
		[CLKID_VPU_1_DIV]	    = &gxbb_vpu_1_div.hw,
		[CLKID_VPU_1]		    = &gxbb_vpu_1.hw,
		[CLKID_VPU]		    = &gxbb_vpu.hw,
		[CLKID_VAPB_0_SEL]	    = &gxbb_vapb_0_sel.hw,
		[CLKID_VAPB_0_DIV]	    = &gxbb_vapb_0_div.hw,
		[CLKID_VAPB_0]		    = &gxbb_vapb_0.hw,
		[CLKID_VAPB_1_SEL]	    = &gxbb_vapb_1_sel.hw,
		[CLKID_VAPB_1_DIV]	    = &gxbb_vapb_1_div.hw,
		[CLKID_VAPB_1]		    = &gxbb_vapb_1.hw,
		[CLKID_VAPB_SEL]	    = &gxbb_vapb_sel.hw,
		[CLKID_VAPB]		    = &gxbb_vapb.hw,
		[CLKID_MPLL0_DIV]	    = &gxl_mpll0_div.hw,
		[CLKID_MPLL1_DIV]	    = &gxbb_mpll1_div.hw,
		[CLKID_MPLL2_DIV]	    = &gxbb_mpll2_div.hw,
		[CLKID_MPLL_PREDIV]	    = &gxbb_mpll_prediv.hw,
		[CLKID_FCLK_DIV2_DIV]	    = &gxbb_fclk_div2_div.hw,
		[CLKID_FCLK_DIV3_DIV]	    = &gxbb_fclk_div3_div.hw,
		[CLKID_FCLK_DIV4_DIV]	    = &gxbb_fclk_div4_div.hw,
		[CLKID_FCLK_DIV5_DIV]	    = &gxbb_fclk_div5_div.hw,
		[CLKID_FCLK_DIV7_DIV]	    = &gxbb_fclk_div7_div.hw,
		[CLKID_VDEC_1_SEL]	    = &gxbb_vdec_1_sel.hw,
		[CLKID_VDEC_1_DIV]	    = &gxbb_vdec_1_div.hw,
		[CLKID_VDEC_1]		    = &gxbb_vdec_1.hw,
		[CLKID_VDEC_HEVC_SEL]	    = &gxbb_vdec_hevc_sel.hw,
		[CLKID_VDEC_HEVC_DIV]	    = &gxbb_vdec_hevc_div.hw,
		[CLKID_VDEC_HEVC]	    = &gxbb_vdec_hevc.hw,
		[CLKID_GEN_CLK_SEL]	    = &gxbb_gen_clk_sel.hw,
		[CLKID_GEN_CLK_DIV]	    = &gxbb_gen_clk_div.hw,
		[CLKID_GEN_CLK]		    = &gxbb_gen_clk.hw,
		[CLKID_FIXED_PLL_DCO]	    = &gxbb_fixed_pll_dco.hw,
		[CLKID_HDMI_PLL_DCO]	    = &gxl_hdmi_pll_dco.hw,
		[CLKID_HDMI_PLL_OD]	    = &gxl_hdmi_pll_od.hw,
		[CLKID_HDMI_PLL_OD2]	    = &gxl_hdmi_pll_od2.hw,
		[CLKID_SYS_PLL_DCO]	    = &gxbb_sys_pll_dco.hw,
		[CLKID_GP0_PLL_DCO]	    = &gxl_gp0_pll_dco.hw,
		[CLKID_VID_PLL_DIV]	    = &gxbb_vid_pll_div.hw,
		[CLKID_VID_PLL_SEL]	    = &gxbb_vid_pll_sel.hw,
		[CLKID_VID_PLL]		    = &gxbb_vid_pll.hw,
		[CLKID_VCLK_SEL]	    = &gxbb_vclk_sel.hw,
		[CLKID_VCLK2_SEL]	    = &gxbb_vclk2_sel.hw,
		[CLKID_VCLK_INPUT]	    = &gxbb_vclk_input.hw,
		[CLKID_VCLK2_INPUT]	    = &gxbb_vclk2_input.hw,
		[CLKID_VCLK_DIV]	    = &gxbb_vclk_div.hw,
		[CLKID_VCLK2_DIV]	    = &gxbb_vclk2_div.hw,
		[CLKID_VCLK]		    = &gxbb_vclk.hw,
		[CLKID_VCLK2]		    = &gxbb_vclk2.hw,
		[CLKID_VCLK_DIV1]	    = &gxbb_vclk_div1.hw,
		[CLKID_VCLK_DIV2_EN]	    = &gxbb_vclk_div2_en.hw,
		[CLKID_VCLK_DIV2]	    = &gxbb_vclk_div2.hw,
		[CLKID_VCLK_DIV4_EN]	    = &gxbb_vclk_div4_en.hw,
		[CLKID_VCLK_DIV4]	    = &gxbb_vclk_div4.hw,
		[CLKID_VCLK_DIV6_EN]	    = &gxbb_vclk_div6_en.hw,
		[CLKID_VCLK_DIV6]	    = &gxbb_vclk_div6.hw,
		[CLKID_VCLK_DIV12_EN]	    = &gxbb_vclk_div12_en.hw,
		[CLKID_VCLK_DIV12]	    = &gxbb_vclk_div12.hw,
		[CLKID_VCLK2_DIV1]	    = &gxbb_vclk2_div1.hw,
		[CLKID_VCLK2_DIV2_EN]	    = &gxbb_vclk2_div2_en.hw,
		[CLKID_VCLK2_DIV2]	    = &gxbb_vclk2_div2.hw,
		[CLKID_VCLK2_DIV4_EN]	    = &gxbb_vclk2_div4_en.hw,
		[CLKID_VCLK2_DIV4]	    = &gxbb_vclk2_div4.hw,
		[CLKID_VCLK2_DIV6_EN]	    = &gxbb_vclk2_div6_en.hw,
		[CLKID_VCLK2_DIV6]	    = &gxbb_vclk2_div6.hw,
		[CLKID_VCLK2_DIV12_EN]	    = &gxbb_vclk2_div12_en.hw,
		[CLKID_VCLK2_DIV12]	    = &gxbb_vclk2_div12.hw,
		[CLKID_CTS_ENCI_SEL]	    = &gxbb_cts_enci_sel.hw,
		[CLKID_CTS_ENCP_SEL]	    = &gxbb_cts_encp_sel.hw,
		[CLKID_CTS_VDAC_SEL]	    = &gxbb_cts_vdac_sel.hw,
		[CLKID_HDMI_TX_SEL]	    = &gxbb_hdmi_tx_sel.hw,
		[CLKID_CTS_ENCI]	    = &gxbb_cts_enci.hw,
		[CLKID_CTS_ENCP]	    = &gxbb_cts_encp.hw,
		[CLKID_CTS_VDAC]	    = &gxbb_cts_vdac.hw,
		[CLKID_HDMI_TX]		    = &gxbb_hdmi_tx.hw,
		[CLKID_HDMI_SEL]	    = &gxbb_hdmi_sel.hw,
		[CLKID_HDMI_DIV]	    = &gxbb_hdmi_div.hw,
		[CLKID_HDMI]		    = &gxbb_hdmi.hw,
		[CLKID_ACODEC]		    = &gxl_acodec.hw,
		[NR_CLKS]		    = NULL,
	},
	.num = NR_CLKS,
};

static struct clk_regmap *const gxbb_clk_regmaps[] = {
	&gxbb_clk81,
	&gxbb_ddr,
	&gxbb_dos,
	&gxbb_isa,
	&gxbb_pl301,
	&gxbb_periphs,
	&gxbb_spicc,
	&gxbb_i2c,
	&gxbb_sar_adc,
	&gxbb_smart_card,
	&gxbb_rng0,
	&gxbb_uart0,
	&gxbb_sdhc,
	&gxbb_stream,
	&gxbb_async_fifo,
	&gxbb_sdio,
	&gxbb_abuf,
	&gxbb_hiu_iface,
	&gxbb_assist_misc,
	&gxbb_spi,
	&gxbb_i2s_spdif,
	&gxbb_eth,
	&gxbb_demux,
	&gxbb_aiu_glue,
	&gxbb_iec958,
	&gxbb_i2s_out,
	&gxbb_amclk,
	&gxbb_aififo2,
	&gxbb_mixer,
	&gxbb_mixer_iface,
	&gxbb_adc,
	&gxbb_blkmv,
	&gxbb_aiu,
	&gxbb_uart1,
	&gxbb_g2d,
	&gxbb_usb0,
	&gxbb_usb1,
	&gxbb_reset,
	&gxbb_nand,
	&gxbb_dos_parser,
	&gxbb_usb,
	&gxbb_vdin1,
	&gxbb_ahb_arb0,
	&gxbb_efuse,
	&gxbb_boot_rom,
	&gxbb_ahb_data_bus,
	&gxbb_ahb_ctrl_bus,
	&gxbb_hdmi_intr_sync,
	&gxbb_hdmi_pclk,
	&gxbb_usb1_ddr_bridge,
	&gxbb_usb0_ddr_bridge,
	&gxbb_mmc_pclk,
	&gxbb_dvin,
	&gxbb_uart2,
	&gxbb_sana,
	&gxbb_vpu_intr,
	&gxbb_sec_ahb_ahb3_bridge,
	&gxbb_clk81_a53,
	&gxbb_vclk2_venci0,
	&gxbb_vclk2_venci1,
	&gxbb_vclk2_vencp0,
	&gxbb_vclk2_vencp1,
	&gxbb_gclk_venci_int0,
	&gxbb_gclk_vencp_int,
	&gxbb_dac_clk,
	&gxbb_aoclk_gate,
	&gxbb_iec958_gate,
	&gxbb_enc480p,
	&gxbb_rng1,
	&gxbb_gclk_venci_int1,
	&gxbb_vclk2_venclmcc,
	&gxbb_vclk2_vencl,
	&gxbb_vclk_other,
	&gxbb_edp,
	&gxbb_ao_media_cpu,
	&gxbb_ao_ahb_sram,
	&gxbb_ao_ahb_bus,
	&gxbb_ao_iface,
	&gxbb_ao_i2c,
	&gxbb_emmc_a,
	&gxbb_emmc_b,
	&gxbb_emmc_c,
	&gxbb_sar_adc_clk,
	&gxbb_mali_0,
	&gxbb_mali_1,
	&gxbb_cts_amclk,
	&gxbb_cts_mclk_i958,
	&gxbb_32k_clk,
	&gxbb_sd_emmc_a_clk0,
	&gxbb_sd_emmc_b_clk0,
	&gxbb_sd_emmc_c_clk0,
	&gxbb_vpu_0,
	&gxbb_vpu_1,
	&gxbb_vapb_0,
	&gxbb_vapb_1,
	&gxbb_vapb,
	&gxbb_mpeg_clk_div,
	&gxbb_sar_adc_clk_div,
	&gxbb_mali_0_div,
	&gxbb_mali_1_div,
	&gxbb_cts_mclk_i958_div,
	&gxbb_32k_clk_div,
	&gxbb_sd_emmc_a_clk0_div,
	&gxbb_sd_emmc_b_clk0_div,
	&gxbb_sd_emmc_c_clk0_div,
	&gxbb_vpu_0_div,
	&gxbb_vpu_1_div,
	&gxbb_vapb_0_div,
	&gxbb_vapb_1_div,
	&gxbb_mpeg_clk_sel,
	&gxbb_sar_adc_clk_sel,
	&gxbb_mali_0_sel,
	&gxbb_mali_1_sel,
	&gxbb_mali,
	&gxbb_cts_amclk_sel,
	&gxbb_cts_mclk_i958_sel,
	&gxbb_cts_i958,
	&gxbb_32k_clk_sel,
	&gxbb_sd_emmc_a_clk0_sel,
	&gxbb_sd_emmc_b_clk0_sel,
	&gxbb_sd_emmc_c_clk0_sel,
	&gxbb_vpu_0_sel,
	&gxbb_vpu_1_sel,
	&gxbb_vpu,
	&gxbb_vapb_0_sel,
	&gxbb_vapb_1_sel,
	&gxbb_vapb_sel,
	&gxbb_mpll0,
	&gxbb_mpll1,
	&gxbb_mpll2,
	&gxbb_mpll0_div,
	&gxbb_mpll1_div,
	&gxbb_mpll2_div,
	&gxbb_cts_amclk_div,
	&gxbb_fixed_pll,
	&gxbb_sys_pll,
	&gxbb_mpll_prediv,
	&gxbb_fclk_div2,
	&gxbb_fclk_div3,
	&gxbb_fclk_div4,
	&gxbb_fclk_div5,
	&gxbb_fclk_div7,
	&gxbb_vdec_1_sel,
	&gxbb_vdec_1_div,
	&gxbb_vdec_1,
	&gxbb_vdec_hevc_sel,
	&gxbb_vdec_hevc_div,
	&gxbb_vdec_hevc,
	&gxbb_gen_clk_sel,
	&gxbb_gen_clk_div,
	&gxbb_gen_clk,
	&gxbb_fixed_pll_dco,
	&gxbb_sys_pll_dco,
	&gxbb_gp0_pll,
	&gxbb_vid_pll,
	&gxbb_vid_pll_sel,
	&gxbb_vid_pll_div,
	&gxbb_vclk,
	&gxbb_vclk_sel,
	&gxbb_vclk_div,
	&gxbb_vclk_input,
	&gxbb_vclk_div1,
	&gxbb_vclk_div2_en,
	&gxbb_vclk_div4_en,
	&gxbb_vclk_div6_en,
	&gxbb_vclk_div12_en,
	&gxbb_vclk2,
	&gxbb_vclk2_sel,
	&gxbb_vclk2_div,
	&gxbb_vclk2_input,
	&gxbb_vclk2_div1,
	&gxbb_vclk2_div2_en,
	&gxbb_vclk2_div4_en,
	&gxbb_vclk2_div6_en,
	&gxbb_vclk2_div12_en,
	&gxbb_cts_enci,
	&gxbb_cts_enci_sel,
	&gxbb_cts_encp,
	&gxbb_cts_encp_sel,
	&gxbb_cts_vdac,
	&gxbb_cts_vdac_sel,
	&gxbb_hdmi_tx,
	&gxbb_hdmi_tx_sel,
	&gxbb_hdmi_sel,
	&gxbb_hdmi_div,
	&gxbb_hdmi,
	&gxbb_gp0_pll_dco,
	&gxbb_hdmi_pll,
	&gxbb_hdmi_pll_od,
	&gxbb_hdmi_pll_od2,
	&gxbb_hdmi_pll_dco,
};

static struct clk_regmap *const gxl_clk_regmaps[] = {
	&gxbb_clk81,
	&gxbb_ddr,
	&gxbb_dos,
	&gxbb_isa,
	&gxbb_pl301,
	&gxbb_periphs,
	&gxbb_spicc,
	&gxbb_i2c,
	&gxbb_sar_adc,
	&gxbb_smart_card,
	&gxbb_rng0,
	&gxbb_uart0,
	&gxbb_sdhc,
	&gxbb_stream,
	&gxbb_async_fifo,
	&gxbb_sdio,
	&gxbb_abuf,
	&gxbb_hiu_iface,
	&gxbb_assist_misc,
	&gxbb_spi,
	&gxbb_i2s_spdif,
	&gxbb_eth,
	&gxbb_demux,
	&gxbb_aiu_glue,
	&gxbb_iec958,
	&gxbb_i2s_out,
	&gxbb_amclk,
	&gxbb_aififo2,
	&gxbb_mixer,
	&gxbb_mixer_iface,
	&gxbb_adc,
	&gxbb_blkmv,
	&gxbb_aiu,
	&gxbb_uart1,
	&gxbb_g2d,
	&gxbb_usb0,
	&gxbb_usb1,
	&gxbb_reset,
	&gxbb_nand,
	&gxbb_dos_parser,
	&gxbb_usb,
	&gxbb_vdin1,
	&gxbb_ahb_arb0,
	&gxbb_efuse,
	&gxbb_boot_rom,
	&gxbb_ahb_data_bus,
	&gxbb_ahb_ctrl_bus,
	&gxbb_hdmi_intr_sync,
	&gxbb_hdmi_pclk,
	&gxbb_usb1_ddr_bridge,
	&gxbb_usb0_ddr_bridge,
	&gxbb_mmc_pclk,
	&gxbb_dvin,
	&gxbb_uart2,
	&gxbb_sana,
	&gxbb_vpu_intr,
	&gxbb_sec_ahb_ahb3_bridge,
	&gxbb_clk81_a53,
	&gxbb_vclk2_venci0,
	&gxbb_vclk2_venci1,
	&gxbb_vclk2_vencp0,
	&gxbb_vclk2_vencp1,
	&gxbb_gclk_venci_int0,
	&gxbb_gclk_vencp_int,
	&gxbb_dac_clk,
	&gxbb_aoclk_gate,
	&gxbb_iec958_gate,
	&gxbb_enc480p,
	&gxbb_rng1,
	&gxbb_gclk_venci_int1,
	&gxbb_vclk2_venclmcc,
	&gxbb_vclk2_vencl,
	&gxbb_vclk_other,
	&gxbb_edp,
	&gxbb_ao_media_cpu,
	&gxbb_ao_ahb_sram,
	&gxbb_ao_ahb_bus,
	&gxbb_ao_iface,
	&gxbb_ao_i2c,
	&gxbb_emmc_a,
	&gxbb_emmc_b,
	&gxbb_emmc_c,
	&gxbb_sar_adc_clk,
	&gxbb_mali_0,
	&gxbb_mali_1,
	&gxbb_cts_amclk,
	&gxbb_cts_mclk_i958,
	&gxbb_32k_clk,
	&gxbb_sd_emmc_a_clk0,
	&gxbb_sd_emmc_b_clk0,
	&gxbb_sd_emmc_c_clk0,
	&gxbb_vpu_0,
	&gxbb_vpu_1,
	&gxbb_vapb_0,
	&gxbb_vapb_1,
	&gxbb_vapb,
	&gxbb_mpeg_clk_div,
	&gxbb_sar_adc_clk_div,
	&gxbb_mali_0_div,
	&gxbb_mali_1_div,
	&gxbb_cts_mclk_i958_div,
	&gxbb_32k_clk_div,
	&gxbb_sd_emmc_a_clk0_div,
	&gxbb_sd_emmc_b_clk0_div,
	&gxbb_sd_emmc_c_clk0_div,
	&gxbb_vpu_0_div,
	&gxbb_vpu_1_div,
	&gxbb_vapb_0_div,
	&gxbb_vapb_1_div,
	&gxbb_mpeg_clk_sel,
	&gxbb_sar_adc_clk_sel,
	&gxbb_mali_0_sel,
	&gxbb_mali_1_sel,
	&gxbb_mali,
	&gxbb_cts_amclk_sel,
	&gxbb_cts_mclk_i958_sel,
	&gxbb_cts_i958,
	&gxbb_32k_clk_sel,
	&gxbb_sd_emmc_a_clk0_sel,
	&gxbb_sd_emmc_b_clk0_sel,
	&gxbb_sd_emmc_c_clk0_sel,
	&gxbb_vpu_0_sel,
	&gxbb_vpu_1_sel,
	&gxbb_vpu,
	&gxbb_vapb_0_sel,
	&gxbb_vapb_1_sel,
	&gxbb_vapb_sel,
	&gxbb_mpll0,
	&gxbb_mpll1,
	&gxbb_mpll2,
	&gxl_mpll0_div,
	&gxbb_mpll1_div,
	&gxbb_mpll2_div,
	&gxbb_cts_amclk_div,
	&gxbb_fixed_pll,
	&gxbb_sys_pll,
	&gxbb_mpll_prediv,
	&gxbb_fclk_div2,
	&gxbb_fclk_div3,
	&gxbb_fclk_div4,
	&gxbb_fclk_div5,
	&gxbb_fclk_div7,
	&gxbb_vdec_1_sel,
	&gxbb_vdec_1_div,
	&gxbb_vdec_1,
	&gxbb_vdec_hevc_sel,
	&gxbb_vdec_hevc_div,
	&gxbb_vdec_hevc,
	&gxbb_gen_clk_sel,
	&gxbb_gen_clk_div,
	&gxbb_gen_clk,
	&gxbb_fixed_pll_dco,
	&gxbb_sys_pll_dco,
	&gxbb_gp0_pll,
	&gxbb_vid_pll,
	&gxbb_vid_pll_sel,
	&gxbb_vid_pll_div,
	&gxbb_vclk,
	&gxbb_vclk_sel,
	&gxbb_vclk_div,
	&gxbb_vclk_input,
	&gxbb_vclk_div1,
	&gxbb_vclk_div2_en,
	&gxbb_vclk_div4_en,
	&gxbb_vclk_div6_en,
	&gxbb_vclk_div12_en,
	&gxbb_vclk2,
	&gxbb_vclk2_sel,
	&gxbb_vclk2_div,
	&gxbb_vclk2_input,
	&gxbb_vclk2_div1,
	&gxbb_vclk2_div2_en,
	&gxbb_vclk2_div4_en,
	&gxbb_vclk2_div6_en,
	&gxbb_vclk2_div12_en,
	&gxbb_cts_enci,
	&gxbb_cts_enci_sel,
	&gxbb_cts_encp,
	&gxbb_cts_encp_sel,
	&gxbb_cts_vdac,
	&gxbb_cts_vdac_sel,
	&gxbb_hdmi_tx,
	&gxbb_hdmi_tx_sel,
	&gxbb_hdmi_sel,
	&gxbb_hdmi_div,
	&gxbb_hdmi,
	&gxl_gp0_pll_dco,
	&gxl_hdmi_pll,
	&gxl_hdmi_pll_od,
	&gxl_hdmi_pll_od2,
	&gxl_hdmi_pll_dco,
	&gxl_acodec,
};

static const struct meson_eeclkc_data gxbb_clkc_data = {
	.regmap_clks = gxbb_clk_regmaps,
	.regmap_clk_num = ARRAY_SIZE(gxbb_clk_regmaps),
	.hw_onecell_data = &gxbb_hw_onecell_data,
};

static const struct meson_eeclkc_data gxl_clkc_data = {
	.regmap_clks = gxl_clk_regmaps,
	.regmap_clk_num = ARRAY_SIZE(gxl_clk_regmaps),
	.hw_onecell_data = &gxl_hw_onecell_data,
};

static const struct of_device_id clkc_match_table[] = {
	{ .compatible = "amlogic,gxbb-clkc", .data = &gxbb_clkc_data },
	{ .compatible = "amlogic,gxl-clkc", .data = &gxl_clkc_data },
	{},
};
MODULE_DEVICE_TABLE(of, clkc_match_table);

static struct platform_driver gxbb_driver = {
	.probe		= meson_eeclkc_probe,
	.driver		= {
		.name	= "gxbb-clkc",
		.of_match_table = clkc_match_table,
	},
};

module_platform_driver(gxbb_driver);
MODULE_LICENSE("GPL v2");
