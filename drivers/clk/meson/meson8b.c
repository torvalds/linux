// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * Copyright (c) 2016 BayLibre, Inc.
 * Michael Turquette <mturquette@baylibre.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "clk-regmap.h"
#include "meson-clkc-utils.h"
#include "clk-pll.h"
#include "clk-mpll.h"

#include <dt-bindings/clock/meson8b-clkc.h>
#include <dt-bindings/reset/amlogic,meson8b-clkc-reset.h>

/*
 * Clock controller register offsets
 *
 * Register offsets from the HardKernel[0] data sheet must be multiplied
 * by 4 before adding them to the base address to get the right value
 *
 * [0] https://dn.odroid.com/S805/Datasheet/S805_Datasheet%20V0.8%2020150126.pdf
 */
#define HHI_GP_PLL_CNTL			0x40
#define HHI_GP_PLL_CNTL2		0x44
#define HHI_GP_PLL_CNTL3		0x48
#define HHI_GP_PLL_CNTL4		0x4C
#define HHI_GP_PLL_CNTL5		0x50
#define HHI_VIID_CLK_DIV		0x128
#define HHI_VIID_CLK_CNTL		0x12c
#define HHI_GCLK_MPEG0			0x140
#define HHI_GCLK_MPEG1			0x144
#define HHI_GCLK_MPEG2			0x148
#define HHI_GCLK_OTHER			0x150
#define HHI_GCLK_AO			0x154
#define HHI_SYS_CPU_CLK_CNTL1		0x15c
#define HHI_VID_CLK_DIV			0x164
#define HHI_MPEG_CLK_CNTL		0x174
#define HHI_AUD_CLK_CNTL		0x178
#define HHI_VID_CLK_CNTL		0x17c
#define HHI_AUD_CLK_CNTL2		0x190
#define HHI_VID_CLK_CNTL2		0x194
#define HHI_VID_DIVIDER_CNTL		0x198
#define HHI_SYS_CPU_CLK_CNTL0		0x19c
#define HHI_MALI_CLK_CNTL		0x1b0
#define HHI_VPU_CLK_CNTL		0x1bc
#define HHI_HDMI_CLK_CNTL		0x1cc
#define HHI_VDEC_CLK_CNTL		0x1e0
#define HHI_VDEC2_CLK_CNTL		0x1e4
#define HHI_VDEC3_CLK_CNTL		0x1e8
#define HHI_NAND_CLK_CNTL		0x25c
#define HHI_MPLL_CNTL			0x280
#define HHI_SYS_PLL_CNTL		0x300
#define HHI_VID_PLL_CNTL		0x320
#define HHI_VID_PLL_CNTL2		0x324
#define HHI_VID_PLL_CNTL3		0x328
#define HHI_VID_PLL_CNTL4		0x32c
#define HHI_VID_PLL_CNTL5		0x330
#define HHI_VID_PLL_CNTL6		0x334
#define HHI_VID2_PLL_CNTL		0x380
#define HHI_VID2_PLL_CNTL2		0x384
#define HHI_VID2_PLL_CNTL3		0x388
#define HHI_VID2_PLL_CNTL4		0x38c
#define HHI_VID2_PLL_CNTL5		0x390
#define HHI_VID2_PLL_CNTL6		0x394

/*
 * MPLL register offeset taken from the S905 datasheet. Vendor kernel source
 * confirm these are the same for the S805.
 */
#define HHI_MPLL_CNTL			0x280
#define HHI_MPLL_CNTL2			0x284
#define HHI_MPLL_CNTL3			0x288
#define HHI_MPLL_CNTL4			0x28c
#define HHI_MPLL_CNTL5			0x290
#define HHI_MPLL_CNTL6			0x294
#define HHI_MPLL_CNTL7			0x298
#define HHI_MPLL_CNTL8			0x29c
#define HHI_MPLL_CNTL9			0x2a0
#define HHI_MPLL_CNTL10			0x2a4

struct meson8b_clk_reset {
	struct reset_controller_dev reset;
	struct regmap *regmap;
};

static const struct pll_params_table sys_pll_params_table[] = {
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
	PLL_PARAMS(67, 1),
	PLL_PARAMS(68, 1),
	PLL_PARAMS(84, 1),
	{ /* sentinel */ },
};

static struct clk_regmap meson8b_fixed_pll_dco = {
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
			.name = "xtal",
			.index = -1,
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_fixed_pll = {
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
			&meson8b_fixed_pll_dco.hw
		},
		.num_parents = 1,
		/*
		 * This clock won't ever change at runtime so
		 * CLK_SET_RATE_PARENT is not required
		 */
	},
};

static struct clk_fixed_factor hdmi_pll_dco_in = {
	.mult = 2,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_dco_in",
		.ops = &clk_fixed_factor_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
			.index = -1,
		},
		.num_parents = 1,
	},
};

/*
 * Taken from the vendor driver for the 2970/2975MHz (both only differ in the
 * FRAC part in HHI_VID_PLL_CNTL2) where these values are identical for Meson8,
 * Meson8b and Meson8m2. This doubles the input (or output - it's not clear
 * which one but the result is the same) clock. The vendor driver additionally
 * has the following comment about: "optimise HPLL VCO 2.97GHz performance".
 */
static const struct reg_sequence meson8b_hdmi_pll_init_regs[] = {
	{ .reg = HHI_VID_PLL_CNTL2,	.def = 0x69c84000 },
	{ .reg = HHI_VID_PLL_CNTL3,	.def = 0x8a46c023 },
	{ .reg = HHI_VID_PLL_CNTL4,	.def = 0x4123b100 },
	{ .reg = HHI_VID_PLL_CNTL5,	.def = 0x00012385 },
	{ .reg = HHI_VID2_PLL_CNTL2,	.def = 0x0430a800 },
};

static const struct pll_params_table meson8b_hdmi_pll_params_table[] = {
	PLL_PARAMS(40, 1),
	PLL_PARAMS(42, 1),
	PLL_PARAMS(44, 1),
	PLL_PARAMS(45, 1),
	PLL_PARAMS(49, 1),
	PLL_PARAMS(52, 1),
	PLL_PARAMS(54, 1),
	PLL_PARAMS(56, 1),
	PLL_PARAMS(59, 1),
	PLL_PARAMS(60, 1),
	PLL_PARAMS(61, 1),
	PLL_PARAMS(62, 1),
	PLL_PARAMS(64, 1),
	PLL_PARAMS(66, 1),
	PLL_PARAMS(68, 1),
	PLL_PARAMS(71, 1),
	PLL_PARAMS(82, 1),
	{ /* sentinel */ }
};

static struct clk_regmap meson8b_hdmi_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = HHI_VID_PLL_CNTL,
			.shift   = 30,
			.width   = 1,
		},
		.m = {
			.reg_off = HHI_VID_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_VID_PLL_CNTL,
			.shift   = 10,
			.width   = 5,
		},
		.frac = {
			.reg_off = HHI_VID_PLL_CNTL2,
			.shift   = 0,
			.width   = 12,
		},
		.l = {
			.reg_off = HHI_VID_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_VID_PLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
		.table = meson8b_hdmi_pll_params_table,
		.init_regs = meson8b_hdmi_pll_init_regs,
		.init_count = ARRAY_SIZE(meson8b_hdmi_pll_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		/* sometimes also called "HPLL" or "HPLL PLL" */
		.name = "hdmi_pll_dco",
		.ops = &meson_clk_pll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&hdmi_pll_dco_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_hdmi_pll_lvds_out = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VID_PLL_CNTL,
		.shift = 16,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_lvds_out",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_hdmi_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_hdmi_pll_hdmi_out = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VID_PLL_CNTL,
		.shift = 18,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll_hdmi_out",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_hdmi_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_sys_pll_dco = {
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
		.table = sys_pll_params_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_pll_dco",
		.ops = &meson_clk_pll_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
			.name = "xtal",
			.index = -1,
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_sys_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_SYS_PLL_CNTL,
		.shift = 16,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_pll",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_sys_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor meson8b_fclk_div2_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_fclk_div2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 27,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fclk_div2_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_fclk_div3_div = {
	.mult = 1,
	.div = 3,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_fclk_div3 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 28,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fclk_div3_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_fclk_div4_div = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_fclk_div4 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 29,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fclk_div4_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_fclk_div5_div = {
	.mult = 1,
	.div = 5,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_fclk_div5 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fclk_div5_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_fclk_div7_div = {
	.mult = 1,
	.div = 7,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_fclk_div7 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fclk_div7_div.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_mpll_prediv = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_MPLL_CNTL5,
		.shift = 12,
		.width = 1,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll_prediv",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fixed_pll.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_mpll0_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = HHI_MPLL_CNTL7,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = HHI_MPLL_CNTL7,
			.shift   = 15,
			.width   = 1,
		},
		.n2 = {
			.reg_off = HHI_MPLL_CNTL7,
			.shift   = 16,
			.width   = 9,
		},
		.ssen = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 25,
			.width   = 1,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll0_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_mpll0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL7,
		.bit_idx = 14,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_mpll0_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_mpll1_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = HHI_MPLL_CNTL8,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = HHI_MPLL_CNTL8,
			.shift   = 15,
			.width   = 1,
		},
		.n2 = {
			.reg_off = HHI_MPLL_CNTL8,
			.shift   = 16,
			.width   = 9,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll1_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_mpll1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL8,
		.bit_idx = 14,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_mpll1_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_mpll2_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = HHI_MPLL_CNTL9,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = HHI_MPLL_CNTL9,
			.shift   = 15,
			.width   = 1,
		},
		.n2 = {
			.reg_off = HHI_MPLL_CNTL9,
			.shift   = 16,
			.width   = 9,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll2_div",
		.ops = &meson_clk_mpll_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_mpll_prediv.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_mpll2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL9,
		.bit_idx = 14,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_mpll2_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* clk81 is often referred as "mpeg_clk" */
static u32 meson8b_clk81_parents_val_table[] = { 6, 5, 7 };
static struct clk_regmap meson8b_clk81_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_MPEG_CLK_CNTL,
		.mask = 0x7,
		.shift = 12,
		.table = meson8b_clk81_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "clk81_sel",
		.ops = &clk_regmap_mux_ro_ops,
		/*
		 * FIXME bits 14:12 selects from 8 possible parents:
		 * xtal, 1'b0 (wtf), fclk_div7, mpll_clkout1, mpll_clkout2,
		 * fclk_div4, fclk_div3, fclk_div5
		 */
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_fclk_div3.hw,
			&meson8b_fclk_div4.hw,
			&meson8b_fclk_div5.hw,
		},
		.num_parents = 3,
	},
};

static struct clk_regmap meson8b_clk81_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_MPEG_CLK_CNTL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "clk81_div",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_clk81_sel.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8b_clk81 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPEG_CLK_CNTL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "clk81",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_clk81_div.hw
		},
		.num_parents = 1,
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_regmap meson8b_cpu_in_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_SYS_CPU_CLK_CNTL0,
		.mask = 0x1,
		.shift = 0,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cpu_in_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data[]) {
			{ .fw_name = "xtal", .name = "xtal", .index = -1, },
			{ .hw = &meson8b_sys_pll.hw, },
		},
		.num_parents = 2,
		.flags = (CLK_SET_RATE_PARENT |
			  CLK_SET_RATE_NO_REPARENT),
	},
};

static struct clk_fixed_factor meson8b_cpu_in_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "cpu_in_div2",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cpu_in_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor meson8b_cpu_in_div3 = {
	.mult = 1,
	.div = 3,
	.hw.init = &(struct clk_init_data){
		.name = "cpu_in_div3",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cpu_in_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_div_table meson8b_cpu_scale_div_table[] = {
	{ .val = 1, .div = 4 },
	{ .val = 2, .div = 6 },
	{ .val = 3, .div = 8 },
	{ .val = 4, .div = 10 },
	{ .val = 5, .div = 12 },
	{ .val = 6, .div = 14 },
	{ .val = 7, .div = 16 },
	{ .val = 8, .div = 18 },
	{ /* sentinel */ },
};

static struct clk_regmap meson8b_cpu_scale_div = {
	.data = &(struct clk_regmap_div_data){
		.offset =  HHI_SYS_CPU_CLK_CNTL1,
		.shift = 20,
		.width = 10,
		.table = meson8b_cpu_scale_div_table,
		.flags = CLK_DIVIDER_ALLOW_ZERO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cpu_scale_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cpu_in_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static u32 meson8b_cpu_scale_out_parents_val_table[] = { 0, 1, 3 };
static struct clk_regmap meson8b_cpu_scale_out_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_SYS_CPU_CLK_CNTL0,
		.mask = 0x3,
		.shift = 2,
		.table = meson8b_cpu_scale_out_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cpu_scale_out_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * NOTE: We are skipping the parent with value 0x2 (which is
		 * meson8b_cpu_in_div3) because it results in a duty cycle of
		 * 33% which makes the system unstable and can result in a
		 * lockup of the whole system.
		 */
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cpu_in_sel.hw,
			&meson8b_cpu_in_div2.hw,
			&meson8b_cpu_scale_div.hw,
		},
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cpu_clk = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_SYS_CPU_CLK_CNTL0,
		.mask = 0x1,
		.shift = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cpu_clk",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data[]) {
			{ .fw_name = "xtal", .name = "xtal", .index = -1, },
			{ .hw = &meson8b_cpu_scale_out_sel.hw, },
		},
		.num_parents = 2,
		.flags = (CLK_SET_RATE_PARENT |
			  CLK_SET_RATE_NO_REPARENT |
			  CLK_IS_CRITICAL),
	},
};

static struct clk_regmap meson8b_nand_clk_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_NAND_CLK_CNTL,
		.mask = 0x7,
		.shift = 9,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "nand_clk_sel",
		.ops = &clk_regmap_mux_ops,
		/* FIXME all other parents are unknown: */
		.parent_data = (const struct clk_parent_data[]) {
			{ .hw = &meson8b_fclk_div4.hw, },
			{ .hw = &meson8b_fclk_div3.hw, },
			{ .hw = &meson8b_fclk_div5.hw, },
			{ .hw = &meson8b_fclk_div7.hw, },
			{ .fw_name = "xtal", .name = "xtal", .index = -1, },
		},
		.num_parents = 5,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_nand_clk_div = {
	.data = &(struct clk_regmap_div_data){
		.offset =  HHI_NAND_CLK_CNTL,
		.shift = 0,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "nand_clk_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_nand_clk_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_nand_clk = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_NAND_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "nand_clk",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_nand_clk_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor meson8b_cpu_clk_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "cpu_clk_div2",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cpu_clk.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_cpu_clk_div3 = {
	.mult = 1,
	.div = 3,
	.hw.init = &(struct clk_init_data){
		.name = "cpu_clk_div3",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cpu_clk.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_cpu_clk_div4 = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "cpu_clk_div4",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cpu_clk.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_cpu_clk_div5 = {
	.mult = 1,
	.div = 5,
	.hw.init = &(struct clk_init_data){
		.name = "cpu_clk_div5",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cpu_clk.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_cpu_clk_div6 = {
	.mult = 1,
	.div = 6,
	.hw.init = &(struct clk_init_data){
		.name = "cpu_clk_div6",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cpu_clk.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_cpu_clk_div7 = {
	.mult = 1,
	.div = 7,
	.hw.init = &(struct clk_init_data){
		.name = "cpu_clk_div7",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cpu_clk.hw
		},
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_cpu_clk_div8 = {
	.mult = 1,
	.div = 8,
	.hw.init = &(struct clk_init_data){
		.name = "cpu_clk_div8",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cpu_clk.hw
		},
		.num_parents = 1,
	},
};

static u32 meson8b_cpu_if_parents_val_table[] = { 1, 2, 3, 4, 5, 6, 7 };
static const struct clk_hw *meson8b_cpu_if_parents[] = {
	&meson8b_cpu_clk_div2.hw,
	&meson8b_cpu_clk_div3.hw,
	&meson8b_cpu_clk_div4.hw,
	&meson8b_cpu_clk_div5.hw,
	&meson8b_cpu_clk_div6.hw,
	&meson8b_cpu_clk_div7.hw,
	&meson8b_cpu_clk_div8.hw,
};

static struct clk_regmap meson8b_apb_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_SYS_CPU_CLK_CNTL1,
		.mask = 0x7,
		.shift = 3,
		.table = meson8b_cpu_if_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "apb_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_cpu_if_parents,
		.num_parents = ARRAY_SIZE(meson8b_cpu_if_parents),
	},
};

static struct clk_regmap meson8b_apb = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_SYS_CPU_CLK_CNTL1,
		.bit_idx = 16,
		.flags = CLK_GATE_SET_TO_DISABLE,
	},
	.hw.init = &(struct clk_init_data){
		.name = "apb",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_apb_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_periph_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_SYS_CPU_CLK_CNTL1,
		.mask = 0x7,
		.shift = 6,
	},
	.hw.init = &(struct clk_init_data){
		.name = "periph_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_cpu_if_parents,
		.num_parents = ARRAY_SIZE(meson8b_cpu_if_parents),
	},
};

static struct clk_regmap meson8b_periph = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_SYS_CPU_CLK_CNTL1,
		.bit_idx = 17,
		.flags = CLK_GATE_SET_TO_DISABLE,
	},
	.hw.init = &(struct clk_init_data){
		.name = "periph",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_periph_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_axi_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_SYS_CPU_CLK_CNTL1,
		.mask = 0x7,
		.shift = 9,
		.table = meson8b_cpu_if_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "axi_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_cpu_if_parents,
		.num_parents = ARRAY_SIZE(meson8b_cpu_if_parents),
	},
};

static struct clk_regmap meson8b_axi = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_SYS_CPU_CLK_CNTL1,
		.bit_idx = 18,
		.flags = CLK_GATE_SET_TO_DISABLE,
	},
	.hw.init = &(struct clk_init_data){
		.name = "axi",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_axi_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_l2_dram_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_SYS_CPU_CLK_CNTL1,
		.mask = 0x7,
		.shift = 12,
	},
	.hw.init = &(struct clk_init_data){
		.name = "l2_dram_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_cpu_if_parents,
		.num_parents = ARRAY_SIZE(meson8b_cpu_if_parents),
	},
};

static struct clk_regmap meson8b_l2_dram = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_SYS_CPU_CLK_CNTL1,
		.bit_idx = 19,
		.flags = CLK_GATE_SET_TO_DISABLE,
	},
	.hw.init = &(struct clk_init_data){
		.name = "l2_dram",
		.ops = &clk_regmap_gate_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_l2_dram_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* also called LVDS_CLK_EN */
static struct clk_regmap meson8b_vid_pll_lvds_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_DIVIDER_CNTL,
		.bit_idx = 11,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vid_pll_lvds_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_hdmi_pll_lvds_out.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vid_pll_in_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VID_DIVIDER_CNTL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vid_pll_in_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * TODO: depending on the SoC there is also a second parent:
		 * Meson8: unknown
		 * Meson8b: hdmi_pll_dco
		 * Meson8m2: vid2_pll
		 */
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vid_pll_lvds_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vid_pll_in_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_DIVIDER_CNTL,
		.bit_idx = 16,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vid_pll_in_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vid_pll_in_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vid_pll_pre_div = {
	.data = &(struct clk_regmap_div_data){
		.offset =  HHI_VID_DIVIDER_CNTL,
		.shift = 4,
		.width = 3,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vid_pll_pre_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vid_pll_in_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vid_pll_post_div = {
	.data = &(struct clk_regmap_div_data){
		.offset =  HHI_VID_DIVIDER_CNTL,
		.shift = 12,
		.width = 3,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vid_pll_post_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vid_pll_pre_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vid_pll = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VID_DIVIDER_CNTL,
		.mask = 0x3,
		.shift = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vid_pll",
		.ops = &clk_regmap_mux_ops,
		/* TODO: parent 0x2 is vid_pll_pre_div_mult7_div2 */
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vid_pll_pre_div.hw,
			&meson8b_vid_pll_post_div.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vid_pll_final_div = {
	.data = &(struct clk_regmap_div_data){
		.offset =  HHI_VID_CLK_DIV,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vid_pll_final_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vid_pll.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_hw *meson8b_vclk_parents[] = {
	&meson8b_vid_pll_final_div.hw,
	&meson8b_fclk_div4.hw,
	&meson8b_fclk_div3.hw,
	&meson8b_fclk_div5.hw,
	&meson8b_vid_pll_final_div.hw,
	&meson8b_fclk_div7.hw,
	&meson8b_mpll1.hw,
};

static struct clk_regmap meson8b_vclk_in_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VID_CLK_CNTL,
		.mask = 0x7,
		.shift = 16,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk_in_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vclk_parents,
		.num_parents = ARRAY_SIZE(meson8b_vclk_parents),
		.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap meson8b_vclk_in_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_DIV,
		.bit_idx = 16,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk_in_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk_in_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vclk_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 19,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk_in_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vclk_div1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor meson8b_vclk_div2_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div2_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	}
};

static struct clk_regmap meson8b_vclk_div2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 1,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk_div2_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor meson8b_vclk_div4_div = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div4_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	}
};

static struct clk_regmap meson8b_vclk_div4 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 2,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div4",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk_div4_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor meson8b_vclk_div6_div = {
	.mult = 1,
	.div = 6,
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div6_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	}
};

static struct clk_regmap meson8b_vclk_div6 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 3,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div6",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk_div6_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor meson8b_vclk_div12_div = {
	.mult = 1,
	.div = 12,
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div12_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	}
};

static struct clk_regmap meson8b_vclk_div12 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL,
		.bit_idx = 4,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk_div12",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk_div12_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vclk2_in_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VIID_CLK_CNTL,
		.mask = 0x7,
		.shift = 16,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_in_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vclk_parents,
		.num_parents = ARRAY_SIZE(meson8b_vclk_parents),
		.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap meson8b_vclk2_in_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_DIV,
		.bit_idx = 16,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_in_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk2_in_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vclk2_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_DIV,
		.bit_idx = 19,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk2_in_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vclk2_div1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_DIV,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk2_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor meson8b_vclk2_div2_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div2_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk2_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	}
};

static struct clk_regmap meson8b_vclk2_div2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_DIV,
		.bit_idx = 1,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk2_div2_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor meson8b_vclk2_div4_div = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div4_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk2_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	}
};

static struct clk_regmap meson8b_vclk2_div4 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_DIV,
		.bit_idx = 2,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div4",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk2_div4_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor meson8b_vclk2_div6_div = {
	.mult = 1,
	.div = 6,
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div6_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk2_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	}
};

static struct clk_regmap meson8b_vclk2_div6 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_DIV,
		.bit_idx = 3,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div6",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk2_div6_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor meson8b_vclk2_div12_div = {
	.mult = 1,
	.div = 12,
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div12_div",
		.ops = &clk_fixed_factor_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk2_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	}
};

static struct clk_regmap meson8b_vclk2_div12 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VIID_CLK_DIV,
		.bit_idx = 4,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk2_div12",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vclk2_div12_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_hw *meson8b_vclk_enc_parents[] = {
	&meson8b_vclk_div1.hw,
	&meson8b_vclk_div2.hw,
	&meson8b_vclk_div4.hw,
	&meson8b_vclk_div6.hw,
	&meson8b_vclk_div12.hw,
};

static struct clk_regmap meson8b_cts_enct_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VID_CLK_DIV,
		.mask = 0xf,
		.shift = 20,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_enct_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vclk_enc_parents,
		.num_parents = ARRAY_SIZE(meson8b_vclk_enc_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_enct = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL2,
		.bit_idx = 1,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_enct",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cts_enct_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_encp_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VID_CLK_DIV,
		.mask = 0xf,
		.shift = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_encp_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vclk_enc_parents,
		.num_parents = ARRAY_SIZE(meson8b_vclk_enc_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_encp = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL2,
		.bit_idx = 2,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_encp",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cts_encp_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_enci_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VID_CLK_DIV,
		.mask = 0xf,
		.shift = 28,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_enci_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vclk_enc_parents,
		.num_parents = ARRAY_SIZE(meson8b_vclk_enc_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_enci = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL2,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_enci",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cts_enci_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_hdmi_tx_pixel_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_HDMI_CLK_CNTL,
		.mask = 0xf,
		.shift = 16,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_tx_pixel_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vclk_enc_parents,
		.num_parents = ARRAY_SIZE(meson8b_vclk_enc_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_hdmi_tx_pixel = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL2,
		.bit_idx = 5,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_tx_pixel",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_hdmi_tx_pixel_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_encl_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VIID_CLK_DIV,
		.mask = 0xf,
		.shift = 12,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_encl_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vclk_enc_parents,
		.num_parents = ARRAY_SIZE(meson8b_vclk_enc_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_encl = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL2,
		.bit_idx = 3,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_encl",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cts_encl_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_vdac0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VIID_CLK_DIV,
		.mask = 0xf,
		.shift = 28,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_vdac0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vclk_enc_parents,
		.num_parents = ARRAY_SIZE(meson8b_vclk_enc_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_vdac0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VID_CLK_CNTL2,
		.bit_idx = 4,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_vdac0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cts_vdac0_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_hdmi_sys_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_HDMI_CLK_CNTL,
		.mask = 0x3,
		.shift = 9,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_sys_sel",
		.ops = &clk_regmap_mux_ops,
		/* FIXME: all other parents are unknown */
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
			.name = "xtal",
			.index = -1,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap meson8b_hdmi_sys_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_HDMI_CLK_CNTL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_sys_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_hdmi_sys_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_hdmi_sys = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_HDMI_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hdmi_sys",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_hdmi_sys_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * The MALI IP is clocked by two identical clocks (mali_0 and mali_1)
 * muxed by a glitch-free switch on Meson8b and Meson8m2. The CCF can
 * actually manage this glitch-free mux because it does top-to-bottom
 * updates the each clock tree and switches to the "inactive" one when
 * CLK_SET_RATE_GATE is set.
 * Meson8 only has mali_0 and no glitch-free mux.
 */
static u32 meson8b_mali_parents_val_table[] = { 0, 2, 3, 4, 5, 6, 7 };
static const struct clk_parent_data meson8b_mali_parents[] = {
	{ .fw_name = "xtal", .name = "xtal", .index = -1, },
	{ .hw = &meson8b_mpll2.hw, },
	{ .hw = &meson8b_mpll1.hw, },
	{ .hw = &meson8b_fclk_div7.hw, },
	{ .hw = &meson8b_fclk_div4.hw, },
	{ .hw = &meson8b_fclk_div3.hw, },
	{ .hw = &meson8b_fclk_div5.hw, },
};

static struct clk_regmap meson8b_mali_0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_MALI_CLK_CNTL,
		.mask = 0x7,
		.shift = 9,
		.table = meson8b_mali_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = meson8b_mali_parents,
		.num_parents = ARRAY_SIZE(meson8b_mali_parents),
		/*
		 * Don't propagate rate changes up because the only changeable
		 * parents are mpll1 and mpll2 but we need those for audio and
		 * RGMII (Ethernet). We don't want to change the audio or
		 * Ethernet clocks when setting the GPU frequency.
		 */
		.flags = 0,
	},
};

static struct clk_regmap meson8b_mali_0_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_MALI_CLK_CNTL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_mali_0_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_mali_0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MALI_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_mali_0_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_GATE | CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_mali_1_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_MALI_CLK_CNTL,
		.mask = 0x7,
		.shift = 25,
		.table = meson8b_mali_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_1_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = meson8b_mali_parents,
		.num_parents = ARRAY_SIZE(meson8b_mali_parents),
		/*
		 * Don't propagate rate changes up because the only changeable
		 * parents are mpll1 and mpll2 but we need those for audio and
		 * RGMII (Ethernet). We don't want to change the audio or
		 * Ethernet clocks when setting the GPU frequency.
		 */
		.flags = 0,
	},
};

static struct clk_regmap meson8b_mali_1_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_MALI_CLK_CNTL,
		.shift = 16,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_1_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_mali_1_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_mali_1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MALI_CLK_CNTL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_mali_1_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_GATE | CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_mali = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_MALI_CLK_CNTL,
		.mask = 1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_mali_0.hw,
			&meson8b_mali_1.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct reg_sequence meson8m2_gp_pll_init_regs[] = {
	{ .reg = HHI_GP_PLL_CNTL2,	.def = 0x59c88000 },
	{ .reg = HHI_GP_PLL_CNTL3,	.def = 0xca463823 },
	{ .reg = HHI_GP_PLL_CNTL4,	.def = 0x0286a027 },
	{ .reg = HHI_GP_PLL_CNTL5,	.def = 0x00003000 },
};

static const struct pll_params_table meson8m2_gp_pll_params_table[] = {
	PLL_PARAMS(182, 3),
	{ /* sentinel */ },
};

static struct clk_regmap meson8m2_gp_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = HHI_GP_PLL_CNTL,
			.shift   = 30,
			.width   = 1,
		},
		.m = {
			.reg_off = HHI_GP_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_GP_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.l = {
			.reg_off = HHI_GP_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_GP_PLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
		.table = meson8m2_gp_pll_params_table,
		.init_regs = meson8m2_gp_pll_init_regs,
		.init_count = ARRAY_SIZE(meson8m2_gp_pll_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "gp_pll_dco",
		.ops = &meson_clk_pll_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
			.name = "xtal",
			.index = -1,
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8m2_gp_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_GP_PLL_CNTL,
		.shift = 16,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gp_pll",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8m2_gp_pll_dco.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_hw *meson8b_vpu_parents[] = {
	&meson8b_fclk_div4.hw,
	&meson8b_fclk_div3.hw,
	&meson8b_fclk_div5.hw,
	&meson8b_fclk_div7.hw,
};

static struct clk_regmap meson8b_vpu_0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VPU_CLK_CNTL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu_0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vpu_parents,
		.num_parents = ARRAY_SIZE(meson8b_vpu_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_hw *mmeson8m2_vpu_parents[] = {
	&meson8b_fclk_div4.hw,
	&meson8b_fclk_div3.hw,
	&meson8b_fclk_div5.hw,
	&meson8m2_gp_pll.hw,
};

static struct clk_regmap meson8m2_vpu_0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VPU_CLK_CNTL,
		.mask = 0x3,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu_0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = mmeson8m2_vpu_parents,
		.num_parents = ARRAY_SIZE(mmeson8m2_vpu_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vpu_0_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VPU_CLK_CNTL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu_0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_data = &(const struct clk_parent_data) {
			/*
			 * Note:
			 * meson8b and meson8m2 have different vpu_0_sels (with
			 * different struct clk_hw). We fallback to the global
			 * naming string mechanism so vpu_0_div picks up the
			 * appropriate one.
			 */
			.name = "vpu_0_sel",
			.index = -1,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vpu_0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VPU_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vpu_0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vpu_0_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_GATE | CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vpu_1_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VPU_CLK_CNTL,
		.mask = 0x3,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu_1_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vpu_parents,
		.num_parents = ARRAY_SIZE(meson8b_vpu_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8m2_vpu_1_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VPU_CLK_CNTL,
		.mask = 0x3,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu_1_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = mmeson8m2_vpu_parents,
		.num_parents = ARRAY_SIZE(mmeson8m2_vpu_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vpu_1_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VPU_CLK_CNTL,
		.shift = 16,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu_1_div",
		.ops = &clk_regmap_divider_ops,
		.parent_data = &(const struct clk_parent_data) {
			/*
			 * Note:
			 * meson8b and meson8m2 have different vpu_1_sels (with
			 * different struct clk_hw). We fallback to the global
			 * naming string mechanism so vpu_1_div picks up the
			 * appropriate one.
			 */
			.name = "vpu_1_sel",
			.index = -1,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vpu_1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VPU_CLK_CNTL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vpu_1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vpu_1_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_GATE | CLK_SET_RATE_PARENT,
	},
};

/*
 * The VPU clock has two identical clock trees (vpu_0 and vpu_1)
 * muxed by a glitch-free switch on Meson8b and Meson8m2. The CCF can
 * actually manage this glitch-free mux because it does top-to-bottom
 * updates the each clock tree and switches to the "inactive" one when
 * CLK_SET_RATE_GATE is set.
 * Meson8 only has vpu_0 and no glitch-free mux.
 */
static struct clk_regmap meson8b_vpu = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VPU_CLK_CNTL,
		.mask = 1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vpu_0.hw,
			&meson8b_vpu_1.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_hw *meson8b_vdec_parents[] = {
	&meson8b_fclk_div4.hw,
	&meson8b_fclk_div3.hw,
	&meson8b_fclk_div5.hw,
	&meson8b_fclk_div7.hw,
	&meson8b_mpll2.hw,
	&meson8b_mpll1.hw,
};

static struct clk_regmap meson8b_vdec_1_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VDEC_CLK_CNTL,
		.mask = 0x3,
		.shift = 9,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_1_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vdec_parents,
		.num_parents = ARRAY_SIZE(meson8b_vdec_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_1_1_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VDEC_CLK_CNTL,
		.shift = 0,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_1_1_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vdec_1_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_1_1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VDEC_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vdec_1_1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vdec_1_1_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_1_2_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VDEC3_CLK_CNTL,
		.shift = 0,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_1_2_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vdec_1_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_1_2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VDEC3_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vdec_1_2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vdec_1_2_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_1 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VDEC3_CLK_CNTL,
		.mask = 0x1,
		.shift = 15,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_1",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vdec_1_1.hw,
			&meson8b_vdec_1_2.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_hcodec_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VDEC_CLK_CNTL,
		.mask = 0x3,
		.shift = 25,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_hcodec_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vdec_parents,
		.num_parents = ARRAY_SIZE(meson8b_vdec_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_hcodec_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VDEC_CLK_CNTL,
		.shift = 16,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_hcodec_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vdec_hcodec_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_hcodec = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VDEC_CLK_CNTL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vdec_hcodec",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vdec_hcodec_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_2_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VDEC2_CLK_CNTL,
		.mask = 0x3,
		.shift = 9,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_2_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vdec_parents,
		.num_parents = ARRAY_SIZE(meson8b_vdec_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_2_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_VDEC2_CLK_CNTL,
		.shift = 0,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_2_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vdec_2_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VDEC2_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vdec_2",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vdec_2_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_hevc_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VDEC2_CLK_CNTL,
		.mask = 0x3,
		.shift = 25,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_hevc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_vdec_parents,
		.num_parents = ARRAY_SIZE(meson8b_vdec_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_hevc_div = {
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
			&meson8b_vdec_hevc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_hevc_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_VDEC2_CLK_CNTL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vdec_hevc_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vdec_hevc_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_vdec_hevc = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_VDEC2_CLK_CNTL,
		.mask = 0x1,
		.shift = 31,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdec_hevc",
		.ops = &clk_regmap_mux_ops,
		/* TODO: The second parent is currently unknown */
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_vdec_hevc_en.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* TODO: the clock at index 0 is "DDR_PLL" which we don't support yet */
static u32 meson8b_cts_mclk_parents_val_table[] = { 1, 2, 3 };
static const struct clk_hw *meson8b_cts_mclk_parents[] = {
	&meson8b_mpll0.hw,
	&meson8b_mpll1.hw,
	&meson8b_mpll2.hw
};

static struct clk_regmap meson8b_cts_amclk_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_AUD_CLK_CNTL,
		.mask = 0x3,
		.shift = 9,
		.table = meson8b_cts_mclk_parents_val_table,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_amclk_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_cts_mclk_parents,
		.num_parents = ARRAY_SIZE(meson8b_cts_mclk_parents),
	},
};

static struct clk_regmap meson8b_cts_amclk_div = {
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
			&meson8b_cts_amclk_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_amclk = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_AUD_CLK_CNTL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_amclk",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cts_amclk_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_mclk_i958_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_AUD_CLK_CNTL2,
		.mask = 0x3,
		.shift = 25,
		.table = meson8b_cts_mclk_parents_val_table,
		.flags = CLK_MUX_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "cts_mclk_i958_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = meson8b_cts_mclk_parents,
		.num_parents = ARRAY_SIZE(meson8b_cts_mclk_parents),
	},
};

static struct clk_regmap meson8b_cts_mclk_i958_div = {
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
			&meson8b_cts_mclk_i958_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_mclk_i958 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_AUD_CLK_CNTL2,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data){
		.name = "cts_mclk_i958",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cts_mclk_i958_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap meson8b_cts_i958 = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_AUD_CLK_CNTL2,
		.mask = 0x1,
		.shift = 27,
		},
	.hw.init = &(struct clk_init_data){
		.name = "cts_i958",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8b_cts_amclk.hw,
			&meson8b_cts_mclk_i958.hw
		},
		.num_parents = 2,
		/*
		 * The parent is specific to origin of the audio data. Let the
		 * consumer choose the appropriate parent.
		 */
		.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
	},
};

static const struct clk_parent_data meson8b_pclk_parents = { .hw = &meson8b_clk81.hw };

#define MESON8B_PCLK(_name, _reg, _bit, _flags) \
	MESON_PCLK(_name, _reg, _bit, &meson8b_pclk_parents, _flags)

/*
 * Everything Else (EE) domain gates
 *
 * NOTE: The gates below are marked with CLK_IGNORE_UNUSED for historic reasons
 * Users are encouraged to test without it and submit changes to:
 *  - remove the flag if not necessary
 *  - replace the flag with something more adequate, such as CLK_IS_CRITICAL,
 *    if appropriate.
 *  - add a comment explaining why the use of CLK_IGNORE_UNUSED is desirable
 *    for a particular clock.
 */
static MESON8B_PCLK(meson8b_ddr,		HHI_GCLK_MPEG0,  0, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_dos,		HHI_GCLK_MPEG0,  1, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_isa,		HHI_GCLK_MPEG0,  5, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_pl301,		HHI_GCLK_MPEG0,  6, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_periphs,		HHI_GCLK_MPEG0,  7, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_spicc,		HHI_GCLK_MPEG0,  8, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_i2c,		HHI_GCLK_MPEG0,  9, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_sar_adc,		HHI_GCLK_MPEG0, 10, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_smart_card,		HHI_GCLK_MPEG0, 11, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_rng0,		HHI_GCLK_MPEG0, 12, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_uart0,		HHI_GCLK_MPEG0, 13, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_sdhc,		HHI_GCLK_MPEG0, 14, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_stream,		HHI_GCLK_MPEG0, 15, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_async_fifo,		HHI_GCLK_MPEG0, 16, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_sdio,		HHI_GCLK_MPEG0, 17, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_abuf,		HHI_GCLK_MPEG0, 18, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_hiu_iface,		HHI_GCLK_MPEG0, 19, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_assist_misc,	HHI_GCLK_MPEG0, 23, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_spi,		HHI_GCLK_MPEG0, 30, CLK_IGNORE_UNUSED);

static MESON8B_PCLK(meson8b_i2s_spdif,		HHI_GCLK_MPEG1,  2, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_eth,		HHI_GCLK_MPEG1,  3, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_demux,		HHI_GCLK_MPEG1,  4, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_blkmv,		HHI_GCLK_MPEG1, 14, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_aiu,		HHI_GCLK_MPEG1, 15, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_uart1,		HHI_GCLK_MPEG1, 16, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_g2d,		HHI_GCLK_MPEG1, 20, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_usb0,		HHI_GCLK_MPEG1, 21, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_usb1,		HHI_GCLK_MPEG1, 22, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_reset,		HHI_GCLK_MPEG1, 23, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_nand,		HHI_GCLK_MPEG1, 24, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_dos_parser,		HHI_GCLK_MPEG1, 25, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_usb,		HHI_GCLK_MPEG1, 26, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_vdin1,		HHI_GCLK_MPEG1, 28, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_ahb_arb0,		HHI_GCLK_MPEG1, 29, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_efuse,		HHI_GCLK_MPEG1, 30, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_boot_rom,		HHI_GCLK_MPEG1, 31, CLK_IGNORE_UNUSED);

static MESON8B_PCLK(meson8b_ahb_data_bus,	HHI_GCLK_MPEG2,  1, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_ahb_ctrl_bus,	HHI_GCLK_MPEG2,  2, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_hdmi_intr_sync,	HHI_GCLK_MPEG2,  3, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_hdmi_pclk,		HHI_GCLK_MPEG2,  4, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_usb1_ddr_bridge,	HHI_GCLK_MPEG2,  8, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_usb0_ddr_bridge,	HHI_GCLK_MPEG2,  9, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_mmc_pclk,		HHI_GCLK_MPEG2, 11, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_dvin,		HHI_GCLK_MPEG2, 12, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_uart2,		HHI_GCLK_MPEG2, 15, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_sana,		HHI_GCLK_MPEG2, 22, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_vpu_intr,		HHI_GCLK_MPEG2, 25, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_sec_ahb_ahb3_bridge, HHI_GCLK_MPEG2, 26, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_clk81_a9,		HHI_GCLK_MPEG2, 29, CLK_IGNORE_UNUSED);

static MESON8B_PCLK(meson8b_vclk2_venci0,	HHI_GCLK_OTHER,  1, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_vclk2_venci1,	HHI_GCLK_OTHER,  2, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_vclk2_vencp0,	HHI_GCLK_OTHER,  3, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_vclk2_vencp1,	HHI_GCLK_OTHER,  4, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_gclk_venci_int,	HHI_GCLK_OTHER,  8, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_gclk_vencp_int,	HHI_GCLK_OTHER,  9, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_dac_clk,		HHI_GCLK_OTHER, 10, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_aoclk_gate,		HHI_GCLK_OTHER, 14, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_iec958_gate,	HHI_GCLK_OTHER, 16, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_enc480p,		HHI_GCLK_OTHER, 20, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_rng1,		HHI_GCLK_OTHER, 21, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_gclk_vencl_int,	HHI_GCLK_OTHER, 22, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_vclk2_venclmcc,	HHI_GCLK_OTHER, 24, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_vclk2_vencl,	HHI_GCLK_OTHER, 25, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_vclk2_other,	HHI_GCLK_OTHER, 26, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_edp,		HHI_GCLK_OTHER, 31, CLK_IGNORE_UNUSED);

/* AIU gates */
static const struct clk_parent_data meson8b_aiu_glue_parents = { .hw = &meson8b_aiu.hw };
static MESON_PCLK(meson8b_aiu_glue, HHI_GCLK_MPEG1, 6,
		  &meson8b_aiu_glue_parents, CLK_IGNORE_UNUSED);

static const struct clk_parent_data meson8b_aiu_pclk_parents = { .hw = &meson8b_aiu_glue.hw };
#define MESON8B_AIU_PCLK(_name, _bit, _flags) \
	MESON_PCLK(_name, HHI_GCLK_MPEG1, _bit, &meson8b_aiu_pclk_parents, _flags)

static MESON8B_AIU_PCLK(meson8b_iec958,		  7, CLK_IGNORE_UNUSED);
static MESON8B_AIU_PCLK(meson8b_i2s_out,	  8, CLK_IGNORE_UNUSED);
static MESON8B_AIU_PCLK(meson8b_amclk,		  9, CLK_IGNORE_UNUSED);
static MESON8B_AIU_PCLK(meson8b_aififo2,	 10, CLK_IGNORE_UNUSED);
static MESON8B_AIU_PCLK(meson8b_mixer,		 11, CLK_IGNORE_UNUSED);
static MESON8B_AIU_PCLK(meson8b_mixer_iface,	 12, CLK_IGNORE_UNUSED);
static MESON8B_AIU_PCLK(meson8b_adc,		 13, CLK_IGNORE_UNUSED);

/* Always On (AO) domain gates */

static MESON8B_PCLK(meson8b_ao_media_cpu,	HHI_GCLK_AO, 0, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_ao_ahb_sram,	HHI_GCLK_AO, 1, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_ao_ahb_bus,		HHI_GCLK_AO, 2, CLK_IGNORE_UNUSED);
static MESON8B_PCLK(meson8b_ao_iface,		HHI_GCLK_AO, 3, CLK_IGNORE_UNUSED);

static struct clk_hw *meson8_hw_clks[] = {
	[CLKID_PLL_FIXED]	    = &meson8b_fixed_pll.hw,
	[CLKID_PLL_VID]		    = &meson8b_vid_pll.hw,
	[CLKID_PLL_SYS]		    = &meson8b_sys_pll.hw,
	[CLKID_FCLK_DIV2]	    = &meson8b_fclk_div2.hw,
	[CLKID_FCLK_DIV3]	    = &meson8b_fclk_div3.hw,
	[CLKID_FCLK_DIV4]	    = &meson8b_fclk_div4.hw,
	[CLKID_FCLK_DIV5]	    = &meson8b_fclk_div5.hw,
	[CLKID_FCLK_DIV7]	    = &meson8b_fclk_div7.hw,
	[CLKID_CPUCLK]		    = &meson8b_cpu_clk.hw,
	[CLKID_MPEG_SEL]	    = &meson8b_clk81_sel.hw,
	[CLKID_MPEG_DIV]	    = &meson8b_clk81_div.hw,
	[CLKID_CLK81]		    = &meson8b_clk81.hw,
	[CLKID_DDR]		    = &meson8b_ddr.hw,
	[CLKID_DOS]		    = &meson8b_dos.hw,
	[CLKID_ISA]		    = &meson8b_isa.hw,
	[CLKID_PL301]		    = &meson8b_pl301.hw,
	[CLKID_PERIPHS]		    = &meson8b_periphs.hw,
	[CLKID_SPICC]		    = &meson8b_spicc.hw,
	[CLKID_I2C]		    = &meson8b_i2c.hw,
	[CLKID_SAR_ADC]		    = &meson8b_sar_adc.hw,
	[CLKID_SMART_CARD]	    = &meson8b_smart_card.hw,
	[CLKID_RNG0]		    = &meson8b_rng0.hw,
	[CLKID_UART0]		    = &meson8b_uart0.hw,
	[CLKID_SDHC]		    = &meson8b_sdhc.hw,
	[CLKID_STREAM]		    = &meson8b_stream.hw,
	[CLKID_ASYNC_FIFO]	    = &meson8b_async_fifo.hw,
	[CLKID_SDIO]		    = &meson8b_sdio.hw,
	[CLKID_ABUF]		    = &meson8b_abuf.hw,
	[CLKID_HIU_IFACE]	    = &meson8b_hiu_iface.hw,
	[CLKID_ASSIST_MISC]	    = &meson8b_assist_misc.hw,
	[CLKID_SPI]		    = &meson8b_spi.hw,
	[CLKID_I2S_SPDIF]	    = &meson8b_i2s_spdif.hw,
	[CLKID_ETH]		    = &meson8b_eth.hw,
	[CLKID_DEMUX]		    = &meson8b_demux.hw,
	[CLKID_AIU_GLUE]	    = &meson8b_aiu_glue.hw,
	[CLKID_IEC958]		    = &meson8b_iec958.hw,
	[CLKID_I2S_OUT]		    = &meson8b_i2s_out.hw,
	[CLKID_AMCLK]		    = &meson8b_amclk.hw,
	[CLKID_AIFIFO2]		    = &meson8b_aififo2.hw,
	[CLKID_MIXER]		    = &meson8b_mixer.hw,
	[CLKID_MIXER_IFACE]	    = &meson8b_mixer_iface.hw,
	[CLKID_ADC]		    = &meson8b_adc.hw,
	[CLKID_BLKMV]		    = &meson8b_blkmv.hw,
	[CLKID_AIU]		    = &meson8b_aiu.hw,
	[CLKID_UART1]		    = &meson8b_uart1.hw,
	[CLKID_G2D]		    = &meson8b_g2d.hw,
	[CLKID_USB0]		    = &meson8b_usb0.hw,
	[CLKID_USB1]		    = &meson8b_usb1.hw,
	[CLKID_RESET]		    = &meson8b_reset.hw,
	[CLKID_NAND]		    = &meson8b_nand.hw,
	[CLKID_DOS_PARSER]	    = &meson8b_dos_parser.hw,
	[CLKID_USB]		    = &meson8b_usb.hw,
	[CLKID_VDIN1]		    = &meson8b_vdin1.hw,
	[CLKID_AHB_ARB0]	    = &meson8b_ahb_arb0.hw,
	[CLKID_EFUSE]		    = &meson8b_efuse.hw,
	[CLKID_BOOT_ROM]	    = &meson8b_boot_rom.hw,
	[CLKID_AHB_DATA_BUS]	    = &meson8b_ahb_data_bus.hw,
	[CLKID_AHB_CTRL_BUS]	    = &meson8b_ahb_ctrl_bus.hw,
	[CLKID_HDMI_INTR_SYNC]	    = &meson8b_hdmi_intr_sync.hw,
	[CLKID_HDMI_PCLK]	    = &meson8b_hdmi_pclk.hw,
	[CLKID_USB1_DDR_BRIDGE]	    = &meson8b_usb1_ddr_bridge.hw,
	[CLKID_USB0_DDR_BRIDGE]	    = &meson8b_usb0_ddr_bridge.hw,
	[CLKID_MMC_PCLK]	    = &meson8b_mmc_pclk.hw,
	[CLKID_DVIN]		    = &meson8b_dvin.hw,
	[CLKID_UART2]		    = &meson8b_uart2.hw,
	[CLKID_SANA]		    = &meson8b_sana.hw,
	[CLKID_VPU_INTR]	    = &meson8b_vpu_intr.hw,
	[CLKID_SEC_AHB_AHB3_BRIDGE] = &meson8b_sec_ahb_ahb3_bridge.hw,
	[CLKID_CLK81_A9]	    = &meson8b_clk81_a9.hw,
	[CLKID_VCLK2_VENCI0]	    = &meson8b_vclk2_venci0.hw,
	[CLKID_VCLK2_VENCI1]	    = &meson8b_vclk2_venci1.hw,
	[CLKID_VCLK2_VENCP0]	    = &meson8b_vclk2_vencp0.hw,
	[CLKID_VCLK2_VENCP1]	    = &meson8b_vclk2_vencp1.hw,
	[CLKID_GCLK_VENCI_INT]	    = &meson8b_gclk_venci_int.hw,
	[CLKID_GCLK_VENCP_INT]	    = &meson8b_gclk_vencp_int.hw,
	[CLKID_DAC_CLK]		    = &meson8b_dac_clk.hw,
	[CLKID_AOCLK_GATE]	    = &meson8b_aoclk_gate.hw,
	[CLKID_IEC958_GATE]	    = &meson8b_iec958_gate.hw,
	[CLKID_ENC480P]		    = &meson8b_enc480p.hw,
	[CLKID_RNG1]		    = &meson8b_rng1.hw,
	[CLKID_GCLK_VENCL_INT]	    = &meson8b_gclk_vencl_int.hw,
	[CLKID_VCLK2_VENCLMCC]	    = &meson8b_vclk2_venclmcc.hw,
	[CLKID_VCLK2_VENCL]	    = &meson8b_vclk2_vencl.hw,
	[CLKID_VCLK2_OTHER]	    = &meson8b_vclk2_other.hw,
	[CLKID_EDP]		    = &meson8b_edp.hw,
	[CLKID_AO_MEDIA_CPU]	    = &meson8b_ao_media_cpu.hw,
	[CLKID_AO_AHB_SRAM]	    = &meson8b_ao_ahb_sram.hw,
	[CLKID_AO_AHB_BUS]	    = &meson8b_ao_ahb_bus.hw,
	[CLKID_AO_IFACE]	    = &meson8b_ao_iface.hw,
	[CLKID_MPLL0]		    = &meson8b_mpll0.hw,
	[CLKID_MPLL1]		    = &meson8b_mpll1.hw,
	[CLKID_MPLL2]		    = &meson8b_mpll2.hw,
	[CLKID_MPLL0_DIV]	    = &meson8b_mpll0_div.hw,
	[CLKID_MPLL1_DIV]	    = &meson8b_mpll1_div.hw,
	[CLKID_MPLL2_DIV]	    = &meson8b_mpll2_div.hw,
	[CLKID_CPU_IN_SEL]	    = &meson8b_cpu_in_sel.hw,
	[CLKID_CPU_IN_DIV2]	    = &meson8b_cpu_in_div2.hw,
	[CLKID_CPU_IN_DIV3]	    = &meson8b_cpu_in_div3.hw,
	[CLKID_CPU_SCALE_DIV]	    = &meson8b_cpu_scale_div.hw,
	[CLKID_CPU_SCALE_OUT_SEL]   = &meson8b_cpu_scale_out_sel.hw,
	[CLKID_MPLL_PREDIV]	    = &meson8b_mpll_prediv.hw,
	[CLKID_FCLK_DIV2_DIV]	    = &meson8b_fclk_div2_div.hw,
	[CLKID_FCLK_DIV3_DIV]	    = &meson8b_fclk_div3_div.hw,
	[CLKID_FCLK_DIV4_DIV]	    = &meson8b_fclk_div4_div.hw,
	[CLKID_FCLK_DIV5_DIV]	    = &meson8b_fclk_div5_div.hw,
	[CLKID_FCLK_DIV7_DIV]	    = &meson8b_fclk_div7_div.hw,
	[CLKID_NAND_SEL]	    = &meson8b_nand_clk_sel.hw,
	[CLKID_NAND_DIV]	    = &meson8b_nand_clk_div.hw,
	[CLKID_NAND_CLK]	    = &meson8b_nand_clk.hw,
	[CLKID_PLL_FIXED_DCO]	    = &meson8b_fixed_pll_dco.hw,
	[CLKID_HDMI_PLL_DCO]	    = &meson8b_hdmi_pll_dco.hw,
	[CLKID_PLL_SYS_DCO]	    = &meson8b_sys_pll_dco.hw,
	[CLKID_CPU_CLK_DIV2]	    = &meson8b_cpu_clk_div2.hw,
	[CLKID_CPU_CLK_DIV3]	    = &meson8b_cpu_clk_div3.hw,
	[CLKID_CPU_CLK_DIV4]	    = &meson8b_cpu_clk_div4.hw,
	[CLKID_CPU_CLK_DIV5]	    = &meson8b_cpu_clk_div5.hw,
	[CLKID_CPU_CLK_DIV6]	    = &meson8b_cpu_clk_div6.hw,
	[CLKID_CPU_CLK_DIV7]	    = &meson8b_cpu_clk_div7.hw,
	[CLKID_CPU_CLK_DIV8]	    = &meson8b_cpu_clk_div8.hw,
	[CLKID_APB_SEL]		    = &meson8b_apb_sel.hw,
	[CLKID_APB]		    = &meson8b_apb.hw,
	[CLKID_PERIPH_SEL]	    = &meson8b_periph_sel.hw,
	[CLKID_PERIPH]		    = &meson8b_periph.hw,
	[CLKID_AXI_SEL]		    = &meson8b_axi_sel.hw,
	[CLKID_AXI]		    = &meson8b_axi.hw,
	[CLKID_L2_DRAM_SEL]	    = &meson8b_l2_dram_sel.hw,
	[CLKID_L2_DRAM]		    = &meson8b_l2_dram.hw,
	[CLKID_HDMI_PLL_LVDS_OUT]   = &meson8b_hdmi_pll_lvds_out.hw,
	[CLKID_HDMI_PLL_HDMI_OUT]   = &meson8b_hdmi_pll_hdmi_out.hw,
	[CLKID_VID_PLL_IN_SEL]	    = &meson8b_vid_pll_in_sel.hw,
	[CLKID_VID_PLL_IN_EN]	    = &meson8b_vid_pll_in_en.hw,
	[CLKID_VID_PLL_PRE_DIV]	    = &meson8b_vid_pll_pre_div.hw,
	[CLKID_VID_PLL_POST_DIV]    = &meson8b_vid_pll_post_div.hw,
	[CLKID_VID_PLL_FINAL_DIV]   = &meson8b_vid_pll_final_div.hw,
	[CLKID_VCLK_IN_SEL]	    = &meson8b_vclk_in_sel.hw,
	[CLKID_VCLK_IN_EN]	    = &meson8b_vclk_in_en.hw,
	[CLKID_VCLK_EN]		    = &meson8b_vclk_en.hw,
	[CLKID_VCLK_DIV1]	    = &meson8b_vclk_div1.hw,
	[CLKID_VCLK_DIV2_DIV]	    = &meson8b_vclk_div2_div.hw,
	[CLKID_VCLK_DIV2]	    = &meson8b_vclk_div2.hw,
	[CLKID_VCLK_DIV4_DIV]	    = &meson8b_vclk_div4_div.hw,
	[CLKID_VCLK_DIV4]	    = &meson8b_vclk_div4.hw,
	[CLKID_VCLK_DIV6_DIV]	    = &meson8b_vclk_div6_div.hw,
	[CLKID_VCLK_DIV6]	    = &meson8b_vclk_div6.hw,
	[CLKID_VCLK_DIV12_DIV]	    = &meson8b_vclk_div12_div.hw,
	[CLKID_VCLK_DIV12]	    = &meson8b_vclk_div12.hw,
	[CLKID_VCLK2_IN_SEL]	    = &meson8b_vclk2_in_sel.hw,
	[CLKID_VCLK2_IN_EN]	    = &meson8b_vclk2_in_en.hw,
	[CLKID_VCLK2_EN]	    = &meson8b_vclk2_en.hw,
	[CLKID_VCLK2_DIV1]	    = &meson8b_vclk2_div1.hw,
	[CLKID_VCLK2_DIV2_DIV]	    = &meson8b_vclk2_div2_div.hw,
	[CLKID_VCLK2_DIV2]	    = &meson8b_vclk2_div2.hw,
	[CLKID_VCLK2_DIV4_DIV]	    = &meson8b_vclk2_div4_div.hw,
	[CLKID_VCLK2_DIV4]	    = &meson8b_vclk2_div4.hw,
	[CLKID_VCLK2_DIV6_DIV]	    = &meson8b_vclk2_div6_div.hw,
	[CLKID_VCLK2_DIV6]	    = &meson8b_vclk2_div6.hw,
	[CLKID_VCLK2_DIV12_DIV]	    = &meson8b_vclk2_div12_div.hw,
	[CLKID_VCLK2_DIV12]	    = &meson8b_vclk2_div12.hw,
	[CLKID_CTS_ENCT_SEL]	    = &meson8b_cts_enct_sel.hw,
	[CLKID_CTS_ENCT]	    = &meson8b_cts_enct.hw,
	[CLKID_CTS_ENCP_SEL]	    = &meson8b_cts_encp_sel.hw,
	[CLKID_CTS_ENCP]	    = &meson8b_cts_encp.hw,
	[CLKID_CTS_ENCI_SEL]	    = &meson8b_cts_enci_sel.hw,
	[CLKID_CTS_ENCI]	    = &meson8b_cts_enci.hw,
	[CLKID_HDMI_TX_PIXEL_SEL]   = &meson8b_hdmi_tx_pixel_sel.hw,
	[CLKID_HDMI_TX_PIXEL]	    = &meson8b_hdmi_tx_pixel.hw,
	[CLKID_CTS_ENCL_SEL]	    = &meson8b_cts_encl_sel.hw,
	[CLKID_CTS_ENCL]	    = &meson8b_cts_encl.hw,
	[CLKID_CTS_VDAC0_SEL]	    = &meson8b_cts_vdac0_sel.hw,
	[CLKID_CTS_VDAC0]	    = &meson8b_cts_vdac0.hw,
	[CLKID_HDMI_SYS_SEL]	    = &meson8b_hdmi_sys_sel.hw,
	[CLKID_HDMI_SYS_DIV]	    = &meson8b_hdmi_sys_div.hw,
	[CLKID_HDMI_SYS]	    = &meson8b_hdmi_sys.hw,
	[CLKID_MALI_0_SEL]	    = &meson8b_mali_0_sel.hw,
	[CLKID_MALI_0_DIV]	    = &meson8b_mali_0_div.hw,
	[CLKID_MALI]		    = &meson8b_mali_0.hw,
	[CLKID_VPU_0_SEL]	    = &meson8b_vpu_0_sel.hw,
	[CLKID_VPU_0_DIV]	    = &meson8b_vpu_0_div.hw,
	[CLKID_VPU]		    = &meson8b_vpu_0.hw,
	[CLKID_VDEC_1_SEL]	    = &meson8b_vdec_1_sel.hw,
	[CLKID_VDEC_1_1_DIV]	    = &meson8b_vdec_1_1_div.hw,
	[CLKID_VDEC_1]		    = &meson8b_vdec_1_1.hw,
	[CLKID_VDEC_HCODEC_SEL]	    = &meson8b_vdec_hcodec_sel.hw,
	[CLKID_VDEC_HCODEC_DIV]	    = &meson8b_vdec_hcodec_div.hw,
	[CLKID_VDEC_HCODEC]	    = &meson8b_vdec_hcodec.hw,
	[CLKID_VDEC_2_SEL]	    = &meson8b_vdec_2_sel.hw,
	[CLKID_VDEC_2_DIV]	    = &meson8b_vdec_2_div.hw,
	[CLKID_VDEC_2]		    = &meson8b_vdec_2.hw,
	[CLKID_VDEC_HEVC_SEL]	    = &meson8b_vdec_hevc_sel.hw,
	[CLKID_VDEC_HEVC_DIV]	    = &meson8b_vdec_hevc_div.hw,
	[CLKID_VDEC_HEVC_EN]	    = &meson8b_vdec_hevc_en.hw,
	[CLKID_VDEC_HEVC]	    = &meson8b_vdec_hevc.hw,
	[CLKID_CTS_AMCLK_SEL]	    = &meson8b_cts_amclk_sel.hw,
	[CLKID_CTS_AMCLK_DIV]	    = &meson8b_cts_amclk_div.hw,
	[CLKID_CTS_AMCLK]	    = &meson8b_cts_amclk.hw,
	[CLKID_CTS_MCLK_I958_SEL]   = &meson8b_cts_mclk_i958_sel.hw,
	[CLKID_CTS_MCLK_I958_DIV]   = &meson8b_cts_mclk_i958_div.hw,
	[CLKID_CTS_MCLK_I958]	    = &meson8b_cts_mclk_i958.hw,
	[CLKID_CTS_I958]	    = &meson8b_cts_i958.hw,
	[CLKID_VID_PLL_LVDS_EN]	    = &meson8b_vid_pll_lvds_en.hw,
	[CLKID_HDMI_PLL_DCO_IN]	    = &hdmi_pll_dco_in.hw,
};

static struct clk_hw *meson8b_hw_clks[] = {
	[CLKID_PLL_FIXED]	    = &meson8b_fixed_pll.hw,
	[CLKID_PLL_VID]		    = &meson8b_vid_pll.hw,
	[CLKID_PLL_SYS]		    = &meson8b_sys_pll.hw,
	[CLKID_FCLK_DIV2]	    = &meson8b_fclk_div2.hw,
	[CLKID_FCLK_DIV3]	    = &meson8b_fclk_div3.hw,
	[CLKID_FCLK_DIV4]	    = &meson8b_fclk_div4.hw,
	[CLKID_FCLK_DIV5]	    = &meson8b_fclk_div5.hw,
	[CLKID_FCLK_DIV7]	    = &meson8b_fclk_div7.hw,
	[CLKID_CPUCLK]		    = &meson8b_cpu_clk.hw,
	[CLKID_MPEG_SEL]	    = &meson8b_clk81_sel.hw,
	[CLKID_MPEG_DIV]	    = &meson8b_clk81_div.hw,
	[CLKID_CLK81]		    = &meson8b_clk81.hw,
	[CLKID_DDR]		    = &meson8b_ddr.hw,
	[CLKID_DOS]		    = &meson8b_dos.hw,
	[CLKID_ISA]		    = &meson8b_isa.hw,
	[CLKID_PL301]		    = &meson8b_pl301.hw,
	[CLKID_PERIPHS]		    = &meson8b_periphs.hw,
	[CLKID_SPICC]		    = &meson8b_spicc.hw,
	[CLKID_I2C]		    = &meson8b_i2c.hw,
	[CLKID_SAR_ADC]		    = &meson8b_sar_adc.hw,
	[CLKID_SMART_CARD]	    = &meson8b_smart_card.hw,
	[CLKID_RNG0]		    = &meson8b_rng0.hw,
	[CLKID_UART0]		    = &meson8b_uart0.hw,
	[CLKID_SDHC]		    = &meson8b_sdhc.hw,
	[CLKID_STREAM]		    = &meson8b_stream.hw,
	[CLKID_ASYNC_FIFO]	    = &meson8b_async_fifo.hw,
	[CLKID_SDIO]		    = &meson8b_sdio.hw,
	[CLKID_ABUF]		    = &meson8b_abuf.hw,
	[CLKID_HIU_IFACE]	    = &meson8b_hiu_iface.hw,
	[CLKID_ASSIST_MISC]	    = &meson8b_assist_misc.hw,
	[CLKID_SPI]		    = &meson8b_spi.hw,
	[CLKID_I2S_SPDIF]	    = &meson8b_i2s_spdif.hw,
	[CLKID_ETH]		    = &meson8b_eth.hw,
	[CLKID_DEMUX]		    = &meson8b_demux.hw,
	[CLKID_AIU_GLUE]	    = &meson8b_aiu_glue.hw,
	[CLKID_IEC958]		    = &meson8b_iec958.hw,
	[CLKID_I2S_OUT]		    = &meson8b_i2s_out.hw,
	[CLKID_AMCLK]		    = &meson8b_amclk.hw,
	[CLKID_AIFIFO2]		    = &meson8b_aififo2.hw,
	[CLKID_MIXER]		    = &meson8b_mixer.hw,
	[CLKID_MIXER_IFACE]	    = &meson8b_mixer_iface.hw,
	[CLKID_ADC]		    = &meson8b_adc.hw,
	[CLKID_BLKMV]		    = &meson8b_blkmv.hw,
	[CLKID_AIU]		    = &meson8b_aiu.hw,
	[CLKID_UART1]		    = &meson8b_uart1.hw,
	[CLKID_G2D]		    = &meson8b_g2d.hw,
	[CLKID_USB0]		    = &meson8b_usb0.hw,
	[CLKID_USB1]		    = &meson8b_usb1.hw,
	[CLKID_RESET]		    = &meson8b_reset.hw,
	[CLKID_NAND]		    = &meson8b_nand.hw,
	[CLKID_DOS_PARSER]	    = &meson8b_dos_parser.hw,
	[CLKID_USB]		    = &meson8b_usb.hw,
	[CLKID_VDIN1]		    = &meson8b_vdin1.hw,
	[CLKID_AHB_ARB0]	    = &meson8b_ahb_arb0.hw,
	[CLKID_EFUSE]		    = &meson8b_efuse.hw,
	[CLKID_BOOT_ROM]	    = &meson8b_boot_rom.hw,
	[CLKID_AHB_DATA_BUS]	    = &meson8b_ahb_data_bus.hw,
	[CLKID_AHB_CTRL_BUS]	    = &meson8b_ahb_ctrl_bus.hw,
	[CLKID_HDMI_INTR_SYNC]	    = &meson8b_hdmi_intr_sync.hw,
	[CLKID_HDMI_PCLK]	    = &meson8b_hdmi_pclk.hw,
	[CLKID_USB1_DDR_BRIDGE]	    = &meson8b_usb1_ddr_bridge.hw,
	[CLKID_USB0_DDR_BRIDGE]	    = &meson8b_usb0_ddr_bridge.hw,
	[CLKID_MMC_PCLK]	    = &meson8b_mmc_pclk.hw,
	[CLKID_DVIN]		    = &meson8b_dvin.hw,
	[CLKID_UART2]		    = &meson8b_uart2.hw,
	[CLKID_SANA]		    = &meson8b_sana.hw,
	[CLKID_VPU_INTR]	    = &meson8b_vpu_intr.hw,
	[CLKID_SEC_AHB_AHB3_BRIDGE] = &meson8b_sec_ahb_ahb3_bridge.hw,
	[CLKID_CLK81_A9]	    = &meson8b_clk81_a9.hw,
	[CLKID_VCLK2_VENCI0]	    = &meson8b_vclk2_venci0.hw,
	[CLKID_VCLK2_VENCI1]	    = &meson8b_vclk2_venci1.hw,
	[CLKID_VCLK2_VENCP0]	    = &meson8b_vclk2_vencp0.hw,
	[CLKID_VCLK2_VENCP1]	    = &meson8b_vclk2_vencp1.hw,
	[CLKID_GCLK_VENCI_INT]	    = &meson8b_gclk_venci_int.hw,
	[CLKID_GCLK_VENCP_INT]	    = &meson8b_gclk_vencp_int.hw,
	[CLKID_DAC_CLK]		    = &meson8b_dac_clk.hw,
	[CLKID_AOCLK_GATE]	    = &meson8b_aoclk_gate.hw,
	[CLKID_IEC958_GATE]	    = &meson8b_iec958_gate.hw,
	[CLKID_ENC480P]		    = &meson8b_enc480p.hw,
	[CLKID_RNG1]		    = &meson8b_rng1.hw,
	[CLKID_GCLK_VENCL_INT]	    = &meson8b_gclk_vencl_int.hw,
	[CLKID_VCLK2_VENCLMCC]	    = &meson8b_vclk2_venclmcc.hw,
	[CLKID_VCLK2_VENCL]	    = &meson8b_vclk2_vencl.hw,
	[CLKID_VCLK2_OTHER]	    = &meson8b_vclk2_other.hw,
	[CLKID_EDP]		    = &meson8b_edp.hw,
	[CLKID_AO_MEDIA_CPU]	    = &meson8b_ao_media_cpu.hw,
	[CLKID_AO_AHB_SRAM]	    = &meson8b_ao_ahb_sram.hw,
	[CLKID_AO_AHB_BUS]	    = &meson8b_ao_ahb_bus.hw,
	[CLKID_AO_IFACE]	    = &meson8b_ao_iface.hw,
	[CLKID_MPLL0]		    = &meson8b_mpll0.hw,
	[CLKID_MPLL1]		    = &meson8b_mpll1.hw,
	[CLKID_MPLL2]		    = &meson8b_mpll2.hw,
	[CLKID_MPLL0_DIV]	    = &meson8b_mpll0_div.hw,
	[CLKID_MPLL1_DIV]	    = &meson8b_mpll1_div.hw,
	[CLKID_MPLL2_DIV]	    = &meson8b_mpll2_div.hw,
	[CLKID_CPU_IN_SEL]	    = &meson8b_cpu_in_sel.hw,
	[CLKID_CPU_IN_DIV2]	    = &meson8b_cpu_in_div2.hw,
	[CLKID_CPU_IN_DIV3]	    = &meson8b_cpu_in_div3.hw,
	[CLKID_CPU_SCALE_DIV]	    = &meson8b_cpu_scale_div.hw,
	[CLKID_CPU_SCALE_OUT_SEL]   = &meson8b_cpu_scale_out_sel.hw,
	[CLKID_MPLL_PREDIV]	    = &meson8b_mpll_prediv.hw,
	[CLKID_FCLK_DIV2_DIV]	    = &meson8b_fclk_div2_div.hw,
	[CLKID_FCLK_DIV3_DIV]	    = &meson8b_fclk_div3_div.hw,
	[CLKID_FCLK_DIV4_DIV]	    = &meson8b_fclk_div4_div.hw,
	[CLKID_FCLK_DIV5_DIV]	    = &meson8b_fclk_div5_div.hw,
	[CLKID_FCLK_DIV7_DIV]	    = &meson8b_fclk_div7_div.hw,
	[CLKID_NAND_SEL]	    = &meson8b_nand_clk_sel.hw,
	[CLKID_NAND_DIV]	    = &meson8b_nand_clk_div.hw,
	[CLKID_NAND_CLK]	    = &meson8b_nand_clk.hw,
	[CLKID_PLL_FIXED_DCO]	    = &meson8b_fixed_pll_dco.hw,
	[CLKID_HDMI_PLL_DCO]	    = &meson8b_hdmi_pll_dco.hw,
	[CLKID_PLL_SYS_DCO]	    = &meson8b_sys_pll_dco.hw,
	[CLKID_CPU_CLK_DIV2]	    = &meson8b_cpu_clk_div2.hw,
	[CLKID_CPU_CLK_DIV3]	    = &meson8b_cpu_clk_div3.hw,
	[CLKID_CPU_CLK_DIV4]	    = &meson8b_cpu_clk_div4.hw,
	[CLKID_CPU_CLK_DIV5]	    = &meson8b_cpu_clk_div5.hw,
	[CLKID_CPU_CLK_DIV6]	    = &meson8b_cpu_clk_div6.hw,
	[CLKID_CPU_CLK_DIV7]	    = &meson8b_cpu_clk_div7.hw,
	[CLKID_CPU_CLK_DIV8]	    = &meson8b_cpu_clk_div8.hw,
	[CLKID_APB_SEL]		    = &meson8b_apb_sel.hw,
	[CLKID_APB]		    = &meson8b_apb.hw,
	[CLKID_PERIPH_SEL]	    = &meson8b_periph_sel.hw,
	[CLKID_PERIPH]		    = &meson8b_periph.hw,
	[CLKID_AXI_SEL]		    = &meson8b_axi_sel.hw,
	[CLKID_AXI]		    = &meson8b_axi.hw,
	[CLKID_L2_DRAM_SEL]	    = &meson8b_l2_dram_sel.hw,
	[CLKID_L2_DRAM]		    = &meson8b_l2_dram.hw,
	[CLKID_HDMI_PLL_LVDS_OUT]   = &meson8b_hdmi_pll_lvds_out.hw,
	[CLKID_HDMI_PLL_HDMI_OUT]   = &meson8b_hdmi_pll_hdmi_out.hw,
	[CLKID_VID_PLL_IN_SEL]	    = &meson8b_vid_pll_in_sel.hw,
	[CLKID_VID_PLL_IN_EN]	    = &meson8b_vid_pll_in_en.hw,
	[CLKID_VID_PLL_PRE_DIV]	    = &meson8b_vid_pll_pre_div.hw,
	[CLKID_VID_PLL_POST_DIV]    = &meson8b_vid_pll_post_div.hw,
	[CLKID_VID_PLL_FINAL_DIV]   = &meson8b_vid_pll_final_div.hw,
	[CLKID_VCLK_IN_SEL]	    = &meson8b_vclk_in_sel.hw,
	[CLKID_VCLK_IN_EN]	    = &meson8b_vclk_in_en.hw,
	[CLKID_VCLK_EN]		    = &meson8b_vclk_en.hw,
	[CLKID_VCLK_DIV1]	    = &meson8b_vclk_div1.hw,
	[CLKID_VCLK_DIV2_DIV]	    = &meson8b_vclk_div2_div.hw,
	[CLKID_VCLK_DIV2]	    = &meson8b_vclk_div2.hw,
	[CLKID_VCLK_DIV4_DIV]	    = &meson8b_vclk_div4_div.hw,
	[CLKID_VCLK_DIV4]	    = &meson8b_vclk_div4.hw,
	[CLKID_VCLK_DIV6_DIV]	    = &meson8b_vclk_div6_div.hw,
	[CLKID_VCLK_DIV6]	    = &meson8b_vclk_div6.hw,
	[CLKID_VCLK_DIV12_DIV]	    = &meson8b_vclk_div12_div.hw,
	[CLKID_VCLK_DIV12]	    = &meson8b_vclk_div12.hw,
	[CLKID_VCLK2_IN_SEL]	    = &meson8b_vclk2_in_sel.hw,
	[CLKID_VCLK2_IN_EN]	    = &meson8b_vclk2_in_en.hw,
	[CLKID_VCLK2_EN]	    = &meson8b_vclk2_en.hw,
	[CLKID_VCLK2_DIV1]	    = &meson8b_vclk2_div1.hw,
	[CLKID_VCLK2_DIV2_DIV]	    = &meson8b_vclk2_div2_div.hw,
	[CLKID_VCLK2_DIV2]	    = &meson8b_vclk2_div2.hw,
	[CLKID_VCLK2_DIV4_DIV]	    = &meson8b_vclk2_div4_div.hw,
	[CLKID_VCLK2_DIV4]	    = &meson8b_vclk2_div4.hw,
	[CLKID_VCLK2_DIV6_DIV]	    = &meson8b_vclk2_div6_div.hw,
	[CLKID_VCLK2_DIV6]	    = &meson8b_vclk2_div6.hw,
	[CLKID_VCLK2_DIV12_DIV]	    = &meson8b_vclk2_div12_div.hw,
	[CLKID_VCLK2_DIV12]	    = &meson8b_vclk2_div12.hw,
	[CLKID_CTS_ENCT_SEL]	    = &meson8b_cts_enct_sel.hw,
	[CLKID_CTS_ENCT]	    = &meson8b_cts_enct.hw,
	[CLKID_CTS_ENCP_SEL]	    = &meson8b_cts_encp_sel.hw,
	[CLKID_CTS_ENCP]	    = &meson8b_cts_encp.hw,
	[CLKID_CTS_ENCI_SEL]	    = &meson8b_cts_enci_sel.hw,
	[CLKID_CTS_ENCI]	    = &meson8b_cts_enci.hw,
	[CLKID_HDMI_TX_PIXEL_SEL]   = &meson8b_hdmi_tx_pixel_sel.hw,
	[CLKID_HDMI_TX_PIXEL]	    = &meson8b_hdmi_tx_pixel.hw,
	[CLKID_CTS_ENCL_SEL]	    = &meson8b_cts_encl_sel.hw,
	[CLKID_CTS_ENCL]	    = &meson8b_cts_encl.hw,
	[CLKID_CTS_VDAC0_SEL]	    = &meson8b_cts_vdac0_sel.hw,
	[CLKID_CTS_VDAC0]	    = &meson8b_cts_vdac0.hw,
	[CLKID_HDMI_SYS_SEL]	    = &meson8b_hdmi_sys_sel.hw,
	[CLKID_HDMI_SYS_DIV]	    = &meson8b_hdmi_sys_div.hw,
	[CLKID_HDMI_SYS]	    = &meson8b_hdmi_sys.hw,
	[CLKID_MALI_0_SEL]	    = &meson8b_mali_0_sel.hw,
	[CLKID_MALI_0_DIV]	    = &meson8b_mali_0_div.hw,
	[CLKID_MALI_0]		    = &meson8b_mali_0.hw,
	[CLKID_MALI_1_SEL]	    = &meson8b_mali_1_sel.hw,
	[CLKID_MALI_1_DIV]	    = &meson8b_mali_1_div.hw,
	[CLKID_MALI_1]		    = &meson8b_mali_1.hw,
	[CLKID_MALI]		    = &meson8b_mali.hw,
	[CLKID_VPU_0_SEL]	    = &meson8b_vpu_0_sel.hw,
	[CLKID_VPU_0_DIV]	    = &meson8b_vpu_0_div.hw,
	[CLKID_VPU_0]		    = &meson8b_vpu_0.hw,
	[CLKID_VPU_1_SEL]	    = &meson8b_vpu_1_sel.hw,
	[CLKID_VPU_1_DIV]	    = &meson8b_vpu_1_div.hw,
	[CLKID_VPU_1]		    = &meson8b_vpu_1.hw,
	[CLKID_VPU]		    = &meson8b_vpu.hw,
	[CLKID_VDEC_1_SEL]	    = &meson8b_vdec_1_sel.hw,
	[CLKID_VDEC_1_1_DIV]	    = &meson8b_vdec_1_1_div.hw,
	[CLKID_VDEC_1_1]	    = &meson8b_vdec_1_1.hw,
	[CLKID_VDEC_1_2_DIV]	    = &meson8b_vdec_1_2_div.hw,
	[CLKID_VDEC_1_2]	    = &meson8b_vdec_1_2.hw,
	[CLKID_VDEC_1]		    = &meson8b_vdec_1.hw,
	[CLKID_VDEC_HCODEC_SEL]	    = &meson8b_vdec_hcodec_sel.hw,
	[CLKID_VDEC_HCODEC_DIV]	    = &meson8b_vdec_hcodec_div.hw,
	[CLKID_VDEC_HCODEC]	    = &meson8b_vdec_hcodec.hw,
	[CLKID_VDEC_2_SEL]	    = &meson8b_vdec_2_sel.hw,
	[CLKID_VDEC_2_DIV]	    = &meson8b_vdec_2_div.hw,
	[CLKID_VDEC_2]		    = &meson8b_vdec_2.hw,
	[CLKID_VDEC_HEVC_SEL]	    = &meson8b_vdec_hevc_sel.hw,
	[CLKID_VDEC_HEVC_DIV]	    = &meson8b_vdec_hevc_div.hw,
	[CLKID_VDEC_HEVC_EN]	    = &meson8b_vdec_hevc_en.hw,
	[CLKID_VDEC_HEVC]	    = &meson8b_vdec_hevc.hw,
	[CLKID_CTS_AMCLK_SEL]	    = &meson8b_cts_amclk_sel.hw,
	[CLKID_CTS_AMCLK_DIV]	    = &meson8b_cts_amclk_div.hw,
	[CLKID_CTS_AMCLK]	    = &meson8b_cts_amclk.hw,
	[CLKID_CTS_MCLK_I958_SEL]   = &meson8b_cts_mclk_i958_sel.hw,
	[CLKID_CTS_MCLK_I958_DIV]   = &meson8b_cts_mclk_i958_div.hw,
	[CLKID_CTS_MCLK_I958]	    = &meson8b_cts_mclk_i958.hw,
	[CLKID_CTS_I958]	    = &meson8b_cts_i958.hw,
	[CLKID_VID_PLL_LVDS_EN]	    = &meson8b_vid_pll_lvds_en.hw,
	[CLKID_HDMI_PLL_DCO_IN]	    = &hdmi_pll_dco_in.hw,
};

static struct clk_hw *meson8m2_hw_clks[] = {
	[CLKID_PLL_FIXED]	    = &meson8b_fixed_pll.hw,
	[CLKID_PLL_VID]		    = &meson8b_vid_pll.hw,
	[CLKID_PLL_SYS]		    = &meson8b_sys_pll.hw,
	[CLKID_FCLK_DIV2]	    = &meson8b_fclk_div2.hw,
	[CLKID_FCLK_DIV3]	    = &meson8b_fclk_div3.hw,
	[CLKID_FCLK_DIV4]	    = &meson8b_fclk_div4.hw,
	[CLKID_FCLK_DIV5]	    = &meson8b_fclk_div5.hw,
	[CLKID_FCLK_DIV7]	    = &meson8b_fclk_div7.hw,
	[CLKID_CPUCLK]		    = &meson8b_cpu_clk.hw,
	[CLKID_MPEG_SEL]	    = &meson8b_clk81_sel.hw,
	[CLKID_MPEG_DIV]	    = &meson8b_clk81_div.hw,
	[CLKID_CLK81]		    = &meson8b_clk81.hw,
	[CLKID_DDR]		    = &meson8b_ddr.hw,
	[CLKID_DOS]		    = &meson8b_dos.hw,
	[CLKID_ISA]		    = &meson8b_isa.hw,
	[CLKID_PL301]		    = &meson8b_pl301.hw,
	[CLKID_PERIPHS]		    = &meson8b_periphs.hw,
	[CLKID_SPICC]		    = &meson8b_spicc.hw,
	[CLKID_I2C]		    = &meson8b_i2c.hw,
	[CLKID_SAR_ADC]		    = &meson8b_sar_adc.hw,
	[CLKID_SMART_CARD]	    = &meson8b_smart_card.hw,
	[CLKID_RNG0]		    = &meson8b_rng0.hw,
	[CLKID_UART0]		    = &meson8b_uart0.hw,
	[CLKID_SDHC]		    = &meson8b_sdhc.hw,
	[CLKID_STREAM]		    = &meson8b_stream.hw,
	[CLKID_ASYNC_FIFO]	    = &meson8b_async_fifo.hw,
	[CLKID_SDIO]		    = &meson8b_sdio.hw,
	[CLKID_ABUF]		    = &meson8b_abuf.hw,
	[CLKID_HIU_IFACE]	    = &meson8b_hiu_iface.hw,
	[CLKID_ASSIST_MISC]	    = &meson8b_assist_misc.hw,
	[CLKID_SPI]		    = &meson8b_spi.hw,
	[CLKID_I2S_SPDIF]	    = &meson8b_i2s_spdif.hw,
	[CLKID_ETH]		    = &meson8b_eth.hw,
	[CLKID_DEMUX]		    = &meson8b_demux.hw,
	[CLKID_AIU_GLUE]	    = &meson8b_aiu_glue.hw,
	[CLKID_IEC958]		    = &meson8b_iec958.hw,
	[CLKID_I2S_OUT]		    = &meson8b_i2s_out.hw,
	[CLKID_AMCLK]		    = &meson8b_amclk.hw,
	[CLKID_AIFIFO2]		    = &meson8b_aififo2.hw,
	[CLKID_MIXER]		    = &meson8b_mixer.hw,
	[CLKID_MIXER_IFACE]	    = &meson8b_mixer_iface.hw,
	[CLKID_ADC]		    = &meson8b_adc.hw,
	[CLKID_BLKMV]		    = &meson8b_blkmv.hw,
	[CLKID_AIU]		    = &meson8b_aiu.hw,
	[CLKID_UART1]		    = &meson8b_uart1.hw,
	[CLKID_G2D]		    = &meson8b_g2d.hw,
	[CLKID_USB0]		    = &meson8b_usb0.hw,
	[CLKID_USB1]		    = &meson8b_usb1.hw,
	[CLKID_RESET]		    = &meson8b_reset.hw,
	[CLKID_NAND]		    = &meson8b_nand.hw,
	[CLKID_DOS_PARSER]	    = &meson8b_dos_parser.hw,
	[CLKID_USB]		    = &meson8b_usb.hw,
	[CLKID_VDIN1]		    = &meson8b_vdin1.hw,
	[CLKID_AHB_ARB0]	    = &meson8b_ahb_arb0.hw,
	[CLKID_EFUSE]		    = &meson8b_efuse.hw,
	[CLKID_BOOT_ROM]	    = &meson8b_boot_rom.hw,
	[CLKID_AHB_DATA_BUS]	    = &meson8b_ahb_data_bus.hw,
	[CLKID_AHB_CTRL_BUS]	    = &meson8b_ahb_ctrl_bus.hw,
	[CLKID_HDMI_INTR_SYNC]	    = &meson8b_hdmi_intr_sync.hw,
	[CLKID_HDMI_PCLK]	    = &meson8b_hdmi_pclk.hw,
	[CLKID_USB1_DDR_BRIDGE]	    = &meson8b_usb1_ddr_bridge.hw,
	[CLKID_USB0_DDR_BRIDGE]	    = &meson8b_usb0_ddr_bridge.hw,
	[CLKID_MMC_PCLK]	    = &meson8b_mmc_pclk.hw,
	[CLKID_DVIN]		    = &meson8b_dvin.hw,
	[CLKID_UART2]		    = &meson8b_uart2.hw,
	[CLKID_SANA]		    = &meson8b_sana.hw,
	[CLKID_VPU_INTR]	    = &meson8b_vpu_intr.hw,
	[CLKID_SEC_AHB_AHB3_BRIDGE] = &meson8b_sec_ahb_ahb3_bridge.hw,
	[CLKID_CLK81_A9]	    = &meson8b_clk81_a9.hw,
	[CLKID_VCLK2_VENCI0]	    = &meson8b_vclk2_venci0.hw,
	[CLKID_VCLK2_VENCI1]	    = &meson8b_vclk2_venci1.hw,
	[CLKID_VCLK2_VENCP0]	    = &meson8b_vclk2_vencp0.hw,
	[CLKID_VCLK2_VENCP1]	    = &meson8b_vclk2_vencp1.hw,
	[CLKID_GCLK_VENCI_INT]	    = &meson8b_gclk_venci_int.hw,
	[CLKID_GCLK_VENCP_INT]	    = &meson8b_gclk_vencp_int.hw,
	[CLKID_DAC_CLK]		    = &meson8b_dac_clk.hw,
	[CLKID_AOCLK_GATE]	    = &meson8b_aoclk_gate.hw,
	[CLKID_IEC958_GATE]	    = &meson8b_iec958_gate.hw,
	[CLKID_ENC480P]		    = &meson8b_enc480p.hw,
	[CLKID_RNG1]		    = &meson8b_rng1.hw,
	[CLKID_GCLK_VENCL_INT]	    = &meson8b_gclk_vencl_int.hw,
	[CLKID_VCLK2_VENCLMCC]	    = &meson8b_vclk2_venclmcc.hw,
	[CLKID_VCLK2_VENCL]	    = &meson8b_vclk2_vencl.hw,
	[CLKID_VCLK2_OTHER]	    = &meson8b_vclk2_other.hw,
	[CLKID_EDP]		    = &meson8b_edp.hw,
	[CLKID_AO_MEDIA_CPU]	    = &meson8b_ao_media_cpu.hw,
	[CLKID_AO_AHB_SRAM]	    = &meson8b_ao_ahb_sram.hw,
	[CLKID_AO_AHB_BUS]	    = &meson8b_ao_ahb_bus.hw,
	[CLKID_AO_IFACE]	    = &meson8b_ao_iface.hw,
	[CLKID_MPLL0]		    = &meson8b_mpll0.hw,
	[CLKID_MPLL1]		    = &meson8b_mpll1.hw,
	[CLKID_MPLL2]		    = &meson8b_mpll2.hw,
	[CLKID_MPLL0_DIV]	    = &meson8b_mpll0_div.hw,
	[CLKID_MPLL1_DIV]	    = &meson8b_mpll1_div.hw,
	[CLKID_MPLL2_DIV]	    = &meson8b_mpll2_div.hw,
	[CLKID_CPU_IN_SEL]	    = &meson8b_cpu_in_sel.hw,
	[CLKID_CPU_IN_DIV2]	    = &meson8b_cpu_in_div2.hw,
	[CLKID_CPU_IN_DIV3]	    = &meson8b_cpu_in_div3.hw,
	[CLKID_CPU_SCALE_DIV]	    = &meson8b_cpu_scale_div.hw,
	[CLKID_CPU_SCALE_OUT_SEL]   = &meson8b_cpu_scale_out_sel.hw,
	[CLKID_MPLL_PREDIV]	    = &meson8b_mpll_prediv.hw,
	[CLKID_FCLK_DIV2_DIV]	    = &meson8b_fclk_div2_div.hw,
	[CLKID_FCLK_DIV3_DIV]	    = &meson8b_fclk_div3_div.hw,
	[CLKID_FCLK_DIV4_DIV]	    = &meson8b_fclk_div4_div.hw,
	[CLKID_FCLK_DIV5_DIV]	    = &meson8b_fclk_div5_div.hw,
	[CLKID_FCLK_DIV7_DIV]	    = &meson8b_fclk_div7_div.hw,
	[CLKID_NAND_SEL]	    = &meson8b_nand_clk_sel.hw,
	[CLKID_NAND_DIV]	    = &meson8b_nand_clk_div.hw,
	[CLKID_NAND_CLK]	    = &meson8b_nand_clk.hw,
	[CLKID_PLL_FIXED_DCO]	    = &meson8b_fixed_pll_dco.hw,
	[CLKID_HDMI_PLL_DCO]	    = &meson8b_hdmi_pll_dco.hw,
	[CLKID_PLL_SYS_DCO]	    = &meson8b_sys_pll_dco.hw,
	[CLKID_CPU_CLK_DIV2]	    = &meson8b_cpu_clk_div2.hw,
	[CLKID_CPU_CLK_DIV3]	    = &meson8b_cpu_clk_div3.hw,
	[CLKID_CPU_CLK_DIV4]	    = &meson8b_cpu_clk_div4.hw,
	[CLKID_CPU_CLK_DIV5]	    = &meson8b_cpu_clk_div5.hw,
	[CLKID_CPU_CLK_DIV6]	    = &meson8b_cpu_clk_div6.hw,
	[CLKID_CPU_CLK_DIV7]	    = &meson8b_cpu_clk_div7.hw,
	[CLKID_CPU_CLK_DIV8]	    = &meson8b_cpu_clk_div8.hw,
	[CLKID_APB_SEL]		    = &meson8b_apb_sel.hw,
	[CLKID_APB]		    = &meson8b_apb.hw,
	[CLKID_PERIPH_SEL]	    = &meson8b_periph_sel.hw,
	[CLKID_PERIPH]		    = &meson8b_periph.hw,
	[CLKID_AXI_SEL]		    = &meson8b_axi_sel.hw,
	[CLKID_AXI]		    = &meson8b_axi.hw,
	[CLKID_L2_DRAM_SEL]	    = &meson8b_l2_dram_sel.hw,
	[CLKID_L2_DRAM]		    = &meson8b_l2_dram.hw,
	[CLKID_HDMI_PLL_LVDS_OUT]   = &meson8b_hdmi_pll_lvds_out.hw,
	[CLKID_HDMI_PLL_HDMI_OUT]   = &meson8b_hdmi_pll_hdmi_out.hw,
	[CLKID_VID_PLL_IN_SEL]	    = &meson8b_vid_pll_in_sel.hw,
	[CLKID_VID_PLL_IN_EN]	    = &meson8b_vid_pll_in_en.hw,
	[CLKID_VID_PLL_PRE_DIV]	    = &meson8b_vid_pll_pre_div.hw,
	[CLKID_VID_PLL_POST_DIV]    = &meson8b_vid_pll_post_div.hw,
	[CLKID_VID_PLL_FINAL_DIV]   = &meson8b_vid_pll_final_div.hw,
	[CLKID_VCLK_IN_SEL]	    = &meson8b_vclk_in_sel.hw,
	[CLKID_VCLK_IN_EN]	    = &meson8b_vclk_in_en.hw,
	[CLKID_VCLK_EN]		    = &meson8b_vclk_en.hw,
	[CLKID_VCLK_DIV1]	    = &meson8b_vclk_div1.hw,
	[CLKID_VCLK_DIV2_DIV]	    = &meson8b_vclk_div2_div.hw,
	[CLKID_VCLK_DIV2]	    = &meson8b_vclk_div2.hw,
	[CLKID_VCLK_DIV4_DIV]	    = &meson8b_vclk_div4_div.hw,
	[CLKID_VCLK_DIV4]	    = &meson8b_vclk_div4.hw,
	[CLKID_VCLK_DIV6_DIV]	    = &meson8b_vclk_div6_div.hw,
	[CLKID_VCLK_DIV6]	    = &meson8b_vclk_div6.hw,
	[CLKID_VCLK_DIV12_DIV]	    = &meson8b_vclk_div12_div.hw,
	[CLKID_VCLK_DIV12]	    = &meson8b_vclk_div12.hw,
	[CLKID_VCLK2_IN_SEL]	    = &meson8b_vclk2_in_sel.hw,
	[CLKID_VCLK2_IN_EN]	    = &meson8b_vclk2_in_en.hw,
	[CLKID_VCLK2_EN]	    = &meson8b_vclk2_en.hw,
	[CLKID_VCLK2_DIV1]	    = &meson8b_vclk2_div1.hw,
	[CLKID_VCLK2_DIV2_DIV]	    = &meson8b_vclk2_div2_div.hw,
	[CLKID_VCLK2_DIV2]	    = &meson8b_vclk2_div2.hw,
	[CLKID_VCLK2_DIV4_DIV]	    = &meson8b_vclk2_div4_div.hw,
	[CLKID_VCLK2_DIV4]	    = &meson8b_vclk2_div4.hw,
	[CLKID_VCLK2_DIV6_DIV]	    = &meson8b_vclk2_div6_div.hw,
	[CLKID_VCLK2_DIV6]	    = &meson8b_vclk2_div6.hw,
	[CLKID_VCLK2_DIV12_DIV]	    = &meson8b_vclk2_div12_div.hw,
	[CLKID_VCLK2_DIV12]	    = &meson8b_vclk2_div12.hw,
	[CLKID_CTS_ENCT_SEL]	    = &meson8b_cts_enct_sel.hw,
	[CLKID_CTS_ENCT]	    = &meson8b_cts_enct.hw,
	[CLKID_CTS_ENCP_SEL]	    = &meson8b_cts_encp_sel.hw,
	[CLKID_CTS_ENCP]	    = &meson8b_cts_encp.hw,
	[CLKID_CTS_ENCI_SEL]	    = &meson8b_cts_enci_sel.hw,
	[CLKID_CTS_ENCI]	    = &meson8b_cts_enci.hw,
	[CLKID_HDMI_TX_PIXEL_SEL]   = &meson8b_hdmi_tx_pixel_sel.hw,
	[CLKID_HDMI_TX_PIXEL]	    = &meson8b_hdmi_tx_pixel.hw,
	[CLKID_CTS_ENCL_SEL]	    = &meson8b_cts_encl_sel.hw,
	[CLKID_CTS_ENCL]	    = &meson8b_cts_encl.hw,
	[CLKID_CTS_VDAC0_SEL]	    = &meson8b_cts_vdac0_sel.hw,
	[CLKID_CTS_VDAC0]	    = &meson8b_cts_vdac0.hw,
	[CLKID_HDMI_SYS_SEL]	    = &meson8b_hdmi_sys_sel.hw,
	[CLKID_HDMI_SYS_DIV]	    = &meson8b_hdmi_sys_div.hw,
	[CLKID_HDMI_SYS]	    = &meson8b_hdmi_sys.hw,
	[CLKID_MALI_0_SEL]	    = &meson8b_mali_0_sel.hw,
	[CLKID_MALI_0_DIV]	    = &meson8b_mali_0_div.hw,
	[CLKID_MALI_0]		    = &meson8b_mali_0.hw,
	[CLKID_MALI_1_SEL]	    = &meson8b_mali_1_sel.hw,
	[CLKID_MALI_1_DIV]	    = &meson8b_mali_1_div.hw,
	[CLKID_MALI_1]		    = &meson8b_mali_1.hw,
	[CLKID_MALI]		    = &meson8b_mali.hw,
	[CLKID_GP_PLL_DCO]	    = &meson8m2_gp_pll_dco.hw,
	[CLKID_GP_PLL]		    = &meson8m2_gp_pll.hw,
	[CLKID_VPU_0_SEL]	    = &meson8m2_vpu_0_sel.hw,
	[CLKID_VPU_0_DIV]	    = &meson8b_vpu_0_div.hw,
	[CLKID_VPU_0]		    = &meson8b_vpu_0.hw,
	[CLKID_VPU_1_SEL]	    = &meson8m2_vpu_1_sel.hw,
	[CLKID_VPU_1_DIV]	    = &meson8b_vpu_1_div.hw,
	[CLKID_VPU_1]		    = &meson8b_vpu_1.hw,
	[CLKID_VPU]		    = &meson8b_vpu.hw,
	[CLKID_VDEC_1_SEL]	    = &meson8b_vdec_1_sel.hw,
	[CLKID_VDEC_1_1_DIV]	    = &meson8b_vdec_1_1_div.hw,
	[CLKID_VDEC_1_1]	    = &meson8b_vdec_1_1.hw,
	[CLKID_VDEC_1_2_DIV]	    = &meson8b_vdec_1_2_div.hw,
	[CLKID_VDEC_1_2]	    = &meson8b_vdec_1_2.hw,
	[CLKID_VDEC_1]		    = &meson8b_vdec_1.hw,
	[CLKID_VDEC_HCODEC_SEL]	    = &meson8b_vdec_hcodec_sel.hw,
	[CLKID_VDEC_HCODEC_DIV]	    = &meson8b_vdec_hcodec_div.hw,
	[CLKID_VDEC_HCODEC]	    = &meson8b_vdec_hcodec.hw,
	[CLKID_VDEC_2_SEL]	    = &meson8b_vdec_2_sel.hw,
	[CLKID_VDEC_2_DIV]	    = &meson8b_vdec_2_div.hw,
	[CLKID_VDEC_2]		    = &meson8b_vdec_2.hw,
	[CLKID_VDEC_HEVC_SEL]	    = &meson8b_vdec_hevc_sel.hw,
	[CLKID_VDEC_HEVC_DIV]	    = &meson8b_vdec_hevc_div.hw,
	[CLKID_VDEC_HEVC_EN]	    = &meson8b_vdec_hevc_en.hw,
	[CLKID_VDEC_HEVC]	    = &meson8b_vdec_hevc.hw,
	[CLKID_CTS_AMCLK_SEL]	    = &meson8b_cts_amclk_sel.hw,
	[CLKID_CTS_AMCLK_DIV]	    = &meson8b_cts_amclk_div.hw,
	[CLKID_CTS_AMCLK]	    = &meson8b_cts_amclk.hw,
	[CLKID_CTS_MCLK_I958_SEL]   = &meson8b_cts_mclk_i958_sel.hw,
	[CLKID_CTS_MCLK_I958_DIV]   = &meson8b_cts_mclk_i958_div.hw,
	[CLKID_CTS_MCLK_I958]	    = &meson8b_cts_mclk_i958.hw,
	[CLKID_CTS_I958]	    = &meson8b_cts_i958.hw,
	[CLKID_VID_PLL_LVDS_EN]	    = &meson8b_vid_pll_lvds_en.hw,
	[CLKID_HDMI_PLL_DCO_IN]	    = &hdmi_pll_dco_in.hw,
};

static const struct meson8b_clk_reset_line {
	u32 reg;
	u8 bit_idx;
	bool active_low;
} meson8b_clk_reset_bits[] = {
	[CLKC_RESET_L2_CACHE_SOFT_RESET] = {
		.reg = HHI_SYS_CPU_CLK_CNTL0,
		.bit_idx = 30,
		.active_low = false,
	},
	[CLKC_RESET_AXI_64_TO_128_BRIDGE_A5_SOFT_RESET] = {
		.reg = HHI_SYS_CPU_CLK_CNTL0,
		.bit_idx = 29,
		.active_low = false,
	},
	[CLKC_RESET_SCU_SOFT_RESET] = {
		.reg = HHI_SYS_CPU_CLK_CNTL0,
		.bit_idx = 28,
		.active_low = false,
	},
	[CLKC_RESET_CPU3_SOFT_RESET] = {
		.reg = HHI_SYS_CPU_CLK_CNTL0,
		.bit_idx = 27,
		.active_low = false,
	},
	[CLKC_RESET_CPU2_SOFT_RESET] = {
		.reg = HHI_SYS_CPU_CLK_CNTL0,
		.bit_idx = 26,
		.active_low = false,
	},
	[CLKC_RESET_CPU1_SOFT_RESET] = {
		.reg = HHI_SYS_CPU_CLK_CNTL0,
		.bit_idx = 25,
		.active_low = false,
	},
	[CLKC_RESET_CPU0_SOFT_RESET] = {
		.reg = HHI_SYS_CPU_CLK_CNTL0,
		.bit_idx = 24,
		.active_low = false,
	},
	[CLKC_RESET_A5_GLOBAL_RESET] = {
		.reg = HHI_SYS_CPU_CLK_CNTL0,
		.bit_idx = 18,
		.active_low = false,
	},
	[CLKC_RESET_A5_AXI_SOFT_RESET] = {
		.reg = HHI_SYS_CPU_CLK_CNTL0,
		.bit_idx = 17,
		.active_low = false,
	},
	[CLKC_RESET_A5_ABP_SOFT_RESET] = {
		.reg = HHI_SYS_CPU_CLK_CNTL0,
		.bit_idx = 16,
		.active_low = false,
	},
	[CLKC_RESET_AXI_64_TO_128_BRIDGE_MMC_SOFT_RESET] = {
		.reg = HHI_SYS_CPU_CLK_CNTL1,
		.bit_idx = 30,
		.active_low = false,
	},
	[CLKC_RESET_VID_CLK_CNTL_SOFT_RESET] = {
		.reg = HHI_VID_CLK_CNTL,
		.bit_idx = 15,
		.active_low = false,
	},
	[CLKC_RESET_VID_DIVIDER_CNTL_SOFT_RESET_POST] = {
		.reg = HHI_VID_DIVIDER_CNTL,
		.bit_idx = 7,
		.active_low = false,
	},
	[CLKC_RESET_VID_DIVIDER_CNTL_SOFT_RESET_PRE] = {
		.reg = HHI_VID_DIVIDER_CNTL,
		.bit_idx = 3,
		.active_low = false,
	},
	[CLKC_RESET_VID_DIVIDER_CNTL_RESET_N_POST] = {
		.reg = HHI_VID_DIVIDER_CNTL,
		.bit_idx = 1,
		.active_low = true,
	},
	[CLKC_RESET_VID_DIVIDER_CNTL_RESET_N_PRE] = {
		.reg = HHI_VID_DIVIDER_CNTL,
		.bit_idx = 0,
		.active_low = true,
	},
};

static int meson8b_clk_reset_update(struct reset_controller_dev *rcdev,
				    unsigned long id, bool assert)
{
	struct meson8b_clk_reset *meson8b_clk_reset =
		container_of(rcdev, struct meson8b_clk_reset, reset);
	const struct meson8b_clk_reset_line *reset;
	unsigned int value = 0;

	if (id >= ARRAY_SIZE(meson8b_clk_reset_bits))
		return -EINVAL;

	reset = &meson8b_clk_reset_bits[id];

	if (assert != reset->active_low)
		value = BIT(reset->bit_idx);

	regmap_update_bits(meson8b_clk_reset->regmap, reset->reg,
			   BIT(reset->bit_idx), value);

	return 0;
}

static int meson8b_clk_reset_assert(struct reset_controller_dev *rcdev,
				     unsigned long id)
{
	return meson8b_clk_reset_update(rcdev, id, true);
}

static int meson8b_clk_reset_deassert(struct reset_controller_dev *rcdev,
				       unsigned long id)
{
	return meson8b_clk_reset_update(rcdev, id, false);
}

static const struct reset_control_ops meson8b_clk_reset_ops = {
	.assert = meson8b_clk_reset_assert,
	.deassert = meson8b_clk_reset_deassert,
};

struct meson8b_nb_data {
	struct notifier_block nb;
	struct clk_hw *cpu_clk;
};

static int meson8b_cpu_clk_notifier_cb(struct notifier_block *nb,
				       unsigned long event, void *data)
{
	struct meson8b_nb_data *nb_data =
		container_of(nb, struct meson8b_nb_data, nb);
	struct clk_hw *parent_clk;
	int ret;

	switch (event) {
	case PRE_RATE_CHANGE:
		/* xtal */
		parent_clk = clk_hw_get_parent_by_index(nb_data->cpu_clk, 0);
		break;

	case POST_RATE_CHANGE:
		/* cpu_scale_out_sel */
		parent_clk = clk_hw_get_parent_by_index(nb_data->cpu_clk, 1);
		break;

	default:
		return NOTIFY_DONE;
	}

	ret = clk_hw_set_parent(nb_data->cpu_clk, parent_clk);
	if (ret)
		return notifier_from_errno(ret);

	udelay(100);

	return NOTIFY_OK;
}

static struct meson8b_nb_data meson8b_cpu_nb_data = {
	.nb.notifier_call = meson8b_cpu_clk_notifier_cb,
};

static struct meson_clk_hw_data meson8_clks = {
	.hws = meson8_hw_clks,
	.num = ARRAY_SIZE(meson8_hw_clks),
};

static struct meson_clk_hw_data meson8b_clks = {
	.hws = meson8b_hw_clks,
	.num = ARRAY_SIZE(meson8b_hw_clks),
};

static struct meson_clk_hw_data meson8m2_clks = {
	.hws = meson8m2_hw_clks,
	.num = ARRAY_SIZE(meson8m2_hw_clks),
};

static void __init meson8b_clkc_init_common(struct device_node *np,
					    struct meson_clk_hw_data *hw_clks)
{
	struct meson8b_clk_reset *rstc;
	struct device_node *parent_np;
	const char *notifier_clk_name;
	struct clk *notifier_clk;
	struct regmap *map;
	int i, ret;

	parent_np = of_get_parent(np);
	map = syscon_node_to_regmap(parent_np);
	of_node_put(parent_np);
	if (IS_ERR(map)) {
		pr_err("failed to get HHI regmap - Trying obsolete regs\n");
		return;
	}

	rstc = kzalloc(sizeof(*rstc), GFP_KERNEL);
	if (!rstc)
		return;

	/* Reset Controller */
	rstc->regmap = map;
	rstc->reset.ops = &meson8b_clk_reset_ops;
	rstc->reset.nr_resets = ARRAY_SIZE(meson8b_clk_reset_bits);
	rstc->reset.of_node = np;
	ret = reset_controller_register(&rstc->reset);
	if (ret) {
		pr_err("%s: Failed to register clkc reset controller: %d\n",
		       __func__, ret);
		return;
	}

	/*
	 * register all clks and start with the first used ID (which is
	 * CLKID_PLL_FIXED)
	 */
	for (i = CLKID_PLL_FIXED; i < hw_clks->num; i++) {
		/* array might be sparse */
		if (!hw_clks->hws[i])
			continue;

		ret = of_clk_hw_register(np, hw_clks->hws[i]);
		if (ret)
			return;
	}

	meson8b_cpu_nb_data.cpu_clk = hw_clks->hws[CLKID_CPUCLK];

	/*
	 * FIXME we shouldn't program the muxes in notifier handlers. The
	 * tricky programming sequence will be handled by the forthcoming
	 * coordinated clock rates mechanism once that feature is released.
	 */
	notifier_clk_name = clk_hw_get_name(&meson8b_cpu_scale_out_sel.hw);
	notifier_clk = __clk_lookup(notifier_clk_name);
	ret = clk_notifier_register(notifier_clk, &meson8b_cpu_nb_data.nb);
	if (ret) {
		pr_err("%s: failed to register the CPU clock notifier\n",
		       __func__);
		return;
	}

	ret = of_clk_add_hw_provider(np, meson_clk_hw_get, hw_clks);
	if (ret)
		pr_err("%s: failed to register clock provider\n", __func__);
}

static void __init meson8_clkc_init(struct device_node *np)
{
	return meson8b_clkc_init_common(np, &meson8_clks);
}

static void __init meson8b_clkc_init(struct device_node *np)
{
	return meson8b_clkc_init_common(np, &meson8b_clks);
}

static void __init meson8m2_clkc_init(struct device_node *np)
{
	return meson8b_clkc_init_common(np, &meson8m2_clks);
}

CLK_OF_DECLARE_DRIVER(meson8_clkc, "amlogic,meson8-clkc",
		      meson8_clkc_init);
CLK_OF_DECLARE_DRIVER(meson8b_clkc, "amlogic,meson8b-clkc",
		      meson8b_clkc_init);
CLK_OF_DECLARE_DRIVER(meson8m2_clkc, "amlogic,meson8m2-clkc",
		      meson8m2_clkc_init);
