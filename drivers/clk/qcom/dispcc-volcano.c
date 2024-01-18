// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,dispcc-volcano.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "reset.h"
#include "vdd-level.h"

#define DISP_CC_MISC_CMD	0xF000

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_HIGH + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mx, VDD_HIGH + 1, 1, vdd_corner);

static struct clk_vdd_class *disp_cc_volcano_regulators[] = {
	&vdd_cx,
	&vdd_mx,
};

enum {
	P_BI_TCXO,
	P_DISP_CC_PLL0_OUT_EVEN,
	P_DISP_CC_PLL0_OUT_MAIN,
	P_DP0_PHY_PLL_LINK_CLK,
	P_DP0_PHY_PLL_VCO_DIV_CLK,
	P_DSI0_PHY_PLL_OUT_BYTECLK,
	P_DSI0_PHY_PLL_OUT_DSICLK,
	P_GCC_DISP_GPLL0_CLK,
	P_SLEEP_CLK,
};

static const struct pll_vco lucid_ole_vco[] = {
	{ 249600000, 2300000000, 0 },
};

/* 257.142858 MHz Configuration */
static const struct alpha_pll_config disp_cc_pll0_config = {
	.l = 0xd,
	.cal_l = 0x44,
	.cal_l_ringosc = 0x44,
	.alpha = 0x6492,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82aa299c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000005,
};

static struct clk_alpha_pll disp_cc_pll0 = {
	.offset = 0x0,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
				.name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_ole_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 615000000,
				[VDD_LOW] = 1100000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000,
				[VDD_HIGH_L1] = 2300000000},
		},
	},
};

static const struct parent_map disp_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data disp_cc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map disp_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_DSI0_PHY_PLL_OUT_DSICLK, 1 },
	{ P_DSI0_PHY_PLL_OUT_BYTECLK, 2 },
};

static const struct clk_parent_data disp_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dsi0_phy_pll_out_dsiclk", .name = "dsi0_phy_pll_out_dsiclk" },
	{ .fw_name = "dsi0_phy_pll_out_byteclk", .name = "dsi0_phy_pll_out_byteclk" },
};

static const struct parent_map disp_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_DP0_PHY_PLL_LINK_CLK, 1 },
	{ P_DP0_PHY_PLL_VCO_DIV_CLK, 2 },
};

static const struct clk_parent_data disp_cc_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dp0_phy_pll_link_clk", .name = "dp0_phy_pll_link_clk" },
	{ .fw_name = "dp0_phy_pll_vco_div_clk", .name = "dp0_phy_pll_vco_div_clk" },
};

static const struct parent_map disp_cc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_DISP_GPLL0_CLK, 4 },
};

static const struct clk_parent_data disp_cc_parent_data_3[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "gcc_disp_gpll0_clk", .name = "gcc_disp_gpll0_clk" },
};

static const struct parent_map disp_cc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_DP0_PHY_PLL_LINK_CLK, 1 },
};

static const struct clk_parent_data disp_cc_parent_data_4[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dp0_phy_pll_link_clk", .name = "dp0_phy_pll_link_clk" },
};

static const struct parent_map disp_cc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_DSI0_PHY_PLL_OUT_BYTECLK, 2 },
};

static const struct clk_parent_data disp_cc_parent_data_5[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dsi0_phy_pll_out_byteclk", .name = "dsi0_phy_pll_out_byteclk" },
};

static const struct parent_map disp_cc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_DISP_CC_PLL0_OUT_MAIN, 1 },
	{ P_GCC_DISP_GPLL0_CLK, 4 },
	{ P_DISP_CC_PLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data disp_cc_parent_data_6[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &disp_cc_pll0.clkr.hw },
	{ .fw_name = "gcc_disp_gpll0_clk", .name = "gcc_disp_gpll0_clk" },
	{ .hw = &disp_cc_pll0.clkr.hw },
};

static const struct parent_map disp_cc_parent_map_7[] = {
	{ P_SLEEP_CLK, 0 },
};

static const struct clk_parent_data disp_cc_parent_data_7_ao[] = {
	{ .fw_name = "sleep_clk" },
};

static const struct freq_tbl ftbl_disp_cc_mdss_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(37500000, P_GCC_DISP_GPLL0_CLK, 8, 0, 0),
	F(75000000, P_GCC_DISP_GPLL0_CLK, 4, 0, 0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_ahb_clk_src = {
	.cmd_rcgr = 0x8130,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_3,
	.freq_tbl = ftbl_disp_cc_mdss_ahb_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_ahb_clk_src",
		.parent_data = disp_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 37500000,
			[VDD_NOMINAL] = 75000000},
	},
};

static struct clk_rcg2 disp_cc_mdss_byte0_clk_src = {
	.cmd_rcgr = 0x8098,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_1,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_byte0_clk_src",
		.parent_data = disp_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_byte2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = disp_cc_volcano_regulators,
		.num_vdd_classes = ARRAY_SIZE(disp_cc_volcano_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 187500000,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 358000000},
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_dptx0_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_dptx0_aux_clk_src = {
	.cmd_rcgr = 0x8118,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.freq_tbl = ftbl_disp_cc_mdss_dptx0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_dptx0_aux_clk_src",
		.parent_data = disp_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_rcg2 disp_cc_mdss_dptx0_link_clk_src = {
	.cmd_rcgr = 0x80cc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_dptx0_link_clk_src",
		.parent_data = disp_cc_parent_data_4,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_byte2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = disp_cc_volcano_regulators,
		.num_vdd_classes = ARRAY_SIZE(disp_cc_volcano_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 270000000,
			[VDD_LOW_L1] = 540000000,
			[VDD_NOMINAL] = 810000000},
	},
};

static struct clk_rcg2 disp_cc_mdss_dptx0_pixel0_clk_src = {
	.cmd_rcgr = 0x80e8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_2,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_dptx0_pixel0_clk_src",
		.parent_data = disp_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_dp_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = disp_cc_volcano_regulators,
		.num_vdd_classes = ARRAY_SIZE(disp_cc_volcano_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 337500000,
			[VDD_LOW_L1] = 405000000,
			[VDD_NOMINAL] = 675000000},
	},
};

static struct clk_rcg2 disp_cc_mdss_dptx0_pixel1_clk_src = {
	.cmd_rcgr = 0x8100,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_2,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_dptx0_pixel1_clk_src",
		.parent_data = disp_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_dp_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = disp_cc_volcano_regulators,
		.num_vdd_classes = ARRAY_SIZE(disp_cc_volcano_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 337500000,
			[VDD_LOW_L1] = 405000000,
			[VDD_NOMINAL] = 675000000},
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_esc0_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(12800000, P_BI_TCXO, 1.5, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};
static struct clk_rcg2 disp_cc_mdss_esc0_clk_src = {
	.cmd_rcgr = 0x80b4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_5,
	.freq_tbl = ftbl_disp_cc_mdss_esc0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_esc0_clk_src",
		.parent_data = disp_cc_parent_data_5,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = disp_cc_volcano_regulators,
		.num_vdd_classes = ARRAY_SIZE(disp_cc_volcano_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_mdp_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(85714286, P_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(100000000, P_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(200000000, P_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(342000000, P_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(402000000, P_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(535000000, P_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(600000000, P_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(630000000, P_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_mdp_clk_src = {
	.cmd_rcgr = 0x8068,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_6,
	.freq_tbl = ftbl_disp_cc_mdss_mdp_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_mdp_clk_src",
		.parent_data = disp_cc_parent_data_6,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = disp_cc_volcano_regulators,
		.num_vdd_classes = ARRAY_SIZE(disp_cc_volcano_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 200000000,
			[VDD_LOW] = 342000000,
			[VDD_LOW_L1] = 402000000,
			[VDD_NOMINAL] = 535000000,
			[VDD_NOMINAL_L1] = 600000000,
			[VDD_HIGH] = 630000000},
	},
};

static struct clk_rcg2 disp_cc_mdss_pclk0_clk_src = {
	.cmd_rcgr = 0x8050,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_1,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_pclk0_clk_src",
		.parent_data = disp_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_pixel_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = disp_cc_volcano_regulators,
		.num_vdd_classes = ARRAY_SIZE(disp_cc_volcano_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 300000000,
			[VDD_LOW] = 480000000,
			[VDD_LOW_L1] = 625000000},
	},
};

static struct clk_rcg2 disp_cc_mdss_vsync_clk_src = {
	.cmd_rcgr = 0x8080,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.freq_tbl = ftbl_disp_cc_mdss_dptx0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_vsync_clk_src",
		.parent_data = disp_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static const struct freq_tbl ftbl_disp_cc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 disp_cc_sleep_clk_src = {
	.cmd_rcgr = 0xe054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_7,
	.freq_tbl = ftbl_disp_cc_sleep_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_sleep_clk_src",
		.parent_data = disp_cc_parent_data_7_ao,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_7_ao),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 disp_cc_xo_clk_src = {
	.cmd_rcgr = 0xe034,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.freq_tbl = ftbl_disp_cc_mdss_dptx0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_xo_clk_src",
		.parent_data = disp_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_regmap_div disp_cc_mdss_byte0_div_clk_src = {
	.reg = 0x80b0,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_byte0_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&disp_cc_mdss_byte0_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div disp_cc_mdss_dptx0_link_div_clk_src = {
	.reg = 0x80e4,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "disp_cc_mdss_dptx0_link_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&disp_cc_mdss_dptx0_link_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch disp_cc_mdss_accu_clk = {
	.halt_reg = 0xe050,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xe050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_accu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_ahb1_clk = {
	.halt_reg = 0xa020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_ahb1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_DONT_HOLD_STATE | CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_ahb_clk = {
	.halt_reg = 0x804c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x804c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_DONT_HOLD_STATE | CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_byte0_clk = {
	.halt_reg = 0x8024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_byte0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_byte0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_byte0_intf_clk = {
	.halt_reg = 0x8028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_byte0_intf_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_byte0_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dptx0_aux_clk = {
	.halt_reg = 0x8048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_dptx0_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_dptx0_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dptx0_crypto_clk = {
	.halt_reg = 0x803c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x803c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_dptx0_crypto_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_dptx0_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dptx0_link_clk = {
	.halt_reg = 0x8030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_dptx0_link_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_dptx0_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dptx0_link_intf_clk = {
	.halt_reg = 0x8038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_dptx0_link_intf_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_dptx0_link_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dptx0_pixel0_clk = {
	.halt_reg = 0x8040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_dptx0_pixel0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_dptx0_pixel0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dptx0_pixel1_clk = {
	.halt_reg = 0x8044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_dptx0_pixel1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_dptx0_pixel1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dptx0_usb_router_link_intf_clk = {
	.halt_reg = 0x8034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_dptx0_usb_router_link_intf_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_dptx0_link_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_esc0_clk = {
	.halt_reg = 0x802c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x802c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_esc0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_esc0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_mdp1_clk = {
	.halt_reg = 0xa004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_mdp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_mdp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_mdp_clk = {
	.halt_reg = 0x8008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_mdp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_mdp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_mdp_lut1_clk = {
	.halt_reg = 0xa010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_mdp_lut1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_mdp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_mdp_lut_clk = {
	.halt_reg = 0x8014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_mdp_lut_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_mdp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_non_gdsc_ahb_clk = {
	.halt_reg = 0xc004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xc004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_non_gdsc_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_pclk0_clk = {
	.halt_reg = 0x8004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_pclk0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_pclk0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_rscc_ahb_clk = {
	.halt_reg = 0xc00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc00c,
		.enable_mask = BIT(0),
		.flags = QCOM_CLK_BOOT_CRITICAL,
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_rscc_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_rscc_vsync_clk = {
	.halt_reg = 0xc008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc008,
		.enable_mask = BIT(0),
		.flags = QCOM_CLK_BOOT_CRITICAL,
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_rscc_vsync_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_vsync_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_vsync1_clk = {
	.halt_reg = 0xa01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_vsync1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_vsync_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_vsync_clk = {
	.halt_reg = 0x8020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "disp_cc_mdss_vsync_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&disp_cc_mdss_vsync_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *disp_cc_volcano_clocks[] = {
	[DISP_CC_MDSS_ACCU_CLK] = &disp_cc_mdss_accu_clk.clkr,
	[DISP_CC_MDSS_AHB1_CLK] = &disp_cc_mdss_ahb1_clk.clkr,
	[DISP_CC_MDSS_AHB_CLK] = &disp_cc_mdss_ahb_clk.clkr,
	[DISP_CC_MDSS_AHB_CLK_SRC] = &disp_cc_mdss_ahb_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_CLK] = &disp_cc_mdss_byte0_clk.clkr,
	[DISP_CC_MDSS_BYTE0_CLK_SRC] = &disp_cc_mdss_byte0_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_DIV_CLK_SRC] = &disp_cc_mdss_byte0_div_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_INTF_CLK] = &disp_cc_mdss_byte0_intf_clk.clkr,
	[DISP_CC_MDSS_DPTX0_AUX_CLK] = &disp_cc_mdss_dptx0_aux_clk.clkr,
	[DISP_CC_MDSS_DPTX0_AUX_CLK_SRC] = &disp_cc_mdss_dptx0_aux_clk_src.clkr,
	[DISP_CC_MDSS_DPTX0_CRYPTO_CLK] = &disp_cc_mdss_dptx0_crypto_clk.clkr,
	[DISP_CC_MDSS_DPTX0_LINK_CLK] = &disp_cc_mdss_dptx0_link_clk.clkr,
	[DISP_CC_MDSS_DPTX0_LINK_CLK_SRC] = &disp_cc_mdss_dptx0_link_clk_src.clkr,
	[DISP_CC_MDSS_DPTX0_LINK_DIV_CLK_SRC] = &disp_cc_mdss_dptx0_link_div_clk_src.clkr,
	[DISP_CC_MDSS_DPTX0_LINK_INTF_CLK] = &disp_cc_mdss_dptx0_link_intf_clk.clkr,
	[DISP_CC_MDSS_DPTX0_PIXEL0_CLK] = &disp_cc_mdss_dptx0_pixel0_clk.clkr,
	[DISP_CC_MDSS_DPTX0_PIXEL0_CLK_SRC] = &disp_cc_mdss_dptx0_pixel0_clk_src.clkr,
	[DISP_CC_MDSS_DPTX0_PIXEL1_CLK] = &disp_cc_mdss_dptx0_pixel1_clk.clkr,
	[DISP_CC_MDSS_DPTX0_PIXEL1_CLK_SRC] = &disp_cc_mdss_dptx0_pixel1_clk_src.clkr,
	[DISP_CC_MDSS_DPTX0_USB_ROUTER_LINK_INTF_CLK] =
		&disp_cc_mdss_dptx0_usb_router_link_intf_clk.clkr,
	[DISP_CC_MDSS_ESC0_CLK] = &disp_cc_mdss_esc0_clk.clkr,
	[DISP_CC_MDSS_ESC0_CLK_SRC] = &disp_cc_mdss_esc0_clk_src.clkr,
	[DISP_CC_MDSS_MDP1_CLK] = &disp_cc_mdss_mdp1_clk.clkr,
	[DISP_CC_MDSS_MDP_CLK] = &disp_cc_mdss_mdp_clk.clkr,
	[DISP_CC_MDSS_MDP_CLK_SRC] = &disp_cc_mdss_mdp_clk_src.clkr,
	[DISP_CC_MDSS_MDP_LUT1_CLK] = &disp_cc_mdss_mdp_lut1_clk.clkr,
	[DISP_CC_MDSS_MDP_LUT_CLK] = &disp_cc_mdss_mdp_lut_clk.clkr,
	[DISP_CC_MDSS_NON_GDSC_AHB_CLK] = &disp_cc_mdss_non_gdsc_ahb_clk.clkr,
	[DISP_CC_MDSS_PCLK0_CLK] = &disp_cc_mdss_pclk0_clk.clkr,
	[DISP_CC_MDSS_PCLK0_CLK_SRC] = &disp_cc_mdss_pclk0_clk_src.clkr,
	[DISP_CC_MDSS_RSCC_AHB_CLK] = &disp_cc_mdss_rscc_ahb_clk.clkr,
	[DISP_CC_MDSS_RSCC_VSYNC_CLK] = &disp_cc_mdss_rscc_vsync_clk.clkr,
	[DISP_CC_MDSS_VSYNC1_CLK] = &disp_cc_mdss_vsync1_clk.clkr,
	[DISP_CC_MDSS_VSYNC_CLK] = &disp_cc_mdss_vsync_clk.clkr,
	[DISP_CC_MDSS_VSYNC_CLK_SRC] = &disp_cc_mdss_vsync_clk_src.clkr,
	[DISP_CC_PLL0] = &disp_cc_pll0.clkr,
	[DISP_CC_SLEEP_CLK_SRC] = &disp_cc_sleep_clk_src.clkr,
	[DISP_CC_XO_CLK_SRC] = &disp_cc_xo_clk_src.clkr,
};

static const struct qcom_reset_map disp_cc_volcano_resets[] = {
	[DISP_CC_MDSS_CORE_BCR] = { 0x8000 },
	[DISP_CC_MDSS_CORE_INT2_BCR] = { 0xa000 },
	[DISP_CC_MDSS_RSCC_BCR] = { 0xc000 },
};

static const struct regmap_config disp_cc_volcano_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x11008,
	.fast_io = true,
};

static struct qcom_cc_desc disp_cc_volcano_desc = {
	.config = &disp_cc_volcano_regmap_config,
	.clks = disp_cc_volcano_clocks,
	.num_clks = ARRAY_SIZE(disp_cc_volcano_clocks),
	.resets = disp_cc_volcano_resets,
	.num_resets = ARRAY_SIZE(disp_cc_volcano_resets),
	.clk_regulators = disp_cc_volcano_regulators,
	.num_clk_regulators = ARRAY_SIZE(disp_cc_volcano_regulators),
};

static const struct of_device_id disp_cc_volcano_match_table[] = {
	{ .compatible = "qcom,volcano-dispcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, disp_cc_volcano_match_table);

static int disp_cc_volcano_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &disp_cc_volcano_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_lucid_ole_pll_configure(&disp_cc_pll0, regmap, &disp_cc_pll0_config);

	/* Enable clock gating for MDP clocks */
	regmap_update_bits(regmap, DISP_CC_MISC_CMD, 0x10, 0x10);

	/*
	 * Keep clocks always enabled:
	 *	disp_cc_sleep_clk
	 *	disp_cc_xo_clk
	 */
	regmap_update_bits(regmap, 0xe06c, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0xe04c, BIT(0), BIT(0));

	ret = qcom_cc_really_probe(pdev, &disp_cc_volcano_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register DISP CC clocks\n");
		return ret;
	}

	dev_info(&pdev->dev, "Registered DISP CC clocks\n");

	return ret;
}

static void disp_cc_volcano_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &disp_cc_volcano_desc);
}

static struct platform_driver disp_cc_volcano_driver = {
	.probe = disp_cc_volcano_probe,
	.driver = {
		.name = "disp_cc-volcano",
		.of_match_table = disp_cc_volcano_match_table,
		.sync_state = disp_cc_volcano_sync_state,
	},
};

static int __init disp_cc_volcano_init(void)
{
	return platform_driver_register(&disp_cc_volcano_driver);
}
subsys_initcall(disp_cc_volcano_init);

static void __exit disp_cc_volcano_exit(void)
{
	platform_driver_unregister(&disp_cc_volcano_driver);
}
module_exit(disp_cc_volcano_exit);

MODULE_DESCRIPTION("QTI DISP_CC VOLCANO Driver");
MODULE_LICENSE("GPL");
