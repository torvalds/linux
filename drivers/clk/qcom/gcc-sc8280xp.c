// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Ltd.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gcc-sc8280xp.h>

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
	DT_SLEEP_CLK,
	DT_UFS_PHY_RX_SYMBOL_0_CLK,
	DT_UFS_PHY_RX_SYMBOL_1_CLK,
	DT_UFS_PHY_TX_SYMBOL_0_CLK,
	DT_UFS_CARD_RX_SYMBOL_0_CLK,
	DT_UFS_CARD_RX_SYMBOL_1_CLK,
	DT_UFS_CARD_TX_SYMBOL_0_CLK,
	DT_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
	DT_GCC_USB4_PHY_PIPEGMUX_CLK_SRC,
	DT_GCC_USB4_PHY_DP_GMUX_CLK_SRC,
	DT_GCC_USB4_PHY_SYS_PIPEGMUX_CLK_SRC,
	DT_USB4_PHY_GCC_USB4_PCIE_PIPE_CLK,
	DT_USB4_PHY_GCC_USB4RTR_MAX_PIPE_CLK,
	DT_QUSB4PHY_GCC_USB4_RX0_CLK,
	DT_QUSB4PHY_GCC_USB4_RX1_CLK,
	DT_USB3_UNI_PHY_SEC_GCC_USB30_PIPE_CLK,
	DT_GCC_USB4_1_PHY_PIPEGMUX_CLK_SRC,
	DT_GCC_USB4_1_PHY_DP_GMUX_CLK_SRC,
	DT_GCC_USB4_1_PHY_SYS_PIPEGMUX_CLK_SRC,
	DT_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK,
	DT_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK,
	DT_QUSB4PHY_1_GCC_USB4_RX0_CLK,
	DT_QUSB4PHY_1_GCC_USB4_RX1_CLK,
	DT_USB3_UNI_PHY_MP_GCC_USB30_PIPE_0_CLK,
	DT_USB3_UNI_PHY_MP_GCC_USB30_PIPE_1_CLK,
	DT_PCIE_2A_PIPE_CLK,
	DT_PCIE_2B_PIPE_CLK,
	DT_PCIE_3A_PIPE_CLK,
	DT_PCIE_3B_PIPE_CLK,
	DT_PCIE_4_PIPE_CLK,
	DT_RXC0_REF_CLK,
	DT_RXC1_REF_CLK,
};

enum {
	P_BI_TCXO,
	P_GCC_GPLL0_OUT_EVEN,
	P_GCC_GPLL0_OUT_MAIN,
	P_GCC_GPLL2_OUT_MAIN,
	P_GCC_GPLL4_OUT_MAIN,
	P_GCC_GPLL7_OUT_MAIN,
	P_GCC_GPLL8_OUT_MAIN,
	P_GCC_GPLL9_OUT_MAIN,
	P_GCC_USB3_PRIM_PHY_PIPE_CLK_SRC,
	P_GCC_USB3_SEC_PHY_PIPE_CLK_SRC,
	P_GCC_USB4_1_PHY_DP_GMUX_CLK_SRC,
	P_GCC_USB4_1_PHY_PCIE_PIPE_CLK_SRC,
	P_GCC_USB4_1_PHY_PCIE_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_1_PHY_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_1_PHY_SYS_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_PHY_DP_GMUX_CLK_SRC,
	P_GCC_USB4_PHY_PCIE_PIPE_CLK_SRC,
	P_GCC_USB4_PHY_PCIE_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_PHY_PIPEGMUX_CLK_SRC,
	P_GCC_USB4_PHY_SYS_PIPEGMUX_CLK_SRC,
	P_QUSB4PHY_1_GCC_USB4_RX0_CLK,
	P_QUSB4PHY_1_GCC_USB4_RX1_CLK,
	P_QUSB4PHY_GCC_USB4_RX0_CLK,
	P_QUSB4PHY_GCC_USB4_RX1_CLK,
	P_RXC0_REF_CLK,
	P_RXC1_REF_CLK,
	P_SLEEP_CLK,
	P_UFS_CARD_RX_SYMBOL_0_CLK,
	P_UFS_CARD_RX_SYMBOL_1_CLK,
	P_UFS_CARD_TX_SYMBOL_0_CLK,
	P_UFS_PHY_RX_SYMBOL_0_CLK,
	P_UFS_PHY_RX_SYMBOL_1_CLK,
	P_UFS_PHY_TX_SYMBOL_0_CLK,
	P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
	P_USB3_UNI_PHY_MP_GCC_USB30_PIPE_0_CLK,
	P_USB3_UNI_PHY_MP_GCC_USB30_PIPE_1_CLK,
	P_USB3_UNI_PHY_SEC_GCC_USB30_PIPE_CLK,
	P_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK,
	P_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK,
	P_USB4_PHY_GCC_USB4_PCIE_PIPE_CLK,
	P_USB4_PHY_GCC_USB4RTR_MAX_PIPE_CLK,
};

static const struct clk_parent_data gcc_parent_data_tcxo = { .index = DT_BI_TCXO };

static struct clk_alpha_pll gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.enable_reg = 0x52028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll0",
			.parent_data = &gcc_parent_data_tcxo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_5lpe_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gcc_gpll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gcc_gpll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gcc_gpll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gcc_gpll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_5lpe_ops,
	},
};

static struct clk_alpha_pll gcc_gpll2 = {
	.offset = 0x2000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.enable_reg = 0x52028,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll2",
			.parent_data = &gcc_parent_data_tcxo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_5lpe_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll4 = {
	.offset = 0x76000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.enable_reg = 0x52028,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll4",
			.parent_data = &gcc_parent_data_tcxo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_5lpe_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll7 = {
	.offset = 0x1a000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.enable_reg = 0x52028,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll7",
			.parent_data = &gcc_parent_data_tcxo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_5lpe_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll8 = {
	.offset = 0x1b000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.enable_reg = 0x52028,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll8",
			.parent_data = &gcc_parent_data_tcxo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_5lpe_ops,
		},
	},
};

static struct clk_alpha_pll gcc_gpll9 = {
	.offset = 0x1c000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.enable_reg = 0x52028,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpll9",
			.parent_data = &gcc_parent_data_tcxo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_5lpe_ops,
		},
	},
};

static struct clk_rcg2 gcc_usb4_1_phy_pcie_pipe_clk_src;
static struct clk_rcg2 gcc_usb4_phy_pcie_pipe_clk_src;

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
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL8_OUT_MAIN, 2 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll8.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll7.clkr.hw },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL2_OUT_MAIN, 2 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll2.clkr.hw },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
	{ P_RXC0_REF_CLK, 3 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .index = DT_RXC0_REF_CLK },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL7_OUT_MAIN, 2 },
	{ P_RXC1_REF_CLK, 3 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_9[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll7.clkr.hw },
	{ .index = DT_RXC1_REF_CLK },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_15[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL9_OUT_MAIN, 2 },
	{ P_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_15[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gcc_gpll0.clkr.hw },
	{ .hw = &gcc_gpll9.clkr.hw },
	{ .hw = &gcc_gpll4.clkr.hw },
	{ .hw = &gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_16[] = {
	{ P_UFS_CARD_RX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_16[] = {
	{ .index = DT_UFS_CARD_RX_SYMBOL_0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_17[] = {
	{ P_UFS_CARD_RX_SYMBOL_1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_17[] = {
	{ .index = DT_UFS_CARD_RX_SYMBOL_1_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_18[] = {
	{ P_UFS_CARD_TX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_18[] = {
	{ .index = DT_UFS_CARD_TX_SYMBOL_0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_19[] = {
	{ P_UFS_PHY_RX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_19[] = {
	{ .index = DT_UFS_PHY_RX_SYMBOL_0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_20[] = {
	{ P_UFS_PHY_RX_SYMBOL_1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_20[] = {
	{ .index = DT_UFS_PHY_RX_SYMBOL_1_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_21[] = {
	{ P_UFS_PHY_TX_SYMBOL_0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_21[] = {
	{ .index = DT_UFS_PHY_TX_SYMBOL_0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_22[] = {
	{ P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_22[] = {
	{ .index = DT_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_23[] = {
	{ P_USB3_UNI_PHY_SEC_GCC_USB30_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_23[] = {
	{ .index = DT_USB3_UNI_PHY_SEC_GCC_USB30_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static struct clk_regmap_mux gcc_usb3_prim_phy_pipe_clk_src = {
	.reg = 0xf060,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_22,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_22,
			.num_parents = ARRAY_SIZE(gcc_parent_data_22),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_sec_phy_pipe_clk_src = {
	.reg = 0x10060,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_23,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_sec_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_23,
			.num_parents = ARRAY_SIZE(gcc_parent_data_23),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
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
	{ P_GCC_USB3_PRIM_PHY_PIPE_CLK_SRC, 0 },
	{ P_USB4_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 1 },
	{ P_GCC_USB4_PHY_PIPEGMUX_CLK_SRC, 3 },
};

static const struct clk_parent_data gcc_parent_data_26[] = {
	{ .hw = &gcc_usb3_prim_phy_pipe_clk_src.clkr.hw },
	{ .index = DT_USB4_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
	{ .index = DT_GCC_USB4_PHY_PIPEGMUX_CLK_SRC },
};

static const struct parent_map gcc_parent_map_27[] = {
	{ P_GCC_USB3_SEC_PHY_PIPE_CLK_SRC, 0 },
	{ P_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 1 },
	{ P_GCC_USB4_1_PHY_PIPEGMUX_CLK_SRC, 3 },
};

static const struct clk_parent_data gcc_parent_data_27[] = {
	{ .hw = &gcc_usb3_sec_phy_pipe_clk_src.clkr.hw },
	{ .index = DT_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
	{ .index = DT_GCC_USB4_1_PHY_PIPEGMUX_CLK_SRC },
};

static const struct parent_map gcc_parent_map_28[] = {
	{ P_GCC_USB4_1_PHY_DP_GMUX_CLK_SRC, 0 },
	{ P_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_28[] = {
	{ .index = DT_GCC_USB4_1_PHY_DP_GMUX_CLK_SRC },
	{ .index = DT_USB4_1_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_29[] = {
	{ P_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_29[] = {
	{ .index = DT_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_30[] = {
	{ P_GCC_USB4_1_PHY_SYS_PIPEGMUX_CLK_SRC, 0 },
	{ P_GCC_USB4_1_PHY_PCIE_PIPE_CLK_SRC, 1 },
};

static const struct clk_parent_data gcc_parent_data_30[] = {
	{ .index = DT_GCC_USB4_1_PHY_SYS_PIPEGMUX_CLK_SRC },
	{ .hw = &gcc_usb4_1_phy_pcie_pipe_clk_src.clkr.hw },
};

static struct clk_regmap_mux gcc_usb4_1_phy_pcie_pipegmux_clk_src = {
	.reg = 0xb80dc,
	.shift = 0,
	.width = 1,
	.parent_map = gcc_parent_map_30,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_pcie_pipegmux_clk_src",
			.parent_data = gcc_parent_data_30,
			.num_parents = ARRAY_SIZE(gcc_parent_data_30),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static const struct parent_map gcc_parent_map_31[] = {
	{ P_GCC_USB4_1_PHY_PCIE_PIPEGMUX_CLK_SRC, 0 },
	{ P_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_31[] = {
	{ .hw = &gcc_usb4_1_phy_pcie_pipegmux_clk_src.clkr.hw },
	{ .index = DT_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_32[] = {
	{ P_QUSB4PHY_1_GCC_USB4_RX0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_32[] = {
	{ .index = DT_QUSB4PHY_1_GCC_USB4_RX0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_33[] = {
	{ P_QUSB4PHY_1_GCC_USB4_RX1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_33[] = {
	{ .index = DT_QUSB4PHY_1_GCC_USB4_RX1_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_34[] = {
	{ P_GCC_USB4_1_PHY_SYS_PIPEGMUX_CLK_SRC, 0 },
	{ P_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_34[] = {
	{ .index = DT_GCC_USB4_1_PHY_SYS_PIPEGMUX_CLK_SRC },
	{ .index = DT_USB4_1_PHY_GCC_USB4_PCIE_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_35[] = {
	{ P_GCC_USB4_PHY_DP_GMUX_CLK_SRC, 0 },
	{ P_USB4_PHY_GCC_USB4RTR_MAX_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_35[] = {
	{ .index = DT_GCC_USB4_PHY_DP_GMUX_CLK_SRC },
	{ .index = DT_USB4_PHY_GCC_USB4RTR_MAX_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_36[] = {
	{ P_USB4_PHY_GCC_USB4_PCIE_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_36[] = {
	{ .index = DT_USB4_PHY_GCC_USB4_PCIE_PIPE_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_37[] = {
	{ P_GCC_USB4_PHY_SYS_PIPEGMUX_CLK_SRC, 0 },
	{ P_GCC_USB4_PHY_PCIE_PIPE_CLK_SRC, 1 },
};

static const struct clk_parent_data gcc_parent_data_37[] = {
	{ .index = DT_GCC_USB4_PHY_SYS_PIPEGMUX_CLK_SRC },
	{ .hw = &gcc_usb4_phy_pcie_pipe_clk_src.clkr.hw },
};

static struct clk_regmap_mux gcc_usb4_phy_pcie_pipegmux_clk_src = {
	.reg = 0x2a0dc,
	.shift = 0,
	.width = 1,
	.parent_map = gcc_parent_map_37,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_pcie_pipegmux_clk_src",
			.parent_data = gcc_parent_data_37,
			.num_parents = ARRAY_SIZE(gcc_parent_data_37),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static const struct parent_map gcc_parent_map_38[] = {
	{ P_GCC_USB4_PHY_PCIE_PIPEGMUX_CLK_SRC, 0 },
	{ P_USB4_PHY_GCC_USB4_PCIE_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_38[] = {
	{ .hw = &gcc_usb4_phy_pcie_pipegmux_clk_src.clkr.hw },
	{ .index = DT_USB4_PHY_GCC_USB4_PCIE_PIPE_CLK },
};

static const struct parent_map gcc_parent_map_39[] = {
	{ P_QUSB4PHY_GCC_USB4_RX0_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_39[] = {
	{ .index = DT_QUSB4PHY_GCC_USB4_RX0_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_40[] = {
	{ P_QUSB4PHY_GCC_USB4_RX1_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_40[] = {
	{ .index = DT_QUSB4PHY_GCC_USB4_RX1_CLK },
	{ .index = DT_BI_TCXO },
};

static const struct parent_map gcc_parent_map_41[] = {
	{ P_GCC_USB4_PHY_SYS_PIPEGMUX_CLK_SRC, 0 },
	{ P_USB4_PHY_GCC_USB4_PCIE_PIPE_CLK, 2 },
};

static const struct clk_parent_data gcc_parent_data_41[] = {
	{ .index = DT_GCC_USB4_PHY_SYS_PIPEGMUX_CLK_SRC },
	{ .index = DT_USB4_PHY_GCC_USB4_PCIE_PIPE_CLK },
};

static struct clk_regmap_phy_mux gcc_pcie_2a_pipe_clk_src = {
	.reg = 0x9d05c,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2a_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_PCIE_2A_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_2b_pipe_clk_src = {
	.reg = 0x9e05c,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2b_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_PCIE_2B_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie_3a_pipe_clk_src = {
	.reg = 0xa005c,
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
	.reg = 0xa205c,
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
	.reg = 0x6b05c,
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

static struct clk_regmap_mux gcc_ufs_card_rx_symbol_0_clk_src = {
	.reg = 0x75058,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_16,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_rx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_16,
			.num_parents = ARRAY_SIZE(gcc_parent_data_16),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_card_rx_symbol_1_clk_src = {
	.reg = 0x750c8,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_17,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_rx_symbol_1_clk_src",
			.parent_data = gcc_parent_data_17,
			.num_parents = ARRAY_SIZE(gcc_parent_data_17),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_card_tx_symbol_0_clk_src = {
	.reg = 0x75048,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_18,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_tx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_18,
			.num_parents = ARRAY_SIZE(gcc_parent_data_18),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_rx_symbol_0_clk_src = {
	.reg = 0x77058,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_19,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_19,
			.num_parents = ARRAY_SIZE(gcc_parent_data_19),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_rx_symbol_1_clk_src = {
	.reg = 0x770c8,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_20,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_1_clk_src",
			.parent_data = gcc_parent_data_20,
			.num_parents = ARRAY_SIZE(gcc_parent_data_20),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_ufs_phy_tx_symbol_0_clk_src = {
	.reg = 0x77048,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_21,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_tx_symbol_0_clk_src",
			.parent_data = gcc_parent_data_21,
			.num_parents = ARRAY_SIZE(gcc_parent_data_21),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb34_prim_phy_pipe_clk_src = {
	.reg = 0xf064,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_26,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb34_prim_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_26,
			.num_parents = ARRAY_SIZE(gcc_parent_data_26),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb34_sec_phy_pipe_clk_src = {
	.reg = 0x10064,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_27,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb34_sec_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_27,
			.num_parents = ARRAY_SIZE(gcc_parent_data_27),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_mp_phy_pipe_0_clk_src = {
	.reg = 0xab060,
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
	.reg = 0xab068,
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

static struct clk_regmap_mux gcc_usb4_1_phy_dp_clk_src = {
	.reg = 0xb8050,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_28,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_dp_clk_src",
			.parent_data = gcc_parent_data_28,
			.num_parents = ARRAY_SIZE(gcc_parent_data_28),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_p2rr2p_pipe_clk_src = {
	.reg = 0xb80b0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_29,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_p2rr2p_pipe_clk_src",
			.parent_data = gcc_parent_data_29,
			.num_parents = ARRAY_SIZE(gcc_parent_data_29),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_pcie_pipe_mux_clk_src = {
	.reg = 0xb80e0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_31,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_pcie_pipe_mux_clk_src",
			.parent_data = gcc_parent_data_31,
			.num_parents = ARRAY_SIZE(gcc_parent_data_31),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_rx0_clk_src = {
	.reg = 0xb8090,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_32,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_rx0_clk_src",
			.parent_data = gcc_parent_data_32,
			.num_parents = ARRAY_SIZE(gcc_parent_data_32),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_rx1_clk_src = {
	.reg = 0xb809c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_33,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_rx1_clk_src",
			.parent_data = gcc_parent_data_33,
			.num_parents = ARRAY_SIZE(gcc_parent_data_33),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_1_phy_sys_clk_src = {
	.reg = 0xb80c0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_34,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_sys_clk_src",
			.parent_data = gcc_parent_data_34,
			.num_parents = ARRAY_SIZE(gcc_parent_data_34),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_phy_dp_clk_src = {
	.reg = 0x2a050,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_35,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_dp_clk_src",
			.parent_data = gcc_parent_data_35,
			.num_parents = ARRAY_SIZE(gcc_parent_data_35),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_phy_p2rr2p_pipe_clk_src = {
	.reg = 0x2a0b0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_36,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_p2rr2p_pipe_clk_src",
			.parent_data = gcc_parent_data_36,
			.num_parents = ARRAY_SIZE(gcc_parent_data_36),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_phy_pcie_pipe_mux_clk_src = {
	.reg = 0x2a0e0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_38,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_pcie_pipe_mux_clk_src",
			.parent_data = gcc_parent_data_38,
			.num_parents = ARRAY_SIZE(gcc_parent_data_38),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_phy_rx0_clk_src = {
	.reg = 0x2a090,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_39,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_rx0_clk_src",
			.parent_data = gcc_parent_data_39,
			.num_parents = ARRAY_SIZE(gcc_parent_data_39),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_phy_rx1_clk_src = {
	.reg = 0x2a09c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_40,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_rx1_clk_src",
			.parent_data = gcc_parent_data_40,
			.num_parents = ARRAY_SIZE(gcc_parent_data_40),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb4_phy_sys_clk_src = {
	.reg = 0x2a0c0,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_41,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_sys_clk_src",
			.parent_data = gcc_parent_data_41,
			.num_parents = ARRAY_SIZE(gcc_parent_data_41),
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_emac0_ptp_clk_src[] = {
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(125000000, P_GCC_GPLL7_OUT_MAIN, 4, 0, 0),
	F(230400000, P_GCC_GPLL4_OUT_MAIN, 3.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_ptp_clk_src = {
	.cmd_rcgr = 0xaa020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_emac0_ptp_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_ptp_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_emac0_rgmii_clk_src[] = {
	F(50000000, P_GCC_GPLL0_OUT_EVEN, 6, 0, 0),
	F(125000000, P_GCC_GPLL7_OUT_MAIN, 4, 0, 0),
	F(250000000, P_GCC_GPLL7_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_emac0_rgmii_clk_src = {
	.cmd_rcgr = 0xaa040,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_emac0_rgmii_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac0_rgmii_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_emac1_ptp_clk_src = {
	.cmd_rcgr = 0xba020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_emac0_ptp_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac1_ptp_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_emac1_rgmii_clk_src = {
	.cmd_rcgr = 0xba040,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_emac0_rgmii_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_emac1_rgmii_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
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
	.cmd_rcgr = 0x64004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x65004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x66004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp4_clk_src = {
	.cmd_rcgr = 0xc2004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp4_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_gp5_clk_src = {
	.cmd_rcgr = 0xc3004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_gp5_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_aux_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_aux_clk_src = {
	.cmd_rcgr = 0xa4054,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_0_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_0_phy_rchng_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_0_phy_rchng_clk_src = {
	.cmd_rcgr = 0xa403c,
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

static const struct freq_tbl ftbl_gcc_pcie_1_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_1_aux_clk_src = {
	.cmd_rcgr = 0x8d054,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_1_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_1_phy_rchng_clk_src = {
	.cmd_rcgr = 0x8d03c,
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

static struct clk_rcg2 gcc_pcie_2a_aux_clk_src = {
	.cmd_rcgr = 0x9d064,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2a_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_2a_phy_rchng_clk_src = {
	.cmd_rcgr = 0x9d044,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2a_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_2b_aux_clk_src = {
	.cmd_rcgr = 0x9e064,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2b_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_2b_phy_rchng_clk_src = {
	.cmd_rcgr = 0x9e044,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2b_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_3a_aux_clk_src = {
	.cmd_rcgr = 0xa0064,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_3a_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_3a_phy_rchng_clk_src = {
	.cmd_rcgr = 0xa0044,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_3a_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_3b_aux_clk_src = {
	.cmd_rcgr = 0xa2064,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_3b_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_3b_phy_rchng_clk_src = {
	.cmd_rcgr = 0xa2044,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_3b_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_4_aux_clk_src = {
	.cmd_rcgr = 0x6b064,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_4_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_4_phy_rchng_clk_src = {
	.cmd_rcgr = 0x6b044,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_0_phy_rchng_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_4_phy_rchng_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_pcie_rscc_xo_clk_src = {
	.cmd_rcgr = 0xae00c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_rscc_xo_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(60000000, P_GCC_GPLL0_OUT_EVEN, 5, 0, 0),
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
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x17148,
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
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x17278,
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
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x173a8,
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
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x174d8,
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
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x17608,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x17738,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s5_clk_src_init,
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s6_clk_src[] = {
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
	F(120000000, P_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_init_data gcc_qupv3_wrap0_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s6_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s6_clk_src = {
	.cmd_rcgr = 0x17868,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s6_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s6_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap0_s7_clk_src_init = {
	.name = "gcc_qupv3_wrap0_s7_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap0_s7_clk_src = {
	.cmd_rcgr = 0x17998,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s6_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap0_s7_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0x18148,
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
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s1_clk_src = {
	.cmd_rcgr = 0x18278,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s2_clk_src = {
	.cmd_rcgr = 0x183a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s3_clk_src = {
	.cmd_rcgr = 0x184d8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0x18608,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0x18738,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s6_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s6_clk_src = {
	.cmd_rcgr = 0x18868,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s6_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s6_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap1_s7_clk_src_init = {
	.name = "gcc_qupv3_wrap1_s7_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap1_s7_clk_src = {
	.cmd_rcgr = 0x18998,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s6_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap1_s7_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s0_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s0_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s0_clk_src = {
	.cmd_rcgr = 0x1e148,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s0_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s1_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s1_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s1_clk_src = {
	.cmd_rcgr = 0x1e278,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s1_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s2_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s2_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s2_clk_src = {
	.cmd_rcgr = 0x1e3a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s2_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s3_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s3_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s3_clk_src = {
	.cmd_rcgr = 0x1e4d8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s3_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s4_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s4_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s4_clk_src = {
	.cmd_rcgr = 0x1e608,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s4_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s5_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s5_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s5_clk_src = {
	.cmd_rcgr = 0x1e738,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s0_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s5_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s6_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s6_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s6_clk_src = {
	.cmd_rcgr = 0x1e868,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s6_clk_src,
	.clkr.hw.init = &gcc_qupv3_wrap2_s6_clk_src_init,
};

static struct clk_init_data gcc_qupv3_wrap2_s7_clk_src_init = {
	.name = "gcc_qupv3_wrap2_s7_clk_src",
	.parent_data = gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_ops,
};

static struct clk_rcg2 gcc_qupv3_wrap2_s7_clk_src = {
	.cmd_rcgr = 0x1e998,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_wrap0_s6_clk_src,
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
	.cmd_rcgr = 0x1400c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_15,
	.freq_tbl = ftbl_gcc_sdcc2_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc2_apps_clk_src",
		.parent_data = gcc_parent_data_15,
		.num_parents = ARRAY_SIZE(gcc_parent_data_15),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc4_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(100000000, P_GCC_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc4_apps_clk_src = {
	.cmd_rcgr = 0x1600c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_sdcc4_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc4_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_card_axi_clk_src[] = {
	F(25000000, P_GCC_GPLL0_OUT_EVEN, 12, 0, 0),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_MAIN, 4, 0, 0),
	F(300000000, P_GCC_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_card_axi_clk_src = {
	.cmd_rcgr = 0x75024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_axi_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_card_axi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_ufs_card_ice_core_clk_src[] = {
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(150000000, P_GCC_GPLL0_OUT_MAIN, 4, 0, 0),
	F(300000000, P_GCC_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_card_ice_core_clk_src = {
	.cmd_rcgr = 0x7506c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_card_ice_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_ufs_card_phy_aux_clk_src = {
	.cmd_rcgr = 0x750a0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_card_phy_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_ufs_card_unipro_core_clk_src = {
	.cmd_rcgr = 0x75084,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_card_unipro_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_axi_clk_src = {
	.cmd_rcgr = 0x77024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_axi_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_axi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_ice_core_clk_src = {
	.cmd_rcgr = 0x7706c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_ice_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_phy_aux_clk_src = {
	.cmd_rcgr = 0x770a0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_0_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_phy_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_ufs_phy_unipro_core_clk_src = {
	.cmd_rcgr = 0x77084,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_card_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_ufs_phy_unipro_core_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
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
	.cmd_rcgr = 0xab020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_mp_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_mp_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb30_mp_mock_utmi_clk_src = {
	.cmd_rcgr = 0xab038,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_mp_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb30_prim_master_clk_src = {
	.cmd_rcgr = 0xf020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_mp_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb30_prim_mock_utmi_clk_src = {
	.cmd_rcgr = 0xf038,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb30_sec_master_clk_src = {
	.cmd_rcgr = 0x10020,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_mp_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_sec_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb30_sec_mock_utmi_clk_src = {
	.cmd_rcgr = 0x10038,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_sec_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb3_mp_phy_aux_clk_src = {
	.cmd_rcgr = 0xab06c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_mp_phy_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb3_prim_phy_aux_clk_src = {
	.cmd_rcgr = 0xf068,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_prim_phy_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb3_sec_phy_aux_clk_src = {
	.cmd_rcgr = 0x10068,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb3_sec_phy_aux_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb4_1_master_clk_src[] = {
	F(85714286, P_GCC_GPLL0_OUT_EVEN, 3.5, 0, 0),
	F(175000000, P_GCC_GPLL8_OUT_MAIN, 4, 0, 0),
	F(350000000, P_GCC_GPLL8_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb4_1_master_clk_src = {
	.cmd_rcgr = 0xb8018,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_usb4_1_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_1_master_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb4_1_phy_pcie_pipe_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(125000000, P_GCC_GPLL7_OUT_MAIN, 4, 0, 0),
	F(250000000, P_GCC_GPLL7_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb4_1_phy_pcie_pipe_clk_src = {
	.cmd_rcgr = 0xb80c4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_usb4_1_phy_pcie_pipe_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_1_phy_pcie_pipe_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb4_1_sb_if_clk_src = {
	.cmd_rcgr = 0xb8070,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_1_sb_if_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb4_1_tmu_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(250000000, P_GCC_GPLL2_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb4_1_tmu_clk_src = {
	.cmd_rcgr = 0xb8054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_usb4_1_tmu_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_1_tmu_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb4_master_clk_src = {
	.cmd_rcgr = 0x2a018,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_usb4_1_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_master_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb4_phy_pcie_pipe_clk_src = {
	.cmd_rcgr = 0x2a0c4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_usb4_1_phy_pcie_pipe_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_phy_pcie_pipe_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb4_sb_if_clk_src = {
	.cmd_rcgr = 0x2a070,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_pcie_1_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_sb_if_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 gcc_usb4_tmu_clk_src = {
	.cmd_rcgr = 0x2a054,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_usb4_1_tmu_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb4_tmu_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div gcc_pcie_2a_pipe_div_clk_src = {
	.reg = 0x9d060,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2a_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_pcie_2a_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_2b_pipe_div_clk_src = {
	.reg = 0x9e060,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_2b_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_pcie_2b_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_3a_pipe_div_clk_src = {
	.reg = 0xa0060,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_3a_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_pcie_3a_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_3b_pipe_div_clk_src = {
	.reg = 0xa2060,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_3b_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_pcie_3b_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_pcie_4_pipe_div_clk_src = {
	.reg = 0x6b060,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_4_pipe_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_pcie_4_pipe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap0_s4_div_clk_src = {
	.reg = 0x17ac8,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap0_s4_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_qupv3_wrap0_s4_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap1_s4_div_clk_src = {
	.reg = 0x18ac8,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap1_s4_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_qupv3_wrap1_s4_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_wrap2_s4_div_clk_src = {
	.reg = 0x1eac8,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_wrap2_s4_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_qupv3_wrap2_s4_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_mp_mock_utmi_postdiv_clk_src = {
	.reg = 0xab050,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_mp_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_usb30_mp_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_prim_mock_utmi_postdiv_clk_src = {
	.reg = 0xf050,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_prim_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_usb30_prim_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_sec_mock_utmi_postdiv_clk_src = {
	.reg = 0x10050,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb30_sec_mock_utmi_postdiv_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gcc_usb30_sec_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_aggre_noc_pcie0_tunnel_axi_clk = {
	.halt_reg = 0xa41a8,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xa41a8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie0_tunnel_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_noc_pcie1_tunnel_axi_clk = {
	.halt_reg = 0x8d07c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x8d07c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie1_tunnel_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_noc_pcie_4_axi_clk = {
	.halt_reg = 0x6b1b8,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x6b1b8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie_4_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_noc_pcie_south_sf_axi_clk = {
	.halt_reg = 0xbf13c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xbf13c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_noc_pcie_south_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_card_axi_clk = {
	.halt_reg = 0x750cc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x750cc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x750cc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_ufs_card_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_card_axi_hw_ctl_clk = {
	.halt_reg = 0x750cc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x750cc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x750cc,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_ufs_card_axi_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_phy_axi_clk = {
	.halt_reg = 0x770cc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x770cc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x770cc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_ufs_phy_axi_hw_ctl_clk = {
	.halt_reg = 0x770cc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x770cc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x770cc,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_ufs_phy_axi_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_mp_axi_clk = {
	.halt_reg = 0xab084,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xab084,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xab084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb3_mp_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_mp_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_prim_axi_clk = {
	.halt_reg = 0xf080,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xf080,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xf080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb3_sec_axi_clk = {
	.halt_reg = 0x10080,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x10080,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x10080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb3_sec_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_sec_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb4_1_axi_clk = {
	.halt_reg = 0xb80e4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xb80e4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb80e4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb4_1_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_1_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb4_axi_clk = {
	.halt_reg = 0x2a0e4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2a0e4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a0e4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb4_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb_noc_axi_clk = {
	.halt_reg = 0x5d024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x5d024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5d024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb_noc_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb_noc_north_axi_clk = {
	.halt_reg = 0x5d020,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x5d020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5d020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb_noc_north_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_aggre_usb_noc_south_axi_clk = {
	.halt_reg = 0x5d01c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x5d01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5d01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_aggre_usb_noc_south_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ahb2phy0_clk = {
	.halt_reg = 0x6a004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6a004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6a004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ahb2phy0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ahb2phy2_clk = {
	.halt_reg = 0x6a008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6a008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6a008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ahb2phy2_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x38004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x38004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_boot_rom_ahb_clk",
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
	.halt_reg = 0x26014,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x26014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_throttle_nrt_axi_clk = {
	.halt_reg = 0x2601c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x2601c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2601c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_throttle_nrt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_throttle_rt_axi_clk = {
	.halt_reg = 0x26018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x26018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x26018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_throttle_rt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_throttle_xo_clk = {
	.halt_reg = 0x26024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x26024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_camera_throttle_xo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_mp_axi_clk = {
	.halt_reg = 0xab088,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xab088,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xab088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb3_mp_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_mp_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_prim_axi_clk = {
	.halt_reg = 0xf084,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xf084,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xf084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb3_prim_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cfg_noc_usb3_sec_axi_clk = {
	.halt_reg = 0x10084,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x10084,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x10084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cfg_noc_usb3_sec_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_sec_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_pcie0_tunnel_clk = {
	.halt_reg = 0xa4074,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_pcie0_tunnel_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_pcie1_tunnel_clk = {
	.halt_reg = 0x8d074,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_pcie1_tunnel_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_pcie4_qx_clk = {
	.halt_reg = 0x6b084,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b084,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_pcie4_qx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_gpu_axi_clk = {
	.halt_reg = 0x7115c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x7115c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7115c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ddrss_gpu_axi_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gcc_ddrss_pcie_sf_tbu_clk = {
	.halt_reg = 0xa602c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xa602c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ddrss_pcie_sf_tbu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp1_hf_axi_clk = {
	.halt_reg = 0xbb010,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xbb010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xbb010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp1_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp1_sf_axi_clk = {
	.halt_reg = 0xbb018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xbb018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xbb018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp1_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp1_throttle_nrt_axi_clk = {
	.halt_reg = 0xbb024,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xbb024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xbb024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp1_throttle_nrt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp1_throttle_rt_axi_clk = {
	.halt_reg = 0xbb020,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xbb020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xbb020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp1_throttle_rt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_hf_axi_clk = {
	.halt_reg = 0x27010,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x27010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x27010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_sf_axi_clk = {
	.halt_reg = 0x27018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x27018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x27018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_throttle_nrt_axi_clk = {
	.halt_reg = 0x27024,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x27024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x27024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_throttle_nrt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_disp_throttle_rt_axi_clk = {
	.halt_reg = 0x27020,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x27020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x27020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_disp_throttle_rt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_axi_clk = {
	.halt_reg = 0xaa010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xaa010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xaa010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_ptp_clk = {
	.halt_reg = 0xaa01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xaa01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_ptp_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_emac0_ptp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_rgmii_clk = {
	.halt_reg = 0xaa038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xaa038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_rgmii_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_emac0_rgmii_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac0_slv_ahb_clk = {
	.halt_reg = 0xaa018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xaa018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xaa018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac0_slv_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_axi_clk = {
	.halt_reg = 0xba010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xba010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_ptp_clk = {
	.halt_reg = 0xba01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xba01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_ptp_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_emac1_ptp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_rgmii_clk = {
	.halt_reg = 0xba038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xba038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_rgmii_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_emac1_rgmii_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_emac1_slv_ahb_clk = {
	.halt_reg = 0xba018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xba018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xba018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_emac1_slv_ahb_clk",
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
			.parent_hws = (const struct clk_hw*[]){
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
			.parent_hws = (const struct clk_hw*[]){
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
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp4_clk = {
	.halt_reg = 0xc2000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc2000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp4_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp5_clk = {
	.halt_reg = 0xc3000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc3000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gp5_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gp5_clk_src.clkr.hw,
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
		.enable_reg = 0x52000,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw*[]){
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
		.enable_reg = 0x52000,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_gpll0_out_even.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_iref_en = {
	.halt_reg = 0x8c014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_iref_en",
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
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gcc_gpu_snoc_dvm_gfx_clk = {
	.halt_reg = 0x71020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x71020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_snoc_dvm_gfx_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gcc_gpu_tcu_throttle_ahb_clk = {
	.halt_reg = 0x71008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_tcu_throttle_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gpu_tcu_throttle_clk = {
	.halt_reg = 0x71018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x71018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x71018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_gpu_tcu_throttle_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_phy_rchng_clk = {
	.halt_reg = 0xa4038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_0_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_phy_rchng_clk = {
	.halt_reg = 0x8d038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_1_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie2a_phy_rchng_clk = {
	.halt_reg = 0x9d040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie2a_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2a_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie2b_phy_rchng_clk = {
	.halt_reg = 0x9e040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie2b_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2b_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie3a_phy_rchng_clk = {
	.halt_reg = 0xa0040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie3a_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_3a_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie3b_phy_rchng_clk = {
	.halt_reg = 0xa2040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie3b_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_3b_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie4_phy_rchng_clk = {
	.halt_reg = 0x6b040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie4_phy_rchng_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_4_phy_rchng_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_aux_clk = {
	.halt_reg = 0xa4028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_0_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_cfg_ahb_clk = {
	.halt_reg = 0xa4024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xa4024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_mstr_axi_clk = {
	.halt_reg = 0xa401c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xa401c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_pipe_clk = {
	.halt_reg = 0xa4030,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_phy_pcie_pipe_mux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_axi_clk = {
	.halt_reg = 0xa4014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xa4014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_slv_q2a_axi_clk = {
	.halt_reg = 0xa4010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_0_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_aux_clk = {
	.halt_reg = 0x8d028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_1_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_cfg_ahb_clk = {
	.halt_reg = 0x8d024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8d024,
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
	.halt_reg = 0x8d01c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x8d01c,
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

static struct clk_branch gcc_pcie_1_pipe_clk = {
	.halt_reg = 0x8d030,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_1_phy_pcie_pipe_mux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_1_slv_axi_clk = {
	.halt_reg = 0x8d014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x8d014,
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
	.halt_reg = 0x8d010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52000,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_1_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2a2b_clkref_clk = {
	.halt_reg = 0x8c034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2a2b_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2a_aux_clk = {
	.halt_reg = 0x9d028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2a_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2a_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2a_cfg_ahb_clk = {
	.halt_reg = 0x9d024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9d024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2a_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2a_mstr_axi_clk = {
	.halt_reg = 0x9d01c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x9d01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2a_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2a_pipe_clk = {
	.halt_reg = 0x9d030,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2a_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2a_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2a_pipediv2_clk = {
	.halt_reg = 0x9d038,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2a_pipediv2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2a_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2a_slv_axi_clk = {
	.halt_reg = 0x9d014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9d014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2a_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2a_slv_q2a_axi_clk = {
	.halt_reg = 0x9d010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2a_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2b_aux_clk = {
	.halt_reg = 0x9e028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2b_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2b_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2b_cfg_ahb_clk = {
	.halt_reg = 0x9e024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9e024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2b_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2b_mstr_axi_clk = {
	.halt_reg = 0x9e01c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x9e01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2b_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2b_pipe_clk = {
	.halt_reg = 0x9e030,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2b_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2b_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2b_pipediv2_clk = {
	.halt_reg = 0x9e038,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2b_pipediv2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_2b_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2b_slv_axi_clk = {
	.halt_reg = 0x9e014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x9e014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2b_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_2b_slv_q2a_axi_clk = {
	.halt_reg = 0x9e010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_2b_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a3b_clkref_clk = {
	.halt_reg = 0x8c038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a3b_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_aux_clk = {
	.halt_reg = 0xa0028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_3a_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_cfg_ahb_clk = {
	.halt_reg = 0xa0024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xa0024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_mstr_axi_clk = {
	.halt_reg = 0xa001c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xa001c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_pipe_clk = {
	.halt_reg = 0xa0030,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_3a_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_pipediv2_clk = {
	.halt_reg = 0xa0038,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_pipediv2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_3a_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_slv_axi_clk = {
	.halt_reg = 0xa0014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xa0014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3a_slv_q2a_axi_clk = {
	.halt_reg = 0xa0010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3a_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_aux_clk = {
	.halt_reg = 0xa2028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_3b_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_cfg_ahb_clk = {
	.halt_reg = 0xa2024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xa2024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_mstr_axi_clk = {
	.halt_reg = 0xa201c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0xa201c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_pipe_clk = {
	.halt_reg = 0xa2030,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_3b_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_pipediv2_clk = {
	.halt_reg = 0xa2038,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_pipediv2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_3b_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_slv_axi_clk = {
	.halt_reg = 0xa2014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xa2014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_3b_slv_q2a_axi_clk = {
	.halt_reg = 0xa2010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_3b_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_aux_clk = {
	.halt_reg = 0x6b028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(3),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_4_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_cfg_ahb_clk = {
	.halt_reg = 0x6b024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_clkref_clk = {
	.halt_reg = 0x8c030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_mstr_axi_clk = {
	.halt_reg = 0x6b01c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x6b01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_pipe_clk = {
	.halt_reg = 0x6b030,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_4_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_pipediv2_clk = {
	.halt_reg = 0x6b038,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_pipediv2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_4_pipe_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_slv_axi_clk = {
	.halt_reg = 0x6b014,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x6b014,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_4_slv_q2a_axi_clk = {
	.halt_reg = 0x6b010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_4_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_rscc_ahb_clk = {
	.halt_reg = 0xae008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xae008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_rscc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_rscc_xo_clk = {
	.halt_reg = 0xae004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_rscc_xo_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_pcie_rscc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_throttle_cfg_clk = {
	.halt_reg = 0xa6028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie_throttle_cfg_clk",
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
			.parent_hws = (const struct clk_hw*[]){
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

static struct clk_branch gcc_qmip_disp1_ahb_clk = {
	.halt_reg = 0xbb008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xbb008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xbb008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_disp1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_disp1_rot_ahb_clk = {
	.halt_reg = 0xbb00c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xbb00c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xbb00c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_disp1_rot_ahb_clk",
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

static struct clk_branch gcc_qmip_disp_rot_ahb_clk = {
	.halt_reg = 0x2700c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2700c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2700c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_disp_rot_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_cvp_ahb_clk = {
	.halt_reg = 0x28008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x28008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x28008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_cvp_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qmip_video_vcodec_ahb_clk = {
	.halt_reg = 0x2800c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2800c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2800c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qmip_video_vcodec_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x17014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x1700c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_qspi0_clk = {
	.halt_reg = 0x17ac4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_qspi0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x17144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s1_clk = {
	.halt_reg = 0x17274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(11),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s2_clk = {
	.halt_reg = 0x173a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s3_clk = {
	.halt_reg = 0x174d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s3_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s4_clk = {
	.halt_reg = 0x17604,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s4_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s4_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s5_clk = {
	.halt_reg = 0x17734,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s5_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s6_clk = {
	.halt_reg = 0x17864,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s6_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap0_s7_clk = {
	.halt_reg = 0x17994,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap0_s7_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap0_s7_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_core_2x_clk = {
	.halt_reg = 0x18014,
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
	.halt_reg = 0x1800c,
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

static struct clk_branch gcc_qupv3_wrap1_qspi0_clk = {
	.halt_reg = 0x18ac4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_qspi0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0x18144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s1_clk = {
	.halt_reg = 0x18274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s2_clk = {
	.halt_reg = 0x183a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s3_clk = {
	.halt_reg = 0x184d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s3_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s4_clk = {
	.halt_reg = 0x18604,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s4_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s4_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s5_clk = {
	.halt_reg = 0x18734,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s5_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s6_clk = {
	.halt_reg = 0x18864,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s6_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap1_s7_clk = {
	.halt_reg = 0x18994,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap1_s7_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap1_s7_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_core_2x_clk = {
	.halt_reg = 0x1e014,
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
	.halt_reg = 0x1e00c,
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

static struct clk_branch gcc_qupv3_wrap2_qspi0_clk = {
	.halt_reg = 0x1eac4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_qspi0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s0_clk = {
	.halt_reg = 0x1e144,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s1_clk = {
	.halt_reg = 0x1e274,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s2_clk = {
	.halt_reg = 0x1e3a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s3_clk = {
	.halt_reg = 0x1e4d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s3_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s4_clk = {
	.halt_reg = 0x1e604,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(8),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s4_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s4_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s5_clk = {
	.halt_reg = 0x1e734,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52010,
		.enable_mask = BIT(9),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s5_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s6_clk = {
	.halt_reg = 0x1e864,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s6_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap2_s7_clk = {
	.halt_reg = 0x1e994,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x52018,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap2_s7_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_qupv3_wrap2_s7_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_m_ahb_clk = {
	.halt_reg = 0x17004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x17004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_0_s_ahb_clk = {
	.halt_reg = 0x17008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x17008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x52008,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_wrap_0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_wrap_1_m_ahb_clk = {
	.halt_reg = 0x18004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x18004,
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
	.halt_reg = 0x18008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x18008,
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
	.halt_reg = 0x1e004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1e004,
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
	.halt_reg = 0x1e008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1e008,
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

static struct clk_branch gcc_sdcc2_ahb_clk = {
	.halt_reg = 0x14008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x14008,
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
			.parent_hws = (const struct clk_hw*[]){
				&gcc_sdcc2_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc4_ahb_clk = {
	.halt_reg = 0x16008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x16008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc4_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc4_apps_clk = {
	.halt_reg = 0x16004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x16004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc4_apps_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_sdcc4_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_usb_axi_clk = {
	.halt_reg = 0x5d000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x5d000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x5d000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sys_noc_usb_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_1_card_clkref_clk = {
	.halt_reg = 0x8c000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_1_card_clkref_clk",
			.parent_data = &gcc_parent_data_tcxo,
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_ahb_clk = {
	.halt_reg = 0x75018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x75018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_axi_clk = {
	.halt_reg = 0x75010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x75010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_axi_hw_ctl_clk = {
	.halt_reg = 0x75010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x75010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75010,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_axi_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_clkref_clk = {
	.halt_reg = 0x8c054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_clkref_clk",
			.parent_data = &gcc_parent_data_tcxo,
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_ice_core_clk = {
	.halt_reg = 0x75064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x75064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_ice_core_hw_ctl_clk = {
	.halt_reg = 0x75064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x75064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x75064,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_ice_core_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_phy_aux_clk = {
	.halt_reg = 0x7509c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7509c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7509c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_phy_aux_hw_ctl_clk = {
	.halt_reg = 0x7509c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7509c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7509c,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_phy_aux_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_rx_symbol_0_clk = {
	.halt_reg = 0x75020,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x75020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_rx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_rx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_rx_symbol_1_clk = {
	.halt_reg = 0x750b8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x750b8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_rx_symbol_1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_rx_symbol_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_tx_symbol_0_clk = {
	.halt_reg = 0x7501c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7501c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_tx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_tx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_unipro_core_clk = {
	.halt_reg = 0x7505c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7505c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7505c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_unipro_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_card_unipro_core_hw_ctl_clk = {
	.halt_reg = 0x7505c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7505c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7505c,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_card_unipro_core_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_card_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ahb_clk = {
	.halt_reg = 0x77018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_clk = {
	.halt_reg = 0x77010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_hw_ctl_clk = {
	.halt_reg = 0x77010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77010,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_axi_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_axi_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ice_core_clk = {
	.halt_reg = 0x77064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_ice_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ice_core_hw_ctl_clk = {
	.halt_reg = 0x77064,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x77064,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x77064,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_ice_core_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_ice_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_phy_aux_clk = {
	.halt_reg = 0x7709c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7709c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7709c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_phy_aux_hw_ctl_clk = {
	.halt_reg = 0x7709c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7709c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7709c,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_phy_aux_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_0_clk = {
	.halt_reg = 0x77020,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x77020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_rx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_1_clk = {
	.halt_reg = 0x770b8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x770b8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_rx_symbol_1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_rx_symbol_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_tx_symbol_0_clk = {
	.halt_reg = 0x7701c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x7701c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_tx_symbol_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_tx_symbol_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_clk = {
	.halt_reg = 0x7705c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7705c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7705c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_unipro_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_hw_ctl_clk = {
	.halt_reg = 0x7705c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x7705c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x7705c,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_phy_unipro_core_hw_ctl_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_ufs_phy_unipro_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_ref_clkref_clk = {
	.halt_reg = 0x8c058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_ufs_ref_clkref_clk",
			.parent_data = &gcc_parent_data_tcxo,
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2_hs0_clkref_clk = {
	.halt_reg = 0x8c044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb2_hs0_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2_hs1_clkref_clk = {
	.halt_reg = 0x8c048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb2_hs1_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2_hs2_clkref_clk = {
	.halt_reg = 0x8c04c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c04c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb2_hs2_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2_hs3_clkref_clk = {
	.halt_reg = 0x8c050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb2_hs3_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mp_master_clk = {
	.halt_reg = 0xab010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xab010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_mp_master_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_mp_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mp_mock_utmi_clk = {
	.halt_reg = 0xab01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xab01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_mp_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_mp_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mp_sleep_clk = {
	.halt_reg = 0xab018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xab018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_mp_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_master_clk = {
	.halt_reg = 0xf010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_master_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_prim_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_mock_utmi_clk = {
	.halt_reg = 0xf01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_prim_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_prim_sleep_clk = {
	.halt_reg = 0xf018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_prim_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_master_clk = {
	.halt_reg = 0x10010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_sec_master_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_sec_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_mock_utmi_clk = {
	.halt_reg = 0x1001c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1001c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_sec_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb30_sec_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sec_sleep_clk = {
	.halt_reg = 0x10018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb30_sec_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mp0_clkref_clk = {
	.halt_reg = 0x8c03c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c03c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp0_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mp1_clkref_clk = {
	.halt_reg = 0x8c040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp1_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mp_phy_aux_clk = {
	.halt_reg = 0xab054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xab054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_mp_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mp_phy_com_aux_clk = {
	.halt_reg = 0xab058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xab058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_mp_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mp_phy_pipe_0_clk = {
	.halt_reg = 0xab05c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xab05c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp_phy_pipe_0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_mp_phy_pipe_0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mp_phy_pipe_1_clk = {
	.halt_reg = 0xab064,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xab064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_mp_phy_pipe_1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_mp_phy_pipe_1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_aux_clk = {
	.halt_reg = 0xf054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_prim_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_com_aux_clk = {
	.halt_reg = 0xf058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_prim_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_phy_pipe_clk = {
	.halt_reg = 0xf05c,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0xf05c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xf05c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_prim_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb34_prim_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_aux_clk = {
	.halt_reg = 0x10054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_sec_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_sec_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_com_aux_clk = {
	.halt_reg = 0x10058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_sec_phy_com_aux_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb3_sec_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sec_phy_pipe_clk = {
	.halt_reg = 0x1005c,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x1005c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1005c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb3_sec_phy_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb34_sec_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_cfg_ahb_clk = {
	.halt_reg = 0xb808c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xb808c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb808c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_dp_clk = {
	.halt_reg = 0xb8048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb8048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_dp_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_1_phy_dp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_master_clk = {
	.halt_reg = 0xb8010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb8010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_master_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_1_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_phy_p2rr2p_pipe_clk = {
	.halt_reg = 0xb80b4,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb80b4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_p2rr2p_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_1_phy_p2rr2p_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_phy_pcie_pipe_clk = {
	.halt_reg = 0xb8038,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_pcie_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_1_phy_pcie_pipe_mux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_phy_rx0_clk = {
	.halt_reg = 0xb8094,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb8094,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_rx0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_1_phy_rx0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_phy_rx1_clk = {
	.halt_reg = 0xb80a0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb80a0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_rx1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_1_phy_rx1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_phy_usb_pipe_clk = {
	.halt_reg = 0xb8088,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0xb8088,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb8088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_phy_usb_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb34_sec_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_sb_if_clk = {
	.halt_reg = 0xb8034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb8034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_sb_if_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_1_sb_if_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_sys_clk = {
	.halt_reg = 0xb8040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb8040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_sys_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_1_phy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_1_tmu_clk = {
	.halt_reg = 0xb806c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xb806c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xb806c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_1_tmu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_1_tmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_cfg_ahb_clk = {
	.halt_reg = 0x2a08c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2a08c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a08c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_clkref_clk = {
	.halt_reg = 0x8c010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_dp_clk = {
	.halt_reg = 0x2a048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_dp_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_phy_dp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_eud_clkref_clk = {
	.halt_reg = 0x8c02c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8c02c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_eud_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_master_clk = {
	.halt_reg = 0x2a010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_master_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_phy_p2rr2p_pipe_clk = {
	.halt_reg = 0x2a0b4,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x2a0b4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_p2rr2p_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_phy_p2rr2p_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_phy_pcie_pipe_clk = {
	.halt_reg = 0x2a038,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x52020,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_pcie_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_phy_pcie_pipe_mux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_phy_rx0_clk = {
	.halt_reg = 0x2a094,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a094,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_rx0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_phy_rx0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_phy_rx1_clk = {
	.halt_reg = 0x2a0a0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a0a0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_rx1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_phy_rx1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_phy_usb_pipe_clk = {
	.halt_reg = 0x2a088,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x2a088,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_phy_usb_pipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb34_prim_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_sb_if_clk = {
	.halt_reg = 0x2a034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_sb_if_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_sb_if_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_sys_clk = {
	.halt_reg = 0x2a040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_sys_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_phy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb4_tmu_clk = {
	.halt_reg = 0x2a06c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2a06c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a06c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb4_tmu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gcc_usb4_tmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi0_clk = {
	.halt_reg = 0x28010,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x28010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x28010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_axi1_clk = {
	.halt_reg = 0x28018,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x28018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x28018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_axi1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_cvp_throttle_clk = {
	.halt_reg = 0x28024,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x28024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x28024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_cvp_throttle_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_video_vcodec_throttle_clk = {
	.halt_reg = 0x28020,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x28020,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x28020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_video_vcodec_throttle_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc pcie_0_tunnel_gdsc = {
	.gdscr = 0xa4004,
	.collapse_ctrl = 0x52128,
	.collapse_mask = BIT(0),
	.pd = {
		.name = "pcie_0_tunnel_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc pcie_1_tunnel_gdsc = {
	.gdscr = 0x8d004,
	.collapse_ctrl = 0x52128,
	.collapse_mask = BIT(1),
	.pd = {
		.name = "pcie_1_tunnel_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

/*
 * The Qualcomm PCIe driver does not yet implement suspend so to keep the
 * PCIe power domains always-on for now.
 */
static struct gdsc pcie_2a_gdsc = {
	.gdscr = 0x9d004,
	.collapse_ctrl = 0x52128,
	.collapse_mask = BIT(2),
	.pd = {
		.name = "pcie_2a_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc pcie_2b_gdsc = {
	.gdscr = 0x9e004,
	.collapse_ctrl = 0x52128,
	.collapse_mask = BIT(3),
	.pd = {
		.name = "pcie_2b_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc pcie_3a_gdsc = {
	.gdscr = 0xa0004,
	.collapse_ctrl = 0x52128,
	.collapse_mask = BIT(4),
	.pd = {
		.name = "pcie_3a_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc pcie_3b_gdsc = {
	.gdscr = 0xa2004,
	.collapse_ctrl = 0x52128,
	.collapse_mask = BIT(5),
	.pd = {
		.name = "pcie_3b_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc pcie_4_gdsc = {
	.gdscr = 0x6b004,
	.collapse_ctrl = 0x52128,
	.collapse_mask = BIT(6),
	.pd = {
		.name = "pcie_4_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc ufs_card_gdsc = {
	.gdscr = 0x75004,
	.pd = {
		.name = "ufs_card_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc ufs_phy_gdsc = {
	.gdscr = 0x77004,
	.pd = {
		.name = "ufs_phy_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc usb30_mp_gdsc = {
	.gdscr = 0xab004,
	.pd = {
		.name = "usb30_mp_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc usb30_prim_gdsc = {
	.gdscr = 0xf004,
	.pd = {
		.name = "usb30_prim_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc usb30_sec_gdsc = {
	.gdscr = 0x10004,
	.pd = {
		.name = "usb30_sec_gdsc",
	},
	.pwrsts = PWRSTS_RET_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc emac_0_gdsc = {
	.gdscr = 0xaa004,
	.pd = {
		.name = "emac_0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc emac_1_gdsc = {
	.gdscr = 0xba004,
	.pd = {
		.name = "emac_1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc usb4_1_gdsc = {
	.gdscr = 0xb8004,
	.pd = {
		.name = "usb4_1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc usb4_gdsc = {
	.gdscr = 0x2a004,
	.pd = {
		.name = "usb4_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct gdsc hlos1_vote_mmnoc_mmu_tbu_hf0_gdsc = {
	.gdscr = 0x7d050,
	.pd = {
		.name = "hlos1_vote_mmnoc_mmu_tbu_hf0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_mmnoc_mmu_tbu_hf1_gdsc = {
	.gdscr = 0x7d058,
	.pd = {
		.name = "hlos1_vote_mmnoc_mmu_tbu_hf1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_mmnoc_mmu_tbu_sf0_gdsc = {
	.gdscr = 0x7d054,
	.pd = {
		.name = "hlos1_vote_mmnoc_mmu_tbu_sf0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_mmnoc_mmu_tbu_sf1_gdsc = {
	.gdscr = 0x7d06c,
	.pd = {
		.name = "hlos1_vote_mmnoc_mmu_tbu_sf1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_turing_mmu_tbu0_gdsc = {
	.gdscr = 0x7d05c,
	.pd = {
		.name = "hlos1_vote_turing_mmu_tbu0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_turing_mmu_tbu1_gdsc = {
	.gdscr = 0x7d060,
	.pd = {
		.name = "hlos1_vote_turing_mmu_tbu1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_turing_mmu_tbu2_gdsc = {
	.gdscr = 0x7d0a0,
	.pd = {
		.name = "hlos1_vote_turing_mmu_tbu2_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc hlos1_vote_turing_mmu_tbu3_gdsc = {
	.gdscr = 0x7d0a4,
	.pd = {
		.name = "hlos1_vote_turing_mmu_tbu3_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct clk_regmap *gcc_sc8280xp_clocks[] = {
	[GCC_AGGRE_NOC_PCIE0_TUNNEL_AXI_CLK] = &gcc_aggre_noc_pcie0_tunnel_axi_clk.clkr,
	[GCC_AGGRE_NOC_PCIE1_TUNNEL_AXI_CLK] = &gcc_aggre_noc_pcie1_tunnel_axi_clk.clkr,
	[GCC_AGGRE_NOC_PCIE_4_AXI_CLK] = &gcc_aggre_noc_pcie_4_axi_clk.clkr,
	[GCC_AGGRE_NOC_PCIE_SOUTH_SF_AXI_CLK] = &gcc_aggre_noc_pcie_south_sf_axi_clk.clkr,
	[GCC_AGGRE_UFS_CARD_AXI_CLK] = &gcc_aggre_ufs_card_axi_clk.clkr,
	[GCC_AGGRE_UFS_CARD_AXI_HW_CTL_CLK] = &gcc_aggre_ufs_card_axi_hw_ctl_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_CLK] = &gcc_aggre_ufs_phy_axi_clk.clkr,
	[GCC_AGGRE_UFS_PHY_AXI_HW_CTL_CLK] = &gcc_aggre_ufs_phy_axi_hw_ctl_clk.clkr,
	[GCC_AGGRE_USB3_MP_AXI_CLK] = &gcc_aggre_usb3_mp_axi_clk.clkr,
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = &gcc_aggre_usb3_prim_axi_clk.clkr,
	[GCC_AGGRE_USB3_SEC_AXI_CLK] = &gcc_aggre_usb3_sec_axi_clk.clkr,
	[GCC_AGGRE_USB4_1_AXI_CLK] = &gcc_aggre_usb4_1_axi_clk.clkr,
	[GCC_AGGRE_USB4_AXI_CLK] = &gcc_aggre_usb4_axi_clk.clkr,
	[GCC_AGGRE_USB_NOC_AXI_CLK] = &gcc_aggre_usb_noc_axi_clk.clkr,
	[GCC_AGGRE_USB_NOC_NORTH_AXI_CLK] = &gcc_aggre_usb_noc_north_axi_clk.clkr,
	[GCC_AGGRE_USB_NOC_SOUTH_AXI_CLK] = &gcc_aggre_usb_noc_south_axi_clk.clkr,
	[GCC_AHB2PHY0_CLK] = &gcc_ahb2phy0_clk.clkr,
	[GCC_AHB2PHY2_CLK] = &gcc_ahb2phy2_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CAMERA_HF_AXI_CLK] = &gcc_camera_hf_axi_clk.clkr,
	[GCC_CAMERA_SF_AXI_CLK] = &gcc_camera_sf_axi_clk.clkr,
	[GCC_CAMERA_THROTTLE_NRT_AXI_CLK] = &gcc_camera_throttle_nrt_axi_clk.clkr,
	[GCC_CAMERA_THROTTLE_RT_AXI_CLK] = &gcc_camera_throttle_rt_axi_clk.clkr,
	[GCC_CAMERA_THROTTLE_XO_CLK] = &gcc_camera_throttle_xo_clk.clkr,
	[GCC_CFG_NOC_USB3_MP_AXI_CLK] = &gcc_cfg_noc_usb3_mp_axi_clk.clkr,
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = &gcc_cfg_noc_usb3_prim_axi_clk.clkr,
	[GCC_CFG_NOC_USB3_SEC_AXI_CLK] = &gcc_cfg_noc_usb3_sec_axi_clk.clkr,
	[GCC_CNOC_PCIE0_TUNNEL_CLK] = &gcc_cnoc_pcie0_tunnel_clk.clkr,
	[GCC_CNOC_PCIE1_TUNNEL_CLK] = &gcc_cnoc_pcie1_tunnel_clk.clkr,
	[GCC_CNOC_PCIE4_QX_CLK] = &gcc_cnoc_pcie4_qx_clk.clkr,
	[GCC_DDRSS_GPU_AXI_CLK] = &gcc_ddrss_gpu_axi_clk.clkr,
	[GCC_DDRSS_PCIE_SF_TBU_CLK] = &gcc_ddrss_pcie_sf_tbu_clk.clkr,
	[GCC_DISP1_HF_AXI_CLK] = &gcc_disp1_hf_axi_clk.clkr,
	[GCC_DISP1_SF_AXI_CLK] = &gcc_disp1_sf_axi_clk.clkr,
	[GCC_DISP1_THROTTLE_NRT_AXI_CLK] = &gcc_disp1_throttle_nrt_axi_clk.clkr,
	[GCC_DISP1_THROTTLE_RT_AXI_CLK] = &gcc_disp1_throttle_rt_axi_clk.clkr,
	[GCC_DISP_HF_AXI_CLK] = &gcc_disp_hf_axi_clk.clkr,
	[GCC_DISP_SF_AXI_CLK] = &gcc_disp_sf_axi_clk.clkr,
	[GCC_DISP_THROTTLE_NRT_AXI_CLK] = &gcc_disp_throttle_nrt_axi_clk.clkr,
	[GCC_DISP_THROTTLE_RT_AXI_CLK] = &gcc_disp_throttle_rt_axi_clk.clkr,
	[GCC_EMAC0_AXI_CLK] = &gcc_emac0_axi_clk.clkr,
	[GCC_EMAC0_PTP_CLK] = &gcc_emac0_ptp_clk.clkr,
	[GCC_EMAC0_PTP_CLK_SRC] = &gcc_emac0_ptp_clk_src.clkr,
	[GCC_EMAC0_RGMII_CLK] = &gcc_emac0_rgmii_clk.clkr,
	[GCC_EMAC0_RGMII_CLK_SRC] = &gcc_emac0_rgmii_clk_src.clkr,
	[GCC_EMAC0_SLV_AHB_CLK] = &gcc_emac0_slv_ahb_clk.clkr,
	[GCC_EMAC1_AXI_CLK] = &gcc_emac1_axi_clk.clkr,
	[GCC_EMAC1_PTP_CLK] = &gcc_emac1_ptp_clk.clkr,
	[GCC_EMAC1_PTP_CLK_SRC] = &gcc_emac1_ptp_clk_src.clkr,
	[GCC_EMAC1_RGMII_CLK] = &gcc_emac1_rgmii_clk.clkr,
	[GCC_EMAC1_RGMII_CLK_SRC] = &gcc_emac1_rgmii_clk_src.clkr,
	[GCC_EMAC1_SLV_AHB_CLK] = &gcc_emac1_slv_ahb_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP1_CLK_SRC] = &gcc_gp1_clk_src.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP2_CLK_SRC] = &gcc_gp2_clk_src.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_GP3_CLK_SRC] = &gcc_gp3_clk_src.clkr,
	[GCC_GP4_CLK] = &gcc_gp4_clk.clkr,
	[GCC_GP4_CLK_SRC] = &gcc_gp4_clk_src.clkr,
	[GCC_GP5_CLK] = &gcc_gp5_clk.clkr,
	[GCC_GP5_CLK_SRC] = &gcc_gp5_clk_src.clkr,
	[GCC_GPLL0] = &gcc_gpll0.clkr,
	[GCC_GPLL0_OUT_EVEN] = &gcc_gpll0_out_even.clkr,
	[GCC_GPLL2] = &gcc_gpll2.clkr,
	[GCC_GPLL4] = &gcc_gpll4.clkr,
	[GCC_GPLL7] = &gcc_gpll7.clkr,
	[GCC_GPLL8] = &gcc_gpll8.clkr,
	[GCC_GPLL9] = &gcc_gpll9.clkr,
	[GCC_GPU_GPLL0_CLK_SRC] = &gcc_gpu_gpll0_clk_src.clkr,
	[GCC_GPU_GPLL0_DIV_CLK_SRC] = &gcc_gpu_gpll0_div_clk_src.clkr,
	[GCC_GPU_IREF_EN] = &gcc_gpu_iref_en.clkr,
	[GCC_GPU_MEMNOC_GFX_CLK] = &gcc_gpu_memnoc_gfx_clk.clkr,
	[GCC_GPU_SNOC_DVM_GFX_CLK] = &gcc_gpu_snoc_dvm_gfx_clk.clkr,
	[GCC_GPU_TCU_THROTTLE_AHB_CLK] = &gcc_gpu_tcu_throttle_ahb_clk.clkr,
	[GCC_GPU_TCU_THROTTLE_CLK] = &gcc_gpu_tcu_throttle_clk.clkr,
	[GCC_PCIE0_PHY_RCHNG_CLK] = &gcc_pcie0_phy_rchng_clk.clkr,
	[GCC_PCIE1_PHY_RCHNG_CLK] = &gcc_pcie1_phy_rchng_clk.clkr,
	[GCC_PCIE2A_PHY_RCHNG_CLK] = &gcc_pcie2a_phy_rchng_clk.clkr,
	[GCC_PCIE2B_PHY_RCHNG_CLK] = &gcc_pcie2b_phy_rchng_clk.clkr,
	[GCC_PCIE3A_PHY_RCHNG_CLK] = &gcc_pcie3a_phy_rchng_clk.clkr,
	[GCC_PCIE3B_PHY_RCHNG_CLK] = &gcc_pcie3b_phy_rchng_clk.clkr,
	[GCC_PCIE4_PHY_RCHNG_CLK] = &gcc_pcie4_phy_rchng_clk.clkr,
	[GCC_PCIE_0_AUX_CLK] = &gcc_pcie_0_aux_clk.clkr,
	[GCC_PCIE_0_AUX_CLK_SRC] = &gcc_pcie_0_aux_clk_src.clkr,
	[GCC_PCIE_0_CFG_AHB_CLK] = &gcc_pcie_0_cfg_ahb_clk.clkr,
	[GCC_PCIE_0_MSTR_AXI_CLK] = &gcc_pcie_0_mstr_axi_clk.clkr,
	[GCC_PCIE_0_PHY_RCHNG_CLK_SRC] = &gcc_pcie_0_phy_rchng_clk_src.clkr,
	[GCC_PCIE_0_PIPE_CLK] = &gcc_pcie_0_pipe_clk.clkr,
	[GCC_PCIE_0_SLV_AXI_CLK] = &gcc_pcie_0_slv_axi_clk.clkr,
	[GCC_PCIE_0_SLV_Q2A_AXI_CLK] = &gcc_pcie_0_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_1_AUX_CLK] = &gcc_pcie_1_aux_clk.clkr,
	[GCC_PCIE_1_AUX_CLK_SRC] = &gcc_pcie_1_aux_clk_src.clkr,
	[GCC_PCIE_1_CFG_AHB_CLK] = &gcc_pcie_1_cfg_ahb_clk.clkr,
	[GCC_PCIE_1_MSTR_AXI_CLK] = &gcc_pcie_1_mstr_axi_clk.clkr,
	[GCC_PCIE_1_PHY_RCHNG_CLK_SRC] = &gcc_pcie_1_phy_rchng_clk_src.clkr,
	[GCC_PCIE_1_PIPE_CLK] = &gcc_pcie_1_pipe_clk.clkr,
	[GCC_PCIE_1_SLV_AXI_CLK] = &gcc_pcie_1_slv_axi_clk.clkr,
	[GCC_PCIE_1_SLV_Q2A_AXI_CLK] = &gcc_pcie_1_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_2A2B_CLKREF_CLK] = &gcc_pcie_2a2b_clkref_clk.clkr,
	[GCC_PCIE_2A_AUX_CLK] = &gcc_pcie_2a_aux_clk.clkr,
	[GCC_PCIE_2A_AUX_CLK_SRC] = &gcc_pcie_2a_aux_clk_src.clkr,
	[GCC_PCIE_2A_CFG_AHB_CLK] = &gcc_pcie_2a_cfg_ahb_clk.clkr,
	[GCC_PCIE_2A_MSTR_AXI_CLK] = &gcc_pcie_2a_mstr_axi_clk.clkr,
	[GCC_PCIE_2A_PHY_RCHNG_CLK_SRC] = &gcc_pcie_2a_phy_rchng_clk_src.clkr,
	[GCC_PCIE_2A_PIPE_CLK] = &gcc_pcie_2a_pipe_clk.clkr,
	[GCC_PCIE_2A_PIPE_CLK_SRC] = &gcc_pcie_2a_pipe_clk_src.clkr,
	[GCC_PCIE_2A_PIPE_DIV_CLK_SRC] = &gcc_pcie_2a_pipe_div_clk_src.clkr,
	[GCC_PCIE_2A_PIPEDIV2_CLK] = &gcc_pcie_2a_pipediv2_clk.clkr,
	[GCC_PCIE_2A_SLV_AXI_CLK] = &gcc_pcie_2a_slv_axi_clk.clkr,
	[GCC_PCIE_2A_SLV_Q2A_AXI_CLK] = &gcc_pcie_2a_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_2B_AUX_CLK] = &gcc_pcie_2b_aux_clk.clkr,
	[GCC_PCIE_2B_AUX_CLK_SRC] = &gcc_pcie_2b_aux_clk_src.clkr,
	[GCC_PCIE_2B_CFG_AHB_CLK] = &gcc_pcie_2b_cfg_ahb_clk.clkr,
	[GCC_PCIE_2B_MSTR_AXI_CLK] = &gcc_pcie_2b_mstr_axi_clk.clkr,
	[GCC_PCIE_2B_PHY_RCHNG_CLK_SRC] = &gcc_pcie_2b_phy_rchng_clk_src.clkr,
	[GCC_PCIE_2B_PIPE_CLK] = &gcc_pcie_2b_pipe_clk.clkr,
	[GCC_PCIE_2B_PIPE_CLK_SRC] = &gcc_pcie_2b_pipe_clk_src.clkr,
	[GCC_PCIE_2B_PIPE_DIV_CLK_SRC] = &gcc_pcie_2b_pipe_div_clk_src.clkr,
	[GCC_PCIE_2B_PIPEDIV2_CLK] = &gcc_pcie_2b_pipediv2_clk.clkr,
	[GCC_PCIE_2B_SLV_AXI_CLK] = &gcc_pcie_2b_slv_axi_clk.clkr,
	[GCC_PCIE_2B_SLV_Q2A_AXI_CLK] = &gcc_pcie_2b_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_3A3B_CLKREF_CLK] = &gcc_pcie_3a3b_clkref_clk.clkr,
	[GCC_PCIE_3A_AUX_CLK] = &gcc_pcie_3a_aux_clk.clkr,
	[GCC_PCIE_3A_AUX_CLK_SRC] = &gcc_pcie_3a_aux_clk_src.clkr,
	[GCC_PCIE_3A_CFG_AHB_CLK] = &gcc_pcie_3a_cfg_ahb_clk.clkr,
	[GCC_PCIE_3A_MSTR_AXI_CLK] = &gcc_pcie_3a_mstr_axi_clk.clkr,
	[GCC_PCIE_3A_PHY_RCHNG_CLK_SRC] = &gcc_pcie_3a_phy_rchng_clk_src.clkr,
	[GCC_PCIE_3A_PIPE_CLK] = &gcc_pcie_3a_pipe_clk.clkr,
	[GCC_PCIE_3A_PIPE_CLK_SRC] = &gcc_pcie_3a_pipe_clk_src.clkr,
	[GCC_PCIE_3A_PIPE_DIV_CLK_SRC] = &gcc_pcie_3a_pipe_div_clk_src.clkr,
	[GCC_PCIE_3A_PIPEDIV2_CLK] = &gcc_pcie_3a_pipediv2_clk.clkr,
	[GCC_PCIE_3A_SLV_AXI_CLK] = &gcc_pcie_3a_slv_axi_clk.clkr,
	[GCC_PCIE_3A_SLV_Q2A_AXI_CLK] = &gcc_pcie_3a_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_3B_AUX_CLK] = &gcc_pcie_3b_aux_clk.clkr,
	[GCC_PCIE_3B_AUX_CLK_SRC] = &gcc_pcie_3b_aux_clk_src.clkr,
	[GCC_PCIE_3B_CFG_AHB_CLK] = &gcc_pcie_3b_cfg_ahb_clk.clkr,
	[GCC_PCIE_3B_MSTR_AXI_CLK] = &gcc_pcie_3b_mstr_axi_clk.clkr,
	[GCC_PCIE_3B_PHY_RCHNG_CLK_SRC] = &gcc_pcie_3b_phy_rchng_clk_src.clkr,
	[GCC_PCIE_3B_PIPE_CLK] = &gcc_pcie_3b_pipe_clk.clkr,
	[GCC_PCIE_3B_PIPE_CLK_SRC] = &gcc_pcie_3b_pipe_clk_src.clkr,
	[GCC_PCIE_3B_PIPE_DIV_CLK_SRC] = &gcc_pcie_3b_pipe_div_clk_src.clkr,
	[GCC_PCIE_3B_PIPEDIV2_CLK] = &gcc_pcie_3b_pipediv2_clk.clkr,
	[GCC_PCIE_3B_SLV_AXI_CLK] = &gcc_pcie_3b_slv_axi_clk.clkr,
	[GCC_PCIE_3B_SLV_Q2A_AXI_CLK] = &gcc_pcie_3b_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_4_AUX_CLK] = &gcc_pcie_4_aux_clk.clkr,
	[GCC_PCIE_4_AUX_CLK_SRC] = &gcc_pcie_4_aux_clk_src.clkr,
	[GCC_PCIE_4_CFG_AHB_CLK] = &gcc_pcie_4_cfg_ahb_clk.clkr,
	[GCC_PCIE_4_CLKREF_CLK] = &gcc_pcie_4_clkref_clk.clkr,
	[GCC_PCIE_4_MSTR_AXI_CLK] = &gcc_pcie_4_mstr_axi_clk.clkr,
	[GCC_PCIE_4_PHY_RCHNG_CLK_SRC] = &gcc_pcie_4_phy_rchng_clk_src.clkr,
	[GCC_PCIE_4_PIPE_CLK] = &gcc_pcie_4_pipe_clk.clkr,
	[GCC_PCIE_4_PIPE_CLK_SRC] = &gcc_pcie_4_pipe_clk_src.clkr,
	[GCC_PCIE_4_PIPE_DIV_CLK_SRC] = &gcc_pcie_4_pipe_div_clk_src.clkr,
	[GCC_PCIE_4_PIPEDIV2_CLK] = &gcc_pcie_4_pipediv2_clk.clkr,
	[GCC_PCIE_4_SLV_AXI_CLK] = &gcc_pcie_4_slv_axi_clk.clkr,
	[GCC_PCIE_4_SLV_Q2A_AXI_CLK] = &gcc_pcie_4_slv_q2a_axi_clk.clkr,
	[GCC_PCIE_RSCC_AHB_CLK] = &gcc_pcie_rscc_ahb_clk.clkr,
	[GCC_PCIE_RSCC_XO_CLK] = &gcc_pcie_rscc_xo_clk.clkr,
	[GCC_PCIE_RSCC_XO_CLK_SRC] = &gcc_pcie_rscc_xo_clk_src.clkr,
	[GCC_PCIE_THROTTLE_CFG_CLK] = &gcc_pcie_throttle_cfg_clk.clkr,
	[GCC_PDM2_CLK] = &gcc_pdm2_clk.clkr,
	[GCC_PDM2_CLK_SRC] = &gcc_pdm2_clk_src.clkr,
	[GCC_PDM_AHB_CLK] = &gcc_pdm_ahb_clk.clkr,
	[GCC_PDM_XO4_CLK] = &gcc_pdm_xo4_clk.clkr,
	[GCC_QMIP_CAMERA_NRT_AHB_CLK] = &gcc_qmip_camera_nrt_ahb_clk.clkr,
	[GCC_QMIP_CAMERA_RT_AHB_CLK] = &gcc_qmip_camera_rt_ahb_clk.clkr,
	[GCC_QMIP_DISP1_AHB_CLK] = &gcc_qmip_disp1_ahb_clk.clkr,
	[GCC_QMIP_DISP1_ROT_AHB_CLK] = &gcc_qmip_disp1_rot_ahb_clk.clkr,
	[GCC_QMIP_DISP_AHB_CLK] = &gcc_qmip_disp_ahb_clk.clkr,
	[GCC_QMIP_DISP_ROT_AHB_CLK] = &gcc_qmip_disp_rot_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_CVP_AHB_CLK] = &gcc_qmip_video_cvp_ahb_clk.clkr,
	[GCC_QMIP_VIDEO_VCODEC_AHB_CLK] = &gcc_qmip_video_vcodec_ahb_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_2X_CLK] = &gcc_qupv3_wrap0_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP0_CORE_CLK] = &gcc_qupv3_wrap0_core_clk.clkr,
	[GCC_QUPV3_WRAP0_QSPI0_CLK] = &gcc_qupv3_wrap0_qspi0_clk.clkr,
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
	[GCC_QUPV3_WRAP0_S4_DIV_CLK_SRC] = &gcc_qupv3_wrap0_s4_div_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S5_CLK] = &gcc_qupv3_wrap0_s5_clk.clkr,
	[GCC_QUPV3_WRAP0_S5_CLK_SRC] = &gcc_qupv3_wrap0_s5_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S6_CLK] = &gcc_qupv3_wrap0_s6_clk.clkr,
	[GCC_QUPV3_WRAP0_S6_CLK_SRC] = &gcc_qupv3_wrap0_s6_clk_src.clkr,
	[GCC_QUPV3_WRAP0_S7_CLK] = &gcc_qupv3_wrap0_s7_clk.clkr,
	[GCC_QUPV3_WRAP0_S7_CLK_SRC] = &gcc_qupv3_wrap0_s7_clk_src.clkr,
	[GCC_QUPV3_WRAP1_CORE_2X_CLK] = &gcc_qupv3_wrap1_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP1_CORE_CLK] = &gcc_qupv3_wrap1_core_clk.clkr,
	[GCC_QUPV3_WRAP1_QSPI0_CLK] = &gcc_qupv3_wrap1_qspi0_clk.clkr,
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
	[GCC_QUPV3_WRAP1_S4_DIV_CLK_SRC] = &gcc_qupv3_wrap1_s4_div_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S5_CLK] = &gcc_qupv3_wrap1_s5_clk.clkr,
	[GCC_QUPV3_WRAP1_S5_CLK_SRC] = &gcc_qupv3_wrap1_s5_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S6_CLK] = &gcc_qupv3_wrap1_s6_clk.clkr,
	[GCC_QUPV3_WRAP1_S6_CLK_SRC] = &gcc_qupv3_wrap1_s6_clk_src.clkr,
	[GCC_QUPV3_WRAP1_S7_CLK] = &gcc_qupv3_wrap1_s7_clk.clkr,
	[GCC_QUPV3_WRAP1_S7_CLK_SRC] = &gcc_qupv3_wrap1_s7_clk_src.clkr,
	[GCC_QUPV3_WRAP2_CORE_2X_CLK] = &gcc_qupv3_wrap2_core_2x_clk.clkr,
	[GCC_QUPV3_WRAP2_CORE_CLK] = &gcc_qupv3_wrap2_core_clk.clkr,
	[GCC_QUPV3_WRAP2_QSPI0_CLK] = &gcc_qupv3_wrap2_qspi0_clk.clkr,
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
	[GCC_QUPV3_WRAP2_S4_DIV_CLK_SRC] = &gcc_qupv3_wrap2_s4_div_clk_src.clkr,
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
	[GCC_SYS_NOC_USB_AXI_CLK] = &gcc_sys_noc_usb_axi_clk.clkr,
	[GCC_UFS_1_CARD_CLKREF_CLK] = &gcc_ufs_1_card_clkref_clk.clkr,
	[GCC_UFS_CARD_AHB_CLK] = &gcc_ufs_card_ahb_clk.clkr,
	[GCC_UFS_CARD_AXI_CLK] = &gcc_ufs_card_axi_clk.clkr,
	[GCC_UFS_CARD_AXI_CLK_SRC] = &gcc_ufs_card_axi_clk_src.clkr,
	[GCC_UFS_CARD_AXI_HW_CTL_CLK] = &gcc_ufs_card_axi_hw_ctl_clk.clkr,
	[GCC_UFS_CARD_CLKREF_CLK] = &gcc_ufs_card_clkref_clk.clkr,
	[GCC_UFS_CARD_ICE_CORE_CLK] = &gcc_ufs_card_ice_core_clk.clkr,
	[GCC_UFS_CARD_ICE_CORE_CLK_SRC] = &gcc_ufs_card_ice_core_clk_src.clkr,
	[GCC_UFS_CARD_ICE_CORE_HW_CTL_CLK] = &gcc_ufs_card_ice_core_hw_ctl_clk.clkr,
	[GCC_UFS_CARD_PHY_AUX_CLK] = &gcc_ufs_card_phy_aux_clk.clkr,
	[GCC_UFS_CARD_PHY_AUX_CLK_SRC] = &gcc_ufs_card_phy_aux_clk_src.clkr,
	[GCC_UFS_CARD_PHY_AUX_HW_CTL_CLK] = &gcc_ufs_card_phy_aux_hw_ctl_clk.clkr,
	[GCC_UFS_CARD_RX_SYMBOL_0_CLK] = &gcc_ufs_card_rx_symbol_0_clk.clkr,
	[GCC_UFS_CARD_RX_SYMBOL_0_CLK_SRC] = &gcc_ufs_card_rx_symbol_0_clk_src.clkr,
	[GCC_UFS_CARD_RX_SYMBOL_1_CLK] = &gcc_ufs_card_rx_symbol_1_clk.clkr,
	[GCC_UFS_CARD_RX_SYMBOL_1_CLK_SRC] = &gcc_ufs_card_rx_symbol_1_clk_src.clkr,
	[GCC_UFS_CARD_TX_SYMBOL_0_CLK] = &gcc_ufs_card_tx_symbol_0_clk.clkr,
	[GCC_UFS_CARD_TX_SYMBOL_0_CLK_SRC] = &gcc_ufs_card_tx_symbol_0_clk_src.clkr,
	[GCC_UFS_CARD_UNIPRO_CORE_CLK] = &gcc_ufs_card_unipro_core_clk.clkr,
	[GCC_UFS_CARD_UNIPRO_CORE_CLK_SRC] = &gcc_ufs_card_unipro_core_clk_src.clkr,
	[GCC_UFS_CARD_UNIPRO_CORE_HW_CTL_CLK] = &gcc_ufs_card_unipro_core_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_AHB_CLK] = &gcc_ufs_phy_ahb_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK] = &gcc_ufs_phy_axi_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK_SRC] = &gcc_ufs_phy_axi_clk_src.clkr,
	[GCC_UFS_PHY_AXI_HW_CTL_CLK] = &gcc_ufs_phy_axi_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK] = &gcc_ufs_phy_ice_core_clk.clkr,
	[GCC_UFS_PHY_ICE_CORE_CLK_SRC] = &gcc_ufs_phy_ice_core_clk_src.clkr,
	[GCC_UFS_PHY_ICE_CORE_HW_CTL_CLK] = &gcc_ufs_phy_ice_core_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK] = &gcc_ufs_phy_phy_aux_clk.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK_SRC] = &gcc_ufs_phy_phy_aux_clk_src.clkr,
	[GCC_UFS_PHY_PHY_AUX_HW_CTL_CLK] = &gcc_ufs_phy_phy_aux_hw_ctl_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK] = &gcc_ufs_phy_rx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK_SRC] = &gcc_ufs_phy_rx_symbol_0_clk_src.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_1_CLK] = &gcc_ufs_phy_rx_symbol_1_clk.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_1_CLK_SRC] = &gcc_ufs_phy_rx_symbol_1_clk_src.clkr,
	[GCC_UFS_PHY_TX_SYMBOL_0_CLK] = &gcc_ufs_phy_tx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_TX_SYMBOL_0_CLK_SRC] = &gcc_ufs_phy_tx_symbol_0_clk_src.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK] = &gcc_ufs_phy_unipro_core_clk.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK_SRC] = &gcc_ufs_phy_unipro_core_clk_src.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_HW_CTL_CLK] = &gcc_ufs_phy_unipro_core_hw_ctl_clk.clkr,
	[GCC_UFS_REF_CLKREF_CLK] = &gcc_ufs_ref_clkref_clk.clkr,
	[GCC_USB2_HS0_CLKREF_CLK] = &gcc_usb2_hs0_clkref_clk.clkr,
	[GCC_USB2_HS1_CLKREF_CLK] = &gcc_usb2_hs1_clkref_clk.clkr,
	[GCC_USB2_HS2_CLKREF_CLK] = &gcc_usb2_hs2_clkref_clk.clkr,
	[GCC_USB2_HS3_CLKREF_CLK] = &gcc_usb2_hs3_clkref_clk.clkr,
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
	[GCC_USB34_PRIM_PHY_PIPE_CLK_SRC] = &gcc_usb34_prim_phy_pipe_clk_src.clkr,
	[GCC_USB34_SEC_PHY_PIPE_CLK_SRC] = &gcc_usb34_sec_phy_pipe_clk_src.clkr,
	[GCC_USB3_MP0_CLKREF_CLK] = &gcc_usb3_mp0_clkref_clk.clkr,
	[GCC_USB3_MP1_CLKREF_CLK] = &gcc_usb3_mp1_clkref_clk.clkr,
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
	[GCC_USB4_1_CFG_AHB_CLK] = &gcc_usb4_1_cfg_ahb_clk.clkr,
	[GCC_USB4_1_DP_CLK] = &gcc_usb4_1_dp_clk.clkr,
	[GCC_USB4_1_MASTER_CLK] = &gcc_usb4_1_master_clk.clkr,
	[GCC_USB4_1_MASTER_CLK_SRC] = &gcc_usb4_1_master_clk_src.clkr,
	[GCC_USB4_1_PHY_DP_CLK_SRC] = &gcc_usb4_1_phy_dp_clk_src.clkr,
	[GCC_USB4_1_PHY_P2RR2P_PIPE_CLK] = &gcc_usb4_1_phy_p2rr2p_pipe_clk.clkr,
	[GCC_USB4_1_PHY_P2RR2P_PIPE_CLK_SRC] = &gcc_usb4_1_phy_p2rr2p_pipe_clk_src.clkr,
	[GCC_USB4_1_PHY_PCIE_PIPE_CLK] = &gcc_usb4_1_phy_pcie_pipe_clk.clkr,
	[GCC_USB4_1_PHY_PCIE_PIPE_CLK_SRC] = &gcc_usb4_1_phy_pcie_pipe_clk_src.clkr,
	[GCC_USB4_1_PHY_PCIE_PIPE_MUX_CLK_SRC] = &gcc_usb4_1_phy_pcie_pipe_mux_clk_src.clkr,
	[GCC_USB4_1_PHY_PCIE_PIPEGMUX_CLK_SRC] = &gcc_usb4_1_phy_pcie_pipegmux_clk_src.clkr,
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
	[GCC_USB4_CFG_AHB_CLK] = &gcc_usb4_cfg_ahb_clk.clkr,
	[GCC_USB4_CLKREF_CLK] = &gcc_usb4_clkref_clk.clkr,
	[GCC_USB4_DP_CLK] = &gcc_usb4_dp_clk.clkr,
	[GCC_USB4_EUD_CLKREF_CLK] = &gcc_usb4_eud_clkref_clk.clkr,
	[GCC_USB4_MASTER_CLK] = &gcc_usb4_master_clk.clkr,
	[GCC_USB4_MASTER_CLK_SRC] = &gcc_usb4_master_clk_src.clkr,
	[GCC_USB4_PHY_DP_CLK_SRC] = &gcc_usb4_phy_dp_clk_src.clkr,
	[GCC_USB4_PHY_P2RR2P_PIPE_CLK] = &gcc_usb4_phy_p2rr2p_pipe_clk.clkr,
	[GCC_USB4_PHY_P2RR2P_PIPE_CLK_SRC] = &gcc_usb4_phy_p2rr2p_pipe_clk_src.clkr,
	[GCC_USB4_PHY_PCIE_PIPE_CLK] = &gcc_usb4_phy_pcie_pipe_clk.clkr,
	[GCC_USB4_PHY_PCIE_PIPE_CLK_SRC] = &gcc_usb4_phy_pcie_pipe_clk_src.clkr,
	[GCC_USB4_PHY_PCIE_PIPE_MUX_CLK_SRC] = &gcc_usb4_phy_pcie_pipe_mux_clk_src.clkr,
	[GCC_USB4_PHY_PCIE_PIPEGMUX_CLK_SRC] = &gcc_usb4_phy_pcie_pipegmux_clk_src.clkr,
	[GCC_USB4_PHY_RX0_CLK] = &gcc_usb4_phy_rx0_clk.clkr,
	[GCC_USB4_PHY_RX0_CLK_SRC] = &gcc_usb4_phy_rx0_clk_src.clkr,
	[GCC_USB4_PHY_RX1_CLK] = &gcc_usb4_phy_rx1_clk.clkr,
	[GCC_USB4_PHY_RX1_CLK_SRC] = &gcc_usb4_phy_rx1_clk_src.clkr,
	[GCC_USB4_PHY_SYS_CLK_SRC] = &gcc_usb4_phy_sys_clk_src.clkr,
	[GCC_USB4_PHY_USB_PIPE_CLK] = &gcc_usb4_phy_usb_pipe_clk.clkr,
	[GCC_USB4_SB_IF_CLK] = &gcc_usb4_sb_if_clk.clkr,
	[GCC_USB4_SB_IF_CLK_SRC] = &gcc_usb4_sb_if_clk_src.clkr,
	[GCC_USB4_SYS_CLK] = &gcc_usb4_sys_clk.clkr,
	[GCC_USB4_TMU_CLK] = &gcc_usb4_tmu_clk.clkr,
	[GCC_USB4_TMU_CLK_SRC] = &gcc_usb4_tmu_clk_src.clkr,
	[GCC_VIDEO_AXI0_CLK] = &gcc_video_axi0_clk.clkr,
	[GCC_VIDEO_AXI1_CLK] = &gcc_video_axi1_clk.clkr,
	[GCC_VIDEO_CVP_THROTTLE_CLK] = &gcc_video_cvp_throttle_clk.clkr,
	[GCC_VIDEO_VCODEC_THROTTLE_CLK] = &gcc_video_vcodec_throttle_clk.clkr,
};

static const struct qcom_reset_map gcc_sc8280xp_resets[] = {
	[GCC_EMAC0_BCR] = { 0xaa000 },
	[GCC_EMAC1_BCR] = { 0xba000 },
	[GCC_PCIE_0_LINK_DOWN_BCR] = { 0x6c014 },
	[GCC_PCIE_0_NOCSR_COM_PHY_BCR] = { 0x6c020 },
	[GCC_PCIE_0_PHY_BCR] = { 0x6c01c },
	[GCC_PCIE_0_PHY_NOCSR_COM_PHY_BCR] = { 0x6c028 },
	[GCC_PCIE_0_TUNNEL_BCR] = { 0xa4000 },
	[GCC_PCIE_1_LINK_DOWN_BCR] = { 0x8e014 },
	[GCC_PCIE_1_NOCSR_COM_PHY_BCR] = { 0x8e020 },
	[GCC_PCIE_1_PHY_BCR] = { 0x8e01c },
	[GCC_PCIE_1_PHY_NOCSR_COM_PHY_BCR] = { 0x8e000 },
	[GCC_PCIE_1_TUNNEL_BCR] = { 0x8d000 },
	[GCC_PCIE_2A_BCR] = { 0x9d000 },
	[GCC_PCIE_2A_LINK_DOWN_BCR] = { 0x9d13c },
	[GCC_PCIE_2A_NOCSR_COM_PHY_BCR] = { 0x9d148 },
	[GCC_PCIE_2A_PHY_BCR] = { 0x9d144 },
	[GCC_PCIE_2A_PHY_NOCSR_COM_PHY_BCR] = { 0x9d14c },
	[GCC_PCIE_2B_BCR] = { 0x9e000 },
	[GCC_PCIE_2B_LINK_DOWN_BCR] = { 0x9e084 },
	[GCC_PCIE_2B_NOCSR_COM_PHY_BCR] = { 0x9e090 },
	[GCC_PCIE_2B_PHY_BCR] = { 0x9e08c },
	[GCC_PCIE_2B_PHY_NOCSR_COM_PHY_BCR] = { 0x9e094 },
	[GCC_PCIE_3A_BCR] = { 0xa0000 },
	[GCC_PCIE_3A_LINK_DOWN_BCR] = { 0xa00f0 },
	[GCC_PCIE_3A_NOCSR_COM_PHY_BCR] = { 0xa00fc },
	[GCC_PCIE_3A_PHY_BCR] = { 0xa00e0 },
	[GCC_PCIE_3A_PHY_NOCSR_COM_PHY_BCR] = { 0xa00e4 },
	[GCC_PCIE_3B_BCR] = { 0xa2000 },
	[GCC_PCIE_3B_LINK_DOWN_BCR] = { 0xa20e0 },
	[GCC_PCIE_3B_NOCSR_COM_PHY_BCR] = { 0xa20ec },
	[GCC_PCIE_3B_PHY_BCR] = { 0xa20e8 },
	[GCC_PCIE_3B_PHY_NOCSR_COM_PHY_BCR] = { 0xa20f0 },
	[GCC_PCIE_4_BCR] = { 0x6b000 },
	[GCC_PCIE_4_LINK_DOWN_BCR] = { 0x6b300 },
	[GCC_PCIE_4_NOCSR_COM_PHY_BCR] = { 0x6b30c },
	[GCC_PCIE_4_PHY_BCR] = { 0x6b308 },
	[GCC_PCIE_4_PHY_NOCSR_COM_PHY_BCR] = { 0x6b310 },
	[GCC_PCIE_PHY_CFG_AHB_BCR] = { 0x6f00c },
	[GCC_PCIE_PHY_COM_BCR] = { 0x6f010 },
	[GCC_PCIE_RSCC_BCR] = { 0xae000 },
	[GCC_QUSB2PHY_HS0_MP_BCR] = { 0x12008 },
	[GCC_QUSB2PHY_HS1_MP_BCR] = { 0x1200c },
	[GCC_QUSB2PHY_HS2_MP_BCR] = { 0x12010 },
	[GCC_QUSB2PHY_HS3_MP_BCR] = { 0x12014 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x12000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x12004 },
	[GCC_SDCC2_BCR] = { 0x14000 },
	[GCC_SDCC4_BCR] = { 0x16000 },
	[GCC_UFS_CARD_BCR] = { 0x75000 },
	[GCC_UFS_PHY_BCR] = { 0x77000 },
	[GCC_USB2_PHY_PRIM_BCR] = { 0x50028 },
	[GCC_USB2_PHY_SEC_BCR] = { 0x5002c },
	[GCC_USB30_MP_BCR] = { 0xab000 },
	[GCC_USB30_PRIM_BCR] = { 0xf000 },
	[GCC_USB30_SEC_BCR] = { 0x10000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x50008 },
	[GCC_USB3_DP_PHY_SEC_BCR] = { 0x50014 },
	[GCC_USB3_PHY_PRIM_BCR] = { 0x50000 },
	[GCC_USB3_PHY_SEC_BCR] = { 0x5000c },
	[GCC_USB3_UNIPHY_MP0_BCR] = { 0x50018 },
	[GCC_USB3_UNIPHY_MP1_BCR] = { 0x5001c },
	[GCC_USB3PHY_PHY_PRIM_BCR] = { 0x50004 },
	[GCC_USB3PHY_PHY_SEC_BCR] = { 0x50010 },
	[GCC_USB3UNIPHY_PHY_MP0_BCR] = { 0x50020 },
	[GCC_USB3UNIPHY_PHY_MP1_BCR] = { 0x50024 },
	[GCC_USB4_1_BCR] = { 0xb8000 },
	[GCC_USB4_1_DP_PHY_PRIM_BCR] = { 0xb9020 },
	[GCC_USB4_1_DPPHY_AUX_BCR] = { 0xb9024 },
	[GCC_USB4_1_PHY_PRIM_BCR] = { 0xb9018 },
	[GCC_USB4_BCR] = { 0x2a000 },
	[GCC_USB4_DP_PHY_PRIM_BCR] = { 0x4a008 },
	[GCC_USB4_DPPHY_AUX_BCR] = { 0x4a00c },
	[GCC_USB4_PHY_PRIM_BCR] = { 0x4a000 },
	[GCC_USB4PHY_1_PHY_PRIM_BCR] = { 0xb901c },
	[GCC_USB4PHY_PHY_PRIM_BCR] = { 0x4a004 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x6a000 },
	[GCC_VIDEO_BCR] = { 0x28000 },
	[GCC_VIDEO_AXI0_CLK_ARES] = { .reg = 0x28010, .bit = 2, .udelay = 400 },
	[GCC_VIDEO_AXI1_CLK_ARES] = { .reg = 0x28018, .bit = 2, .udelay = 400 },
};

static struct gdsc *gcc_sc8280xp_gdscs[] = {
	[PCIE_0_TUNNEL_GDSC] = &pcie_0_tunnel_gdsc,
	[PCIE_1_TUNNEL_GDSC] = &pcie_1_tunnel_gdsc,
	[PCIE_2A_GDSC] = &pcie_2a_gdsc,
	[PCIE_2B_GDSC] = &pcie_2b_gdsc,
	[PCIE_3A_GDSC] = &pcie_3a_gdsc,
	[PCIE_3B_GDSC] = &pcie_3b_gdsc,
	[PCIE_4_GDSC] = &pcie_4_gdsc,
	[UFS_CARD_GDSC] = &ufs_card_gdsc,
	[UFS_PHY_GDSC] = &ufs_phy_gdsc,
	[USB30_MP_GDSC] = &usb30_mp_gdsc,
	[USB30_PRIM_GDSC] = &usb30_prim_gdsc,
	[USB30_SEC_GDSC] = &usb30_sec_gdsc,
	[EMAC_0_GDSC] = &emac_0_gdsc,
	[EMAC_1_GDSC] = &emac_1_gdsc,
	[USB4_1_GDSC] = &usb4_1_gdsc,
	[USB4_GDSC] = &usb4_gdsc,
	[HLOS1_VOTE_MMNOC_MMU_TBU_HF0_GDSC] = &hlos1_vote_mmnoc_mmu_tbu_hf0_gdsc,
	[HLOS1_VOTE_MMNOC_MMU_TBU_HF1_GDSC] = &hlos1_vote_mmnoc_mmu_tbu_hf1_gdsc,
	[HLOS1_VOTE_MMNOC_MMU_TBU_SF0_GDSC] = &hlos1_vote_mmnoc_mmu_tbu_sf0_gdsc,
	[HLOS1_VOTE_MMNOC_MMU_TBU_SF1_GDSC] = &hlos1_vote_mmnoc_mmu_tbu_sf1_gdsc,
	[HLOS1_VOTE_TURING_MMU_TBU0_GDSC] = &hlos1_vote_turing_mmu_tbu0_gdsc,
	[HLOS1_VOTE_TURING_MMU_TBU1_GDSC] = &hlos1_vote_turing_mmu_tbu1_gdsc,
	[HLOS1_VOTE_TURING_MMU_TBU2_GDSC] = &hlos1_vote_turing_mmu_tbu2_gdsc,
	[HLOS1_VOTE_TURING_MMU_TBU3_GDSC] = &hlos1_vote_turing_mmu_tbu3_gdsc,
};

static const struct clk_rcg_dfs_data gcc_dfs_clocks[] = {
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s6_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap0_s7_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s6_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap1_s7_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s0_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s1_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s2_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s3_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s4_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s5_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s6_clk_src),
	DEFINE_RCG_DFS(gcc_qupv3_wrap2_s7_clk_src),
};

static const struct regmap_config gcc_sc8280xp_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xc3014,
	.fast_io = true,
};

static const struct qcom_cc_desc gcc_sc8280xp_desc = {
	.config = &gcc_sc8280xp_regmap_config,
	.clks = gcc_sc8280xp_clocks,
	.num_clks = ARRAY_SIZE(gcc_sc8280xp_clocks),
	.resets = gcc_sc8280xp_resets,
	.num_resets = ARRAY_SIZE(gcc_sc8280xp_resets),
	.gdscs = gcc_sc8280xp_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_sc8280xp_gdscs),
};

static int gcc_sc8280xp_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		return ret;

	regmap = qcom_cc_map(pdev, &gcc_sc8280xp_desc);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		goto err_put_rpm;
	}

	/* Keep some clocks always-on */
	qcom_branch_set_clk_en(regmap, 0x26004); /* GCC_CAMERA_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0x26020); /* GCC_CAMERA_XO_CLK */
	qcom_branch_set_clk_en(regmap, 0x27004); /* GCC_DISP_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0x27028); /* GCC_DISP_XO_CLK */
	qcom_branch_set_clk_en(regmap, 0x71004); /* GCC_GPU_CFG_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0x28004); /* GCC_VIDEO_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0x28028); /* GCC_VIDEO_XO_CLK */
	qcom_branch_set_clk_en(regmap, 0xbb004); /* GCC_DISP1_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0xbb028); /* GCC_DISP1_XO_CLK */

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks, ARRAY_SIZE(gcc_dfs_clocks));
	if (ret)
		goto err_put_rpm;

	ret = qcom_cc_really_probe(pdev, &gcc_sc8280xp_desc, regmap);
	if (ret)
		goto err_put_rpm;

	pm_runtime_put(&pdev->dev);

	return 0;

err_put_rpm:
	pm_runtime_put_sync(&pdev->dev);

	return ret;
}

static const struct of_device_id gcc_sc8280xp_match_table[] = {
	{ .compatible = "qcom,gcc-sc8280xp" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_sc8280xp_match_table);

static struct platform_driver gcc_sc8280xp_driver = {
	.probe = gcc_sc8280xp_probe,
	.driver = {
		.name = "gcc-sc8280xp",
		.of_match_table = gcc_sc8280xp_match_table,
	},
};

static int __init gcc_sc8280xp_init(void)
{
	return platform_driver_register(&gcc_sc8280xp_driver);
}
subsys_initcall(gcc_sc8280xp_init);

static void __exit gcc_sc8280xp_exit(void)
{
	platform_driver_unregister(&gcc_sc8280xp_driver);
}
module_exit(gcc_sc8280xp_exit);

MODULE_DESCRIPTION("Qualcomm SC8280XP GCC driver");
MODULE_LICENSE("GPL");
