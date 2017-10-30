/*
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) BayLibre, SAS.
 * Author : Neil Armstrong <narmstrong@baylibre.com>
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

#include <dt-bindings/clock/qcom,gcc-mdm9615.h>
#include <dt-bindings/reset/qcom,gcc-mdm9615.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"

static struct clk_fixed_factor cxo = {
	.mult = 1,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "cxo",
		.parent_names = (const char *[]){ "cxo_board" },
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_pll pll0 = {
	.l_reg = 0x30c4,
	.m_reg = 0x30c8,
	.n_reg = 0x30cc,
	.config_reg = 0x30d4,
	.mode_reg = 0x30c0,
	.status_reg = 0x30d8,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll0",
		.parent_names = (const char *[]){ "cxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap pll0_vote = {
	.enable_reg = 0x34c0,
	.enable_mask = BIT(0),
	.hw.init = &(struct clk_init_data){
		.name = "pll0_vote",
		.parent_names = (const char *[]){ "pll8" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_regmap pll4_vote = {
	.enable_reg = 0x34c0,
	.enable_mask = BIT(4),
	.hw.init = &(struct clk_init_data){
		.name = "pll4_vote",
		.parent_names = (const char *[]){ "pll4" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

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
		.parent_names = (const char *[]){ "cxo" },
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

static struct clk_pll pll14 = {
	.l_reg = 0x31c4,
	.m_reg = 0x31c8,
	.n_reg = 0x31cc,
	.config_reg = 0x31d4,
	.mode_reg = 0x31c0,
	.status_reg = 0x31d8,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll14",
		.parent_names = (const char *[]){ "cxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_regmap pll14_vote = {
	.enable_reg = 0x34c0,
	.enable_mask = BIT(11),
	.hw.init = &(struct clk_init_data){
		.name = "pll14_vote",
		.parent_names = (const char *[]){ "pll14" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

enum {
	P_CXO,
	P_PLL8,
	P_PLL14,
};

static const struct parent_map gcc_cxo_pll8_map[] = {
	{ P_CXO, 0 },
	{ P_PLL8, 3 }
};

static const char * const gcc_cxo_pll8[] = {
	"cxo",
	"pll8_vote",
};

static const struct parent_map gcc_cxo_pll14_map[] = {
	{ P_CXO, 0 },
	{ P_PLL14, 4 }
};

static const char * const gcc_cxo_pll14[] = {
	"cxo",
	"pll14_vote",
};

static const struct parent_map gcc_cxo_map[] = {
	{ P_CXO, 0 },
};

static const char * const gcc_cxo[] = {
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x29d4,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi1_uart_src",
			.parent_names = gcc_cxo_pll8,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x29f4,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi2_uart_src",
			.parent_names = gcc_cxo_pll8,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2a14,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi3_uart_src",
			.parent_names = gcc_cxo_pll8,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2a34,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi4_uart_src",
			.parent_names = gcc_cxo_pll8,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_uart,
	.clkr = {
		.enable_reg = 0x2a54,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi5_uart_src",
			.parent_names = gcc_cxo_pll8,
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

static struct freq_tbl clk_tbl_gsbi_qup[] = {
	{   960000, P_CXO,  4, 1,  5 },
	{  4800000, P_CXO,  4, 0,  1 },
	{  9600000, P_CXO,  2, 0,  1 },
	{ 15060000, P_PLL8, 1, 2, 51 },
	{ 24000000, P_PLL8, 4, 1,  4 },
	{ 25600000, P_PLL8, 1, 1, 15 },
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x29cc,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi1_qup_src",
			.parent_names = gcc_cxo_pll8,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x29ec,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi2_qup_src",
			.parent_names = gcc_cxo_pll8,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2a0c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi3_qup_src",
			.parent_names = gcc_cxo_pll8,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2a2c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi4_qup_src",
			.parent_names = gcc_cxo_pll8,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_gsbi_qup,
	.clkr = {
		.enable_reg = 0x2a4c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi5_qup_src",
			.parent_names = gcc_cxo_pll8,
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

static const struct freq_tbl clk_tbl_gp[] = {
	{ 9600000, P_CXO,  2, 0, 0 },
	{ 19200000, P_CXO,  1, 0, 0 },
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
		.parent_map = gcc_cxo_map,
	},
	.freq_tbl = clk_tbl_gp,
	.clkr = {
		.enable_reg = 0x2d24,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gp0_src",
			.parent_names = gcc_cxo,
			.num_parents = 1,
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
		.parent_map = gcc_cxo_map,
	},
	.freq_tbl = clk_tbl_gp,
	.clkr = {
		.enable_reg = 0x2d44,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gp1_src",
			.parent_names = gcc_cxo,
			.num_parents = 1,
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
		.parent_map = gcc_cxo_map,
	},
	.freq_tbl = clk_tbl_gp,
	.clkr = {
		.enable_reg = 0x2d64,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gp2_src",
			.parent_names = gcc_cxo,
			.num_parents = 1,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "prng_src",
			.parent_names = gcc_cxo_pll8,
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
	{    144000, P_CXO,   1, 1, 133 },
	{    400000, P_PLL8,  4, 1, 240 },
	{  16000000, P_PLL8,  4, 1,   6 },
	{  17070000, P_PLL8,  1, 2,  45 },
	{  20210000, P_PLL8,  1, 1,  19 },
	{  24000000, P_PLL8,  4, 1,   4 },
	{  38400000, P_PLL8,  2, 1,   5 },
	{  48000000, P_PLL8,  4, 1,   2 },
	{  64000000, P_PLL8,  3, 1,   2 },
	{  76800000, P_PLL8,  1, 1,   5 },
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = 0x282c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdc1_src",
			.parent_names = gcc_cxo_pll8,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = 0x284c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdc2_src",
			.parent_names = gcc_cxo_pll8,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_usb,
	.clkr = {
		.enable_reg = 0x290c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs1_xcvr_src",
			.parent_names = gcc_cxo_pll8,
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

static struct clk_rcg usb_hsic_xcvr_fs_src = {
	.ns_reg = 0x2928,
	.md_reg = 0x2924,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_usb,
	.clkr = {
		.enable_reg = 0x2928,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hsic_xcvr_fs_src",
			.parent_names = gcc_cxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch usb_hsic_xcvr_fs_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 9,
	.clkr = {
		.enable_reg = 0x2928,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hsic_xcvr_fs_clk",
			.parent_names =
				(const char *[]){ "usb_hsic_xcvr_fs_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_usb_hs1_system[] = {
	{ 60000000, P_PLL8, 1, 5, 32 },
	{ }
};

static struct clk_rcg usb_hs1_system_src = {
	.ns_reg = 0x36a4,
	.md_reg = 0x36a0,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_usb_hs1_system,
	.clkr = {
		.enable_reg = 0x36a4,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs1_system_src",
			.parent_names = gcc_cxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch usb_hs1_system_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 4,
	.clkr = {
		.enable_reg = 0x36a4,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.parent_names =
				(const char *[]){ "usb_hs1_system_src" },
			.num_parents = 1,
			.name = "usb_hs1_system_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		},
	},
};

static const struct freq_tbl clk_tbl_usb_hsic_system[] = {
	{ 64000000, P_PLL8, 1, 1, 6 },
	{ }
};

static struct clk_rcg usb_hsic_system_src = {
	.ns_reg = 0x2b58,
	.md_reg = 0x2b54,
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
		.parent_map = gcc_cxo_pll8_map,
	},
	.freq_tbl = clk_tbl_usb_hsic_system,
	.clkr = {
		.enable_reg = 0x2b58,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hsic_system_src",
			.parent_names = gcc_cxo_pll8,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch usb_hsic_system_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = 0x2b58,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.parent_names =
				(const char *[]){ "usb_hsic_system_src" },
			.num_parents = 1,
			.name = "usb_hsic_system_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_usb_hsic_hsic[] = {
	{ 48000000, P_PLL14, 1, 0, 0 },
	{ }
};

static struct clk_rcg usb_hsic_hsic_src = {
	.ns_reg = 0x2b50,
	.md_reg = 0x2b4c,
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
		.parent_map = gcc_cxo_pll14_map,
	},
	.freq_tbl = clk_tbl_usb_hsic_hsic,
	.clkr = {
		.enable_reg = 0x2b50,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hsic_hsic_src",
			.parent_names = gcc_cxo_pll14,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	}
};

static struct clk_branch usb_hsic_hsic_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x2b50,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.parent_names = (const char *[]){ "usb_hsic_hsic_src" },
			.num_parents = 1,
			.name = "usb_hsic_hsic_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch usb_hsic_hsio_cal_clk = {
	.halt_reg = 0x2fc8,
	.halt_bit = 8,
	.clkr = {
		.enable_reg = 0x2b48,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.parent_names = (const char *[]){ "cxo" },
			.num_parents = 1,
			.name = "usb_hsic_hsio_cal_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch ce1_core_clk = {
	.hwcg_reg = 0x2724,
	.hwcg_bit = 6,
	.halt_reg = 0x2fd4,
	.halt_bit = 27,
	.clkr = {
		.enable_reg = 0x2724,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "ce1_core_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch ce1_h_clk = {
	.halt_reg = 0x2fd4,
	.halt_bit = 1,
	.clkr = {
		.enable_reg = 0x2720,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "ce1_h_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch dma_bam_h_clk = {
	.hwcg_reg = 0x25c0,
	.hwcg_bit = 6,
	.halt_reg = 0x2fc8,
	.halt_bit = 12,
	.clkr = {
		.enable_reg = 0x25c0,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "dma_bam_h_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch gsbi1_h_clk = {
	.hwcg_reg = 0x29c0,
	.hwcg_bit = 6,
	.halt_reg = 0x2fcc,
	.halt_bit = 11,
	.clkr = {
		.enable_reg = 0x29c0,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi1_h_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch gsbi2_h_clk = {
	.hwcg_reg = 0x29e0,
	.hwcg_bit = 6,
	.halt_reg = 0x2fcc,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = 0x29e0,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi2_h_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch gsbi3_h_clk = {
	.hwcg_reg = 0x2a00,
	.hwcg_bit = 6,
	.halt_reg = 0x2fcc,
	.halt_bit = 3,
	.clkr = {
		.enable_reg = 0x2a00,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi3_h_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch gsbi4_h_clk = {
	.hwcg_reg = 0x2a20,
	.hwcg_bit = 6,
	.halt_reg = 0x2fd0,
	.halt_bit = 27,
	.clkr = {
		.enable_reg = 0x2a20,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi4_h_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch gsbi5_h_clk = {
	.hwcg_reg = 0x2a40,
	.hwcg_bit = 6,
	.halt_reg = 0x2fd0,
	.halt_bit = 23,
	.clkr = {
		.enable_reg = 0x2a40,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gsbi5_h_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch usb_hs1_h_clk = {
	.hwcg_reg = 0x2900,
	.hwcg_bit = 6,
	.halt_reg = 0x2fc8,
	.halt_bit = 1,
	.clkr = {
		.enable_reg = 0x2900,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs1_h_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch usb_hsic_h_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 28,
	.clkr = {
		.enable_reg = 0x2920,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hsic_h_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch sdc1_h_clk = {
	.hwcg_reg = 0x2820,
	.hwcg_bit = 6,
	.halt_reg = 0x2fc8,
	.halt_bit = 11,
	.clkr = {
		.enable_reg = 0x2820,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "sdc1_h_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch sdc2_h_clk = {
	.hwcg_reg = 0x2840,
	.hwcg_bit = 6,
	.halt_reg = 0x2fc8,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = 0x2840,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "sdc2_h_clk",
			.ops = &clk_branch_ops,
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
		},
	},
};

static struct clk_branch adm0_pbus_clk = {
	.hwcg_reg = 0x2208,
	.hwcg_bit = 6,
	.halt_reg = 0x2fdc,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 13,
	.clkr = {
		.enable_reg = 0x3080,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "adm0_pbus_clk",
			.ops = &clk_branch_ops,
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
		},
	},
};

static struct clk_branch ebi2_clk = {
	.hwcg_reg = 0x2664,
	.hwcg_bit = 6,
	.halt_reg = 0x2fcc,
	.halt_bit = 24,
	.clkr = {
		.enable_reg = 0x2664,
		.enable_mask = BIT(6) | BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "ebi2_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch ebi2_aon_clk = {
	.halt_reg = 0x2fcc,
	.halt_bit = 23,
	.clkr = {
		.enable_reg = 0x2664,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "ebi2_aon_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_hw *gcc_mdm9615_hws[] = {
	&cxo.hw,
};

static struct clk_regmap *gcc_mdm9615_clks[] = {
	[PLL0] = &pll0.clkr,
	[PLL0_VOTE] = &pll0_vote,
	[PLL4_VOTE] = &pll4_vote,
	[PLL8] = &pll8.clkr,
	[PLL8_VOTE] = &pll8_vote,
	[PLL14] = &pll14.clkr,
	[PLL14_VOTE] = &pll14_vote,
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
	[GP0_SRC] = &gp0_src.clkr,
	[GP0_CLK] = &gp0_clk.clkr,
	[GP1_SRC] = &gp1_src.clkr,
	[GP1_CLK] = &gp1_clk.clkr,
	[GP2_SRC] = &gp2_src.clkr,
	[GP2_CLK] = &gp2_clk.clkr,
	[PMEM_A_CLK] = &pmem_clk.clkr,
	[PRNG_SRC] = &prng_src.clkr,
	[PRNG_CLK] = &prng_clk.clkr,
	[SDC1_SRC] = &sdc1_src.clkr,
	[SDC1_CLK] = &sdc1_clk.clkr,
	[SDC2_SRC] = &sdc2_src.clkr,
	[SDC2_CLK] = &sdc2_clk.clkr,
	[USB_HS1_XCVR_SRC] = &usb_hs1_xcvr_src.clkr,
	[USB_HS1_XCVR_CLK] = &usb_hs1_xcvr_clk.clkr,
	[USB_HS1_SYSTEM_CLK_SRC] = &usb_hs1_system_src.clkr,
	[USB_HS1_SYSTEM_CLK] = &usb_hs1_system_clk.clkr,
	[USB_HSIC_XCVR_FS_SRC] = &usb_hsic_xcvr_fs_src.clkr,
	[USB_HSIC_XCVR_FS_CLK] = &usb_hsic_xcvr_fs_clk.clkr,
	[USB_HSIC_SYSTEM_CLK_SRC] = &usb_hsic_system_src.clkr,
	[USB_HSIC_SYSTEM_CLK] = &usb_hsic_system_clk.clkr,
	[USB_HSIC_HSIC_CLK_SRC] = &usb_hsic_hsic_src.clkr,
	[USB_HSIC_HSIC_CLK] = &usb_hsic_hsic_clk.clkr,
	[USB_HSIC_HSIO_CAL_CLK] = &usb_hsic_hsio_cal_clk.clkr,
	[CE1_CORE_CLK] = &ce1_core_clk.clkr,
	[CE1_H_CLK] = &ce1_h_clk.clkr,
	[DMA_BAM_H_CLK] = &dma_bam_h_clk.clkr,
	[GSBI1_H_CLK] = &gsbi1_h_clk.clkr,
	[GSBI2_H_CLK] = &gsbi2_h_clk.clkr,
	[GSBI3_H_CLK] = &gsbi3_h_clk.clkr,
	[GSBI4_H_CLK] = &gsbi4_h_clk.clkr,
	[GSBI5_H_CLK] = &gsbi5_h_clk.clkr,
	[USB_HS1_H_CLK] = &usb_hs1_h_clk.clkr,
	[USB_HSIC_H_CLK] = &usb_hsic_h_clk.clkr,
	[SDC1_H_CLK] = &sdc1_h_clk.clkr,
	[SDC2_H_CLK] = &sdc2_h_clk.clkr,
	[ADM0_CLK] = &adm0_clk.clkr,
	[ADM0_PBUS_CLK] = &adm0_pbus_clk.clkr,
	[PMIC_ARB0_H_CLK] = &pmic_arb0_h_clk.clkr,
	[PMIC_ARB1_H_CLK] = &pmic_arb1_h_clk.clkr,
	[PMIC_SSBI2_CLK] = &pmic_ssbi2_clk.clkr,
	[RPM_MSG_RAM_H_CLK] = &rpm_msg_ram_h_clk.clkr,
	[EBI2_CLK] = &ebi2_clk.clkr,
	[EBI2_AON_CLK] = &ebi2_aon_clk.clkr,
};

static const struct qcom_reset_map gcc_mdm9615_resets[] = {
	[DMA_BAM_RESET] = { 0x25c0, 7 },
	[CE1_H_RESET] = { 0x2720, 7 },
	[CE1_CORE_RESET] = { 0x2724, 7 },
	[SDC1_RESET] = { 0x2830 },
	[SDC2_RESET] = { 0x2850 },
	[ADM0_C2_RESET] = { 0x220c, 4 },
	[ADM0_C1_RESET] = { 0x220c, 3 },
	[ADM0_C0_RESET] = { 0x220c, 2 },
	[ADM0_PBUS_RESET] = { 0x220c, 1 },
	[ADM0_RESET] = { 0x220c },
	[USB_HS1_RESET] = { 0x2910 },
	[USB_HSIC_RESET] = { 0x2934 },
	[GSBI1_RESET] = { 0x29dc },
	[GSBI2_RESET] = { 0x29fc },
	[GSBI3_RESET] = { 0x2a1c },
	[GSBI4_RESET] = { 0x2a3c },
	[GSBI5_RESET] = { 0x2a5c },
	[PDM_RESET] = { 0x2CC0, 12 },
};

static const struct regmap_config gcc_mdm9615_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x3660,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_mdm9615_desc = {
	.config = &gcc_mdm9615_regmap_config,
	.clks = gcc_mdm9615_clks,
	.num_clks = ARRAY_SIZE(gcc_mdm9615_clks),
	.resets = gcc_mdm9615_resets,
	.num_resets = ARRAY_SIZE(gcc_mdm9615_resets),
};

static const struct of_device_id gcc_mdm9615_match_table[] = {
	{ .compatible = "qcom,gcc-mdm9615" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_mdm9615_match_table);

static int gcc_mdm9615_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	int ret;
	int i;

	regmap = qcom_cc_map(pdev, &gcc_mdm9615_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	for (i = 0; i < ARRAY_SIZE(gcc_mdm9615_hws); i++) {
		ret = devm_clk_hw_register(dev, gcc_mdm9615_hws[i]);
		if (ret)
			return ret;
	}

	return qcom_cc_really_probe(pdev, &gcc_mdm9615_desc, regmap);
}

static struct platform_driver gcc_mdm9615_driver = {
	.probe		= gcc_mdm9615_probe,
	.driver		= {
		.name	= "gcc-mdm9615",
		.of_match_table = gcc_mdm9615_match_table,
	},
};

static int __init gcc_mdm9615_init(void)
{
	return platform_driver_register(&gcc_mdm9615_driver);
}
core_initcall(gcc_mdm9615_init);

static void __exit gcc_mdm9615_exit(void)
{
	platform_driver_unregister(&gcc_mdm9615_driver);
}
module_exit(gcc_mdm9615_exit);

MODULE_DESCRIPTION("QCOM GCC MDM9615 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gcc-mdm9615");
