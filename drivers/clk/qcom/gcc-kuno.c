// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,kuno-gcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-phy-mux.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

/* Need to match the order of clocks in DT binding */
enum {
	DT_BI_TCXO,
	DT_BI_TCXO_AO,
	DT_SLEEP_CLK,
	DT_PCIE_PIPE_CLK,
};

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_EVEN,
	P_GPLL0_OUT_MAIN,
	P_GPLL2_OUT_MAIN,
	P_GPLL3_OUT_MAIN,
	P_GPLL4_OUT_EVEN,
	P_GPLL4_OUT_MAIN,
	P_SLEEP_CLK,
};

static struct clk_alpha_pll gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x7d000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 10,
	.post_div_table = post_div_table_gpll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

static struct clk_alpha_pll gpll2 = {
	.offset = 0x2000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x7d000,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll2",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
	},
};

static struct clk_alpha_pll gpll3 = {
	.offset = 0x3000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x7d000,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll3",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
	},
};

static struct clk_alpha_pll gpll4 = {
	.offset = 0x4000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x7d000,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll4",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_evo_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll4_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll4_out_even = {
	.offset = 0x4000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_gpll4_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll4_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll4_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll4.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL4_OUT_EVEN, 2 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll4_out_even.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL4_OUT_EVEN, 2 },
	{ P_SLEEP_CLK, 5 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll4_out_even.clkr.hw },
	{ .index = DT_SLEEP_CLK },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL4_OUT_EVEN, 2 },
	{ P_GPLL4_OUT_MAIN, 3 },
	{ P_GPLL2_OUT_MAIN, 4 },
	{ P_GPLL3_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll4_out_even.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &gpll2.clkr.hw },
	{ .hw = &gpll3.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_BI_TCXO, 2 },
};

static struct clk_regmap_mux gcc_pcie_aux_clk_src = {
	.reg = 0x5308c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_6,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_aux_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_pipe_clk_src = {
	.reg = 0x53070,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_emac0_phy_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_phy_aux_clk_src = {
	.cmd_rcgr = 0x7102c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_phy_aux_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_ptp_clk_src[] = {
	F(62500000, P_GPLL4_OUT_EVEN, 4, 0, 0),
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(125000000, P_GPLL2_OUT_MAIN, 4, 0, 0),
	F(230400000, P_GPLL3_OUT_MAIN, 3.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_ptp_clk_src = {
	.cmd_rcgr = 0x71064,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_emac0_ptp_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_ptp_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_rgmii_clk_src[] = {
	F(41666667, P_GPLL4_OUT_EVEN, 6, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(125000000, P_GPLL2_OUT_MAIN, 4, 0, 0),
	F(250000000, P_GPLL2_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_rgmii_clk_src = {
	.cmd_rcgr = 0x7104c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_emac0_rgmii_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_rgmii_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(41666667, P_GPLL4_OUT_EVEN, 6, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x47004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x48004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x49004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie_aux_phy_clk_src = {
	.cmd_rcgr = 0x53074,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_aux_phy_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_rchng_phy_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(83333333, P_GPLL4_OUT_EVEN, 3, 0, 0),
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_rchng_phy_clk_src = {
	.cmd_rcgr = 0x53038,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_rchng_phy_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_rchng_phy_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(60000000, P_GPLL0_OUT_MAIN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x34010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s0_clk_src[] = {
	F(7372800, P_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GPLL0_OUT_EVEN, 1, 4, 25),
	F(62500000, P_GPLL4_OUT_EVEN, 4, 0, 0),
	F(64000000, P_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x6c004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s1_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x6c13c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x6c274,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x6c3ac,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x6c4e4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_sdcc4_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_EVEN, 12, 1, 1),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(83333333, P_GPLL4_OUT_EVEN, 3, 0, 0),
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc4_apps_clk_src = {
	.cmd_rcgr = 0x6a01c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_sdcc4_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc4_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb20_master_clk_src[] = {
	F(50000000, P_GPLL4_OUT_EVEN, 5, 0, 0),
	F(60000000, P_GPLL0_OUT_EVEN, 5, 0, 0),
	F(120000000, P_GPLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb20_master_clk_src = {
	.cmd_rcgr = 0x27048,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb20_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb20_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb20_mock_utmi_clk_src = {
	.cmd_rcgr = 0x2702c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_emac0_phy_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb20_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div gcc_usb20_mock_utmi_postdiv_clk_src = {
	.reg = 0x27044,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb20_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb20_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x37004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x37004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_axi_clk = {
	.halt_reg = 0x71018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_phy_aux_clk = {
	.halt_reg = 0x71028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x71028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_ptp_clk = {
	.halt_reg = 0x71044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x71044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_ptp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_ptp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_rgmii_clk = {
	.halt_reg = 0x71048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x71048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_rgmii_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_emac0_rgmii_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_slv_ahb_clk = {
	.halt_reg = 0x71024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_slv_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac_0_clkref_en = {
	.halt_reg = 0x94004,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x94004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac_0_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x47000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x47000,
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
	.halt_reg = 0x48000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x48000,
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
	.halt_reg = 0x49000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x49000,
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

static struct clk_branch gcc_pcie_0_clkref_en = {
	.halt_reg = 0x94000,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x94000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_aux_clk = {
	.halt_reg = 0x53054,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x53054,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_cfg_ahb_clk = {
	.halt_reg = 0x53034,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53034,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_mstr_axi_clk = {
	.halt_reg = 0x53028,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_pipe_clk = {
	.halt_reg = 0x53064,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x53064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_rchng_phy_clk = {
	.halt_reg = 0x53050,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53050,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_rchng_phy_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_rchng_phy_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_sleep_clk = {
	.halt_reg = 0x53060,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53060,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_sleep_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_phy_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_slv_axi_clk = {
	.halt_reg = 0x5301c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_slv_q2a_axi_clk = {
	.halt_reg = 0x53018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x53018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d010,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x3400c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3400c,
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
	.halt_reg = 0x34004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x34004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_xo4_clk = {
	.halt_reg = 0x34008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x34008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x2d018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x2d008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x6c130,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(16),
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
	.halt_reg = 0x6c268,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(17),
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
	.halt_reg = 0x6c3a0,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(18),
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
	.halt_reg = 0x6c4d8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(19),
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
	.halt_reg = 0x6c610,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(20),
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

static struct clk_branch gcc_qupv3_wrap_0_m_ahb_clk = {
	.halt_reg = 0x2d000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2d000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_s_ahb_clk = {
	.halt_reg = 0x2d004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2d004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7d008,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc4_ahb_clk = {
	.halt_reg = 0x6a010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6a010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc4_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc4_apps_clk = {
	.halt_reg = 0x6a004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6a004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc4_apps_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sdcc4_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_snoc_cnoc_usb3_clk = {
	.halt_reg = 0x27060,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x27060,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x27060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_snoc_cnoc_usb3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_usb_sf_axi_clk = {
	.halt_reg = 0x27064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x27064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x27064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sys_noc_usb_sf_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_master_clk = {
	.halt_reg = 0x27018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x27018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb20_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_mock_utmi_clk = {
	.halt_reg = 0x27028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x27028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb20_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_sleep_clk = {
	.halt_reg = 0x27024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x27024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb20_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2_clkref_en = {
	.halt_reg = 0x94008,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x94008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb2_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_clkref_en = {
	.halt_reg = 0x9400c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x9400c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_phy_cfg_ahb2phy_clk = {
	.halt_reg = 0x29004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x29004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x29004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb_phy_cfg_ahb2phy_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct gdsc gcc_emac0_gdsc = {
	.gdscr = 0x71004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_emac0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc gcc_pcie_gdsc = {
	.gdscr = 0x53004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb20_gdsc = {
	.gdscr = 0x27004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb20_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct clk_regmap *gcc_kuno_clocks[] = {
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_EMAC0_AXI_CLK] = &gcc_emac0_axi_clk.clkr,
	[GCC_EMAC0_PHY_AUX_CLK] = &gcc_emac0_phy_aux_clk.clkr,
	[GCC_EMAC0_PHY_AUX_CLK_SRC] = &gcc_emac0_phy_aux_clk_src.clkr,
	[GCC_EMAC0_PTP_CLK] = &gcc_emac0_ptp_clk.clkr,
	[GCC_EMAC0_PTP_CLK_SRC] = &gcc_emac0_ptp_clk_src.clkr,
	[GCC_EMAC0_RGMII_CLK] = &gcc_emac0_rgmii_clk.clkr,
	[GCC_EMAC0_RGMII_CLK_SRC] = &gcc_emac0_rgmii_clk_src.clkr,
	[GCC_EMAC0_SLV_AHB_CLK] = &gcc_emac0_slv_ahb_clk.clkr,
	[GCC_EMAC_0_CLKREF_EN] = &gcc_emac_0_clkref_en.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_PCIE_0_CLKREF_EN] = &gcc_pcie_0_clkref_en.clkr,
	[GCC_PCIE_AUX_CLK] = &gcc_pcie_aux_clk.clkr,
	[GCC_PCIE_AUX_CLK_SRC] = &gcc_pcie_aux_clk_src.clkr,
	[GCC_PCIE_AUX_PHY_CLK_SRC] = &gcc_pcie_aux_phy_clk_src.clkr,
	[GCC_PCIE_CFG_AHB_CLK] = &gcc_pcie_cfg_ahb_clk.clkr,
	[GCC_PCIE_MSTR_AXI_CLK] = &gcc_pcie_mstr_axi_clk.clkr,
	[GCC_PCIE_PIPE_CLK] = &gcc_pcie_pipe_clk.clkr,
	[GCC_PCIE_PIPE_CLK_SRC] = &gcc_pcie_pipe_clk_src.clkr,
	[GCC_PCIE_RCHNG_PHY_CLK] = &gcc_pcie_rchng_phy_clk.clkr,
	[GCC_PCIE_RCHNG_PHY_CLK_SRC] = &gcc_pcie_rchng_phy_clk_src.clkr,
	[GCC_PCIE_SLEEP_CLK] = &gcc_pcie_sleep_clk.clkr,
	[GCC_PCIE_SLV_AXI_CLK] = &gcc_pcie_slv_axi_clk.clkr,
	[GCC_PCIE_SLV_Q2A_AXI_CLK] = &gcc_pcie_slv_q2a_axi_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
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
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = &gcc_qupv3_wrap_0_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = &gcc_qupv3_wrap_0_s_ahb_clk.clkr,
	[GCC_SDCC4_AHB_CLK] = &gcc_sdcc4_ahb_clk.clkr,
	[GCC_SDCC4_APPS_CLK] = &gcc_sdcc4_apps_clk.clkr,
	[GCC_SDCC4_APPS_CLK_SRC] = &gcc_sdcc4_apps_clk_src.clkr,
	[GCC_SNOC_CNOC_USB3_CLK] = &gcc_snoc_cnoc_usb3_clk.clkr,
	[GCC_SYS_NOC_USB_SF_AXI_CLK] = &gcc_sys_noc_usb_sf_axi_clk.clkr,
	[GCC_USB20_MASTER_CLK] = &gcc_usb20_master_clk.clkr,
	[GCC_USB20_MASTER_CLK_SRC] = &gcc_usb20_master_clk_src.clkr,
	[GCC_USB20_MOCK_UTMI_CLK] = &gcc_usb20_mock_utmi_clk.clkr,
	[GCC_USB20_MOCK_UTMI_CLK_SRC] = &gcc_usb20_mock_utmi_clk_src.clkr,
	[GCC_USB20_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb20_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB20_SLEEP_CLK] = &gcc_usb20_sleep_clk.clkr,
	[GCC_USB2_CLKREF_EN] = &gcc_usb2_clkref_en.clkr,
	[GCC_USB3_PRIM_CLKREF_EN] = &gcc_usb3_prim_clkref_en.clkr,
	[GCC_USB_PHY_CFG_AHB2PHY_CLK] = &gcc_usb_phy_cfg_ahb2phy_clk.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL0_OUT_EVEN] = &gpll0_out_even.clkr,
	[GPLL2] = &gpll2.clkr,
	[GPLL3] = &gpll3.clkr,
	[GPLL4] = &gpll4.clkr,
	[GPLL4_OUT_EVEN] = &gpll4_out_even.clkr,
};

static const struct qcom_reset_map gcc_kuno_resets[] = {
	[GCC_EMAC0_BCR] = { 0x71000 },
	[GCC_PCIE_BCR] = { 0x53000 },
	[GCC_PCIE_LINK_DOWN_BCR] = { 0x87000 },
	[GCC_PCIE_NOCSR_COM_PHY_BCR] = { 0x88008 },
	[GCC_PCIE_PHY_BCR] = { 0x54000 },
	[GCC_PCIE_PHY_CFG_AHB_BCR] = { 0x88000 },
	[GCC_PCIE_PHY_COM_BCR] = { 0x88004 },
	[GCC_PCIE_PHY_NOCSR_COM_PHY_BCR] = { 0x8800c },
	[GCC_PDM_BCR] = { 0x34000 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x6c000 },
	[GCC_QUSB2PHY_BCR] = { 0x2a000 },
	[GCC_SDCC4_BCR] = { 0x6a000 },
	[GCC_TCSR_PCIE_BCR] = { 0x84000 },
	[GCC_USB20_BCR] = { 0x27000 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x29000 },
};

static struct gdsc *gcc_kuno_gdscs[] = {
	[GCC_EMAC0_GDSC] = &gcc_emac0_gdsc,
	[GCC_PCIE_GDSC] = &gcc_pcie_gdsc,
	[GCC_USB20_GDSC] = &gcc_usb20_gdsc,
};

static const struct clk_rcg_dfs_data gcc_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s4_clk_src),
};

static const struct regmap_config gcc_kuno_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1f41f0,
	.fast_io = true,
};

static const u32 gcc_kuno_critical_cbcrs[] = {
	0x3e004, /* GCC_AHB_PCIE_LINK_CLK */
	0x3e008, /* GCC_XO_PCIE_LINK_CLK */
};

static const struct qcom_cc_driver_data gcc_kuno_driver_data = {
	.dfs_rcgs = gcc_dfs_clocks,
	.num_dfs_rcgs = ARRAY_SIZE(gcc_dfs_clocks),
	.clk_cbcrs = gcc_kuno_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(gcc_kuno_critical_cbcrs),
};

static const struct qcom_cc_desc gcc_kuno_desc = {
	.config = &gcc_kuno_regmap_config,
	.clks = gcc_kuno_clocks,
	.num_clks = ARRAY_SIZE(gcc_kuno_clocks),
	.resets = gcc_kuno_resets,
	.num_resets = ARRAY_SIZE(gcc_kuno_resets),
	.gdscs = gcc_kuno_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_kuno_gdscs),
	.use_rpm = true,
	.driver_data = &gcc_kuno_driver_data,
};

static const struct of_device_id gcc_kuno_match_table[] = {
	{ .compatible = "qcom,kuno-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_kuno_match_table);

static int gcc_kuno_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gcc_kuno_desc);
}

static struct platform_driver gcc_kuno_driver = {
	.probe = gcc_kuno_probe,
	.driver = {
		.name = "gcc-kuno",
		.of_match_table = gcc_kuno_match_table,
	},
};

subsys_platform_driver(gcc_kuno_driver);

MODULE_DESCRIPTION("QTI GCC Kuno Driver");
MODULE_LICENSE("GPL");
