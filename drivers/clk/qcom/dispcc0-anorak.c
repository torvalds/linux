// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include <dt-bindings/clock/qcom,dispcc-anorak.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "vdd-level.h"

#define DISP_CC_MISC_CMD	0xF000

static DEFINE_VDD_REGULATORS(vdd_mm, VDD_HIGH + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mxa, VDD_NOMINAL_L1 + 1, 1, vdd_corner);

static struct clk_vdd_class *disp_cc_0_anorak_regulators[] = {
	&vdd_mm,
	&vdd_mxa,
};

enum {
	P_BI_TCXO,
	P_DP0_PHY_PLL_LINK_CLK,
	P_DP0_PHY_PLL_VCO_DIV_CLK,
	P_DP1_PHY_PLL_LINK_CLK,
	P_DP1_PHY_PLL_VCO_DIV_CLK,
	P_DP2_PHY_PLL_LINK_CLK,
	P_DP2_PHY_PLL_VCO_DIV_CLK,
	P_DP3_PHY_PLL_LINK_CLK,
	P_DP3_PHY_PLL_VCO_DIV_CLK,
	P_DSI0_PHY_PLL_OUT_BYTECLK,
	P_DSI0_PHY_PLL_OUT_DSICLK,
	P_DSI1_PHY_PLL_OUT_BYTECLK,
	P_DSI1_PHY_PLL_OUT_DSICLK,
	P_DSI_M_PHY_PLL_OUT_BYTECLK,
	P_DSI_M_PHY_PLL_OUT_DSICLK,
	P_MDSS_0_DISP_CC_PLL0_OUT_MAIN,
	P_MDSS_0_DISP_CC_PLL1_OUT_EVEN,
	P_MDSS_0_DISP_CC_PLL1_OUT_MAIN,
	P_SLEEP_CLK,
};

static const struct pll_vco lucid_evo_vco[] = {
	{ 249600000, 2000000000, 0 },
};

/* 600MHz Configuration */
static const struct alpha_pll_config mdss_0_disp_cc_pll0_config = {
	.l = 0x1F,
	.cal_l = 0x44,
	.alpha = 0x4000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32AA299C,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00000805,
};

static struct clk_alpha_pll mdss_0_disp_cc_pll0 = {
	.offset = 0x0,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mm,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2000000000},
		},
	},
};

/* 600MHz Configuration */
static const struct alpha_pll_config mdss_0_disp_cc_pll1_config = {
	.l = 0x1F,
	.cal_l = 0x44,
	.alpha = 0x4000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32AA299C,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000805,
};

static struct clk_alpha_pll mdss_0_disp_cc_pll1 = {
	.offset = 0x1000,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_pll1",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mm,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2000000000},
		},
	},
};

static const struct parent_map disp_cc_0_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_DP0_PHY_PLL_LINK_CLK, 1 },
	{ P_DP0_PHY_PLL_VCO_DIV_CLK, 2 },
	{ P_DP3_PHY_PLL_VCO_DIV_CLK, 3 },
	{ P_DP1_PHY_PLL_VCO_DIV_CLK, 4 },
	{ P_DP2_PHY_PLL_VCO_DIV_CLK, 6 },
};

static const struct clk_parent_data disp_cc_0_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dp0_phy_pll_link_clk", .name = "dp0_phy_pll_link_clk" },
	{ .fw_name = "dp0_phy_pll_vco_div_clk", .name = "dp0_phy_pll_vco_div_clk" },
	{ .fw_name = "dp3_phy_pll_vco_div_clk", .name = "dp3_phy_pll_vco_div_clk" },
	{ .fw_name = "dp1_phy_pll_vco_div_clk", .name = "dp1_phy_pll_vco_div_clk" },
	{ .fw_name = "dp2_phy_pll_vco_div_clk", .name = "dp2_phy_pll_vco_div_clk" },
};

static const struct parent_map disp_cc_0_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data disp_cc_0_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
};

static const struct clk_parent_data disp_cc_0_parent_data_1_ao[] = {
	{ .fw_name = "bi_tcxo_ao" },
};

static const struct parent_map disp_cc_0_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_DSI0_PHY_PLL_OUT_DSICLK, 1 },
	{ P_DSI0_PHY_PLL_OUT_BYTECLK, 2 },
	{ P_DSI1_PHY_PLL_OUT_DSICLK, 3 },
	{ P_DSI1_PHY_PLL_OUT_BYTECLK, 4 },
	{ P_DSI_M_PHY_PLL_OUT_BYTECLK, 5 },
	{ P_DSI_M_PHY_PLL_OUT_DSICLK, 6 },
};

static const struct clk_parent_data disp_cc_0_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dsi0_phy_pll_out_dsiclk", .name = "dsi0_phy_pll_out_dsiclk" },
	{ .fw_name = "dsi0_phy_pll_out_byteclk", .name = "dsi0_phy_pll_out_byteclk" },
	{ .fw_name = "dsi1_phy_pll_out_dsiclk", .name = "dsi1_phy_pll_out_dsiclk" },
	{ .fw_name = "dsi1_phy_pll_out_byteclk", .name = "dsi1_phy_pll_out_byteclk" },
	{ .fw_name = "dsi_m_phy_pll_out_byteclk", .name = "dsi_m_phy_pll_out_byteclk" },
	{ .fw_name = "dsi_m_phy_pll_out_dsiclk", .name = "dsi_m_phy_pll_out_dsiclk" },
};

static const struct parent_map disp_cc_0_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_DP0_PHY_PLL_LINK_CLK, 1 },
	{ P_DP1_PHY_PLL_LINK_CLK, 2 },
	{ P_DP2_PHY_PLL_LINK_CLK, 3 },
	{ P_DP3_PHY_PLL_LINK_CLK, 4 },
};

static const struct clk_parent_data disp_cc_0_parent_data_3[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dp0_phy_pll_link_clk", .name = "dp0_phy_pll_link_clk" },
	{ .fw_name = "dp1_phy_pll_link_clk", .name = "dp1_phy_pll_link_clk" },
	{ .fw_name = "dp2_phy_pll_link_clk", .name = "dp2_phy_pll_link_clk" },
	{ .fw_name = "dp3_phy_pll_link_clk", .name = "dp3_phy_pll_link_clk" },
};

static const struct parent_map disp_cc_0_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_DSI0_PHY_PLL_OUT_BYTECLK, 2 },
	{ P_DSI1_PHY_PLL_OUT_BYTECLK, 4 },
};

static const struct clk_parent_data disp_cc_0_parent_data_4[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dsi0_phy_pll_out_byteclk", .name = "dsi0_phy_pll_out_byteclk" },
	{ .fw_name = "dsi1_phy_pll_out_byteclk", .name = "dsi1_phy_pll_out_byteclk" },
};

static const struct parent_map disp_cc_0_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_MDSS_0_DISP_CC_PLL1_OUT_MAIN, 4 },
	{ P_MDSS_0_DISP_CC_PLL1_OUT_EVEN, 6 },
};

static const struct clk_parent_data disp_cc_0_parent_data_5[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &mdss_0_disp_cc_pll1.clkr.hw },
	{ .hw = &mdss_0_disp_cc_pll1.clkr.hw },
};

static const struct parent_map disp_cc_0_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_MDSS_0_DISP_CC_PLL0_OUT_MAIN, 1 },
	{ P_MDSS_0_DISP_CC_PLL1_OUT_MAIN, 4 },
	{ P_MDSS_0_DISP_CC_PLL1_OUT_EVEN, 6 },
};

static const struct clk_parent_data disp_cc_0_parent_data_6[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &mdss_0_disp_cc_pll0.clkr.hw },
	{ .hw = &mdss_0_disp_cc_pll1.clkr.hw },
	{ .hw = &mdss_0_disp_cc_pll1.clkr.hw },
};

static const struct parent_map disp_cc_0_parent_map_7[] = {
	{ P_SLEEP_CLK, 0 },
};

static const struct clk_parent_data disp_cc_0_parent_data_7[] = {
	{ .fw_name = "sleep_clk" },
};

static const struct freq_tbl ftbl_mdss_0_disp_cc_mdss_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(37500000, P_MDSS_0_DISP_CC_PLL1_OUT_MAIN, 16, 0, 0),
	F(75000000, P_MDSS_0_DISP_CC_PLL1_OUT_MAIN, 8, 0, 0),
	{ }
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_ahb_clk_src = {
	.cmd_rcgr = 0x82ec,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_5,
	.freq_tbl = ftbl_mdss_0_disp_cc_mdss_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_ahb_clk_src",
		.parent_data = disp_cc_0_parent_data_5,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 37500000,
			[VDD_NOMINAL] = 75000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_byte0_clk_src = {
	.cmd_rcgr = 0x810c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_2,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_byte0_clk_src",
		.parent_data = disp_cc_0_parent_data_2,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_byte2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 187500000,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 358000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_byte1_clk_src = {
	.cmd_rcgr = 0x8128,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_2,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_byte1_clk_src",
		.parent_data = disp_cc_0_parent_data_2,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_byte2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 187500000,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 358000000},
	},
};

static const struct freq_tbl ftbl_mdss_0_disp_cc_mdss_dptx0_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx0_aux_clk_src = {
	.cmd_rcgr = 0x81c0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_1,
	.freq_tbl = ftbl_mdss_0_disp_cc_mdss_dptx0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx0_aux_clk_src",
		.parent_data = disp_cc_0_parent_data_1,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx0_link_clk_src = {
	.cmd_rcgr = 0x8174,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_3,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx0_link_clk_src",
		.parent_data = disp_cc_0_parent_data_3,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_byte2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 270000000,
			[VDD_LOW_L1] = 540000000,
			[VDD_NOMINAL] = 810000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx0_pixel0_clk_src = {
	.cmd_rcgr = 0x8190,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx0_pixel0_clk_src",
		.parent_data = disp_cc_0_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_dp_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 337500000,
			[VDD_LOW_L1] = 405000000,
			[VDD_NOMINAL] = 675000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx0_pixel1_clk_src = {
	.cmd_rcgr = 0x81a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx0_pixel1_clk_src",
		.parent_data = disp_cc_0_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_dp_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 337500000,
			[VDD_LOW_L1] = 405000000,
			[VDD_NOMINAL] = 675000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx1_aux_clk_src = {
	.cmd_rcgr = 0x8224,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_1,
	.freq_tbl = ftbl_mdss_0_disp_cc_mdss_dptx0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx1_aux_clk_src",
		.parent_data = disp_cc_0_parent_data_1,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx1_link_clk_src = {
	.cmd_rcgr = 0x8208,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_3,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx1_link_clk_src",
		.parent_data = disp_cc_0_parent_data_3,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_byte2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 270000000,
			[VDD_LOW_L1] = 540000000,
			[VDD_NOMINAL] = 810000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx1_pixel0_clk_src = {
	.cmd_rcgr = 0x81d8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx1_pixel0_clk_src",
		.parent_data = disp_cc_0_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_dp_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 337500000,
			[VDD_LOW_L1] = 405000000,
			[VDD_NOMINAL] = 675000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx1_pixel1_clk_src = {
	.cmd_rcgr = 0x81f0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx1_pixel1_clk_src",
		.parent_data = disp_cc_0_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_dp_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 337500000,
			[VDD_LOW_L1] = 405000000,
			[VDD_NOMINAL] = 675000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx2_aux_clk_src = {
	.cmd_rcgr = 0x8288,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_1,
	.freq_tbl = ftbl_mdss_0_disp_cc_mdss_dptx0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx2_aux_clk_src",
		.parent_data = disp_cc_0_parent_data_1,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx2_link_clk_src = {
	.cmd_rcgr = 0x823c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_3,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx2_link_clk_src",
		.parent_data = disp_cc_0_parent_data_3,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_byte2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 270000000,
			[VDD_LOW_L1] = 540000000,
			[VDD_NOMINAL] = 810000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx2_pixel0_clk_src = {
	.cmd_rcgr = 0x8258,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx2_pixel0_clk_src",
		.parent_data = disp_cc_0_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_dp_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 337500000,
			[VDD_LOW_L1] = 405000000,
			[VDD_NOMINAL] = 675000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx2_pixel1_clk_src = {
	.cmd_rcgr = 0x8270,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx2_pixel1_clk_src",
		.parent_data = disp_cc_0_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_dp_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 337500000,
			[VDD_LOW_L1] = 405000000,
			[VDD_NOMINAL] = 675000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx3_aux_clk_src = {
	.cmd_rcgr = 0x82d4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_1,
	.freq_tbl = ftbl_mdss_0_disp_cc_mdss_dptx0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx3_aux_clk_src",
		.parent_data = disp_cc_0_parent_data_1,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx3_link_clk_src = {
	.cmd_rcgr = 0x82b8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_3,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx3_link_clk_src",
		.parent_data = disp_cc_0_parent_data_3,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_byte2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 270000000,
			[VDD_LOW] = 594000000,
			[VDD_NOMINAL] = 810000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_dptx3_pixel0_clk_src = {
	.cmd_rcgr = 0x82a0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_dptx3_pixel0_clk_src",
		.parent_data = disp_cc_0_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_dp_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 337500000,
			[VDD_LOW_L1] = 405000000,
			[VDD_NOMINAL] = 675000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_esc0_clk_src = {
	.cmd_rcgr = 0x8144,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_4,
	.freq_tbl = ftbl_mdss_0_disp_cc_mdss_dptx0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_esc0_clk_src",
		.parent_data = disp_cc_0_parent_data_4,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_esc1_clk_src = {
	.cmd_rcgr = 0x815c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_4,
	.freq_tbl = ftbl_mdss_0_disp_cc_mdss_dptx0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_esc1_clk_src",
		.parent_data = disp_cc_0_parent_data_4,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static const struct freq_tbl ftbl_mdss_0_disp_cc_mdss_mdp_clk_src[] = {
	F(200000000, P_MDSS_0_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(325000000, P_MDSS_0_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(375000000, P_MDSS_0_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(500000000, P_MDSS_0_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(550000000, P_MDSS_0_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_mdp_clk_src = {
	.cmd_rcgr = 0x80dc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_6,
	.freq_tbl = ftbl_mdss_0_disp_cc_mdss_mdp_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_mdp_clk_src",
		.parent_data = disp_cc_0_parent_data_6,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = disp_cc_0_anorak_regulators,
		.num_vdd_classes = ARRAY_SIZE(disp_cc_0_anorak_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 200000000,
			[VDD_LOW] = 325000000,
			[VDD_LOW_L1] = 375000000,
			[VDD_NOMINAL] = 500000000,
			[VDD_NOMINAL_L1] = 550000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_pclk0_clk_src = {
	.cmd_rcgr = 0x80ac,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_2,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_pclk0_clk_src",
		.parent_data = disp_cc_0_parent_data_2,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_pixel_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 300000000,
			[VDD_LOW] = 480000000,
			[VDD_LOW_L1] = 625000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_pclk1_clk_src = {
	.cmd_rcgr = 0x80c4,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_2,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_pclk1_clk_src",
		.parent_data = disp_cc_0_parent_data_2,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_pixel_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 300000000,
			[VDD_LOW] = 480000000,
			[VDD_LOW_L1] = 625000000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_mdss_vsync_clk_src = {
	.cmd_rcgr = 0x80f4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_1,
	.freq_tbl = ftbl_mdss_0_disp_cc_mdss_dptx0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_mdss_vsync_clk_src",
		.parent_data = disp_cc_0_parent_data_1,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static const struct freq_tbl ftbl_mdss_0_disp_cc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 mdss_0_disp_cc_sleep_clk_src = {
	.cmd_rcgr = 0xe05c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_7,
	.freq_tbl = ftbl_mdss_0_disp_cc_sleep_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_sleep_clk_src",
		.parent_data = disp_cc_0_parent_data_7,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_7),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 32000},
	},
};

static struct clk_rcg2 mdss_0_disp_cc_xo_clk_src = {
	.cmd_rcgr = 0xe03c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_0_parent_map_1,
	.freq_tbl = ftbl_mdss_0_disp_cc_mdss_dptx0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "mdss_0_disp_cc_xo_clk_src",
		.parent_data = disp_cc_0_parent_data_1_ao,
		.num_parents = ARRAY_SIZE(disp_cc_0_parent_data_1_ao),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div mdss_0_disp_cc_mdss_byte0_div_clk_src = {
	.reg = 0x8124,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "mdss_0_disp_cc_mdss_byte0_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&mdss_0_disp_cc_mdss_byte0_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div mdss_0_disp_cc_mdss_byte1_div_clk_src = {
	.reg = 0x8140,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "mdss_0_disp_cc_mdss_byte1_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&mdss_0_disp_cc_mdss_byte1_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div mdss_0_disp_cc_mdss_dptx0_link_div_clk_src = {
	.reg = 0x818c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "mdss_0_disp_cc_mdss_dptx0_link_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&mdss_0_disp_cc_mdss_dptx0_link_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div mdss_0_disp_cc_mdss_dptx1_link_div_clk_src = {
	.reg = 0x8220,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "mdss_0_disp_cc_mdss_dptx1_link_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&mdss_0_disp_cc_mdss_dptx1_link_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div mdss_0_disp_cc_mdss_dptx2_link_div_clk_src = {
	.reg = 0x8254,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "mdss_0_disp_cc_mdss_dptx2_link_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&mdss_0_disp_cc_mdss_dptx2_link_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div mdss_0_disp_cc_mdss_dptx3_link_div_clk_src = {
	.reg = 0x82d0,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "mdss_0_disp_cc_mdss_dptx3_link_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&mdss_0_disp_cc_mdss_dptx3_link_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_accu_clk = {
	.halt_reg = 0xe058,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xe058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_accu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_ahb1_clk = {
	.halt_reg = 0xa020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_ahb1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_DONT_HOLD_STATE | CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_ahb_clk = {
	.halt_reg = 0x80a8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x80a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_DONT_HOLD_STATE | CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_byte0_clk = {
	.halt_reg = 0x8028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_byte0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_byte0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_byte0_intf_clk = {
	.halt_reg = 0x802c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x802c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_byte0_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_byte0_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_byte1_clk = {
	.halt_reg = 0x8030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_byte1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_byte1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_byte1_intf_clk = {
	.halt_reg = 0x8034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_byte1_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_byte1_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx0_aux_clk = {
	.halt_reg = 0x8058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx0_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx0_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx0_crypto_clk = {
	.halt_reg = 0x804c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x804c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx0_crypto_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx0_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx0_link_clk = {
	.halt_reg = 0x8040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx0_link_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx0_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx0_link_intf_clk = {
	.halt_reg = 0x8048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx0_link_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx0_link_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx0_pixel0_clk = {
	.halt_reg = 0x8050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx0_pixel0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx0_pixel0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx0_pixel1_clk = {
	.halt_reg = 0x8054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx0_pixel1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx0_pixel1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx0_usb_router_link_intf_clk = {
	.halt_reg = 0x8044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx0_usb_router_link_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx0_link_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx1_aux_clk = {
	.halt_reg = 0x8074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx1_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx1_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx1_crypto_clk = {
	.halt_reg = 0x8070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx1_crypto_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx1_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx1_link_clk = {
	.halt_reg = 0x8064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx1_link_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx1_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx1_link_intf_clk = {
	.halt_reg = 0x806c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x806c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx1_link_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx1_link_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx1_pixel0_clk = {
	.halt_reg = 0x805c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x805c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx1_pixel0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx1_pixel0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx1_pixel1_clk = {
	.halt_reg = 0x8060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx1_pixel1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx1_pixel1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx1_usb_router_link_intf_clk = {
	.halt_reg = 0x8068,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx1_usb_router_link_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx1_link_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx2_aux_clk = {
	.halt_reg = 0x8090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx2_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx2_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx2_crypto_clk = {
	.halt_reg = 0x808c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x808c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx2_crypto_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx2_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx2_link_clk = {
	.halt_reg = 0x8080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx2_link_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx2_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx2_link_intf_clk = {
	.halt_reg = 0x8084,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx2_link_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx2_link_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx2_pixel0_clk = {
	.halt_reg = 0x8078,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8078,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx2_pixel0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx2_pixel0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx2_pixel1_clk = {
	.halt_reg = 0x807c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x807c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx2_pixel1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx2_pixel1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx2_usb_router_link_intf_clk = {
	.halt_reg = 0x8088,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx2_usb_router_link_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx2_link_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx3_aux_clk = {
	.halt_reg = 0x80a0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x80a0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx3_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx3_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx3_crypto_clk = {
	.halt_reg = 0x80a4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x80a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx3_crypto_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx3_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx3_link_clk = {
	.halt_reg = 0x8098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8098,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx3_link_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx3_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx3_link_intf_clk = {
	.halt_reg = 0x809c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x809c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx3_link_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx3_link_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_dptx3_pixel0_clk = {
	.halt_reg = 0x8094,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8094,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_dptx3_pixel0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_dptx3_pixel0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_esc0_clk = {
	.halt_reg = 0x8038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_esc0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_esc0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_esc1_clk = {
	.halt_reg = 0x803c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x803c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_esc1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_esc1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_mdp1_clk = {
	.halt_reg = 0xa004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_mdp1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_mdp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_mdp_clk = {
	.halt_reg = 0x800c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x800c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_mdp_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_mdp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_mdp_lut1_clk = {
	.halt_reg = 0xa010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_mdp_lut1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_mdp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_mdp_lut_clk = {
	.halt_reg = 0x8018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x8018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_mdp_lut_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_mdp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_non_gdsc_ahb_clk = {
	.halt_reg = 0xc004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xc004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_non_gdsc_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_pclk0_clk = {
	.halt_reg = 0x8004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_pclk0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_pclk0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_pclk1_clk = {
	.halt_reg = 0x8008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_pclk1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_pclk1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_rscc_ahb_clk = {
	.halt_reg = 0xc00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc00c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_rscc_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_rscc_vsync_clk = {
	.halt_reg = 0xc008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_rscc_vsync_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_vsync_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_vsync1_clk = {
	.halt_reg = 0xa01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_vsync1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_vsync_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_mdss_vsync_clk = {
	.halt_reg = 0x8024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_mdss_vsync_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_mdss_vsync_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_0_disp_cc_sleep_clk = {
	.halt_reg = 0xe074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "mdss_0_disp_cc_sleep_clk",
			.parent_hws = (const struct clk_hw*[]){
				&mdss_0_disp_cc_sleep_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *disp_cc_0_anorak_clocks[] = {
	[DISP_CC_MDSS_ACCU_CLK] = &mdss_0_disp_cc_mdss_accu_clk.clkr,
	[DISP_CC_MDSS_AHB1_CLK] = &mdss_0_disp_cc_mdss_ahb1_clk.clkr,
	[DISP_CC_MDSS_AHB_CLK] = &mdss_0_disp_cc_mdss_ahb_clk.clkr,
	[DISP_CC_MDSS_AHB_CLK_SRC] = &mdss_0_disp_cc_mdss_ahb_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_CLK] = &mdss_0_disp_cc_mdss_byte0_clk.clkr,
	[DISP_CC_MDSS_BYTE0_CLK_SRC] = &mdss_0_disp_cc_mdss_byte0_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_DIV_CLK_SRC] = &mdss_0_disp_cc_mdss_byte0_div_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_INTF_CLK] = &mdss_0_disp_cc_mdss_byte0_intf_clk.clkr,
	[DISP_CC_MDSS_BYTE1_CLK] = &mdss_0_disp_cc_mdss_byte1_clk.clkr,
	[DISP_CC_MDSS_BYTE1_CLK_SRC] = &mdss_0_disp_cc_mdss_byte1_clk_src.clkr,
	[DISP_CC_MDSS_BYTE1_DIV_CLK_SRC] = &mdss_0_disp_cc_mdss_byte1_div_clk_src.clkr,
	[DISP_CC_MDSS_BYTE1_INTF_CLK] = &mdss_0_disp_cc_mdss_byte1_intf_clk.clkr,
	[DISP_CC_MDSS_DPTX0_AUX_CLK] = &mdss_0_disp_cc_mdss_dptx0_aux_clk.clkr,
	[DISP_CC_MDSS_DPTX0_AUX_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx0_aux_clk_src.clkr,
	[DISP_CC_MDSS_DPTX0_CRYPTO_CLK] = &mdss_0_disp_cc_mdss_dptx0_crypto_clk.clkr,
	[DISP_CC_MDSS_DPTX0_LINK_CLK] = &mdss_0_disp_cc_mdss_dptx0_link_clk.clkr,
	[DISP_CC_MDSS_DPTX0_LINK_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx0_link_clk_src.clkr,
	[DISP_CC_MDSS_DPTX0_LINK_DIV_CLK_SRC] =
		&mdss_0_disp_cc_mdss_dptx0_link_div_clk_src.clkr,
	[DISP_CC_MDSS_DPTX0_LINK_INTF_CLK] = &mdss_0_disp_cc_mdss_dptx0_link_intf_clk.clkr,
	[DISP_CC_MDSS_DPTX0_PIXEL0_CLK] = &mdss_0_disp_cc_mdss_dptx0_pixel0_clk.clkr,
	[DISP_CC_MDSS_DPTX0_PIXEL0_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx0_pixel0_clk_src.clkr,
	[DISP_CC_MDSS_DPTX0_PIXEL1_CLK] = &mdss_0_disp_cc_mdss_dptx0_pixel1_clk.clkr,
	[DISP_CC_MDSS_DPTX0_PIXEL1_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx0_pixel1_clk_src.clkr,
	[DISP_CC_MDSS_DPTX0_USB_ROUTER_LINK_INTF_CLK] =
		&mdss_0_disp_cc_mdss_dptx0_usb_router_link_intf_clk.clkr,
	[DISP_CC_MDSS_DPTX1_AUX_CLK] = &mdss_0_disp_cc_mdss_dptx1_aux_clk.clkr,
	[DISP_CC_MDSS_DPTX1_AUX_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx1_aux_clk_src.clkr,
	[DISP_CC_MDSS_DPTX1_CRYPTO_CLK] = &mdss_0_disp_cc_mdss_dptx1_crypto_clk.clkr,
	[DISP_CC_MDSS_DPTX1_LINK_CLK] = &mdss_0_disp_cc_mdss_dptx1_link_clk.clkr,
	[DISP_CC_MDSS_DPTX1_LINK_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx1_link_clk_src.clkr,
	[DISP_CC_MDSS_DPTX1_LINK_DIV_CLK_SRC] =
		&mdss_0_disp_cc_mdss_dptx1_link_div_clk_src.clkr,
	[DISP_CC_MDSS_DPTX1_LINK_INTF_CLK] = &mdss_0_disp_cc_mdss_dptx1_link_intf_clk.clkr,
	[DISP_CC_MDSS_DPTX1_PIXEL0_CLK] = &mdss_0_disp_cc_mdss_dptx1_pixel0_clk.clkr,
	[DISP_CC_MDSS_DPTX1_PIXEL0_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx1_pixel0_clk_src.clkr,
	[DISP_CC_MDSS_DPTX1_PIXEL1_CLK] = &mdss_0_disp_cc_mdss_dptx1_pixel1_clk.clkr,
	[DISP_CC_MDSS_DPTX1_PIXEL1_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx1_pixel1_clk_src.clkr,
	[DISP_CC_MDSS_DPTX1_USB_ROUTER_LINK_INTF_CLK] =
		&mdss_0_disp_cc_mdss_dptx1_usb_router_link_intf_clk.clkr,
	[DISP_CC_MDSS_DPTX2_AUX_CLK] = &mdss_0_disp_cc_mdss_dptx2_aux_clk.clkr,
	[DISP_CC_MDSS_DPTX2_AUX_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx2_aux_clk_src.clkr,
	[DISP_CC_MDSS_DPTX2_CRYPTO_CLK] = &mdss_0_disp_cc_mdss_dptx2_crypto_clk.clkr,
	[DISP_CC_MDSS_DPTX2_LINK_CLK] = &mdss_0_disp_cc_mdss_dptx2_link_clk.clkr,
	[DISP_CC_MDSS_DPTX2_LINK_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx2_link_clk_src.clkr,
	[DISP_CC_MDSS_DPTX2_LINK_DIV_CLK_SRC] =
		&mdss_0_disp_cc_mdss_dptx2_link_div_clk_src.clkr,
	[DISP_CC_MDSS_DPTX2_LINK_INTF_CLK] = &mdss_0_disp_cc_mdss_dptx2_link_intf_clk.clkr,
	[DISP_CC_MDSS_DPTX2_PIXEL0_CLK] = &mdss_0_disp_cc_mdss_dptx2_pixel0_clk.clkr,
	[DISP_CC_MDSS_DPTX2_PIXEL0_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx2_pixel0_clk_src.clkr,
	[DISP_CC_MDSS_DPTX2_PIXEL1_CLK] = &mdss_0_disp_cc_mdss_dptx2_pixel1_clk.clkr,
	[DISP_CC_MDSS_DPTX2_PIXEL1_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx2_pixel1_clk_src.clkr,
	[DISP_CC_MDSS_DPTX2_USB_ROUTER_LINK_INTF_CLK] =
		&mdss_0_disp_cc_mdss_dptx2_usb_router_link_intf_clk.clkr,
	[DISP_CC_MDSS_DPTX3_AUX_CLK] = &mdss_0_disp_cc_mdss_dptx3_aux_clk.clkr,
	[DISP_CC_MDSS_DPTX3_AUX_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx3_aux_clk_src.clkr,
	[DISP_CC_MDSS_DPTX3_CRYPTO_CLK] = &mdss_0_disp_cc_mdss_dptx3_crypto_clk.clkr,
	[DISP_CC_MDSS_DPTX3_LINK_CLK] = &mdss_0_disp_cc_mdss_dptx3_link_clk.clkr,
	[DISP_CC_MDSS_DPTX3_LINK_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx3_link_clk_src.clkr,
	[DISP_CC_MDSS_DPTX3_LINK_DIV_CLK_SRC] =
		&mdss_0_disp_cc_mdss_dptx3_link_div_clk_src.clkr,
	[DISP_CC_MDSS_DPTX3_LINK_INTF_CLK] = &mdss_0_disp_cc_mdss_dptx3_link_intf_clk.clkr,
	[DISP_CC_MDSS_DPTX3_PIXEL0_CLK] = &mdss_0_disp_cc_mdss_dptx3_pixel0_clk.clkr,
	[DISP_CC_MDSS_DPTX3_PIXEL0_CLK_SRC] = &mdss_0_disp_cc_mdss_dptx3_pixel0_clk_src.clkr,
	[DISP_CC_MDSS_ESC0_CLK] = &mdss_0_disp_cc_mdss_esc0_clk.clkr,
	[DISP_CC_MDSS_ESC0_CLK_SRC] = &mdss_0_disp_cc_mdss_esc0_clk_src.clkr,
	[DISP_CC_MDSS_ESC1_CLK] = &mdss_0_disp_cc_mdss_esc1_clk.clkr,
	[DISP_CC_MDSS_ESC1_CLK_SRC] = &mdss_0_disp_cc_mdss_esc1_clk_src.clkr,
	[DISP_CC_MDSS_MDP1_CLK] = &mdss_0_disp_cc_mdss_mdp1_clk.clkr,
	[DISP_CC_MDSS_MDP_CLK] = &mdss_0_disp_cc_mdss_mdp_clk.clkr,
	[DISP_CC_MDSS_MDP_CLK_SRC] = &mdss_0_disp_cc_mdss_mdp_clk_src.clkr,
	[DISP_CC_MDSS_MDP_LUT1_CLK] = &mdss_0_disp_cc_mdss_mdp_lut1_clk.clkr,
	[DISP_CC_MDSS_MDP_LUT_CLK] = &mdss_0_disp_cc_mdss_mdp_lut_clk.clkr,
	[DISP_CC_MDSS_NON_GDSC_AHB_CLK] = &mdss_0_disp_cc_mdss_non_gdsc_ahb_clk.clkr,
	[DISP_CC_MDSS_PCLK0_CLK] = &mdss_0_disp_cc_mdss_pclk0_clk.clkr,
	[DISP_CC_MDSS_PCLK0_CLK_SRC] = &mdss_0_disp_cc_mdss_pclk0_clk_src.clkr,
	[DISP_CC_MDSS_PCLK1_CLK] = &mdss_0_disp_cc_mdss_pclk1_clk.clkr,
	[DISP_CC_MDSS_PCLK1_CLK_SRC] = &mdss_0_disp_cc_mdss_pclk1_clk_src.clkr,
	[DISP_CC_MDSS_RSCC_AHB_CLK] = &mdss_0_disp_cc_mdss_rscc_ahb_clk.clkr,
	[DISP_CC_MDSS_RSCC_VSYNC_CLK] = &mdss_0_disp_cc_mdss_rscc_vsync_clk.clkr,
	[DISP_CC_MDSS_VSYNC1_CLK] = &mdss_0_disp_cc_mdss_vsync1_clk.clkr,
	[DISP_CC_MDSS_VSYNC_CLK] = &mdss_0_disp_cc_mdss_vsync_clk.clkr,
	[DISP_CC_MDSS_VSYNC_CLK_SRC] = &mdss_0_disp_cc_mdss_vsync_clk_src.clkr,
	[DISP_CC_PLL0] = &mdss_0_disp_cc_pll0.clkr,
	[DISP_CC_PLL1] = &mdss_0_disp_cc_pll1.clkr,
	[DISP_CC_SLEEP_CLK] = &mdss_0_disp_cc_sleep_clk.clkr,
	[DISP_CC_SLEEP_CLK_SRC] = &mdss_0_disp_cc_sleep_clk_src.clkr,
	[DISP_CC_XO_CLK_SRC] = &mdss_0_disp_cc_xo_clk_src.clkr,
};

static const struct regmap_config disp_cc_0_anorak_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x11008,
	.fast_io = true,
};

static struct qcom_cc_desc disp_cc_0_anorak_desc = {
	.config = &disp_cc_0_anorak_regmap_config,
	.clks = disp_cc_0_anorak_clocks,
	.num_clks = ARRAY_SIZE(disp_cc_0_anorak_clocks),
	.clk_regulators = disp_cc_0_anorak_regulators,
	.num_clk_regulators = ARRAY_SIZE(disp_cc_0_anorak_regulators),
};

static const struct of_device_id disp_cc_0_anorak_match_table[] = {
	{ .compatible = "qcom,anorak-dispcc0" },
	{ }
};
MODULE_DEVICE_TABLE(of, disp_cc_0_anorak_match_table);

static int disp_cc_0_anorak_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &disp_cc_0_anorak_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = qcom_cc_runtime_init(pdev, &disp_cc_0_anorak_desc);
	if (ret)
		return ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret)
		return ret;

	clk_lucid_evo_pll_configure(&mdss_0_disp_cc_pll0, regmap, &mdss_0_disp_cc_pll0_config);
	clk_lucid_evo_pll_configure(&mdss_0_disp_cc_pll1, regmap, &mdss_0_disp_cc_pll1_config);

	/* Enable clock gating for MDP clocks */
	regmap_update_bits(regmap, DISP_CC_MISC_CMD, 0x10, 0x10);

	/*
	 * Keep clocks always enabled:
	 *	mdss_0_disp_cc_xo_clk
	 */
	regmap_update_bits(regmap, 0xe054, BIT(0), BIT(0));

	ret = qcom_cc_really_probe(pdev, &disp_cc_0_anorak_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register DISP CC 0 clocks\n");
		return ret;
	}

	pm_runtime_put_sync(&pdev->dev);
	dev_info(&pdev->dev, "Registered DISP CC 0 clocks\n");

	return ret;
}

static void disp_cc_0_anorak_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &disp_cc_0_anorak_desc);
}

static const struct dev_pm_ops disp_cc_0_anorak_pm_ops = {
	SET_RUNTIME_PM_OPS(qcom_cc_runtime_suspend, qcom_cc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver disp_cc_0_anorak_driver = {
	.probe = disp_cc_0_anorak_probe,
	.driver = {
		.name = "disp_cc_0-anorak",
		.of_match_table = disp_cc_0_anorak_match_table,
		.sync_state = disp_cc_0_anorak_sync_state,
		.pm = &disp_cc_0_anorak_pm_ops,
	},
};

static int __init disp_cc_0_anorak_init(void)
{
	return platform_driver_register(&disp_cc_0_anorak_driver);
}
subsys_initcall(disp_cc_0_anorak_init);

static void __exit disp_cc_0_anorak_exit(void)
{
	platform_driver_unregister(&disp_cc_0_anorak_driver);
}
module_exit(disp_cc_0_anorak_exit);

MODULE_DESCRIPTION("QTI DISP_CC_0 ANORAK Driver");
MODULE_LICENSE("GPL");
