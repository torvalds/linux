// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,glymur-gcc.h>

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
	DT_BI_TCXO_AO,
	DT_SLEEP_CLK,
	DT_GCC_USB4_0_PHY_DP0_GMUX_CLK_SRC,
	DT_GCC_USB4_0_PHY_DP1_GMUX_CLK_SRC,
	DT_GCC_USB4_0_PHY_PCIE_PIPEGMUX_CLK_SRC,
	DT_GCC_USB4_0_PHY_PIPEGMUX_CLK_SRC,
	DT_GCC_USB4_0_PHY_SYS_PIPEGMUX_CLK_SRC,
	DT_GCC_USB4_1_PHY_DP0_GMUX_2_CLK_SRC,
	DT_GCC_USB4_1_PHY_DP1_GMUX_2_CLK_SRC,
	DT_GCC_USB4_1_PHY_PCIE_PIPEGMUX_CLK_SRC,
	DT_GCC_USB4_1_PHY_PIPEGMUX_CLK_SRC,
	DT_GCC_USB4_1_PHY_SYS_PIPEGMUX_CLK_SRC,
	DT_GCC_USB4_2_PHY_DP0_GMUX_CLK_SRC,
	DT_GCC_USB4_2_PHY_DP1_GMUX_CLK_SRC,
	DT_GCC_USB4_2_PHY_PCIE_PIPEGMUX_CLK_SRC,
	DT_GCC_USB4_2_PHY_PIPEGMUX_CLK_SRC,
	DT_GCC_USB4_2_PHY_SYS_PIPEGMUX_CLK_SRC,
	DT_PCIE_3A_PIPE_CLK,
	DT_PCIE_3B_PIPE_CLK,
	DT_PCIE_4_PIPE_CLK,
	DT_PCIE_5_PIPE_CLK,
	DT_PCIE_6_PIPE_CLK,
	DT_QUSB4PHY_0_GCC_USB4_RX0_CLK,
	DT_QUSB4PHY_0_GCC_USB4_RX1_CLK,
	DT_QUSB4PHY_1_GCC_USB4_RX0_CLK,
	DT_QUSB4PHY_1_GCC_USB4_RX1_CLK,
	DT_QUSB4PHY_2_GCC_USB4_RX0_CLK,
	DT_QUSB4PHY_2_GCC_USB4_RX1_CLK,
	DT_UFS_PHY_RX_SYMBOL_0_CLK,
	DT_UFS_PHY_RX_SYMBOL_1_CLK,
	DT_UFS_PHY_TX_SYMBOL_0_CLK,
	DT_USB3_PHY_0_WRAPPER_GCC_USB30_PIPE_CLK,
	DT_USB3_PHY_1_WRAPPER_GCC_USB30_PIPE_CLK,
	DT_USB3_PHY_2_WRAPPER_GCC_USB30_PIPE_CLK,
	DT_USB3_UNI_PHY_MP_GCC_USB30_PIPE_0_CLK,
	DT_USB3_UNI_PHY_MP_GCC_USB30_PIPE_1_CLK,
	DT_USB4_0_PHY_GCC_USB4_PCIE_PIPE_CLK,
	DT_USB4_0_PHY_GCC_USB4RTR_MAX_PIPE_CLK,
	DT_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK,
	DT_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK,
	DT_USB4_2_PHY_GCC_USB4_PCIE_PIPE_CLK,
	DT_USB4_2_PHY_GCC_USB4RTR_MAX_PIPE_CLK,
};

enum {
	P_BI_TCXO,
	P_GCC_GPLL0_OUT_EVEN,
	P_GCC_GPLL0_OUT_MAIN,
	P_GCC_GPLL14_OUT_EVEN,
	P_GCC_GPLL14_OUT_MAIN,
	P_GCC_GPLL1_OUT_MAIN,
	P_GCC_GPLL4_OUT_MAIN,
	P_GCC_GPLL5_OUT_MAIN,
	P_GCC_GPLL7_OUT_MAIN,
	P_GCC_GPLL8_OUT_MAIN,
	P_GCC_GPLL9_OUT_MAIN,
	P_GCC_USB3_PRIM_PHY_PIPE_CLK_SRC,
	P_GCC_USB3_SEC_PHY_PIPE_CLK_SRC,
	P_GCC_USB3_TERT_PHY_PIPE_CLK_SRC,
	P_GCC_USB4_0_PHY_DP0_GMUX_CLK_SRC,
	P_GCC_USB4_0_PHY_DP1_GMUX_CLK_SRC,
	P_GCC_USB4_0_PHY_PCIE_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_0_PHY_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_0_PHY_SYS_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_1_PHY_DP0_GMUX_2_CLK_SRC,
	P_GCC_USB4_1_PHY_DP1_GMUX_2_CLK_SRC,
	P_GCC_USB4_1_PHY_PCIE_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_1_PHY_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_1_PHY_PLL_PIPE_CLK_SRC,
	P_GCC_USB4_1_PHY_SYS_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_2_PHY_DP0_GMUX_CLK_SRC,
	P_GCC_USB4_2_PHY_DP1_GMUX_CLK_SRC,
	P_GCC_USB4_2_PHY_PCIE_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_2_PHY_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_2_PHY_SYS_PIPEGMUX_CLK_SRC,
	P_PCIE_3A_PIPE_CLK,
	P_PCIE_3B_PIPE_CLK,
	P_PCIE_4_PIPE_CLK,
	P_PCIE_5_PIPE_CLK,
	P_PCIE_6_PIPE_CLK,
	P_QUSB4PHY_0_GCC_USB4_RX0_CLK,
	P_QUSB4PHY_0_GCC_USB4_RX1_CLK,
	P_QUSB4PHY_1_GCC_USB4_RX0_CLK,
	P_QUSB4PHY_1_GCC_USB4_RX1_CLK,
	P_QUSB4PHY_2_GCC_USB4_RX0_CLK,
	P_QUSB4PHY_2_GCC_USB4_RX1_CLK,
	P_SLEEP_CLK,
	P_UFS_PHY_RX_SYMBOL_0_CLK,
	P_UFS_PHY_RX_SYMBOL_1_CLK,
	P_UFS_PHY_TX_SYMBOL_0_CLK,
	P_USB3_PHY_0_WRAPPER_GCC_USB30_PIPE_CLK,
	P_USB3_PHY_1_WRAPPER_GCC_USB30_PIPE_CLK,
	P_USB3_PHY_2_WRAPPER_GCC_USB30_PIPE_CLK,
	P_USB3_UNI_PHY_MP_GCC_USB30_PIPE_0_CLK,
	P_USB3_UNI_PHY_MP_GCC_USB30_PIPE_1_CLK,
	P_USB4_0_PHY_GCC_USB4_PCIE_PIPE_CLK,
	P_USB4_0_PHY_GCC_USB4RTR_MAX_PIPE_CLK,
	P_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK,
	P_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK,
	P_USB4_2_PHY_GCC_USB4_PCIE_PIPE_CLK,
	P_USB4_2_PHY_GCC_USB4RTR_MAX_PIPE_CLK,
};

static struct clk_alpha_pll gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x62040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
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

static struct clk_alpha_pll gcc_gpll1 = {
	.offset = 0x1000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x62040,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll1",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll14 = {
	.offset = 0xe000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x62040,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll14",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gcc_gpll14_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gcc_gpll14_out_even = {
	.offset = 0xe000,
	.post_div_shift = 10,
	.post_div_table = post_div_table_gcc_gpll14_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gcc_gpll14_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gpll14_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_gpll14.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_taycan_eko_t_ops,
	},
};

static struct clk_alpha_pll gcc_gpll4 = {
	.offset = 0x4000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x62040,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll4",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll5 = {
	.offset = 0x5000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x62040,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll5",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll7 = {
	.offset = 0x7000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x62040,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll7",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll8 = {
	.offset = 0x8000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x62040,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll8",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll9 = {
	.offset = 0x9000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T],
	.clkr = {
		.enable_reg = 0x62040,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll9",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_taycan_eko_t_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_prim_phy_pipe_clk_src;
static struct clk_regmap_mux gcc_usb3_sec_phy_pipe_clk_src;
static struct clk_regmap_mux gcc_usb3_tert_phy_pipe_clk_src;

static struct clk_rcg2 gcc_usb4_1_phy_pll_pipe_clk_src;

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
	{ P_GCC_GPLL1_OUT_MAIN, 4 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll1.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL1_OUT_MAIN, 4 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll1.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL14_OUT_MAIN, 1 },
	{ P_GCC_GPLL14_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll14.clkr.hw },
	{ .hw = &gcc_gpll14_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll4.clkr.hw },
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL8_OUT_MAIN, 2 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_9[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll8.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
};

static const struct clk_parent_data gcc_parent_data_10[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll7.clkr.hw },
};

static const struct parent_map gcc_parent_map_11[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
	{ P_GCC_GPLL8_OUT_MAIN, 3 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_11[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .hw = &gcc_gpll8.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_17[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL9_OUT_MAIN, 2 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_17[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll9.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_18[] = {
	{ P_UFS_PHY_RX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_18[] = {
	{ .index = DT_UFS_PHY_RX_SYMBOL_0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_19[] = {
	{ P_UFS_PHY_RX_SYMBOL_1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_19[] = {
	{ .index = DT_UFS_PHY_RX_SYMBOL_1_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_20[] = {
	{ P_UFS_PHY_TX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_20[] = {
	{ .index = DT_UFS_PHY_TX_SYMBOL_0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_21[] = {
	{ P_GCC_USB3_PRIM_PHY_PIPE_CLK_SRC, 0 },
	{ P_USB4_0_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 1 },
	{ P_GCC_USB4_0_PHY_PIPEGMUX_CLK_SRC, 3 },
};

static const struct clk_parent_data gcc_parent_data_21[] = {
	{ .hw = &gcc_usb3_prim_phy_pipe_clk_src.clkr.hw },
	{ .index = DT_USB4_0_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
	{ .index = DT_GCC_USB4_0_PHY_PIPEGMUX_CLK_SRC },
};

static const struct parent_map gcc_parent_map_22[] = {
	{ P_GCC_USB3_SEC_PHY_PIPE_CLK_SRC, 0 },
	{ P_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 1 },
	{ P_GCC_USB4_1_PHY_PLL_PIPE_CLK_SRC, 2 },
	{ P_GCC_USB4_1_PHY_PIPEGMUX_CLK_SRC, 3 },
};

static const struct clk_parent_data gcc_parent_data_22[] = {
	{ .hw = &gcc_usb3_sec_phy_pipe_clk_src.clkr.hw },
	{ .index = DT_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
	{ .hw = &gcc_usb4_1_phy_pll_pipe_clk_src.clkr.hw },
	{ .index = DT_GCC_USB4_1_PHY_PIPEGMUX_CLK_SRC },
};

static const struct parent_map gcc_parent_map_23[] = {
	{ P_GCC_USB3_TERT_PHY_PIPE_CLK_SRC, 0 },
	{ P_USB4_2_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 1 },
	{ P_GCC_USB4_2_PHY_PIPEGMUX_CLK_SRC, 3 },
};

static const struct clk_parent_data gcc_parent_data_23[] = {
	{ .hw = &gcc_usb3_tert_phy_pipe_clk_src.clkr.hw },
	{ .index = DT_USB4_2_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
	{ .index = DT_GCC_USB4_2_PHY_PIPEGMUX_CLK_SRC },
};

static const struct parent_map gcc_parent_map_24[] = {
	{ P_USB3_UNI_PHY_MP_GCC_USB30_PIPE_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_24[] = {
	{ .index = DT_USB3_UNI_PHY_MP_GCC_USB30_PIPE_0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_25[] = {
	{ P_USB3_UNI_PHY_MP_GCC_USB30_PIPE_1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_25[] = {
	{ .index = DT_USB3_UNI_PHY_MP_GCC_USB30_PIPE_1_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_26[] = {
	{ P_USB3_PHY_0_WRAPPER_GCC_USB30_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_26[] = {
	{ .index = DT_USB3_PHY_0_WRAPPER_GCC_USB30_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_27[] = {
	{ P_USB3_PHY_1_WRAPPER_GCC_USB30_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_27[] = {
	{ .index = DT_USB3_PHY_1_WRAPPER_GCC_USB30_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_28[] = {
	{ P_USB3_PHY_2_WRAPPER_GCC_USB30_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_28[] = {
	{ .index = DT_USB3_PHY_2_WRAPPER_GCC_USB30_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_29[] = {
	{ P_GCC_USB4_0_PHY_DP0_GMUX_CLK_SRC, 0 },
	{ P_USB4_0_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_29[] = {
	{ .index = DT_GCC_USB4_0_PHY_DP0_GMUX_CLK_SRC },
	{ .index = DT_USB4_0_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_30[] = {
	{ P_GCC_USB4_0_PHY_DP1_GMUX_CLK_SRC, 0 },
	{ P_USB4_0_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_30[] = {
	{ .index = DT_GCC_USB4_0_PHY_DP1_GMUX_CLK_SRC },
	{ .index = DT_USB4_0_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_31[] = {
	{ P_USB4_0_PHY_GCC_USB4_PCIE_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_31[] = {
	{ .index = DT_USB4_0_PHY_GCC_USB4_PCIE_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_32[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_32[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_33[] = {
	{ P_GCC_USB4_0_PHY_PCIE_PIPEGMUX_CLK_SRC, 0 },
	{ P_USB4_0_PHY_GCC_USB4_PCIE_PIPE_CLK, 1 },
};

static const struct clk_parent_data gcc_parent_data_33[] = {
	{ .index = DT_GCC_USB4_0_PHY_PCIE_PIPEGMUX_CLK_SRC },
	{ .index = DT_USB4_0_PHY_GCC_USB4_PCIE_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_34[] = {
	{ P_QUSB4PHY_0_GCC_USB4_RX0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_34[] = {
	{ .index = DT_QUSB4PHY_0_GCC_USB4_RX0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_35[] = {
	{ P_QUSB4PHY_0_GCC_USB4_RX1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_35[] = {
	{ .index = DT_QUSB4PHY_0_GCC_USB4_RX1_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_36[] = {
	{ P_GCC_USB4_0_PHY_SYS_PIPEGMUX_CLK_SRC, 0 },
	{ P_USB4_0_PHY_GCC_USB4_PCIE_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_36[] = {
	{ .index = DT_GCC_USB4_0_PHY_SYS_PIPEGMUX_CLK_SRC },
	{ .index = DT_USB4_0_PHY_GCC_USB4_PCIE_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_37[] = {
	{ P_GCC_USB4_1_PHY_DP0_GMUX_2_CLK_SRC, 0 },
	{ P_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_37[] = {
	{ .index = DT_GCC_USB4_1_PHY_DP0_GMUX_2_CLK_SRC },
	{ .index = DT_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_38[] = {
	{ P_GCC_USB4_1_PHY_DP1_GMUX_2_CLK_SRC, 0 },
	{ P_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_38[] = {
	{ .index = DT_GCC_USB4_1_PHY_DP1_GMUX_2_CLK_SRC },
	{ .index = DT_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_39[] = {
	{ P_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_39[] = {
	{ .index = DT_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_40[] = {
	{ P_GCC_USB4_1_PHY_PCIE_PIPEGMUX_CLK_SRC, 0 },
	{ P_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK, 1 },
};

static const struct clk_parent_data gcc_parent_data_40[] = {
	{ .index = DT_GCC_USB4_1_PHY_PCIE_PIPEGMUX_CLK_SRC },
	{ .index = DT_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_41[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL5_OUT_MAIN, 3 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_41[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll5.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_42[] = {
	{ P_QUSB4PHY_1_GCC_USB4_RX0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_42[] = {
	{ .index = DT_QUSB4PHY_1_GCC_USB4_RX0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_43[] = {
	{ P_QUSB4PHY_1_GCC_USB4_RX1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_43[] = {
	{ .index = DT_QUSB4PHY_1_GCC_USB4_RX1_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_44[] = {
	{ P_GCC_USB4_1_PHY_SYS_PIPEGMUX_CLK_SRC, 0 },
	{ P_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_44[] = {
	{ .index = DT_GCC_USB4_1_PHY_SYS_PIPEGMUX_CLK_SRC },
	{ .index = DT_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_45[] = {
	{ P_GCC_USB4_2_PHY_DP0_GMUX_CLK_SRC, 0 },
	{ P_USB4_2_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_45[] = {
	{ .index = DT_GCC_USB4_2_PHY_DP0_GMUX_CLK_SRC },
	{ .index = DT_USB4_2_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_46[] = {
	{ P_GCC_USB4_2_PHY_DP1_GMUX_CLK_SRC, 0 },
	{ P_USB4_2_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_46[] = {
	{ .index = DT_GCC_USB4_2_PHY_DP1_GMUX_CLK_SRC },
	{ .index = DT_USB4_2_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_47[] = {
	{ P_USB4_2_PHY_GCC_USB4_PCIE_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_47[] = {
	{ .index = DT_USB4_2_PHY_GCC_USB4_PCIE_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_48[] = {
	{ P_GCC_USB4_2_PHY_PCIE_PIPEGMUX_CLK_SRC, 0 },
	{ P_USB4_2_PHY_GCC_USB4_PCIE_PIPE_CLK, 1 },
};

static const struct clk_parent_data gcc_parent_data_48[] = {
	{ .index = DT_GCC_USB4_2_PHY_PCIE_PIPEGMUX_CLK_SRC },
	{ .index = DT_USB4_2_PHY_GCC_USB4_PCIE_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_49[] = {
	{ P_QUSB4PHY_2_GCC_USB4_RX0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_49[] = {
	{ .index = DT_QUSB4PHY_2_GCC_USB4_RX0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_50[] = {
	{ P_QUSB4PHY_2_GCC_USB4_RX1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_50[] = {
	{ .index = DT_QUSB4PHY_2_GCC_USB4_RX1_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_51[] = {
	{ P_GCC_USB4_2_PHY_SYS_PIPEGMUX_CLK_SRC, 0 },
	{ P_USB4_2_PHY_GCC_USB4_PCIE_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_51[] = {
	{ .index = DT_GCC_USB4_2_PHY_SYS_PIPEGMUX_CLK_SRC },
	{ .index = DT_USB4_2_PHY_GCC_USB4_PCIE_PIPE_CLK },
};

static struct clk_regmap_phy_mux gcc_pcie_3a_pipe_clk_src = {
	.reg = 0xdc088,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_PCIE_3A_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_3b_pipe_clk_src = {
	.reg = 0x941b4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_PCIE_3B_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_4_pipe_clk_src = {
	.reg = 0x881a4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_PCIE_4_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_5_pipe_clk_src = {
	.reg = 0xc309c,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_5_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_PCIE_5_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_6_pipe_clk_src = {
	.reg = 0x8a1a4,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_6_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_PCIE_6_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_rx_symbol_0_clk_src = {
	.reg = 0x7706c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_18,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_18,
			.num_parents = ARRAY_SIZE(gcc_parent_data_18),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_rx_symbol_1_clk_src = {
	.reg = 0x770f0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_19,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_1_clk_src",
			.parent_data = gcc_parent_data_19,
			.num_parents = ARRAY_SIZE(gcc_parent_data_19),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_tx_symbol_0_clk_src = {
	.reg = 0x7705c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_20,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_tx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_20,
			.num_parents = ARRAY_SIZE(gcc_parent_data_20),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb34_prim_phy_pipe_clk_src = {
	.reg = 0x2b0b8,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_21,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb34_prim_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_21,
			.num_parents = ARRAY_SIZE(gcc_parent_data_21),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb34_sec_phy_pipe_clk_src = {
	.reg = 0x2d0c4,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_22,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb34_sec_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_22,
			.num_parents = ARRAY_SIZE(gcc_parent_data_22),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb34_tert_phy_pipe_clk_src = {
	.reg = 0xe00bc,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_23,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb34_tert_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_23,
			.num_parents = ARRAY_SIZE(gcc_parent_data_23),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_mp_phy_pipe_0_clk_src = {
	.reg = 0x9a07c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_24,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp_phy_pipe_0_clk_src",
			.parent_data = gcc_parent_data_24,
			.num_parents = ARRAY_SIZE(gcc_parent_data_24),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_mp_phy_pipe_1_clk_src = {
	.reg = 0x9a084,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_25,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp_phy_pipe_1_clk_src",
			.parent_data = gcc_parent_data_25,
			.num_parents = ARRAY_SIZE(gcc_parent_data_25),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_prim_phy_pipe_clk_src = {
	.reg = 0x3f08c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_26,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_26,
			.num_parents = ARRAY_SIZE(gcc_parent_data_26),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_sec_phy_pipe_clk_src = {
	.reg = 0xe207c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_27,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_sec_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_27,
			.num_parents = ARRAY_SIZE(gcc_parent_data_27),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_tert_phy_pipe_clk_src = {
	.reg = 0xe107c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_28,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_tert_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_28,
			.num_parents = ARRAY_SIZE(gcc_parent_data_28),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_0_phy_dp0_clk_src = {
	.reg = 0x2b080,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_29,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_dp0_clk_src",
			.parent_data = gcc_parent_data_29,
			.num_parents = ARRAY_SIZE(gcc_parent_data_29),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_0_phy_dp1_clk_src = {
	.reg = 0x2b134,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_30,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_dp1_clk_src",
			.parent_data = gcc_parent_data_30,
			.num_parents = ARRAY_SIZE(gcc_parent_data_30),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_0_phy_p2rr2p_pipe_clk_src = {
	.reg = 0x2b0f0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_31,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_p2rr2p_pipe_clk_src",
			.parent_data = gcc_parent_data_31,
			.num_parents = ARRAY_SIZE(gcc_parent_data_31),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_0_phy_pcie_pipe_mux_clk_src = {
	.reg = 0x2b120,
	.shift = 0,
	.width = 1,
	.parent_map = gcc_parent_map_33,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_pcie_pipe_mux_clk_src",
			.parent_data = gcc_parent_data_33,
			.num_parents = ARRAY_SIZE(gcc_parent_data_33),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_0_phy_rx0_clk_src = {
	.reg = 0x2b0c0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_34,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_rx0_clk_src",
			.parent_data = gcc_parent_data_34,
			.num_parents = ARRAY_SIZE(gcc_parent_data_34),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_0_phy_rx1_clk_src = {
	.reg = 0x2b0d4,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_35,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_rx1_clk_src",
			.parent_data = gcc_parent_data_35,
			.num_parents = ARRAY_SIZE(gcc_parent_data_35),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_0_phy_sys_clk_src = {
	.reg = 0x2b100,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_36,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_sys_clk_src",
			.parent_data = gcc_parent_data_36,
			.num_parents = ARRAY_SIZE(gcc_parent_data_36),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_dp0_clk_src = {
	.reg = 0x2d08c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_37,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_dp0_clk_src",
			.parent_data = gcc_parent_data_37,
			.num_parents = ARRAY_SIZE(gcc_parent_data_37),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_dp1_clk_src = {
	.reg = 0x2d154,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_38,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_dp1_clk_src",
			.parent_data = gcc_parent_data_38,
			.num_parents = ARRAY_SIZE(gcc_parent_data_38),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_p2rr2p_pipe_clk_src = {
	.reg = 0x2d114,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_39,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_p2rr2p_pipe_clk_src",
			.parent_data = gcc_parent_data_39,
			.num_parents = ARRAY_SIZE(gcc_parent_data_39),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_pcie_pipe_mux_clk_src = {
	.reg = 0x2d140,
	.shift = 0,
	.width = 1,
	.parent_map = gcc_parent_map_40,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_pcie_pipe_mux_clk_src",
			.parent_data = gcc_parent_data_40,
			.num_parents = ARRAY_SIZE(gcc_parent_data_40),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_rx0_clk_src = {
	.reg = 0x2d0e4,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_42,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_rx0_clk_src",
			.parent_data = gcc_parent_data_42,
			.num_parents = ARRAY_SIZE(gcc_parent_data_42),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_rx1_clk_src = {
	.reg = 0x2d0f8,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_43,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_rx1_clk_src",
			.parent_data = gcc_parent_data_43,
			.num_parents = ARRAY_SIZE(gcc_parent_data_43),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_sys_clk_src = {
	.reg = 0x2d124,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_44,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_sys_clk_src",
			.parent_data = gcc_parent_data_44,
			.num_parents = ARRAY_SIZE(gcc_parent_data_44),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_2_phy_dp0_clk_src = {
	.reg = 0xe0084,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_45,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_dp0_clk_src",
			.parent_data = gcc_parent_data_45,
			.num_parents = ARRAY_SIZE(gcc_parent_data_45),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_2_phy_dp1_clk_src = {
	.reg = 0xe013c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_46,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_dp1_clk_src",
			.parent_data = gcc_parent_data_46,
			.num_parents = ARRAY_SIZE(gcc_parent_data_46),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_2_phy_p2rr2p_pipe_clk_src = {
	.reg = 0xe00f4,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_47,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_p2rr2p_pipe_clk_src",
			.parent_data = gcc_parent_data_47,
			.num_parents = ARRAY_SIZE(gcc_parent_data_47),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_2_phy_pcie_pipe_mux_clk_src = {
	.reg = 0xe0124,
	.shift = 0,
	.width = 1,
	.parent_map = gcc_parent_map_48,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_pcie_pipe_mux_clk_src",
			.parent_data = gcc_parent_data_48,
			.num_parents = ARRAY_SIZE(gcc_parent_data_48),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_2_phy_rx0_clk_src = {
	.reg = 0xe00c4,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_49,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_rx0_clk_src",
			.parent_data = gcc_parent_data_49,
			.num_parents = ARRAY_SIZE(gcc_parent_data_49),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_2_phy_rx1_clk_src = {
	.reg = 0xe00d8,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_50,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_rx1_clk_src",
			.parent_data = gcc_parent_data_50,
			.num_parents = ARRAY_SIZE(gcc_parent_data_50),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_2_phy_sys_clk_src = {
	.reg = 0xe0104,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_51,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_sys_clk_src",
			.parent_data = gcc_parent_data_51,
			.num_parents = ARRAY_SIZE(gcc_parent_data_51),
			.ops = &clk_regmap_mux_closest_ops,
		},
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
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x92004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x93004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_aux_clk_src = {
	.cmd_rcgr = 0xc8168,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_phy_rchng_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_phy_rchng_clk_src = {
	.cmd_rcgr = 0xc803c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_aux_clk_src = {
	.cmd_rcgr = 0x2e168,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_phy_rchng_clk_src = {
	.cmd_rcgr = 0x2e03c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_2_aux_clk_src = {
	.cmd_rcgr = 0xc0168,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_2_phy_rchng_clk_src = {
	.cmd_rcgr = 0xc003c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_3a_aux_clk_src = {
	.cmd_rcgr = 0xdc08c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_3a_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_3a_phy_rchng_clk_src = {
	.cmd_rcgr = 0xdc070,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_3a_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_3b_aux_clk_src = {
	.cmd_rcgr = 0x941b8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_3b_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_3b_phy_rchng_clk_src = {
	.cmd_rcgr = 0x94088,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_3b_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_4_aux_clk_src = {
	.cmd_rcgr = 0x881a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_4_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_4_phy_rchng_clk_src = {
	.cmd_rcgr = 0x88078,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_4_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_5_aux_clk_src = {
	.cmd_rcgr = 0xc30a0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_5_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_5_phy_rchng_clk_src = {
	.cmd_rcgr = 0xc3084,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_5_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_6_aux_clk_src = {
	.cmd_rcgr = 0x8a1a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_6_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_6_phy_rchng_clk_src = {
	.cmd_rcgr = 0x8a078,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_6_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_phy_3a_aux_clk_src = {
	.cmd_rcgr = 0x6c01c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_phy_3a_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_phy_3b_aux_clk_src = {
	.cmd_rcgr = 0x7501c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_phy_3b_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_phy_4_aux_clk_src = {
	.cmd_rcgr = 0xd3018,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_phy_4_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_phy_5_aux_clk_src = {
	.cmd_rcgr = 0xd2018,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_phy_5_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_pcie_phy_6_aux_clk_src = {
	.cmd_rcgr = 0xd4018,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_phy_6_aux_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
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
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_oob_qspi_s0_clk_src[] = {
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
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_EVEN, 2, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	F(403000000, P_GCC_GPLL4_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_oob_qspi_s0_clk_src_init = {
	.name = "gcc_qupv3_oob_qspi_s0_clk_src",
	.parent_data = gcc_parent_data_3,
	.num_parents = ARRAY_SIZE(gcc_parent_data_3),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_oob_qspi_s0_clk_src = {
	.cmd_rcgr = 0xe7044,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_qupv3_oob_qspi_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_oob_qspi_s0_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_qupv3_oob_qspi_s1_clk_src[] = {
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
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_EVEN, 2, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_oob_qspi_s1_clk_src_init = {
	.name = "gcc_qupv3_oob_qspi_s1_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_oob_qspi_s1_clk_src = {
	.cmd_rcgr = 0xe7170,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_oob_qspi_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_oob_qspi_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_qspi_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap0_qspi_s2_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_qspi_s2_clk_src = {
	.cmd_rcgr = 0x287a0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_oob_qspi_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_qspi_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_qspi_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap0_qspi_s3_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_qspi_s3_clk_src = {
	.cmd_rcgr = 0x288d0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_oob_qspi_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_qspi_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_qspi_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap0_qspi_s6_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_qspi_s6_clk_src = {
	.cmd_rcgr = 0x2866c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_oob_qspi_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_qspi_s6_clk_src_init,
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
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x28014,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s1_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x28150,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s1_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s4_clk_src[] = {
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

static struct clk_init_data gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x282b4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x283f0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s7_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s7_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s7_clk_src = {
	.cmd_rcgr = 0x28540,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s7_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_qspi_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap1_qspi_s2_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_qspi_s2_clk_src = {
	.cmd_rcgr = 0xb37a0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_oob_qspi_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_qspi_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_qspi_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap1_qspi_s3_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_qspi_s3_clk_src = {
	.cmd_rcgr = 0xb38d0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_oob_qspi_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_qspi_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_qspi_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap1_qspi_s6_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_qspi_s6_clk_src = {
	.cmd_rcgr = 0xb366c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_oob_qspi_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_qspi_s6_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s0_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0xb3014,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s1_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s1_clk_src = {
	.cmd_rcgr = 0xb3150,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0xb32b4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0xb33f0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s7_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s7_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s7_clk_src = {
	.cmd_rcgr = 0xb3540,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s7_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_qspi_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap2_qspi_s2_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_qspi_s2_clk_src = {
	.cmd_rcgr = 0xb47a0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_oob_qspi_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_qspi_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_qspi_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap2_qspi_s3_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_qspi_s3_clk_src = {
	.cmd_rcgr = 0xb48d0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_oob_qspi_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_qspi_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_qspi_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap2_qspi_s6_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_qspi_s6_clk_src = {
	.cmd_rcgr = 0xb466c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_oob_qspi_s1_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_qspi_s6_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s0_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s0_clk_src = {
	.cmd_rcgr = 0xb4014,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s1_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s1_clk_src = {
	.cmd_rcgr = 0xb4150,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s4_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s4_clk_src = {
	.cmd_rcgr = 0xb42b4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s5_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s5_clk_src = {
	.cmd_rcgr = 0xb43f0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s7_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s7_clk_src",
	.parent_data = gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s7_clk_src = {
	.cmd_rcgr = 0xb4540,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s4_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s7_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(202000000, P_GCC_GPLL9_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc2_apps_clk_src = {
	.cmd_rcgr = 0xb001c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_17,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parent_data_17,
		.num_parents = ARRAY_SIZE(gcc_parent_data_17),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc4_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(75000000, P_GCC_GPLL0_OUT_MAIN, 8, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc4_apps_clk_src = {
	.cmd_rcgr = 0xdf01c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_sdcc4_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc4_apps_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_axi_clk_src[] = {
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(201500000, P_GCC_GPLL4_OUT_MAIN, 4, 0, 0),
	F(403000000, P_GCC_GPLL4_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_axi_clk_src = {
	.cmd_rcgr = 0x77038,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_ufs_phy_axi_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_axi_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_ice_core_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(201500000, P_GCC_GPLL4_OUT_MAIN, 4, 0, 0),
	F(403000000, P_GCC_GPLL4_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x77090,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_ufs_phy_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_ice_core_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x770c4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_phy_aux_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x770a8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_ufs_phy_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_unipro_core_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb20_master_clk_src[] = {
	F(60000000, P_GCC_GPLL14_OUT_MAIN, 10, 0, 0),
	F(120000000, P_GCC_GPLL14_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb20_master_clk_src = {
	.cmd_rcgr = 0xbc030,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_usb20_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb20_master_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb20_mock_utmi_clk_src = {
	.cmd_rcgr = 0xbc048,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb20_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb30_mp_master_clk_src[] = {
	F(66666667, P_GCC_GPLL0_OUT_EVEN, 4.5, 0, 0),
	F(133333333, P_GCC_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(200000000, P_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	F(240000000, P_GCC_GPLL0_OUT_MAIN, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_mp_master_clk_src = {
	.cmd_rcgr = 0x9a03c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_mp_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_mp_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb30_mp_mock_utmi_clk_src = {
	.cmd_rcgr = 0x9a054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_mp_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb30_prim_master_clk_src = {
	.cmd_rcgr = 0x3f04c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_mp_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb30_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0x3f064,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb30_sec_master_clk_src = {
	.cmd_rcgr = 0xe203c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_mp_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_sec_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb30_sec_mock_utmi_clk_src = {
	.cmd_rcgr = 0xe2054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_sec_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb30_tert_master_clk_src = {
	.cmd_rcgr = 0xe103c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_mp_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_tert_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb30_tert_mock_utmi_clk_src = {
	.cmd_rcgr = 0xe1054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_tert_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb3_mp_phy_aux_clk_src = {
	.cmd_rcgr = 0x9a088,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_mp_phy_aux_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0x3f090,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb3_sec_phy_aux_clk_src = {
	.cmd_rcgr = 0xe2080,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_sec_phy_aux_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb3_tert_phy_aux_clk_src = {
	.cmd_rcgr = 0xe1080,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_tert_phy_aux_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb4_0_master_clk_src[] = {
	F(85714286, P_GCC_GPLL0_OUT_EVEN, 3.5, 0, 0),
	F(177666750, P_GCC_GPLL8_OUT_MAIN, 4, 0, 0),
	F(355333500, P_GCC_GPLL8_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb4_0_master_clk_src = {
	.cmd_rcgr = 0x2b02c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_usb4_0_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_0_master_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb4_0_phy_pcie_pipe_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(125000000, P_GCC_GPLL7_OUT_MAIN, 4, 0, 0),
	F(250000000, P_GCC_GPLL7_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb4_0_phy_pcie_pipe_clk_src = {
	.cmd_rcgr = 0x2b104,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_32,
	.freq_tbl = ftbl_gcc_usb4_0_phy_pcie_pipe_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_0_phy_pcie_pipe_clk_src",
		.parent_data = gcc_parent_data_32,
		.num_parents = ARRAY_SIZE(gcc_parent_data_32),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb4_0_sb_if_clk_src = {
	.cmd_rcgr = 0x2b0a0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_0_sb_if_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb4_0_tmu_clk_src = {
	.cmd_rcgr = 0x2b084,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_usb4_0_phy_pcie_pipe_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_0_tmu_clk_src",
		.parent_data = gcc_parent_data_10,
		.num_parents = ARRAY_SIZE(gcc_parent_data_10),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb4_1_master_clk_src = {
	.cmd_rcgr = 0x2d02c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_usb4_0_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_1_master_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb4_1_phy_pcie_pipe_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(177666750, P_GCC_GPLL8_OUT_MAIN, 4, 0, 0),
	F(355333500, P_GCC_GPLL8_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb4_1_phy_pcie_pipe_clk_src = {
	.cmd_rcgr = 0x2d128,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_usb4_1_phy_pcie_pipe_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_1_phy_pcie_pipe_clk_src",
		.parent_data = gcc_parent_data_11,
		.num_parents = ARRAY_SIZE(gcc_parent_data_11),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb4_1_phy_pll_pipe_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	F(311000000, P_GCC_GPLL5_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb4_1_phy_pll_pipe_clk_src = {
	.cmd_rcgr = 0x2d0c8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_41,
	.freq_tbl = ftbl_gcc_usb4_1_phy_pll_pipe_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_1_phy_pll_pipe_clk_src",
		.parent_data = gcc_parent_data_41,
		.num_parents = ARRAY_SIZE(gcc_parent_data_41),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb4_1_sb_if_clk_src = {
	.cmd_rcgr = 0x2d0ac,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_1_sb_if_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb4_1_tmu_clk_src = {
	.cmd_rcgr = 0x2d090,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_usb4_0_phy_pcie_pipe_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_1_tmu_clk_src",
		.parent_data = gcc_parent_data_10,
		.num_parents = ARRAY_SIZE(gcc_parent_data_10),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb4_2_master_clk_src = {
	.cmd_rcgr = 0xe002c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_usb4_0_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_2_master_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb4_2_phy_pcie_pipe_clk_src = {
	.cmd_rcgr = 0xe0108,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_usb4_0_phy_pcie_pipe_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_2_phy_pcie_pipe_clk_src",
		.parent_data = gcc_parent_data_11,
		.num_parents = ARRAY_SIZE(gcc_parent_data_11),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb4_2_sb_if_clk_src = {
	.cmd_rcgr = 0xe00a4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_2_sb_if_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_rcg2 gcc_usb4_2_tmu_clk_src = {
	.cmd_rcgr = 0xe0088,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_usb4_0_phy_pcie_pipe_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_2_tmu_clk_src",
		.parent_data = gcc_parent_data_10,
		.num_parents = ARRAY_SIZE(gcc_parent_data_10),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_no_init_park_ops,
	},
};

static struct clk_regmap_div gcc_pcie_3b_pipe_div_clk_src = {
	.reg = 0x94070,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_3b_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_pcie_3b_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_4_pipe_div_clk_src = {
	.reg = 0x88060,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_4_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_pcie_4_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_5_pipe_div_clk_src = {
	.reg = 0xc306c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_5_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_pcie_5_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_6_pipe_div_clk_src = {
	.reg = 0x8a060,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_6_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_pcie_6_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_oob_s0_clk_src = {
	.reg = 0xe7024,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_oob_s0_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_oob_qspi_s0_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_oob_s1_clk_src = {
	.reg = 0xe7038,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_oob_s1_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_oob_qspi_s1_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap0_s2_clk_src = {
	.reg = 0x2828c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap0_s2_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap0_qspi_s2_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap0_s3_clk_src = {
	.reg = 0x282a0,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap0_s3_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap0_qspi_s3_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap0_s6_clk_src = {
	.reg = 0x2852c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap0_s6_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap0_qspi_s6_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap1_s2_clk_src = {
	.reg = 0xb328c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap1_s2_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap1_qspi_s2_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap1_s3_clk_src = {
	.reg = 0xb32a0,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap1_s3_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap1_qspi_s3_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap1_s6_clk_src = {
	.reg = 0xb352c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap1_s6_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap1_qspi_s6_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap2_s2_clk_src = {
	.reg = 0xb428c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap2_s2_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap2_qspi_s2_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap2_s3_clk_src = {
	.reg = 0xb42a0,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap2_s3_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap2_qspi_s3_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap2_s6_clk_src = {
	.reg = 0xb452c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap2_s6_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_wrap2_qspi_s6_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb20_mock_utmi_postdiv_clk_src = {
	.reg = 0xbc174,
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

static struct clk_regmap_div gcc_usb30_mp_mock_utmi_postdiv_clk_src = {
	.reg = 0x9a06c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_mp_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb30_mp_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_prim_mock_utmi_postdiv_clk_src = {
	.reg = 0x3f07c,
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

static struct clk_regmap_div gcc_usb30_sec_mock_utmi_postdiv_clk_src = {
	.reg = 0xe206c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_sec_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb30_sec_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_tert_mock_utmi_postdiv_clk_src = {
	.reg = 0xe106c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_tert_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb30_tert_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_aggre_noc_pcie_3a_west_sf_axi_clk = {
	.halt_reg = 0xdc0bc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie_3a_west_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_noc_pcie_3b_west_sf_axi_clk = {
	.halt_reg = 0x941ec,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie_3b_west_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_noc_pcie_4_west_sf_axi_clk = {
	.halt_reg = 0x881d0,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie_4_west_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_noc_pcie_5_east_sf_axi_clk = {
	.halt_reg = 0xc30d0,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie_5_east_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_noc_pcie_6_west_sf_axi_clk = {
	.halt_reg = 0x8a1d0,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie_6_west_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_phy_axi_clk = {
	.halt_reg = 0x77000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb2_prim_axi_clk = {
	.halt_reg = 0xbc17c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xbc17c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xbc17c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb2_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_mp_axi_clk = {
	.halt_reg = 0x9a004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9a004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9a004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb3_mp_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_mp_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_prim_axi_clk = {
	.halt_reg = 0x3f00c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3f00c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3f00c,
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

static struct clk_branch gcc_aggre_usb3_sec_axi_clk = {
	.halt_reg = 0xe2004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xe2004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xe2004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb3_sec_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_sec_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_tert_axi_clk = {
	.halt_reg = 0xe1004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xe1004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xe1004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb3_tert_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_tert_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb4_0_axi_clk = {
	.halt_reg = 0x2b000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2b000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2b000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb4_0_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb4_1_axi_clk = {
	.halt_reg = 0x2d000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2d000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2d000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb4_1_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb4_2_axi_clk = {
	.halt_reg = 0xe0000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xe0000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xe0000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb4_2_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_av1e_ahb_clk = {
	.halt_reg = 0x9b02c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9b02c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9b02c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_av1e_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_av1e_axi_clk = {
	.halt_reg = 0x9b030,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x9b030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9b030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_av1e_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_av1e_xo_clk = {
	.halt_reg = 0x9b044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9b044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_av1e_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x34038,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x34038,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_hf_axi_clk = {
	.halt_reg = 0x26014,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x26014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_sf_axi_clk = {
	.halt_reg = 0x26028,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x26028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_pcie_anoc_ahb_clk = {
	.halt_reg = 0x82004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x82004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_pcie_anoc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_pcie_anoc_south_ahb_clk = {
	.halt_reg = 0xba2ec,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba2ec,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_pcie_anoc_south_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb2_prim_axi_clk = {
	.halt_reg = 0xbc178,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xbc178,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xbc178,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb2_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb20_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_mp_axi_clk = {
	.halt_reg = 0x9a000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9a000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9a000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb3_mp_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_mp_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0x3f000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3f000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3f000,
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

static struct clk_branch gcc_cfg_noc_usb3_sec_axi_clk = {
	.halt_reg = 0xe2000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xe2000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xe2000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb3_sec_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_sec_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_tert_axi_clk = {
	.halt_reg = 0xe1000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xe1000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xe1000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb3_tert_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_tert_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb_anoc_ahb_clk = {
	.halt_reg = 0x3f004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3f004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb_anoc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb_anoc_south_ahb_clk = {
	.halt_reg = 0x3f008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3f008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb_anoc_south_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_hf_axi_clk = {
	.halt_reg = 0x27008,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x27008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_hf_axi_clk",
			.ops = &clk_branch2_ops,
			.flags = CLK_IS_CRITICAL,
		},
	},
};

static struct clk_branch gcc_eva_ahb_clk = {
	.halt_reg = 0x9b004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9b004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9b004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_eva_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_eva_axi0_clk = {
	.halt_reg = 0x9b008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x9b008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9b008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_eva_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_eva_axi0c_clk = {
	.halt_reg = 0x9b01c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9b01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9b01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_eva_axi0c_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_eva_xo_clk = {
	.halt_reg = 0x9b024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9b024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_eva_xo_clk",
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
	.halt_reg = 0x92000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x92000,
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
	.halt_reg = 0x93000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x93000,
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

static struct clk_branch gcc_gpu_gemnoc_gfx_clk = {
	.halt_reg = 0x71010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_gemnoc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_gpll0_clk_src = {
	.halt_reg = 0x71024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62038,
		.enable_mask = BIT(0),
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
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7102c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62038,
		.enable_mask = BIT(1),
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

static struct clk_branch gcc_pcie_0_aux_clk = {
	.halt_reg = 0xc8018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(25),
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
	.halt_reg = 0xba4a8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba4a8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_mstr_axi_clk = {
	.halt_reg = 0xba498,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xba498,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_phy_rchng_clk = {
	.halt_reg = 0xc8038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(27),
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
	.halt_reg = 0xc8028,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_phy_pcie_pipe_mux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_axi_clk = {
	.halt_reg = 0xba488,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba488,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_q2a_axi_clk = {
	.halt_reg = 0xba484,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_aux_clk = {
	.halt_reg = 0x2e018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(18),
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
	.halt_reg = 0xba480,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba480,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_mstr_axi_clk = {
	.halt_reg = 0xba470,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xba470,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_phy_rchng_clk = {
	.halt_reg = 0x2e038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(20),
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
	.halt_reg = 0x2e028,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_phy_pcie_pipe_mux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_axi_clk = {
	.halt_reg = 0xba460,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba460,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_q2a_axi_clk = {
	.halt_reg = 0xba45c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_aux_clk = {
	.halt_reg = 0xc0018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_2_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_cfg_ahb_clk = {
	.halt_reg = 0xba4d0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba4d0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_mstr_axi_clk = {
	.halt_reg = 0xba4c0,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xba4c0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_phy_rchng_clk = {
	.halt_reg = 0xc0038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_2_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_pipe_clk = {
	.halt_reg = 0xc0028,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_phy_pcie_pipe_mux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_slv_axi_clk = {
	.halt_reg = 0xba4b0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba4b0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2_slv_q2a_axi_clk = {
	.halt_reg = 0xba4ac,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_aux_clk = {
	.halt_reg = 0xdc04c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xdc04c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_3a_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_cfg_ahb_clk = {
	.halt_reg = 0xba4f0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba4f0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_mstr_axi_clk = {
	.halt_reg = 0xdc038,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xdc038,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_phy_rchng_clk = {
	.halt_reg = 0xdc06c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xdc06c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_3a_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_pipe_clk = {
	.halt_reg = 0xdc05c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xdc05c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_3a_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_slv_axi_clk = {
	.halt_reg = 0xdc024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xdc024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_slv_q2a_axi_clk = {
	.halt_reg = 0xdc01c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xdc01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_aux_clk = {
	.halt_reg = 0x94050,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_3b_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_cfg_ahb_clk = {
	.halt_reg = 0xba4f4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba4f4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_mstr_axi_clk = {
	.halt_reg = 0x94038,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x94038,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_phy_rchng_clk = {
	.halt_reg = 0x94084,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_3b_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_pipe_clk = {
	.halt_reg = 0x94060,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_3b_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_pipe_div2_clk = {
	.halt_reg = 0x94074,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_3b_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_slv_axi_clk = {
	.halt_reg = 0x94024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x94024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_slv_q2a_axi_clk = {
	.halt_reg = 0x9401c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_aux_clk = {
	.halt_reg = 0x88040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_4_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_cfg_ahb_clk = {
	.halt_reg = 0xba4fc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba4fc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_mstr_axi_clk = {
	.halt_reg = 0x88030,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x88030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_phy_rchng_clk = {
	.halt_reg = 0x88074,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_4_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_pipe_clk = {
	.halt_reg = 0x88050,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_4_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_pipe_div2_clk = {
	.halt_reg = 0x88064,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_4_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_slv_axi_clk = {
	.halt_reg = 0x88020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x88020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_slv_q2a_axi_clk = {
	.halt_reg = 0x8801c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_5_aux_clk = {
	.halt_reg = 0xc304c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_5_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_5_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_5_cfg_ahb_clk = {
	.halt_reg = 0xba4f8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba4f8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_5_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_5_mstr_axi_clk = {
	.halt_reg = 0xc3038,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xc3038,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_5_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_5_phy_rchng_clk = {
	.halt_reg = 0xc3080,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_5_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_5_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_5_pipe_clk = {
	.halt_reg = 0xc305c,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_5_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_5_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_5_pipe_div2_clk = {
	.halt_reg = 0xc3070,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_5_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_5_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_5_slv_axi_clk = {
	.halt_reg = 0xc3024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xc3024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_5_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_5_slv_q2a_axi_clk = {
	.halt_reg = 0xc301c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_5_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_6_aux_clk = {
	.halt_reg = 0x8a040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_6_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_6_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_6_cfg_ahb_clk = {
	.halt_reg = 0xba500,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba500,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_6_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_6_mstr_axi_clk = {
	.halt_reg = 0x8a030,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x8a030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_6_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_6_phy_rchng_clk = {
	.halt_reg = 0x8a074,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_6_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_6_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_6_pipe_clk = {
	.halt_reg = 0x8a050,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_6_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_6_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_6_pipe_div2_clk = {
	.halt_reg = 0x8a064,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_6_pipe_div2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_6_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_6_slv_axi_clk = {
	.halt_reg = 0x8a020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8a020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_6_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_6_slv_q2a_axi_clk = {
	.halt_reg = 0x8a01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_6_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_pwrctl_clk = {
	.halt_reg = 0xba2ac,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_pwrctl_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_qosgen_extref_clk = {
	.halt_reg = 0xba2a8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_qosgen_extref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_sf_center_clk = {
	.halt_reg = 0xba2b0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba2b0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_sf_center_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_slave_sf_east_clk = {
	.halt_reg = 0xba2b8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba2b8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_slave_sf_east_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_slave_sf_west_clk = {
	.halt_reg = 0xba2c0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba2c0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_slave_sf_west_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_noc_tsctr_clk = {
	.halt_reg = 0xba2a4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba2a4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62008,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_noc_tsctr_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_phy_3a_aux_clk = {
	.halt_reg = 0x6c038,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6c038,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_phy_3a_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_phy_3a_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_phy_3b_aux_clk = {
	.halt_reg = 0x75034,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_phy_3b_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_phy_3b_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_phy_4_aux_clk = {
	.halt_reg = 0xd3030,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_phy_4_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_phy_4_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_phy_5_aux_clk = {
	.halt_reg = 0xd2030,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_phy_5_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_phy_5_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_phy_6_aux_clk = {
	.halt_reg = 0xd4030,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_phy_6_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_phy_6_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_rscc_cfg_ahb_clk = {
	.halt_reg = 0xb8004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xb8004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62038,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_rscc_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_rscc_xo_clk = {
	.halt_reg = 0xb8008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62038,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_rscc_xo_clk",
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
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_av1e_ahb_clk = {
	.halt_reg = 0x9b048,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9b048,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x9b048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_av1e_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_camera_cmd_ahb_clk = {
	.halt_reg = 0x26010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x26010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_camera_cmd_ahb_clk",
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

static struct clk_branch gcc_qmip_pcie_3a_ahb_clk = {
	.halt_reg = 0xdc018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xdc018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_pcie_3a_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_pcie_3b_ahb_clk = {
	.halt_reg = 0x94018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x94018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62028,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_pcie_3b_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_pcie_4_ahb_clk = {
	.halt_reg = 0x88018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x88018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_pcie_4_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_pcie_5_ahb_clk = {
	.halt_reg = 0xc3018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xc3018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_pcie_5_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_pcie_6_ahb_clk = {
	.halt_reg = 0x8a018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8a018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62030,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_pcie_6_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cv_cpu_ahb_clk = {
	.halt_reg = 0x32018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x32018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32018,
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
	.halt_reg = 0x32014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x32014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_v_cpu_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_vcodec1_ahb_clk = {
	.halt_reg = 0x32010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x32010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_vcodec1_ahb_clk",
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

static struct clk_branch gcc_qupv3_oob_core_2x_clk = {
	.halt_reg = 0xc5040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_oob_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_oob_core_clk = {
	.halt_reg = 0xc502c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_oob_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_oob_m_ahb_clk = {
	.halt_reg = 0xe7004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xe7004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xe7004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_oob_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_oob_qspi_s0_clk = {
	.halt_reg = 0xe7040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_oob_qspi_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_oob_qspi_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_oob_qspi_s1_clk = {
	.halt_reg = 0xe729c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_oob_qspi_s1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_oob_qspi_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_oob_s0_clk = {
	.halt_reg = 0xe7014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_oob_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_oob_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_oob_s1_clk = {
	.halt_reg = 0xe7028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_oob_s1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_oob_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_oob_s_ahb_clk = {
	.halt_reg = 0xc5028,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xc5028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_oob_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_oob_tcxo_clk = {
	.halt_reg = 0xe703c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_oob_tcxo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0xc5448,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0xc5434,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_qspi_s2_clk = {
	.halt_reg = 0x2879c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_qspi_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_qspi_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_qspi_s3_clk = {
	.halt_reg = 0x288cc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_qspi_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_qspi_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_qspi_s6_clk = {
	.halt_reg = 0x28798,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_qspi_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_qspi_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x28004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(13),
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
	.halt_reg = 0x28140,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(14),
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
		.enable_reg = 0x62020,
		.enable_mask = BIT(15),
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
	.halt_reg = 0x28290,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(16),
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
	.halt_reg = 0x282a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(17),
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
	.halt_reg = 0x283e0,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(18),
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

static struct clk_branch gcc_qupv3_wrap0_s6_clk = {
	.halt_reg = 0x2851c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s7_clk = {
	.halt_reg = 0x28530,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s7_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap0_s7_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_2x_clk = {
	.halt_reg = 0xc5198,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_clk = {
	.halt_reg = 0xc5184,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_qspi_s2_clk = {
	.halt_reg = 0xb379c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_qspi_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_qspi_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_qspi_s3_clk = {
	.halt_reg = 0xb38cc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_qspi_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_qspi_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_qspi_s6_clk = {
	.halt_reg = 0xb3798,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_qspi_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_qspi_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0xb3004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(15),
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
	.halt_reg = 0xb3140,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(16),
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
	.halt_reg = 0xb327c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(17),
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
	.halt_reg = 0xb3290,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(18),
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
	.halt_reg = 0xb32a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(19),
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
	.halt_reg = 0xb33e0,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(20),
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

static struct clk_branch gcc_qupv3_wrap1_s6_clk = {
	.halt_reg = 0xb351c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s7_clk = {
	.halt_reg = 0xb3530,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s7_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap1_s7_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_core_2x_clk = {
	.halt_reg = 0xc52f0,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_core_clk = {
	.halt_reg = 0xc52dc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_qspi_s2_clk = {
	.halt_reg = 0xb479c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_qspi_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap2_qspi_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_qspi_s3_clk = {
	.halt_reg = 0xb48cc,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_qspi_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap2_qspi_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_qspi_s6_clk = {
	.halt_reg = 0xb4798,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_qspi_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap2_qspi_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s0_clk = {
	.halt_reg = 0xb4004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(30),
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
	.halt_reg = 0xb4140,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(31),
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
	.halt_reg = 0xb427c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(0),
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
	.halt_reg = 0xb4290,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(1),
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
	.halt_reg = 0xb42a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(2),
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
	.halt_reg = 0xb43e0,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(3),
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

static struct clk_branch gcc_qupv3_wrap2_s6_clk = {
	.halt_reg = 0xb451c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap2_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s7_clk = {
	.halt_reg = 0xb4530,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s7_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_wrap2_s7_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_m_ahb_clk = {
	.halt_reg = 0xc542c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xc542c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_s_ahb_clk = {
	.halt_reg = 0xc5430,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xc5430,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62020,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_m_ahb_clk = {
	.halt_reg = 0xc517c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xc517c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_1_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_s_ahb_clk = {
	.halt_reg = 0xc5180,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xc5180,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_1_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_2_m_ahb_clk = {
	.halt_reg = 0xc52d4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xc52d4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_2_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_2_s_ahb_clk = {
	.halt_reg = 0xc52d8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xc52d8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x62018,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_2_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0xb0014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb0014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc2_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc2_apps_clk = {
	.halt_reg = 0xb0004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb0004,
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

static struct clk_branch gcc_sdcc4_ahb_clk = {
	.halt_reg = 0xdf014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xdf014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc4_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc4_apps_clk = {
	.halt_reg = 0xdf004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xdf004,
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

static struct clk_branch gcc_ufs_phy_ahb_clk = {
	.halt_reg = 0xba504,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba504,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xba504,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_clk = {
	.halt_reg = 0x7701c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7701c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7701c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ice_core_clk = {
	.halt_reg = 0x77080,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77080,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_phy_aux_clk = {
	.halt_reg = 0x770c0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x770c0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x770c0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_0_clk = {
	.halt_reg = 0x77034,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x77034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_rx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_1_clk = {
	.halt_reg = 0x770dc,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x770dc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_rx_symbol_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_tx_symbol_0_clk = {
	.halt_reg = 0x77030,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x77030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_tx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_tx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_clk = {
	.halt_reg = 0x77070,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77070,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_unipro_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_ufs_phy_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb20_master_clk = {
	.halt_reg = 0xbc018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xbc018,
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
	.halt_reg = 0xbc02c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xbc02c,
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
	.halt_reg = 0xbc028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xbc028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb20_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mp_master_clk = {
	.halt_reg = 0x9a024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9a024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_mp_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_mp_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mp_mock_utmi_clk = {
	.halt_reg = 0x9a038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9a038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_mp_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_mp_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mp_sleep_clk = {
	.halt_reg = 0x9a034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9a034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_mp_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_master_clk = {
	.halt_reg = 0x3f030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3f030,
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
	.halt_reg = 0x3f048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3f048,
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
	.halt_reg = 0x3f044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3f044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_master_clk = {
	.halt_reg = 0xe2024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe2024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_sec_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_sec_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_mock_utmi_clk = {
	.halt_reg = 0xe2038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe2038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_sec_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_sec_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_sleep_clk = {
	.halt_reg = 0xe2034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe2034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_sec_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_tert_master_clk = {
	.halt_reg = 0xe1024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe1024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_tert_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_tert_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_tert_mock_utmi_clk = {
	.halt_reg = 0xe1038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe1038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_tert_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb30_tert_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_tert_sleep_clk = {
	.halt_reg = 0xe1034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe1034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_tert_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mp_phy_aux_clk = {
	.halt_reg = 0x9a070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9a070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_mp_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mp_phy_com_aux_clk = {
	.halt_reg = 0x9a074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9a074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_mp_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mp_phy_pipe_0_clk = {
	.halt_reg = 0x9a078,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x9a078,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp_phy_pipe_0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_mp_phy_pipe_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mp_phy_pipe_1_clk = {
	.halt_reg = 0x9a080,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x9a080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp_phy_pipe_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_mp_phy_pipe_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_aux_clk = {
	.halt_reg = 0x3f080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3f080,
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
	.halt_reg = 0x3f084,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3f084,
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
	.halt_reg = 0x3f088,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x3f088,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3f088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb34_prim_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_aux_clk = {
	.halt_reg = 0xe2070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe2070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_sec_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_sec_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_com_aux_clk = {
	.halt_reg = 0xe2074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe2074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_sec_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_sec_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_pipe_clk = {
	.halt_reg = 0xe2078,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xe2078,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xe2078,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_sec_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb34_sec_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_tert_phy_aux_clk = {
	.halt_reg = 0xe1070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe1070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_tert_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_tert_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_tert_phy_com_aux_clk = {
	.halt_reg = 0xe1074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe1074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_tert_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb3_tert_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_tert_phy_pipe_clk = {
	.halt_reg = 0xe1078,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xe1078,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xe1078,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_tert_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb34_tert_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_cfg_ahb_clk = {
	.halt_reg = 0xba450,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba450,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xba450,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_dp0_clk = {
	.halt_reg = 0x2b070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_dp0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_phy_dp0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_dp1_clk = {
	.halt_reg = 0x2b124,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b124,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_dp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_phy_dp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_master_clk = {
	.halt_reg = 0x2b01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_phy_p2rr2p_pipe_clk = {
	.halt_reg = 0x2b0f4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b0f4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_p2rr2p_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_phy_p2rr2p_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_phy_pcie_pipe_clk = {
	.halt_reg = 0x2b04c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_pcie_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_phy_pcie_pipe_mux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_phy_rx0_clk = {
	.halt_reg = 0x2b0c4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b0c4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_rx0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_phy_rx0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_phy_rx1_clk = {
	.halt_reg = 0x2b0d8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b0d8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_rx1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_phy_rx1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_phy_usb_pipe_clk = {
	.halt_reg = 0x2b0bc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2b0bc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2b0bc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_phy_usb_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb34_prim_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_sb_if_clk = {
	.halt_reg = 0x2b048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_sb_if_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_sb_if_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_sys_clk = {
	.halt_reg = 0x2b05c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b05c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_phy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_tmu_clk = {
	.halt_reg = 0x2b09c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2b09c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2b09c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_tmu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_tmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_0_uc_hrr_clk = {
	.halt_reg = 0x2b06c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b06c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_0_uc_hrr_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_0_phy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_cfg_ahb_clk = {
	.halt_reg = 0xba454,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba454,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xba454,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_dp0_clk = {
	.halt_reg = 0x2d07c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d07c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_dp0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_phy_dp0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_dp1_clk = {
	.halt_reg = 0x2d144,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d144,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_dp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_phy_dp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_master_clk = {
	.halt_reg = 0x2d01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_phy_p2rr2p_pipe_clk = {
	.halt_reg = 0x2d118,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d118,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_p2rr2p_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_phy_p2rr2p_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_phy_pcie_pipe_clk = {
	.halt_reg = 0x2d04c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_pcie_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_phy_pcie_pipe_mux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_phy_rx0_clk = {
	.halt_reg = 0x2d0e8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d0e8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_rx0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_phy_rx0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_phy_rx1_clk = {
	.halt_reg = 0x2d0fc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d0fc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_rx1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_phy_rx1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_phy_usb_pipe_clk = {
	.halt_reg = 0x2d0e0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2d0e0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2d0e0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_usb_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb34_sec_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_sb_if_clk = {
	.halt_reg = 0x2d048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_sb_if_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_sb_if_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_sys_clk = {
	.halt_reg = 0x2d05c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d05c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_phy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_tmu_clk = {
	.halt_reg = 0x2d0a8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2d0a8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2d0a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_tmu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_tmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_uc_hrr_clk = {
	.halt_reg = 0x2d06c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d06c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_uc_hrr_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_1_phy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_cfg_ahb_clk = {
	.halt_reg = 0xba458,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba458,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xba458,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_dp0_clk = {
	.halt_reg = 0xe0070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe0070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_dp0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_phy_dp0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_dp1_clk = {
	.halt_reg = 0xe0128,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe0128,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_dp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_phy_dp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_master_clk = {
	.halt_reg = 0xe001c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe001c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_phy_p2rr2p_pipe_clk = {
	.halt_reg = 0xe00f8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe00f8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_p2rr2p_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_phy_p2rr2p_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_phy_pcie_pipe_clk = {
	.halt_reg = 0xe004c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x62010,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_pcie_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_phy_pcie_pipe_mux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_phy_rx0_clk = {
	.halt_reg = 0xe00c8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe00c8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_rx0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_phy_rx0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_phy_rx1_clk = {
	.halt_reg = 0xe00dc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe00dc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_rx1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_phy_rx1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_phy_usb_pipe_clk = {
	.halt_reg = 0xe00c0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xe00c0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xe00c0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_phy_usb_pipe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb34_tert_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_sb_if_clk = {
	.halt_reg = 0xe0048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe0048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_sb_if_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_sb_if_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_sys_clk = {
	.halt_reg = 0xe005c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe005c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_phy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_tmu_clk = {
	.halt_reg = 0xe00a0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xe00a0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xe00a0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_tmu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_tmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_2_uc_hrr_clk = {
	.halt_reg = 0xe006c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xe006c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_2_uc_hrr_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb4_2_phy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi0_clk = {
	.halt_reg = 0x3201c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x3201c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3201c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi0c_clk = {
	.halt_reg = 0x32030,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x32030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi0c_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi1_clk = {
	.halt_reg = 0x32044,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x32044,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x32044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc gcc_pcie_0_tunnel_gdsc = {
	.gdscr = 0xc8004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_0_tunnel_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_1_tunnel_gdsc = {
	.gdscr = 0x2e004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_1_tunnel_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_2_tunnel_gdsc = {
	.gdscr = 0xc0004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_2_tunnel_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_3a_gdsc = {
	.gdscr = 0xdc004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_3a_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_3a_phy_gdsc = {
	.gdscr = 0x6c004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_pcie_3a_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_3b_gdsc = {
	.gdscr = 0x94004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_3b_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_3b_phy_gdsc = {
	.gdscr = 0x75004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_pcie_3b_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_4_gdsc = {
	.gdscr = 0x88004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_4_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_4_phy_gdsc = {
	.gdscr = 0xd3004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_pcie_4_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_5_gdsc = {
	.gdscr = 0xc3004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_5_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_5_phy_gdsc = {
	.gdscr = 0xd2004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_pcie_5_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_6_gdsc = {
	.gdscr = 0x8a004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_pcie_6_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_pcie_6_phy_gdsc = {
	.gdscr = 0xd4004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_pcie_6_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | VOTABLE,
};

static struct gdsc gcc_ufs_phy_gdsc = {
	.gdscr = 0x77008,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_ufs_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb20_prim_gdsc = {
	.gdscr = 0xbc004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb20_prim_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb30_mp_gdsc = {
	.gdscr = 0x9a010,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb30_mp_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb30_prim_gdsc = {
	.gdscr = 0x3f01c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb30_prim_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb30_sec_gdsc = {
	.gdscr = 0xe2010,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb30_sec_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb30_tert_gdsc = {
	.gdscr = 0xe1010,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb30_tert_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb3_mp_ss0_phy_gdsc = {
	.gdscr = 0x5400c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_usb3_mp_ss0_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb3_mp_ss1_phy_gdsc = {
	.gdscr = 0x5402c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_usb3_mp_ss1_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb4_0_gdsc = {
	.gdscr = 0x2b008,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb4_0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb4_1_gdsc = {
	.gdscr = 0x2d008,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb4_1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb4_2_gdsc = {
	.gdscr = 0xe0008,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gcc_usb4_2_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb_0_phy_gdsc = {
	.gdscr = 0xdb024,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_usb_0_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb_1_phy_gdsc = {
	.gdscr = 0x2c024,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_usb_1_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc gcc_usb_2_phy_gdsc = {
	.gdscr = 0xbe024,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gcc_usb_2_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct clk_regmap *gcc_glymur_clocks[] = {
	[GCC_AGGRE_NOC_PCIE_3A_WEST_SF_AXI_CLK] = &gcc_aggre_noc_pcie_3a_west_sf_axi_clk.clkr,
	[GCC_AGGRE_NOC_PCIE_3B_WEST_SF_AXI_CLK] = &gcc_aggre_noc_pcie_3b_west_sf_axi_clk.clkr,
	[GCC_AGGRE_NOC_PCIE_4_WEST_SF_AXI_CLK] = &gcc_aggre_noc_pcie_4_west_sf_axi_clk.clkr,
	[GCC_AGGRE_NOC_PCIE_5_EAST_SF_AXI_CLK] = &gcc_aggre_noc_pcie_5_east_sf_axi_clk.clkr,
	[GCC_AGGRE_NOC_PCIE_6_WEST_SF_AXI_CLK] = &gcc_aggre_noc_pcie_6_west_sf_axi_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_CLK] = &gcc_aggre_ufs_phy_axi_clk.clkr,
	[GCC_AGGRE_USB2_PRIM_AXI_CLK] = &gcc_aggre_usb2_prim_axi_clk.clkr,
	[GCC_AGGRE_USB3_MP_AXI_CLK] = &gcc_aggre_usb3_mp_axi_clk.clkr,
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = &gcc_aggre_usb3_prim_axi_clk.clkr,
	[GCC_AGGRE_USB3_SEC_AXI_CLK] = &gcc_aggre_usb3_sec_axi_clk.clkr,
	[GCC_AGGRE_USB3_TERT_AXI_CLK] = &gcc_aggre_usb3_tert_axi_clk.clkr,
	[GCC_AGGRE_USB4_0_AXI_CLK] = &gcc_aggre_usb4_0_axi_clk.clkr,
	[GCC_AGGRE_USB4_1_AXI_CLK] = &gcc_aggre_usb4_1_axi_clk.clkr,
	[GCC_AGGRE_USB4_2_AXI_CLK] = &gcc_aggre_usb4_2_axi_clk.clkr,
	[GCC_AV1E_AHB_CLK] = &gcc_av1e_ahb_clk.clkr,
	[GCC_AV1E_AXI_CLK] = &gcc_av1e_axi_clk.clkr,
	[GCC_AV1E_XO_CLK] = &gcc_av1e_xo_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAMERA_HF_AXI_CLK] = &gcc_camera_hf_axi_clk.clkr,
	[GCC_CAMERA_SF_AXI_CLK] = &gcc_camera_sf_axi_clk.clkr,
	[GCC_CFG_NOC_PCIE_ANOC_AHB_CLK] = &gcc_cfg_noc_pcie_anoc_ahb_clk.clkr,
	[GCC_CFG_NOC_PCIE_ANOC_SOUTH_AHB_CLK] = &gcc_cfg_noc_pcie_anoc_south_ahb_clk.clkr,
	[GCC_CFG_NOC_USB2_PRIM_AXI_CLK] = &gcc_cfg_noc_usb2_prim_axi_clk.clkr,
	[GCC_CFG_NOC_USB3_MP_AXI_CLK] = &gcc_cfg_noc_usb3_mp_axi_clk.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_CFG_NOC_USB3_SEC_AXI_CLK] = &gcc_cfg_noc_usb3_sec_axi_clk.clkr,
	[GCC_CFG_NOC_USB3_TERT_AXI_CLK] = &gcc_cfg_noc_usb3_tert_axi_clk.clkr,
	[GCC_CFG_NOC_USB_ANOC_AHB_CLK] = &gcc_cfg_noc_usb_anoc_ahb_clk.clkr,
	[GCC_CFG_NOC_USB_ANOC_SOUTH_AHB_CLK] = &gcc_cfg_noc_usb_anoc_south_ahb_clk.clkr,
	[GCC_DISP_HF_AXI_CLK] = &gcc_disp_hf_axi_clk.clkr,
	[GCC_EVA_AHB_CLK] = &gcc_eva_ahb_clk.clkr,
	[GCC_EVA_AXI0_CLK] = &gcc_eva_axi0_clk.clkr,
	[GCC_EVA_AXI0C_CLK] = &gcc_eva_axi0c_clk.clkr,
	[GCC_EVA_XO_CLK] = &gcc_eva_xo_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GPLL0] = &gcc_gpll0.clkr,
	[GCC_GPLL0_OUT_EVEN] = &gcc_gpll0_out_even.clkr,
	[GCC_GPLL1] = &gcc_gpll1.clkr,
	[GCC_GPLL14] = &gcc_gpll14.clkr,
	[GCC_GPLL14_OUT_EVEN] = &gcc_gpll14_out_even.clkr,
	[GCC_GPLL4] = &gcc_gpll4.clkr,
	[GCC_GPLL5] = &gcc_gpll5.clkr,
	[GCC_GPLL7] = &gcc_gpll7.clkr,
	[GCC_GPLL8] = &gcc_gpll8.clkr,
	[GCC_GPLL9] = &gcc_gpll9.clkr,
	[GCC_GPU_GEMNOC_GFX_CLK] = &gcc_gpu_gemnoc_gfx_clk.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_PCIE_0_AUX_CLK] = &gcc_pcie_0_aux_clk.clkr,
	[GCC_PCIE_0_AUX_CLK_SRC] = &gcc_pcie_0_aux_clk_src.clkr,
	[GCC_PCIE_0_CFG_AHB_CLK] = &gcc_pcie_0_cfg_ahb_clk.clkr,
	[GCC_PCIE_0_MSTR_AXI_CLK] = &gcc_pcie_0_mstr_axi_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK] = &gcc_pcie_0_phy_rchng_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK_SRC] = &gcc_pcie_0_phy_rchng_clk_src.clkr,
	[GCC_PCIE_0_PIPE_CLK] = &gcc_pcie_0_pipe_clk.clkr,
	[GCC_PCIE_0_SLV_AXI_CLK] = &gcc_pcie_0_slv_axi_clk.clkr,
	[GCC_PCIE_0_SLV_Q2A_AXI_CLK] = &gcc_pcie_0_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_1_AUX_CLK] = &gcc_pcie_1_aux_clk.clkr,
	[GCC_PCIE_1_AUX_CLK_SRC] = &gcc_pcie_1_aux_clk_src.clkr,
	[GCC_PCIE_1_CFG_AHB_CLK] = &gcc_pcie_1_cfg_ahb_clk.clkr,
	[GCC_PCIE_1_MSTR_AXI_CLK] = &gcc_pcie_1_mstr_axi_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK] = &gcc_pcie_1_phy_rchng_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK_SRC] = &gcc_pcie_1_phy_rchng_clk_src.clkr,
	[GCC_PCIE_1_PIPE_CLK] = &gcc_pcie_1_pipe_clk.clkr,
	[GCC_PCIE_1_SLV_AXI_CLK] = &gcc_pcie_1_slv_axi_clk.clkr,
	[GCC_PCIE_1_SLV_Q2A_AXI_CLK] = &gcc_pcie_1_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_2_AUX_CLK] = &gcc_pcie_2_aux_clk.clkr,
	[GCC_PCIE_2_AUX_CLK_SRC] = &gcc_pcie_2_aux_clk_src.clkr,
	[GCC_PCIE_2_CFG_AHB_CLK] = &gcc_pcie_2_cfg_ahb_clk.clkr,
	[GCC_PCIE_2_MSTR_AXI_CLK] = &gcc_pcie_2_mstr_axi_clk.clkr,
	[GCC_PCIE_2_PHY_RCHNG_CLK] = &gcc_pcie_2_phy_rchng_clk.clkr,
	[GCC_PCIE_2_PHY_RCHNG_CLK_SRC] = &gcc_pcie_2_phy_rchng_clk_src.clkr,
	[GCC_PCIE_2_PIPE_CLK] = &gcc_pcie_2_pipe_clk.clkr,
	[GCC_PCIE_2_SLV_AXI_CLK] = &gcc_pcie_2_slv_axi_clk.clkr,
	[GCC_PCIE_2_SLV_Q2A_AXI_CLK] = &gcc_pcie_2_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_3A_AUX_CLK] = &gcc_pcie_3a_aux_clk.clkr,
	[GCC_PCIE_3A_AUX_CLK_SRC] = &gcc_pcie_3a_aux_clk_src.clkr,
	[GCC_PCIE_3A_CFG_AHB_CLK] = &gcc_pcie_3a_cfg_ahb_clk.clkr,
	[GCC_PCIE_3A_MSTR_AXI_CLK] = &gcc_pcie_3a_mstr_axi_clk.clkr,
	[GCC_PCIE_3A_PHY_RCHNG_CLK] = &gcc_pcie_3a_phy_rchng_clk.clkr,
	[GCC_PCIE_3A_PHY_RCHNG_CLK_SRC] = &gcc_pcie_3a_phy_rchng_clk_src.clkr,
	[GCC_PCIE_3A_PIPE_CLK] = &gcc_pcie_3a_pipe_clk.clkr,
	[GCC_PCIE_3A_PIPE_CLK_SRC] = &gcc_pcie_3a_pipe_clk_src.clkr,
	[GCC_PCIE_3A_SLV_AXI_CLK] = &gcc_pcie_3a_slv_axi_clk.clkr,
	[GCC_PCIE_3A_SLV_Q2A_AXI_CLK] = &gcc_pcie_3a_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_3B_AUX_CLK] = &gcc_pcie_3b_aux_clk.clkr,
	[GCC_PCIE_3B_AUX_CLK_SRC] = &gcc_pcie_3b_aux_clk_src.clkr,
	[GCC_PCIE_3B_CFG_AHB_CLK] = &gcc_pcie_3b_cfg_ahb_clk.clkr,
	[GCC_PCIE_3B_MSTR_AXI_CLK] = &gcc_pcie_3b_mstr_axi_clk.clkr,
	[GCC_PCIE_3B_PHY_RCHNG_CLK] = &gcc_pcie_3b_phy_rchng_clk.clkr,
	[GCC_PCIE_3B_PHY_RCHNG_CLK_SRC] = &gcc_pcie_3b_phy_rchng_clk_src.clkr,
	[GCC_PCIE_3B_PIPE_CLK] = &gcc_pcie_3b_pipe_clk.clkr,
	[GCC_PCIE_3B_PIPE_CLK_SRC] = &gcc_pcie_3b_pipe_clk_src.clkr,
	[GCC_PCIE_3B_PIPE_DIV2_CLK] = &gcc_pcie_3b_pipe_div2_clk.clkr,
	[GCC_PCIE_3B_PIPE_DIV_CLK_SRC] = &gcc_pcie_3b_pipe_div_clk_src.clkr,
	[GCC_PCIE_3B_SLV_AXI_CLK] = &gcc_pcie_3b_slv_axi_clk.clkr,
	[GCC_PCIE_3B_SLV_Q2A_AXI_CLK] = &gcc_pcie_3b_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_4_AUX_CLK] = &gcc_pcie_4_aux_clk.clkr,
	[GCC_PCIE_4_AUX_CLK_SRC] = &gcc_pcie_4_aux_clk_src.clkr,
	[GCC_PCIE_4_CFG_AHB_CLK] = &gcc_pcie_4_cfg_ahb_clk.clkr,
	[GCC_PCIE_4_MSTR_AXI_CLK] = &gcc_pcie_4_mstr_axi_clk.clkr,
	[GCC_PCIE_4_PHY_RCHNG_CLK] = &gcc_pcie_4_phy_rchng_clk.clkr,
	[GCC_PCIE_4_PHY_RCHNG_CLK_SRC] = &gcc_pcie_4_phy_rchng_clk_src.clkr,
	[GCC_PCIE_4_PIPE_CLK] = &gcc_pcie_4_pipe_clk.clkr,
	[GCC_PCIE_4_PIPE_CLK_SRC] = &gcc_pcie_4_pipe_clk_src.clkr,
	[GCC_PCIE_4_PIPE_DIV2_CLK] = &gcc_pcie_4_pipe_div2_clk.clkr,
	[GCC_PCIE_4_PIPE_DIV_CLK_SRC] = &gcc_pcie_4_pipe_div_clk_src.clkr,
	[GCC_PCIE_4_SLV_AXI_CLK] = &gcc_pcie_4_slv_axi_clk.clkr,
	[GCC_PCIE_4_SLV_Q2A_AXI_CLK] = &gcc_pcie_4_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_5_AUX_CLK] = &gcc_pcie_5_aux_clk.clkr,
	[GCC_PCIE_5_AUX_CLK_SRC] = &gcc_pcie_5_aux_clk_src.clkr,
	[GCC_PCIE_5_CFG_AHB_CLK] = &gcc_pcie_5_cfg_ahb_clk.clkr,
	[GCC_PCIE_5_MSTR_AXI_CLK] = &gcc_pcie_5_mstr_axi_clk.clkr,
	[GCC_PCIE_5_PHY_RCHNG_CLK] = &gcc_pcie_5_phy_rchng_clk.clkr,
	[GCC_PCIE_5_PHY_RCHNG_CLK_SRC] = &gcc_pcie_5_phy_rchng_clk_src.clkr,
	[GCC_PCIE_5_PIPE_CLK] = &gcc_pcie_5_pipe_clk.clkr,
	[GCC_PCIE_5_PIPE_CLK_SRC] = &gcc_pcie_5_pipe_clk_src.clkr,
	[GCC_PCIE_5_PIPE_DIV2_CLK] = &gcc_pcie_5_pipe_div2_clk.clkr,
	[GCC_PCIE_5_PIPE_DIV_CLK_SRC] = &gcc_pcie_5_pipe_div_clk_src.clkr,
	[GCC_PCIE_5_SLV_AXI_CLK] = &gcc_pcie_5_slv_axi_clk.clkr,
	[GCC_PCIE_5_SLV_Q2A_AXI_CLK] = &gcc_pcie_5_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_6_AUX_CLK] = &gcc_pcie_6_aux_clk.clkr,
	[GCC_PCIE_6_AUX_CLK_SRC] = &gcc_pcie_6_aux_clk_src.clkr,
	[GCC_PCIE_6_CFG_AHB_CLK] = &gcc_pcie_6_cfg_ahb_clk.clkr,
	[GCC_PCIE_6_MSTR_AXI_CLK] = &gcc_pcie_6_mstr_axi_clk.clkr,
	[GCC_PCIE_6_PHY_RCHNG_CLK] = &gcc_pcie_6_phy_rchng_clk.clkr,
	[GCC_PCIE_6_PHY_RCHNG_CLK_SRC] = &gcc_pcie_6_phy_rchng_clk_src.clkr,
	[GCC_PCIE_6_PIPE_CLK] = &gcc_pcie_6_pipe_clk.clkr,
	[GCC_PCIE_6_PIPE_CLK_SRC] = &gcc_pcie_6_pipe_clk_src.clkr,
	[GCC_PCIE_6_PIPE_DIV2_CLK] = &gcc_pcie_6_pipe_div2_clk.clkr,
	[GCC_PCIE_6_PIPE_DIV_CLK_SRC] = &gcc_pcie_6_pipe_div_clk_src.clkr,
	[GCC_PCIE_6_SLV_AXI_CLK] = &gcc_pcie_6_slv_axi_clk.clkr,
	[GCC_PCIE_6_SLV_Q2A_AXI_CLK] = &gcc_pcie_6_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_NOC_PWRCTL_CLK] = &gcc_pcie_noc_pwrctl_clk.clkr,
	[GCC_PCIE_NOC_QOSGEN_EXTREF_CLK] = &gcc_pcie_noc_qosgen_extref_clk.clkr,
	[GCC_PCIE_NOC_SF_CENTER_CLK] = &gcc_pcie_noc_sf_center_clk.clkr,
	[GCC_PCIE_NOC_SLAVE_SF_EAST_CLK] = &gcc_pcie_noc_slave_sf_east_clk.clkr,
	[GCC_PCIE_NOC_SLAVE_SF_WEST_CLK] = &gcc_pcie_noc_slave_sf_west_clk.clkr,
	[GCC_PCIE_NOC_TSCTR_CLK] = &gcc_pcie_noc_tsctr_clk.clkr,
	[GCC_PCIE_PHY_3A_AUX_CLK] = &gcc_pcie_phy_3a_aux_clk.clkr,
	[GCC_PCIE_PHY_3A_AUX_CLK_SRC] = &gcc_pcie_phy_3a_aux_clk_src.clkr,
	[GCC_PCIE_PHY_3B_AUX_CLK] = &gcc_pcie_phy_3b_aux_clk.clkr,
	[GCC_PCIE_PHY_3B_AUX_CLK_SRC] = &gcc_pcie_phy_3b_aux_clk_src.clkr,
	[GCC_PCIE_PHY_4_AUX_CLK] = &gcc_pcie_phy_4_aux_clk.clkr,
	[GCC_PCIE_PHY_4_AUX_CLK_SRC] = &gcc_pcie_phy_4_aux_clk_src.clkr,
	[GCC_PCIE_PHY_5_AUX_CLK] = &gcc_pcie_phy_5_aux_clk.clkr,
	[GCC_PCIE_PHY_5_AUX_CLK_SRC] = &gcc_pcie_phy_5_aux_clk_src.clkr,
	[GCC_PCIE_PHY_6_AUX_CLK] = &gcc_pcie_phy_6_aux_clk.clkr,
	[GCC_PCIE_PHY_6_AUX_CLK_SRC] = &gcc_pcie_phy_6_aux_clk_src.clkr,
	[GCC_PCIE_RSCC_CFG_AHB_CLK] = &gcc_pcie_rscc_cfg_ahb_clk.clkr,
	[GCC_PCIE_RSCC_XO_CLK] = &gcc_pcie_rscc_xo_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_QMIP_AV1E_AHB_CLK] = &gcc_qmip_av1e_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_CMD_AHB_CLK] = &gcc_qmip_camera_cmd_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_NRT_AHB_CLK] = &gcc_qmip_camera_nrt_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_RT_AHB_CLK] = &gcc_qmip_camera_rt_ahb_clk.clkr,
	[GCC_QMIP_GPU_AHB_CLK] = &gcc_qmip_gpu_ahb_clk.clkr,
	[GCC_QMIP_PCIE_3A_AHB_CLK] = &gcc_qmip_pcie_3a_ahb_clk.clkr,
	[GCC_QMIP_PCIE_3B_AHB_CLK] = &gcc_qmip_pcie_3b_ahb_clk.clkr,
	[GCC_QMIP_PCIE_4_AHB_CLK] = &gcc_qmip_pcie_4_ahb_clk.clkr,
	[GCC_QMIP_PCIE_5_AHB_CLK] = &gcc_qmip_pcie_5_ahb_clk.clkr,
	[GCC_QMIP_PCIE_6_AHB_CLK] = &gcc_qmip_pcie_6_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CV_CPU_AHB_CLK] = &gcc_qmip_video_cv_cpu_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CVP_AHB_CLK] = &gcc_qmip_video_cvp_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_V_CPU_AHB_CLK] = &gcc_qmip_video_v_cpu_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_VCODEC1_AHB_CLK] = &gcc_qmip_video_vcodec1_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_VCODEC_AHB_CLK] = &gcc_qmip_video_vcodec_ahb_clk.clkr,
	[GCC_QUPV3_OOB_CORE_2X_CLK] = &gcc_qupv3_oob_core_2x_clk.clkr,
	[GCC_QUPV3_OOB_CORE_CLK] = &gcc_qupv3_oob_core_clk.clkr,
	[GCC_QUPV3_OOB_M_AHB_CLK] = &gcc_qupv3_oob_m_ahb_clk.clkr,
	[GCC_QUPV3_OOB_QSPI_S0_CLK] = &gcc_qupv3_oob_qspi_s0_clk.clkr,
	[GCC_QUPV3_OOB_QSPI_S0_CLK_SRC] = &gcc_qupv3_oob_qspi_s0_clk_src.clkr,
	[GCC_QUPV3_OOB_QSPI_S1_CLK] = &gcc_qupv3_oob_qspi_s1_clk.clkr,
	[GCC_QUPV3_OOB_QSPI_S1_CLK_SRC] = &gcc_qupv3_oob_qspi_s1_clk_src.clkr,
	[GCC_QUPV3_OOB_S0_CLK] = &gcc_qupv3_oob_s0_clk.clkr,
	[GCC_QUPV3_OOB_S0_CLK_SRC] = &gcc_qupv3_oob_s0_clk_src.clkr,
	[GCC_QUPV3_OOB_S1_CLK] = &gcc_qupv3_oob_s1_clk.clkr,
	[GCC_QUPV3_OOB_S1_CLK_SRC] = &gcc_qupv3_oob_s1_clk_src.clkr,
	[GCC_QUPV3_OOB_S_AHB_CLK] = &gcc_qupv3_oob_s_ahb_clk.clkr,
	[GCC_QUPV3_OOB_TCXO_CLK] = &gcc_qupv3_oob_tcxo_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_2X_CLK] = &gcc_qupv3_wrap0_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_CLK] = &gcc_qupv3_wrap0_core_clk.clkr,
	[GCC_QUPV3_WRAP0_QSPI_S2_CLK] = &gcc_qupv3_wrap0_qspi_s2_clk.clkr,
	[GCC_QUPV3_WRAP0_QSPI_S2_CLK_SRC] = &gcc_qupv3_wrap0_qspi_s2_clk_src.clkr,
	[GCC_QUPV3_WRAP0_QSPI_S3_CLK] = &gcc_qupv3_wrap0_qspi_s3_clk.clkr,
	[GCC_QUPV3_WRAP0_QSPI_S3_CLK_SRC] = &gcc_qupv3_wrap0_qspi_s3_clk_src.clkr,
	[GCC_QUPV3_WRAP0_QSPI_S6_CLK] = &gcc_qupv3_wrap0_qspi_s6_clk.clkr,
	[GCC_QUPV3_WRAP0_QSPI_S6_CLK_SRC] = &gcc_qupv3_wrap0_qspi_s6_clk_src.clkr,
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
	[GCC_QUPV3_WRAP0_S6_CLK] = &gcc_qupv3_wrap0_s6_clk.clkr,
	[GCC_QUPV3_WRAP0_S6_CLK_SRC] = &gcc_qupv3_wrap0_s6_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S7_CLK] = &gcc_qupv3_wrap0_s7_clk.clkr,
	[GCC_QUPV3_WRAP0_S7_CLK_SRC] = &gcc_qupv3_wrap0_s7_clk_src.clkr,
	[GCC_QUPV3_WRAP1_CORE_2X_CLK] = &gcc_qupv3_wrap1_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP1_CORE_CLK] = &gcc_qupv3_wrap1_core_clk.clkr,
	[GCC_QUPV3_WRAP1_QSPI_S2_CLK] = &gcc_qupv3_wrap1_qspi_s2_clk.clkr,
	[GCC_QUPV3_WRAP1_QSPI_S2_CLK_SRC] = &gcc_qupv3_wrap1_qspi_s2_clk_src.clkr,
	[GCC_QUPV3_WRAP1_QSPI_S3_CLK] = &gcc_qupv3_wrap1_qspi_s3_clk.clkr,
	[GCC_QUPV3_WRAP1_QSPI_S3_CLK_SRC] = &gcc_qupv3_wrap1_qspi_s3_clk_src.clkr,
	[GCC_QUPV3_WRAP1_QSPI_S6_CLK] = &gcc_qupv3_wrap1_qspi_s6_clk.clkr,
	[GCC_QUPV3_WRAP1_QSPI_S6_CLK_SRC] = &gcc_qupv3_wrap1_qspi_s6_clk_src.clkr,
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
	[GCC_QUPV3_WRAP1_S6_CLK] = &gcc_qupv3_wrap1_s6_clk.clkr,
	[GCC_QUPV3_WRAP1_S6_CLK_SRC] = &gcc_qupv3_wrap1_s6_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S7_CLK] = &gcc_qupv3_wrap1_s7_clk.clkr,
	[GCC_QUPV3_WRAP1_S7_CLK_SRC] = &gcc_qupv3_wrap1_s7_clk_src.clkr,
	[GCC_QUPV3_WRAP2_CORE_2X_CLK] = &gcc_qupv3_wrap2_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP2_CORE_CLK] = &gcc_qupv3_wrap2_core_clk.clkr,
	[GCC_QUPV3_WRAP2_QSPI_S2_CLK] = &gcc_qupv3_wrap2_qspi_s2_clk.clkr,
	[GCC_QUPV3_WRAP2_QSPI_S2_CLK_SRC] = &gcc_qupv3_wrap2_qspi_s2_clk_src.clkr,
	[GCC_QUPV3_WRAP2_QSPI_S3_CLK] = &gcc_qupv3_wrap2_qspi_s3_clk.clkr,
	[GCC_QUPV3_WRAP2_QSPI_S3_CLK_SRC] = &gcc_qupv3_wrap2_qspi_s3_clk_src.clkr,
	[GCC_QUPV3_WRAP2_QSPI_S6_CLK] = &gcc_qupv3_wrap2_qspi_s6_clk.clkr,
	[GCC_QUPV3_WRAP2_QSPI_S6_CLK_SRC] = &gcc_qupv3_wrap2_qspi_s6_clk_src.clkr,
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
	[GCC_QUPV3_WRAP2_S6_CLK] = &gcc_qupv3_wrap2_s6_clk.clkr,
	[GCC_QUPV3_WRAP2_S6_CLK_SRC] = &gcc_qupv3_wrap2_s6_clk_src.clkr,
	[GCC_QUPV3_WRAP2_S7_CLK] = &gcc_qupv3_wrap2_s7_clk.clkr,
	[GCC_QUPV3_WRAP2_S7_CLK_SRC] = &gcc_qupv3_wrap2_s7_clk_src.clkr,
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = &gcc_qupv3_wrap_0_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = &gcc_qupv3_wrap_0_s_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_M_AHB_CLK] = &gcc_qupv3_wrap_1_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_1_S_AHB_CLK] = &gcc_qupv3_wrap_1_s_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_2_M_AHB_CLK] = &gcc_qupv3_wrap_2_m_ahb_clk.clkr,
	[GCC_QUPV3_WRAP_2_S_AHB_CLK] = &gcc_qupv3_wrap_2_s_ahb_clk.clkr,
	[GCC_SDCC2_AHB_CLK] = &gcc_sdcc2_ahb_clk.clkr,
	[GCC_SDCC2_APPS_CLK] = &gcc_sdcc2_apps_clk.clkr,
	[GCC_SDCC2_APPS_CLK_SRC] = &gcc_sdcc2_apps_clk_src.clkr,
	[GCC_SDCC4_AHB_CLK] = &gcc_sdcc4_ahb_clk.clkr,
	[GCC_SDCC4_APPS_CLK] = &gcc_sdcc4_apps_clk.clkr,
	[GCC_SDCC4_APPS_CLK_SRC] = &gcc_sdcc4_apps_clk_src.clkr,
	[GCC_UFS_PHY_AHB_CLK] = &gcc_ufs_phy_ahb_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK] = &gcc_ufs_phy_axi_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK_SRC] = &gcc_ufs_phy_axi_clk_src.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK] = &gcc_ufs_phy_ice_core_clk.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK_SRC] = &gcc_ufs_phy_ice_core_clk_src.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK] = &gcc_ufs_phy_phy_aux_clk.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK_SRC] = &gcc_ufs_phy_phy_aux_clk_src.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK] = &gcc_ufs_phy_rx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK_SRC] = &gcc_ufs_phy_rx_symbol_0_clk_src.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_1_CLK] = &gcc_ufs_phy_rx_symbol_1_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_1_CLK_SRC] = &gcc_ufs_phy_rx_symbol_1_clk_src.clkr,
	[GCC_UFS_PHY_TX_SYMBOL_0_CLK] = &gcc_ufs_phy_tx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_TX_SYMBOL_0_CLK_SRC] = &gcc_ufs_phy_tx_symbol_0_clk_src.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK] = &gcc_ufs_phy_unipro_core_clk.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK_SRC] = &gcc_ufs_phy_unipro_core_clk_src.clkr,
	[GCC_USB20_MASTER_CLK] = &gcc_usb20_master_clk.clkr,
	[GCC_USB20_MASTER_CLK_SRC] = &gcc_usb20_master_clk_src.clkr,
	[GCC_USB20_MOCK_UTMI_CLK] = &gcc_usb20_mock_utmi_clk.clkr,
	[GCC_USB20_MOCK_UTMI_CLK_SRC] = &gcc_usb20_mock_utmi_clk_src.clkr,
	[GCC_USB20_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb20_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB20_SLEEP_CLK] = &gcc_usb20_sleep_clk.clkr,
	[GCC_USB30_MP_MASTER_CLK] = &gcc_usb30_mp_master_clk.clkr,
	[GCC_USB30_MP_MASTER_CLK_SRC] = &gcc_usb30_mp_master_clk_src.clkr,
	[GCC_USB30_MP_MOCK_UTMI_CLK] = &gcc_usb30_mp_mock_utmi_clk.clkr,
	[GCC_USB30_MP_MOCK_UTMI_CLK_SRC] = &gcc_usb30_mp_mock_utmi_clk_src.clkr,
	[GCC_USB30_MP_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb30_mp_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_MP_SLEEP_CLK] = &gcc_usb30_mp_sleep_clk.clkr,
	[GCC_USB30_PRIM_MASTER_CLK] = &gcc_usb30_prim_master_clk.clkr,
	[GCC_USB30_PRIM_MASTER_CLK_SRC] = &gcc_usb30_prim_master_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK] = &gcc_usb30_prim_mock_utmi_clk.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_CLK_SRC] = &gcc_usb30_prim_mock_utmi_clk_src.clkr,
	[GCC_USB30_PRIM_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb30_prim_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_PRIM_SLEEP_CLK] = &gcc_usb30_prim_sleep_clk.clkr,
	[GCC_USB30_SEC_MASTER_CLK] = &gcc_usb30_sec_master_clk.clkr,
	[GCC_USB30_SEC_MASTER_CLK_SRC] = &gcc_usb30_sec_master_clk_src.clkr,
	[GCC_USB30_SEC_MOCK_UTMI_CLK] = &gcc_usb30_sec_mock_utmi_clk.clkr,
	[GCC_USB30_SEC_MOCK_UTMI_CLK_SRC] = &gcc_usb30_sec_mock_utmi_clk_src.clkr,
	[GCC_USB30_SEC_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb30_sec_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_SEC_SLEEP_CLK] = &gcc_usb30_sec_sleep_clk.clkr,
	[GCC_USB30_TERT_MASTER_CLK] = &gcc_usb30_tert_master_clk.clkr,
	[GCC_USB30_TERT_MASTER_CLK_SRC] = &gcc_usb30_tert_master_clk_src.clkr,
	[GCC_USB30_TERT_MOCK_UTMI_CLK] = &gcc_usb30_tert_mock_utmi_clk.clkr,
	[GCC_USB30_TERT_MOCK_UTMI_CLK_SRC] = &gcc_usb30_tert_mock_utmi_clk_src.clkr,
	[GCC_USB30_TERT_MOCK_UTMI_POSTDIV_CLK_SRC] = &gcc_usb30_tert_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_TERT_SLEEP_CLK] = &gcc_usb30_tert_sleep_clk.clkr,
	[GCC_USB34_PRIM_PHY_PIPE_CLK_SRC] = &gcc_usb34_prim_phy_pipe_clk_src.clkr,
	[GCC_USB34_SEC_PHY_PIPE_CLK_SRC] = &gcc_usb34_sec_phy_pipe_clk_src.clkr,
	[GCC_USB34_TERT_PHY_PIPE_CLK_SRC] = &gcc_usb34_tert_phy_pipe_clk_src.clkr,
	[GCC_USB3_MP_PHY_AUX_CLK] = &gcc_usb3_mp_phy_aux_clk.clkr,
	[GCC_USB3_MP_PHY_AUX_CLK_SRC] = &gcc_usb3_mp_phy_aux_clk_src.clkr,
	[GCC_USB3_MP_PHY_COM_AUX_CLK] = &gcc_usb3_mp_phy_com_aux_clk.clkr,
	[GCC_USB3_MP_PHY_PIPE_0_CLK] = &gcc_usb3_mp_phy_pipe_0_clk.clkr,
	[GCC_USB3_MP_PHY_PIPE_0_CLK_SRC] = &gcc_usb3_mp_phy_pipe_0_clk_src.clkr,
	[GCC_USB3_MP_PHY_PIPE_1_CLK] = &gcc_usb3_mp_phy_pipe_1_clk.clkr,
	[GCC_USB3_MP_PHY_PIPE_1_CLK_SRC] = &gcc_usb3_mp_phy_pipe_1_clk_src.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK] = &gcc_usb3_prim_phy_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_AUX_CLK_SRC] = &gcc_usb3_prim_phy_aux_clk_src.clkr,
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = &gcc_usb3_prim_phy_com_aux_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = &gcc_usb3_prim_phy_pipe_clk.clkr,
	[GCC_USB3_PRIM_PHY_PIPE_CLK_SRC] = &gcc_usb3_prim_phy_pipe_clk_src.clkr,
	[GCC_USB3_SEC_PHY_AUX_CLK] = &gcc_usb3_sec_phy_aux_clk.clkr,
	[GCC_USB3_SEC_PHY_AUX_CLK_SRC] = &gcc_usb3_sec_phy_aux_clk_src.clkr,
	[GCC_USB3_SEC_PHY_COM_AUX_CLK] = &gcc_usb3_sec_phy_com_aux_clk.clkr,
	[GCC_USB3_SEC_PHY_PIPE_CLK] = &gcc_usb3_sec_phy_pipe_clk.clkr,
	[GCC_USB3_SEC_PHY_PIPE_CLK_SRC] = &gcc_usb3_sec_phy_pipe_clk_src.clkr,
	[GCC_USB3_TERT_PHY_AUX_CLK] = &gcc_usb3_tert_phy_aux_clk.clkr,
	[GCC_USB3_TERT_PHY_AUX_CLK_SRC] = &gcc_usb3_tert_phy_aux_clk_src.clkr,
	[GCC_USB3_TERT_PHY_COM_AUX_CLK] = &gcc_usb3_tert_phy_com_aux_clk.clkr,
	[GCC_USB3_TERT_PHY_PIPE_CLK] = &gcc_usb3_tert_phy_pipe_clk.clkr,
	[GCC_USB3_TERT_PHY_PIPE_CLK_SRC] = &gcc_usb3_tert_phy_pipe_clk_src.clkr,
	[GCC_USB4_0_CFG_AHB_CLK] = &gcc_usb4_0_cfg_ahb_clk.clkr,
	[GCC_USB4_0_DP0_CLK] = &gcc_usb4_0_dp0_clk.clkr,
	[GCC_USB4_0_DP1_CLK] = &gcc_usb4_0_dp1_clk.clkr,
	[GCC_USB4_0_MASTER_CLK] = &gcc_usb4_0_master_clk.clkr,
	[GCC_USB4_0_MASTER_CLK_SRC] = &gcc_usb4_0_master_clk_src.clkr,
	[GCC_USB4_0_PHY_DP0_CLK_SRC] = &gcc_usb4_0_phy_dp0_clk_src.clkr,
	[GCC_USB4_0_PHY_DP1_CLK_SRC] = &gcc_usb4_0_phy_dp1_clk_src.clkr,
	[GCC_USB4_0_PHY_P2RR2P_PIPE_CLK] = &gcc_usb4_0_phy_p2rr2p_pipe_clk.clkr,
	[GCC_USB4_0_PHY_P2RR2P_PIPE_CLK_SRC] = &gcc_usb4_0_phy_p2rr2p_pipe_clk_src.clkr,
	[GCC_USB4_0_PHY_PCIE_PIPE_CLK] = &gcc_usb4_0_phy_pcie_pipe_clk.clkr,
	[GCC_USB4_0_PHY_PCIE_PIPE_CLK_SRC] = &gcc_usb4_0_phy_pcie_pipe_clk_src.clkr,
	[GCC_USB4_0_PHY_PCIE_PIPE_MUX_CLK_SRC] = &gcc_usb4_0_phy_pcie_pipe_mux_clk_src.clkr,
	[GCC_USB4_0_PHY_RX0_CLK] = &gcc_usb4_0_phy_rx0_clk.clkr,
	[GCC_USB4_0_PHY_RX0_CLK_SRC] = &gcc_usb4_0_phy_rx0_clk_src.clkr,
	[GCC_USB4_0_PHY_RX1_CLK] = &gcc_usb4_0_phy_rx1_clk.clkr,
	[GCC_USB4_0_PHY_RX1_CLK_SRC] = &gcc_usb4_0_phy_rx1_clk_src.clkr,
	[GCC_USB4_0_PHY_SYS_CLK_SRC] = &gcc_usb4_0_phy_sys_clk_src.clkr,
	[GCC_USB4_0_PHY_USB_PIPE_CLK] = &gcc_usb4_0_phy_usb_pipe_clk.clkr,
	[GCC_USB4_0_SB_IF_CLK] = &gcc_usb4_0_sb_if_clk.clkr,
	[GCC_USB4_0_SB_IF_CLK_SRC] = &gcc_usb4_0_sb_if_clk_src.clkr,
	[GCC_USB4_0_SYS_CLK] = &gcc_usb4_0_sys_clk.clkr,
	[GCC_USB4_0_TMU_CLK] = &gcc_usb4_0_tmu_clk.clkr,
	[GCC_USB4_0_TMU_CLK_SRC] = &gcc_usb4_0_tmu_clk_src.clkr,
	[GCC_USB4_0_UC_HRR_CLK] = &gcc_usb4_0_uc_hrr_clk.clkr,
	[GCC_USB4_1_CFG_AHB_CLK] = &gcc_usb4_1_cfg_ahb_clk.clkr,
	[GCC_USB4_1_DP0_CLK] = &gcc_usb4_1_dp0_clk.clkr,
	[GCC_USB4_1_DP1_CLK] = &gcc_usb4_1_dp1_clk.clkr,
	[GCC_USB4_1_MASTER_CLK] = &gcc_usb4_1_master_clk.clkr,
	[GCC_USB4_1_MASTER_CLK_SRC] = &gcc_usb4_1_master_clk_src.clkr,
	[GCC_USB4_1_PHY_DP0_CLK_SRC] = &gcc_usb4_1_phy_dp0_clk_src.clkr,
	[GCC_USB4_1_PHY_DP1_CLK_SRC] = &gcc_usb4_1_phy_dp1_clk_src.clkr,
	[GCC_USB4_1_PHY_P2RR2P_PIPE_CLK] = &gcc_usb4_1_phy_p2rr2p_pipe_clk.clkr,
	[GCC_USB4_1_PHY_P2RR2P_PIPE_CLK_SRC] = &gcc_usb4_1_phy_p2rr2p_pipe_clk_src.clkr,
	[GCC_USB4_1_PHY_PCIE_PIPE_CLK] = &gcc_usb4_1_phy_pcie_pipe_clk.clkr,
	[GCC_USB4_1_PHY_PCIE_PIPE_CLK_SRC] = &gcc_usb4_1_phy_pcie_pipe_clk_src.clkr,
	[GCC_USB4_1_PHY_PCIE_PIPE_MUX_CLK_SRC] = &gcc_usb4_1_phy_pcie_pipe_mux_clk_src.clkr,
	[GCC_USB4_1_PHY_PLL_PIPE_CLK_SRC] = &gcc_usb4_1_phy_pll_pipe_clk_src.clkr,
	[GCC_USB4_1_PHY_RX0_CLK] = &gcc_usb4_1_phy_rx0_clk.clkr,
	[GCC_USB4_1_PHY_RX0_CLK_SRC] = &gcc_usb4_1_phy_rx0_clk_src.clkr,
	[GCC_USB4_1_PHY_RX1_CLK] = &gcc_usb4_1_phy_rx1_clk.clkr,
	[GCC_USB4_1_PHY_RX1_CLK_SRC] = &gcc_usb4_1_phy_rx1_clk_src.clkr,
	[GCC_USB4_1_PHY_SYS_CLK_SRC] = &gcc_usb4_1_phy_sys_clk_src.clkr,
	[GCC_USB4_1_PHY_USB_PIPE_CLK] = &gcc_usb4_1_phy_usb_pipe_clk.clkr,
	[GCC_USB4_1_SB_IF_CLK] = &gcc_usb4_1_sb_if_clk.clkr,
	[GCC_USB4_1_SB_IF_CLK_SRC] = &gcc_usb4_1_sb_if_clk_src.clkr,
	[GCC_USB4_1_SYS_CLK] = &gcc_usb4_1_sys_clk.clkr,
	[GCC_USB4_1_TMU_CLK] = &gcc_usb4_1_tmu_clk.clkr,
	[GCC_USB4_1_TMU_CLK_SRC] = &gcc_usb4_1_tmu_clk_src.clkr,
	[GCC_USB4_1_UC_HRR_CLK] = &gcc_usb4_1_uc_hrr_clk.clkr,
	[GCC_USB4_2_CFG_AHB_CLK] = &gcc_usb4_2_cfg_ahb_clk.clkr,
	[GCC_USB4_2_DP0_CLK] = &gcc_usb4_2_dp0_clk.clkr,
	[GCC_USB4_2_DP1_CLK] = &gcc_usb4_2_dp1_clk.clkr,
	[GCC_USB4_2_MASTER_CLK] = &gcc_usb4_2_master_clk.clkr,
	[GCC_USB4_2_MASTER_CLK_SRC] = &gcc_usb4_2_master_clk_src.clkr,
	[GCC_USB4_2_PHY_DP0_CLK_SRC] = &gcc_usb4_2_phy_dp0_clk_src.clkr,
	[GCC_USB4_2_PHY_DP1_CLK_SRC] = &gcc_usb4_2_phy_dp1_clk_src.clkr,
	[GCC_USB4_2_PHY_P2RR2P_PIPE_CLK] = &gcc_usb4_2_phy_p2rr2p_pipe_clk.clkr,
	[GCC_USB4_2_PHY_P2RR2P_PIPE_CLK_SRC] = &gcc_usb4_2_phy_p2rr2p_pipe_clk_src.clkr,
	[GCC_USB4_2_PHY_PCIE_PIPE_CLK] = &gcc_usb4_2_phy_pcie_pipe_clk.clkr,
	[GCC_USB4_2_PHY_PCIE_PIPE_CLK_SRC] = &gcc_usb4_2_phy_pcie_pipe_clk_src.clkr,
	[GCC_USB4_2_PHY_PCIE_PIPE_MUX_CLK_SRC] = &gcc_usb4_2_phy_pcie_pipe_mux_clk_src.clkr,
	[GCC_USB4_2_PHY_RX0_CLK] = &gcc_usb4_2_phy_rx0_clk.clkr,
	[GCC_USB4_2_PHY_RX0_CLK_SRC] = &gcc_usb4_2_phy_rx0_clk_src.clkr,
	[GCC_USB4_2_PHY_RX1_CLK] = &gcc_usb4_2_phy_rx1_clk.clkr,
	[GCC_USB4_2_PHY_RX1_CLK_SRC] = &gcc_usb4_2_phy_rx1_clk_src.clkr,
	[GCC_USB4_2_PHY_SYS_CLK_SRC] = &gcc_usb4_2_phy_sys_clk_src.clkr,
	[GCC_USB4_2_PHY_USB_PIPE_CLK] = &gcc_usb4_2_phy_usb_pipe_clk.clkr,
	[GCC_USB4_2_SB_IF_CLK] = &gcc_usb4_2_sb_if_clk.clkr,
	[GCC_USB4_2_SB_IF_CLK_SRC] = &gcc_usb4_2_sb_if_clk_src.clkr,
	[GCC_USB4_2_SYS_CLK] = &gcc_usb4_2_sys_clk.clkr,
	[GCC_USB4_2_TMU_CLK] = &gcc_usb4_2_tmu_clk.clkr,
	[GCC_USB4_2_TMU_CLK_SRC] = &gcc_usb4_2_tmu_clk_src.clkr,
	[GCC_USB4_2_UC_HRR_CLK] = &gcc_usb4_2_uc_hrr_clk.clkr,
	[GCC_VIDEO_AXI0_CLK] = &gcc_video_axi0_clk.clkr,
	[GCC_VIDEO_AXI0C_CLK] = &gcc_video_axi0c_clk.clkr,
	[GCC_VIDEO_AXI1_CLK] = &gcc_video_axi1_clk.clkr,
};

static struct gdsc *gcc_glymur_gdscs[] = {
	[GCC_PCIE_0_TUNNEL_GDSC] = &gcc_pcie_0_tunnel_gdsc,
	[GCC_PCIE_1_TUNNEL_GDSC] = &gcc_pcie_1_tunnel_gdsc,
	[GCC_PCIE_2_TUNNEL_GDSC] = &gcc_pcie_2_tunnel_gdsc,
	[GCC_PCIE_3A_GDSC] = &gcc_pcie_3a_gdsc,
	[GCC_PCIE_3A_PHY_GDSC] = &gcc_pcie_3a_phy_gdsc,
	[GCC_PCIE_3B_GDSC] = &gcc_pcie_3b_gdsc,
	[GCC_PCIE_3B_PHY_GDSC] = &gcc_pcie_3b_phy_gdsc,
	[GCC_PCIE_4_GDSC] = &gcc_pcie_4_gdsc,
	[GCC_PCIE_4_PHY_GDSC] = &gcc_pcie_4_phy_gdsc,
	[GCC_PCIE_5_GDSC] = &gcc_pcie_5_gdsc,
	[GCC_PCIE_5_PHY_GDSC] = &gcc_pcie_5_phy_gdsc,
	[GCC_PCIE_6_GDSC] = &gcc_pcie_6_gdsc,
	[GCC_PCIE_6_PHY_GDSC] = &gcc_pcie_6_phy_gdsc,
	[GCC_UFS_PHY_GDSC] = &gcc_ufs_phy_gdsc,
	[GCC_USB20_PRIM_GDSC] = &gcc_usb20_prim_gdsc,
	[GCC_USB30_MP_GDSC] = &gcc_usb30_mp_gdsc,
	[GCC_USB30_PRIM_GDSC] = &gcc_usb30_prim_gdsc,
	[GCC_USB30_SEC_GDSC] = &gcc_usb30_sec_gdsc,
	[GCC_USB30_TERT_GDSC] = &gcc_usb30_tert_gdsc,
	[GCC_USB3_MP_SS0_PHY_GDSC] = &gcc_usb3_mp_ss0_phy_gdsc,
	[GCC_USB3_MP_SS1_PHY_GDSC] = &gcc_usb3_mp_ss1_phy_gdsc,
	[GCC_USB4_0_GDSC] = &gcc_usb4_0_gdsc,
	[GCC_USB4_1_GDSC] = &gcc_usb4_1_gdsc,
	[GCC_USB4_2_GDSC] = &gcc_usb4_2_gdsc,
	[GCC_USB_0_PHY_GDSC] = &gcc_usb_0_phy_gdsc,
	[GCC_USB_1_PHY_GDSC] = &gcc_usb_1_phy_gdsc,
	[GCC_USB_2_PHY_GDSC] = &gcc_usb_2_phy_gdsc,
};

static const struct qcom_reset_map gcc_glymur_resets[] = {
	[GCC_AV1E_BCR] = { 0x9b028 },
	[GCC_CAMERA_BCR] = { 0x26000 },
	[GCC_DISPLAY_BCR] = { 0x27000 },
	[GCC_EVA_BCR] = { 0x9b000 },
	[GCC_GPU_BCR] = { 0x71000 },
	[GCC_PCIE_0_LINK_DOWN_BCR] = { 0xbc2d0 },
	[GCC_PCIE_0_NOCSR_COM_PHY_BCR] = { 0xbc2dc },
	[GCC_PCIE_0_PHY_BCR] = { 0xbc2d8 },
	[GCC_PCIE_0_PHY_NOCSR_COM_PHY_BCR] = { 0xbc2e0 },
	[GCC_PCIE_0_TUNNEL_BCR] = { 0xc8000 },
	[GCC_PCIE_1_LINK_DOWN_BCR] = { 0x7f018 },
	[GCC_PCIE_1_NOCSR_COM_PHY_BCR] = { 0x7f024 },
	[GCC_PCIE_1_PHY_BCR] = { 0x7f020 },
	[GCC_PCIE_1_PHY_NOCSR_COM_PHY_BCR] = { 0x7f028 },
	[GCC_PCIE_1_TUNNEL_BCR] = { 0x2e000 },
	[GCC_PCIE_2_LINK_DOWN_BCR] = { 0x281d0 },
	[GCC_PCIE_2_NOCSR_COM_PHY_BCR] = { 0x281dc },
	[GCC_PCIE_2_PHY_BCR] = { 0x281d8 },
	[GCC_PCIE_2_PHY_NOCSR_COM_PHY_BCR] = { 0x281e0 },
	[GCC_PCIE_2_TUNNEL_BCR] = { 0xc0000 },
	[GCC_PCIE_3A_BCR] = { 0xdc000 },
	[GCC_PCIE_3A_LINK_DOWN_BCR] = { 0x7b0a0 },
	[GCC_PCIE_3A_NOCSR_COM_PHY_BCR] = { 0x7b0ac },
	[GCC_PCIE_3A_PHY_BCR] = { 0x6c000 },
	[GCC_PCIE_3A_PHY_NOCSR_COM_PHY_BCR] = { 0x7b0b0 },
	[GCC_PCIE_3B_BCR] = { 0x94000 },
	[GCC_PCIE_3B_LINK_DOWN_BCR] = { 0x7a0c0 },
	[GCC_PCIE_3B_NOCSR_COM_PHY_BCR] = { 0x7a0cc },
	[GCC_PCIE_3B_PHY_BCR] = { 0x75000 },
	[GCC_PCIE_3B_PHY_NOCSR_COM_PHY_BCR] = { 0x7a0c8 },
	[GCC_PCIE_4_BCR] = { 0x88000 },
	[GCC_PCIE_4_LINK_DOWN_BCR] = { 0x980c0 },
	[GCC_PCIE_4_NOCSR_COM_PHY_BCR] = { 0x980cc },
	[GCC_PCIE_4_PHY_BCR] = { 0xd3000 },
	[GCC_PCIE_4_PHY_NOCSR_COM_PHY_BCR] = { 0x980d0 },
	[GCC_PCIE_5_BCR] = { 0xc3000 },
	[GCC_PCIE_5_LINK_DOWN_BCR] = { 0x850c0 },
	[GCC_PCIE_5_NOCSR_COM_PHY_BCR] = { 0x850cc },
	[GCC_PCIE_5_PHY_BCR] = { 0xd2000 },
	[GCC_PCIE_5_PHY_NOCSR_COM_PHY_BCR] = { 0x850d0 },
	[GCC_PCIE_6_BCR] = { 0x8a000 },
	[GCC_PCIE_6_LINK_DOWN_BCR] = { 0x3a0b0 },
	[GCC_PCIE_6_NOCSR_COM_PHY_BCR] = { 0x3a0bc },
	[GCC_PCIE_6_PHY_BCR] = { 0xd4000 },
	[GCC_PCIE_6_PHY_NOCSR_COM_PHY_BCR] = { 0x3a0c0 },
	[GCC_PCIE_NOC_BCR] = { 0xba294 },
	[GCC_PCIE_PHY_BCR] = { 0x6f000 },
	[GCC_PCIE_PHY_CFG_AHB_BCR] = { 0x7f00c },
	[GCC_PCIE_PHY_COM_BCR] = { 0x7f010 },
	[GCC_PCIE_RSCC_BCR] = { 0xb8000 },
	[GCC_PDM_BCR] = { 0x33000 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x28000 },
	[GCC_QUPV3_WRAPPER_1_BCR] = { 0xb3000 },
	[GCC_QUPV3_WRAPPER_2_BCR] = { 0xb4000 },
	[GCC_QUPV3_WRAPPER_OOB_BCR] = { 0xe7000 },
	[GCC_QUSB2PHY_HS0_MP_BCR] = { 0xca000 },
	[GCC_QUSB2PHY_HS1_MP_BCR] = { 0xe6000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0xad024 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0xae000 },
	[GCC_QUSB2PHY_TERT_BCR] = { 0xc9000 },
	[GCC_QUSB2PHY_USB20_HS_BCR] = { 0xe9000 },
	[GCC_SDCC2_BCR] = { 0xb0000 },
	[GCC_SDCC4_BCR] = { 0xdf000 },
	[GCC_TCSR_PCIE_BCR] = { 0x281e4 },
	[GCC_UFS_PHY_BCR] = { 0x77004 },
	[GCC_USB20_PRIM_BCR] = { 0xbc000 },
	[GCC_USB30_MP_BCR] = { 0x9a00c },
	[GCC_USB30_PRIM_BCR] = { 0x3f018 },
	[GCC_USB30_SEC_BCR] = { 0xe200c },
	[GCC_USB30_TERT_BCR] = { 0xe100c },
	[GCC_USB3_MP_SS0_PHY_BCR] = { 0x54008 },
	[GCC_USB3_MP_SS1_PHY_BCR] = { 0x54028 },
	[GCC_USB3_PHY_PRIM_BCR] = { 0xdb000 },
	[GCC_USB3_PHY_SEC_BCR] = { 0x2c000 },
	[GCC_USB3_PHY_TERT_BCR] = { 0xbe000 },
	[GCC_USB3_UNIPHY_MP0_BCR] = { 0x54000 },
	[GCC_USB3_UNIPHY_MP1_BCR] = { 0x54020 },
	[GCC_USB3PHY_PHY_PRIM_BCR] = { 0xdb004 },
	[GCC_USB3PHY_PHY_SEC_BCR] = { 0x2c004 },
	[GCC_USB3PHY_PHY_TERT_BCR] = { 0xbe004 },
	[GCC_USB3UNIPHY_PHY_MP0_BCR] = { 0x54004 },
	[GCC_USB3UNIPHY_PHY_MP1_BCR] = { 0x54024 },
	[GCC_USB4_0_BCR] = { 0x2b004 },
	[GCC_USB4_0_DP0_PHY_PRIM_BCR] = { 0xdb010 },
	[GCC_USB4_1_BCR] = { 0x2d004 },
	[GCC_USB4_2_BCR] = { 0xe0004 },
	[GCC_USB_0_PHY_BCR] = { 0xdb020 },
	[GCC_USB_1_PHY_BCR] = { 0x2c020 },
	[GCC_USB_2_PHY_BCR] = { 0xbe020 },
	[GCC_VIDEO_AXI0_CLK_ARES] = { 0x3201c, 2 },
	[GCC_VIDEO_AXI1_CLK_ARES] = { 0x32044, 2 },
	[GCC_VIDEO_BCR] = { 0x32000 },
};

static const struct clk_rcg_dfs_data gcc_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_oob_qspi_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_oob_qspi_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_qspi_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_qspi_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_qspi_s6_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s7_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_qspi_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_qspi_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_qspi_s6_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s7_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_qspi_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_qspi_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_qspi_s6_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s7_clk_src),
};

static u32 gcc_glymur_critical_cbcrs[] = {
	0x26004, /* GCC_CAMERA_AHB_CLK */
	0x26040, /* GCC_CAMERA_XO_CLK */
	0x27004, /* GCC_DISP_AHB_CLK */
	0x71004, /* GCC_GPU_CFG_AHB_CLK */
	0x32004, /* GCC_VIDEO_AHB_CLK */
	0x32058, /* GCC_VIDEO_XO_CLK */
};

static const struct regmap_config gcc_glymur_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1f8ff0,
	.fast_io = true,
};

static void clk_glymur_regs_configure(struct device *dev, struct regmap *regmap)
{
	/* FORCE_MEM_CORE_ON for ufs phy ice core clocks */
	qcom_branch_set_force_mem_core(regmap, gcc_ufs_phy_ice_core_clk, true);
}

static struct qcom_cc_driver_data gcc_glymur_driver_data = {
	.clk_cbcrs = gcc_glymur_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(gcc_glymur_critical_cbcrs),
	.dfs_rcgs = gcc_dfs_clocks,
	.num_dfs_rcgs = ARRAY_SIZE(gcc_dfs_clocks),
	.clk_regs_configure = clk_glymur_regs_configure,
};

static const struct qcom_cc_desc gcc_glymur_desc = {
	.config = &gcc_glymur_regmap_config,
	.clks = gcc_glymur_clocks,
	.num_clks = ARRAY_SIZE(gcc_glymur_clocks),
	.resets = gcc_glymur_resets,
	.num_resets = ARRAY_SIZE(gcc_glymur_resets),
	.gdscs = gcc_glymur_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_glymur_gdscs),
	.driver_data = &gcc_glymur_driver_data,
};

static const struct of_device_id gcc_glymur_match_table[] = {
	{ .compatible = "qcom,glymur-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_glymur_match_table);

static int gcc_glymur_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gcc_glymur_desc);
}

static struct platform_driver gcc_glymur_driver = {
	.probe = gcc_glymur_probe,
	.driver = {
		.name = "gcc-glymur",
		.of_match_table = gcc_glymur_match_table,
	},
};

static int __init gcc_glymur_init(void)
{
	return platform_driver_register(&gcc_glymur_driver);
}
subsys_initcall(gcc_glymur_init);

static void __exit gcc_glymur_exit(void)
{
	platform_driver_unregister(&gcc_glymur_driver);
}
module_exit(gcc_glymur_exit);

MODULE_DESCRIPTION("QTI GCC GLYMUR Driver");
MODULE_LICENSE("GPL");
