// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,shikra-audiocorecc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_SLEEP_CLK,
	DT_AUD_REF_CLK_SRC,
};

enum {
	P_AUD_REF_CLK_SRC,
	P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX,
	P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX2,
	P_BI_TCXO,
	P_SLEEP_CLK,
};

static const struct pll_vco spark_vco[] = {
	{ 500000000, 1000000000, 2 },
};

/* 614.4 MHz Configuration */
static const struct alpha_pll_config audio_core_cc_dig_pll_config = {
	.l = 0x20,
	.alpha = 0x0,
	.vco_val = BIT(21),
	.post_div_val = 0x28100,
	.post_div_mask = GENMASK(17, 8),
	.vco_mask = GENMASK(21, 20),
	.main_output_mask = BIT(0),
	.aux_output_mask = BIT(1),
	.aux2_output_mask = BIT(2),
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
};

static struct clk_alpha_pll audio_core_cc_dig_pll = {
	.offset = 0x0,
	.config = &audio_core_cc_dig_pll_config,
	.vco_table = spark_vco,
	.num_vco = ARRAY_SIZE(spark_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_dig_pll",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_ops,
		},
	},
};

static struct clk_fixed_factor audio_core_cc_dig_pll_out_aux = {
	.mult = 1,
	.div = 5,
	.hw.init = &(struct clk_init_data) {
		.name = "audio_core_cc_dig_pll_out_aux",
		.parent_data = &(const struct clk_parent_data) {
			.hw = &audio_core_cc_dig_pll.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_fixed_factor audio_core_cc_dig_pll_out_aux2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data) {
		.name = "audio_core_cc_dig_pll_out_aux2",
		.parent_data = &(const struct clk_parent_data) {
			.hw = &audio_core_cc_dig_pll.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static const struct parent_map audio_core_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_AUD_REF_CLK_SRC, 1 },
	{ P_SLEEP_CLK, 2 },
	{ P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 4 },
	{ P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX2, 6 },
};

static const struct clk_parent_data audio_core_cc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .index = DT_AUD_REF_CLK_SRC },
	{ .index = DT_SLEEP_CLK },
	{ .hw = &audio_core_cc_dig_pll_out_aux.hw },
	{ .hw = &audio_core_cc_dig_pll_out_aux2.hw },
};

static const struct freq_tbl ftbl_audio_core_cc_aif_if0_clk_src[] = {
	F(240000, P_BI_TCXO, 10, 1, 8),
	F(256000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 1, 32),
	F(512000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 1, 16),
	F(768000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 10, 1, 16),
	F(1024000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 1, 8),
	F(1536000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 10, 1, 8),
	F(2048000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 1, 4),
	F(3072000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 10, 1, 4),
	F(4096000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 1, 2),
	F(6144000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 10, 1, 2),
	F(8192000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 0, 0),
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(12288000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 10, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(24576000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 5, 0, 0),
	{ }
};

static struct clk_rcg2 audio_core_cc_aif_if0_clk_src = {
	.cmd_rcgr = 0x104c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = audio_core_cc_parent_map_0,
	.freq_tbl = ftbl_audio_core_cc_aif_if0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "audio_core_cc_aif_if0_clk_src",
		.parent_data = audio_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(audio_core_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 audio_core_cc_aif_if1_clk_src = {
	.cmd_rcgr = 0x10b0,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = audio_core_cc_parent_map_0,
	.freq_tbl = ftbl_audio_core_cc_aif_if0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "audio_core_cc_aif_if1_clk_src",
		.parent_data = audio_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(audio_core_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 audio_core_cc_aif_if2_clk_src = {
	.cmd_rcgr = 0x1114,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = audio_core_cc_parent_map_0,
	.freq_tbl = ftbl_audio_core_cc_aif_if0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "audio_core_cc_aif_if2_clk_src",
		.parent_data = audio_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(audio_core_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_audio_core_cc_aif_if3_clk_src[] = {
	F(240000, P_BI_TCXO, 10, 1, 8),
	F(256000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 1, 32),
	F(512000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 1, 16),
	F(768000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 10, 1, 16),
	F(1024000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 1, 8),
	F(1536000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 10, 1, 8),
	F(2048000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 1, 4),
	F(3072000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 10, 1, 4),
	F(4096000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 1, 2),
	F(6144000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 10, 1, 2),
	F(8192000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 15, 0, 0),
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(12288000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 10, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(24576000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 5, 0, 0),
	F(49152000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 audio_core_cc_aif_if3_clk_src = {
	.cmd_rcgr = 0x1178,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = audio_core_cc_parent_map_0,
	.freq_tbl = ftbl_audio_core_cc_aif_if3_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "audio_core_cc_aif_if3_clk_src",
		.parent_data = audio_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(audio_core_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_audio_core_cc_aud_dma_clk_src[] = {
	F(38400000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX2, 8, 0, 0),
	F(102400000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX2, 3, 0, 0),
	F(153600000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX2, 2, 0, 0),
	F(307200000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX2, 1, 0, 0),
	{ }
};

static struct clk_rcg2 audio_core_cc_aud_dma_clk_src = {
	.cmd_rcgr = 0x1028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = audio_core_cc_parent_map_0,
	.freq_tbl = ftbl_audio_core_cc_aud_dma_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "audio_core_cc_aud_dma_clk_src",
		.parent_data = audio_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(audio_core_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_audio_core_cc_bus_clk_src[] = {
	F(38400000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX2, 8, 0, 0),
	F(76800000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX2, 4, 0, 0),
	{ }
};

static struct clk_rcg2 audio_core_cc_bus_clk_src = {
	.cmd_rcgr = 0x1008,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = audio_core_cc_parent_map_0,
	.freq_tbl = ftbl_audio_core_cc_bus_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "audio_core_cc_bus_clk_src",
		.parent_data = audio_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(audio_core_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 audio_core_cc_ext_mclka_clk_src = {
	.cmd_rcgr = 0x123c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = audio_core_cc_parent_map_0,
	.freq_tbl = ftbl_audio_core_cc_aif_if0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "audio_core_cc_ext_mclka_clk_src",
		.parent_data = audio_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(audio_core_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 audio_core_cc_ext_mclkb_clk_src = {
	.cmd_rcgr = 0x125c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = audio_core_cc_parent_map_0,
	.freq_tbl = ftbl_audio_core_cc_aif_if0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "audio_core_cc_ext_mclkb_clk_src",
		.parent_data = audio_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(audio_core_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_audio_core_cc_lpaif_pcmoe_clk_src[] = {
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(15360000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 8, 0, 0),
	F(30720000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 4, 0, 0),
	F(61440000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 2, 0, 0),
	{ }
};

static struct clk_rcg2 audio_core_cc_lpaif_pcmoe_clk_src = {
	.cmd_rcgr = 0x12ac,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = audio_core_cc_parent_map_0,
	.freq_tbl = ftbl_audio_core_cc_lpaif_pcmoe_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "audio_core_cc_lpaif_pcmoe_clk_src",
		.parent_data = audio_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(audio_core_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_audio_core_cc_tx_mclk_rcg_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(24576000, P_AUDIO_CORE_CC_DIG_PLL_OUT_AUX, 5, 0, 0),
	{ }
};

static struct clk_rcg2 audio_core_cc_tx_mclk_rcg_clk_src = {
	.cmd_rcgr = 0x127c,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = audio_core_cc_parent_map_0,
	.freq_tbl = ftbl_audio_core_cc_tx_mclk_rcg_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "audio_core_cc_tx_mclk_rcg_clk_src",
		.parent_data = audio_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(audio_core_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div audio_core_cc_cdiv_tx_mclk_div_clk_src = {
	.reg = 0x129c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "audio_core_cc_cdiv_tx_mclk_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&audio_core_cc_tx_mclk_rcg_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch audio_core_cc_aif_if0_ebit_clk = {
	.halt_reg = 0x1068,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1068,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_aif_if0_ebit_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_aif_if0_ibit_clk = {
	.halt_reg = 0x1064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_aif_if0_ibit_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_aif_if0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_aif_if1_ebit_clk = {
	.halt_reg = 0x10cc,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x10cc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_aif_if1_ebit_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_aif_if1_ibit_clk = {
	.halt_reg = 0x10c8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10c8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_aif_if1_ibit_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_aif_if1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_aif_if2_ebit_clk = {
	.halt_reg = 0x1130,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1130,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_aif_if2_ebit_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_aif_if2_ibit_clk = {
	.halt_reg = 0x112c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x112c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_aif_if2_ibit_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_aif_if2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_aif_if3_ebit_clk = {
	.halt_reg = 0x1194,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1194,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_aif_if3_ebit_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_aif_if3_ibit_clk = {
	.halt_reg = 0x1190,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1190,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_aif_if3_ibit_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_aif_if3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_aud_dma_clk = {
	.halt_reg = 0x1040,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1040,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_aud_dma_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_aud_dma_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_aud_dma_mem_clk = {
	.halt_reg = 0x1044,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1044,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1044,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_aud_dma_mem_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_aud_dma_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_bus_clk = {
	.halt_reg = 0x1020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x1020,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_bus_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_bus_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch audio_core_cc_ext_mclka_out_clk = {
	.halt_reg = 0x1254,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1254,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_ext_mclka_out_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_ext_mclka_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_ext_mclkb_out_clk = {
	.halt_reg = 0x1274,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1274,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_ext_mclkb_out_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_ext_mclkb_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_im_sleep_clk = {
	.halt_reg = 0x12cc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12cc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_im_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_lpaif_pcmoe_clk = {
	.halt_reg = 0x12c4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12c4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_lpaif_pcmoe_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_lpaif_pcmoe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_rx_mclk_2x_clk = {
	.halt_reg = 0x1298,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1298,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_rx_mclk_2x_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_tx_mclk_rcg_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_rx_mclk_clk = {
	.halt_reg = 0x12a4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_rx_mclk_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_cdiv_tx_mclk_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_sampling_clk = {
	.halt_reg = 0x1000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_sampling_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_tx_mclk_2x_clk = {
	.halt_reg = 0x1294,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1294,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_tx_mclk_2x_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_tx_mclk_rcg_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch audio_core_cc_tx_mclk_clk = {
	.halt_reg = 0x12a0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x12a0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "audio_core_cc_tx_mclk_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&audio_core_cc_cdiv_tx_mclk_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_hw *audio_core_cc_shikra_hws[] = {
	[AUDIO_CORE_CC_DIG_PLL_OUT_AUX] = &audio_core_cc_dig_pll_out_aux.hw,
	[AUDIO_CORE_CC_DIG_PLL_OUT_AUX2] = &audio_core_cc_dig_pll_out_aux2.hw,
};

static struct clk_regmap *audio_core_cc_shikra_clocks[] = {
	[AUDIO_CORE_CC_AIF_IF0_CLK_SRC] = &audio_core_cc_aif_if0_clk_src.clkr,
	[AUDIO_CORE_CC_AIF_IF0_EBIT_CLK] = &audio_core_cc_aif_if0_ebit_clk.clkr,
	[AUDIO_CORE_CC_AIF_IF0_IBIT_CLK] = &audio_core_cc_aif_if0_ibit_clk.clkr,
	[AUDIO_CORE_CC_AIF_IF1_CLK_SRC] = &audio_core_cc_aif_if1_clk_src.clkr,
	[AUDIO_CORE_CC_AIF_IF1_EBIT_CLK] = &audio_core_cc_aif_if1_ebit_clk.clkr,
	[AUDIO_CORE_CC_AIF_IF1_IBIT_CLK] = &audio_core_cc_aif_if1_ibit_clk.clkr,
	[AUDIO_CORE_CC_AIF_IF2_CLK_SRC] = &audio_core_cc_aif_if2_clk_src.clkr,
	[AUDIO_CORE_CC_AIF_IF2_EBIT_CLK] = &audio_core_cc_aif_if2_ebit_clk.clkr,
	[AUDIO_CORE_CC_AIF_IF2_IBIT_CLK] = &audio_core_cc_aif_if2_ibit_clk.clkr,
	[AUDIO_CORE_CC_AIF_IF3_CLK_SRC] = &audio_core_cc_aif_if3_clk_src.clkr,
	[AUDIO_CORE_CC_AIF_IF3_EBIT_CLK] = &audio_core_cc_aif_if3_ebit_clk.clkr,
	[AUDIO_CORE_CC_AIF_IF3_IBIT_CLK] = &audio_core_cc_aif_if3_ibit_clk.clkr,
	[AUDIO_CORE_CC_AUD_DMA_CLK] = &audio_core_cc_aud_dma_clk.clkr,
	[AUDIO_CORE_CC_AUD_DMA_CLK_SRC] = &audio_core_cc_aud_dma_clk_src.clkr,
	[AUDIO_CORE_CC_AUD_DMA_MEM_CLK] = &audio_core_cc_aud_dma_mem_clk.clkr,
	[AUDIO_CORE_CC_BUS_CLK] = &audio_core_cc_bus_clk.clkr,
	[AUDIO_CORE_CC_BUS_CLK_SRC] = &audio_core_cc_bus_clk_src.clkr,
	[AUDIO_CORE_CC_CDIV_TX_MCLK_DIV_CLK_SRC] = &audio_core_cc_cdiv_tx_mclk_div_clk_src.clkr,
	[AUDIO_CORE_CC_DIG_PLL] = &audio_core_cc_dig_pll.clkr,
	[AUDIO_CORE_CC_EXT_MCLKA_CLK_SRC] = &audio_core_cc_ext_mclka_clk_src.clkr,
	[AUDIO_CORE_CC_EXT_MCLKA_OUT_CLK] = &audio_core_cc_ext_mclka_out_clk.clkr,
	[AUDIO_CORE_CC_EXT_MCLKB_CLK_SRC] = &audio_core_cc_ext_mclkb_clk_src.clkr,
	[AUDIO_CORE_CC_EXT_MCLKB_OUT_CLK] = &audio_core_cc_ext_mclkb_out_clk.clkr,
	[AUDIO_CORE_CC_IM_SLEEP_CLK] = &audio_core_cc_im_sleep_clk.clkr,
	[AUDIO_CORE_CC_LPAIF_PCMOE_CLK] = &audio_core_cc_lpaif_pcmoe_clk.clkr,
	[AUDIO_CORE_CC_LPAIF_PCMOE_CLK_SRC] = &audio_core_cc_lpaif_pcmoe_clk_src.clkr,
	[AUDIO_CORE_CC_RX_MCLK_2X_CLK] = &audio_core_cc_rx_mclk_2x_clk.clkr,
	[AUDIO_CORE_CC_RX_MCLK_CLK] = &audio_core_cc_rx_mclk_clk.clkr,
	[AUDIO_CORE_CC_SAMPLING_CLK] = &audio_core_cc_sampling_clk.clkr,
	[AUDIO_CORE_CC_TX_MCLK_2X_CLK] = &audio_core_cc_tx_mclk_2x_clk.clkr,
	[AUDIO_CORE_CC_TX_MCLK_CLK] = &audio_core_cc_tx_mclk_clk.clkr,
	[AUDIO_CORE_CC_TX_MCLK_RCG_CLK_SRC] = &audio_core_cc_tx_mclk_rcg_clk_src.clkr,
};

static struct clk_alpha_pll *audio_core_cc_shikra_plls[] = {
	&audio_core_cc_dig_pll,
};

static const struct qcom_cc_driver_data audio_core_cc_shikra_driver_data = {
	.alpha_plls = audio_core_cc_shikra_plls,
	.num_alpha_plls = ARRAY_SIZE(audio_core_cc_shikra_plls),
};

static const struct regmap_config audio_core_cc_shikra_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x305c,
	.fast_io = true,
};

static const struct qcom_reset_map audio_core_csr_shikra_resets[] = {
	[AUDIO_CORE_CSR_RX_SWR_CGCR] = { 0x1c, 1 },
	[AUDIO_CORE_CSR_TX_SWR_CGCR] = { 0x30, 1 },
};

static const struct regmap_config audio_core_csr_shikra_regmap_config = {
	.name = "audio_core_cc_shikra_reset",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = 0x34,
};

static const struct qcom_cc_desc audio_core_csr_shikra_desc = {
	.config = &audio_core_csr_shikra_regmap_config,
	.resets = audio_core_csr_shikra_resets,
	.num_resets = ARRAY_SIZE(audio_core_csr_shikra_resets),
};

static const struct qcom_cc_desc audio_core_cc_shikra_desc = {
	.config = &audio_core_cc_shikra_regmap_config,
	.clk_hws = audio_core_cc_shikra_hws,
	.num_clk_hws = ARRAY_SIZE(audio_core_cc_shikra_hws),
	.clks = audio_core_cc_shikra_clocks,
	.num_clks = ARRAY_SIZE(audio_core_cc_shikra_clocks),
	.driver_data = &audio_core_cc_shikra_driver_data,
};

static const struct of_device_id audio_core_cc_shikra_match_table[] = {
	{ .compatible = "qcom,shikra-audiocorecc", .data = &audio_core_cc_shikra_desc },
	{ .compatible = "qcom,shikra-audiocore-csr", .data = &audio_core_csr_shikra_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, audio_core_cc_shikra_match_table);

static int audio_core_cc_shikra_probe(struct platform_device *pdev)
{
	const struct qcom_cc_desc *desc;

	desc = device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	return qcom_cc_probe(pdev, desc);
}

static struct platform_driver audio_core_cc_shikra_driver = {
	.probe = audio_core_cc_shikra_probe,
	.driver = {
		.name = "audiocorecc-shikra",
		.of_match_table = audio_core_cc_shikra_match_table,
	},
};

module_platform_driver(audio_core_cc_shikra_driver);

MODULE_DESCRIPTION("QTI AUDIOCORECC Shikra Driver");
MODULE_LICENSE("GPL");
