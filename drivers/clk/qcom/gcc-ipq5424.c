// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018,2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,ipq5424-gcc.h>
#include <dt-bindings/reset/qcom,ipq5424-gcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-phy-mux.h"
#include "common.h"
#include "reset.h"

enum {
	DT_XO,
	DT_SLEEP_CLK,
	DT_PCIE30_PHY0_PIPE_CLK,
	DT_PCIE30_PHY1_PIPE_CLK,
	DT_PCIE30_PHY2_PIPE_CLK,
	DT_PCIE30_PHY3_PIPE_CLK,
	DT_USB_PCIE_WRAPPER_PIPE_CLK,
};

enum {
	P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC,
	P_GPLL0_OUT_AUX,
	P_GPLL0_OUT_MAIN,
	P_GPLL2_OUT_AUX,
	P_GPLL2_OUT_MAIN,
	P_GPLL4_OUT_AUX,
	P_GPLL4_OUT_MAIN,
	P_SLEEP_CLK,
	P_XO,
	P_USB3PHY_0_PIPE,
};

static const struct clk_parent_data gcc_parent_data_xo = { .index = DT_XO };

static struct clk_alpha_pll gpll0 = {
	.offset = 0x20000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0xb000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll0",
			.parent_data = &gcc_parent_data_xo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static struct clk_fixed_factor gpll0_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(const struct clk_init_data) {
		.name = "gpll0_div2",
		.parent_hws = (const struct clk_hw *[]) {
			&gpll0.clkr.hw
		},
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_alpha_pll gpll2 = {
	.offset = 0x21000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_NSS_HUAYRA],
	.clkr = {
		.enable_reg = 0xb000,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll2",
			.parent_data = &gcc_parent_data_xo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpll2_out_main[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll2_out_main = {
	.offset = 0x21000,
	.post_div_table = post_div_table_gpll2_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll2_out_main),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_NSS_HUAYRA],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpll2_out_main",
		.parent_hws = (const struct clk_hw*[]) {
			&gpll2.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static struct clk_alpha_pll gpll4 = {
	.offset = 0x22000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO],
	.clkr = {
		.enable_reg = 0xb000,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpll4",
			.parent_data = &gcc_parent_data_xo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
			/*
			 * There are no consumers for this GPLL in kernel yet,
			 * (will be added soon), so the clock framework
			 * disables this source. But some of the clocks
			 * initialized by boot loaders uses this source. So we
			 * need to keep this clock ON. Add the
			 * CLK_IGNORE_UNUSED flag so the clock will not be
			 * disabled. Once the consumer in kernel is added, we
			 * can get rid of this flag.
			 */
			.flags = CLK_IGNORE_UNUSED,
		},
	},
};

static const struct parent_map gcc_parent_map_xo[] = {
	{ P_XO, 0 },
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 4 },
};

static const struct clk_parent_data gcc_parent_data_0[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_div2.hw },
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data gcc_parent_data_1[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
};

static const struct parent_map gcc_parent_map_2[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL4_OUT_MAIN, 2 },
};

static const struct clk_parent_data gcc_parent_data_2[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_XO, 0 },
	{ P_GPLL4_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_AUX, 2 },
	{ P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 4 },
};

static const struct clk_parent_data gcc_parent_data_3[] = {
	{ .index = DT_XO },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_div2.hw },
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_XO, 0 },
};

static const struct clk_parent_data gcc_parent_data_4[] = {
	{ .index = DT_XO },
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_XO, 0 },
	{ P_GPLL4_OUT_AUX, 1 },
	{ P_GPLL0_OUT_MAIN, 3 },
	{ P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 4 },
};

static const struct clk_parent_data gcc_parent_data_5[] = {
	{ .index = DT_XO },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0_div2.hw },
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_SLEEP_CLK, 6 },
};

static const struct clk_parent_data gcc_parent_data_6[] = {
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_AUX, 2 },
	{ P_SLEEP_CLK, 6 },
};

static const struct clk_parent_data gcc_parent_data_7[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL2_OUT_MAIN, 2 },
	{ P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 4 },
};

static const struct clk_parent_data gcc_parent_data_8[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll2_out_main.clkr.hw },
	{ .hw = &gpll0_div2.hw },
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL4_OUT_MAIN, 2 },
	{ P_GCC_GPLL0_OUT_MAIN_DIV_CLK_SRC, 4 },
};

static const struct clk_parent_data gcc_parent_data_9[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll4.clkr.hw },
	{ .hw = &gpll0_div2.hw },
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_AUX, 2 },
	{ P_SLEEP_CLK, 6 },
};

static const struct clk_parent_data gcc_parent_data_10[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map gcc_parent_map_11[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 1 },
	{ P_GPLL2_OUT_AUX, 2 },
};

static const struct clk_parent_data gcc_parent_data_11[] = {
	{ .index = DT_XO },
	{ .hw = &gpll0.clkr.hw },
	{ .hw = &gpll2.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_adss_pwm_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_adss_pwm_clk_src = {
	.cmd_rcgr = 0x1c004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_adss_pwm_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_adss_pwm_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_nss_ts_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_xo_clk_src = {
	.cmd_rcgr = 0x34004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_xo,
	.freq_tbl = ftbl_gcc_nss_ts_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_xo_clk_src",
		.parent_data = &gcc_parent_data_xo,
		.num_parents = 1,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_fixed_factor gcc_xo_div4_clk_src = {
	.mult = 1,
	.div = 4,
	.hw.init = &(const struct clk_init_data) {
		.name = "gcc_xo_div4_clk_src",
		.parent_hws = (const struct clk_hw *[]) {
			&gcc_xo_clk_src.clkr.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_rcg2 gcc_nss_ts_clk_src = {
	.cmd_rcgr = 0x17088,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_nss_ts_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_nss_ts_clk_src",
		.parent_data = gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(gcc_parent_data_4),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie0_axi_m_clk_src[] = {
	F(240000000, P_GPLL4_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie0_axi_m_clk_src = {
	.cmd_rcgr = 0x28018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie0_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie0_axi_m_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie0_axi_s_clk_src = {
	.cmd_rcgr = 0x28020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie0_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie0_axi_s_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie1_axi_m_clk_src = {
	.cmd_rcgr = 0x29018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie0_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie1_axi_m_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie1_axi_s_clk_src = {
	.cmd_rcgr = 0x29020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie0_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie1_axi_s_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie2_axi_m_clk_src[] = {
	F(266666667, P_GPLL4_OUT_MAIN, 4.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcie2_axi_m_clk_src = {
	.cmd_rcgr = 0x2a018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie2_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie2_axi_m_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie2_axi_s_clk_src = {
	.cmd_rcgr = 0x2a020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie0_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie2_axi_s_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie3_axi_m_clk_src = {
	.cmd_rcgr = 0x2b018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie2_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie3_axi_m_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie3_axi_s_clk_src = {
	.cmd_rcgr = 0x2b020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_2,
	.freq_tbl = ftbl_gcc_pcie0_axi_m_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie3_axi_s_clk_src",
		.parent_data = gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(gcc_parent_data_2),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcie_aux_clk_src[] = {
	F(20000000, P_GPLL0_OUT_MAIN, 10, 1, 4),
	{ }
};

static struct clk_rcg2 gcc_pcie_aux_clk_src = {
	.cmd_rcgr = 0x28004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_pcie_aux_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie_aux_clk_src",
		.parent_data = gcc_parent_data_7,
		.num_parents = ARRAY_SIZE(gcc_parent_data_7),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_i2c0_clk_src[] = {
	F(4800000, P_XO, 5, 0, 0),
	F(9600000, P_XO, 2.5, 0, 0),
	F(24000000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	F(64000000, P_GPLL0_OUT_MAIN, 12.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qupv3_i2c0_clk_src = {
	.cmd_rcgr = 0x2018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_i2c0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_i2c0_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_qupv3_i2c1_clk_src = {
	.cmd_rcgr = 0x3018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_i2c0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_i2c1_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_spi0_clk_src[] = {
	F(960000, P_XO, 10, 2, 5),
	F(4800000, P_XO, 5, 0, 0),
	F(9600000, P_XO, 2, 4, 5),
	F(16000000, P_GPLL0_OUT_MAIN, 10, 1, 5),
	F(24000000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_MAIN, 16, 1, 2),
	F(32000000, P_GPLL0_OUT_MAIN, 10, 2, 5),
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qupv3_spi0_clk_src = {
	.cmd_rcgr = 0x4004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_spi0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_spi0_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_qupv3_spi1_clk_src = {
	.cmd_rcgr = 0x5004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_spi0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_spi1_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qupv3_uart0_clk_src[] = {
	F(960000, P_XO, 10, 2, 5),
	F(4800000, P_XO, 5, 0, 0),
	F(9600000, P_XO, 2, 4, 5),
	F(16000000, P_GPLL0_OUT_MAIN, 10, 1, 5),
	F(24000000, P_XO, 1, 0, 0),
	F(25000000, P_GPLL0_OUT_MAIN, 16, 1, 2),
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	F(64000000, P_GPLL0_OUT_MAIN, 12.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qupv3_uart0_clk_src = {
	.cmd_rcgr = 0x202c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_uart0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_uart0_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_qupv3_uart1_clk_src = {
	.cmd_rcgr = 0x302c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_qupv3_uart0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_uart1_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, P_XO, 16, 12, 125),
	F(400000, P_XO, 12, 1, 5),
	F(24000000, P_XO, 1, 0, 0),
	F(48000000, P_GPLL2_OUT_MAIN, 12, 1, 2),
	F(96000000, P_GPLL2_OUT_MAIN, 6, 1, 2),
	F(177777778, P_GPLL0_OUT_MAIN, 4.5, 0, 0),
	F(192000000, P_GPLL2_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x33004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_apps_clk_src",
		.parent_data = gcc_parent_data_8,
		.num_parents = ARRAY_SIZE(gcc_parent_data_8),
		.ops = &clk_rcg2_floor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sdcc1_ice_core_clk_src[] = {
	F(300000000, P_GPLL4_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sdcc1_ice_core_clk_src = {
	.cmd_rcgr = 0x33018,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_sdcc1_ice_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sdcc1_ice_core_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_uniphy_sys_clk_src = {
	.cmd_rcgr = 0x17090,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_nss_ts_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_uniphy_sys_clk_src",
		.parent_data = &gcc_parent_data_xo,
		.num_parents = 1,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb0_aux_clk_src = {
	.cmd_rcgr = 0x2c018,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_nss_ts_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb0_aux_clk_src",
		.parent_data = gcc_parent_data_10,
		.num_parents = ARRAY_SIZE(gcc_parent_data_10),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb0_master_clk_src[] = {
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_usb0_master_clk_src = {
	.cmd_rcgr = 0x2c004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_usb0_master_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb0_master_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_usb0_mock_utmi_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(60000000, P_GPLL4_OUT_AUX, 10, 1, 2),
	{ }
};

static struct clk_rcg2 gcc_usb0_mock_utmi_clk_src = {
	.cmd_rcgr = 0x2c02c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_usb0_mock_utmi_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb0_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_usb1_mock_utmi_clk_src = {
	.cmd_rcgr = 0x3c004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_usb0_mock_utmi_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb1_mock_utmi_clk_src",
		.parent_data = gcc_parent_data_5,
		.num_parents = ARRAY_SIZE(gcc_parent_data_5),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_wcss_ahb_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(133333333, P_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_wcss_ahb_clk_src = {
	.cmd_rcgr = 0x25030,
	.freq_tbl = ftbl_gcc_wcss_ahb_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_wcss_ahb_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qdss_at_clk_src[] = {
	F(240000000, P_GPLL4_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qdss_at_clk_src = {
	.cmd_rcgr = 0x2d004,
	.freq_tbl = ftbl_gcc_qdss_at_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qdss_at_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qdss_tsctr_clk_src[] = {
	F(600000000, P_GPLL4_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qdss_tsctr_clk_src = {
	.cmd_rcgr = 0x2d01c,
	.freq_tbl = ftbl_gcc_qdss_tsctr_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qdss_tsctr_clk_src",
		.parent_data = gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(gcc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_fixed_factor gcc_qdss_tsctr_div2_clk_src = {
	.mult = 1,
	.div = 2,
	.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qdss_tsctr_div2_clk_src",
		.parent_hws = (const struct clk_hw *[]) {
			&gcc_qdss_tsctr_clk_src.clkr.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_fixed_factor gcc_qdss_dap_sync_clk_src = {
	.mult = 1,
	.div = 4,
	.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qdss_dap_sync_clk_src",
		.parent_hws = (const struct clk_hw *[]) {
			&gcc_qdss_tsctr_clk_src.clkr.hw
		},
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_system_noc_bfdcd_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(133333333, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(266666667, P_GPLL4_OUT_MAIN, 4.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_system_noc_bfdcd_clk_src = {
	.cmd_rcgr = 0x2e004,
	.freq_tbl = ftbl_gcc_system_noc_bfdcd_clk_src,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_system_noc_bfdcd_clk_src",
		.parent_data = gcc_parent_data_9,
		.num_parents = ARRAY_SIZE(gcc_parent_data_9),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_pcnoc_bfdcd_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_MAIN, 16, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcnoc_bfdcd_clk_src = {
	.cmd_rcgr = 0x31004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_pcnoc_bfdcd_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcnoc_bfdcd_clk_src",
		.parent_data = gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(gcc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_lpass_sway_clk_src[] = {
	F(133333333, P_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_lpass_sway_clk_src = {
	.cmd_rcgr = 0x27004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_lpass_sway_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_lpass_sway_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_lpass_axim_clk_src = {
	.cmd_rcgr = 0x2700c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_lpass_sway_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_lpass_axim_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_fixed_factor gcc_eud_at_div_clk_src = {
	.mult = 1,
	.div = 6,
	.hw.init = &(const struct clk_init_data) {
		.name = "gcc_eud_at_div_clk_src",
		.parent_hws = (const struct clk_hw *[]) {
			&gcc_qdss_at_clk_src.clkr.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static const struct freq_tbl ftbl_gcc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sleep_clk_src = {
	.cmd_rcgr = 0x3400c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_sleep_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_sleep_clk_src",
		.parent_data = gcc_parent_data_6,
		.num_parents = ARRAY_SIZE(gcc_parent_data_6),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gcc_qpic_io_macro_clk_src[] = {
	F(24000000, P_XO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 8, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(320000000, P_GPLL0_OUT_MAIN, 2.5, 0, 0),
	F(400000000, P_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_qpic_io_macro_clk_src = {
	.cmd_rcgr = 0x32004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_qpic_io_macro_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qpic_io_macro_clk_src",
		.parent_data = gcc_parent_data_11,
		.num_parents = ARRAY_SIZE(gcc_parent_data_11),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_qpic_clk_src = {
	.cmd_rcgr = 0x32020,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_qpic_io_macro_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qpic_clk_src",
		.parent_data = gcc_parent_data_11,
		.num_parents = ARRAY_SIZE(gcc_parent_data_11),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie0_rchng_clk_src = {
	.cmd_rcgr = 0x28028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_adss_pwm_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie0_rchng_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie1_rchng_clk_src = {
	.cmd_rcgr = 0x29028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_adss_pwm_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie1_rchng_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie2_rchng_clk_src = {
	.cmd_rcgr = 0x2a028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_adss_pwm_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie2_rchng_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 gcc_pcie3_rchng_clk_src = {
	.cmd_rcgr = 0x2b028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_1,
	.freq_tbl = ftbl_gcc_adss_pwm_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_pcie3_rchng_clk_src",
		.parent_data = gcc_parent_data_1,
		.num_parents = ARRAY_SIZE(gcc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_i2c0_div_clk_src = {
	.reg = 0x2020,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_i2c0_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_i2c0_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_qupv3_i2c1_div_clk_src = {
	.reg = 0x3020,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_qupv3_i2c1_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_qupv3_i2c1_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb0_mock_utmi_div_clk_src = {
	.reg = 0x2c040,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb0_mock_utmi_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb0_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gcc_usb1_mock_utmi_div_clk_src = {
	.reg = 0x3c018,
	.shift = 0,
	.width = 2,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gcc_usb1_mock_utmi_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gcc_usb1_mock_utmi_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gcc_adss_pwm_clk = {
	.halt_reg = 0x1c00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1c00c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_adss_pwm_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_adss_pwm_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_apss_dbg_clk = {
	.halt_reg = 0x2402c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x2402c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_apss_dbg_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qdss_dap_sync_clk_src.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_pcie0_1lane_s_clk = {
	.halt_reg = 0x31088,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x31088,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_pcie0_1lane_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie0_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_pcie1_1lane_s_clk = {
	.halt_reg = 0x3108c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3108c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_pcie1_1lane_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie1_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_pcie2_2lane_s_clk = {
	.halt_reg = 0x31090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x31090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_pcie2_2lane_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie2_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_pcie3_2lane_s_clk = {
	.halt_reg = 0x31094,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x31094,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_pcie3_2lane_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie3_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_usb_clk = {
	.halt_reg = 0x310a8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x310a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_usb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb0_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_mdio_ahb_clk = {
	.halt_reg = 0x17040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_mdio_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nss_ts_clk = {
	.halt_reg = 0x17018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x17018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nss_ts_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_nss_ts_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nsscc_clk = {
	.halt_reg = 0x17034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nsscc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nsscfg_clk = {
	.halt_reg = 0x1702c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1702c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nsscfg_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_atb_clk = {
	.halt_reg = 0x17014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_atb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qdss_at_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_nsscc_clk = {
	.halt_reg = 0x17030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_nsscc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_pcnoc_1_clk = {
	.halt_reg = 0x17080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_pcnoc_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_qosgen_ref_clk = {
	.halt_reg = 0x1701c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1701c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_qosgen_ref_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_xo_div4_clk_src.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_snoc_1_clk = {
	.halt_reg = 0x1707c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1707c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_snoc_1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_system_noc_bfdcd_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_snoc_clk = {
	.halt_reg = 0x17028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_snoc_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_system_noc_bfdcd_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_timeout_ref_clk = {
	.halt_reg = 0x17020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_timeout_ref_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_xo_div4_clk_src.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_nssnoc_xo_dcd_clk = {
	.halt_reg = 0x17074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x17074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_nssnoc_xo_dcd_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_ahb_clk = {
	.halt_reg = 0x28030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_aux_clk = {
	.halt_reg = 0x28070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_axi_m_clk = {
	.halt_reg = 0x28038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_axi_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie0_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_anoc_pcie0_1lane_m_clk = {
	.halt_reg = 0x2e07c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e07c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_anoc_pcie0_1lane_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie0_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_axi_s_bridge_clk = {
	.halt_reg = 0x28048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_axi_s_bridge_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie0_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_axi_s_clk = {
	.halt_reg = 0x28040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_axi_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie0_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie0_pipe_clk_src = {
	.reg = 0x28064,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "pcie0_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE30_PHY0_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_pipe_clk = {
	.halt_reg = 0x28068,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x28068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_pipe_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie0_pipe_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_ahb_clk = {
	.halt_reg = 0x29030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x29030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_aux_clk = {
	.halt_reg = 0x29074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x29074,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_axi_m_clk = {
	.halt_reg = 0x29038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x29038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_axi_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie1_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_anoc_pcie1_1lane_m_clk = {
	.halt_reg = 0x2e084,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e084,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_anoc_pcie1_1lane_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie1_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_axi_s_bridge_clk = {
	.halt_reg = 0x29048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x29048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_axi_s_bridge_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie1_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_axi_s_clk = {
	.halt_reg = 0x29040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x29040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_axi_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie1_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie1_pipe_clk_src = {
	.reg = 0x29064,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "pcie1_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE30_PHY1_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_pipe_clk = {
	.halt_reg = 0x29068,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x29068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_pipe_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie1_pipe_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,

			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie2_ahb_clk = {
	.halt_reg = 0x2a030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie2_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie2_aux_clk = {
	.halt_reg = 0x2a078,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a078,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie2_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie2_axi_m_clk = {
	.halt_reg = 0x2a038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie2_axi_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie2_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_anoc_pcie2_2lane_m_clk = {
	.halt_reg = 0x2e080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e080,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_anoc_pcie2_2lane_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie2_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie2_axi_s_bridge_clk = {
	.halt_reg = 0x2a048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie2_axi_s_bridge_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie2_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie2_axi_s_clk = {
	.halt_reg = 0x2a040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie2_axi_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie2_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie2_pipe_clk_src = {
	.reg = 0x2a064,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "pcie2_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE30_PHY2_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_branch gcc_pcie2_pipe_clk = {
	.halt_reg = 0x2a068,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x2a068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie2_pipe_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie2_pipe_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie3_ahb_clk = {
	.halt_reg = 0x2b030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie3_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie3_aux_clk = {
	.halt_reg = 0x2b07c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b07c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie3_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie3_axi_m_clk = {
	.halt_reg = 0x2b038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie3_axi_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie3_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_anoc_pcie3_2lane_m_clk = {
	.halt_reg = 0x2e090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_anoc_pcie3_2lane_m_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie3_axi_m_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie3_axi_s_bridge_clk = {
	.halt_reg = 0x2b048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie3_axi_s_bridge_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie3_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie3_axi_s_clk = {
	.halt_reg = 0x2b040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2b040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie3_axi_s_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcie3_axi_s_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_pcie3_pipe_clk_src = {
	.reg = 0x2b064,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "pcie3_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_PCIE30_PHY3_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_branch gcc_pcie3_pipe_clk = {
	.halt_reg = 0x2b068,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x2b068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie3_pipe_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie3_pipe_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_prng_ahb_clk = {
	.halt_reg = 0x13024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb004,
		.enable_mask = BIT(10),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_prng_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_ahb_mst_clk = {
	.halt_reg = 0x1014,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb004,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_ahb_mst_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_ahb_slv_clk = {
	.halt_reg = 0x102c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0xb004,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_ahb_slv_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_i2c0_clk = {
	.halt_reg = 0x2024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_i2c0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_i2c0_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_i2c1_clk = {
	.halt_reg = 0x3024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_i2c1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_i2c1_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_spi0_clk = {
	.halt_reg = 0x4020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_spi0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_spi0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_spi1_clk = {
	.halt_reg = 0x5020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_spi1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_spi1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_uart0_clk = {
	.halt_reg = 0x2040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_uart0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_uart0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qupv3_uart1_clk = {
	.halt_reg = 0x3040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qupv3_uart1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qupv3_uart1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x3303c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3303c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_sdcc1_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x3302c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3302c,
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
	.halt_reg = 0x33034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x33034,
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

static struct clk_branch gcc_uniphy0_ahb_clk = {
	.halt_reg = 0x1704c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1704c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy0_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy0_sys_clk = {
	.halt_reg = 0x17048,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x17048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy0_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_uniphy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy1_ahb_clk = {
	.halt_reg = 0x1705c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1705c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy1_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy1_sys_clk = {
	.halt_reg = 0x17058,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x17058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy1_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_uniphy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy2_ahb_clk = {
	.halt_reg = 0x1706c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1706c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy2_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_uniphy2_sys_clk = {
	.halt_reg = 0x17068,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x17068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_uniphy2_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_uniphy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_aux_clk = {
	.halt_reg = 0x2c04c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x2c04c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb0_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_master_clk = {
	.halt_reg = 0x2c044,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x2c044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb0_master_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_mock_utmi_clk = {
	.halt_reg = 0x2c050,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x2c050,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb0_mock_utmi_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb1_mock_utmi_clk = {
	.halt_reg = 0x3c024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x3c024,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb1_mock_utmi_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_usb1_mock_utmi_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_phy_cfg_ahb_clk = {
	.halt_reg = 0x2c05c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x2c05c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_phy_cfg_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb1_phy_cfg_ahb_clk = {
	.halt_reg = 0x3c01c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x3c01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb1_phy_cfg_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb1_master_clk = {
	.halt_reg = 0x3c028,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x3c028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb1_master_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap_phy_mux gcc_usb0_pipe_clk_src = {
	.reg = 0x2c074,
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_pipe_clk_src",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_USB_PCIE_WRAPPER_PIPE_CLK,
			},
			.num_parents = 1,
			.ops = &clk_regmap_phy_mux_ops,
		},
	},
};

static struct clk_branch gcc_usb0_pipe_clk = {
	.halt_reg = 0x2c054,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x2c054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_pipe_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_usb0_pipe_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_sleep_clk = {
	.halt_reg = 0x2c058,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x2c058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_sleep_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sleep_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb1_sleep_clk = {
	.halt_reg = 0x3c020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x3c020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb1_sleep_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_sleep_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cmn_12gpll_ahb_clk = {
	.halt_reg = 0x3a004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3a004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cmn_12gpll_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cmn_12gpll_sys_clk = {
	.halt_reg = 0x3a008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3a008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cmn_12gpll_sys_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_uniphy_sys_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_lpass_sway_clk = {
	.halt_reg = 0x27014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x27014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_lpass_sway_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_lpass_sway_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_cnoc_lpass_cfg_clk = {
	.halt_reg = 0x2e028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_cnoc_lpass_cfg_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_lpass_sway_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_lpass_core_axim_clk = {
	.halt_reg = 0x27018,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x27018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_lpass_core_axim_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_lpass_axim_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_snoc_lpass_clk = {
	.halt_reg = 0x31020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x31020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_snoc_lpass_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_lpass_axim_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb0_eud_at_clk = {
	.halt_reg = 0x30004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x30004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_usb0_eud_at_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_eud_at_div_clk_src.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_ahb_clk = {
	.halt_reg = 0x32010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x32010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qpic_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_pcnoc_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_clk = {
	.halt_reg = 0x32028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x32028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qpic_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qpic_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_io_macro_clk = {
	.halt_reg = 0x3200c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x3200c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qpic_io_macro_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gcc_qpic_io_macro_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qdss_dap_clk = {
	.halt_reg = 0x2d058,
	.clkr = {
		.enable_reg = 0x2d058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qdss_dap_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_qdss_dap_sync_clk_src.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qdss_at_clk = {
	.halt_reg = 0x2d034,
	.clkr = {
		.enable_reg = 0x2d034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_qdss_at_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_qdss_at_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie0_rchng_clk = {
	.halt_reg = 0x28028,
	.clkr = {
		.enable_reg = 0x28028,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie0_rchng_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie0_rchng_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie1_rchng_clk = {
	.halt_reg = 0x29028,
	.clkr = {
		.enable_reg = 0x29028,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie1_rchng_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie1_rchng_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie2_rchng_clk = {
	.halt_reg = 0x2a028,
	.clkr = {
		.enable_reg = 0x2a028,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie2_rchng_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie2_rchng_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie3_rchng_clk = {
	.halt_reg = 0x2b028,
	.clkr = {
		.enable_reg = 0x2b028,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "gcc_pcie3_rchng_clk",
			.parent_hws = (const struct clk_hw *[]) {
				&gcc_pcie3_rchng_clk_src.clkr.hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *gcc_ipq5424_clocks[] = {
	[GCC_ADSS_PWM_CLK] = &gcc_adss_pwm_clk.clkr,
	[GCC_ADSS_PWM_CLK_SRC] = &gcc_adss_pwm_clk_src.clkr,
	[GCC_APSS_DBG_CLK] = &gcc_apss_dbg_clk.clkr,
	[GCC_CNOC_PCIE0_1LANE_S_CLK] = &gcc_cnoc_pcie0_1lane_s_clk.clkr,
	[GCC_CNOC_PCIE1_1LANE_S_CLK] = &gcc_cnoc_pcie1_1lane_s_clk.clkr,
	[GCC_CNOC_PCIE2_2LANE_S_CLK] = &gcc_cnoc_pcie2_2lane_s_clk.clkr,
	[GCC_CNOC_PCIE3_2LANE_S_CLK] = &gcc_cnoc_pcie3_2lane_s_clk.clkr,
	[GCC_CNOC_USB_CLK] = &gcc_cnoc_usb_clk.clkr,
	[GCC_MDIO_AHB_CLK] = &gcc_mdio_ahb_clk.clkr,
	[GCC_NSS_TS_CLK] = &gcc_nss_ts_clk.clkr,
	[GCC_NSS_TS_CLK_SRC] = &gcc_nss_ts_clk_src.clkr,
	[GCC_NSSCC_CLK] = &gcc_nsscc_clk.clkr,
	[GCC_NSSCFG_CLK] = &gcc_nsscfg_clk.clkr,
	[GCC_NSSNOC_ATB_CLK] = &gcc_nssnoc_atb_clk.clkr,
	[GCC_NSSNOC_NSSCC_CLK] = &gcc_nssnoc_nsscc_clk.clkr,
	[GCC_NSSNOC_PCNOC_1_CLK] = &gcc_nssnoc_pcnoc_1_clk.clkr,
	[GCC_NSSNOC_QOSGEN_REF_CLK] = &gcc_nssnoc_qosgen_ref_clk.clkr,
	[GCC_NSSNOC_SNOC_1_CLK] = &gcc_nssnoc_snoc_1_clk.clkr,
	[GCC_NSSNOC_SNOC_CLK] = &gcc_nssnoc_snoc_clk.clkr,
	[GCC_NSSNOC_TIMEOUT_REF_CLK] = &gcc_nssnoc_timeout_ref_clk.clkr,
	[GCC_NSSNOC_XO_DCD_CLK] = &gcc_nssnoc_xo_dcd_clk.clkr,
	[GCC_PCIE0_AHB_CLK] = &gcc_pcie0_ahb_clk.clkr,
	[GCC_PCIE0_AUX_CLK] = &gcc_pcie0_aux_clk.clkr,
	[GCC_PCIE0_AXI_M_CLK] = &gcc_pcie0_axi_m_clk.clkr,
	[GCC_PCIE0_AXI_M_CLK_SRC] = &gcc_pcie0_axi_m_clk_src.clkr,
	[GCC_PCIE0_AXI_S_BRIDGE_CLK] = &gcc_pcie0_axi_s_bridge_clk.clkr,
	[GCC_PCIE0_AXI_S_CLK] = &gcc_pcie0_axi_s_clk.clkr,
	[GCC_PCIE0_AXI_S_CLK_SRC] = &gcc_pcie0_axi_s_clk_src.clkr,
	[GCC_PCIE0_PIPE_CLK] = &gcc_pcie0_pipe_clk.clkr,
	[GCC_ANOC_PCIE0_1LANE_M_CLK] = &gcc_anoc_pcie0_1lane_m_clk.clkr,
	[GCC_PCIE0_PIPE_CLK_SRC] = &gcc_pcie0_pipe_clk_src.clkr,
	[GCC_PCIE0_RCHNG_CLK_SRC] = &gcc_pcie0_rchng_clk_src.clkr,
	[GCC_PCIE0_RCHNG_CLK] = &gcc_pcie0_rchng_clk.clkr,
	[GCC_PCIE1_AHB_CLK] = &gcc_pcie1_ahb_clk.clkr,
	[GCC_PCIE1_AUX_CLK] = &gcc_pcie1_aux_clk.clkr,
	[GCC_PCIE1_AXI_M_CLK] = &gcc_pcie1_axi_m_clk.clkr,
	[GCC_PCIE1_AXI_M_CLK_SRC] = &gcc_pcie1_axi_m_clk_src.clkr,
	[GCC_PCIE1_AXI_S_BRIDGE_CLK] = &gcc_pcie1_axi_s_bridge_clk.clkr,
	[GCC_PCIE1_AXI_S_CLK] = &gcc_pcie1_axi_s_clk.clkr,
	[GCC_PCIE1_AXI_S_CLK_SRC] = &gcc_pcie1_axi_s_clk_src.clkr,
	[GCC_PCIE1_PIPE_CLK] = &gcc_pcie1_pipe_clk.clkr,
	[GCC_ANOC_PCIE1_1LANE_M_CLK] = &gcc_anoc_pcie1_1lane_m_clk.clkr,
	[GCC_PCIE1_PIPE_CLK_SRC] = &gcc_pcie1_pipe_clk_src.clkr,
	[GCC_PCIE1_RCHNG_CLK_SRC] = &gcc_pcie1_rchng_clk_src.clkr,
	[GCC_PCIE1_RCHNG_CLK] = &gcc_pcie1_rchng_clk.clkr,
	[GCC_PCIE2_AHB_CLK] = &gcc_pcie2_ahb_clk.clkr,
	[GCC_PCIE2_AUX_CLK] = &gcc_pcie2_aux_clk.clkr,
	[GCC_PCIE2_AXI_M_CLK] = &gcc_pcie2_axi_m_clk.clkr,
	[GCC_PCIE2_AXI_M_CLK_SRC] = &gcc_pcie2_axi_m_clk_src.clkr,
	[GCC_PCIE2_AXI_S_BRIDGE_CLK] = &gcc_pcie2_axi_s_bridge_clk.clkr,
	[GCC_PCIE2_AXI_S_CLK] = &gcc_pcie2_axi_s_clk.clkr,
	[GCC_PCIE2_AXI_S_CLK_SRC] = &gcc_pcie2_axi_s_clk_src.clkr,
	[GCC_PCIE2_PIPE_CLK] = &gcc_pcie2_pipe_clk.clkr,
	[GCC_ANOC_PCIE2_2LANE_M_CLK] = &gcc_anoc_pcie2_2lane_m_clk.clkr,
	[GCC_PCIE2_PIPE_CLK_SRC] = &gcc_pcie2_pipe_clk_src.clkr,
	[GCC_PCIE2_RCHNG_CLK_SRC] = &gcc_pcie2_rchng_clk_src.clkr,
	[GCC_PCIE2_RCHNG_CLK] = &gcc_pcie2_rchng_clk.clkr,
	[GCC_PCIE3_AHB_CLK] = &gcc_pcie3_ahb_clk.clkr,
	[GCC_PCIE3_AUX_CLK] = &gcc_pcie3_aux_clk.clkr,
	[GCC_PCIE3_AXI_M_CLK] = &gcc_pcie3_axi_m_clk.clkr,
	[GCC_PCIE3_AXI_M_CLK_SRC] = &gcc_pcie3_axi_m_clk_src.clkr,
	[GCC_PCIE3_AXI_S_BRIDGE_CLK] = &gcc_pcie3_axi_s_bridge_clk.clkr,
	[GCC_PCIE3_AXI_S_CLK] = &gcc_pcie3_axi_s_clk.clkr,
	[GCC_PCIE3_AXI_S_CLK_SRC] = &gcc_pcie3_axi_s_clk_src.clkr,
	[GCC_PCIE3_PIPE_CLK] = &gcc_pcie3_pipe_clk.clkr,
	[GCC_ANOC_PCIE3_2LANE_M_CLK] = &gcc_anoc_pcie3_2lane_m_clk.clkr,
	[GCC_PCIE3_PIPE_CLK_SRC] = &gcc_pcie3_pipe_clk_src.clkr,
	[GCC_PCIE3_RCHNG_CLK_SRC] = &gcc_pcie3_rchng_clk_src.clkr,
	[GCC_PCIE3_RCHNG_CLK] = &gcc_pcie3_rchng_clk.clkr,
	[GCC_PCIE_AUX_CLK_SRC] = &gcc_pcie_aux_clk_src.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_QUPV3_AHB_MST_CLK] = &gcc_qupv3_ahb_mst_clk.clkr,
	[GCC_QUPV3_AHB_SLV_CLK] = &gcc_qupv3_ahb_slv_clk.clkr,
	[GCC_QUPV3_I2C0_CLK] = &gcc_qupv3_i2c0_clk.clkr,
	[GCC_QUPV3_I2C0_CLK_SRC] = &gcc_qupv3_i2c0_clk_src.clkr,
	[GCC_QUPV3_I2C0_DIV_CLK_SRC] = &gcc_qupv3_i2c0_div_clk_src.clkr,
	[GCC_QUPV3_I2C1_CLK] = &gcc_qupv3_i2c1_clk.clkr,
	[GCC_QUPV3_I2C1_CLK_SRC] = &gcc_qupv3_i2c1_clk_src.clkr,
	[GCC_QUPV3_I2C1_DIV_CLK_SRC] = &gcc_qupv3_i2c1_div_clk_src.clkr,
	[GCC_QUPV3_SPI0_CLK] = &gcc_qupv3_spi0_clk.clkr,
	[GCC_QUPV3_SPI0_CLK_SRC] = &gcc_qupv3_spi0_clk_src.clkr,
	[GCC_QUPV3_SPI1_CLK] = &gcc_qupv3_spi1_clk.clkr,
	[GCC_QUPV3_SPI1_CLK_SRC] = &gcc_qupv3_spi1_clk_src.clkr,
	[GCC_QUPV3_UART0_CLK] = &gcc_qupv3_uart0_clk.clkr,
	[GCC_QUPV3_UART0_CLK_SRC] = &gcc_qupv3_uart0_clk_src.clkr,
	[GCC_QUPV3_UART1_CLK] = &gcc_qupv3_uart1_clk.clkr,
	[GCC_QUPV3_UART1_CLK_SRC] = &gcc_qupv3_uart1_clk_src.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_SDCC1_APPS_CLK_SRC] = &gcc_sdcc1_apps_clk_src.clkr,
	[GCC_SDCC1_ICE_CORE_CLK] = &gcc_sdcc1_ice_core_clk.clkr,
	[GCC_SDCC1_ICE_CORE_CLK_SRC] = &gcc_sdcc1_ice_core_clk_src.clkr,
	[GCC_UNIPHY0_AHB_CLK] = &gcc_uniphy0_ahb_clk.clkr,
	[GCC_UNIPHY0_SYS_CLK] = &gcc_uniphy0_sys_clk.clkr,
	[GCC_UNIPHY1_AHB_CLK] = &gcc_uniphy1_ahb_clk.clkr,
	[GCC_UNIPHY1_SYS_CLK] = &gcc_uniphy1_sys_clk.clkr,
	[GCC_UNIPHY2_AHB_CLK] = &gcc_uniphy2_ahb_clk.clkr,
	[GCC_UNIPHY2_SYS_CLK] = &gcc_uniphy2_sys_clk.clkr,
	[GCC_UNIPHY_SYS_CLK_SRC] = &gcc_uniphy_sys_clk_src.clkr,
	[GCC_USB0_AUX_CLK] = &gcc_usb0_aux_clk.clkr,
	[GCC_USB0_AUX_CLK_SRC] = &gcc_usb0_aux_clk_src.clkr,
	[GCC_USB0_MASTER_CLK] = &gcc_usb0_master_clk.clkr,
	[GCC_USB0_MASTER_CLK_SRC] = &gcc_usb0_master_clk_src.clkr,
	[GCC_USB0_MOCK_UTMI_CLK] = &gcc_usb0_mock_utmi_clk.clkr,
	[GCC_USB0_MOCK_UTMI_CLK_SRC] = &gcc_usb0_mock_utmi_clk_src.clkr,
	[GCC_USB0_MOCK_UTMI_DIV_CLK_SRC] = &gcc_usb0_mock_utmi_div_clk_src.clkr,
	[GCC_USB0_EUD_AT_CLK] = &gcc_usb0_eud_at_clk.clkr,
	[GCC_USB0_PIPE_CLK_SRC] = &gcc_usb0_pipe_clk_src.clkr,
	[GCC_USB1_MOCK_UTMI_CLK] = &gcc_usb1_mock_utmi_clk.clkr,
	[GCC_USB1_MOCK_UTMI_CLK_SRC] = &gcc_usb1_mock_utmi_clk_src.clkr,
	[GCC_USB1_MOCK_UTMI_DIV_CLK_SRC] = &gcc_usb1_mock_utmi_div_clk_src.clkr,
	[GCC_USB0_PHY_CFG_AHB_CLK] = &gcc_usb0_phy_cfg_ahb_clk.clkr,
	[GCC_USB1_PHY_CFG_AHB_CLK] = &gcc_usb1_phy_cfg_ahb_clk.clkr,
	[GCC_USB0_PIPE_CLK] = &gcc_usb0_pipe_clk.clkr,
	[GCC_USB0_SLEEP_CLK] = &gcc_usb0_sleep_clk.clkr,
	[GCC_USB1_SLEEP_CLK] = &gcc_usb1_sleep_clk.clkr,
	[GCC_USB1_MASTER_CLK] = &gcc_usb1_master_clk.clkr,
	[GCC_WCSS_AHB_CLK_SRC] = &gcc_wcss_ahb_clk_src.clkr,
	[GCC_CMN_12GPLL_AHB_CLK] = &gcc_cmn_12gpll_ahb_clk.clkr,
	[GCC_CMN_12GPLL_SYS_CLK] = &gcc_cmn_12gpll_sys_clk.clkr,
	[GCC_LPASS_SWAY_CLK] = &gcc_lpass_sway_clk.clkr,
	[GCC_CNOC_LPASS_CFG_CLK] = &gcc_cnoc_lpass_cfg_clk.clkr,
	[GCC_LPASS_CORE_AXIM_CLK] = &gcc_lpass_core_axim_clk.clkr,
	[GCC_SNOC_LPASS_CLK] = &gcc_snoc_lpass_clk.clkr,
	[GCC_QDSS_AT_CLK_SRC] = &gcc_qdss_at_clk_src.clkr,
	[GCC_QDSS_TSCTR_CLK_SRC] = &gcc_qdss_tsctr_clk_src.clkr,
	[GCC_SYSTEM_NOC_BFDCD_CLK_SRC] = &gcc_system_noc_bfdcd_clk_src.clkr,
	[GCC_PCNOC_BFDCD_CLK_SRC] = &gcc_pcnoc_bfdcd_clk_src.clkr,
	[GCC_LPASS_SWAY_CLK_SRC] = &gcc_lpass_sway_clk_src.clkr,
	[GCC_LPASS_AXIM_CLK_SRC] = &gcc_lpass_axim_clk_src.clkr,
	[GCC_SLEEP_CLK_SRC] = &gcc_sleep_clk_src.clkr,
	[GCC_QPIC_IO_MACRO_CLK] = &gcc_qpic_io_macro_clk.clkr,
	[GCC_QPIC_IO_MACRO_CLK_SRC] = &gcc_qpic_io_macro_clk_src.clkr,
	[GCC_QPIC_CLK] = &gcc_qpic_clk.clkr,
	[GCC_QPIC_CLK_SRC] = &gcc_qpic_clk_src.clkr,
	[GCC_QPIC_AHB_CLK] = &gcc_qpic_ahb_clk.clkr,
	[GCC_XO_CLK_SRC] = &gcc_xo_clk_src.clkr,
	[GCC_QDSS_DAP_CLK] = &gcc_qdss_dap_clk.clkr,
	[GCC_QDSS_AT_CLK] = &gcc_qdss_at_clk.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL2] = &gpll2.clkr,
	[GPLL2_OUT_MAIN] = &gpll2_out_main.clkr,
	[GPLL4] = &gpll4.clkr,
};

static const struct qcom_reset_map gcc_ipq5424_resets[] = {
	[GCC_QUPV3_BCR] = { 0x01000, 0 },
	[GCC_QUPV3_I2C0_BCR] = { 0x02000, 0 },
	[GCC_QUPV3_UART0_BCR] = { 0x02020, 0 },
	[GCC_QUPV3_I2C1_BCR] = { 0x03000, 0 },
	[GCC_QUPV3_UART1_BCR] = { 0x03028, 0 },
	[GCC_QUPV3_SPI0_BCR] = { 0x04000, 0 },
	[GCC_QUPV3_SPI1_BCR] = { 0x05000, 0 },
	[GCC_IMEM_BCR] = { 0x0e000, 0 },
	[GCC_TME_BCR] = { 0x100000, 0 },
	[GCC_DDRSS_BCR] = { 0x11000, 0 },
	[GCC_PRNG_BCR] = { 0x13020, 0 },
	[GCC_BOOT_ROM_BCR] = { 0x13028, 0 },
	[GCC_NSS_BCR] = { 0x17000, 0 },
	[GCC_MDIO_BCR] = { 0x1703c, 0 },
	[GCC_UNIPHY0_BCR] = { 0x17044, 0 },
	[GCC_UNIPHY1_BCR] = { 0x17054, 0 },
	[GCC_UNIPHY2_BCR] = { 0x17064, 0 },
	[GCC_WCSS_BCR] = { 0x18004, 0 },
	[GCC_SEC_CTRL_BCR] = { 0x1a000, 0 },
	[GCC_TME_SEC_BUS_BCR] = { 0xa1030, 0 },
	[GCC_ADSS_BCR] = { 0x1c000, 0 },
	[GCC_LPASS_BCR] = { 0x27000, 0 },
	[GCC_PCIE0_BCR] = { 0x28000, 0 },
	[GCC_PCIE0_LINK_DOWN_BCR] = { 0x28054, 0 },
	[GCC_PCIE0PHY_PHY_BCR] = { 0x2805c, 0 },
	[GCC_PCIE0_PHY_BCR] = { 0x28060, 0 },
	[GCC_PCIE1_BCR] = { 0x29000, 0 },
	[GCC_PCIE1_LINK_DOWN_BCR] = { 0x29054, 0 },
	[GCC_PCIE1PHY_PHY_BCR] = { 0x2905c, 0 },
	[GCC_PCIE1_PHY_BCR] = { 0x29060, 0 },
	[GCC_PCIE2_BCR] = { 0x2a000, 0 },
	[GCC_PCIE2_LINK_DOWN_BCR] = { 0x2a054, 0 },
	[GCC_PCIE2PHY_PHY_BCR] = { 0x2a05c, 0 },
	[GCC_PCIE2_PHY_BCR] = { 0x2a060, 0 },
	[GCC_PCIE3_BCR] = { 0x2b000, 0 },
	[GCC_PCIE3_LINK_DOWN_BCR] = { 0x2b054, 0 },
	[GCC_PCIE3PHY_PHY_BCR] = { 0x2b05c, 0 },
	[GCC_PCIE3_PHY_BCR] = { 0x2b060, 0 },
	[GCC_USB_BCR] = { 0x2c000, 0 },
	[GCC_QUSB2_0_PHY_BCR] = { 0x2c068, 0 },
	[GCC_USB0_PHY_BCR] = { 0x2c06c, 0 },
	[GCC_USB3PHY_0_PHY_BCR] = { 0x2c070, 0 },
	[GCC_QDSS_BCR] = { 0x2d000, 0 },
	[GCC_SNOC_BCR] = { 0x2e000, 0 },
	[GCC_ANOC_BCR] = { 0x2e074, 0 },
	[GCC_PCNOC_BCR] = { 0x31000, 0 },
	[GCC_PCNOC_BUS_TIMEOUT0_BCR] = { 0x31030, 0 },
	[GCC_PCNOC_BUS_TIMEOUT1_BCR] = { 0x31038, 0 },
	[GCC_PCNOC_BUS_TIMEOUT2_BCR] = { 0x31040, 0 },
	[GCC_PCNOC_BUS_TIMEOUT3_BCR] = { 0x31048, 0 },
	[GCC_PCNOC_BUS_TIMEOUT4_BCR] = { 0x31050, 0 },
	[GCC_PCNOC_BUS_TIMEOUT5_BCR] = { 0x31058, 0 },
	[GCC_PCNOC_BUS_TIMEOUT6_BCR] = { 0x31060, 0 },
	[GCC_PCNOC_BUS_TIMEOUT7_BCR] = { 0x31068, 0 },
	[GCC_PCNOC_BUS_TIMEOUT8_BCR] = { 0x31070, 0 },
	[GCC_PCNOC_BUS_TIMEOUT9_BCR] = { 0x31078, 0 },
	[GCC_QPIC_BCR] = { 0x32000, 0 },
	[GCC_SDCC_BCR] = { 0x33000, 0 },
	[GCC_DCC_BCR] = { 0x35000, 0 },
	[GCC_SPDM_BCR] = { 0x36000, 0 },
	[GCC_MPM_BCR] = { 0x37000, 0 },
	[GCC_APC0_VOLTAGE_DROOP_DETECTOR_BCR] = { 0x38000, 0 },
	[GCC_RBCPR_BCR] = { 0x39000, 0 },
	[GCC_CMN_BLK_BCR] = { 0x3a000, 0 },
	[GCC_TCSR_BCR] = { 0x3d000, 0 },
	[GCC_TLMM_BCR] = { 0x3e000, 0 },
	[GCC_QUPV3_AHB_MST_ARES] = { 0x01014, 2 },
	[GCC_QUPV3_CORE_ARES] = { 0x01018, 2 },
	[GCC_QUPV3_2X_CORE_ARES] = { 0x01020, 2 },
	[GCC_QUPV3_SLEEP_ARES] = { 0x01028, 2 },
	[GCC_QUPV3_AHB_SLV_ARES] = { 0x0102c, 2 },
	[GCC_QUPV3_I2C0_ARES] = { 0x02024, 2 },
	[GCC_QUPV3_UART0_ARES] = { 0x02040, 2 },
	[GCC_QUPV3_I2C1_ARES] = { 0x03024, 2 },
	[GCC_QUPV3_UART1_ARES] = { 0x03040, 2 },
	[GCC_QUPV3_SPI0_ARES] = { 0x04020, 2 },
	[GCC_QUPV3_SPI1_ARES] = { 0x05020, 2 },
	[GCC_DEBUG_ARES] = { 0x06068, 2 },
	[GCC_GP1_ARES] = { 0x08018, 2 },
	[GCC_GP2_ARES] = { 0x09018, 2 },
	[GCC_GP3_ARES] = { 0x0a018, 2 },
	[GCC_IMEM_AXI_ARES] = { 0x0e004, 2 },
	[GCC_IMEM_CFG_AHB_ARES] = { 0x0e00c, 2 },
	[GCC_TME_ARES] = { 0x100b4, 2 },
	[GCC_TME_TS_ARES] = { 0x100c0, 2 },
	[GCC_TME_SLOW_ARES] = { 0x100d0, 2 },
	[GCC_TME_RTC_TOGGLE_ARES] = { 0x100d8, 2 },
	[GCC_TIC_ARES] = { 0x12004, 2 },
	[GCC_PRNG_AHB_ARES] = { 0x13024, 2 },
	[GCC_BOOT_ROM_AHB_ARES] = { 0x1302c, 2 },
	[GCC_NSSNOC_ATB_ARES] = { 0x17014, 2 },
	[GCC_NSS_TS_ARES] = { 0x17018, 2 },
	[GCC_NSSNOC_QOSGEN_REF_ARES] = { 0x1701c, 2 },
	[GCC_NSSNOC_TIMEOUT_REF_ARES] = { 0x17020, 2 },
	[GCC_NSSNOC_MEMNOC_ARES] = { 0x17024, 2 },
	[GCC_NSSNOC_SNOC_ARES] = { 0x17028, 2 },
	[GCC_NSSCFG_ARES] = { 0x1702c, 2 },
	[GCC_NSSNOC_NSSCC_ARES] = { 0x17030, 2 },
	[GCC_NSSCC_ARES] = { 0x17034, 2 },
	[GCC_MDIO_AHB_ARES] = { 0x17040, 2 },
	[GCC_UNIPHY0_SYS_ARES] = { 0x17048, 2 },
	[GCC_UNIPHY0_AHB_ARES] = { 0x1704c, 2 },
	[GCC_UNIPHY1_SYS_ARES] = { 0x17058, 2 },
	[GCC_UNIPHY1_AHB_ARES] = { 0x1705c, 2 },
	[GCC_UNIPHY2_SYS_ARES] = { 0x17068, 2 },
	[GCC_UNIPHY2_AHB_ARES] = { 0x1706c, 2 },
	[GCC_NSSNOC_XO_DCD_ARES] = { 0x17074, 2 },
	[GCC_NSSNOC_SNOC_1_ARES] = { 0x1707c, 2 },
	[GCC_NSSNOC_PCNOC_1_ARES] = { 0x17080, 2 },
	[GCC_NSSNOC_MEMNOC_1_ARES] = { 0x17084, 2 },
	[GCC_DDRSS_ATB_ARES] = { 0x19004, 2 },
	[GCC_DDRSS_AHB_ARES] = { 0x19008, 2 },
	[GCC_GEMNOC_AHB_ARES] = { 0x1900c, 2 },
	[GCC_GEMNOC_Q6_AXI_ARES] = { 0x19010, 2 },
	[GCC_GEMNOC_NSSNOC_ARES] = { 0x19014, 2 },
	[GCC_GEMNOC_SNOC_ARES] = { 0x19018, 2 },
	[GCC_GEMNOC_APSS_ARES] = { 0x1901c, 2 },
	[GCC_GEMNOC_QOSGEN_EXTREF_ARES] = { 0x19024, 2 },
	[GCC_GEMNOC_TS_ARES] = { 0x19028, 2 },
	[GCC_DDRSS_SMS_SLOW_ARES] = { 0x1902c, 2 },
	[GCC_GEMNOC_CNOC_ARES] = { 0x19038, 2 },
	[GCC_GEMNOC_XO_DBG_ARES] = { 0x19040, 2 },
	[GCC_GEMNOC_ANOC_ARES] = { 0x19048, 2 },
	[GCC_DDRSS_LLCC_ATB_ARES] = { 0x1904c, 2 },
	[GCC_LLCC_TPDM_CFG_ARES] = { 0x19050, 2 },
	[GCC_TME_BUS_ARES] = { 0x1a014, 2 },
	[GCC_SEC_CTRL_ACC_ARES] = { 0x1a018, 2 },
	[GCC_SEC_CTRL_ARES] = { 0x1a020, 2 },
	[GCC_SEC_CTRL_SENSE_ARES] = { 0x1a028, 2 },
	[GCC_SEC_CTRL_AHB_ARES] = { 0x1a038, 2 },
	[GCC_SEC_CTRL_BOOT_ROM_PATCH_ARES] = { 0x1a03c, 2 },
	[GCC_ADSS_PWM_ARES] = { 0x1c00c, 2 },
	[GCC_TME_ATB_ARES] = { 0x1e030, 2 },
	[GCC_TME_DBGAPB_ARES] = { 0x1e034, 2 },
	[GCC_TME_DEBUG_ARES] = { 0x1e038, 2 },
	[GCC_TME_AT_ARES] = { 0x1e03C, 2 },
	[GCC_TME_APB_ARES] = { 0x1e040, 2 },
	[GCC_TME_DMI_DBG_HS_ARES] = { 0x1e044, 2 },
	[GCC_APSS_AHB_ARES] = { 0x24014, 2 },
	[GCC_APSS_AXI_ARES] = { 0x24018, 2 },
	[GCC_CPUSS_TRIG_ARES] = { 0x2401c, 2 },
	[GCC_APSS_DBG_ARES] = { 0x2402c, 2 },
	[GCC_APSS_TS_ARES] = { 0x24030, 2 },
	[GCC_APSS_ATB_ARES] = { 0x24034, 2 },
	[GCC_Q6_AXIM_ARES] = { 0x2500c, 2 },
	[GCC_Q6_AXIS_ARES] = { 0x25010, 2 },
	[GCC_Q6_AHB_ARES] = { 0x25014, 2 },
	[GCC_Q6_AHB_S_ARES] = { 0x25018, 2 },
	[GCC_Q6SS_ATBM_ARES] = { 0x2501c, 2 },
	[GCC_Q6_TSCTR_1TO2_ARES] = { 0x25020, 2 },
	[GCC_Q6SS_PCLKDBG_ARES] = { 0x25024, 2 },
	[GCC_Q6SS_TRIG_ARES] = { 0x25028, 2 },
	[GCC_Q6SS_BOOT_CBCR_ARES] = { 0x2502c, 2 },
	[GCC_WCSS_DBG_IFC_APB_ARES] = { 0x25038, 2 },
	[GCC_WCSS_DBG_IFC_ATB_ARES] = { 0x2503c, 2 },
	[GCC_WCSS_DBG_IFC_NTS_ARES] = { 0x25040, 2 },
	[GCC_WCSS_DBG_IFC_DAPBUS_ARES] = { 0x25044, 2 },
	[GCC_WCSS_DBG_IFC_APB_BDG_ARES] = { 0x25048, 2 },
	[GCC_WCSS_DBG_IFC_NTS_BDG_ARES] = { 0x25050, 2 },
	[GCC_WCSS_DBG_IFC_DAPBUS_BDG_ARES] = { 0x25054, 2 },
	[GCC_WCSS_ECAHB_ARES] = { 0x25058, 2 },
	[GCC_WCSS_ACMT_ARES] = { 0x2505c, 2 },
	[GCC_WCSS_AHB_S_ARES] = { 0x25060, 2 },
	[GCC_WCSS_AXI_M_ARES] = { 0x25064, 2 },
	[GCC_PCNOC_WAPSS_ARES] = { 0x25080, 2 },
	[GCC_SNOC_WAPSS_ARES] = { 0x25090, 2 },
	[GCC_LPASS_SWAY_ARES] = { 0x27014, 2 },
	[GCC_LPASS_CORE_AXIM_ARES] = { 0x27018, 2 },
	[GCC_PCIE0_AHB_ARES] = { 0x28030, 2 },
	[GCC_PCIE0_AXI_M_ARES] = { 0x28038, 2 },
	[GCC_PCIE0_AXI_S_ARES] = { 0x28040, 2 },
	[GCC_PCIE0_AXI_S_BRIDGE_ARES] = { 0x28048, 2},
	[GCC_PCIE0_PIPE_ARES] = { 0x28068, 2},
	[GCC_PCIE0_AUX_ARES] = { 0x28070, 2 },
	[GCC_PCIE1_AHB_ARES] = { 0x29030, 2 },
	[GCC_PCIE1_AXI_M_ARES] = { 0x29038, 2 },
	[GCC_PCIE1_AXI_S_ARES] = { 0x29040, 2 },
	[GCC_PCIE1_AXI_S_BRIDGE_ARES] = { 0x29048, 2 },
	[GCC_PCIE1_PIPE_ARES] = { 0x29068, 2 },
	[GCC_PCIE1_AUX_ARES] = { 0x29074, 2 },
	[GCC_PCIE2_AHB_ARES] = { 0x2a030, 2 },
	[GCC_PCIE2_AXI_M_ARES] = { 0x2a038, 2 },
	[GCC_PCIE2_AXI_S_ARES] = { 0x2a040, 2 },
	[GCC_PCIE2_AXI_S_BRIDGE_ARES] = { 0x2a048, 2 },
	[GCC_PCIE2_PIPE_ARES] = { 0x2a068, 2 },
	[GCC_PCIE2_AUX_ARES] = { 0x2a078, 2 },
	[GCC_PCIE3_AHB_ARES] = { 0x2b030, 2 },
	[GCC_PCIE3_AXI_M_ARES] = { 0x2b038, 2 },
	[GCC_PCIE3_AXI_S_ARES] = { 0x2b040, 2 },
	[GCC_PCIE3_AXI_S_BRIDGE_ARES] = { 0x2b048, 2 },
	[GCC_PCIE3_PIPE_ARES] = { 0x2b068, 2 },
	[GCC_PCIE3_AUX_ARES] = { 0x2b07C, 2 },
	[GCC_USB0_MASTER_ARES] = { 0x2c044, 2 },
	[GCC_USB0_AUX_ARES] = { 0x2c04c, 2 },
	[GCC_USB0_MOCK_UTMI_ARES] = { 0x2c050, 2 },
	[GCC_USB0_PIPE_ARES] = { 0x2c054, 2 },
	[GCC_USB0_SLEEP_ARES] = { 0x2c058, 2 },
	[GCC_USB0_PHY_CFG_AHB_ARES] = { 0x2c05c, 2 },
	[GCC_QDSS_AT_ARES] = { 0x2d034, 2 },
	[GCC_QDSS_STM_ARES] = { 0x2d03C, 2 },
	[GCC_QDSS_TRACECLKIN_ARES] = { 0x2d040, 2 },
	[GCC_QDSS_TSCTR_DIV2_ARES] = { 0x2d044, 2 },
	[GCC_QDSS_TSCTR_DIV3_ARES] = { 0x2d048, 2 },
	[GCC_QDSS_TSCTR_DIV4_ARES] = { 0x2d04c, 2 },
	[GCC_QDSS_TSCTR_DIV8_ARES] = { 0x2d050, 2 },
	[GCC_QDSS_TSCTR_DIV16_ARES] = { 0x2d054, 2 },
	[GCC_QDSS_DAP_ARES] = { 0x2d058, 2 },
	[GCC_QDSS_APB2JTAG_ARES] = { 0x2d05c, 2 },
	[GCC_QDSS_ETR_USB_ARES] = { 0x2d060, 2 },
	[GCC_QDSS_DAP_AHB_ARES] = { 0x2d064, 2 },
	[GCC_QDSS_CFG_AHB_ARES] = { 0x2d068, 2 },
	[GCC_QDSS_EUD_AT_ARES] = { 0x2d06c, 2 },
	[GCC_QDSS_TS_ARES] = { 0x2d078, 2 },
	[GCC_QDSS_USB_ARES] = { 0x2d07c, 2 },
	[GCC_SYS_NOC_AXI_ARES] = { 0x2e01c, 2 },
	[GCC_SNOC_QOSGEN_EXTREF_ARES] = { 0x2e020, 2 },
	[GCC_CNOC_LPASS_CFG_ARES] = { 0x2e028, 2 },
	[GCC_SYS_NOC_AT_ARES] = { 0x2e038, 2 },
	[GCC_SNOC_PCNOC_AHB_ARES] = { 0x2e03c, 2 },
	[GCC_SNOC_TME_ARES] = { 0x2e05c, 2 },
	[GCC_SNOC_XO_DCD_ARES] = { 0x2e060, 2 },
	[GCC_SNOC_TS_ARES] = { 0x2e068, 2 },
	[GCC_ANOC0_AXI_ARES] = { 0x2e078, 2 },
	[GCC_ANOC_PCIE0_1LANE_M_ARES] = { 0x2e07c, 2 },
	[GCC_ANOC_PCIE2_2LANE_M_ARES] = { 0x2e080, 2 },
	[GCC_ANOC_PCIE1_1LANE_M_ARES] = { 0x2e084, 2 },
	[GCC_ANOC_PCIE3_2LANE_M_ARES] = { 0x2e090, 2 },
	[GCC_ANOC_PCNOC_AHB_ARES] = { 0x2e094, 2 },
	[GCC_ANOC_QOSGEN_EXTREF_ARES] = { 0x2e098, 2 },
	[GCC_ANOC_XO_DCD_ARES] = { 0x2e09C, 2 },
	[GCC_SNOC_XO_DBG_ARES] = { 0x2e0a0, 2 },
	[GCC_AGGRNOC_ATB_ARES] = { 0x2e0ac, 2 },
	[GCC_AGGRNOC_TS_ARES] = { 0x2e0b0, 2 },
	[GCC_USB0_EUD_AT_ARES] = { 0x30004, 2 },
	[GCC_PCNOC_TIC_ARES] = { 0x31014, 2 },
	[GCC_PCNOC_AHB_ARES] = { 0x31018, 2 },
	[GCC_PCNOC_XO_DBG_ARES] = { 0x3101c, 2 },
	[GCC_SNOC_LPASS_ARES] = { 0x31020, 2 },
	[GCC_PCNOC_AT_ARES] = { 0x31024, 2 },
	[GCC_PCNOC_XO_DCD_ARES] = { 0x31028, 2 },
	[GCC_PCNOC_TS_ARES] = { 0x3102c, 2 },
	[GCC_PCNOC_BUS_TIMEOUT0_AHB_ARES] = { 0x31034, 2 },
	[GCC_PCNOC_BUS_TIMEOUT1_AHB_ARES] = { 0x3103c, 2 },
	[GCC_PCNOC_BUS_TIMEOUT2_AHB_ARES] = { 0x31044, 2 },
	[GCC_PCNOC_BUS_TIMEOUT3_AHB_ARES] = { 0x3104c, 2 },
	[GCC_PCNOC_BUS_TIMEOUT4_AHB_ARES] = { 0x31054, 2 },
	[GCC_PCNOC_BUS_TIMEOUT5_AHB_ARES] = { 0x3105c, 2 },
	[GCC_PCNOC_BUS_TIMEOUT6_AHB_ARES] = { 0x31064, 2 },
	[GCC_PCNOC_BUS_TIMEOUT7_AHB_ARES] = { 0x3106c, 2 },
	[GCC_Q6_AXIM_RESET] = { 0x2506c, 0 },
	[GCC_Q6_AXIS_RESET] = { 0x2506c, 1 },
	[GCC_Q6_AHB_S_RESET] = { 0x2506c, 2 },
	[GCC_Q6_AHB_RESET] = { 0x2506c, 3 },
	[GCC_Q6SS_DBG_RESET] = { 0x2506c, 4 },
	[GCC_WCSS_ECAHB_RESET] = { 0x25070, 0 },
	[GCC_WCSS_DBG_BDG_RESET] = { 0x25070, 1 },
	[GCC_WCSS_DBG_RESET] = { 0x25070, 2 },
	[GCC_WCSS_AXI_M_RESET] = { 0x25070, 3 },
	[GCC_WCSS_AHB_S_RESET] = { 0x25070, 4 },
	[GCC_WCSS_ACMT_RESET] = { 0x25070, 5 },
	[GCC_WCSSAON_RESET] = { 0x25074, 0 },
	[GCC_PCIE0_PIPE_RESET] = { 0x28058, 0 },
	[GCC_PCIE0_CORE_STICKY_RESET] = { 0x28058, 1 },
	[GCC_PCIE0_AXI_S_STICKY_RESET] = { 0x28058, 2 },
	[GCC_PCIE0_AXI_S_RESET] = { 0x28058, 3 },
	[GCC_PCIE0_AXI_M_STICKY_RESET] = { 0x28058, 4 },
	[GCC_PCIE0_AXI_M_RESET] = { 0x28058, 5 },
	[GCC_PCIE0_AUX_RESET] = { 0x28058, 6 },
	[GCC_PCIE0_AHB_RESET] = { 0x28058, 7 },
	[GCC_PCIE1_PIPE_RESET] = { 0x29058, 0 },
	[GCC_PCIE1_CORE_STICKY_RESET] = { 0x29058, 1 },
	[GCC_PCIE1_AXI_S_STICKY_RESET] = { 0x29058, 2 },
	[GCC_PCIE1_AXI_S_RESET] = { 0x29058, 3 },
	[GCC_PCIE1_AXI_M_STICKY_RESET] = { 0x29058, 4 },
	[GCC_PCIE1_AXI_M_RESET] = { 0x29058, 5 },
	[GCC_PCIE1_AUX_RESET] = { 0x29058, 6 },
	[GCC_PCIE1_AHB_RESET] = { 0x29058, 7 },
	[GCC_PCIE2_PIPE_RESET] = { 0x2a058, 0 },
	[GCC_PCIE2_CORE_STICKY_RESET] = { 0x2a058, 1 },
	[GCC_PCIE2_AXI_S_STICKY_RESET] = { 0x2a058, 2 },
	[GCC_PCIE2_AXI_S_RESET] = { 0x2a058, 3 },
	[GCC_PCIE2_AXI_M_STICKY_RESET] = { 0x2a058, 4 },
	[GCC_PCIE2_AXI_M_RESET] = { 0x2a058, 5 },
	[GCC_PCIE2_AUX_RESET] = { 0x2a058, 6 },
	[GCC_PCIE2_AHB_RESET] = { 0x2a058, 7 },
	[GCC_PCIE3_PIPE_RESET] = { 0x2b058, 0 },
	[GCC_PCIE3_CORE_STICKY_RESET] = { 0x2b058, 1 },
	[GCC_PCIE3_AXI_S_STICKY_RESET] = { 0x2b058, 2 },
	[GCC_PCIE3_AXI_S_RESET] = { 0x2b058, 3 },
	[GCC_PCIE3_AXI_M_STICKY_RESET] = { 0x2b058, 4 },
	[GCC_PCIE3_AXI_M_RESET] = { 0x2b058, 5 },
	[GCC_PCIE3_AUX_RESET] = { 0x2b058, 6 },
	[GCC_PCIE3_AHB_RESET] = { 0x2b058, 7 },
	[GCC_NSS_PARTIAL_RESET] = { 0x17078, 0 },
	[GCC_UNIPHY0_XPCS_ARES] = { 0x17050, 2 },
	[GCC_UNIPHY1_XPCS_ARES] = { 0x17060, 2 },
	[GCC_UNIPHY2_XPCS_ARES] = { 0x17070, 2 },
	[GCC_USB1_BCR] = { 0x3C000, 0 },
	[GCC_QUSB2_1_PHY_BCR] = { 0x3C030, 0 },
};

static const struct of_device_id gcc_ipq5424_match_table[] = {
	{ .compatible = "qcom,ipq5424-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_ipq5424_match_table);

static const struct regmap_config gcc_ipq5424_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register   = 0x3f024,
	.fast_io	= true,
};

static struct clk_hw *gcc_ipq5424_hws[] = {
	&gpll0_div2.hw,
	&gcc_xo_div4_clk_src.hw,
	&gcc_qdss_tsctr_div2_clk_src.hw,
	&gcc_qdss_dap_sync_clk_src.hw,
	&gcc_eud_at_div_clk_src.hw,
};

static const struct qcom_cc_desc gcc_ipq5424_desc = {
	.config = &gcc_ipq5424_regmap_config,
	.clks = gcc_ipq5424_clocks,
	.num_clks = ARRAY_SIZE(gcc_ipq5424_clocks),
	.resets = gcc_ipq5424_resets,
	.num_resets = ARRAY_SIZE(gcc_ipq5424_resets),
	.clk_hws = gcc_ipq5424_hws,
	.num_clk_hws = ARRAY_SIZE(gcc_ipq5424_hws),
};

static int gcc_ipq5424_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gcc_ipq5424_desc);
}

static struct platform_driver gcc_ipq5424_driver = {
	.probe = gcc_ipq5424_probe,
	.driver = {
		.name   = "qcom,gcc-ipq5424",
		.of_match_table = gcc_ipq5424_match_table,
	},
};

static int __init gcc_ipq5424_init(void)
{
	return platform_driver_register(&gcc_ipq5424_driver);
}
core_initcall(gcc_ipq5424_init);

static void __exit gcc_ipq5424_exit(void)
{
	platform_driver_unregister(&gcc_ipq5424_driver);
}
module_exit(gcc_ipq5424_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. GCC IPQ5424 Driver");
MODULE_LICENSE("GPL");
