// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gcc-seraph.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "reset.h"
#include "vdd-level.h"

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_HIGH + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mx, VDD_HIGH + 1, 1, vdd_corner);

static struct clk_vdd_class *gcc_seraph_regulators[] = {
	&vdd_cx,
	&vdd_mx,
};

enum {
	P_BI_TCXO,
	P_GCC_GPLL0_OUT_EVEN,
	P_GCC_GPLL0_OUT_MAIN,
	P_GCC_GPLL2_OUT_MAIN,
	P_GCC_GPLL4_OUT_MAIN,
	P_GCC_GPLL6_OUT_MAIN,
	P_GCC_GPLL7_OUT_MAIN,
	P_GCC_GPLL8_OUT_MAIN,
	P_PCIE_0_PIPE_CLK,
	P_PCIE_1_PIPE_CLK,
	P_SLEEP_CLK,
	P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
};

static struct clk_alpha_pll gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 621000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000,
				[VDD_HIGH] = 2500000000},
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
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_taycan_eko_t_ops,
	},
};

static struct clk_alpha_pll gcc_gpll2 = {
	.offset = 0x2000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll2",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 621000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000,
				[VDD_HIGH] = 2500000000},
		},
	},
};

static struct clk_alpha_pll gcc_gpll4 = {
	.offset = 0x4000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll4",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 621000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000,
				[VDD_HIGH] = 2500000000},
		},
	},
};

static struct clk_alpha_pll gcc_gpll6 = {
	.offset = 0x6000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll6",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 621000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000,
				[VDD_HIGH] = 2500000000},
		},
	},
};

static struct clk_alpha_pll gcc_gpll7 = {
	.offset = 0x7000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll7",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 621000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000,
				[VDD_HIGH] = 2500000000},
		},
	},
};

static struct clk_alpha_pll gcc_gpll8 = {
	.offset = 0x8000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll8",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 621000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000,
				[VDD_HIGH] = 2500000000},
		},
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .fw_name = "sleep_clk" },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "sleep_clk" },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL2_OUT_MAIN, 2 },
	{ P_GCC_GPLL7_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll2.clkr.hw },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL2_OUT_MAIN, 2 },
	{ P_GCC_GPLL7_OUT_MAIN, 3 },
	{ P_GCC_GPLL6_OUT_MAIN, 4 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll2.clkr.hw },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .hw = &gcc_gpll6.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_PCIE_0_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .fw_name = "pcie_0_pipe_clk" },
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_PCIE_1_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .fw_name = "pcie_1_pipe_clk" },
	{ .fw_name = "bi_tcxo" },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL8_OUT_MAIN, 2 },
	{ P_GCC_GPLL7_OUT_MAIN, 3 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll8.clkr.hw },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
	{ P_GCC_GPLL0_OUT_EVEN, 3 },
};

static const struct clk_parent_data gcc_parent_data_9[] = {
	{ .fw_name = "usb3_phy_wrapper_gcc_usb30_pipe_clk" },
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static struct clk_regmap_mux gcc_pcie_0_pipe_clk_src = {
	.reg = 0x6b090,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_6,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_pipe_clk_src",
			.parent_data = gcc_parent_data_6,
			.num_parents = ARRAY_SIZE(gcc_parent_data_6),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_pcie_1_pipe_clk_src = {
	.reg = 0x8d08c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_7,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_pipe_clk_src",
			.parent_data = gcc_parent_data_7,
			.num_parents = ARRAY_SIZE(gcc_parent_data_7),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_prim_phy_pipe_clk_src = {
	.reg = 0x39070,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_9,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_9,
			.num_parents = ARRAY_SIZE(gcc_parent_data_9),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_gp10_clk_src[] = {
	F(30000000, P_GCC_GPLL0_OUT_EVEN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp10_clk_src = {
	.cmd_rcgr = 0xad004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp10_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp11_clk_src = {
	.cmd_rcgr = 0xae004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp11_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 30000000},
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x64004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 50000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 200000000},
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x65004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 50000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 200000000},
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x66004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 50000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 200000000},
	},
};

static struct clk_rcg2 gcc_gp4_clk_src = {
	.cmd_rcgr = 0xa7004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp4_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp5_clk_src = {
	.cmd_rcgr = 0xa8004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp5_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp6_clk_src = {
	.cmd_rcgr = 0xa9004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp6_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp7_clk_src = {
	.cmd_rcgr = 0xaa004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp7_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp8_clk_src = {
	.cmd_rcgr = 0xab004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp8_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 30000000},
	},
};

static struct clk_rcg2 gcc_gp9_clk_src = {
	.cmd_rcgr = 0xac004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp10_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp9_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 30000000},
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_aux_clk_src = {
	.cmd_rcgr = 0x6b098,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_aux_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 19200000},
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_phy_rchng_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_phy_rchng_clk_src = {
	.cmd_rcgr = 0x6b078,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 100000000},
	},
};

static struct clk_rcg2 gcc_pcie_1_aux_clk_src = {
	.cmd_rcgr = 0x8d094,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 19200000},
	},
};

static struct clk_rcg2 gcc_pcie_1_phy_rchng_clk_src = {
	.cmd_rcgr = 0x8d074,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 100000000},
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(60000000, P_GCC_GPLL0_OUT_MAIN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x33010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 60000000},
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap1_s0_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(51200000, P_GCC_GPLL0_OUT_EVEN, 1, 64, 375),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(102400000, P_GCC_GPLL0_OUT_EVEN, 1, 128, 375),
	F(112000000, P_GCC_GPLL0_OUT_EVEN, 1, 28, 75),
	F(117964800, P_GCC_GPLL0_OUT_EVEN, 1, 6144, 15625),
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap1_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s0_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0x18014,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s0_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap1_s0_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 150000000},
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap1_s1_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(51200000, P_GCC_GPLL0_OUT_EVEN, 1, 64, 375),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(102400000, P_GCC_GPLL0_OUT_EVEN, 1, 128, 375),
	F(112000000, P_GCC_GPLL0_OUT_EVEN, 1, 28, 75),
	F(117964800, P_GCC_GPLL0_OUT_EVEN, 1, 6144, 15625),
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	F(128000000, P_GCC_GPLL2_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap1_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s1_clk_src",
	.parent_data = gcc_parent_data_4,
	.num_parents = ARRAY_SIZE(gcc_parent_data_4),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s1_clk_src = {
	.cmd_rcgr = 0x18150,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s1_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap1_s1_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 128000000},
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap1_s2_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(51200000, P_GCC_GPLL0_OUT_EVEN, 1, 64, 375),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap1_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s2_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s2_clk_src = {
	.cmd_rcgr = 0x1828c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s2_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap1_s2_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 100000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s3_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s3_clk_src = {
	.cmd_rcgr = 0x183c8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s2_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap1_s3_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 100000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0x18504,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s0_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap1_s4_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 150000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0x18640,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s2_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap1_s5_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 100000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap2_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s0_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s0_clk_src = {
	.cmd_rcgr = 0x1e014,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s0_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap2_s0_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 150000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap2_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s1_clk_src",
	.parent_data = gcc_parent_data_4,
	.num_parents = ARRAY_SIZE(gcc_parent_data_4),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s1_clk_src = {
	.cmd_rcgr = 0x1e150,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s1_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap2_s1_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 128000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap2_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s2_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s2_clk_src = {
	.cmd_rcgr = 0x1e28c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s2_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap2_s2_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 100000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap2_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s3_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s3_clk_src = {
	.cmd_rcgr = 0x1e3c8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s2_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap2_s3_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 100000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap2_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s4_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s4_clk_src = {
	.cmd_rcgr = 0x1e504,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s0_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap2_s4_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 150000000},
	},
};

static struct clk_init_data gcc_qupv3_wrap2_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s5_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.ops = &clk_rcg2_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s5_clk_src = {
	.cmd_rcgr = 0x1e640,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap1_s2_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &gcc_qupv3_wrap2_s5_clk_src_init,
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 100000000},
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, P_BI_TCXO, 16, 3, 25),
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(20000000, P_GCC_GPLL0_OUT_EVEN, 5, 1, 3),
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(192000000, P_GCC_GPLL2_OUT_MAIN, 2, 0, 0),
	F(384000000, P_GCC_GPLL2_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_apps_clk_src = {
	.cmd_rcgr = 0xa3018,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_floor_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 75000000,
			[VDD_LOWER] = 100000000,
			[VDD_LOW_L1] = 384000000},
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_ice_core_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_EVEN, 2, 0, 0),
	F(300000000, P_GCC_GPLL0_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0xa3040,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_sdcc1_ice_core_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_ice_core_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 100000000,
			[VDD_LOW] = 150000000,
			[VDD_LOW_L1] = 300000000},
	},
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(37500000, P_GCC_GPLL0_OUT_EVEN, 8, 0, 0),
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(202000000, P_GCC_GPLL8_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc2_apps_clk_src = {
	.cmd_rcgr = 0x1401c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_floor_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 75000000,
			[VDD_LOWER] = 100000000,
			[VDD_LOW_L1] = 202000000},
	},
};

static const struct freq_tbl ftbl_gcc_usb30_prim_master_clk_src[] = {
	F(66666667, P_GCC_GPLL0_OUT_EVEN, 4.5, 0, 0),
	F(133333333, P_GCC_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	F(240000000, P_GCC_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_prim_master_clk_src = {
	.cmd_rcgr = 0x39030,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_prim_master_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gcc_seraph_regulators,
		.num_vdd_classes = ARRAY_SIZE(gcc_seraph_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 66666667,
			[VDD_LOW] = 133333333,
			[VDD_NOMINAL] = 200000000,
			[VDD_HIGH] = 240000000},
	},
};

static struct clk_rcg2 gcc_usb30_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0x39048,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 19200000},
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0x39074,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER_D1] = 19200000},
	},
};

static struct clk_regmap_div gcc_gp10_div_clk_src = {
	.reg = 0xad01c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp10_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gp10_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp11_div_clk_src = {
	.reg = 0xae01c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp11_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gp11_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp4_div_clk_src = {
	.reg = 0xa701c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp4_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gp4_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp5_div_clk_src = {
	.reg = 0xa801c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp5_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gp5_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp6_div_clk_src = {
	.reg = 0xa901c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp6_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gp6_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp7_div_clk_src = {
	.reg = 0xaa01c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp7_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gp7_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp8_div_clk_src = {
	.reg = 0xab01c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp8_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gp8_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_gp9_div_clk_src = {
	.reg = 0xac01c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp9_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gp9_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_0_pipe_div_clk_src = {
	.reg = 0x6b094,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_pcie_0_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_1_pipe_div_clk_src = {
	.reg = 0x8d090,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_pcie_1_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pdm_xo4_div_clk_src = {
	.reg = 0x33028,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pdm_xo4_div_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_pwm0_xo512_clk_src = {
	.reg = 0x33030,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pwm0_xo512_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_pwm1_xo512_clk_src = {
	.reg = 0x33038,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pwm1_xo512_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_pwm2_xo512_clk_src = {
	.reg = 0x33040,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pwm2_xo512_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_pwm3_xo512_clk_src = {
	.reg = 0x33048,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pwm3_xo512_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_pwm4_xo512_clk_src = {
	.reg = 0x33050,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pwm4_xo512_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_pwm5_xo512_clk_src = {
	.reg = 0x33058,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pwm5_xo512_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_pwm6_xo512_clk_src = {
	.reg = 0x33060,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pwm6_xo512_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_pwm7_xo512_clk_src = {
	.reg = 0x33068,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pwm7_xo512_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_pwm8_xo512_clk_src = {
	.reg = 0x33070,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pwm8_xo512_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_pwm9_xo512_clk_src = {
	.reg = 0x33078,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pwm9_xo512_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "bi_tcxo",
		},
		.num_parents = 1,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div gcc_usb30_prim_mock_utmi_postdiv_clk_src = {
	.reg = 0x39060,
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

static struct clk_branch gcc_aggre_noc_pcie_axi_clk = {
	.halt_reg = 0x10068,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x10068,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_prim_axi_clk = {
	.halt_reg = 0x39094,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x39094,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x39094,
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
	.halt_reg = 0x38004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_cti_clk = {
	.halt_reg = 0x26028,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x26028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_cti_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_hf_axi_clk = {
	.halt_reg = 0x26010,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x26010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_sf_axi_clk = {
	.halt_reg = 0x26018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x26018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_tsctr_clk = {
	.halt_reg = 0x2602c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2602c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2602c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_tsctr_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_pcie_anoc_ahb_clk = {
	.halt_reg = 0x10050,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x10050,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_pcie_anoc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x39090,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x39090,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x39090,
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

static struct clk_branch gcc_cnoc_pcie_sf_axi_clk = {
	.halt_reg = 0x10058,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x10058,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_pcie_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_pcie_sf_qtb_clk = {
	.halt_reg = 0x10080,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x10080,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ddrss_pcie_sf_qtb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_0_hf_axi_clk = {
	.halt_reg = 0x2700c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x2700c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2700c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_0_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_sf_axi_clk = {
	.halt_reg = 0x27028,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x27028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x27028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_tsctr_clk = {
	.halt_reg = 0x2703c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2703c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2703c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_tsctr_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_eva_axi0_clk = {
	.halt_reg = 0xb2008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xb2008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb2008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_eva_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_eva_axi0c_clk = {
	.halt_reg = 0xb201c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xb201c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb201c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_eva_axi0c_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp10_clk = {
	.halt_reg = 0xad000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xad000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp10_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp10_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp11_clk = {
	.halt_reg = 0xae000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xae000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp11_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp11_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x64000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x64000,
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
	.halt_reg = 0x65000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x65000,
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
	.halt_reg = 0x66000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x66000,
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

static struct clk_branch gcc_gp4_clk = {
	.halt_reg = 0xa7000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa7000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp4_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp5_clk = {
	.halt_reg = 0xa8000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa8000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp5_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp6_clk = {
	.halt_reg = 0xa9000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa9000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp6_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp7_clk = {
	.halt_reg = 0xaa000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xaa000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp7_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp7_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp8_clk = {
	.halt_reg = 0xab000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xab000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp8_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp8_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp9_clk = {
	.halt_reg = 0xac000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xac000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp9_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_gp9_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_gpll0_clk_src = {
	.halt_reg = 0x71024,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x71024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
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
	.halt_reg = 0x7102c,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x7102c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
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
	.halt_reg = 0x71010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_memnoc_gfx_clk",
			.flags = CLK_DONT_HOLD_STATE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_lsr_axi0_clk = {
	.halt_reg = 0xb3008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xb3008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb3008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_lsr_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_lsr_axi_cv_cpu_clk = {
	.halt_reg = 0xb301c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xb301c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb301c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_lsr_axi_cv_cpu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_aux_clk = {
	.halt_reg = 0x6b044,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
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
	.halt_reg = 0x6b040,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_mstr_axi_clk = {
	.halt_reg = 0x6b030,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_phy_rchng_clk = {
	.halt_reg = 0x6b074,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
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
	.halt_reg = 0x6b054,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52008,
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

static struct clk_branch gcc_pcie_0_pipe_div2_clk = {
	.halt_reg = 0x6b064,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_0_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_axi_clk = {
	.halt_reg = 0x6b020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_q2a_axi_clk = {
	.halt_reg = 0x6b01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_aux_clk = {
	.halt_reg = 0x8d040,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8d040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
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
	.halt_reg = 0x8d03c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8d03c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_mstr_axi_clk = {
	.halt_reg = 0x8d02c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x8d02c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_phy_rchng_clk = {
	.halt_reg = 0x8d070,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8d070,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
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
	.halt_reg = 0x8d050,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x8d050,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
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

static struct clk_branch gcc_pcie_1_pipe_div2_clk = {
	.halt_reg = 0x8d060,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x8d060,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_1_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_axi_clk = {
	.halt_reg = 0x8d01c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8d01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_q2a_axi_clk = {
	.halt_reg = 0x8d018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8d018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x3300c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3300c,
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
	.halt_reg = 0x33004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x33004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x33004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_xo4_clk = {
	.halt_reg = 0x33008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x33008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pdm_xo4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pdm_xo4_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm0_xo512_clk = {
	.halt_reg = 0x3302c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3302c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pwm0_xo512_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pwm0_xo512_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm1_xo512_clk = {
	.halt_reg = 0x33034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x33034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pwm1_xo512_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pwm1_xo512_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm2_xo512_clk = {
	.halt_reg = 0x3303c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3303c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pwm2_xo512_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pwm2_xo512_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm3_xo512_clk = {
	.halt_reg = 0x33044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x33044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pwm3_xo512_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pwm3_xo512_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm4_xo512_clk = {
	.halt_reg = 0x3304c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3304c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pwm4_xo512_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pwm4_xo512_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm5_xo512_clk = {
	.halt_reg = 0x33054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x33054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pwm5_xo512_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pwm5_xo512_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm6_xo512_clk = {
	.halt_reg = 0x3305c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3305c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pwm6_xo512_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pwm6_xo512_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm7_xo512_clk = {
	.halt_reg = 0x33064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x33064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pwm7_xo512_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pwm7_xo512_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm8_xo512_clk = {
	.halt_reg = 0x3306c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3306c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pwm8_xo512_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pwm8_xo512_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pwm9_xo512_clk = {
	.halt_reg = 0x33074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x33074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pwm9_xo512_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pwm9_xo512_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_icp_ahb_clk = {
	.halt_reg = 0x26024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x26024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_camera_icp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_nrt_ahb_clk = {
	.halt_reg = 0x26008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x26008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_camera_nrt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_rt_ahb_clk = {
	.halt_reg = 0x2600c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2600c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2600c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_camera_rt_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_disp_ahb_clk = {
	.halt_reg = 0x27008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x27008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x27008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_disp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_gpu_ahb_clk = {
	.halt_reg = 0x71008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_gpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_pcie_ahb_clk = {
	.halt_reg = 0x6b018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_pcie_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_venus_lsr0_ahb_clk = {
	.halt_reg = 0xb3028,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xb3028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb3028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_venus_lsr0_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_venus_lsr1_ahb_clk = {
	.halt_reg = 0xb302c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xb302c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb302c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_venus_lsr1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_venus_lsr_cv_cpu_ahb_clk = {
	.halt_reg = 0xb3030,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xb3030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb3030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_venus_lsr_cv_cpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cv_cpu_ahb_clk = {
	.halt_reg = 0x32014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x32014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_cv_cpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cvp_ahb_clk = {
	.halt_reg = 0x32008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x32008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_cvp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_v_cpu_ahb_clk = {
	.halt_reg = 0x32010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x32010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_v_cpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_vcodec_ahb_clk = {
	.halt_reg = 0x3200c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3200c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3200c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_vcodec_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_2x_clk = {
	.halt_reg = 0x23158,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_clk = {
	.halt_reg = 0x23144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0x18004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(22),
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
	.halt_reg = 0x18140,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(23),
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
	.halt_reg = 0x1827c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(24),
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
	.halt_reg = 0x183b8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(25),
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
	.halt_reg = 0x184f4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(26),
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
	.halt_reg = 0x18630,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(27),
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

static struct clk_branch gcc_qupv3_wrap2_core_2x_clk = {
	.halt_reg = 0x232b0,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_core_clk = {
	.halt_reg = 0x2329c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s0_clk = {
	.halt_reg = 0x1e004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap2_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s1_clk = {
	.halt_reg = 0x1e140,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap2_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s2_clk = {
	.halt_reg = 0x1e27c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap2_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s3_clk = {
	.halt_reg = 0x1e3b8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap2_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s4_clk = {
	.halt_reg = 0x1e4f4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap2_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s5_clk = {
	.halt_reg = 0x1e630,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap2_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_m_ahb_clk = {
	.halt_reg = 0x2313c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2313c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_1_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_s_ahb_clk = {
	.halt_reg = 0x23140,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23140,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_1_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_2_m_ahb_clk = {
	.halt_reg = 0x23294,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23294,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_2_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_2_s_ahb_clk = {
	.halt_reg = 0x23298,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23298,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_2_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0xa3004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa3004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0xa3008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa3008,
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
	.halt_reg = 0xa3030,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xa3030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xa3030,
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

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x14014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x14014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_apps_clk = {
	.halt_reg = 0x14004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x14004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc2_apps_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sdcc2_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_master_clk = {
	.halt_reg = 0x39018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39018,
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
	.halt_reg = 0x3902c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3902c,
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
	.halt_reg = 0x39028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_aux_clk = {
	.halt_reg = 0x39064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39064,
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
	.halt_reg = 0x39068,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39068,
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
	.halt_reg = 0x3906c,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x3906c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3906c,
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
	.halt_reg = 0x32018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x32018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi1_clk = {
	.halt_reg = 0x3202c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x3202c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3202c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *gcc_seraph_clocks[] = {
	[GCC_AGGRE_NOC_PCIE_AXI_CLK] = &gcc_aggre_noc_pcie_axi_clk.clkr,
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = &gcc_aggre_usb3_prim_axi_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAMERA_CTI_CLK] = &gcc_camera_cti_clk.clkr,
	[GCC_CAMERA_HF_AXI_CLK] = &gcc_camera_hf_axi_clk.clkr,
	[GCC_CAMERA_SF_AXI_CLK] = &gcc_camera_sf_axi_clk.clkr,
	[GCC_CAMERA_TSCTR_CLK] = &gcc_camera_tsctr_clk.clkr,
	[GCC_CFG_NOC_PCIE_ANOC_AHB_CLK] = &gcc_cfg_noc_pcie_anoc_ahb_clk.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_CNOC_PCIE_SF_AXI_CLK] = &gcc_cnoc_pcie_sf_axi_clk.clkr,
	[GCC_DDRSS_PCIE_SF_QTB_CLK] = &gcc_ddrss_pcie_sf_qtb_clk.clkr,
	[GCC_DISP_0_HF_AXI_CLK] = &gcc_disp_0_hf_axi_clk.clkr,
	[GCC_DISP_SF_AXI_CLK] = &gcc_disp_sf_axi_clk.clkr,
	[GCC_DISP_TSCTR_CLK] = &gcc_disp_tsctr_clk.clkr,
	[GCC_EVA_AXI0_CLK] = &gcc_eva_axi0_clk.clkr,
	[GCC_EVA_AXI0C_CLK] = &gcc_eva_axi0c_clk.clkr,
	[GCC_GP10_CLK] = &gcc_gp10_clk.clkr,
	[GCC_GP10_CLK_SRC] = &gcc_gp10_clk_src.clkr,
	[GCC_GP10_DIV_CLK_SRC] = &gcc_gp10_div_clk_src.clkr,
	[GCC_GP11_CLK] = &gcc_gp11_clk.clkr,
	[GCC_GP11_CLK_SRC] = &gcc_gp11_clk_src.clkr,
	[GCC_GP11_DIV_CLK_SRC] = &gcc_gp11_div_clk_src.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GP4_CLK] = &gcc_gp4_clk.clkr,
	[GCC_GP4_CLK_SRC] = &gcc_gp4_clk_src.clkr,
	[GCC_GP4_DIV_CLK_SRC] = &gcc_gp4_div_clk_src.clkr,
	[GCC_GP5_CLK] = &gcc_gp5_clk.clkr,
	[GCC_GP5_CLK_SRC] = &gcc_gp5_clk_src.clkr,
	[GCC_GP5_DIV_CLK_SRC] = &gcc_gp5_div_clk_src.clkr,
	[GCC_GP6_CLK] = &gcc_gp6_clk.clkr,
	[GCC_GP6_CLK_SRC] = &gcc_gp6_clk_src.clkr,
	[GCC_GP6_DIV_CLK_SRC] = &gcc_gp6_div_clk_src.clkr,
	[GCC_GP7_CLK] = &gcc_gp7_clk.clkr,
	[GCC_GP7_CLK_SRC] = &gcc_gp7_clk_src.clkr,
	[GCC_GP7_DIV_CLK_SRC] = &gcc_gp7_div_clk_src.clkr,
	[GCC_GP8_CLK] = &gcc_gp8_clk.clkr,
	[GCC_GP8_CLK_SRC] = &gcc_gp8_clk_src.clkr,
	[GCC_GP8_DIV_CLK_SRC] = &gcc_gp8_div_clk_src.clkr,
	[GCC_GP9_CLK] = &gcc_gp9_clk.clkr,
	[GCC_GP9_CLK_SRC] = &gcc_gp9_clk_src.clkr,
	[GCC_GP9_DIV_CLK_SRC] = &gcc_gp9_div_clk_src.clkr,
	[GCC_GPLL0] = &gcc_gpll0.clkr,
	[GCC_GPLL0_OUT_EVEN] = &gcc_gpll0_out_even.clkr,
	[GCC_GPLL2] = &gcc_gpll2.clkr,
	[GCC_GPLL4] = &gcc_gpll4.clkr,
	[GCC_GPLL6] = &gcc_gpll6.clkr,
	[GCC_GPLL7] = &gcc_gpll7.clkr,
	[GCC_GPLL8] = &gcc_gpll8.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_GPU_MEMNOC_GFX_CLK] = &gcc_gpu_memnoc_gfx_clk.clkr,
	[GCC_LSR_AXI0_CLK] = &gcc_lsr_axi0_clk.clkr,
	[GCC_LSR_AXI_CV_CPU_CLK] = &gcc_lsr_axi_cv_cpu_clk.clkr,
	[GCC_PCIE_0_AUX_CLK] = &gcc_pcie_0_aux_clk.clkr,
	[GCC_PCIE_0_AUX_CLK_SRC] = &gcc_pcie_0_aux_clk_src.clkr,
	[GCC_PCIE_0_CFG_AHB_CLK] = &gcc_pcie_0_cfg_ahb_clk.clkr,
	[GCC_PCIE_0_MSTR_AXI_CLK] = &gcc_pcie_0_mstr_axi_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK] = &gcc_pcie_0_phy_rchng_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK_SRC] = &gcc_pcie_0_phy_rchng_clk_src.clkr,
	[GCC_PCIE_0_PIPE_CLK] = &gcc_pcie_0_pipe_clk.clkr,
	[GCC_PCIE_0_PIPE_CLK_SRC] = &gcc_pcie_0_pipe_clk_src.clkr,
	[GCC_PCIE_0_PIPE_DIV2_CLK] = &gcc_pcie_0_pipe_div2_clk.clkr,
	[GCC_PCIE_0_PIPE_DIV_CLK_SRC] = &gcc_pcie_0_pipe_div_clk_src.clkr,
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
	[GCC_PCIE_1_PIPE_DIV2_CLK] = &gcc_pcie_1_pipe_div2_clk.clkr,
	[GCC_PCIE_1_PIPE_DIV_CLK_SRC] = &gcc_pcie_1_pipe_div_clk_src.clkr,
	[GCC_PCIE_1_SLV_AXI_CLK] = &gcc_pcie_1_slv_axi_clk.clkr,
	[GCC_PCIE_1_SLV_Q2A_AXI_CLK] = &gcc_pcie_1_slv_q2a_axi_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_PDM_XO4_DIV_CLK_SRC] = &gcc_pdm_xo4_div_clk_src.clkr,
	[GCC_PWM0_XO512_CLK] = &gcc_pwm0_xo512_clk.clkr,
	[GCC_PWM0_XO512_CLK_SRC] = &gcc_pwm0_xo512_clk_src.clkr,
	[GCC_PWM1_XO512_CLK] = &gcc_pwm1_xo512_clk.clkr,
	[GCC_PWM1_XO512_CLK_SRC] = &gcc_pwm1_xo512_clk_src.clkr,
	[GCC_PWM2_XO512_CLK] = &gcc_pwm2_xo512_clk.clkr,
	[GCC_PWM2_XO512_CLK_SRC] = &gcc_pwm2_xo512_clk_src.clkr,
	[GCC_PWM3_XO512_CLK] = &gcc_pwm3_xo512_clk.clkr,
	[GCC_PWM3_XO512_CLK_SRC] = &gcc_pwm3_xo512_clk_src.clkr,
	[GCC_PWM4_XO512_CLK] = &gcc_pwm4_xo512_clk.clkr,
	[GCC_PWM4_XO512_CLK_SRC] = &gcc_pwm4_xo512_clk_src.clkr,
	[GCC_PWM5_XO512_CLK] = &gcc_pwm5_xo512_clk.clkr,
	[GCC_PWM5_XO512_CLK_SRC] = &gcc_pwm5_xo512_clk_src.clkr,
	[GCC_PWM6_XO512_CLK] = &gcc_pwm6_xo512_clk.clkr,
	[GCC_PWM6_XO512_CLK_SRC] = &gcc_pwm6_xo512_clk_src.clkr,
	[GCC_PWM7_XO512_CLK] = &gcc_pwm7_xo512_clk.clkr,
	[GCC_PWM7_XO512_CLK_SRC] = &gcc_pwm7_xo512_clk_src.clkr,
	[GCC_PWM8_XO512_CLK] = &gcc_pwm8_xo512_clk.clkr,
	[GCC_PWM8_XO512_CLK_SRC] = &gcc_pwm8_xo512_clk_src.clkr,
	[GCC_PWM9_XO512_CLK] = &gcc_pwm9_xo512_clk.clkr,
	[GCC_PWM9_XO512_CLK_SRC] = &gcc_pwm9_xo512_clk_src.clkr,
	[GCC_QMIP_CAMERA_ICP_AHB_CLK] = &gcc_qmip_camera_icp_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_NRT_AHB_CLK] = &gcc_qmip_camera_nrt_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_RT_AHB_CLK] = &gcc_qmip_camera_rt_ahb_clk.clkr,
	[GCC_QMIP_DISP_AHB_CLK] = &gcc_qmip_disp_ahb_clk.clkr,
	[GCC_QMIP_GPU_AHB_CLK] = &gcc_qmip_gpu_ahb_clk.clkr,
	[GCC_QMIP_PCIE_AHB_CLK] = &gcc_qmip_pcie_ahb_clk.clkr,
	[GCC_QMIP_VENUS_LSR0_AHB_CLK] = &gcc_qmip_venus_lsr0_ahb_clk.clkr,
	[GCC_QMIP_VENUS_LSR1_AHB_CLK] = &gcc_qmip_venus_lsr1_ahb_clk.clkr,
	[GCC_QMIP_VENUS_LSR_CV_CPU_AHB_CLK] = &gcc_qmip_venus_lsr_cv_cpu_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CV_CPU_AHB_CLK] = &gcc_qmip_video_cv_cpu_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CVP_AHB_CLK] = &gcc_qmip_video_cvp_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_V_CPU_AHB_CLK] = &gcc_qmip_video_v_cpu_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_VCODEC_AHB_CLK] = &gcc_qmip_video_vcodec_ahb_clk.clkr,
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
	[GCC_QUPV3_WRAP2_CORE_2X_CLK] = &gcc_qupv3_wrap2_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP2_CORE_CLK] = &gcc_qupv3_wrap2_core_clk.clkr,
	[GCC_QUPV3_WRAP2_S0_CLK] = &gcc_qupv3_wrap2_s0_clk.clkr,
	[GCC_QUPV3_WRAP2_S0_CLK_SRC] = &gcc_qupv3_wrap2_s0_clk_src.clkr,
	[GCC_QUPV3_WRAP2_S1_CLK] = &gcc_qupv3_wrap2_s1_clk.clkr,
	[GCC_QUPV3_WRAP2_S1_CLK_SRC] = &gcc_qupv3_wrap2_s1_clk_src.clkr,
	[GCC_QUPV3_WRAP2_S2_CLK] = &gcc_qupv3_wrap2_s2_clk.clkr,
	[GCC_QUPV3_WRAP2_S2_CLK_SRC] = &gcc_qupv3_wrap2_s2_clk_src.clkr,
	[GCC_QUPV3_WRAP2_S3_CLK] = &gcc_qupv3_wrap2_s3_clk.clkr,
	[GCC_QUPV3_WRAP2_S3_CLK_SRC] = &gcc_qupv3_wrap2_s3_clk_src.clkr,
	[GCC_QUPV3_WRAP2_S4_CLK] = &gcc_qupv3_wrap2_s4_clk.clkr,
	[GCC_QUPV3_WRAP2_S4_CLK_SRC] = &gcc_qupv3_wrap2_s4_clk_src.clkr,
	[GCC_QUPV3_WRAP2_S5_CLK] = &gcc_qupv3_wrap2_s5_clk.clkr,
	[GCC_QUPV3_WRAP2_S5_CLK_SRC] = &gcc_qupv3_wrap2_s5_clk_src.clkr,
	[GCC_QUPV3_WRAP_1_M_AHB_CLK] = &gcc_qupv3_wrap_1_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_S_AHB_CLK] = &gcc_qupv3_wrap_1_s_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_2_M_AHB_CLK] = &gcc_qupv3_wrap_2_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_2_S_AHB_CLK] = &gcc_qupv3_wrap_2_s_ahb_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_APPS_CLK_SRC] = &gcc_sdcc1_apps_clk_src.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK_SRC] = &gcc_sdcc1_ice_core_clk_src.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_SDCC2_APPS_CLK_SRC] = &gcc_sdcc2_apps_clk_src.clkr,
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

static const struct qcom_reset_map gcc_seraph_resets[] = {
	[GCC_CAMERA_BCR] = { 0x26000 },
	[GCC_DISPLAY_0_BCR] = { 0x27000 },
	[GCC_EVA_AXI0_CLK_ARES] = { 0xb2008, 2 },
	[GCC_EVA_AXI0C_CLK_ARES] = { 0xb201c, 2 },
	[GCC_EVA_BCR] = { 0xb2000 },
	[GCC_GPU_BCR] = { 0x71000 },
	[GCC_LSR_AXI0_CLK_ARES] = { 0xb3008, 2 },
	[GCC_LSR_AXI_CV_CPU_CLK_ARES] = { 0xb301c, 2 },
	[GCC_LSR_BCR] = { 0xb3000 },
	[GCC_PCIE_0_BCR] = { 0x6b000 },
	[GCC_PCIE_0_LINK_DOWN_BCR] = { 0x6c014 },
	[GCC_PCIE_0_NOCSR_COM_PHY_BCR] = { 0x6c020 },
	[GCC_PCIE_0_PHY_BCR] = { 0x6c01c },
	[GCC_PCIE_0_PHY_NOCSR_COM_PHY_BCR] = { 0x6c028 },
	[GCC_PCIE_1_BCR] = { 0x8d000 },
	[GCC_PCIE_1_LINK_DOWN_BCR] = { 0x8e014 },
	[GCC_PCIE_1_NOCSR_COM_PHY_BCR] = { 0x8e020 },
	[GCC_PCIE_1_PHY_BCR] = { 0x8e01c },
	[GCC_PCIE_1_PHY_NOCSR_COM_PHY_BCR] = { 0x8e024 },
	[GCC_PCIE_RSCC_BCR] = { 0x11000 },
	[GCC_PDM_BCR] = { 0x33000 },
	[GCC_QUPV3_WRAPPER_1_BCR] = { 0x18000 },
	[GCC_QUPV3_WRAPPER_2_BCR] = { 0x1e000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x12000 },
	[GCC_SDCC1_BCR] = { 0xa3000 },
	[GCC_SDCC2_BCR] = { 0x14000 },
	[GCC_TCSR_PCIE_BCR] = { 0x8e908 },
	[GCC_USB30_PRIM_BCR] = { 0x39000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x50008 },
	[GCC_USB3_PHY_PRIM_BCR] = { 0x50000 },
	[GCC_USB3PHY_PHY_PRIM_BCR] = { 0x50004 },
	[GCC_VIDEO_AXI0_CLK_ARES] = { 0x32018, 2 },
	[GCC_VIDEO_AXI1_CLK_ARES] = { 0x3202c, 2 },
	[GCC_VIDEO_BCR] = { 0x32000 },
	[GCC_VIDEO_XO_CLK_ARES] = { 0x32040, 2 },
};

static const struct clk_rcg_dfs_data gcc_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s5_clk_src),
};

static const struct regmap_config gcc_seraph_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1f41f0,
	.fast_io = true,
};

static const struct qcom_cc_desc gcc_seraph_desc = {
	.config = &gcc_seraph_regmap_config,
	.clks = gcc_seraph_clocks,
	.num_clks = ARRAY_SIZE(gcc_seraph_clocks),
	.resets = gcc_seraph_resets,
	.num_resets = ARRAY_SIZE(gcc_seraph_resets),
	.clk_regulators = gcc_seraph_regulators,
	.num_clk_regulators = ARRAY_SIZE(gcc_seraph_regulators),
};

static const struct of_device_id gcc_seraph_match_table[] = {
	{ .compatible = "qcom,seraph-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_seraph_match_table);

static int gcc_seraph_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gcc_seraph_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks,
				       ARRAY_SIZE(gcc_dfs_clocks));
	if (ret)
		return ret;

	/*
	 * Keep clocks always enabled:
	 *	gcc_camera_ahb_clk
	 *	gcc_camera_xo_clk
	 *	gcc_disp_0_ahb_clk
	 *	gcc_disp_0_xo_clk
	 *	gcc_eva_ahb_clk
	 *	gcc_eva_xo_clk
	 *	gcc_gpu_cfg_ahb_clk
	 *	gcc_lsr_ahb_clk
	 *	gcc_lsr_xo_clk
	 *	gcc_pcie_rscc_cfg_ahb_clk
	 *	gcc_pcie_rscc_xo_clk
	 *	gcc_video_ahb_clk
	 *	gcc_video_xo_clk
	 */
	regmap_update_bits(regmap, 0x26004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x26020, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x27004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x27020, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0xb2004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0xb2024, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x71004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0xb3004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0xb3024, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x52010, BIT(20), BIT(20));
	regmap_update_bits(regmap, 0x52010, BIT(21), BIT(21));
	regmap_update_bits(regmap, 0x32004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x32040, BIT(0), BIT(0));

	/* Clear GDSC_SLEEP_ENA_VOTE to stop votes being auto-removed in sleep. */
	regmap_write(regmap, 0x52150, 0x0);

	ret = qcom_cc_really_probe(pdev, &gcc_seraph_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GCC clocks ret=%d\n", ret);
		return ret;
	}

	dev_info(&pdev->dev, "Registered GCC clocks\n");

	return ret;
}

static void gcc_seraph_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &gcc_seraph_desc);
}

static struct platform_driver gcc_seraph_driver = {
	.probe = gcc_seraph_probe,
	.driver = {
		.name = "gcc-seraph",
		.of_match_table = gcc_seraph_match_table,
		.sync_state = gcc_seraph_sync_state,
	},
};

static int __init gcc_seraph_init(void)
{
	return platform_driver_register(&gcc_seraph_driver);
}
subsys_initcall(gcc_seraph_init);

static void __exit gcc_seraph_exit(void)
{
	platform_driver_unregister(&gcc_seraph_driver);
}
module_exit(gcc_seraph_exit);

MODULE_DESCRIPTION("QTI GCC SERAPH Driver");
MODULE_LICENSE("GPL");
