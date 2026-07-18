// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sm8750-gpucc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_GPLL0_OUT_MAIN,
	DT_GPLL0_OUT_MAIN_DIV,
};

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_MAIN_DIV,
	P_GPU_CC_PLL0_OUT_EVEN,
	P_GPU_CC_PLL0_OUT_MAIN,
	P_GPU_CC_PLL0_OUT_ODD,
};

static const struct pll_vco taycan_elu_vco[] = {
	{ 249600000, 2500000000, 0 },
};

static const struct alpha_pll_config gpu_cc_pll0_config = {
	.l = 0x34,
	.alpha = 0x1555,
	.config_ctl_val = 0x19660387,
	.config_ctl_hi_val = 0x098060a0,
	.config_ctl_hi1_val = 0xb416cb20,
	.user_ctl_val = 0x00000400,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll gpu_cc_pll0 = {
	.offset = 0x0,
	.config = &gpu_cc_pll0_config,
	.vco_table = taycan_elu_vco,
	.num_vco = ARRAY_SIZE(taycan_elu_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_ELU],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_elu_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpu_cc_pll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpu_cc_pll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 10,
	.post_div_table = post_div_table_gpu_cc_pll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_gpu_cc_pll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_ELU],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_pll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&gpu_cc_pll0.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_taycan_elu_ops,
	},
};

static const struct parent_map gpu_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_MAIN, 1 },
	{ P_GPU_CC_PLL0_OUT_EVEN, 2 },
	{ P_GPU_CC_PLL0_OUT_ODD, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .hw = &gpu_cc_pll0_out_even.clkr.hw },
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .index = DT_GPLL0_OUT_MAIN },
	{ .index = DT_GPLL0_OUT_MAIN_DIV },
};

static const struct freq_tbl ftbl_gpu_cc_gmu_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(500000000, P_GPU_CC_PLL0_OUT_EVEN, 1, 0, 0),
	F(650000000, P_GPU_CC_PLL0_OUT_EVEN, 1, 0, 0),
	F(687500000, P_GPU_CC_PLL0_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gmu_clk_src = {
	.cmd_rcgr = 0x9318,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_1,
	.freq_tbl = ftbl_gpu_cc_gmu_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_gmu_clk_src",
		.parent_data = gpu_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gpu_cc_hub_clk_src[] = {
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	F(300000000, P_GPLL0_OUT_MAIN, 2, 0, 0),
	F(400000000, P_GPLL0_OUT_MAIN, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_hub_clk_src = {
	.cmd_rcgr = 0x93ec,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_1,
	.freq_tbl = ftbl_gpu_cc_hub_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_hub_clk_src",
		.parent_data = gpu_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div gpu_cc_hub_div_clk_src = {
	.reg = 0x942c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_hub_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gpu_cc_hub_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gpu_cc_ahb_clk = {
	.halt_reg = 0x90bc,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x90bc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_ahb_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_hub_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_accu_shift_clk = {
	.halt_reg = 0x910c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x910c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cx_accu_shift_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gmu_clk = {
	.halt_reg = 0x90d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x90d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cx_gmu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gpu_cc_cxo_clk = {
	.halt_reg = 0x90e4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x90e4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cxo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_demet_clk = {
	.halt_reg = 0x9010,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_demet_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_dpm_clk = {
	.halt_reg = 0x9110,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9110,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_dpm_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_freq_measure_clk = {
	.halt_reg = 0x900c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x900c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_freq_measure_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gx_accu_shift_clk = {
	.halt_reg = 0x9070,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9070,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_gx_accu_shift_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gx_gmu_clk = {
	.halt_reg = 0x9060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_gx_gmu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_hlos1_vote_gpu_smmu_clk = {
	.halt_reg = 0x7000,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_hlos1_vote_gpu_smmu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_hub_aon_clk = {
	.halt_reg = 0x93e8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x93e8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_hub_aon_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_hub_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gpu_cc_hub_cx_int_clk = {
	.halt_reg = 0x90e8,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x90e8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_hub_cx_int_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_hub_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gpu_cc_memnoc_gfx_clk = {
	.halt_reg = 0x90f4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x90f4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_memnoc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc gpu_cc_cx_gdsc = {
	.gdscr = 0x9080,
	.gds_hw_ctrl = 0x9094,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x8,
	.pd = {
		.name = "gpu_cc_cx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE | VOTABLE,
};

static struct clk_regmap *gpu_cc_sm8750_clocks[] = {
	[GPU_CC_AHB_CLK] = &gpu_cc_ahb_clk.clkr,
	[GPU_CC_CX_ACCU_SHIFT_CLK] = &gpu_cc_cx_accu_shift_clk.clkr,
	[GPU_CC_CX_GMU_CLK] = &gpu_cc_cx_gmu_clk.clkr,
	[GPU_CC_CXO_CLK] = &gpu_cc_cxo_clk.clkr,
	[GPU_CC_DEMET_CLK] = &gpu_cc_demet_clk.clkr,
	[GPU_CC_DPM_CLK] = &gpu_cc_dpm_clk.clkr,
	[GPU_CC_FREQ_MEASURE_CLK] = &gpu_cc_freq_measure_clk.clkr,
	[GPU_CC_GMU_CLK_SRC] = &gpu_cc_gmu_clk_src.clkr,
	[GPU_CC_GX_ACCU_SHIFT_CLK] = &gpu_cc_gx_accu_shift_clk.clkr,
	[GPU_CC_GX_GMU_CLK] = &gpu_cc_gx_gmu_clk.clkr,
	[GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK] = &gpu_cc_hlos1_vote_gpu_smmu_clk.clkr,
	[GPU_CC_HUB_AON_CLK] = &gpu_cc_hub_aon_clk.clkr,
	[GPU_CC_HUB_CLK_SRC] = &gpu_cc_hub_clk_src.clkr,
	[GPU_CC_HUB_CX_INT_CLK] = &gpu_cc_hub_cx_int_clk.clkr,
	[GPU_CC_HUB_DIV_CLK_SRC] = &gpu_cc_hub_div_clk_src.clkr,
	[GPU_CC_MEMNOC_GFX_CLK] = &gpu_cc_memnoc_gfx_clk.clkr,
	[GPU_CC_PLL0] = &gpu_cc_pll0.clkr,
	[GPU_CC_PLL0_OUT_EVEN] = &gpu_cc_pll0_out_even.clkr,
};

static struct gdsc *gpu_cc_sm8750_gdscs[] = {
	[GPU_CC_CX_GDSC] = &gpu_cc_cx_gdsc,
};

static const struct qcom_reset_map gpu_cc_sm8750_resets[] = {
	[GPU_CC_GPU_CC_XO_BCR] = { 0x9000 },
	[GPU_CC_GPU_CC_GX_BCR] = { 0x905c },
	[GPU_CC_GPU_CC_CX_BCR] = { 0x907c },
	[GPU_CC_GPU_CC_GMU_BCR] = { 0x9314 },
	[GPU_CC_GPU_CC_CB_BCR] = { 0x93a0 },
	[GPU_CC_GPU_CC_FAST_HUB_BCR] = { 0x93e4 },
};

static const struct regmap_config gpu_cc_sm8750_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x9800,
	.fast_io = true,
};

static struct clk_alpha_pll *gpu_cc_alpha_plls[] = {
	&gpu_cc_pll0,
};

static const u32 gpu_cc_sm8750_critical_cbcrs[] = {
	0x9004, /* GPU_CC_RSCC_XO_AON_CLK */
	0x9008, /* GPU_CC_CXO_AON_CLK */
	0x9064, /* GPU_CC_GX_AHB_FF_CLK */
	0x90cc, /* GPU_CC_SLEEP_CLK */
	0x93a4, /* GPU_CC_CB_CLK */
	0x93a8, /* GPU_CC_RSCC_HUB_AON_CLK */
};

static const struct qcom_cc_driver_data gpu_cc_sm8750_driver_data = {
	.alpha_plls = gpu_cc_alpha_plls,
	.num_alpha_plls = ARRAY_SIZE(gpu_cc_alpha_plls),
	.clk_cbcrs = gpu_cc_sm8750_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(gpu_cc_sm8750_critical_cbcrs),
};

static const struct qcom_cc_desc gpu_cc_sm8750_desc = {
	.config = &gpu_cc_sm8750_regmap_config,
	.clks = gpu_cc_sm8750_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_sm8750_clocks),
	.resets = gpu_cc_sm8750_resets,
	.num_resets = ARRAY_SIZE(gpu_cc_sm8750_resets),
	.gdscs = gpu_cc_sm8750_gdscs,
	.num_gdscs = ARRAY_SIZE(gpu_cc_sm8750_gdscs),
	.use_rpm = true,
	.driver_data = &gpu_cc_sm8750_driver_data,
};

static const struct of_device_id gpu_cc_sm8750_match_table[] = {
	{ .compatible = "qcom,sm8750-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpu_cc_sm8750_match_table);

static int gpu_cc_sm8750_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gpu_cc_sm8750_desc);
}

static struct platform_driver gpu_cc_sm8750_driver = {
	.probe = gpu_cc_sm8750_probe,
	.driver = {
		.name = "sm8750-gpucc",
		.of_match_table = gpu_cc_sm8750_match_table,
	},
};
module_platform_driver(gpu_cc_sm8750_driver);

MODULE_DESCRIPTION("QTI GPU_CC SM8750 Driver");
MODULE_LICENSE("GPL");
