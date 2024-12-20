// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2023, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sar2130p-gcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-phy-mux.h"
#include "gdsc.h"
#include "reset.h"

/* Need to match the order of clocks in DT binding */
enum {
	DT_BI_TCXO,
	DT_SLEEP_CLK,

	DT_PCIE_0_PIPE,
	DT_PCIE_1_PIPE,

	DT_USB3_PHY_WRAPPER_GCC_USB30_PIPE,
};

enum {
	P_BI_TCXO,
	P_GCC_GPLL0_OUT_EVEN,
	P_GCC_GPLL0_OUT_MAIN,
	P_GCC_GPLL1_OUT_EVEN,
	P_GCC_GPLL1_OUT_MAIN,
	P_GCC_GPLL4_OUT_MAIN,
	P_GCC_GPLL5_OUT_MAIN,
	P_GCC_GPLL7_OUT_MAIN,
	P_GCC_GPLL9_OUT_EVEN,
	P_PCIE_0_PIPE_CLK,
	P_PCIE_1_PIPE_CLK,
	P_SLEEP_CLK,
	P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
};

static struct clk_alpha_pll gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gcc_gpll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gcc_gpll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 10,
	.post_div_table = post_div_table_gcc_gpll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gcc_gpll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_ole_ops,
	},
};

static struct clk_alpha_pll gcc_gpll1 = {
	.offset = 0x1000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll1",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll4 = {
	.offset = 0x4000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll4",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll5 = {
	.offset = 0x5000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll5",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll7 = {
	.offset = 0x7000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll7",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll9 = {
	.offset = 0x9000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll9",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gcc_gpll9_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gcc_gpll9_out_even = {
	.offset = 0x9000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_gcc_gpll9_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gcc_gpll9_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gpll9_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gpll9.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_ole_ops,
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
	{ P_GCC_GPLL5_OUT_MAIN, 3 },
	{ P_GCC_GPLL1_OUT_MAIN, 4 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .hw = &gcc_gpll5.clkr.hw },
	{ .hw = &gcc_gpll1.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL9_OUT_EVEN, 2 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll9_out_even.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL1_OUT_EVEN, 2 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll1.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .index = DT_USB3_PHY_WRAPPER_GCC_USB30_PIPE },
	{ .index = DT_BI_TCXO },
};

static struct clk_regmap_phy_mux gcc_pcie_0_pipe_clk_src = {
	.reg = 0x7b070,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE_0_PIPE,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_1_pipe_clk_src = {
	.reg = 0x9d06c,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE_1_PIPE,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_prim_phy_pipe_clk_src = {
	.reg = 0x4906c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_8,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_8,
			.num_parents = ARRAY_SIZE(gcc_parent_data_8),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_ddrss_spad_clk_src[] = {
	F(300000000, P_GCC_GPLL0_OUT_EVEN, 1, 0, 0),
	F(403000000, P_GCC_GPLL4_OUT_MAIN, 2, 0, 0),
	F(426400000, P_GCC_GPLL1_OUT_MAIN, 2.5, 0, 0),
	F(500000000, P_GCC_GPLL7_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ddrss_spad_clk_src = {
	.cmd_rcgr = 0x70004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_ddrss_spad_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ddrss_spad_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x74004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x75004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x76004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_aux_clk_src = {
	.cmd_rcgr = 0x7b074,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_phy_rchng_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_phy_rchng_clk_src = {
	.cmd_rcgr = 0x7b058,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_aux_clk_src = {
	.cmd_rcgr = 0x9d070,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_phy_rchng_clk_src = {
	.cmd_rcgr = 0x9d054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(60000000, P_GCC_GPLL0_OUT_MAIN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x43010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s0_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(102400000, P_GCC_GPLL0_OUT_EVEN, 1, 128, 375),
	F(112000000, P_GCC_GPLL0_OUT_EVEN, 1, 28, 75),
	F(117964800, P_GCC_GPLL0_OUT_EVEN, 1, 6144, 15625),
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x28018,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s0_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s1_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s1_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x28150,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x28288,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x283c0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x284f8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x28630,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0x2e018,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s1_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s1_clk_src = {
	.cmd_rcgr = 0x2e150,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s2_clk_src = {
	.cmd_rcgr = 0x2e288,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s3_clk_src = {
	.cmd_rcgr = 0x2e3c0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0x2e4f8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0x2e630,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s5_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, P_BI_TCXO, 16, 3, 25),
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(20000000, P_GCC_GPLL0_OUT_EVEN, 5, 1, 3),
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(192000000, P_GCC_GPLL9_OUT_EVEN, 2, 0, 0),
	F(384000000, P_GCC_GPLL9_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x26018,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.ops = &clk_rcg2_shared_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_ice_core_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_EVEN, 2, 0, 0),
	F(300000000, P_GCC_GPLL0_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0x2603c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_sdcc1_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_ice_core_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.ops = &clk_rcg2_shared_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb30_prim_master_clk_src[] = {
	F(66666667, P_GCC_GPLL0_OUT_EVEN, 4.5, 0, 0),
	F(133333333, P_GCC_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_prim_master_clk_src = {
	.cmd_rcgr = 0x4902c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb30_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0x49044,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0x49070,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div gcc_usb30_prim_mock_utmi_postdiv_clk_src = {
	.reg = 0x4905c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb30_prim_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_aggre_noc_pcie_1_axi_clk = {
	.halt_reg = 0x7b094,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x7b094,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie_1_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_prim_axi_clk = {
	.halt_reg = 0x4908c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4908c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4908c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x48004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x48004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_pcie_anoc_ahb_clk = {
	.halt_reg = 0x20034,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x20034,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_pcie_anoc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x49088,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x49088,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x49088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_gpu_axi_clk = {
	.halt_reg = 0x81154,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x81154,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x81154,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ddrss_gpu_axi_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_pcie_sf_clk = {
	.halt_reg = 0x9d098,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x9d098,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ddrss_pcie_sf_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_spad_clk = {
	.halt_reg = 0x70000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x70000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x70000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ddrss_spad_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ddrss_spad_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_hf_axi_clk = {
	.halt_reg = 0x37008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x37008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x37008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x74000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x74000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp2_clk = {
	.halt_reg = 0x75000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x75000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp3_clk = {
	.halt_reg = 0x76000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x76000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_gpll0_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gpll0.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_gpll0_div_clk_src = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gpll0_out_even.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_memnoc_gfx_clk = {
	.halt_reg = 0x9b010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9b010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9b010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_memnoc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_snoc_dvm_gfx_clk = {
	.halt_reg = 0x9b018,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x9b018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_snoc_dvm_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_iris_ss_hf_axi1_clk = {
	.halt_reg = 0x42030,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x42030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x42030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_iris_ss_hf_axi1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_iris_ss_spd_axi1_clk = {
	.halt_reg = 0x70020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x70020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x70020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_iris_ss_spd_axi1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ddrss_spad_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_aux_clk = {
	.halt_reg = 0x7b03c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_0_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_cfg_ahb_clk = {
	.halt_reg = 0x7b038,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7b038,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_mstr_axi_clk = {
	.halt_reg = 0x7b02c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x7b02c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_phy_rchng_clk = {
	.halt_reg = 0x7b054,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_0_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_pipe_clk = {
	.halt_reg = 0x7b048,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_0_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_axi_clk = {
	.halt_reg = 0x7b020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7b020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_q2a_axi_clk = {
	.halt_reg = 0x7b01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_aux_clk = {
	.halt_reg = 0x9d038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_1_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_cfg_ahb_clk = {
	.halt_reg = 0x9d034,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9d034,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_mstr_axi_clk = {
	.halt_reg = 0x9d028,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x9d028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_phy_rchng_clk = {
	.halt_reg = 0x9d050,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_1_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_pipe_clk = {
	.halt_reg = 0x9d044,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_1_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_axi_clk = {
	.halt_reg = 0x9d01c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9d01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_q2a_axi_clk = {
	.halt_reg = 0x9d018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x4300c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4300c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pdm2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_ahb_clk = {
	.halt_reg = 0x43004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x43004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x43004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_xo4_clk = {
	.halt_reg = 0x43008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x43008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_gpu_ahb_clk = {
	.halt_reg = 0x9b008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9b008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9b008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_gpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_pcie_ahb_clk = {
	.halt_reg = 0x7b018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7b018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62000,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_pcie_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cv_cpu_ahb_clk = {
	.halt_reg = 0x42014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x42014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x42014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_cv_cpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cvp_ahb_clk = {
	.halt_reg = 0x42008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x42008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x42008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_cvp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_lsr_ahb_clk = {
	.halt_reg = 0x4204c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4204c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4204c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_lsr_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_v_cpu_ahb_clk = {
	.halt_reg = 0x42010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x42010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x42010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_v_cpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_vcodec_ahb_clk = {
	.halt_reg = 0x4200c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4200c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x4200c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_vcodec_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x33034,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x33024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x2800c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s1_clk = {
	.halt_reg = 0x28144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s2_clk = {
	.halt_reg = 0x2827c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s3_clk = {
	.halt_reg = 0x283b4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s4_clk = {
	.halt_reg = 0x284ec,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s5_clk = {
	.halt_reg = 0x28624,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_2x_clk = {
	.halt_reg = 0x3317c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_clk = {
	.halt_reg = 0x3316c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0x2e00c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s1_clk = {
	.halt_reg = 0x2e144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s2_clk = {
	.halt_reg = 0x2e27c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s3_clk = {
	.halt_reg = 0x2e3b4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s4_clk = {
	.halt_reg = 0x2e4ec,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s5_clk = {
	.halt_reg = 0x2e624,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_m_ahb_clk = {
	.halt_reg = 0x28004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x28004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_s_ahb_clk = {
	.halt_reg = 0x28008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x28008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_m_ahb_clk = {
	.halt_reg = 0x2e004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2e004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_1_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_s_ahb_clk = {
	.halt_reg = 0x2e008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2e008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_1_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x26010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x26010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x26004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x26004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_apps_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sdcc1_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ice_core_clk = {
	.halt_reg = 0x26030,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x26030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sdcc1_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_master_clk = {
	.halt_reg = 0x49018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_mock_utmi_clk = {
	.halt_reg = 0x49028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_prim_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_sleep_clk = {
	.halt_reg = 0x49024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_aux_clk = {
	.halt_reg = 0x49060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_prim_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_com_aux_clk = {
	.halt_reg = 0x49064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_prim_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_pipe_clk = {
	.halt_reg = 0x49068,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x49068,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x49068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_prim_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi0_clk = {
	.halt_reg = 0x42018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x42018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x42018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi1_clk = {
	.halt_reg = 0x42024,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x42024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x42024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc hlos1_vote_mm_snoc_mmu_tbu_hf0_gdsc = {
	.gdscr = 0x8d204,
	.pd = {
		.name = "hlos1_vote_mm_snoc_mmu_tbu_hf0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_mm_snoc_mmu_tbu_sf0_gdsc = {
	.gdscr = 0x8d054,
	.pd = {
		.name = "hlos1_vote_mm_snoc_mmu_tbu_sf0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_turing_mmu_tbu0_gdsc = {
	.gdscr = 0x8d05c,
	.pd = {
		.name = "hlos1_vote_turing_mmu_tbu0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_turing_mmu_tbu1_gdsc = {
	.gdscr = 0x8d060,
	.pd = {
		.name = "hlos1_vote_turing_mmu_tbu1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc pcie_0_gdsc = {
	.gdscr = 0x7b004,
	.collapse_ctrl = 0x62200,
	.collapse_mask = BIT(0),
	.pd = {
		.name = "pcie_0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc pcie_0_phy_gdsc = {
	.gdscr = 0x7c000,
	.collapse_ctrl = 0x62200,
	.collapse_mask = BIT(3),
	.pd = {
		.name = "pcie_0_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc pcie_1_gdsc = {
	.gdscr = 0x9d004,
	.collapse_ctrl = 0x62200,
	.collapse_mask = BIT(1),
	.pd = {
		.name = "pcie_1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc pcie_1_phy_gdsc = {
	.gdscr = 0x9e000,
	.collapse_ctrl = 0x62200,
	.collapse_mask = BIT(4),
	.pd = {
		.name = "pcie_1_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc usb30_prim_gdsc = {
	.gdscr = 0x49004,
	.pd = {
		.name = "usb30_prim_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc usb3_phy_gdsc = {
	.gdscr = 0x60018,
	.pd = {
		.name = "usb3_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct clk_regmap *gcc_sar2130p_clocks[] = {
	[GCC_AGGRE_NOC_PCIE_1_AXI_CLK] = &gcc_aggre_noc_pcie_1_axi_clk.clkr,
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = &gcc_aggre_usb3_prim_axi_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CFG_NOC_PCIE_ANOC_AHB_CLK] = &gcc_cfg_noc_pcie_anoc_ahb_clk.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_DDRSS_GPU_AXI_CLK] = &gcc_ddrss_gpu_axi_clk.clkr,
	[GCC_DDRSS_PCIE_SF_CLK] = &gcc_ddrss_pcie_sf_clk.clkr,
	[GCC_DDRSS_SPAD_CLK] = &gcc_ddrss_spad_clk.clkr,
	[GCC_DDRSS_SPAD_CLK_SRC] = &gcc_ddrss_spad_clk_src.clkr,
	[GCC_DISP_HF_AXI_CLK] = &gcc_disp_hf_axi_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GPLL0] = &gcc_gpll0.clkr,
	[GCC_GPLL0_OUT_EVEN] = &gcc_gpll0_out_even.clkr,
	[GCC_GPLL1] = &gcc_gpll1.clkr,
	[GCC_GPLL4] = &gcc_gpll4.clkr,
	[GCC_GPLL5] = &gcc_gpll5.clkr,
	[GCC_GPLL7] = &gcc_gpll7.clkr,
	[GCC_GPLL9] = &gcc_gpll9.clkr,
	[GCC_GPLL9_OUT_EVEN] = &gcc_gpll9_out_even.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_GPU_MEMNOC_GFX_CLK] = &gcc_gpu_memnoc_gfx_clk.clkr,
	[GCC_GPU_SNOC_DVM_GFX_CLK] = &gcc_gpu_snoc_dvm_gfx_clk.clkr,
	[GCC_IRIS_SS_HF_AXI1_CLK] = &gcc_iris_ss_hf_axi1_clk.clkr,
	[GCC_IRIS_SS_SPD_AXI1_CLK] = &gcc_iris_ss_spd_axi1_clk.clkr,
	[GCC_PCIE_0_AUX_CLK] = &gcc_pcie_0_aux_clk.clkr,
	[GCC_PCIE_0_AUX_CLK_SRC] = &gcc_pcie_0_aux_clk_src.clkr,
	[GCC_PCIE_0_CFG_AHB_CLK] = &gcc_pcie_0_cfg_ahb_clk.clkr,
	[GCC_PCIE_0_MSTR_AXI_CLK] = &gcc_pcie_0_mstr_axi_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK] = &gcc_pcie_0_phy_rchng_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK_SRC] = &gcc_pcie_0_phy_rchng_clk_src.clkr,
	[GCC_PCIE_0_PIPE_CLK] = &gcc_pcie_0_pipe_clk.clkr,
	[GCC_PCIE_0_PIPE_CLK_SRC] = &gcc_pcie_0_pipe_clk_src.clkr,
	[GCC_PCIE_0_SLV_AXI_CLK] = &gcc_pcie_0_slv_axi_clk.clkr,
	[GCC_PCIE_0_SLV_Q2A_AXI_CLK] = &gcc_pcie_0_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_1_AUX_CLK] = &gcc_pcie_1_aux_clk.clkr,
	[GCC_PCIE_1_AUX_CLK_SRC] = &gcc_pcie_1_aux_clk_src.clkr,
	[GCC_PCIE_1_CFG_AHB_CLK] = &gcc_pcie_1_cfg_ahb_clk.clkr,
	[GCC_PCIE_1_MSTR_AXI_CLK] = &gcc_pcie_1_mstr_axi_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK] = &gcc_pcie_1_phy_rchng_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK_SRC] = &gcc_pcie_1_phy_rchng_clk_src.clkr,
	[GCC_PCIE_1_PIPE_CLK] = &gcc_pcie_1_pipe_clk.clkr,
	[GCC_PCIE_1_PIPE_CLK_SRC] = &gcc_pcie_1_pipe_clk_src.clkr,
	[GCC_PCIE_1_SLV_AXI_CLK] = &gcc_pcie_1_slv_axi_clk.clkr,
	[GCC_PCIE_1_SLV_Q2A_AXI_CLK] = &gcc_pcie_1_slv_q2a_axi_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_QMIP_GPU_AHB_CLK] = &gcc_qmip_gpu_ahb_clk.clkr,
	[GCC_QMIP_PCIE_AHB_CLK] = &gcc_qmip_pcie_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CV_CPU_AHB_CLK] = &gcc_qmip_video_cv_cpu_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CVP_AHB_CLK] = &gcc_qmip_video_cvp_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_LSR_AHB_CLK] = &gcc_qmip_video_lsr_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_V_CPU_AHB_CLK] = &gcc_qmip_video_v_cpu_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_VCODEC_AHB_CLK] = &gcc_qmip_video_vcodec_ahb_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_2X_CLK] = &gcc_qupv3_wrap0_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_CLK] = &gcc_qupv3_wrap0_core_clk.clkr,
	[GCC_QUPV3_WRAP0_S0_CLK] = &gcc_qupv3_wrap0_s0_clk.clkr,
	[GCC_QUPV3_WRAP0_S0_CLK_SRC] = &gcc_qupv3_wrap0_s0_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S1_CLK] = &gcc_qupv3_wrap0_s1_clk.clkr,
	[GCC_QUPV3_WRAP0_S1_CLK_SRC] = &gcc_qupv3_wrap0_s1_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S2_CLK] = &gcc_qupv3_wrap0_s2_clk.clkr,
	[GCC_QUPV3_WRAP0_S2_CLK_SRC] = &gcc_qupv3_wrap0_s2_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S3_CLK] = &gcc_qupv3_wrap0_s3_clk.clkr,
	[GCC_QUPV3_WRAP0_S3_CLK_SRC] = &gcc_qupv3_wrap0_s3_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S4_CLK] = &gcc_qupv3_wrap0_s4_clk.clkr,
	[GCC_QUPV3_WRAP0_S4_CLK_SRC] = &gcc_qupv3_wrap0_s4_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S5_CLK] = &gcc_qupv3_wrap0_s5_clk.clkr,
	[GCC_QUPV3_WRAP0_S5_CLK_SRC] = &gcc_qupv3_wrap0_s5_clk_src.clkr,
	[GCC_QUPV3_WRAP1_CORE_2X_CLK] = &gcc_qupv3_wrap1_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP1_CORE_CLK] = &gcc_qupv3_wrap1_core_clk.clkr,
	[GCC_QUPV3_WRAP1_S0_CLK] = &gcc_qupv3_wrap1_s0_clk.clkr,
	[GCC_QUPV3_WRAP1_S0_CLK_SRC] = &gcc_qupv3_wrap1_s0_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S1_CLK] = &gcc_qupv3_wrap1_s1_clk.clkr,
	[GCC_QUPV3_WRAP1_S1_CLK_SRC] = &gcc_qupv3_wrap1_s1_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S2_CLK] = &gcc_qupv3_wrap1_s2_clk.clkr,
	[GCC_QUPV3_WRAP1_S2_CLK_SRC] = &gcc_qupv3_wrap1_s2_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S3_CLK] = &gcc_qupv3_wrap1_s3_clk.clkr,
	[GCC_QUPV3_WRAP1_S3_CLK_SRC] = &gcc_qupv3_wrap1_s3_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S4_CLK] = &gcc_qupv3_wrap1_s4_clk.clkr,
	[GCC_QUPV3_WRAP1_S4_CLK_SRC] = &gcc_qupv3_wrap1_s4_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S5_CLK] = &gcc_qupv3_wrap1_s5_clk.clkr,
	[GCC_QUPV3_WRAP1_S5_CLK_SRC] = &gcc_qupv3_wrap1_s5_clk_src.clkr,
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = &gcc_qupv3_wrap_0_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = &gcc_qupv3_wrap_0_s_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_M_AHB_CLK] = &gcc_qupv3_wrap_1_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_S_AHB_CLK] = &gcc_qupv3_wrap_1_s_ahb_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_APPS_CLK_SRC] = &gcc_sdcc1_apps_clk_src.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK_SRC] = &gcc_sdcc1_ice_core_clk_src.clkr,
	[GCC_USB30_PRIM_MASTER_CLK] = &gcc_usb30_prim_master_clk.clkr,
	[GCC_USB30_PRIM_MASTER_CLK_SRC] = &gcc_usb30_prim_master_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK] = &gcc_usb30_prim_mock_utmi_clk.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK_SRC] = &gcc_usb30_prim_mock_utmi_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb30_prim_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_PRIM_SLEEP_CLK] = &gcc_usb30_prim_sleep_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK] = &gcc_usb3_prim_phy_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK_SRC] = &gcc_usb3_prim_phy_aux_clk_src.clkr,
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = &gcc_usb3_prim_phy_com_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = &gcc_usb3_prim_phy_pipe_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK_SRC] = &gcc_usb3_prim_phy_pipe_clk_src.clkr,
	[GCC_VIDEO_AXI0_CLK] = &gcc_video_axi0_clk.clkr,
	[GCC_VIDEO_AXI1_CLK] = &gcc_video_axi1_clk.clkr,
};

static const struct qcom_reset_map gcc_sar2130p_resets[] = {
	[GCC_DISPLAY_BCR] = { 0x37000 },
	[GCC_GPU_BCR] = { 0x9b000 },
	[GCC_PCIE_0_BCR] = { 0x7b000 },
	[GCC_PCIE_0_LINK_DOWN_BCR] = { 0x7c014 },
	[GCC_PCIE_0_NOCSR_COM_PHY_BCR] = { 0x7c020 },
	[GCC_PCIE_0_PHY_BCR] = { 0x7c01c },
	[GCC_PCIE_0_PHY_NOCSR_COM_PHY_BCR] = { 0x7c028 },
	[GCC_PCIE_1_BCR] = { 0x9d000 },
	[GCC_PCIE_1_LINK_DOWN_BCR] = { 0x9e014 },
	[GCC_PCIE_1_NOCSR_COM_PHY_BCR] = { 0x9e020 },
	[GCC_PCIE_1_PHY_BCR] = { 0x9e01c },
	[GCC_PCIE_1_PHY_NOCSR_COM_PHY_BCR] = { 0x9e024 },
	[GCC_PCIE_PHY_BCR] = { 0x7f000 },
	[GCC_PCIE_PHY_CFG_AHB_BCR] = { 0x7f00c },
	[GCC_PCIE_PHY_COM_BCR] = { 0x7f010 },
	[GCC_PDM_BCR] = { 0x43000 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x28000 },
	[GCC_QUPV3_WRAPPER_1_BCR] = { 0x2e000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x22000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x22004 },
	[GCC_SDCC1_BCR] = { 0x26000 },
	[GCC_USB30_PRIM_BCR] = { 0x49000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x60008 },
	[GCC_USB3_DP_PHY_SEC_BCR] = { 0x60014 },
	[GCC_USB3_PHY_PRIM_BCR] = { 0x60000 },
	[GCC_USB3_PHY_SEC_BCR] = { 0x6000c },
	[GCC_USB3PHY_PHY_PRIM_BCR] = { 0x60004 },
	[GCC_USB3PHY_PHY_SEC_BCR] = { 0x60010 },
	[GCC_VIDEO_AXI0_CLK_ARES] = { .reg = 0x42018, .bit = 2, .udelay = 1000 },
	[GCC_VIDEO_AXI1_CLK_ARES] = { .reg = 0x42024, .bit = 2, .udelay = 1000 },
	[GCC_VIDEO_BCR] = { 0x42000 },
	[GCC_IRIS_SS_HF_AXI_CLK_ARES] = { .reg = 0x42030, .bit = 2 },
	[GCC_IRIS_SS_SPD_AXI_CLK_ARES] = { .reg = 0x70020, .bit = 2 },
	[GCC_DDRSS_SPAD_CLK_ARES] = { .reg = 0x70000, .bit = 2 },
};

static const struct clk_rcg_dfs_data gcc_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s5_clk_src),
};

static struct gdsc *gcc_sar2130p_gdscs[] = {
	[HLOS1_VOTE_MM_SNOC_MMU_TBU_HF0_GDSC] = &hlos1_vote_mm_snoc_mmu_tbu_hf0_gdsc,
	[HLOS1_VOTE_MM_SNOC_MMU_TBU_SF0_GDSC] = &hlos1_vote_mm_snoc_mmu_tbu_sf0_gdsc,
	[HLOS1_VOTE_TURING_MMU_TBU0_GDSC] = &hlos1_vote_turing_mmu_tbu0_gdsc,
	[HLOS1_VOTE_TURING_MMU_TBU1_GDSC] = &hlos1_vote_turing_mmu_tbu1_gdsc,
	[PCIE_0_GDSC] = &pcie_0_gdsc,
	[PCIE_0_PHY_GDSC] = &pcie_0_phy_gdsc,
	[PCIE_1_GDSC] = &pcie_1_gdsc,
	[PCIE_1_PHY_GDSC] = &pcie_1_phy_gdsc,
	[USB30_PRIM_GDSC] = &usb30_prim_gdsc,
	[USB3_PHY_GDSC] = &usb3_phy_gdsc,
};

static const struct regmap_config gcc_sar2130p_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1f1030,
	.fast_io = true,
};

static const struct qcom_cc_desc gcc_sar2130p_desc = {
	.config = &gcc_sar2130p_regmap_config,
	.clks = gcc_sar2130p_clocks,
	.num_clks = ARRAY_SIZE(gcc_sar2130p_clocks),
	.resets = gcc_sar2130p_resets,
	.num_resets = ARRAY_SIZE(gcc_sar2130p_resets),
	.gdscs = gcc_sar2130p_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_sar2130p_gdscs),
};

static const struct of_device_id gcc_sar2130p_match_table[] = {
	{ .compatible = "qcom,sar2130p-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_sar2130p_match_table);

static int gcc_sar2130p_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gcc_sar2130p_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks,
				       ARRAY_SIZE(gcc_dfs_clocks));
	if (ret)
		return ret;

	/* Keep some clocks always-on */
	qcom_branch_set_clk_en(regmap, 0x37004); /* GCC_DISP_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0x42004); /* GCC_VIDEO_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0x42028); /* GCC_VIDEO_XO_CLK */
	qcom_branch_set_clk_en(regmap, 0x9b004); /* GCC_GPU_CFG_AHB_CLK */

	/* Clear GDSC_SLEEP_ENA_VOTE to stop votes being auto-removed in sleep. */
	regmap_write(regmap, 0x62204, 0x0);

	return qcom_cc_really_probe(&pdev->dev, &gcc_sar2130p_desc, regmap);
}

static struct platform_driver gcc_sar2130p_driver = {
	.probe = gcc_sar2130p_probe,
	.driver = {
		.name = "gcc-sar2130p",
		.of_match_table = gcc_sar2130p_match_table,
	},
};

static int __init gcc_sar2130p_init(void)
{
	return platform_driver_register(&gcc_sar2130p_driver);
}
subsys_initcall(gcc_sar2130p_init);

static void __exit gcc_sar2130p_exit(void)
{
	platform_driver_unregister(&gcc_sar2130p_driver);
}
module_exit(gcc_sar2130p_exit);

MODULE_DESCRIPTION("QTI GCC SAR2130P Driver");
MODULE_LICENSE("GPL");
