// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,nord-gpucc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
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
	P_GPU_CC_PLL0_OUT_MAIN,
	P_GPU_CC_PLL1_OUT_MAIN,
};

static const struct pll_vco lucid_ole_vco[] = {
	{ 249600000, 2300000000, 0 },
};

/* 936.0 MHz Configuration */
static const struct alpha_pll_config gpu_cc_pll0_config = {
	.l = 0x30,
	.alpha = 0xc000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82aa299c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00400005,
};

static struct clk_alpha_pll gpu_cc_pll0 = {
	.offset = 0x0,
	.config = &gpu_cc_pll0_config,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

/* 1250.0 MHz Configuration */
static const struct alpha_pll_config gpu_cc_pll1_config = {
	.l = 0x41,
	.alpha = 0x1aaa,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82aa299c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00400005,
};

static struct clk_alpha_pll gpu_cc_pll1 = {
	.offset = 0x1000,
	.config = &gpu_cc_pll1_config,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_pll1",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

static const struct parent_map gpu_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .index = DT_GPLL0_OUT_MAIN },
	{ .index = DT_GPLL0_OUT_MAIN_DIV },
};

static const struct parent_map gpu_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_MAIN, 1 },
	{ P_GPU_CC_PLL1_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .index = DT_GPLL0_OUT_MAIN },
	{ .index = DT_GPLL0_OUT_MAIN_DIV },
};

static const struct parent_map gpu_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL1_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .index = DT_GPLL0_OUT_MAIN },
	{ .index = DT_GPLL0_OUT_MAIN_DIV },
};

static const struct freq_tbl ftbl_gpu_cc_ff_clk_src[] = {
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_ff_clk_src = {
	.cmd_rcgr = 0x93d4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_0,
	.freq_tbl = ftbl_gpu_cc_ff_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_ff_clk_src",
		.parent_data = gpu_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gpu_cc_gmu_clk_src[] = {
	F(416666667, P_GPU_CC_PLL1_OUT_MAIN, 3, 0, 0),
	F(625000000, P_GPU_CC_PLL1_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gmu_clk_src = {
	.cmd_rcgr = 0x92b8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_1,
	.freq_tbl = ftbl_gpu_cc_gmu_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_gmu_clk_src",
		.parent_data = gpu_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gpu_cc_hub_clk_src[] = {
	F(300000000, P_GPLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_hub_clk_src = {
	.cmd_rcgr = 0x938c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_2,
	.freq_tbl = ftbl_gpu_cc_hub_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_hub_clk_src",
		.parent_data = gpu_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div gpu_cc_hub_div_clk_src = {
	.reg = 0x93cc,
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

static struct clk_branch gpu_cc_acd_gfx3d_clk = {
	.halt_reg = 0x92a8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x92a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_acd_gfx3d_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_acmu_clk = {
	.halt_reg = 0x9294,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9294,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_acmu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_ahb_clk = {
	.halt_reg = 0x9150,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x9150,
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

static struct clk_branch gpu_cc_crc_ahb_clk = {
	.halt_reg = 0x9154,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9154,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_crc_ahb_clk",
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
	.halt_reg = 0x91a4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x91a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cx_accu_shift_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_ff_clk = {
	.halt_reg = 0x9184,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9184,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cx_ff_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_ff_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gmu_clk = {
	.halt_reg = 0x916c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x916c,
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
	.halt_reg = 0x917c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x917c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cxo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_dpm_clk = {
	.halt_reg = 0x91a8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x91a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_dpm_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_freq_measure_clk = {
	.halt_reg = 0x9008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_freq_measure_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gpu_smmu_vote_clk = {
	.halt_reg = 0x7000,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x7000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_gpu_smmu_vote_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_hub_aon_clk = {
	.halt_reg = 0x9388,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9388,
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
	.halt_reg = 0x9180,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9180,
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
	.halt_reg = 0x9188,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9188,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_memnoc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_mnd1x_gfx3d_clk = {
	.halt_reg = 0x92ac,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x92ac,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_mnd1x_gfx3d_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_sleep_clk = {
	.halt_reg = 0x9164,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9164,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc gpu_cc_cx_gdsc = {
	.gdscr = 0x90e8,
	.gds_hw_ctrl = 0x9128,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gpu_cc_cx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc gpu_cc_gx_gdsc = {
	.gdscr = 0x905c,
	.clamp_io_ctrl = 0x9504,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gpu_cc_gx_gdsc",
		.power_on = gdsc_gx_do_nothing_enable,
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = CLAMP_IO | POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct clk_regmap *gpu_cc_nord_clocks[] = {
	[GPU_CC_ACD_GFX3D_CLK] = &gpu_cc_acd_gfx3d_clk.clkr,
	[GPU_CC_ACMU_CLK] = &gpu_cc_acmu_clk.clkr,
	[GPU_CC_AHB_CLK] = &gpu_cc_ahb_clk.clkr,
	[GPU_CC_CRC_AHB_CLK] = &gpu_cc_crc_ahb_clk.clkr,
	[GPU_CC_CX_ACCU_SHIFT_CLK] = &gpu_cc_cx_accu_shift_clk.clkr,
	[GPU_CC_CX_FF_CLK] = &gpu_cc_cx_ff_clk.clkr,
	[GPU_CC_CX_GMU_CLK] = &gpu_cc_cx_gmu_clk.clkr,
	[GPU_CC_CXO_CLK] = &gpu_cc_cxo_clk.clkr,
	[GPU_CC_DPM_CLK] = &gpu_cc_dpm_clk.clkr,
	[GPU_CC_FF_CLK_SRC] = &gpu_cc_ff_clk_src.clkr,
	[GPU_CC_FREQ_MEASURE_CLK] = &gpu_cc_freq_measure_clk.clkr,
	[GPU_CC_GMU_CLK_SRC] = &gpu_cc_gmu_clk_src.clkr,
	[GPU_CC_GPU_SMMU_VOTE_CLK] = &gpu_cc_gpu_smmu_vote_clk.clkr,
	[GPU_CC_HUB_AON_CLK] = &gpu_cc_hub_aon_clk.clkr,
	[GPU_CC_HUB_CLK_SRC] = &gpu_cc_hub_clk_src.clkr,
	[GPU_CC_HUB_CX_INT_CLK] = &gpu_cc_hub_cx_int_clk.clkr,
	[GPU_CC_HUB_DIV_CLK_SRC] = &gpu_cc_hub_div_clk_src.clkr,
	[GPU_CC_MEMNOC_GFX_CLK] = &gpu_cc_memnoc_gfx_clk.clkr,
	[GPU_CC_MND1X_GFX3D_CLK] = &gpu_cc_mnd1x_gfx3d_clk.clkr,
	[GPU_CC_PLL0] = &gpu_cc_pll0.clkr,
	[GPU_CC_PLL1] = &gpu_cc_pll1.clkr,
	[GPU_CC_SLEEP_CLK] = &gpu_cc_sleep_clk.clkr,
};

static struct gdsc *gpu_cc_nord_gdscs[] = {
	[GPU_CC_CX_GDSC] = &gpu_cc_cx_gdsc,
	[GPU_CC_GX_GDSC] = &gpu_cc_gx_gdsc,
};

static const struct qcom_reset_map gpu_cc_nord_resets[] = {
	[GPU_CC_ACD_BCR] = { 0x92f8 },
	[GPU_CC_CB_BCR] = { 0x9340 },
	[GPU_CC_CX_BCR] = { 0x90e4 },
	[GPU_CC_FAST_HUB_BCR] = { 0x9384 },
	[GPU_CC_GFX3D_AON_BCR] = { 0x91ac },
	[GPU_CC_GX_BCR] = { 0x9058 },
	[GPU_CC_XO_BCR] = { 0x9000 },
};

static struct clk_alpha_pll *gpu_cc_nord_plls[] = {
	&gpu_cc_pll0,
	&gpu_cc_pll1,
};

static const u32 gpu_cc_nord_critical_cbcrs[] = {
	0x9004, /* GPU_CC_CXO_AON_CLK */
	0x900c, /* GPU_CC_DEMET_CLK */
};

static const struct regmap_config gpu_cc_nord_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x9660,
	.fast_io = true,
};

static const struct qcom_cc_driver_data gpu_cc_nord_driver_data = {
	.alpha_plls = gpu_cc_nord_plls,
	.num_alpha_plls = ARRAY_SIZE(gpu_cc_nord_plls),
	.clk_cbcrs = gpu_cc_nord_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(gpu_cc_nord_critical_cbcrs),
};

static const struct qcom_cc_desc gpu_cc_nord_desc = {
	.config = &gpu_cc_nord_regmap_config,
	.clks = gpu_cc_nord_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_nord_clocks),
	.resets = gpu_cc_nord_resets,
	.num_resets = ARRAY_SIZE(gpu_cc_nord_resets),
	.gdscs = gpu_cc_nord_gdscs,
	.num_gdscs = ARRAY_SIZE(gpu_cc_nord_gdscs),
	.use_rpm = true,
	.driver_data = &gpu_cc_nord_driver_data,
};

static const struct of_device_id gpu_cc_nord_match_table[] = {
	{ .compatible = "qcom,nord-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpu_cc_nord_match_table);

static int gpu_cc_nord_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gpu_cc_nord_desc);
}

static struct platform_driver gpu_cc_nord_driver = {
	.probe = gpu_cc_nord_probe,
	.driver = {
		.name = "gpucc-nord",
		.of_match_table = gpu_cc_nord_match_table,
	},
};

module_platform_driver(gpu_cc_nord_driver);

MODULE_DESCRIPTION("QTI GPUCC Nord Driver");
MODULE_LICENSE("GPL");
