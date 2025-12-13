// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2022, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,dispcc-sc7280.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	P_BI_TCXO,
	P_DISP_CC_PLL0_OUT_EVEN,
	P_DISP_CC_PLL0_OUT_MAIN,
	P_DP_PHY_PLL_LINK_CLK,
	P_DP_PHY_PLL_VCO_DIV_CLK,
	P_DSI0_PHY_PLL_OUT_BYTECLK,
	P_DSI0_PHY_PLL_OUT_DSICLK,
	P_EDP_PHY_PLL_LINK_CLK,
	P_EDP_PHY_PLL_VCO_DIV_CLK,
	P_GCC_DISP_GPLL0_CLK,
};

static const struct pll_vco lucid_vco[] = {
	{ 249600000, 2000000000, 0 },
};

/* 1520MHz Configuration*/
static const struct alpha_pll_config disp_cc_pll0_config = {
	.l = 0x4F,
	.alpha = 0x2AAA,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002261,
	.config_ctl_hi1_val = 0x329A299C,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x00000000,
};

static struct clk_alpha_pll disp_cc_pll0 = {
	.offset = 0x0,
	.vco_table = lucid_vco,
	.num_vco = ARRAY_SIZE(lucid_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_ops,
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
	{ P_DP_PHY_PLL_LINK_CLK, 1 },
	{ P_DP_PHY_PLL_VCO_DIV_CLK, 2 },
};

static const struct clk_parent_data disp_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dp_phy_pll_link_clk" },
	{ .fw_name = "dp_phy_pll_vco_div_clk" },
};

static const struct parent_map disp_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_DSI0_PHY_PLL_OUT_BYTECLK, 1 },
};

static const struct clk_parent_data disp_cc_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dsi0_phy_pll_out_byteclk" },
};

static const struct parent_map disp_cc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_EDP_PHY_PLL_LINK_CLK, 1 },
	{ P_EDP_PHY_PLL_VCO_DIV_CLK, 2 },
};

static const struct clk_parent_data disp_cc_parent_data_3[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "edp_phy_pll_link_clk" },
	{ .fw_name = "edp_phy_pll_vco_div_clk" },
};

static const struct parent_map disp_cc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_DISP_CC_PLL0_OUT_MAIN, 1 },
	{ P_GCC_DISP_GPLL0_CLK, 4 },
	{ P_DISP_CC_PLL0_OUT_EVEN, 5 },
};

static const struct clk_parent_data disp_cc_parent_data_4[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &disp_cc_pll0.clkr.hw },
	{ .fw_name = "gcc_disp_gpll0_clk" },
	{ .hw = &disp_cc_pll0.clkr.hw },
};

static const struct parent_map disp_cc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_DISP_GPLL0_CLK, 4 },
};

static const struct clk_parent_data disp_cc_parent_data_5[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "gcc_disp_gpll0_clk" },
};

static const struct parent_map disp_cc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_DSI0_PHY_PLL_OUT_DSICLK, 1 },
};

static const struct clk_parent_data disp_cc_parent_data_6[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "dsi0_phy_pll_out_dsiclk" },
};

static const struct freq_tbl ftbl_disp_cc_mdss_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(37500000, P_GCC_DISP_GPLL0_CLK, 16, 0, 0),
	F(75000000, P_GCC_DISP_GPLL0_CLK, 8, 0, 0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_ahb_clk_src = {
	.cmd_rcgr = 0x1170,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_5,
	.freq_tbl = ftbl_disp_cc_mdss_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_ahb_clk_src",
		.parent_data = disp_cc_parent_data_5,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_5),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 disp_cc_mdss_byte0_clk_src = {
	.cmd_rcgr = 0x10d8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_2,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_byte0_clk_src",
		.parent_data = disp_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_byte2_ops,
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_dp_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_dp_aux_clk_src = {
	.cmd_rcgr = 0x1158,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.freq_tbl = ftbl_disp_cc_mdss_dp_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_aux_clk_src",
		.parent_data = disp_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 disp_cc_mdss_dp_crypto_clk_src = {
	.cmd_rcgr = 0x1128,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_1,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_crypto_clk_src",
		.parent_data = disp_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_1),
		.ops = &clk_byte2_ops,
	},
};

static struct clk_rcg2 disp_cc_mdss_dp_link_clk_src = {
	.cmd_rcgr = 0x110c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_1,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_link_clk_src",
		.parent_data = disp_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_1),
		.ops = &clk_byte2_ops,
	},
};

static struct clk_rcg2 disp_cc_mdss_dp_pixel_clk_src = {
	.cmd_rcgr = 0x1140,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_1,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_pixel_clk_src",
		.parent_data = disp_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_1),
		.ops = &clk_dp_ops,
	},
};

static struct clk_rcg2 disp_cc_mdss_edp_aux_clk_src = {
	.cmd_rcgr = 0x11d0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.freq_tbl = ftbl_disp_cc_mdss_dp_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_edp_aux_clk_src",
		.parent_data = disp_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 disp_cc_mdss_edp_link_clk_src = {
	.cmd_rcgr = 0x11a0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_3,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_edp_link_clk_src",
		.parent_data = disp_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_byte2_ops,
	},
};

static struct clk_rcg2 disp_cc_mdss_edp_pixel_clk_src = {
	.cmd_rcgr = 0x1188,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_3,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_edp_pixel_clk_src",
		.parent_data = disp_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_3),
		.ops = &clk_dp_ops,
	},
};

static struct clk_rcg2 disp_cc_mdss_esc0_clk_src = {
	.cmd_rcgr = 0x10f4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_2,
	.freq_tbl = ftbl_disp_cc_mdss_dp_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_esc0_clk_src",
		.parent_data = disp_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_mdp_clk_src[] = {
	F(200000000, P_GCC_DISP_GPLL0_CLK, 3, 0, 0),
	F(300000000, P_GCC_DISP_GPLL0_CLK, 2, 0, 0),
	F(380000000, P_DISP_CC_PLL0_OUT_MAIN, 4, 0, 0),
	F(506666667, P_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	F(608000000, P_DISP_CC_PLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_mdp_clk_src = {
	.cmd_rcgr = 0x1090,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_4,
	.freq_tbl = ftbl_disp_cc_mdss_mdp_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_mdp_clk_src",
		.parent_data = disp_cc_parent_data_4,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_4),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 disp_cc_mdss_pclk0_clk_src = {
	.cmd_rcgr = 0x1078,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_6,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_pclk0_clk_src",
		.parent_data = disp_cc_parent_data_6,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_pixel_ops,
	},
};

static struct clk_rcg2 disp_cc_mdss_rot_clk_src = {
	.cmd_rcgr = 0x10a8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_4,
	.freq_tbl = ftbl_disp_cc_mdss_mdp_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_rot_clk_src",
		.parent_data = disp_cc_parent_data_4,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_4),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 disp_cc_mdss_vsync_clk_src = {
	.cmd_rcgr = 0x10c0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.freq_tbl = ftbl_disp_cc_mdss_dp_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_vsync_clk_src",
		.parent_data = disp_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(disp_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div disp_cc_mdss_byte0_div_clk_src = {
	.reg = 0x10f0,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "disp_cc_mdss_byte0_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&disp_cc_mdss_byte0_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div disp_cc_mdss_dp_link_div_clk_src = {
	.reg = 0x1124,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "disp_cc_mdss_dp_link_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&disp_cc_mdss_dp_link_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div disp_cc_mdss_edp_link_div_clk_src = {
	.reg = 0x11b8,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "disp_cc_mdss_edp_link_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&disp_cc_mdss_edp_link_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch disp_cc_mdss_ahb_clk = {
	.halt_reg = 0x1050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_byte0_clk = {
	.halt_reg = 0x1030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_byte0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_byte0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_byte0_intf_clk = {
	.halt_reg = 0x1034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_byte0_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_byte0_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_aux_clk = {
	.halt_reg = 0x104c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x104c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_dp_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_crypto_clk = {
	.halt_reg = 0x1044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_crypto_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_dp_crypto_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_link_clk = {
	.halt_reg = 0x103c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x103c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_link_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_dp_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_link_intf_clk = {
	.halt_reg = 0x1040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_link_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_dp_link_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_pixel_clk = {
	.halt_reg = 0x1048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_pixel_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_dp_pixel_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_edp_aux_clk = {
	.halt_reg = 0x1060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1060,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_edp_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_edp_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_edp_link_clk = {
	.halt_reg = 0x1058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_edp_link_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_edp_link_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_edp_link_intf_clk = {
	.halt_reg = 0x105c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x105c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_edp_link_intf_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_edp_link_div_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_edp_pixel_clk = {
	.halt_reg = 0x1054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_edp_pixel_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_edp_pixel_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_esc0_clk = {
	.halt_reg = 0x1038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_esc0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_esc0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_mdp_clk = {
	.halt_reg = 0x1014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_mdp_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_mdp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_mdp_lut_clk = {
	.halt_reg = 0x1024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x1024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_mdp_lut_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_mdp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_non_gdsc_ahb_clk = {
	.halt_reg = 0x2004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x2004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_non_gdsc_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_pclk0_clk = {
	.halt_reg = 0x1010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_pclk0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_pclk0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_rot_clk = {
	.halt_reg = 0x101c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x101c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_rot_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_rot_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_rscc_ahb_clk = {
	.halt_reg = 0x200c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x200c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_rscc_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_ahb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_rscc_vsync_clk = {
	.halt_reg = 0x2008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_rscc_vsync_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_vsync_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_vsync_clk = {
	.halt_reg = 0x102c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x102c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_vsync_clk",
			.parent_hws = (const struct clk_hw*[]){
				&disp_cc_mdss_vsync_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_sleep_clk = {
	.halt_reg = 0x5004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc disp_cc_mdss_core_gdsc = {
	.gdscr = 0x1004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "disp_cc_mdss_core_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = HW_CTRL | RETAIN_FF_ENABLE,
};

static struct clk_regmap *disp_cc_sc7280_clocks[] = {
	[DISP_CC_MDSS_AHB_CLK] = &disp_cc_mdss_ahb_clk.clkr,
	[DISP_CC_MDSS_AHB_CLK_SRC] = &disp_cc_mdss_ahb_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_CLK] = &disp_cc_mdss_byte0_clk.clkr,
	[DISP_CC_MDSS_BYTE0_CLK_SRC] = &disp_cc_mdss_byte0_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_DIV_CLK_SRC] = &disp_cc_mdss_byte0_div_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_INTF_CLK] = &disp_cc_mdss_byte0_intf_clk.clkr,
	[DISP_CC_MDSS_DP_AUX_CLK] = &disp_cc_mdss_dp_aux_clk.clkr,
	[DISP_CC_MDSS_DP_AUX_CLK_SRC] = &disp_cc_mdss_dp_aux_clk_src.clkr,
	[DISP_CC_MDSS_DP_CRYPTO_CLK] = &disp_cc_mdss_dp_crypto_clk.clkr,
	[DISP_CC_MDSS_DP_CRYPTO_CLK_SRC] = &disp_cc_mdss_dp_crypto_clk_src.clkr,
	[DISP_CC_MDSS_DP_LINK_CLK] = &disp_cc_mdss_dp_link_clk.clkr,
	[DISP_CC_MDSS_DP_LINK_CLK_SRC] = &disp_cc_mdss_dp_link_clk_src.clkr,
	[DISP_CC_MDSS_DP_LINK_DIV_CLK_SRC] =
		&disp_cc_mdss_dp_link_div_clk_src.clkr,
	[DISP_CC_MDSS_DP_LINK_INTF_CLK] = &disp_cc_mdss_dp_link_intf_clk.clkr,
	[DISP_CC_MDSS_DP_PIXEL_CLK] = &disp_cc_mdss_dp_pixel_clk.clkr,
	[DISP_CC_MDSS_DP_PIXEL_CLK_SRC] = &disp_cc_mdss_dp_pixel_clk_src.clkr,
	[DISP_CC_MDSS_EDP_AUX_CLK] = &disp_cc_mdss_edp_aux_clk.clkr,
	[DISP_CC_MDSS_EDP_AUX_CLK_SRC] = &disp_cc_mdss_edp_aux_clk_src.clkr,
	[DISP_CC_MDSS_EDP_LINK_CLK] = &disp_cc_mdss_edp_link_clk.clkr,
	[DISP_CC_MDSS_EDP_LINK_CLK_SRC] = &disp_cc_mdss_edp_link_clk_src.clkr,
	[DISP_CC_MDSS_EDP_LINK_DIV_CLK_SRC] =
		&disp_cc_mdss_edp_link_div_clk_src.clkr,
	[DISP_CC_MDSS_EDP_LINK_INTF_CLK] = &disp_cc_mdss_edp_link_intf_clk.clkr,
	[DISP_CC_MDSS_EDP_PIXEL_CLK] = &disp_cc_mdss_edp_pixel_clk.clkr,
	[DISP_CC_MDSS_EDP_PIXEL_CLK_SRC] = &disp_cc_mdss_edp_pixel_clk_src.clkr,
	[DISP_CC_MDSS_ESC0_CLK] = &disp_cc_mdss_esc0_clk.clkr,
	[DISP_CC_MDSS_ESC0_CLK_SRC] = &disp_cc_mdss_esc0_clk_src.clkr,
	[DISP_CC_MDSS_MDP_CLK] = &disp_cc_mdss_mdp_clk.clkr,
	[DISP_CC_MDSS_MDP_CLK_SRC] = &disp_cc_mdss_mdp_clk_src.clkr,
	[DISP_CC_MDSS_MDP_LUT_CLK] = &disp_cc_mdss_mdp_lut_clk.clkr,
	[DISP_CC_MDSS_NON_GDSC_AHB_CLK] = &disp_cc_mdss_non_gdsc_ahb_clk.clkr,
	[DISP_CC_MDSS_PCLK0_CLK] = &disp_cc_mdss_pclk0_clk.clkr,
	[DISP_CC_MDSS_PCLK0_CLK_SRC] = &disp_cc_mdss_pclk0_clk_src.clkr,
	[DISP_CC_MDSS_ROT_CLK] = &disp_cc_mdss_rot_clk.clkr,
	[DISP_CC_MDSS_ROT_CLK_SRC] = &disp_cc_mdss_rot_clk_src.clkr,
	[DISP_CC_MDSS_RSCC_AHB_CLK] = &disp_cc_mdss_rscc_ahb_clk.clkr,
	[DISP_CC_MDSS_RSCC_VSYNC_CLK] = &disp_cc_mdss_rscc_vsync_clk.clkr,
	[DISP_CC_MDSS_VSYNC_CLK] = &disp_cc_mdss_vsync_clk.clkr,
	[DISP_CC_MDSS_VSYNC_CLK_SRC] = &disp_cc_mdss_vsync_clk_src.clkr,
	[DISP_CC_PLL0] = &disp_cc_pll0.clkr,
	[DISP_CC_SLEEP_CLK] = &disp_cc_sleep_clk.clkr,
};

static struct gdsc *disp_cc_sc7280_gdscs[] = {
	[DISP_CC_MDSS_CORE_GDSC] = &disp_cc_mdss_core_gdsc,
};

static const struct qcom_reset_map disp_cc_sc7280_resets[] = {
	[DISP_CC_MDSS_CORE_BCR] = { 0x1000 },
	[DISP_CC_MDSS_RSCC_BCR] = { 0x2000 },
};

static const struct regmap_config disp_cc_sc7280_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x10000,
	.fast_io = true,
};

static const struct qcom_cc_desc disp_cc_sc7280_desc = {
	.config = &disp_cc_sc7280_regmap_config,
	.clks = disp_cc_sc7280_clocks,
	.num_clks = ARRAY_SIZE(disp_cc_sc7280_clocks),
	.gdscs = disp_cc_sc7280_gdscs,
	.num_gdscs = ARRAY_SIZE(disp_cc_sc7280_gdscs),
	.resets = disp_cc_sc7280_resets,
	.num_resets = ARRAY_SIZE(disp_cc_sc7280_resets),
};

static const struct of_device_id disp_cc_sc7280_match_table[] = {
	{ .compatible = "qcom,sc7280-dispcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, disp_cc_sc7280_match_table);

static int disp_cc_sc7280_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &disp_cc_sc7280_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_lucid_pll_configure(&disp_cc_pll0, regmap, &disp_cc_pll0_config);

	/* Keep some clocks always-on */
	qcom_branch_set_clk_en(regmap, 0x5008); /* DISP_CC_XO_CLK */

	return qcom_cc_really_probe(&pdev->dev, &disp_cc_sc7280_desc, regmap);
}

static struct platform_driver disp_cc_sc7280_driver = {
	.probe = disp_cc_sc7280_probe,
	.driver = {
		.name = "disp_cc-sc7280",
		.of_match_table = disp_cc_sc7280_match_table,
	},
};

module_platform_driver(disp_cc_sc7280_driver);

MODULE_DESCRIPTION("QTI DISP_CC sc7280 Driver");
MODULE_LICENSE("GPL v2");
