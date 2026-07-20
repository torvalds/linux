// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,nord-negcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-phy-mux.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_SLEEP_CLK,
	DT_UFS_PHY_RX_SYMBOL_0_CLK,
	DT_UFS_PHY_RX_SYMBOL_1_CLK,
	DT_UFS_PHY_TX_SYMBOL_0_CLK,
	DT_USB3_PHY_SEC_WRAPPER_NE_GCC_USB31_PIPE_CLK,
	DT_USB3_PHY_WRAPPER_NE_GCC_USB31_PIPE_CLK,
};

enum {
	P_BI_TCXO,
	P_NE_GCC_GPLL0_OUT_EVEN,
	P_NE_GCC_GPLL0_OUT_MAIN,
	P_NE_GCC_GPLL2_OUT_MAIN,
	P_SLEEP_CLK,
	P_UFS_PHY_RX_SYMBOL_0_CLK,
	P_UFS_PHY_RX_SYMBOL_1_CLK,
	P_UFS_PHY_TX_SYMBOL_0_CLK,
	P_USB3_PHY_SEC_WRAPPER_NE_GCC_USB31_PIPE_CLK,
	P_USB3_PHY_WRAPPER_NE_GCC_USB31_PIPE_CLK,
};

static struct clk_alpha_pll ne_gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static const struct clk_div_table post_div_table_ne_gcc_gpll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv ne_gcc_gpll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 10,
	.post_div_table = post_div_table_ne_gcc_gpll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_ne_gcc_gpll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&ne_gcc_gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_ole_ops,
	},
};

static struct clk_alpha_pll ne_gcc_gpll2 = {
	.offset = 0x2000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x0,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_gpll2",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static const struct parent_map ne_gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_NE_GCC_GPLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data ne_gcc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &ne_gcc_gpll0.clkr.hw },
};

static const struct parent_map ne_gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_NE_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_NE_GCC_GPLL0_OUT_EVEN, 5 },
};

static const struct clk_parent_data ne_gcc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &ne_gcc_gpll0.clkr.hw },
	{ .hw = &ne_gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map ne_gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_NE_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_NE_GCC_GPLL2_OUT_MAIN, 3 },
};

static const struct clk_parent_data ne_gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &ne_gcc_gpll0.clkr.hw },
	{ .hw = &ne_gcc_gpll2.clkr.hw },
};

static const struct parent_map ne_gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data ne_gcc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map ne_gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_NE_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data ne_gcc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &ne_gcc_gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map ne_gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data ne_gcc_parent_data_5[] = {
	{ .index = DT_BI_TCXO },
};

static const struct parent_map ne_gcc_parent_map_6[] = {
	{ P_USB3_PHY_WRAPPER_NE_GCC_USB31_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data ne_gcc_parent_data_6[] = {
	{ .index = DT_USB3_PHY_WRAPPER_NE_GCC_USB31_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map ne_gcc_parent_map_7[] = {
	{ P_USB3_PHY_SEC_WRAPPER_NE_GCC_USB31_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data ne_gcc_parent_data_7[] = {
	{ .index = DT_USB3_PHY_SEC_WRAPPER_NE_GCC_USB31_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static struct clk_regmap_phy_mux ne_gcc_ufs_phy_rx_symbol_0_clk_src = {
	.reg = 0x33068,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ufs_phy_rx_symbol_0_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_UFS_PHY_RX_SYMBOL_0_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux ne_gcc_ufs_phy_rx_symbol_1_clk_src = {
	.reg = 0x330f0,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ufs_phy_rx_symbol_1_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_UFS_PHY_RX_SYMBOL_1_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux ne_gcc_ufs_phy_tx_symbol_0_clk_src = {
	.reg = 0x33058,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ufs_phy_tx_symbol_0_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_UFS_PHY_TX_SYMBOL_0_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_mux ne_gcc_usb3_prim_phy_pipe_clk_src = {
	.reg = 0x2a078,
	.shift = 0,
	.width = 2,
	.parent_map = ne_gcc_parent_map_6,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb3_prim_phy_pipe_clk_src",
			.parent_data = ne_gcc_parent_data_6,
			.num_parents = ARRAY_SIZE(ne_gcc_parent_data_6),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux ne_gcc_usb3_sec_phy_pipe_clk_src = {
	.reg = 0x2c078,
	.shift = 0,
	.width = 2,
	.parent_map = ne_gcc_parent_map_7,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb3_sec_phy_pipe_clk_src",
			.parent_data = ne_gcc_parent_data_7,
			.num_parents = ARRAY_SIZE(ne_gcc_parent_data_7),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static const struct freq_tbl ftbl_ne_gcc_gp1_clk_src[] = {
	F(66666667, P_NE_GCC_GPLL0_OUT_MAIN, 9, 0, 0),
	F(100000000, P_NE_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_NE_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 ne_gcc_gp1_clk_src = {
	.cmd_rcgr = 0x21004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_4,
	.freq_tbl = ftbl_ne_gcc_gp1_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_gp1_clk_src",
		.parent_data = ne_gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 ne_gcc_gp2_clk_src = {
	.cmd_rcgr = 0x22004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_4,
	.freq_tbl = ftbl_ne_gcc_gp1_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_gp2_clk_src",
		.parent_data = ne_gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_ne_gcc_qupv3_wrap2_s0_clk_src[] = {
	F(7372800, P_NE_GCC_GPLL0_OUT_MAIN, 1, 192, 15625),
	F(14745600, P_NE_GCC_GPLL0_OUT_MAIN, 1, 384, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_NE_GCC_GPLL0_OUT_MAIN, 1, 768, 15625),
	F(32000000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 4, 75),
	F(48000000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 2, 25),
	F(51200000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 32, 375),
	F(64000000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 8, 75),
	F(66666667, P_NE_GCC_GPLL0_OUT_MAIN, 9, 0, 0),
	F(75000000, P_NE_GCC_GPLL0_OUT_MAIN, 8, 0, 0),
	F(80000000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 2, 15),
	F(96000000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 4, 25),
	F(100000000, P_NE_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(102400000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 64, 375),
	F(112000000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 14, 75),
	F(117964800, P_NE_GCC_GPLL0_OUT_MAIN, 1, 3072, 15625),
	F(120000000, P_NE_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_init_data ne_gcc_qupv3_wrap2_s0_clk_src_init = {
	.name = "ne_gcc_qupv3_wrap2_s0_clk_src",
	.parent_data = ne_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 ne_gcc_qupv3_wrap2_s0_clk_src = {
	.cmd_rcgr = 0x3816c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_qupv3_wrap2_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &ne_gcc_qupv3_wrap2_s0_clk_src_init,
};

static struct clk_init_data ne_gcc_qupv3_wrap2_s1_clk_src_init = {
	.name = "ne_gcc_qupv3_wrap2_s1_clk_src",
	.parent_data = ne_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 ne_gcc_qupv3_wrap2_s1_clk_src = {
	.cmd_rcgr = 0x382a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_qupv3_wrap2_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &ne_gcc_qupv3_wrap2_s1_clk_src_init,
};

static const struct freq_tbl ftbl_ne_gcc_qupv3_wrap2_s2_clk_src[] = {
	F(7372800, P_NE_GCC_GPLL0_OUT_MAIN, 1, 192, 15625),
	F(14745600, P_NE_GCC_GPLL0_OUT_MAIN, 1, 384, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_NE_GCC_GPLL0_OUT_MAIN, 1, 768, 15625),
	F(32000000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 4, 75),
	F(48000000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 2, 25),
	F(51200000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 32, 375),
	F(64000000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 8, 75),
	F(66666667, P_NE_GCC_GPLL0_OUT_MAIN, 9, 0, 0),
	F(75000000, P_NE_GCC_GPLL0_OUT_MAIN, 8, 0, 0),
	F(80000000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 2, 15),
	F(96000000, P_NE_GCC_GPLL0_OUT_MAIN, 1, 4, 25),
	F(100000000, P_NE_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_init_data ne_gcc_qupv3_wrap2_s2_clk_src_init = {
	.name = "ne_gcc_qupv3_wrap2_s2_clk_src",
	.parent_data = ne_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 ne_gcc_qupv3_wrap2_s2_clk_src = {
	.cmd_rcgr = 0x383e4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_qupv3_wrap2_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &ne_gcc_qupv3_wrap2_s2_clk_src_init,
};

static struct clk_init_data ne_gcc_qupv3_wrap2_s3_clk_src_init = {
	.name = "ne_gcc_qupv3_wrap2_s3_clk_src",
	.parent_data = ne_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 ne_gcc_qupv3_wrap2_s3_clk_src = {
	.cmd_rcgr = 0x38520,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_qupv3_wrap2_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &ne_gcc_qupv3_wrap2_s3_clk_src_init,
};

static struct clk_init_data ne_gcc_qupv3_wrap2_s4_clk_src_init = {
	.name = "ne_gcc_qupv3_wrap2_s4_clk_src",
	.parent_data = ne_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 ne_gcc_qupv3_wrap2_s4_clk_src = {
	.cmd_rcgr = 0x3865c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_qupv3_wrap2_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &ne_gcc_qupv3_wrap2_s4_clk_src_init,
};

static struct clk_init_data ne_gcc_qupv3_wrap2_s5_clk_src_init = {
	.name = "ne_gcc_qupv3_wrap2_s5_clk_src",
	.parent_data = ne_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 ne_gcc_qupv3_wrap2_s5_clk_src = {
	.cmd_rcgr = 0x38798,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_qupv3_wrap2_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &ne_gcc_qupv3_wrap2_s5_clk_src_init,
};

static struct clk_init_data ne_gcc_qupv3_wrap2_s6_clk_src_init = {
	.name = "ne_gcc_qupv3_wrap2_s6_clk_src",
	.parent_data = ne_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 ne_gcc_qupv3_wrap2_s6_clk_src = {
	.cmd_rcgr = 0x388d4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_qupv3_wrap2_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &ne_gcc_qupv3_wrap2_s6_clk_src_init,
};

static const struct freq_tbl ftbl_ne_gcc_sdcc4_apps_clk_src[] = {
	F(37500000, P_NE_GCC_GPLL0_OUT_MAIN, 16, 0, 0),
	F(50000000, P_NE_GCC_GPLL0_OUT_MAIN, 12, 0, 0),
	F(100000000, P_NE_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 ne_gcc_sdcc4_apps_clk_src = {
	.cmd_rcgr = 0x1801c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_sdcc4_apps_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_sdcc4_apps_clk_src",
		.parent_data = ne_gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_floor_ops,
	},
};

static const struct freq_tbl ftbl_ne_gcc_ufs_phy_axi_clk_src[] = {
	F(120000000, P_NE_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	F(201500000, P_NE_GCC_GPLL2_OUT_MAIN, 4, 0, 0),
	F(300000000, P_NE_GCC_GPLL0_OUT_MAIN, 2, 0, 0),
	F(403000000, P_NE_GCC_GPLL2_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 ne_gcc_ufs_phy_axi_clk_src = {
	.cmd_rcgr = 0x33034,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_2,
	.freq_tbl = ftbl_ne_gcc_ufs_phy_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_ufs_phy_axi_clk_src",
		.parent_data = ne_gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 ne_gcc_ufs_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x3308c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_2,
	.freq_tbl = ftbl_ne_gcc_ufs_phy_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_ufs_phy_ice_core_clk_src",
		.parent_data = ne_gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_ne_gcc_ufs_phy_phy_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 ne_gcc_ufs_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x330c0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_5,
	.freq_tbl = ftbl_ne_gcc_ufs_phy_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_ufs_phy_phy_aux_clk_src",
		.parent_data = ne_gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 ne_gcc_ufs_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x330a4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_2,
	.freq_tbl = ftbl_ne_gcc_ufs_phy_axi_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_ufs_phy_unipro_core_clk_src",
		.parent_data = ne_gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_ne_gcc_usb20_master_clk_src[] = {
	F(75000000, P_NE_GCC_GPLL0_OUT_MAIN, 8, 0, 0),
	F(120000000, P_NE_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 ne_gcc_usb20_master_clk_src = {
	.cmd_rcgr = 0x31030,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_usb20_master_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_usb20_master_clk_src",
		.parent_data = ne_gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 ne_gcc_usb20_mock_utmi_clk_src = {
	.cmd_rcgr = 0x31048,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_ufs_phy_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_usb20_mock_utmi_clk_src",
		.parent_data = ne_gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_ne_gcc_usb31_prim_master_clk_src[] = {
	F(85714286, P_NE_GCC_GPLL0_OUT_MAIN, 7, 0, 0),
	F(133333333, P_NE_GCC_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(200000000, P_NE_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	F(240000000, P_NE_GCC_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 ne_gcc_usb31_prim_master_clk_src = {
	.cmd_rcgr = 0x2a038,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_1,
	.freq_tbl = ftbl_ne_gcc_usb31_prim_master_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_usb31_prim_master_clk_src",
		.parent_data = ne_gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 ne_gcc_usb31_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0x2a050,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_ufs_phy_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_usb31_prim_mock_utmi_clk_src",
		.parent_data = ne_gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 ne_gcc_usb31_sec_master_clk_src = {
	.cmd_rcgr = 0x2c038,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_usb31_prim_master_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_usb31_sec_master_clk_src",
		.parent_data = ne_gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 ne_gcc_usb31_sec_mock_utmi_clk_src = {
	.cmd_rcgr = 0x2c050,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_0,
	.freq_tbl = ftbl_ne_gcc_ufs_phy_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_usb31_sec_mock_utmi_clk_src",
		.parent_data = ne_gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 ne_gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0x2a07c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_3,
	.freq_tbl = ftbl_ne_gcc_ufs_phy_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = ne_gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 ne_gcc_usb3_sec_phy_aux_clk_src = {
	.cmd_rcgr = 0x2c07c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = ne_gcc_parent_map_3,
	.freq_tbl = ftbl_ne_gcc_ufs_phy_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_usb3_sec_phy_aux_clk_src",
		.parent_data = ne_gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(ne_gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div ne_gcc_usb20_mock_utmi_postdiv_clk_src = {
	.reg = 0x31060,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_usb20_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&ne_gcc_usb20_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div ne_gcc_usb31_prim_mock_utmi_postdiv_clk_src = {
	.reg = 0x2a068,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_usb31_prim_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&ne_gcc_usb31_prim_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div ne_gcc_usb31_sec_mock_utmi_postdiv_clk_src = {
	.reg = 0x2c068,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "ne_gcc_usb31_sec_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&ne_gcc_usb31_sec_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch ne_gcc_aggre_noc_ufs_phy_axi_clk = {
	.halt_reg = 0x330f4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x330f4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x330f4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_aggre_noc_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_aggre_noc_usb2_axi_clk = {
	.halt_reg = 0x31068,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x31068,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x31068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_aggre_noc_usb2_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_aggre_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x2a098,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2a098,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a098,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_aggre_noc_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb31_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_aggre_noc_usb3_sec_axi_clk = {
	.halt_reg = 0x2c098,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2c098,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2c098,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_aggre_noc_usb3_sec_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb31_sec_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_ahb2phy_clk = {
	.halt_reg = 0x30004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x30004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x30004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ahb2phy_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_cnoc_usb2_axi_clk = {
	.halt_reg = 0x31064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x31064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x31064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_cnoc_usb2_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_cnoc_usb3_prim_axi_clk = {
	.halt_reg = 0x2a094,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2a094,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a094,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_cnoc_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb31_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_cnoc_usb3_sec_axi_clk = {
	.halt_reg = 0x2c094,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2c094,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2c094,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_cnoc_usb3_sec_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb31_sec_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_frq_measure_ref_clk = {
	.halt_reg = 0x20008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x20008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_frq_measure_ref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_gp1_clk = {
	.halt_reg = 0x21000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x21000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_gp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_gp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_gp2_clk = {
	.halt_reg = 0x22000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x22000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_gp2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_gp2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_gpu_2_cfg_clk = {
	.halt_reg = 0x34004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x34004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x34004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_gpu_2_cfg_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_gpu_2_gpll0_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_gpu_2_gpll0_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_gpll0.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_gpu_2_gpll0_div_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_gpu_2_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_gpll0_out_even.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_gpu_2_hscnoc_gfx_clk = {
	.halt_reg = 0x34014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x34014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x34014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_gpu_2_hscnoc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_gpu_2_smmu_vote_clk = {
	.halt_reg = 0x57028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_gpu_2_smmu_vote_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_qupv3_wrap2_core_2x_clk = {
	.halt_reg = 0x38020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57008,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_qupv3_wrap2_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_qupv3_wrap2_core_clk = {
	.halt_reg = 0x3800c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_qupv3_wrap2_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_qupv3_wrap2_m_ahb_clk = {
	.halt_reg = 0x38004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x38004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_qupv3_wrap2_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_qupv3_wrap2_s0_clk = {
	.halt_reg = 0x3815c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57008,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_qupv3_wrap2_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_qupv3_wrap2_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_qupv3_wrap2_s1_clk = {
	.halt_reg = 0x38298,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57008,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_qupv3_wrap2_s1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_qupv3_wrap2_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_qupv3_wrap2_s2_clk = {
	.halt_reg = 0x383d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57008,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_qupv3_wrap2_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_qupv3_wrap2_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_qupv3_wrap2_s3_clk = {
	.halt_reg = 0x38510,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57008,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_qupv3_wrap2_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_qupv3_wrap2_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_qupv3_wrap2_s4_clk = {
	.halt_reg = 0x3864c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57008,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_qupv3_wrap2_s4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_qupv3_wrap2_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_qupv3_wrap2_s5_clk = {
	.halt_reg = 0x38788,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57008,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_qupv3_wrap2_s5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_qupv3_wrap2_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_qupv3_wrap2_s6_clk = {
	.halt_reg = 0x388c4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57008,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_qupv3_wrap2_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_qupv3_wrap2_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_qupv3_wrap2_s_ahb_clk = {
	.halt_reg = 0x38008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x38008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_qupv3_wrap2_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_sdcc4_apps_clk = {
	.halt_reg = 0x18004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x18004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_sdcc4_apps_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_sdcc4_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_sdcc4_axi_clk = {
	.halt_reg = 0x18014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x18014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_sdcc4_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_ufs_phy_ahb_clk = {
	.halt_reg = 0x33028,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x33028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x33028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ufs_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_ufs_phy_axi_clk = {
	.halt_reg = 0x33018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x33018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x33018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_ufs_phy_ice_core_clk = {
	.halt_reg = 0x3307c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3307c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3307c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ufs_phy_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_ufs_phy_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_ufs_phy_phy_aux_clk = {
	.halt_reg = 0x330bc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x330bc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x330bc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ufs_phy_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_ufs_phy_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_ufs_phy_rx_symbol_0_clk = {
	.halt_reg = 0x33030,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x33030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ufs_phy_rx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_ufs_phy_rx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_ufs_phy_rx_symbol_1_clk = {
	.halt_reg = 0x330d8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x330d8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ufs_phy_rx_symbol_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_ufs_phy_rx_symbol_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_ufs_phy_tx_symbol_0_clk = {
	.halt_reg = 0x3302c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x3302c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ufs_phy_tx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_ufs_phy_tx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_ufs_phy_unipro_core_clk = {
	.halt_reg = 0x3306c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3306c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3306c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_ufs_phy_unipro_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_ufs_phy_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb20_master_clk = {
	.halt_reg = 0x31018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x31018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb20_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb20_mock_utmi_clk = {
	.halt_reg = 0x3102c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3102c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb20_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb20_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb20_sleep_clk = {
	.halt_reg = 0x31028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x31028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb20_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb31_prim_atb_clk = {
	.halt_reg = 0x2a018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x2a018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb31_prim_atb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb31_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb31_prim_eud_ahb_clk = {
	.halt_reg = 0x2a02c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2a02c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a02c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb31_prim_eud_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb31_prim_master_clk = {
	.halt_reg = 0x2a01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb31_prim_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb31_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb31_prim_mock_utmi_clk = {
	.halt_reg = 0x2a034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb31_prim_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb31_prim_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb31_prim_sleep_clk = {
	.halt_reg = 0x2a030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb31_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb31_sec_atb_clk = {
	.halt_reg = 0x2c018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x2c018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb31_sec_atb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb31_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb31_sec_eud_ahb_clk = {
	.halt_reg = 0x2c02c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2c02c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2c02c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb31_sec_eud_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb31_sec_master_clk = {
	.halt_reg = 0x2c01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb31_sec_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb31_sec_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb31_sec_mock_utmi_clk = {
	.halt_reg = 0x2c034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb31_sec_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb31_sec_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb31_sec_sleep_clk = {
	.halt_reg = 0x2c030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb31_sec_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb3_prim_phy_aux_clk = {
	.halt_reg = 0x2a06c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a06c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb3_prim_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb3_prim_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb3_prim_phy_com_aux_clk = {
	.halt_reg = 0x2a070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb3_prim_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb3_prim_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb3_prim_phy_pipe_clk = {
	.halt_reg = 0x2a074,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2a074,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb3_prim_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb3_prim_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb3_sec_phy_aux_clk = {
	.halt_reg = 0x2c06c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c06c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb3_sec_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb3_sec_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb3_sec_phy_com_aux_clk = {
	.halt_reg = 0x2c070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb3_sec_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb3_sec_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch ne_gcc_usb3_sec_phy_pipe_clk = {
	.halt_reg = 0x2c074,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2c074,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2c074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "ne_gcc_usb3_sec_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&ne_gcc_usb3_sec_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc ne_gcc_ufs_mem_phy_gdsc = {
	.gdscr = 0x32000,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "ne_gcc_ufs_mem_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc ne_gcc_ufs_phy_gdsc = {
	.gdscr = 0x33004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ne_gcc_ufs_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc ne_gcc_usb20_prim_gdsc = {
	.gdscr = 0x31004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ne_gcc_usb20_prim_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc ne_gcc_usb31_prim_gdsc = {
	.gdscr = 0x2a004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ne_gcc_usb31_prim_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc ne_gcc_usb31_sec_gdsc = {
	.gdscr = 0x2c004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ne_gcc_usb31_sec_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc ne_gcc_usb3_phy_gdsc = {
	.gdscr = 0x2b00c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "ne_gcc_usb3_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc ne_gcc_usb3_sec_phy_gdsc = {
	.gdscr = 0x2d00c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "ne_gcc_usb3_sec_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct clk_regmap *ne_gcc_nord_clocks[] = {
	[NE_GCC_AGGRE_NOC_UFS_PHY_AXI_CLK] = &ne_gcc_aggre_noc_ufs_phy_axi_clk.clkr,
	[NE_GCC_AGGRE_NOC_USB2_AXI_CLK] = &ne_gcc_aggre_noc_usb2_axi_clk.clkr,
	[NE_GCC_AGGRE_NOC_USB3_PRIM_AXI_CLK] = &ne_gcc_aggre_noc_usb3_prim_axi_clk.clkr,
	[NE_GCC_AGGRE_NOC_USB3_SEC_AXI_CLK] = &ne_gcc_aggre_noc_usb3_sec_axi_clk.clkr,
	[NE_GCC_AHB2PHY_CLK] = &ne_gcc_ahb2phy_clk.clkr,
	[NE_GCC_CNOC_USB2_AXI_CLK] = &ne_gcc_cnoc_usb2_axi_clk.clkr,
	[NE_GCC_CNOC_USB3_PRIM_AXI_CLK] = &ne_gcc_cnoc_usb3_prim_axi_clk.clkr,
	[NE_GCC_CNOC_USB3_SEC_AXI_CLK] = &ne_gcc_cnoc_usb3_sec_axi_clk.clkr,
	[NE_GCC_FRQ_MEASURE_REF_CLK] = &ne_gcc_frq_measure_ref_clk.clkr,
	[NE_GCC_GP1_CLK] = &ne_gcc_gp1_clk.clkr,
	[NE_GCC_GP1_CLK_SRC] = &ne_gcc_gp1_clk_src.clkr,
	[NE_GCC_GP2_CLK] = &ne_gcc_gp2_clk.clkr,
	[NE_GCC_GP2_CLK_SRC] = &ne_gcc_gp2_clk_src.clkr,
	[NE_GCC_GPLL0] = &ne_gcc_gpll0.clkr,
	[NE_GCC_GPLL0_OUT_EVEN] = &ne_gcc_gpll0_out_even.clkr,
	[NE_GCC_GPLL2] = &ne_gcc_gpll2.clkr,
	[NE_GCC_GPU_2_CFG_CLK] = &ne_gcc_gpu_2_cfg_clk.clkr,
	[NE_GCC_GPU_2_GPLL0_CLK_SRC] = &ne_gcc_gpu_2_gpll0_clk_src.clkr,
	[NE_GCC_GPU_2_GPLL0_DIV_CLK_SRC] = &ne_gcc_gpu_2_gpll0_div_clk_src.clkr,
	[NE_GCC_GPU_2_HSCNOC_GFX_CLK] = &ne_gcc_gpu_2_hscnoc_gfx_clk.clkr,
	[NE_GCC_GPU_2_SMMU_VOTE_CLK] = &ne_gcc_gpu_2_smmu_vote_clk.clkr,
	[NE_GCC_QUPV3_WRAP2_CORE_2X_CLK] = &ne_gcc_qupv3_wrap2_core_2x_clk.clkr,
	[NE_GCC_QUPV3_WRAP2_CORE_CLK] = &ne_gcc_qupv3_wrap2_core_clk.clkr,
	[NE_GCC_QUPV3_WRAP2_M_AHB_CLK] = &ne_gcc_qupv3_wrap2_m_ahb_clk.clkr,
	[NE_GCC_QUPV3_WRAP2_S0_CLK] = &ne_gcc_qupv3_wrap2_s0_clk.clkr,
	[NE_GCC_QUPV3_WRAP2_S0_CLK_SRC] = &ne_gcc_qupv3_wrap2_s0_clk_src.clkr,
	[NE_GCC_QUPV3_WRAP2_S1_CLK] = &ne_gcc_qupv3_wrap2_s1_clk.clkr,
	[NE_GCC_QUPV3_WRAP2_S1_CLK_SRC] = &ne_gcc_qupv3_wrap2_s1_clk_src.clkr,
	[NE_GCC_QUPV3_WRAP2_S2_CLK] = &ne_gcc_qupv3_wrap2_s2_clk.clkr,
	[NE_GCC_QUPV3_WRAP2_S2_CLK_SRC] = &ne_gcc_qupv3_wrap2_s2_clk_src.clkr,
	[NE_GCC_QUPV3_WRAP2_S3_CLK] = &ne_gcc_qupv3_wrap2_s3_clk.clkr,
	[NE_GCC_QUPV3_WRAP2_S3_CLK_SRC] = &ne_gcc_qupv3_wrap2_s3_clk_src.clkr,
	[NE_GCC_QUPV3_WRAP2_S4_CLK] = &ne_gcc_qupv3_wrap2_s4_clk.clkr,
	[NE_GCC_QUPV3_WRAP2_S4_CLK_SRC] = &ne_gcc_qupv3_wrap2_s4_clk_src.clkr,
	[NE_GCC_QUPV3_WRAP2_S5_CLK] = &ne_gcc_qupv3_wrap2_s5_clk.clkr,
	[NE_GCC_QUPV3_WRAP2_S5_CLK_SRC] = &ne_gcc_qupv3_wrap2_s5_clk_src.clkr,
	[NE_GCC_QUPV3_WRAP2_S6_CLK] = &ne_gcc_qupv3_wrap2_s6_clk.clkr,
	[NE_GCC_QUPV3_WRAP2_S6_CLK_SRC] = &ne_gcc_qupv3_wrap2_s6_clk_src.clkr,
	[NE_GCC_QUPV3_WRAP2_S_AHB_CLK] = &ne_gcc_qupv3_wrap2_s_ahb_clk.clkr,
	[NE_GCC_SDCC4_APPS_CLK] = &ne_gcc_sdcc4_apps_clk.clkr,
	[NE_GCC_SDCC4_APPS_CLK_SRC] = &ne_gcc_sdcc4_apps_clk_src.clkr,
	[NE_GCC_SDCC4_AXI_CLK] = &ne_gcc_sdcc4_axi_clk.clkr,
	[NE_GCC_UFS_PHY_AHB_CLK] = &ne_gcc_ufs_phy_ahb_clk.clkr,
	[NE_GCC_UFS_PHY_AXI_CLK] = &ne_gcc_ufs_phy_axi_clk.clkr,
	[NE_GCC_UFS_PHY_AXI_CLK_SRC] = &ne_gcc_ufs_phy_axi_clk_src.clkr,
	[NE_GCC_UFS_PHY_ICE_CORE_CLK] = &ne_gcc_ufs_phy_ice_core_clk.clkr,
	[NE_GCC_UFS_PHY_ICE_CORE_CLK_SRC] = &ne_gcc_ufs_phy_ice_core_clk_src.clkr,
	[NE_GCC_UFS_PHY_PHY_AUX_CLK] = &ne_gcc_ufs_phy_phy_aux_clk.clkr,
	[NE_GCC_UFS_PHY_PHY_AUX_CLK_SRC] = &ne_gcc_ufs_phy_phy_aux_clk_src.clkr,
	[NE_GCC_UFS_PHY_RX_SYMBOL_0_CLK] = &ne_gcc_ufs_phy_rx_symbol_0_clk.clkr,
	[NE_GCC_UFS_PHY_RX_SYMBOL_0_CLK_SRC] = &ne_gcc_ufs_phy_rx_symbol_0_clk_src.clkr,
	[NE_GCC_UFS_PHY_RX_SYMBOL_1_CLK] = &ne_gcc_ufs_phy_rx_symbol_1_clk.clkr,
	[NE_GCC_UFS_PHY_RX_SYMBOL_1_CLK_SRC] = &ne_gcc_ufs_phy_rx_symbol_1_clk_src.clkr,
	[NE_GCC_UFS_PHY_TX_SYMBOL_0_CLK] = &ne_gcc_ufs_phy_tx_symbol_0_clk.clkr,
	[NE_GCC_UFS_PHY_TX_SYMBOL_0_CLK_SRC] = &ne_gcc_ufs_phy_tx_symbol_0_clk_src.clkr,
	[NE_GCC_UFS_PHY_UNIPRO_CORE_CLK] = &ne_gcc_ufs_phy_unipro_core_clk.clkr,
	[NE_GCC_UFS_PHY_UNIPRO_CORE_CLK_SRC] = &ne_gcc_ufs_phy_unipro_core_clk_src.clkr,
	[NE_GCC_USB20_MASTER_CLK] = &ne_gcc_usb20_master_clk.clkr,
	[NE_GCC_USB20_MASTER_CLK_SRC] = &ne_gcc_usb20_master_clk_src.clkr,
	[NE_GCC_USB20_MOCK_UTMI_CLK] = &ne_gcc_usb20_mock_utmi_clk.clkr,
	[NE_GCC_USB20_MOCK_UTMI_CLK_SRC] = &ne_gcc_usb20_mock_utmi_clk_src.clkr,
	[NE_GCC_USB20_MOCK_UTMI_POSTDIV_CLK_SRC] = &ne_gcc_usb20_mock_utmi_postdiv_clk_src.clkr,
	[NE_GCC_USB20_SLEEP_CLK] = &ne_gcc_usb20_sleep_clk.clkr,
	[NE_GCC_USB31_PRIM_ATB_CLK] = &ne_gcc_usb31_prim_atb_clk.clkr,
	[NE_GCC_USB31_PRIM_EUD_AHB_CLK] = &ne_gcc_usb31_prim_eud_ahb_clk.clkr,
	[NE_GCC_USB31_PRIM_MASTER_CLK] = &ne_gcc_usb31_prim_master_clk.clkr,
	[NE_GCC_USB31_PRIM_MASTER_CLK_SRC] = &ne_gcc_usb31_prim_master_clk_src.clkr,
	[NE_GCC_USB31_PRIM_MOCK_UTMI_CLK] = &ne_gcc_usb31_prim_mock_utmi_clk.clkr,
	[NE_GCC_USB31_PRIM_MOCK_UTMI_CLK_SRC] = &ne_gcc_usb31_prim_mock_utmi_clk_src.clkr,
	[NE_GCC_USB31_PRIM_MOCK_UTMI_POSTDIV_CLK_SRC] =
		&ne_gcc_usb31_prim_mock_utmi_postdiv_clk_src.clkr,
	[NE_GCC_USB31_PRIM_SLEEP_CLK] = &ne_gcc_usb31_prim_sleep_clk.clkr,
	[NE_GCC_USB31_SEC_ATB_CLK] = &ne_gcc_usb31_sec_atb_clk.clkr,
	[NE_GCC_USB31_SEC_EUD_AHB_CLK] = &ne_gcc_usb31_sec_eud_ahb_clk.clkr,
	[NE_GCC_USB31_SEC_MASTER_CLK] = &ne_gcc_usb31_sec_master_clk.clkr,
	[NE_GCC_USB31_SEC_MASTER_CLK_SRC] = &ne_gcc_usb31_sec_master_clk_src.clkr,
	[NE_GCC_USB31_SEC_MOCK_UTMI_CLK] = &ne_gcc_usb31_sec_mock_utmi_clk.clkr,
	[NE_GCC_USB31_SEC_MOCK_UTMI_CLK_SRC] = &ne_gcc_usb31_sec_mock_utmi_clk_src.clkr,
	[NE_GCC_USB31_SEC_MOCK_UTMI_POSTDIV_CLK_SRC] =
		&ne_gcc_usb31_sec_mock_utmi_postdiv_clk_src.clkr,
	[NE_GCC_USB31_SEC_SLEEP_CLK] = &ne_gcc_usb31_sec_sleep_clk.clkr,
	[NE_GCC_USB3_PRIM_PHY_AUX_CLK] = &ne_gcc_usb3_prim_phy_aux_clk.clkr,
	[NE_GCC_USB3_PRIM_PHY_AUX_CLK_SRC] = &ne_gcc_usb3_prim_phy_aux_clk_src.clkr,
	[NE_GCC_USB3_PRIM_PHY_COM_AUX_CLK] = &ne_gcc_usb3_prim_phy_com_aux_clk.clkr,
	[NE_GCC_USB3_PRIM_PHY_PIPE_CLK] = &ne_gcc_usb3_prim_phy_pipe_clk.clkr,
	[NE_GCC_USB3_PRIM_PHY_PIPE_CLK_SRC] = &ne_gcc_usb3_prim_phy_pipe_clk_src.clkr,
	[NE_GCC_USB3_SEC_PHY_AUX_CLK] = &ne_gcc_usb3_sec_phy_aux_clk.clkr,
	[NE_GCC_USB3_SEC_PHY_AUX_CLK_SRC] = &ne_gcc_usb3_sec_phy_aux_clk_src.clkr,
	[NE_GCC_USB3_SEC_PHY_COM_AUX_CLK] = &ne_gcc_usb3_sec_phy_com_aux_clk.clkr,
	[NE_GCC_USB3_SEC_PHY_PIPE_CLK] = &ne_gcc_usb3_sec_phy_pipe_clk.clkr,
	[NE_GCC_USB3_SEC_PHY_PIPE_CLK_SRC] = &ne_gcc_usb3_sec_phy_pipe_clk_src.clkr,
};

static struct gdsc *ne_gcc_nord_gdscs[] = {
	[NE_GCC_UFS_MEM_PHY_GDSC] = &ne_gcc_ufs_mem_phy_gdsc,
	[NE_GCC_UFS_PHY_GDSC] = &ne_gcc_ufs_phy_gdsc,
	[NE_GCC_USB20_PRIM_GDSC] = &ne_gcc_usb20_prim_gdsc,
	[NE_GCC_USB31_PRIM_GDSC] = &ne_gcc_usb31_prim_gdsc,
	[NE_GCC_USB31_SEC_GDSC] = &ne_gcc_usb31_sec_gdsc,
	[NE_GCC_USB3_PHY_GDSC] = &ne_gcc_usb3_phy_gdsc,
	[NE_GCC_USB3_SEC_PHY_GDSC] = &ne_gcc_usb3_sec_phy_gdsc,
};

static const struct qcom_reset_map ne_gcc_nord_resets[] = {
	[NE_GCC_GPU_2_BCR] = { 0x34000 },
	[NE_GCC_QUPV3_WRAPPER_2_BCR] = { 0x38000 },
	[NE_GCC_SDCC4_BCR] = { 0x18000 },
	[NE_GCC_UFS_PHY_BCR] = { 0x33000 },
	[NE_GCC_USB20_PRIM_BCR] = { 0x31000 },
	[NE_GCC_USB31_PRIM_BCR] = { 0x2a000 },
	[NE_GCC_USB31_SEC_BCR] = { 0x2c000 },
	[NE_GCC_USB3_DP_PHY_PRIM_BCR] = { 0x2b008 },
	[NE_GCC_USB3_DP_PHY_SEC_BCR] = { 0x2d008 },
	[NE_GCC_USB3_PHY_PRIM_BCR] = { 0x2b000 },
	[NE_GCC_USB3_PHY_SEC_BCR] = { 0x2d000 },
	[NE_GCC_USB3PHY_PHY_PRIM_BCR] = { 0x2b004 },
	[NE_GCC_USB3PHY_PHY_SEC_BCR] = { 0x2d004 },
	[NE_GCC_QUSB2PHY_PRIM_BCR] = { 0x2e000 },
};

static const struct clk_rcg_dfs_data ne_gcc_nord_dfs_clocks[] = {
	DEFINE_RCG_DFS(ne_gcc_qupv3_wrap2_s0_clk_src),
	DEFINE_RCG_DFS(ne_gcc_qupv3_wrap2_s1_clk_src),
	DEFINE_RCG_DFS(ne_gcc_qupv3_wrap2_s2_clk_src),
	DEFINE_RCG_DFS(ne_gcc_qupv3_wrap2_s3_clk_src),
	DEFINE_RCG_DFS(ne_gcc_qupv3_wrap2_s4_clk_src),
	DEFINE_RCG_DFS(ne_gcc_qupv3_wrap2_s5_clk_src),
	DEFINE_RCG_DFS(ne_gcc_qupv3_wrap2_s6_clk_src),
};

static const struct regmap_config ne_gcc_nord_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xf41f0,
	.fast_io = true,
};

static void clk_nord_regs_configure(struct device *dev, struct regmap *regmap)
{
	/* FORCE_MEM_CORE_ON for  ne_gcc_ufs_phy_ice_core_clk and ne_gcc_ufs_phy_axi_clk */
	qcom_branch_set_force_mem_core(regmap, ne_gcc_ufs_phy_ice_core_clk, true);
	qcom_branch_set_force_mem_core(regmap, ne_gcc_ufs_phy_axi_clk, true);
}

static const struct qcom_cc_driver_data ne_gcc_nord_driver_data = {
	.dfs_rcgs = ne_gcc_nord_dfs_clocks,
	.num_dfs_rcgs = ARRAY_SIZE(ne_gcc_nord_dfs_clocks),
	.clk_regs_configure = clk_nord_regs_configure,
};

static const struct qcom_cc_desc ne_gcc_nord_desc = {
	.config = &ne_gcc_nord_regmap_config,
	.clks = ne_gcc_nord_clocks,
	.num_clks = ARRAY_SIZE(ne_gcc_nord_clocks),
	.resets = ne_gcc_nord_resets,
	.num_resets = ARRAY_SIZE(ne_gcc_nord_resets),
	.gdscs = ne_gcc_nord_gdscs,
	.num_gdscs = ARRAY_SIZE(ne_gcc_nord_gdscs),
	.driver_data = &ne_gcc_nord_driver_data,
};

static const struct of_device_id ne_gcc_nord_match_table[] = {
	{ .compatible = "qcom,nord-negcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, ne_gcc_nord_match_table);

static int ne_gcc_nord_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &ne_gcc_nord_desc);
}

static struct platform_driver ne_gcc_nord_driver = {
	.probe = ne_gcc_nord_probe,
	.driver = {
		.name = "negcc-nord",
		.of_match_table = ne_gcc_nord_match_table,
	},
};

module_platform_driver(ne_gcc_nord_driver);

MODULE_DESCRIPTION("QTI NEGCC NORD Driver");
MODULE_LICENSE("GPL");
