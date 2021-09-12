// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Konrad Dybcio <konrad.dybcio@somainline.org>
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/clk.h>

#include <dt-bindings/clock/qcom,mmcc-msm8994.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-alpha-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"
#include "gdsc.h"


enum {
	P_XO,
	P_GPLL0,
	P_MMPLL0,
	P_MMPLL1,
	P_MMPLL3,
	P_MMPLL4,
	P_MMPLL5, /* Is this one even used by anything? Downstream doesn't tell. */
	P_DSI0PLL,
	P_DSI1PLL,
	P_DSI0PLL_BYTE,
	P_DSI1PLL_BYTE,
	P_HDMIPLL,
};
static const struct parent_map mmcc_xo_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 5 }
};

static const struct clk_parent_data mmcc_xo_gpll0[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "gpll0" },
};

static const struct parent_map mmss_xo_hdmi_map[] = {
	{ P_XO, 0 },
	{ P_HDMIPLL, 3 }
};

static const struct clk_parent_data mmss_xo_hdmi[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "hdmipll" },
};

static const struct parent_map mmcc_xo_dsi0pll_dsi1pll_map[] = {
	{ P_XO, 0 },
	{ P_DSI0PLL, 1 },
	{ P_DSI1PLL, 2 }
};

static const struct clk_parent_data mmcc_xo_dsi0pll_dsi1pll[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "dsi0pll" },
	{ .fw_name = "dsi1pll" },
};

static const struct parent_map mmcc_xo_dsibyte_map[] = {
	{ P_XO, 0 },
	{ P_DSI0PLL_BYTE, 1 },
	{ P_DSI1PLL_BYTE, 2 }
};

static const struct clk_parent_data mmcc_xo_dsibyte[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "dsi0pllbyte" },
	{ .fw_name = "dsi1pllbyte" },
};

static struct pll_vco mmpll_p_vco[] = {
	{ 250000000, 500000000, 3 },
	{ 500000000, 1000000000, 2 },
	{ 1000000000, 1500000000, 1 },
	{ 1500000000, 2000000000, 0 },
};

static struct pll_vco mmpll_t_vco[] = {
	{ 500000000, 1500000000, 0 },
};

static const struct alpha_pll_config mmpll_p_config = {
	.post_div_mask = 0xf00,
};

static struct clk_alpha_pll mmpll0_early = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_p_vco,
	.num_vco = ARRAY_SIZE(mmpll_p_vco),
	.clkr = {
		.enable_reg = 0x100,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmpll0_early",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_alpha_pll_postdiv mmpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll0",
		.parent_hws = (const struct clk_hw *[]){ &mmpll0_early.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll mmpll1_early = {
	.offset = 0x30,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_p_vco,
	.num_vco = ARRAY_SIZE(mmpll_p_vco),
	.clkr = {
		.enable_reg = 0x100,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "mmpll1_early",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		}
	},
};

static struct clk_alpha_pll_postdiv mmpll1 = {
	.offset = 0x30,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll1",
		.parent_hws = (const struct clk_hw *[]){ &mmpll1_early.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll mmpll3_early = {
	.offset = 0x60,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_p_vco,
	.num_vco = ARRAY_SIZE(mmpll_p_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll3_early",
		.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
	},
};

static struct clk_alpha_pll_postdiv mmpll3 = {
	.offset = 0x60,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll3",
		.parent_hws = (const struct clk_hw *[]){ &mmpll3_early.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_alpha_pll mmpll4_early = {
	.offset = 0x90,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_t_vco,
	.num_vco = ARRAY_SIZE(mmpll_t_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll4_early",
		.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
	},
};

static struct clk_alpha_pll_postdiv mmpll4 = {
	.offset = 0x90,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll4",
		.parent_hws = (const struct clk_hw *[]){ &mmpll4_early.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct parent_map mmcc_xo_gpll0_mmpll1_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 5 },
	{ P_MMPLL1, 2 }
};

static const struct clk_parent_data mmcc_xo_gpll0_mmpll1[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "gpll0" },
	{ .hw = &mmpll1.clkr.hw },
};

static const struct parent_map mmcc_xo_gpll0_mmpll0_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 5 },
	{ P_MMPLL0, 1 }
};

static const struct clk_parent_data mmcc_xo_gpll0_mmpll0[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "gpll0" },
	{ .hw = &mmpll0.clkr.hw },
};

static const struct parent_map mmcc_xo_gpll0_mmpll0_mmpll3_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 5 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL3, 3 }
};

static const struct clk_parent_data mmcc_xo_gpll0_mmpll0_mmpll3[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "gpll0" },
	{ .hw = &mmpll0.clkr.hw },
	{ .hw = &mmpll3.clkr.hw },
};

static const struct parent_map mmcc_xo_gpll0_mmpll0_mmpll4_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 5 },
	{ P_MMPLL0, 1 },
	{ P_MMPLL4, 3 }
};

static const struct clk_parent_data mmcc_xo_gpll0_mmpll0_mmpll4[] = {
	{ .fw_name = "xo" },
	{ .fw_name = "gpll0" },
	{ .hw = &mmpll0.clkr.hw },
	{ .hw = &mmpll4.clkr.hw },
};

static struct clk_alpha_pll mmpll5_early = {
	.offset = 0xc0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = mmpll_p_vco,
	.num_vco = ARRAY_SIZE(mmpll_p_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll5_early",
		.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
	},
};

static struct clk_alpha_pll_postdiv mmpll5 = {
	.offset = 0xc0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmpll5",
		.parent_hws = (const struct clk_hw *[]){ &mmpll5_early.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_ahb_clk_src[] = {
	/* Note: There might be more frequencies desired here. */
	F(19200000, P_XO, 1, 0, 0),
	F(40000000, P_GPLL0, 15, 0, 0),
	F(80000000, P_MMPLL0, 10, 0, 0),
	{ }
};

static struct clk_rcg2 ahb_clk_src = {
	.cmd_rcgr = 0x5000,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ahb_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_axi_clk_src[] = {
	F(75000000, P_GPLL0, 8, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(333430000, P_MMPLL1, 3.5, 0, 0),
	F(466800000, P_MMPLL1, 2.5, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_axi_clk_src_8992[] = {
	F(75000000, P_GPLL0, 8, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(300000000, P_GPLL0, 2, 0, 0),
	F(404000000, P_MMPLL1, 2, 0, 0),
	{ }
};

static struct clk_rcg2 axi_clk_src = {
	.cmd_rcgr = 0x5040,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll1_map,
	.freq_tbl = ftbl_axi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "axi_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll1,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_csi0_1_2_3_clk_src[] = {
	F(100000000, P_GPLL0, 6, 0, 0),
	F(240000000, P_GPLL0, 2.5, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_csi0_1_2_3_clk_src_8992[] = {
	F(100000000, P_GPLL0, 6, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	{ }
};

static struct clk_rcg2 csi0_clk_src = {
	.cmd_rcgr = 0x3090,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_csi0_1_2_3_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi0_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_vcodec0_clk_src[] = {
	F(66670000, P_GPLL0, 9, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(133330000, P_GPLL0, 4.5, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(200000000, P_MMPLL0, 4, 0, 0),
	F(240000000, P_GPLL0, 2.5, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(510000000, P_MMPLL3, 2, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_vcodec0_clk_src_8992[] = {
	F(66670000, P_GPLL0, 9, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(133330000, P_GPLL0, 4.5, 0, 0),
	F(200000000, P_MMPLL0, 4, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(510000000, P_MMPLL3, 2, 0, 0),
	{ }
};

static struct clk_rcg2 vcodec0_clk_src = {
	.cmd_rcgr = 0x1000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_mmpll3_map,
	.freq_tbl = ftbl_vcodec0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vcodec0_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0_mmpll3,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0_mmpll3),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi1_clk_src = {
	.cmd_rcgr = 0x3100,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_csi0_1_2_3_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi1_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi2_clk_src = {
	.cmd_rcgr = 0x3160,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_csi0_1_2_3_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi2_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi3_clk_src = {
	.cmd_rcgr = 0x31c0,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_csi0_1_2_3_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi3_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_vfe0_clk_src[] = {
	F(80000000, P_GPLL0, 7.5, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(400000000, P_MMPLL0, 2, 0, 0),
	F(480000000, P_MMPLL4, 2, 0, 0),
	F(533330000, P_MMPLL0, 1.5, 0, 0),
	F(600000000, P_GPLL0, 1, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_vfe0_1_clk_src_8992[] = {
	F(80000000, P_GPLL0, 7.5, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(480000000, P_MMPLL4, 2, 0, 0),
	F(600000000, P_GPLL0, 1, 0, 0),
	{ }
};

static struct clk_rcg2 vfe0_clk_src = {
	.cmd_rcgr = 0x3600,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_mmpll4_map,
	.freq_tbl = ftbl_vfe0_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vfe0_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0_mmpll4,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0_mmpll4),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_vfe1_clk_src[] = {
	F(80000000, P_GPLL0, 7.5, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(400000000, P_MMPLL0, 2, 0, 0),
	F(533330000, P_MMPLL0, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 vfe1_clk_src = {
	.cmd_rcgr = 0x3620,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_mmpll4_map,
	.freq_tbl = ftbl_vfe1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vfe1_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0_mmpll4,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0_mmpll4),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_cpp_clk_src[] = {
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(480000000, P_MMPLL4, 2, 0, 0),
	F(600000000, P_GPLL0, 1, 0, 0),
	F(640000000, P_MMPLL4, 1.5, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_cpp_clk_src_8992[] = {
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(480000000, P_MMPLL4, 2, 0, 0),
	F(640000000, P_MMPLL4, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 cpp_clk_src = {
	.cmd_rcgr = 0x3640,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_mmpll4_map,
	.freq_tbl = ftbl_cpp_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cpp_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0_mmpll4,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0_mmpll4),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_jpeg0_1_clk_src[] = {
	F(75000000, P_GPLL0, 8, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(228570000, P_MMPLL0, 3.5, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(480000000, P_MMPLL4, 2, 0, 0),
	{ }
};

static struct clk_rcg2 jpeg1_clk_src = {
	.cmd_rcgr = 0x3520,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_mmpll4_map,
	.freq_tbl = ftbl_jpeg0_1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "jpeg1_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0_mmpll4,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0_mmpll4),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_jpeg2_clk_src[] = {
	F(75000000, P_GPLL0, 8, 0, 0),
	F(133330000, P_GPLL0, 4.5, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(228570000, P_MMPLL0, 3.5, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 jpeg2_clk_src = {
	.cmd_rcgr = 0x3540,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_jpeg2_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "jpeg2_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_csi2phytimer_clk_src[] = {
	F(50000000, P_GPLL0, 12, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_MMPLL0, 4, 0, 0),
	{ }
};

static struct clk_rcg2 csi2phytimer_clk_src = {
	.cmd_rcgr = 0x3060,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_csi2phytimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi2phytimer_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_fd_core_clk_src[] = {
	F(60000000, P_GPLL0, 10, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(400000000, P_MMPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 fd_core_clk_src = {
	.cmd_rcgr = 0x3b00,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_fd_core_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "fd_core_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_mdp_clk_src[] = {
	F(85710000, P_GPLL0, 7, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(120000000, P_GPLL0, 5, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(171430000, P_GPLL0, 3.5, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(240000000, P_GPLL0, 2.5, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	F(300000000, P_GPLL0, 2, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(400000000, P_MMPLL0, 2, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_mdp_clk_src_8992[] = {
	F(85710000, P_GPLL0, 7, 0, 0),
	F(171430000, P_GPLL0, 3.5, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(240000000, P_GPLL0, 2.5, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(400000000, P_MMPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 mdp_clk_src = {
	.cmd_rcgr = 0x2040,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_mdp_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mdp_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 pclk0_clk_src = {
	.cmd_rcgr = 0x2000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsi0pll_dsi1pll_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pclk0_clk_src",
		.parent_data = mmcc_xo_dsi0pll_dsi1pll,
		.num_parents = ARRAY_SIZE(mmcc_xo_dsi0pll_dsi1pll),
		.ops = &clk_pixel_ops,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_rcg2 pclk1_clk_src = {
	.cmd_rcgr = 0x2020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsi0pll_dsi1pll_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pclk1_clk_src",
		.parent_data = mmcc_xo_dsi0pll_dsi1pll,
		.num_parents = ARRAY_SIZE(mmcc_xo_dsi0pll_dsi1pll),
		.ops = &clk_pixel_ops,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
	},
};

static const struct freq_tbl ftbl_ocmemnoc_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(75000000, P_GPLL0, 8, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(228570000, P_MMPLL0, 3.5, 0, 0),
	F(266670000, P_MMPLL0, 3, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(400000000, P_MMPLL0, 2, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_ocmemnoc_clk_src_8992[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(75000000, P_GPLL0, 8, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(150000000, P_GPLL0, 4, 0, 0),
	F(320000000, P_MMPLL0, 2.5, 0, 0),
	F(400000000, P_MMPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 ocmemnoc_clk_src = {
	.cmd_rcgr = 0x5090,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_ocmemnoc_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "ocmemnoc_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_cci_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(37500000, P_GPLL0, 16, 0, 0),
	F(50000000, P_GPLL0, 12, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	{ }
};

static struct clk_rcg2 cci_clk_src = {
	.cmd_rcgr = 0x3300,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_map,
	.freq_tbl = ftbl_cci_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cci_clk_src",
		.parent_data = mmcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_mmss_gp0_1_clk_src[] = {
	F(10000, P_XO, 16, 10, 120),
	F(24000, P_GPLL0, 16, 1, 50),
	F(6000000, P_GPLL0, 10, 1, 10),
	F(12000000, P_GPLL0, 10, 1, 5),
	F(13000000, P_GPLL0, 4, 13, 150),
	F(24000000, P_GPLL0, 5, 1, 5),
	{ }
};

static struct clk_rcg2 mmss_gp0_clk_src = {
	.cmd_rcgr = 0x3420,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_map,
	.freq_tbl = ftbl_mmss_gp0_1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmss_gp0_clk_src",
		.parent_data = mmcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 mmss_gp1_clk_src = {
	.cmd_rcgr = 0x3450,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_map,
	.freq_tbl = ftbl_mmss_gp0_1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mmss_gp1_clk_src",
		.parent_data = mmcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 jpeg0_clk_src = {
	.cmd_rcgr = 0x3500,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_mmpll4_map,
	.freq_tbl = ftbl_jpeg0_1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "jpeg0_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0_mmpll4,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0_mmpll4),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 jpeg_dma_clk_src = {
	.cmd_rcgr = 0x3560,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_mmpll4_map,
	.freq_tbl = ftbl_jpeg0_1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "jpeg_dma_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0_mmpll4,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0_mmpll4),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_mclk0_1_2_3_clk_src[] = {
	F(4800000, P_XO, 4, 0, 0),
	F(6000000, P_GPLL0, 10, 1, 10),
	F(8000000, P_GPLL0, 15, 1, 5),
	F(9600000, P_XO, 2, 0, 0),
	F(16000000, P_MMPLL0, 10, 1, 5),
	F(19200000, P_XO, 1, 0, 0),
	F(24000000, P_GPLL0, 5, 1, 5),
	F(32000000, P_MMPLL0, 5, 1, 5),
	F(48000000, P_GPLL0, 12.5, 0, 0),
	F(64000000, P_MMPLL0, 12.5, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_mclk0_clk_src_8992[] = {
	F(4800000, P_XO, 4, 0, 0),
	F(6000000, P_MMPLL4, 10, 1, 16),
	F(8000000, P_MMPLL4, 10, 1, 12),
	F(9600000, P_XO, 2, 0, 0),
	F(12000000, P_MMPLL4, 10, 1, 8),
	F(16000000, P_MMPLL4, 10, 1, 6),
	F(19200000, P_XO, 1, 0, 0),
	F(24000000, P_MMPLL4, 10, 1, 4),
	F(32000000, P_MMPLL4, 10, 1, 3),
	F(48000000, P_MMPLL4, 10, 1, 2),
	F(64000000, P_MMPLL4, 15, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_mclk1_2_3_clk_src_8992[] = {
	F(4800000, P_XO, 4, 0, 0),
	F(6000000, P_MMPLL4, 10, 1, 16),
	F(8000000, P_MMPLL4, 10, 1, 12),
	F(9600000, P_XO, 2, 0, 0),
	F(16000000, P_MMPLL4, 10, 1, 6),
	F(19200000, P_XO, 1, 0, 0),
	F(24000000, P_MMPLL4, 10, 1, 4),
	F(32000000, P_MMPLL4, 10, 1, 3),
	F(48000000, P_MMPLL4, 10, 1, 2),
	F(64000000, P_MMPLL4, 15, 0, 0),
	{ }
};

static struct clk_rcg2 mclk0_clk_src = {
	.cmd_rcgr = 0x3360,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_mclk0_1_2_3_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk0_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 mclk1_clk_src = {
	.cmd_rcgr = 0x3390,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_mclk0_1_2_3_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk1_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 mclk2_clk_src = {
	.cmd_rcgr = 0x33c0,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_mclk0_1_2_3_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk2_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 mclk3_clk_src = {
	.cmd_rcgr = 0x33f0,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_mclk0_1_2_3_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "mclk3_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_csi0_1phytimer_clk_src[] = {
	F(50000000, P_GPLL0, 12, 0, 0),
	F(100000000, P_GPLL0, 6, 0, 0),
	F(200000000, P_MMPLL0, 4, 0, 0),
	{ }
};

static struct clk_rcg2 csi0phytimer_clk_src = {
	.cmd_rcgr = 0x3000,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_csi0_1phytimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi0phytimer_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 csi1phytimer_clk_src = {
	.cmd_rcgr = 0x3030,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_mmpll0_map,
	.freq_tbl = ftbl_csi0_1phytimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "csi1phytimer_clk_src",
		.parent_data = mmcc_xo_gpll0_mmpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0_mmpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 byte0_clk_src = {
	.cmd_rcgr = 0x2120,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsibyte_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "byte0_clk_src",
		.parent_data = mmcc_xo_dsibyte,
		.num_parents = ARRAY_SIZE(mmcc_xo_dsibyte),
		.ops = &clk_byte2_ops,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_rcg2 byte1_clk_src = {
	.cmd_rcgr = 0x2140,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsibyte_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "byte1_clk_src",
		.parent_data = mmcc_xo_dsibyte,
		.num_parents = ARRAY_SIZE(mmcc_xo_dsibyte),
		.ops = &clk_byte2_ops,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
	},
};

static struct freq_tbl ftbl_mdss_esc0_1_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 esc0_clk_src = {
	.cmd_rcgr = 0x2160,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsibyte_map,
	.freq_tbl = ftbl_mdss_esc0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "esc0_clk_src",
		.parent_data = mmcc_xo_dsibyte,
		.num_parents = ARRAY_SIZE(mmcc_xo_dsibyte),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 esc1_clk_src = {
	.cmd_rcgr = 0x2180,
	.hid_width = 5,
	.parent_map = mmcc_xo_dsibyte_map,
	.freq_tbl = ftbl_mdss_esc0_1_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "esc1_clk_src",
		.parent_data = mmcc_xo_dsibyte,
		.num_parents = ARRAY_SIZE(mmcc_xo_dsibyte),
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl extpclk_freq_tbl[] = {
	{ .src = P_HDMIPLL },
	{ }
};

static struct clk_rcg2 extpclk_clk_src = {
	.cmd_rcgr = 0x2060,
	.hid_width = 5,
	.parent_map = mmss_xo_hdmi_map,
	.freq_tbl = extpclk_freq_tbl,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "extpclk_clk_src",
		.parent_data = mmss_xo_hdmi,
		.num_parents = ARRAY_SIZE(mmss_xo_hdmi),
		.ops = &clk_rcg2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct freq_tbl ftbl_hdmi_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 hdmi_clk_src = {
	.cmd_rcgr = 0x2100,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_map,
	.freq_tbl = ftbl_hdmi_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "hdmi_clk_src",
		.parent_data = mmcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct freq_tbl ftbl_mdss_vsync_clk[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 vsync_clk_src = {
	.cmd_rcgr = 0x2080,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_map,
	.freq_tbl = ftbl_mdss_vsync_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "vsync_clk_src",
		.parent_data = mmcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_rbbmtimer_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 rbbmtimer_clk_src = {
	.cmd_rcgr = 0x4090,
	.hid_width = 5,
	.parent_map = mmcc_xo_gpll0_map,
	.freq_tbl = ftbl_rbbmtimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "rbbmtimer_clk_src",
		.parent_data = mmcc_xo_gpll0,
		.num_parents = ARRAY_SIZE(mmcc_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch camss_ahb_clk = {
	.halt_reg = 0x348c,
	.clkr = {
		.enable_reg = 0x348c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_cci_cci_ahb_clk = {
	.halt_reg = 0x3348,
	.clkr = {
		.enable_reg = 0x3348,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_cci_cci_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_cci_cci_clk = {
	.halt_reg = 0x3344,
	.clkr = {
		.enable_reg = 0x3344,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_cci_cci_clk",
			.parent_hws = (const struct clk_hw *[]){ &cci_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_cpp_ahb_clk = {
	.halt_reg = 0x36b4,
	.clkr = {
		.enable_reg = 0x36b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_cpp_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_cpp_axi_clk = {
	.halt_reg = 0x36c4,
	.clkr = {
		.enable_reg = 0x36c4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_cpp_axi_clk",
			.parent_hws = (const struct clk_hw *[]){ &axi_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_cpp_clk = {
	.halt_reg = 0x36b0,
	.clkr = {
		.enable_reg = 0x36b0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_cpp_clk",
			.parent_hws = (const struct clk_hw *[]){ &cpp_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi0_ahb_clk = {
	.halt_reg = 0x30bc,
	.clkr = {
		.enable_reg = 0x30bc,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi0_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi0_clk = {
	.halt_reg = 0x30b4,
	.clkr = {
		.enable_reg = 0x30b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi0_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi0_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi0phy_clk = {
	.halt_reg = 0x30c4,
	.clkr = {
		.enable_reg = 0x30c4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi0phy_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi0_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi0pix_clk = {
	.halt_reg = 0x30e4,
	.clkr = {
		.enable_reg = 0x30e4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi0pix_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi0_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi0rdi_clk = {
	.halt_reg = 0x30d4,
	.clkr = {
		.enable_reg = 0x30d4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi0rdi_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi0_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi1_ahb_clk = {
	.halt_reg = 0x3128,
	.clkr = {
		.enable_reg = 0x3128,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi1_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi1_clk = {
	.halt_reg = 0x3124,
	.clkr = {
		.enable_reg = 0x3124,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi1_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi1phy_clk = {
	.halt_reg = 0x3134,
	.clkr = {
		.enable_reg = 0x3134,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi1phy_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi1pix_clk = {
	.halt_reg = 0x3154,
	.clkr = {
		.enable_reg = 0x3154,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi1pix_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi1rdi_clk = {
	.halt_reg = 0x3144,
	.clkr = {
		.enable_reg = 0x3144,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi1rdi_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi2_ahb_clk = {
	.halt_reg = 0x3188,
	.clkr = {
		.enable_reg = 0x3188,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi2_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi2_clk = {
	.halt_reg = 0x3184,
	.clkr = {
		.enable_reg = 0x3184,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi2_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi2phy_clk = {
	.halt_reg = 0x3194,
	.clkr = {
		.enable_reg = 0x3194,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi2phy_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi2pix_clk = {
	.halt_reg = 0x31b4,
	.clkr = {
		.enable_reg = 0x31b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi2pix_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi2rdi_clk = {
	.halt_reg = 0x31a4,
	.clkr = {
		.enable_reg = 0x31a4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi2rdi_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi3_ahb_clk = {
	.halt_reg = 0x31e8,
	.clkr = {
		.enable_reg = 0x31e8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi3_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi3_clk = {
	.halt_reg = 0x31e4,
	.clkr = {
		.enable_reg = 0x31e4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi3_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi3phy_clk = {
	.halt_reg = 0x31f4,
	.clkr = {
		.enable_reg = 0x31f4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi3phy_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi3pix_clk = {
	.halt_reg = 0x3214,
	.clkr = {
		.enable_reg = 0x3214,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi3pix_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi3rdi_clk = {
	.halt_reg = 0x3204,
	.clkr = {
		.enable_reg = 0x3204,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi3rdi_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi_vfe0_clk = {
	.halt_reg = 0x3704,
	.clkr = {
		.enable_reg = 0x3704,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi_vfe0_clk",
			.parent_hws = (const struct clk_hw *[]){ &vfe0_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_csi_vfe1_clk = {
	.halt_reg = 0x3714,
	.clkr = {
		.enable_reg = 0x3714,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_csi_vfe1_clk",
			.parent_hws = (const struct clk_hw *[]){ &vfe1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_gp0_clk = {
	.halt_reg = 0x3444,
	.clkr = {
		.enable_reg = 0x3444,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_gp0_clk",
			.parent_hws = (const struct clk_hw *[]){ &mmss_gp0_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_gp1_clk = {
	.halt_reg = 0x3474,
	.clkr = {
		.enable_reg = 0x3474,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_gp1_clk",
			.parent_hws = (const struct clk_hw *[]){ &mmss_gp1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_ispif_ahb_clk = {
	.halt_reg = 0x3224,
	.clkr = {
		.enable_reg = 0x3224,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_ispif_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_dma_clk = {
	.halt_reg = 0x35c0,
	.clkr = {
		.enable_reg = 0x35c0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_dma_clk",
			.parent_hws = (const struct clk_hw *[]){ &jpeg_dma_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_jpeg0_clk = {
	.halt_reg = 0x35a8,
	.clkr = {
		.enable_reg = 0x35a8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_jpeg0_clk",
			.parent_hws = (const struct clk_hw *[]){ &jpeg0_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_jpeg1_clk = {
	.halt_reg = 0x35ac,
	.clkr = {
		.enable_reg = 0x35ac,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_jpeg1_clk",
			.parent_hws = (const struct clk_hw *[]){ &jpeg1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_jpeg2_clk = {
	.halt_reg = 0x35b0,
	.clkr = {
		.enable_reg = 0x35b0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_jpeg2_clk",
			.parent_hws = (const struct clk_hw *[]){ &jpeg2_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_jpeg_ahb_clk = {
	.halt_reg = 0x35b4,
	.clkr = {
		.enable_reg = 0x35b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_jpeg_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_jpeg_jpeg_axi_clk = {
	.halt_reg = 0x35b8,
	.clkr = {
		.enable_reg = 0x35b8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_jpeg_jpeg_axi_clk",
			.parent_hws = (const struct clk_hw *[]){ &axi_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_mclk0_clk = {
	.halt_reg = 0x3384,
	.clkr = {
		.enable_reg = 0x3384,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_mclk0_clk",
			.parent_hws = (const struct clk_hw *[]){ &mclk0_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_mclk1_clk = {
	.halt_reg = 0x33b4,
	.clkr = {
		.enable_reg = 0x33b4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_mclk1_clk",
			.parent_hws = (const struct clk_hw *[]){ &mclk1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_mclk2_clk = {
	.halt_reg = 0x33e4,
	.clkr = {
		.enable_reg = 0x33e4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_mclk2_clk",
			.parent_hws = (const struct clk_hw *[]){ &mclk2_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_mclk3_clk = {
	.halt_reg = 0x3414,
	.clkr = {
		.enable_reg = 0x3414,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_mclk3_clk",
			.parent_hws = (const struct clk_hw *[]){ &mclk3_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_micro_ahb_clk = {
	.halt_reg = 0x3494,
	.clkr = {
		.enable_reg = 0x3494,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_micro_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_phy0_csi0phytimer_clk = {
	.halt_reg = 0x3024,
	.clkr = {
		.enable_reg = 0x3024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_phy0_csi0phytimer_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi0phytimer_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_phy1_csi1phytimer_clk = {
	.halt_reg = 0x3054,
	.clkr = {
		.enable_reg = 0x3054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_phy1_csi1phytimer_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi1phytimer_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_phy2_csi2phytimer_clk = {
	.halt_reg = 0x3084,
	.clkr = {
		.enable_reg = 0x3084,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_phy2_csi2phytimer_clk",
			.parent_hws = (const struct clk_hw *[]){ &csi2phytimer_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_top_ahb_clk = {
	.halt_reg = 0x3484,
	.clkr = {
		.enable_reg = 0x3484,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_top_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_vfe0_clk = {
	.halt_reg = 0x36a8,
	.clkr = {
		.enable_reg = 0x36a8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_vfe0_clk",
			.parent_hws = (const struct clk_hw *[]){ &vfe0_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_vfe1_clk = {
	.halt_reg = 0x36ac,
	.clkr = {
		.enable_reg = 0x36ac,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_vfe1_clk",
			.parent_hws = (const struct clk_hw *[]){ &vfe1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_vfe_ahb_clk = {
	.halt_reg = 0x36b8,
	.clkr = {
		.enable_reg = 0x36b8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_vfe_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch camss_vfe_vfe_axi_clk = {
	.halt_reg = 0x36bc,
	.clkr = {
		.enable_reg = 0x36bc,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camss_vfe_vfe_axi_clk",
			.parent_hws = (const struct clk_hw *[]){ &axi_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch fd_ahb_clk = {
	.halt_reg = 0x3b74,
	.clkr = {
		.enable_reg = 0x3b74,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "fd_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch fd_axi_clk = {
	.halt_reg = 0x3b70,
	.clkr = {
		.enable_reg = 0x3b70,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "fd_axi_clk",
			.parent_hws = (const struct clk_hw *[]){ &axi_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch fd_core_clk = {
	.halt_reg = 0x3b68,
	.clkr = {
		.enable_reg = 0x3b68,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "fd_core_clk",
			.parent_hws = (const struct clk_hw *[]){ &fd_core_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch fd_core_uar_clk = {
	.halt_reg = 0x3b6c,
	.clkr = {
		.enable_reg = 0x3b6c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "fd_core_uar_clk",
			.parent_hws = (const struct clk_hw *[]){ &fd_core_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_ahb_clk = {
	.halt_reg = 0x2308,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2308,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_axi_clk = {
	.halt_reg = 0x2310,
	.clkr = {
		.enable_reg = 0x2310,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_axi_clk",
			.parent_hws = (const struct clk_hw *[]){ &axi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_byte0_clk = {
	.halt_reg = 0x233c,
	.clkr = {
		.enable_reg = 0x233c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_byte0_clk",
			.parent_hws = (const struct clk_hw *[]){ &byte0_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_byte1_clk = {
	.halt_reg = 0x2340,
	.clkr = {
		.enable_reg = 0x2340,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_byte1_clk",
			.parent_hws = (const struct clk_hw *[]){ &byte1_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_esc0_clk = {
	.halt_reg = 0x2344,
	.clkr = {
		.enable_reg = 0x2344,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_esc0_clk",
			.parent_hws = (const struct clk_hw *[]){ &esc0_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_esc1_clk = {
	.halt_reg = 0x2348,
	.clkr = {
		.enable_reg = 0x2348,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_esc1_clk",
			.parent_hws = (const struct clk_hw *[]){ &esc1_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_extpclk_clk = {
	.halt_reg = 0x2324,
	.clkr = {
		.enable_reg = 0x2324,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_extpclk_clk",
			.parent_hws = (const struct clk_hw *[]){ &extpclk_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_hdmi_ahb_clk = {
	.halt_reg = 0x230c,
	.clkr = {
		.enable_reg = 0x230c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_hdmi_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_hdmi_clk = {
	.halt_reg = 0x2338,
	.clkr = {
		.enable_reg = 0x2338,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_hdmi_clk",
			.parent_hws = (const struct clk_hw *[]){ &hdmi_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_mdp_clk = {
	.halt_reg = 0x231c,
	.clkr = {
		.enable_reg = 0x231c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_mdp_clk",
			.parent_hws = (const struct clk_hw *[]){ &mdp_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_pclk0_clk = {
	.halt_reg = 0x2314,
	.clkr = {
		.enable_reg = 0x2314,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_pclk0_clk",
			.parent_hws = (const struct clk_hw *[]){ &pclk0_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_pclk1_clk = {
	.halt_reg = 0x2318,
	.clkr = {
		.enable_reg = 0x2318,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_pclk1_clk",
			.parent_hws = (const struct clk_hw *[]){ &pclk1_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mdss_vsync_clk = {
	.halt_reg = 0x2328,
	.clkr = {
		.enable_reg = 0x2328,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdss_vsync_clk",
			.parent_hws = (const struct clk_hw *[]){ &vsync_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_misc_ahb_clk = {
	.halt_reg = 0x502c,
	.clkr = {
		.enable_reg = 0x502c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_misc_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_mmssnoc_axi_clk = {
	.halt_reg = 0x506c,
	.clkr = {
		.enable_reg = 0x506c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_mmssnoc_axi_clk",
			.parent_hws = (const struct clk_hw *[]){ &axi_clk_src.clkr.hw },
			.num_parents = 1,
			/* Gating this clock will wreck havoc among MMSS! */
			.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch mmss_s0_axi_clk = {
	.halt_reg = 0x5064,
	.clkr = {
		.enable_reg = 0x5064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_s0_axi_clk",
			.parent_hws = (const struct clk_hw *[]){ &axi_clk_src.clkr.hw, },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ocmemcx_ocmemnoc_clk = {
	.halt_reg = 0x4058,
	.clkr = {
		.enable_reg = 0x4058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "ocmemcx_ocmemnoc_clk",
			.parent_hws = (const struct clk_hw *[]){ &ocmemnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch oxili_gfx3d_clk = {
	.halt_reg = 0x4028,
	.clkr = {
		.enable_reg = 0x4028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "oxili_gfx3d_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "oxili_gfx3d_clk_src",
				.name = "oxili_gfx3d_clk_src"
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch oxili_rbbmtimer_clk = {
	.halt_reg = 0x40b0,
	.clkr = {
		.enable_reg = 0x40b0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "oxili_rbbmtimer_clk",
			.parent_hws = (const struct clk_hw *[]){ &rbbmtimer_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch oxilicx_ahb_clk = {
	.halt_reg = 0x403c,
	.clkr = {
		.enable_reg = 0x403c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "oxilicx_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_ahb_clk = {
	.halt_reg = 0x1030,
	.clkr = {
		.enable_reg = 0x1030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){ &ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_axi_clk = {
	.halt_reg = 0x1034,
	.clkr = {
		.enable_reg = 0x1034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_axi_clk",
			.parent_hws = (const struct clk_hw *[]){ &axi_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_ocmemnoc_clk = {
	.halt_reg = 0x1038,
	.clkr = {
		.enable_reg = 0x1038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_ocmemnoc_clk",
			.parent_hws = (const struct clk_hw *[]){ &ocmemnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_vcodec0_clk = {
	.halt_reg = 0x1028,
	.clkr = {
		.enable_reg = 0x1028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_vcodec0_clk",
			.parent_hws = (const struct clk_hw *[]){ &vcodec0_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_core0_vcodec_clk = {
	.halt_reg = 0x1048,
	.clkr = {
		.enable_reg = 0x1048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_core0_vcodec_clk",
			.parent_hws = (const struct clk_hw *[]){ &vcodec0_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_core1_vcodec_clk = {
	.halt_reg = 0x104c,
	.clkr = {
		.enable_reg = 0x104c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_core1_vcodec_clk",
			.parent_hws = (const struct clk_hw *[]){ &vcodec0_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch venus0_core2_vcodec_clk = {
	.halt_reg = 0x1054,
	.clkr = {
		.enable_reg = 0x1054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "venus0_core2_vcodec_clk",
			.parent_hws = (const struct clk_hw *[]){ &vcodec0_clk_src.clkr.hw },
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc venus_gdsc = {
	.gdscr = 0x1024,
	.cxcs = (unsigned int []){ 0x1038, 0x1034, 0x1048 },
	.cxc_count = 3,
	.pd = {
		.name = "venus_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc venus_core0_gdsc = {
	.gdscr = 0x1040,
	.cxcs = (unsigned int []){ 0x1048 },
	.cxc_count = 1,
	.pd = {
		.name = "venus_core0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = HW_CTRL,
};

static struct gdsc venus_core1_gdsc = {
	.gdscr = 0x1044,
	.cxcs = (unsigned int []){ 0x104c },
	.cxc_count = 1,
	.pd = {
	.name = "venus_core1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = HW_CTRL,
};

static struct gdsc venus_core2_gdsc = {
	.gdscr = 0x1050,
	.cxcs = (unsigned int []){ 0x1054 },
	.cxc_count = 1,
	.pd = {
		.name = "venus_core2_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = HW_CTRL,
};

static struct gdsc mdss_gdsc = {
	.gdscr = 0x2304,
	.cxcs = (unsigned int []){ 0x2310, 0x231c },
	.cxc_count = 2,
	.pd = {
		.name = "mdss_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc camss_top_gdsc = {
	.gdscr = 0x34a0,
	.cxcs = (unsigned int []){ 0x3704, 0x3714, 0x3494 },
	.cxc_count = 3,
	.pd = {
		.name = "camss_top_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc jpeg_gdsc = {
	.gdscr = 0x35a4,
	.cxcs = (unsigned int []){ 0x35a8 },
	.cxc_count = 1,
	.pd = {
		.name = "jpeg_gdsc",
	},
	.parent = &camss_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc vfe_gdsc = {
	.gdscr = 0x36a4,
	.cxcs = (unsigned int []){ 0x36bc },
	.cxc_count = 1,
	.pd = {
		.name = "vfe_gdsc",
	},
	.parent = &camss_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc cpp_gdsc = {
	.gdscr = 0x36d4,
	.cxcs = (unsigned int []){ 0x36c4, 0x36b0 },
	.cxc_count = 2,
	.pd = {
		.name = "cpp_gdsc",
	},
	.parent = &camss_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc fd_gdsc = {
	.gdscr = 0x3b64,
	.cxcs = (unsigned int []){ 0x3b70, 0x3b68 },
	.pd = {
		.name = "fd_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc oxili_cx_gdsc = {
	.gdscr = 0x4034,
	.pd = {
		.name = "oxili_cx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc oxili_gx_gdsc = {
	.gdscr = 0x4024,
	.cxcs = (unsigned int []){ 0x4028 },
	.cxc_count = 1,
	.pd = {
		.name = "oxili_gx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &oxili_cx_gdsc.pd,
	.flags = CLAMP_IO,
	.supply = "VDD_GFX",
};

static struct clk_regmap *mmcc_msm8994_clocks[] = {
	[MMPLL0_EARLY] = &mmpll0_early.clkr,
	[MMPLL0_PLL] = &mmpll0.clkr,
	[MMPLL1_EARLY] = &mmpll1_early.clkr,
	[MMPLL1_PLL] = &mmpll1.clkr,
	[MMPLL3_EARLY] = &mmpll3_early.clkr,
	[MMPLL3_PLL] = &mmpll3.clkr,
	[MMPLL4_EARLY] = &mmpll4_early.clkr,
	[MMPLL4_PLL] = &mmpll4.clkr,
	[MMPLL5_EARLY] = &mmpll5_early.clkr,
	[MMPLL5_PLL] = &mmpll5.clkr,
	[AHB_CLK_SRC] = &ahb_clk_src.clkr,
	[AXI_CLK_SRC] = &axi_clk_src.clkr,
	[CSI0_CLK_SRC] = &csi0_clk_src.clkr,
	[CSI1_CLK_SRC] = &csi1_clk_src.clkr,
	[CSI2_CLK_SRC] = &csi2_clk_src.clkr,
	[CSI3_CLK_SRC] = &csi3_clk_src.clkr,
	[VFE0_CLK_SRC] = &vfe0_clk_src.clkr,
	[VFE1_CLK_SRC] = &vfe1_clk_src.clkr,
	[CPP_CLK_SRC] = &cpp_clk_src.clkr,
	[JPEG0_CLK_SRC] = &jpeg0_clk_src.clkr,
	[JPEG1_CLK_SRC] = &jpeg1_clk_src.clkr,
	[JPEG2_CLK_SRC] = &jpeg2_clk_src.clkr,
	[CSI2PHYTIMER_CLK_SRC] = &csi2phytimer_clk_src.clkr,
	[FD_CORE_CLK_SRC] = &fd_core_clk_src.clkr,
	[MDP_CLK_SRC] = &mdp_clk_src.clkr,
	[PCLK0_CLK_SRC] = &pclk0_clk_src.clkr,
	[PCLK1_CLK_SRC] = &pclk1_clk_src.clkr,
	[OCMEMNOC_CLK_SRC] = &ocmemnoc_clk_src.clkr,
	[CCI_CLK_SRC] = &cci_clk_src.clkr,
	[MMSS_GP0_CLK_SRC] = &mmss_gp0_clk_src.clkr,
	[MMSS_GP1_CLK_SRC] = &mmss_gp1_clk_src.clkr,
	[JPEG_DMA_CLK_SRC] = &jpeg_dma_clk_src.clkr,
	[MCLK0_CLK_SRC] = &mclk0_clk_src.clkr,
	[MCLK1_CLK_SRC] = &mclk1_clk_src.clkr,
	[MCLK2_CLK_SRC] = &mclk2_clk_src.clkr,
	[MCLK3_CLK_SRC] = &mclk3_clk_src.clkr,
	[CSI0PHYTIMER_CLK_SRC] = &csi0phytimer_clk_src.clkr,
	[CSI1PHYTIMER_CLK_SRC] = &csi1phytimer_clk_src.clkr,
	[BYTE0_CLK_SRC] = &byte0_clk_src.clkr,
	[BYTE1_CLK_SRC] = &byte1_clk_src.clkr,
	[ESC0_CLK_SRC] = &esc0_clk_src.clkr,
	[ESC1_CLK_SRC] = &esc1_clk_src.clkr,
	[MDSS_ESC0_CLK] = &mdss_esc0_clk.clkr,
	[MDSS_ESC1_CLK] = &mdss_esc1_clk.clkr,
	[EXTPCLK_CLK_SRC] = &extpclk_clk_src.clkr,
	[HDMI_CLK_SRC] = &hdmi_clk_src.clkr,
	[VSYNC_CLK_SRC] = &vsync_clk_src.clkr,
	[RBBMTIMER_CLK_SRC] = &rbbmtimer_clk_src.clkr,
	[CAMSS_AHB_CLK] = &camss_ahb_clk.clkr,
	[CAMSS_CCI_CCI_AHB_CLK] = &camss_cci_cci_ahb_clk.clkr,
	[CAMSS_CCI_CCI_CLK] = &camss_cci_cci_clk.clkr,
	[CAMSS_VFE_CPP_AHB_CLK] = &camss_vfe_cpp_ahb_clk.clkr,
	[CAMSS_VFE_CPP_AXI_CLK] = &camss_vfe_cpp_axi_clk.clkr,
	[CAMSS_VFE_CPP_CLK] = &camss_vfe_cpp_clk.clkr,
	[CAMSS_CSI0_AHB_CLK] = &camss_csi0_ahb_clk.clkr,
	[CAMSS_CSI0_CLK] = &camss_csi0_clk.clkr,
	[CAMSS_CSI0PHY_CLK] = &camss_csi0phy_clk.clkr,
	[CAMSS_CSI0PIX_CLK] = &camss_csi0pix_clk.clkr,
	[CAMSS_CSI0RDI_CLK] = &camss_csi0rdi_clk.clkr,
	[CAMSS_CSI1_AHB_CLK] = &camss_csi1_ahb_clk.clkr,
	[CAMSS_CSI1_CLK] = &camss_csi1_clk.clkr,
	[CAMSS_CSI1PHY_CLK] = &camss_csi1phy_clk.clkr,
	[CAMSS_CSI1PIX_CLK] = &camss_csi1pix_clk.clkr,
	[CAMSS_CSI1RDI_CLK] = &camss_csi1rdi_clk.clkr,
	[CAMSS_CSI2_AHB_CLK] = &camss_csi2_ahb_clk.clkr,
	[CAMSS_CSI2_CLK] = &camss_csi2_clk.clkr,
	[CAMSS_CSI2PHY_CLK] = &camss_csi2phy_clk.clkr,
	[CAMSS_CSI2PIX_CLK] = &camss_csi2pix_clk.clkr,
	[CAMSS_CSI2RDI_CLK] = &camss_csi2rdi_clk.clkr,
	[CAMSS_CSI3_AHB_CLK] = &camss_csi3_ahb_clk.clkr,
	[CAMSS_CSI3_CLK] = &camss_csi3_clk.clkr,
	[CAMSS_CSI3PHY_CLK] = &camss_csi3phy_clk.clkr,
	[CAMSS_CSI3PIX_CLK] = &camss_csi3pix_clk.clkr,
	[CAMSS_CSI3RDI_CLK] = &camss_csi3rdi_clk.clkr,
	[CAMSS_CSI_VFE0_CLK] = &camss_csi_vfe0_clk.clkr,
	[CAMSS_CSI_VFE1_CLK] = &camss_csi_vfe1_clk.clkr,
	[CAMSS_GP0_CLK] = &camss_gp0_clk.clkr,
	[CAMSS_GP1_CLK] = &camss_gp1_clk.clkr,
	[CAMSS_ISPIF_AHB_CLK] = &camss_ispif_ahb_clk.clkr,
	[CAMSS_JPEG_DMA_CLK] = &camss_jpeg_dma_clk.clkr,
	[CAMSS_JPEG_JPEG0_CLK] = &camss_jpeg_jpeg0_clk.clkr,
	[CAMSS_JPEG_JPEG1_CLK] = &camss_jpeg_jpeg1_clk.clkr,
	[CAMSS_JPEG_JPEG2_CLK] = &camss_jpeg_jpeg2_clk.clkr,
	[CAMSS_JPEG_JPEG_AHB_CLK] = &camss_jpeg_jpeg_ahb_clk.clkr,
	[CAMSS_JPEG_JPEG_AXI_CLK] = &camss_jpeg_jpeg_axi_clk.clkr,
	[CAMSS_MCLK0_CLK] = &camss_mclk0_clk.clkr,
	[CAMSS_MCLK1_CLK] = &camss_mclk1_clk.clkr,
	[CAMSS_MCLK2_CLK] = &camss_mclk2_clk.clkr,
	[CAMSS_MCLK3_CLK] = &camss_mclk3_clk.clkr,
	[CAMSS_MICRO_AHB_CLK] = &camss_micro_ahb_clk.clkr,
	[CAMSS_PHY0_CSI0PHYTIMER_CLK] = &camss_phy0_csi0phytimer_clk.clkr,
	[CAMSS_PHY1_CSI1PHYTIMER_CLK] = &camss_phy1_csi1phytimer_clk.clkr,
	[CAMSS_PHY2_CSI2PHYTIMER_CLK] = &camss_phy2_csi2phytimer_clk.clkr,
	[CAMSS_TOP_AHB_CLK] = &camss_top_ahb_clk.clkr,
	[CAMSS_VFE_VFE0_CLK] = &camss_vfe_vfe0_clk.clkr,
	[CAMSS_VFE_VFE1_CLK] = &camss_vfe_vfe1_clk.clkr,
	[CAMSS_VFE_VFE_AHB_CLK] = &camss_vfe_vfe_ahb_clk.clkr,
	[CAMSS_VFE_VFE_AXI_CLK] = &camss_vfe_vfe_axi_clk.clkr,
	[FD_AHB_CLK] = &fd_ahb_clk.clkr,
	[FD_AXI_CLK] = &fd_axi_clk.clkr,
	[FD_CORE_CLK] = &fd_core_clk.clkr,
	[FD_CORE_UAR_CLK] = &fd_core_uar_clk.clkr,
	[MDSS_AHB_CLK] = &mdss_ahb_clk.clkr,
	[MDSS_AXI_CLK] = &mdss_axi_clk.clkr,
	[MDSS_BYTE0_CLK] = &mdss_byte0_clk.clkr,
	[MDSS_BYTE1_CLK] = &mdss_byte1_clk.clkr,
	[MDSS_EXTPCLK_CLK] = &mdss_extpclk_clk.clkr,
	[MDSS_HDMI_AHB_CLK] = &mdss_hdmi_ahb_clk.clkr,
	[MDSS_HDMI_CLK] = &mdss_hdmi_clk.clkr,
	[MDSS_MDP_CLK] = &mdss_mdp_clk.clkr,
	[MDSS_PCLK0_CLK] = &mdss_pclk0_clk.clkr,
	[MDSS_PCLK1_CLK] = &mdss_pclk1_clk.clkr,
	[MDSS_VSYNC_CLK] = &mdss_vsync_clk.clkr,
	[MMSS_MISC_AHB_CLK] = &mmss_misc_ahb_clk.clkr,
	[MMSS_MMSSNOC_AXI_CLK] = &mmss_mmssnoc_axi_clk.clkr,
	[MMSS_S0_AXI_CLK] = &mmss_s0_axi_clk.clkr,
	[OCMEMCX_OCMEMNOC_CLK] = &ocmemcx_ocmemnoc_clk.clkr,
	[OXILI_GFX3D_CLK] = &oxili_gfx3d_clk.clkr,
	[OXILI_RBBMTIMER_CLK] = &oxili_rbbmtimer_clk.clkr,
	[OXILICX_AHB_CLK] = &oxilicx_ahb_clk.clkr,
	[VENUS0_AHB_CLK] = &venus0_ahb_clk.clkr,
	[VENUS0_AXI_CLK] = &venus0_axi_clk.clkr,
	[VENUS0_OCMEMNOC_CLK] = &venus0_ocmemnoc_clk.clkr,
	[VENUS0_VCODEC0_CLK] = &venus0_vcodec0_clk.clkr,
	[VENUS0_CORE0_VCODEC_CLK] = &venus0_core0_vcodec_clk.clkr,
	[VENUS0_CORE1_VCODEC_CLK] = &venus0_core1_vcodec_clk.clkr,
	[VENUS0_CORE2_VCODEC_CLK] = &venus0_core2_vcodec_clk.clkr,
};

static struct gdsc *mmcc_msm8994_gdscs[] = {
	[VENUS_GDSC] = &venus_gdsc,
	[VENUS_CORE0_GDSC] = &venus_core0_gdsc,
	[VENUS_CORE1_GDSC] = &venus_core1_gdsc,
	[VENUS_CORE2_GDSC] = &venus_core2_gdsc,
	[CAMSS_TOP_GDSC] = &camss_top_gdsc,
	[MDSS_GDSC] = &mdss_gdsc,
	[JPEG_GDSC] = &jpeg_gdsc,
	[VFE_GDSC] = &vfe_gdsc,
	[CPP_GDSC] = &cpp_gdsc,
	[OXILI_GX_GDSC] = &oxili_gx_gdsc,
	[OXILI_CX_GDSC] = &oxili_cx_gdsc,
	[FD_GDSC] = &fd_gdsc,
};

static const struct qcom_reset_map mmcc_msm8994_resets[] = {
	[CAMSS_MICRO_BCR] = { 0x3490 },
};

static const struct regmap_config mmcc_msm8994_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x5200,
	.fast_io	= true,
};

static const struct qcom_cc_desc mmcc_msm8994_desc = {
	.config = &mmcc_msm8994_regmap_config,
	.clks = mmcc_msm8994_clocks,
	.num_clks = ARRAY_SIZE(mmcc_msm8994_clocks),
	.resets = mmcc_msm8994_resets,
	.num_resets = ARRAY_SIZE(mmcc_msm8994_resets),
	.gdscs = mmcc_msm8994_gdscs,
	.num_gdscs = ARRAY_SIZE(mmcc_msm8994_gdscs),
};

static const struct of_device_id mmcc_msm8994_match_table[] = {
	{ .compatible = "qcom,mmcc-msm8992" },
	{ .compatible = "qcom,mmcc-msm8994" }, /* V2 and V2.1 */
	{ }
};
MODULE_DEVICE_TABLE(of, mmcc_msm8994_match_table);

static int mmcc_msm8994_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	if (of_device_is_compatible(pdev->dev.of_node, "qcom,mmcc-msm8992")) {
		/* MSM8992 features less clocks and some have different freq tables */
		mmcc_msm8994_desc.clks[CAMSS_JPEG_JPEG1_CLK] = NULL;
		mmcc_msm8994_desc.clks[CAMSS_JPEG_JPEG2_CLK] = NULL;
		mmcc_msm8994_desc.clks[FD_CORE_CLK_SRC] = NULL;
		mmcc_msm8994_desc.clks[FD_CORE_CLK] = NULL;
		mmcc_msm8994_desc.clks[FD_CORE_UAR_CLK] = NULL;
		mmcc_msm8994_desc.clks[FD_AXI_CLK] = NULL;
		mmcc_msm8994_desc.clks[FD_AHB_CLK] = NULL;
		mmcc_msm8994_desc.clks[JPEG1_CLK_SRC] = NULL;
		mmcc_msm8994_desc.clks[JPEG2_CLK_SRC] = NULL;
		mmcc_msm8994_desc.clks[VENUS0_CORE2_VCODEC_CLK] = NULL;

		mmcc_msm8994_desc.gdscs[FD_GDSC] = NULL;
		mmcc_msm8994_desc.gdscs[VENUS_CORE2_GDSC] = NULL;

		axi_clk_src.freq_tbl = ftbl_axi_clk_src_8992;
		cpp_clk_src.freq_tbl = ftbl_cpp_clk_src_8992;
		csi0_clk_src.freq_tbl = ftbl_csi0_1_2_3_clk_src_8992;
		csi1_clk_src.freq_tbl = ftbl_csi0_1_2_3_clk_src_8992;
		csi2_clk_src.freq_tbl = ftbl_csi0_1_2_3_clk_src_8992;
		csi3_clk_src.freq_tbl = ftbl_csi0_1_2_3_clk_src_8992;
		mclk0_clk_src.freq_tbl = ftbl_mclk0_clk_src_8992;
		mclk1_clk_src.freq_tbl = ftbl_mclk1_2_3_clk_src_8992;
		mclk2_clk_src.freq_tbl = ftbl_mclk1_2_3_clk_src_8992;
		mclk3_clk_src.freq_tbl = ftbl_mclk1_2_3_clk_src_8992;
		mdp_clk_src.freq_tbl = ftbl_mdp_clk_src_8992;
		ocmemnoc_clk_src.freq_tbl = ftbl_ocmemnoc_clk_src_8992;
		vcodec0_clk_src.freq_tbl = ftbl_vcodec0_clk_src_8992;
		vfe0_clk_src.freq_tbl = ftbl_vfe0_1_clk_src_8992;
		vfe1_clk_src.freq_tbl = ftbl_vfe0_1_clk_src_8992;
	}

	regmap = qcom_cc_map(pdev, &mmcc_msm8994_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_alpha_pll_configure(&mmpll0_early, regmap, &mmpll_p_config);
	clk_alpha_pll_configure(&mmpll1_early, regmap, &mmpll_p_config);
	clk_alpha_pll_configure(&mmpll3_early, regmap, &mmpll_p_config);
	clk_alpha_pll_configure(&mmpll5_early, regmap, &mmpll_p_config);

	return qcom_cc_really_probe(pdev, &mmcc_msm8994_desc, regmap);
}

static struct platform_driver mmcc_msm8994_driver = {
	.probe		= mmcc_msm8994_probe,
	.driver		= {
		.name	= "mmcc-msm8994",
		.of_match_table = mmcc_msm8994_match_table,
	},
};
module_platform_driver(mmcc_msm8994_driver);

MODULE_DESCRIPTION("QCOM MMCC MSM8994 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mmcc-msm8994");
