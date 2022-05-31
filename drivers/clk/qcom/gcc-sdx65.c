// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gcc-sdx65.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_EVEN,
	P_GPLL0_OUT_MAIN,
	P_PCIE_PIPE_CLK,
	P_SLEEP_CLK,
	P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
};

static struct clk_alpha_pll gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.enable_reg = 0x6d000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
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
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0_out_even",
		.parent_hws = (const struct clk_hw *[]){ &gpll0.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_evo_ops,
	},
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct clk_parent_data gcc_parent_data_0_ao[] = {
	{ .fw_name = "bi_tcxo_ao" },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_out_even.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpll0.clkr.hw },
	{ .fw_name = "sleep_clk" },
	{ .hw = &gpll0_out_even.clkr.hw },
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
	{ P_BI_TCXO, 2 },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_PCIE_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .fw_name = "pcie_pipe_clk"},
	{ .fw_name = "bi_tcxo"},
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK, 0 },
	{ P_BI_TCXO, 2 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .fw_name = "usb3_phy_wrapper_gcc_usb30_pipe_clk"},
	{ .fw_name = "bi_tcxo"},
};

static struct clk_regmap_mux gcc_pcie_aux_clk_src = {
	.reg = 0x43060,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_aux_clk_src",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_pcie_pipe_clk_src = {
	.reg = 0x43044,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_5,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_pipe_clk_src",
			.parent_data = gcc_parent_data_5,
			.num_parents = 2,
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static struct clk_regmap_mux gcc_usb3_phy_pipe_clk_src = {
	.reg = 0x1706c,
	.shift = 0,
	.width = 2,
	.parent_map = gcc_parent_map_6,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_phy_pipe_clk_src",
			.parent_data = gcc_parent_data_6,
			.num_parents = 2,
			.ops = &clk_regmap_mux_closest_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_qup1_i2c_apps_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_MAIN, 12, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_blsp1_qup1_i2c_apps_clk_src = {
	.cmd_rcgr = 0x1c024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_qup1_i2c_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_qup1_spi_apps_clk_src[] = {
	F(960000, P_BI_TCXO, 10, 1, 2),
	F(4800000, P_BI_TCXO, 4, 0, 0),
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(15000000, P_GPLL0_OUT_EVEN, 5, 1, 4),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(24000000, P_GPLL0_OUT_MAIN, 12.5, 1, 2),
	F(25000000, P_GPLL0_OUT_MAIN, 12, 1, 2),
	F(50000000, P_GPLL0_OUT_MAIN, 12, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_blsp1_qup1_spi_apps_clk_src = {
	.cmd_rcgr = 0x1c00c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_qup1_spi_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_blsp1_qup2_i2c_apps_clk_src = {
	.cmd_rcgr = 0x1e024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_qup2_i2c_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_blsp1_qup2_spi_apps_clk_src = {
	.cmd_rcgr = 0x1e00c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_qup2_spi_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_blsp1_qup3_i2c_apps_clk_src = {
	.cmd_rcgr = 0x20024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_qup3_i2c_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_blsp1_qup3_spi_apps_clk_src = {
	.cmd_rcgr = 0x2000c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_qup3_spi_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_blsp1_qup4_i2c_apps_clk_src = {
	.cmd_rcgr = 0x22024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_qup1_i2c_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_qup4_i2c_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_blsp1_qup4_spi_apps_clk_src = {
	.cmd_rcgr = 0x2200c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_qup1_spi_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_qup4_spi_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_uart1_apps_clk_src[] = {
	F(3686400, P_GPLL0_OUT_EVEN, 1, 192, 15625),
	F(7372800, P_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(14745600, P_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(16000000, P_GPLL0_OUT_EVEN, 1, 4, 75),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(19354839, P_GPLL0_OUT_MAIN, 15.5, 1, 2),
	F(20000000, P_GPLL0_OUT_MAIN, 15, 1, 2),
	F(20689655, P_GPLL0_OUT_MAIN, 14.5, 1, 2),
	F(21428571, P_GPLL0_OUT_MAIN, 14, 1, 2),
	F(22222222, P_GPLL0_OUT_MAIN, 13.5, 1, 2),
	F(23076923, P_GPLL0_OUT_MAIN, 13, 1, 2),
	F(24000000, P_GPLL0_OUT_MAIN, 5, 1, 5),
	F(25000000, P_GPLL0_OUT_MAIN, 12, 1, 2),
	F(26086957, P_GPLL0_OUT_MAIN, 11.5, 1, 2),
	F(27272727, P_GPLL0_OUT_MAIN, 11, 1, 2),
	F(28571429, P_GPLL0_OUT_MAIN, 10.5, 1, 2),
	F(32000000, P_GPLL0_OUT_MAIN, 1, 4, 75),
	F(40000000, P_GPLL0_OUT_MAIN, 15, 0, 0),
	F(46400000, P_GPLL0_OUT_MAIN, 1, 29, 375),
	F(48000000, P_GPLL0_OUT_MAIN, 12.5, 0, 0),
	F(51200000, P_GPLL0_OUT_MAIN, 1, 32, 375),
	F(56000000, P_GPLL0_OUT_MAIN, 1, 7, 75),
	F(58982400, P_GPLL0_OUT_MAIN, 1, 1536, 15625),
	F(60000000, P_GPLL0_OUT_MAIN, 10, 0, 0),
	F(63157895, P_GPLL0_OUT_MAIN, 9.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_blsp1_uart1_apps_clk_src = {
	.cmd_rcgr = 0x1d00c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_uart1_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_blsp1_uart2_apps_clk_src = {
	.cmd_rcgr = 0x1f00c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_uart2_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_blsp1_uart3_apps_clk_src = {
	.cmd_rcgr = 0x2100c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_uart3_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_blsp1_uart4_apps_clk_src = {
	.cmd_rcgr = 0x2300c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_blsp1_uart1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_blsp1_uart4_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_cpuss_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(133333333, P_GPLL0_OUT_MAIN, 4.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_cpuss_ahb_clk_src = {
	.cmd_rcgr = 0x3000c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_cpuss_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_cpuss_ahb_clk_src",
		.parent_data = gcc_parent_data_0_ao,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_gp1_clk_src = {
	.cmd_rcgr = 0x37004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp1_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_gp2_clk_src = {
	.cmd_rcgr = 0x38004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp2_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_gp3_clk_src = {
	.cmd_rcgr = 0x39004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_gp1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_gp3_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_aux_phy_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_aux_phy_clk_src = {
	.cmd_rcgr = 0x43048,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_pcie_aux_phy_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pcie_aux_phy_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_rchng_phy_clk_src[] = {
	F(100000000, P_GPLL0_OUT_EVEN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie_rchng_phy_clk_src = {
	.cmd_rcgr = 0x43064,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie_rchng_phy_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pcie_rchng_phy_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(60000000, P_GPLL0_OUT_MAIN, 10, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pdm2_clk_src = {
	.cmd_rcgr = 0x24010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pdm2_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pdm2_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(400000, P_BI_TCXO, 12, 1, 4),
	F(25000000, P_GPLL0_OUT_EVEN, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_EVEN, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x1a010,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb30_master_clk_src[] = {
	F(200000000, P_GPLL0_OUT_EVEN, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb30_master_clk_src = {
	.cmd_rcgr = 0x17030,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb30_master_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb30_mock_utmi_clk_src = {
	.cmd_rcgr = 0x17048,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcie_aux_phy_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb30_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb3_phy_aux_clk_src[] = {
	F(1000000, P_BI_TCXO, 1, 5, 96),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb3_phy_aux_clk_src = {
	.cmd_rcgr = 0x17070,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_usb3_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_usb3_phy_aux_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div gcc_cpuss_ahb_postdiv_clk_src = {
	.reg = 0x30024,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "gcc_cpuss_ahb_postdiv_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gcc_cpuss_ahb_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb30_mock_utmi_postdiv_clk_src = {
	.reg = 0x17060,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "gcc_usb30_mock_utmi_postdiv_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gcc_usb30_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_ahb_pcie_link_clk = {
	.halt_reg = 0x2e004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ahb_pcie_link_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_ahb_clk = {
	.halt_reg = 0x1b004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6d008,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_i2c_apps_clk = {
	.halt_reg = 0x1c008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1c008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_i2c_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_qup1_i2c_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup1_spi_apps_clk = {
	.halt_reg = 0x1c004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1c004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_spi_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_qup1_spi_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_i2c_apps_clk = {
	.halt_reg = 0x1e008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1e008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_i2c_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_qup2_i2c_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup2_spi_apps_clk = {
	.halt_reg = 0x1e004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1e004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_spi_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_qup2_spi_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_i2c_apps_clk = {
	.halt_reg = 0x20008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x20008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_i2c_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_qup3_i2c_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup3_spi_apps_clk = {
	.halt_reg = 0x20004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x20004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup3_spi_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_qup3_spi_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_i2c_apps_clk = {
	.halt_reg = 0x22008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x22008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_i2c_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_qup4_i2c_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_qup4_spi_apps_clk = {
	.halt_reg = 0x22004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x22004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup4_spi_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_qup4_spi_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_sleep_clk = {
	.halt_reg = 0x1b00c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6d008,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart1_apps_clk = {
	.halt_reg = 0x1d004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1d004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart1_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_uart1_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart2_apps_clk = {
	.halt_reg = 0x1f004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1f004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart2_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_uart2_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart3_apps_clk = {
	.halt_reg = 0x21004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x21004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart3_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_uart3_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_blsp1_uart4_apps_clk = {
	.halt_reg = 0x23004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x23004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart4_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_blsp1_uart4_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x27004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x27004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6d008,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_boot_rom_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x37000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x37000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp1_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_gp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp2_clk = {
	.halt_reg = 0x38000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x38000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp2_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_gp2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_gp3_clk = {
	.halt_reg = 0x39000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x39000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp3_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_gp3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_0_clkref_en = {
	.halt_reg = 0x88004,
	/*
	 * The clock controller does not handle the status bit for
	 * the clocks with gdscs(powerdomains) in hw controlled mode
	 * and hence avoid checking for the status bit of those clocks
	 * by setting the BRANCH_HALT_DELAY flag
	 */
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x88004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_0_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_aux_clk = {
	.halt_reg = 0x43034,
	/*
	 * The clock controller does not handle the status bit for
	 * the clocks with gdscs(powerdomains) in hw controlled mode
	 * and hence avoid checking for the status bit of those clocks
	 * by setting the BRANCH_HALT_DELAY flag
	 */
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x43034,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6d010,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_aux_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_pcie_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_cfg_ahb_clk = {
	.halt_reg = 0x4302c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4302c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6d010,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_cfg_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_mstr_axi_clk = {
	.halt_reg = 0x43024,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x43024,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6d010,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_pipe_clk = {
	.halt_reg = 0x4303c,
	/*
	 * The clock controller does not handle the status bit for
	 * the clocks with gdscs(powerdomains) in hw controlled mode
	 * and hence avoid checking for the status bit of those clocks
	 * by setting the BRANCH_HALT_DELAY flag
	 */
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x4303c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6d010,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_pipe_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_pcie_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_rchng_phy_clk = {
	.halt_reg = 0x43030,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x43030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6d010,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_rchng_phy_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_pcie_rchng_phy_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_sleep_clk = {
	.halt_reg = 0x43038,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x43038,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6d010,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_sleep_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_pcie_aux_phy_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_slv_axi_clk = {
	.halt_reg = 0x4301c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x4301c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6d010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_slv_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_slv_q2a_axi_clk = {
	.halt_reg = 0x43018,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x43018,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x6d010,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_slv_q2a_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm2_clk = {
	.halt_reg = 0x2400c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2400c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm2_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_pdm2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_ahb_clk = {
	.halt_reg = 0x24004,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x24004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x24004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pdm_xo4_clk = {
	.halt_reg = 0x24008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x24008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pdm_xo4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_rx1_usb2_clkref_en = {
	.halt_reg = 0x88008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x88008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_rx1_usb2_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x1a00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x1a004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_apps_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_sdcc1_apps_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_master_clk = {
	.halt_reg = 0x17018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_master_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_usb30_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mock_utmi_clk = {
	.halt_reg = 0x1702c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1702c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_mock_utmi_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw =
					&gcc_usb30_mock_utmi_postdiv_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_mstr_axi_clk = {
	.halt_reg = 0x17020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_mstr_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_sleep_clk = {
	.halt_reg = 0x17028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb30_slv_ahb_clk = {
	.halt_reg = 0x17024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb30_slv_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_phy_aux_clk = {
	.halt_reg = 0x17064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_phy_aux_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_usb3_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc usb30_gdsc = {
	.gdscr = 0x17004,
	.pd = {
		.name = "usb30_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc pcie_gdsc = {
	.gdscr = 0x43004,
	.pd = {
		.name = "pcie_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct clk_branch gcc_usb3_phy_pipe_clk = {
	.halt_reg = 0x17068,
	/*
	 * The clock controller does not handle the status bit for
	 * the clocks with gdscs(powerdomains) in hw controlled mode
	 * and hence avoid checking for the status bit of those clocks
	 * by setting the BRANCH_HALT_DELAY flag
	 */
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x17068,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x17068,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_phy_pipe_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gcc_usb3_phy_pipe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_prim_clkref_en = {
	.halt_reg = 0x88000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x88000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_prim_clkref_en",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb_phy_cfg_ahb2phy_clk = {
	.halt_reg = 0x19008,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x19008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x19008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb_phy_cfg_ahb2phy_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_xo_div4_clk = {
	.halt_reg = 0x2e010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_xo_div4_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_xo_pcie_link_clk = {
	.halt_reg = 0x2e008,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x2e008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2e008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_xo_pcie_link_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *gcc_sdx65_clocks[] = {
	[GCC_AHB_PCIE_LINK_CLK] = &gcc_ahb_pcie_link_clk.clkr,
	[GCC_BLSP1_AHB_CLK] = &gcc_blsp1_ahb_clk.clkr,
	[GCC_BLSP1_QUP1_I2C_APPS_CLK] = &gcc_blsp1_qup1_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP1_I2C_APPS_CLK_SRC] = &gcc_blsp1_qup1_i2c_apps_clk_src.clkr,
	[GCC_BLSP1_QUP1_SPI_APPS_CLK] = &gcc_blsp1_qup1_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP1_SPI_APPS_CLK_SRC] = &gcc_blsp1_qup1_spi_apps_clk_src.clkr,
	[GCC_BLSP1_QUP2_I2C_APPS_CLK] = &gcc_blsp1_qup2_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP2_I2C_APPS_CLK_SRC] = &gcc_blsp1_qup2_i2c_apps_clk_src.clkr,
	[GCC_BLSP1_QUP2_SPI_APPS_CLK] = &gcc_blsp1_qup2_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP2_SPI_APPS_CLK_SRC] = &gcc_blsp1_qup2_spi_apps_clk_src.clkr,
	[GCC_BLSP1_QUP3_I2C_APPS_CLK] = &gcc_blsp1_qup3_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP3_I2C_APPS_CLK_SRC] = &gcc_blsp1_qup3_i2c_apps_clk_src.clkr,
	[GCC_BLSP1_QUP3_SPI_APPS_CLK] = &gcc_blsp1_qup3_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP3_SPI_APPS_CLK_SRC] = &gcc_blsp1_qup3_spi_apps_clk_src.clkr,
	[GCC_BLSP1_QUP4_I2C_APPS_CLK] = &gcc_blsp1_qup4_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP4_I2C_APPS_CLK_SRC] = &gcc_blsp1_qup4_i2c_apps_clk_src.clkr,
	[GCC_BLSP1_QUP4_SPI_APPS_CLK] = &gcc_blsp1_qup4_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP4_SPI_APPS_CLK_SRC] = &gcc_blsp1_qup4_spi_apps_clk_src.clkr,
	[GCC_BLSP1_SLEEP_CLK] = &gcc_blsp1_sleep_clk.clkr,
	[GCC_BLSP1_UART1_APPS_CLK] = &gcc_blsp1_uart1_apps_clk.clkr,
	[GCC_BLSP1_UART1_APPS_CLK_SRC] = &gcc_blsp1_uart1_apps_clk_src.clkr,
	[GCC_BLSP1_UART2_APPS_CLK] = &gcc_blsp1_uart2_apps_clk.clkr,
	[GCC_BLSP1_UART2_APPS_CLK_SRC] = &gcc_blsp1_uart2_apps_clk_src.clkr,
	[GCC_BLSP1_UART3_APPS_CLK] = &gcc_blsp1_uart3_apps_clk.clkr,
	[GCC_BLSP1_UART3_APPS_CLK_SRC] = &gcc_blsp1_uart3_apps_clk_src.clkr,
	[GCC_BLSP1_UART4_APPS_CLK] = &gcc_blsp1_uart4_apps_clk.clkr,
	[GCC_BLSP1_UART4_APPS_CLK_SRC] = &gcc_blsp1_uart4_apps_clk_src.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CPUSS_AHB_CLK_SRC] = &gcc_cpuss_ahb_clk_src.clkr,
	[GCC_CPUSS_AHB_POSTDIV_CLK_SRC] = &gcc_cpuss_ahb_postdiv_clk_src.clkr,
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
	[GCC_RX1_USB2_CLKREF_EN] = &gcc_rx1_usb2_clkref_en.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_APPS_CLK_SRC] = &gcc_sdcc1_apps_clk_src.clkr,
	[GCC_USB30_MASTER_CLK] = &gcc_usb30_master_clk.clkr,
	[GCC_USB30_MASTER_CLK_SRC] = &gcc_usb30_master_clk_src.clkr,
	[GCC_USB30_MOCK_UTMI_CLK] = &gcc_usb30_mock_utmi_clk.clkr,
	[GCC_USB30_MOCK_UTMI_CLK_SRC] = &gcc_usb30_mock_utmi_clk_src.clkr,
	[GCC_USB30_MOCK_UTMI_POSTDIV_CLK_SRC] =	&gcc_usb30_mock_utmi_postdiv_clk_src.clkr,
	[GCC_USB30_MSTR_AXI_CLK] = &gcc_usb30_mstr_axi_clk.clkr,
	[GCC_USB30_SLEEP_CLK] = &gcc_usb30_sleep_clk.clkr,
	[GCC_USB30_SLV_AHB_CLK] = &gcc_usb30_slv_ahb_clk.clkr,
	[GCC_USB3_PHY_AUX_CLK] = &gcc_usb3_phy_aux_clk.clkr,
	[GCC_USB3_PHY_AUX_CLK_SRC] = &gcc_usb3_phy_aux_clk_src.clkr,
	[GCC_USB3_PHY_PIPE_CLK] = &gcc_usb3_phy_pipe_clk.clkr,
	[GCC_USB3_PHY_PIPE_CLK_SRC] = &gcc_usb3_phy_pipe_clk_src.clkr,
	[GCC_USB3_PRIM_CLKREF_EN] = &gcc_usb3_prim_clkref_en.clkr,
	[GCC_USB_PHY_CFG_AHB2PHY_CLK] = &gcc_usb_phy_cfg_ahb2phy_clk.clkr,
	[GCC_XO_DIV4_CLK] = &gcc_xo_div4_clk.clkr,
	[GCC_XO_PCIE_LINK_CLK] = &gcc_xo_pcie_link_clk.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL0_OUT_EVEN] = &gpll0_out_even.clkr,
};

static const struct qcom_reset_map gcc_sdx65_resets[] = {
	[GCC_BLSP1_QUP1_BCR] = { 0x1c000 },
	[GCC_BLSP1_QUP2_BCR] = { 0x1e000 },
	[GCC_BLSP1_QUP3_BCR] = { 0x20000 },
	[GCC_BLSP1_QUP4_BCR] = { 0x22000 },
	[GCC_BLSP1_UART1_BCR] = { 0x1d000 },
	[GCC_BLSP1_UART2_BCR] = { 0x1f000 },
	[GCC_BLSP1_UART3_BCR] = { 0x21000 },
	[GCC_BLSP1_UART4_BCR] = { 0x23000 },
	[GCC_PCIE_BCR] = { 0x43000 },
	[GCC_PCIE_LINK_DOWN_BCR] = { 0x77000 },
	[GCC_PCIE_NOCSR_COM_PHY_BCR] = { 0x78008 },
	[GCC_PCIE_PHY_BCR] = { 0x44000 },
	[GCC_PCIE_PHY_CFG_AHB_BCR] = { 0x78000 },
	[GCC_PCIE_PHY_COM_BCR] = { 0x78004 },
	[GCC_PCIE_PHY_NOCSR_COM_PHY_BCR] = { 0x7800c },
	[GCC_PDM_BCR] = { 0x24000 },
	[GCC_QUSB2PHY_BCR] = { 0x19000 },
	[GCC_SDCC1_BCR] = { 0x1a000 },
	[GCC_TCSR_PCIE_BCR] = { 0x57000 },
	[GCC_USB30_BCR] = { 0x17000 },
	[GCC_USB3_PHY_BCR] = { 0x18000 },
	[GCC_USB3PHY_PHY_BCR] = { 0x18004 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x19004 },
};

static struct gdsc *gcc_sdx65_gdscs[] = {
	[USB30_GDSC] = &usb30_gdsc,
	[PCIE_GDSC] = &pcie_gdsc,
};

static const struct regmap_config gcc_sdx65_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1f101c,
	.fast_io = true,
};

static const struct qcom_cc_desc gcc_sdx65_desc = {
	.config = &gcc_sdx65_regmap_config,
	.clks = gcc_sdx65_clocks,
	.num_clks = ARRAY_SIZE(gcc_sdx65_clocks),
	.resets = gcc_sdx65_resets,
	.num_resets = ARRAY_SIZE(gcc_sdx65_resets),
	.gdscs = gcc_sdx65_gdscs,
	.num_gdscs = ARRAY_SIZE(gcc_sdx65_gdscs),
};

static const struct of_device_id gcc_sdx65_match_table[] = {
	{ .compatible = "qcom,gcc-sdx65" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_sdx65_match_table);

static int gcc_sdx65_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &gcc_sdx65_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);
	/*
	 * Keep the clocks always-ON as they are critical to the functioning
	 * of the system:
	 * GCC_SYS_NOC_CPUSS_AHB_CLK, GCC_CPUSS_AHB_CLK, GCC_CPUSS_GNOC_CLK
	 */
	regmap_update_bits(regmap, 0x6d008, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x6d008, BIT(21), BIT(21));
	regmap_update_bits(regmap, 0x6d008, BIT(22), BIT(22));

	return qcom_cc_really_probe(pdev, &gcc_sdx65_desc, regmap);
}

static struct platform_driver gcc_sdx65_driver = {
	.probe = gcc_sdx65_probe,
	.driver = {
		.name = "gcc-sdx65",
		.of_match_table = gcc_sdx65_match_table,
	},
};

static int __init gcc_sdx65_init(void)
{
	return platform_driver_register(&gcc_sdx65_driver);
}
subsys_initcall(gcc_sdx65_init);

static void __exit gcc_sdx65_exit(void)
{
	platform_driver_unregister(&gcc_sdx65_driver);
}
module_exit(gcc_sdx65_exit);

MODULE_DESCRIPTION("QTI GCC SDX65 Driver");
MODULE_LICENSE("GPL v2");
