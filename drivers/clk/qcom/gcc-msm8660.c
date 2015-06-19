/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

#include <dt-bindings/clock/qcom,gcc-msm8660.h>
#include <dt-bindings/reset/qcom,gcc-msm8660.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"

static struct clk_pll pll8 = {
	.l_reg = 0x3144,
	.m_reg = 0x3148,
	.n_reg = 0x314c,
	.config_reg = 0x3154,
	.mode_reg = 0x3140,
	.status_reg = 0x3158,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll8",
		.parent_names = (const char *[]){ "pxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap pll8_vote = {
	.enable_reg = 0x34c0,
	.enable_mask = BIT(8),
	.hw.init = &(struct clk_init_data){
		.name = "pll8_vote",
		.parent_names = (const char *[]){ "pll8" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

enum {
	P_PXO,
	P_PLL8,
	P_CXO,
};

static const struct parent_map gcc_pxo_pll8_map[] = {
	{ P_PXO, 0 },
	{ P_PLL8, 3 }
};

static const char *gcc_pxo_pll8[] = {
	"pxo",
	"pll8_vote",
};

static const struct parent_map gcc_pxo_pll8_cxo_map[] = {
	{ P_PXO, 0 },
	{ P_PLL8, 3 },
	{ P_CXO, 5 }
};

static const char *gcc_pxo_pll8_cxo[] = {
	"pxo",
	"pll8_vote",
	"cxo",
};

static struct freq_tbl clk_tbl_gsbi_uart[] = {
	{  1843200, P_PLL8, 2,  6, 625 },
	{  3686400, P_PLL8, 2, 12, 625 },
	{  7372800, P_PLL8, 2, 24, 625 },
	{ 14745600, P_PLL8, 2, 48, 625 },
	{ 16000000, P_PLL8, 4,  1,   6 },
	{ 24000000, P_PLL8, 4,  1,   4 },
	{ 32000000, P_PLL8, 4,  1,   3 },
	{ 40000000, P_PLL8, 1,  5,  48 },
	{ 46400000, P_PLL8, 1, 29, 240 },
	{ 48000000, P_PLL8, 4,  1,   2 },
	{ 51200000, P_PLL8, 1,  2,  15 },
	{ 56000000, P_PLL8, 1,  7,  48 },
	{ 58982400, P_PLL8, 1, 96, 625 },
	{ 64000000, P_PLL8, 2,  1,   3 },
	{ }
};

static struct clk_rcg gsbi1_uart_src = {
	.ns_reg = 0x29d4,
	.md_reg = 0x29d0,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x29d4,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi1_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi1_uart_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = 0x29d4,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi1_uart_clk",
			.parent_names = (const char *[]){
				"gsbi1_uart_src",
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi2_uart_src = {
	.ns_reg = 0x29f4,
	.md_reg = 0x29f0,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x29f4,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi2_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi2_uart_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 6,
	.clkr = {
		.enable_reg = 0x29f4,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi2_uart_clk",
			.parent_names = (const char *[]){
				"gsbi2_uart_src",
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi3_uart_src = {
	.ns_reg = 0x2a14,
	.md_reg = 0x2a10,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2a14,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi3_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi3_uart_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 2,
	.clkr = {
		.enable_reg = 0x2a14,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi3_uart_clk",
			.parent_names = (const char *[]){
				"gsbi3_uart_src",
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi4_uart_src = {
	.ns_reg = 0x2a34,
	.md_reg = 0x2a30,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2a34,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi4_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi4_uart_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 26,
	.clkr = {
		.enable_reg = 0x2a34,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi4_uart_clk",
			.parent_names = (const char *[]){
				"gsbi4_uart_src",
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi5_uart_src = {
	.ns_reg = 0x2a54,
	.md_reg = 0x2a50,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2a54,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi5_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi5_uart_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 22,
	.clkr = {
		.enable_reg = 0x2a54,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi5_uart_clk",
			.parent_names = (const char *[]){
				"gsbi5_uart_src",
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi6_uart_src = {
	.ns_reg = 0x2a74,
	.md_reg = 0x2a70,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2a74,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi6_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi6_uart_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 18,
	.clkr = {
		.enable_reg = 0x2a74,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi6_uart_clk",
			.parent_names = (const char *[]){
				"gsbi6_uart_src",
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi7_uart_src = {
	.ns_reg = 0x2a94,
	.md_reg = 0x2a90,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2a94,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi7_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi7_uart_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 14,
	.clkr = {
		.enable_reg = 0x2a94,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi7_uart_clk",
			.parent_names = (const char *[]){
				"gsbi7_uart_src",
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi8_uart_src = {
	.ns_reg = 0x2ab4,
	.md_reg = 0x2ab0,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2ab4,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi8_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi8_uart_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = 0x2ab4,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi8_uart_clk",
			.parent_names = (const char *[]){ "gsbi8_uart_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi9_uart_src = {
	.ns_reg = 0x2ad4,
	.md_reg = 0x2ad0,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2ad4,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi9_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi9_uart_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 6,
	.clkr = {
		.enable_reg = 0x2ad4,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi9_uart_clk",
			.parent_names = (const char *[]){ "gsbi9_uart_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi10_uart_src = {
	.ns_reg = 0x2af4,
	.md_reg = 0x2af0,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2af4,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi10_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi10_uart_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 2,
	.clkr = {
		.enable_reg = 0x2af4,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi10_uart_clk",
			.parent_names = (const char *[]){ "gsbi10_uart_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi11_uart_src = {
	.ns_reg = 0x2b14,
	.md_reg = 0x2b10,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2b14,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi11_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi11_uart_clk = {
	.halt_reg = 0x2fd4,
	.halt_bit = 17,
	.clkr = {
		.enable_reg = 0x2b14,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi11_uart_clk",
			.parent_names = (const char *[]){ "gsbi11_uart_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi12_uart_src = {
	.ns_reg = 0x2b34,
	.md_reg = 0x2b30,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2b34,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi12_uart_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi12_uart_clk = {
	.halt_reg = 0x2fd4,
	.halt_bit = 13,
	.clkr = {
		.enable_reg = 0x2b34,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi12_uart_clk",
			.parent_names = (const char *[]){ "gsbi12_uart_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_gsbi_qup[] = {
	{  1100000, P_PXO,  1, 2, 49 },
	{  5400000, P_PXO,  1, 1,  5 },
	{ 10800000, P_PXO,  1, 2,  5 },
	{ 15060000, P_PLL8, 1, 2, 51 },
	{ 24000000, P_PLL8, 4, 1,  4 },
	{ 25600000, P_PLL8, 1, 1, 15 },
	{ 27000000, P_PXO,  1, 0,  0 },
	{ 48000000, P_PLL8, 4, 1,  2 },
	{ 51200000, P_PLL8, 1, 2, 15 },
	{ }
};

static struct clk_rcg gsbi1_qup_src = {
	.ns_reg = 0x29cc,
	.md_reg = 0x29c8,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x29cc,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi1_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi1_qup_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 9,
	.clkr = {
		.enable_reg = 0x29cc,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi1_qup_clk",
			.parent_names = (const char *[]){ "gsbi1_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi2_qup_src = {
	.ns_reg = 0x29ec,
	.md_reg = 0x29e8,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x29ec,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi2_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi2_qup_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 4,
	.clkr = {
		.enable_reg = 0x29ec,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi2_qup_clk",
			.parent_names = (const char *[]){ "gsbi2_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi3_qup_src = {
	.ns_reg = 0x2a0c,
	.md_reg = 0x2a08,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2a0c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi3_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi3_qup_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 0,
	.clkr = {
		.enable_reg = 0x2a0c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi3_qup_clk",
			.parent_names = (const char *[]){ "gsbi3_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi4_qup_src = {
	.ns_reg = 0x2a2c,
	.md_reg = 0x2a28,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2a2c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi4_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi4_qup_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 24,
	.clkr = {
		.enable_reg = 0x2a2c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi4_qup_clk",
			.parent_names = (const char *[]){ "gsbi4_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi5_qup_src = {
	.ns_reg = 0x2a4c,
	.md_reg = 0x2a48,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2a4c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi5_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi5_qup_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 20,
	.clkr = {
		.enable_reg = 0x2a4c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi5_qup_clk",
			.parent_names = (const char *[]){ "gsbi5_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi6_qup_src = {
	.ns_reg = 0x2a6c,
	.md_reg = 0x2a68,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2a6c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi6_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi6_qup_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 16,
	.clkr = {
		.enable_reg = 0x2a6c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi6_qup_clk",
			.parent_names = (const char *[]){ "gsbi6_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi7_qup_src = {
	.ns_reg = 0x2a8c,
	.md_reg = 0x2a88,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2a8c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi7_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi7_qup_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 12,
	.clkr = {
		.enable_reg = 0x2a8c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi7_qup_clk",
			.parent_names = (const char *[]){ "gsbi7_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi8_qup_src = {
	.ns_reg = 0x2aac,
	.md_reg = 0x2aa8,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2aac,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi8_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi8_qup_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 8,
	.clkr = {
		.enable_reg = 0x2aac,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi8_qup_clk",
			.parent_names = (const char *[]){ "gsbi8_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi9_qup_src = {
	.ns_reg = 0x2acc,
	.md_reg = 0x2ac8,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2acc,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi9_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi9_qup_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 4,
	.clkr = {
		.enable_reg = 0x2acc,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi9_qup_clk",
			.parent_names = (const char *[]){ "gsbi9_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi10_qup_src = {
	.ns_reg = 0x2aec,
	.md_reg = 0x2ae8,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2aec,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi10_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi10_qup_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 0,
	.clkr = {
		.enable_reg = 0x2aec,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi10_qup_clk",
			.parent_names = (const char *[]){ "gsbi10_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi11_qup_src = {
	.ns_reg = 0x2b0c,
	.md_reg = 0x2b08,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2b0c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi11_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi11_qup_clk = {
	.halt_reg = 0x2fd4,
	.halt_bit = 15,
	.clkr = {
		.enable_reg = 0x2b0c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi11_qup_clk",
			.parent_names = (const char *[]){ "gsbi11_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gsbi12_qup_src = {
	.ns_reg = 0x2b2c,
	.md_reg = 0x2b28,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2b2c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi12_qup_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	},
};

static struct clk_branch gsbi12_qup_clk = {
	.halt_reg = 0x2fd4,
	.halt_bit = 11,
	.clkr = {
		.enable_reg = 0x2b2c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi12_qup_clk",
			.parent_names = (const char *[]){ "gsbi12_qup_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_gp[] = {
	{ 9600000, P_CXO,  2, 0, 0 },
	{ 13500000, P_PXO,  2, 0, 0 },
	{ 19200000, P_CXO,  1, 0, 0 },
	{ 27000000, P_PXO,  1, 0, 0 },
	{ 64000000, P_PLL8, 2, 1, 3 },
	{ 76800000, P_PLL8, 1, 1, 5 },
	{ 96000000, P_PLL8, 4, 0, 0 },
	{ 128000000, P_PLL8, 3, 0, 0 },
	{ 192000000, P_PLL8, 2, 0, 0 },
	{ }
};

static struct clk_rcg gp0_src = {
	.ns_reg = 0x2d24,
	.md_reg = 0x2d00,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_cxo_map,
	},
	.freq_tbl = clk_tbl_gp,
	.clkr = {
		.enable_reg = 0x2d24,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gp0_src",
			.parent_names = gcc_pxo_pll8_cxo,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_PARENT_GATE,
		},
	}
};

static struct clk_branch gp0_clk = {
	.halt_reg = 0x2fd8,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = 0x2d24,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gp0_clk",
			.parent_names = (const char *[]){ "gp0_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gp1_src = {
	.ns_reg = 0x2d44,
	.md_reg = 0x2d40,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_cxo_map,
	},
	.freq_tbl = clk_tbl_gp,
	.clkr = {
		.enable_reg = 0x2d44,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gp1_src",
			.parent_names = gcc_pxo_pll8_cxo,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch gp1_clk = {
	.halt_reg = 0x2fd8,
	.halt_bit = 6,
	.clkr = {
		.enable_reg = 0x2d44,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gp1_clk",
			.parent_names = (const char *[]){ "gp1_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg gp2_src = {
	.ns_reg = 0x2d64,
	.md_reg = 0x2d60,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_cxo_map,
	},
	.freq_tbl = clk_tbl_gp,
	.clkr = {
		.enable_reg = 0x2d64,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gp2_src",
			.parent_names = gcc_pxo_pll8_cxo,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch gp2_clk = {
	.halt_reg = 0x2fd8,
	.halt_bit = 5,
	.clkr = {
		.enable_reg = 0x2d64,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gp2_clk",
			.parent_names = (const char *[]){ "gp2_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch pmem_clk = {
	.hwcg_reg = 0x25a0,
	.hwcg_bit = 6,
	.halt_reg = 0x2fc8,
	.halt_bit = 20,
	.clkr = {
		.enable_reg = 0x25a0,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "pmem_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_rcg prng_src = {
	.ns_reg = 0x2e80,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 4,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.clkr.hw = {
		.init = &(struct clk_init_data){
			.name = "prng_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch prng_clk = {
	.halt_reg = 0x2fd8,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "prng_clk",
			.parent_names = (const char *[]){ "prng_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static const struct freq_tbl clk_tbl_sdc[] = {
	{    144000, P_PXO,   3, 2, 125 },
	{    400000, P_PLL8,  4, 1, 240 },
	{  16000000, P_PLL8,  4, 1,   6 },
	{  17070000, P_PLL8,  1, 2,  45 },
	{  20210000, P_PLL8,  1, 1,  19 },
	{  24000000, P_PLL8,  4, 1,   4 },
	{  48000000, P_PLL8,  4, 1,   2 },
	{ }
};

static struct clk_rcg sdc1_src = {
	.ns_reg = 0x282c,
	.md_reg = 0x2828,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = 0x282c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdc1_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch sdc1_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 6,
	.clkr = {
		.enable_reg = 0x282c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "sdc1_clk",
			.parent_names = (const char *[]){ "sdc1_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg sdc2_src = {
	.ns_reg = 0x284c,
	.md_reg = 0x2848,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = 0x284c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdc2_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch sdc2_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 5,
	.clkr = {
		.enable_reg = 0x284c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "sdc2_clk",
			.parent_names = (const char *[]){ "sdc2_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg sdc3_src = {
	.ns_reg = 0x286c,
	.md_reg = 0x2868,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = 0x286c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdc3_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch sdc3_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 4,
	.clkr = {
		.enable_reg = 0x286c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "sdc3_clk",
			.parent_names = (const char *[]){ "sdc3_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg sdc4_src = {
	.ns_reg = 0x288c,
	.md_reg = 0x2888,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = 0x288c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdc4_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch sdc4_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 3,
	.clkr = {
		.enable_reg = 0x288c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "sdc4_clk",
			.parent_names = (const char *[]){ "sdc4_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg sdc5_src = {
	.ns_reg = 0x28ac,
	.md_reg = 0x28a8,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = 0x28ac,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdc5_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch sdc5_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 2,
	.clkr = {
		.enable_reg = 0x28ac,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "sdc5_clk",
			.parent_names = (const char *[]){ "sdc5_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_tsif_ref[] = {
	{ 105000, P_PXO,  1, 1, 256 },
	{ }
};

static struct clk_rcg tsif_ref_src = {
	.ns_reg = 0x2710,
	.md_reg = 0x270c,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_tsif_ref,
	.clkr = {
		.enable_reg = 0x2710,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "tsif_ref_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch tsif_ref_clk = {
	.halt_reg = 0x2fd4,
	.halt_bit = 5,
	.clkr = {
		.enable_reg = 0x2710,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "tsif_ref_clk",
			.parent_names = (const char *[]){ "tsif_ref_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_usb[] = {
	{ 60000000, P_PLL8, 1, 5, 32 },
	{ }
};

static struct clk_rcg usb_hs1_xcvr_src = {
	.ns_reg = 0x290c,
	.md_reg = 0x2908,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_usb,
	.clkr = {
		.enable_reg = 0x290c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs1_xcvr_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch usb_hs1_xcvr_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 0,
	.clkr = {
		.enable_reg = 0x290c,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs1_xcvr_clk",
			.parent_names = (const char *[]){ "usb_hs1_xcvr_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg usb_fs1_xcvr_fs_src = {
	.ns_reg = 0x2968,
	.md_reg = 0x2964,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_usb,
	.clkr = {
		.enable_reg = 0x2968,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "usb_fs1_xcvr_fs_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static const char *usb_fs1_xcvr_fs_src_p[] = { "usb_fs1_xcvr_fs_src" };

static struct clk_branch usb_fs1_xcvr_fs_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 15,
	.clkr = {
		.enable_reg = 0x2968,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "usb_fs1_xcvr_fs_clk",
			.parent_names = usb_fs1_xcvr_fs_src_p,
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch usb_fs1_system_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 16,
	.clkr = {
		.enable_reg = 0x296c,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.parent_names = usb_fs1_xcvr_fs_src_p,
			.num_parents = 1,
			.name = "usb_fs1_system_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg usb_fs2_xcvr_fs_src = {
	.ns_reg = 0x2988,
	.md_reg = 0x2984,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pxo_pll8_map,
	},
	.freq_tbl = clk_tbl_usb,
	.clkr = {
		.enable_reg = 0x2988,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "usb_fs2_xcvr_fs_src",
			.parent_names = gcc_pxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static const char *usb_fs2_xcvr_fs_src_p[] = { "usb_fs2_xcvr_fs_src" };

static struct clk_branch usb_fs2_xcvr_fs_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 12,
	.clkr = {
		.enable_reg = 0x2988,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "usb_fs2_xcvr_fs_clk",
			.parent_names = usb_fs2_xcvr_fs_src_p,
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch usb_fs2_system_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 13,
	.clkr = {
		.enable_reg = 0x298c,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "usb_fs2_system_clk",
			.parent_names = usb_fs2_xcvr_fs_src_p,
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gsbi1_h_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 11,
	.clkr = {
		.enable_reg = 0x29c0,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi1_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gsbi2_h_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = 0x29e0,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi2_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gsbi3_h_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 3,
	.clkr = {
		.enable_reg = 0x2a00,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi3_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gsbi4_h_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 27,
	.clkr = {
		.enable_reg = 0x2a20,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi4_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gsbi5_h_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 23,
	.clkr = {
		.enable_reg = 0x2a40,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi5_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gsbi6_h_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 19,
	.clkr = {
		.enable_reg = 0x2a60,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi6_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gsbi7_h_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 15,
	.clkr = {
		.enable_reg = 0x2a80,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi7_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gsbi8_h_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 11,
	.clkr = {
		.enable_reg = 0x2aa0,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi8_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gsbi9_h_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = 0x2ac0,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi9_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gsbi10_h_clk = {
	.halt_reg = 0x2fd0,
	.halt_bit = 3,
	.clkr = {
		.enable_reg = 0x2ae0,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi10_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gsbi11_h_clk = {
	.halt_reg = 0x2fd4,
	.halt_bit = 18,
	.clkr = {
		.enable_reg = 0x2b00,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi11_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gsbi12_h_clk = {
	.halt_reg = 0x2fd4,
	.halt_bit = 14,
	.clkr = {
		.enable_reg = 0x2b20,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi12_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch tsif_h_clk = {
	.halt_reg = 0x2fd4,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = 0x2700,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "tsif_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch usb_fs1_h_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 17,
	.clkr = {
		.enable_reg = 0x2960,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "usb_fs1_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch usb_fs2_h_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 14,
	.clkr = {
		.enable_reg = 0x2980,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "usb_fs2_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch usb_hs1_h_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 1,
	.clkr = {
		.enable_reg = 0x2900,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs1_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch sdc1_h_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 11,
	.clkr = {
		.enable_reg = 0x2820,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "sdc1_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch sdc2_h_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = 0x2840,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "sdc2_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch sdc3_h_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 9,
	.clkr = {
		.enable_reg = 0x2860,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "sdc3_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch sdc4_h_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 8,
	.clkr = {
		.enable_reg = 0x2880,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "sdc4_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch sdc5_h_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = 0x28a0,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "sdc5_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch adm0_clk = {
	.halt_reg = 0x2fdc,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 14,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "adm0_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch adm0_pbus_clk = {
	.halt_reg = 0x2fdc,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 13,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "adm0_pbus_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch adm1_clk = {
	.halt_reg = 0x2fdc,
	.halt_bit = 12,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "adm1_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch adm1_pbus_clk = {
	.halt_reg = 0x2fdc,
	.halt_bit = 11,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "adm1_pbus_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch modem_ahb1_h_clk = {
	.halt_reg = 0x2fdc,
	.halt_bit = 8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "modem_ahb1_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch modem_ahb2_h_clk = {
	.halt_reg = 0x2fdc,
	.halt_bit = 7,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "modem_ahb2_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch pmic_arb0_h_clk = {
	.halt_reg = 0x2fd8,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 22,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "pmic_arb0_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch pmic_arb1_h_clk = {
	.halt_reg = 0x2fd8,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 21,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "pmic_arb1_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch pmic_ssbi2_clk = {
	.halt_reg = 0x2fd8,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 23,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "pmic_ssbi2_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch rpm_msg_ram_h_clk = {
	.hwcg_reg = 0x27e0,
	.hwcg_bit = 6,
	.halt_reg = 0x2fd8,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 12,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "rpm_msg_ram_h_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_regmap *gcc_msm8660_clks[] = {
	[PLL8] = &pll8.clkr,
	[PLL8_VOTE] = &pll8_vote,
	[GSBI1_UART_SRC] = &gsbi1_uart_src.clkr,
	[GSBI1_UART_CLK] = &gsbi1_uart_clk.clkr,
	[GSBI2_UART_SRC] = &gsbi2_uart_src.clkr,
	[GSBI2_UART_CLK] = &gsbi2_uart_clk.clkr,
	[GSBI3_UART_SRC] = &gsbi3_uart_src.clkr,
	[GSBI3_UART_CLK] = &gsbi3_uart_clk.clkr,
	[GSBI4_UART_SRC] = &gsbi4_uart_src.clkr,
	[GSBI4_UART_CLK] = &gsbi4_uart_clk.clkr,
	[GSBI5_UART_SRC] = &gsbi5_uart_src.clkr,
	[GSBI5_UART_CLK] = &gsbi5_uart_clk.clkr,
	[GSBI6_UART_SRC] = &gsbi6_uart_src.clkr,
	[GSBI6_UART_CLK] = &gsbi6_uart_clk.clkr,
	[GSBI7_UART_SRC] = &gsbi7_uart_src.clkr,
	[GSBI7_UART_CLK] = &gsbi7_uart_clk.clkr,
	[GSBI8_UART_SRC] = &gsbi8_uart_src.clkr,
	[GSBI8_UART_CLK] = &gsbi8_uart_clk.clkr,
	[GSBI9_UART_SRC] = &gsbi9_uart_src.clkr,
	[GSBI9_UART_CLK] = &gsbi9_uart_clk.clkr,
	[GSBI10_UART_SRC] = &gsbi10_uart_src.clkr,
	[GSBI10_UART_CLK] = &gsbi10_uart_clk.clkr,
	[GSBI11_UART_SRC] = &gsbi11_uart_src.clkr,
	[GSBI11_UART_CLK] = &gsbi11_uart_clk.clkr,
	[GSBI12_UART_SRC] = &gsbi12_uart_src.clkr,
	[GSBI12_UART_CLK] = &gsbi12_uart_clk.clkr,
	[GSBI1_QUP_SRC] = &gsbi1_qup_src.clkr,
	[GSBI1_QUP_CLK] = &gsbi1_qup_clk.clkr,
	[GSBI2_QUP_SRC] = &gsbi2_qup_src.clkr,
	[GSBI2_QUP_CLK] = &gsbi2_qup_clk.clkr,
	[GSBI3_QUP_SRC] = &gsbi3_qup_src.clkr,
	[GSBI3_QUP_CLK] = &gsbi3_qup_clk.clkr,
	[GSBI4_QUP_SRC] = &gsbi4_qup_src.clkr,
	[GSBI4_QUP_CLK] = &gsbi4_qup_clk.clkr,
	[GSBI5_QUP_SRC] = &gsbi5_qup_src.clkr,
	[GSBI5_QUP_CLK] = &gsbi5_qup_clk.clkr,
	[GSBI6_QUP_SRC] = &gsbi6_qup_src.clkr,
	[GSBI6_QUP_CLK] = &gsbi6_qup_clk.clkr,
	[GSBI7_QUP_SRC] = &gsbi7_qup_src.clkr,
	[GSBI7_QUP_CLK] = &gsbi7_qup_clk.clkr,
	[GSBI8_QUP_SRC] = &gsbi8_qup_src.clkr,
	[GSBI8_QUP_CLK] = &gsbi8_qup_clk.clkr,
	[GSBI9_QUP_SRC] = &gsbi9_qup_src.clkr,
	[GSBI9_QUP_CLK] = &gsbi9_qup_clk.clkr,
	[GSBI10_QUP_SRC] = &gsbi10_qup_src.clkr,
	[GSBI10_QUP_CLK] = &gsbi10_qup_clk.clkr,
	[GSBI11_QUP_SRC] = &gsbi11_qup_src.clkr,
	[GSBI11_QUP_CLK] = &gsbi11_qup_clk.clkr,
	[GSBI12_QUP_SRC] = &gsbi12_qup_src.clkr,
	[GSBI12_QUP_CLK] = &gsbi12_qup_clk.clkr,
	[GP0_SRC] = &gp0_src.clkr,
	[GP0_CLK] = &gp0_clk.clkr,
	[GP1_SRC] = &gp1_src.clkr,
	[GP1_CLK] = &gp1_clk.clkr,
	[GP2_SRC] = &gp2_src.clkr,
	[GP2_CLK] = &gp2_clk.clkr,
	[PMEM_CLK] = &pmem_clk.clkr,
	[PRNG_SRC] = &prng_src.clkr,
	[PRNG_CLK] = &prng_clk.clkr,
	[SDC1_SRC] = &sdc1_src.clkr,
	[SDC1_CLK] = &sdc1_clk.clkr,
	[SDC2_SRC] = &sdc2_src.clkr,
	[SDC2_CLK] = &sdc2_clk.clkr,
	[SDC3_SRC] = &sdc3_src.clkr,
	[SDC3_CLK] = &sdc3_clk.clkr,
	[SDC4_SRC] = &sdc4_src.clkr,
	[SDC4_CLK] = &sdc4_clk.clkr,
	[SDC5_SRC] = &sdc5_src.clkr,
	[SDC5_CLK] = &sdc5_clk.clkr,
	[TSIF_REF_SRC] = &tsif_ref_src.clkr,
	[TSIF_REF_CLK] = &tsif_ref_clk.clkr,
	[USB_HS1_XCVR_SRC] = &usb_hs1_xcvr_src.clkr,
	[USB_HS1_XCVR_CLK] = &usb_hs1_xcvr_clk.clkr,
	[USB_FS1_XCVR_FS_SRC] = &usb_fs1_xcvr_fs_src.clkr,
	[USB_FS1_XCVR_FS_CLK] = &usb_fs1_xcvr_fs_clk.clkr,
	[USB_FS1_SYSTEM_CLK] = &usb_fs1_system_clk.clkr,
	[USB_FS2_XCVR_FS_SRC] = &usb_fs2_xcvr_fs_src.clkr,
	[USB_FS2_XCVR_FS_CLK] = &usb_fs2_xcvr_fs_clk.clkr,
	[USB_FS2_SYSTEM_CLK] = &usb_fs2_system_clk.clkr,
	[GSBI1_H_CLK] = &gsbi1_h_clk.clkr,
	[GSBI2_H_CLK] = &gsbi2_h_clk.clkr,
	[GSBI3_H_CLK] = &gsbi3_h_clk.clkr,
	[GSBI4_H_CLK] = &gsbi4_h_clk.clkr,
	[GSBI5_H_CLK] = &gsbi5_h_clk.clkr,
	[GSBI6_H_CLK] = &gsbi6_h_clk.clkr,
	[GSBI7_H_CLK] = &gsbi7_h_clk.clkr,
	[GSBI8_H_CLK] = &gsbi8_h_clk.clkr,
	[GSBI9_H_CLK] = &gsbi9_h_clk.clkr,
	[GSBI10_H_CLK] = &gsbi10_h_clk.clkr,
	[GSBI11_H_CLK] = &gsbi11_h_clk.clkr,
	[GSBI12_H_CLK] = &gsbi12_h_clk.clkr,
	[TSIF_H_CLK] = &tsif_h_clk.clkr,
	[USB_FS1_H_CLK] = &usb_fs1_h_clk.clkr,
	[USB_FS2_H_CLK] = &usb_fs2_h_clk.clkr,
	[USB_HS1_H_CLK] = &usb_hs1_h_clk.clkr,
	[SDC1_H_CLK] = &sdc1_h_clk.clkr,
	[SDC2_H_CLK] = &sdc2_h_clk.clkr,
	[SDC3_H_CLK] = &sdc3_h_clk.clkr,
	[SDC4_H_CLK] = &sdc4_h_clk.clkr,
	[SDC5_H_CLK] = &sdc5_h_clk.clkr,
	[ADM0_CLK] = &adm0_clk.clkr,
	[ADM0_PBUS_CLK] = &adm0_pbus_clk.clkr,
	[ADM1_CLK] = &adm1_clk.clkr,
	[ADM1_PBUS_CLK] = &adm1_pbus_clk.clkr,
	[MODEM_AHB1_H_CLK] = &modem_ahb1_h_clk.clkr,
	[MODEM_AHB2_H_CLK] = &modem_ahb2_h_clk.clkr,
	[PMIC_ARB0_H_CLK] = &pmic_arb0_h_clk.clkr,
	[PMIC_ARB1_H_CLK] = &pmic_arb1_h_clk.clkr,
	[PMIC_SSBI2_CLK] = &pmic_ssbi2_clk.clkr,
	[RPM_MSG_RAM_H_CLK] = &rpm_msg_ram_h_clk.clkr,
};

static const struct qcom_reset_map gcc_msm8660_resets[] = {
	[AFAB_CORE_RESET] = { 0x2080, 7 },
	[SCSS_SYS_RESET] = { 0x20b4, 1 },
	[SCSS_SYS_POR_RESET] = { 0x20b4 },
	[AFAB_SMPSS_S_RESET] = { 0x20b8, 2 },
	[AFAB_SMPSS_M1_RESET] = { 0x20b8, 1 },
	[AFAB_SMPSS_M0_RESET] = { 0x20b8 },
	[AFAB_EBI1_S_RESET] = { 0x20c0, 7 },
	[SFAB_CORE_RESET] = { 0x2120, 7 },
	[SFAB_ADM0_M0_RESET] = { 0x21e0, 7 },
	[SFAB_ADM0_M1_RESET] = { 0x21e4, 7 },
	[SFAB_ADM0_M2_RESET] = { 0x21e4, 7 },
	[ADM0_C2_RESET] = { 0x220c, 4 },
	[ADM0_C1_RESET] = { 0x220c, 3 },
	[ADM0_C0_RESET] = { 0x220c, 2 },
	[ADM0_PBUS_RESET] = { 0x220c, 1 },
	[ADM0_RESET] = { 0x220c },
	[SFAB_ADM1_M0_RESET] = { 0x2220, 7 },
	[SFAB_ADM1_M1_RESET] = { 0x2224, 7 },
	[SFAB_ADM1_M2_RESET] = { 0x2228, 7 },
	[MMFAB_ADM1_M3_RESET] = { 0x2240, 7 },
	[ADM1_C3_RESET] = { 0x226c, 5 },
	[ADM1_C2_RESET] = { 0x226c, 4 },
	[ADM1_C1_RESET] = { 0x226c, 3 },
	[ADM1_C0_RESET] = { 0x226c, 2 },
	[ADM1_PBUS_RESET] = { 0x226c, 1 },
	[ADM1_RESET] = { 0x226c },
	[IMEM0_RESET] = { 0x2280, 7 },
	[SFAB_LPASS_Q6_RESET] = { 0x23a0, 7 },
	[SFAB_AFAB_M_RESET] = { 0x23e0, 7 },
	[AFAB_SFAB_M0_RESET] = { 0x2420, 7 },
	[AFAB_SFAB_M1_RESET] = { 0x2424, 7 },
	[DFAB_CORE_RESET] = { 0x24ac, 7 },
	[SFAB_DFAB_M_RESET] = { 0x2500, 7 },
	[DFAB_SFAB_M_RESET] = { 0x2520, 7 },
	[DFAB_SWAY0_RESET] = { 0x2540, 7 },
	[DFAB_SWAY1_RESET] = { 0x2544, 7 },
	[DFAB_ARB0_RESET] = { 0x2560, 7 },
	[DFAB_ARB1_RESET] = { 0x2564, 7 },
	[PPSS_PROC_RESET] = { 0x2594, 1 },
	[PPSS_RESET] = { 0x2594 },
	[PMEM_RESET] = { 0x25a0, 7 },
	[DMA_BAM_RESET] = { 0x25c0, 7 },
	[SIC_RESET] = { 0x25e0, 7 },
	[SPS_TIC_RESET] = { 0x2600, 7 },
	[CFBP0_RESET] = { 0x2650, 7 },
	[CFBP1_RESET] = { 0x2654, 7 },
	[CFBP2_RESET] = { 0x2658, 7 },
	[EBI2_RESET] = { 0x2664, 7 },
	[SFAB_CFPB_M_RESET] = { 0x2680, 7 },
	[CFPB_MASTER_RESET] = { 0x26a0, 7 },
	[SFAB_CFPB_S_RESET] = { 0x26c0, 7 },
	[CFPB_SPLITTER_RESET] = { 0x26e0, 7 },
	[TSIF_RESET] = { 0x2700, 7 },
	[CE1_RESET] = { 0x2720, 7 },
	[CE2_RESET] = { 0x2740, 7 },
	[SFAB_SFPB_M_RESET] = { 0x2780, 7 },
	[SFAB_SFPB_S_RESET] = { 0x27a0, 7 },
	[RPM_PROC_RESET] = { 0x27c0, 7 },
	[RPM_BUS_RESET] = { 0x27c4, 7 },
	[RPM_MSG_RAM_RESET] = { 0x27e0, 7 },
	[PMIC_ARB0_RESET] = { 0x2800, 7 },
	[PMIC_ARB1_RESET] = { 0x2804, 7 },
	[PMIC_SSBI2_RESET] = { 0x280c, 12 },
	[SDC1_RESET] = { 0x2830 },
	[SDC2_RESET] = { 0x2850 },
	[SDC3_RESET] = { 0x2870 },
	[SDC4_RESET] = { 0x2890 },
	[SDC5_RESET] = { 0x28b0 },
	[USB_HS1_RESET] = { 0x2910 },
	[USB_HS2_XCVR_RESET] = { 0x2934, 1 },
	[USB_HS2_RESET] = { 0x2934 },
	[USB_FS1_XCVR_RESET] = { 0x2974, 1 },
	[USB_FS1_RESET] = { 0x2974 },
	[USB_FS2_XCVR_RESET] = { 0x2994, 1 },
	[USB_FS2_RESET] = { 0x2994 },
	[GSBI1_RESET] = { 0x29dc },
	[GSBI2_RESET] = { 0x29fc },
	[GSBI3_RESET] = { 0x2a1c },
	[GSBI4_RESET] = { 0x2a3c },
	[GSBI5_RESET] = { 0x2a5c },
	[GSBI6_RESET] = { 0x2a7c },
	[GSBI7_RESET] = { 0x2a9c },
	[GSBI8_RESET] = { 0x2abc },
	[GSBI9_RESET] = { 0x2adc },
	[GSBI10_RESET] = { 0x2afc },
	[GSBI11_RESET] = { 0x2b1c },
	[GSBI12_RESET] = { 0x2b3c },
	[SPDM_RESET] = { 0x2b6c },
	[SEC_CTRL_RESET] = { 0x2b80, 7 },
	[TLMM_H_RESET] = { 0x2ba0, 7 },
	[TLMM_RESET] = { 0x2ba4, 7 },
	[MARRM_PWRON_RESET] = { 0x2bd4, 1 },
	[MARM_RESET] = { 0x2bd4 },
	[MAHB1_RESET] = { 0x2be4, 7 },
	[SFAB_MSS_S_RESET] = { 0x2c00, 7 },
	[MAHB2_RESET] = { 0x2c20, 7 },
	[MODEM_SW_AHB_RESET] = { 0x2c48, 1 },
	[MODEM_RESET] = { 0x2c48 },
	[SFAB_MSS_MDM1_RESET] = { 0x2c4c, 1 },
	[SFAB_MSS_MDM0_RESET] = { 0x2c4c },
	[MSS_SLP_RESET] = { 0x2c60, 7 },
	[MSS_MARM_SAW_RESET] = { 0x2c68, 1 },
	[MSS_WDOG_RESET] = { 0x2c68 },
	[TSSC_RESET] = { 0x2ca0, 7 },
	[PDM_RESET] = { 0x2cc0, 12 },
	[SCSS_CORE0_RESET] = { 0x2d60, 1 },
	[SCSS_CORE0_POR_RESET] = { 0x2d60 },
	[SCSS_CORE1_RESET] = { 0x2d80, 1 },
	[SCSS_CORE1_POR_RESET] = { 0x2d80 },
	[MPM_RESET] = { 0x2da4, 1 },
	[EBI1_1X_DIV_RESET] = { 0x2dec, 9 },
	[EBI1_RESET] = { 0x2dec, 7 },
	[SFAB_SMPSS_S_RESET] = { 0x2e00, 7 },
	[USB_PHY0_RESET] = { 0x2e20 },
	[USB_PHY1_RESET] = { 0x2e40 },
	[PRNG_RESET] = { 0x2e80, 12 },
};

static const struct regmap_config gcc_msm8660_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x363c,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_msm8660_desc = {
	.config = &gcc_msm8660_regmap_config,
	.clks = gcc_msm8660_clks,
	.num_clks = ARRAY_SIZE(gcc_msm8660_clks),
	.resets = gcc_msm8660_resets,
	.num_resets = ARRAY_SIZE(gcc_msm8660_resets),
};

static const struct of_device_id gcc_msm8660_match_table[] = {
	{ .compatible = "qcom,gcc-msm8660" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_msm8660_match_table);

static int gcc_msm8660_probe(struct platform_device *pdev)
{
	struct clk *clk;
	struct device *dev = &pdev->dev;

	/* Temporary until RPM clocks supported */
	clk = clk_register_fixed_rate(dev, "cxo", NULL, CLK_IS_ROOT, 19200000);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clk = clk_register_fixed_rate(dev, "pxo", NULL, CLK_IS_ROOT, 27000000);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	return qcom_cc_probe(pdev, &gcc_msm8660_desc);
}

static int gcc_msm8660_remove(struct platform_device *pdev)
{
	qcom_cc_remove(pdev);
	return 0;
}

static struct platform_driver gcc_msm8660_driver = {
	.probe		= gcc_msm8660_probe,
	.remove		= gcc_msm8660_remove,
	.driver		= {
		.name	= "gcc-msm8660",
		.of_match_table = gcc_msm8660_match_table,
	},
};

static int __init gcc_msm8660_init(void)
{
	return platform_driver_register(&gcc_msm8660_driver);
}
core_initcall(gcc_msm8660_init);

static void __exit gcc_msm8660_exit(void)
{
	platform_driver_unregister(&gcc_msm8660_driver);
}
module_exit(gcc_msm8660_exit);

MODULE_DESCRIPTION("GCC MSM 8660 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gcc-msm8660");
