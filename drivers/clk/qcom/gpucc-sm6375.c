// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sm6375-gpucc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-phy-mux.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_GCC_GPU_GPLL0_CLK_SRC,
	DT_GCC_GPU_GPLL0_DIV_CLK_SRC,
	DT_GCC_GPU_SNOC_DVM_GFX_CLK,
};

enum {
	P_BI_TCXO,
	P_GCC_GPU_GPLL0_CLK_SRC,
	P_GCC_GPU_GPLL0_DIV_CLK_SRC,
	P_GPU_CC_PLL0_OUT_EVEN,
	P_GPU_CC_PLL0_OUT_MAIN,
	P_GPU_CC_PLL0_OUT_ODD,
	P_GPU_CC_PLL1_OUT_EVEN,
	P_GPU_CC_PLL1_OUT_MAIN,
	P_GPU_CC_PLL1_OUT_ODD,
};

static struct pll_vco lucid_vco[] = {
	{ 249600000, 2000000000, 0 },
};

/* 532MHz Configuration */
static const struct alpha_pll_config gpucc_pll0_config = {
	.l = 0x1b,
	.alpha = 0xb555,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002261,
	.config_ctl_hi1_val = 0x329a299c,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x00000000,
};

static struct clk_alpha_pll gpucc_pll0 = {
	.offset = 0x0,
	.vco_table = lucid_vco,
	.num_vco = ARRAY_SIZE(lucid_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_pll0",
			.parent_data = &(const struct clk_parent_data){
				.index = P_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_ops,
		},
	},
};

/* 514MHz Configuration */
static const struct alpha_pll_config gpucc_pll1_config = {
	.l = 0x1a,
	.alpha = 0xc555,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002261,
	.config_ctl_hi1_val = 0x329a299c,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x00000000,
};

static struct clk_alpha_pll gpucc_pll1 = {
	.offset = 0x100,
	.vco_table = lucid_vco,
	.num_vco = ARRAY_SIZE(lucid_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_pll1",
			.parent_data = &(const struct clk_parent_data){
				.index = P_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_ops,
		},
	},
};

static const struct parent_map gpucc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_MAIN, 1 },
	{ P_GPU_CC_PLL1_OUT_MAIN, 3 },
	{ P_GCC_GPU_GPLL0_CLK_SRC, 5 },
	{ P_GCC_GPU_GPLL0_DIV_CLK_SRC, 6 },
};

static const struct clk_parent_data gpucc_parent_data_0[] = {
	{ .index = P_BI_TCXO },
	{ .hw = &gpucc_pll0.clkr.hw },
	{ .hw = &gpucc_pll1.clkr.hw },
	{ .index = DT_GCC_GPU_GPLL0_CLK_SRC },
	{ .index = DT_GCC_GPU_GPLL0_DIV_CLK_SRC },
};

static const struct parent_map gpucc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_EVEN, 1 },
	{ P_GPU_CC_PLL0_OUT_ODD, 2 },
	{ P_GPU_CC_PLL1_OUT_EVEN, 3 },
	{ P_GPU_CC_PLL1_OUT_ODD, 4 },
	{ P_GCC_GPU_GPLL0_CLK_SRC, 5 },
};

static const struct clk_parent_data gpucc_parent_data_1[] = {
	{ .index = P_BI_TCXO },
	{ .hw = &gpucc_pll0.clkr.hw },
	{ .hw = &gpucc_pll0.clkr.hw },
	{ .hw = &gpucc_pll1.clkr.hw },
	{ .hw = &gpucc_pll1.clkr.hw },
	{ .index = DT_GCC_GPU_GPLL0_CLK_SRC },
};

static const struct freq_tbl ftbl_gpucc_gmu_clk_src[] = {
	F(200000000, P_GCC_GPU_GPLL0_DIV_CLK_SRC, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 gpucc_gmu_clk_src = {
	.cmd_rcgr = 0x1120,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpucc_parent_map_0,
	.freq_tbl = ftbl_gpucc_gmu_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpucc_gmu_clk_src",
		.parent_data = gpucc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpucc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gpucc_gx_gfx3d_clk_src[] = {
	F(266000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(390000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(490000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(650000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(770000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(840000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(900000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gpucc_gx_gfx3d_clk_src = {
	.cmd_rcgr = 0x101c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpucc_parent_map_1,
	.freq_tbl = ftbl_gpucc_gx_gfx3d_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpucc_gx_gfx3d_clk_src",
		.parent_data = gpucc_parent_data_1,
		.num_parents = ARRAY_SIZE(gpucc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gpucc_ahb_clk = {
	.halt_reg = 0x1078,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1078,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_cx_gfx3d_clk = {
	.halt_reg = 0x10a4,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x10a4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cx_gfx3d_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpucc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_cx_gfx3d_slv_clk = {
	.halt_reg = 0x10a8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x10a8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cx_gfx3d_slv_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpucc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_cx_gmu_clk = {
	.halt_reg = 0x1098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cx_gmu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpucc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_cx_snoc_dvm_clk = {
	.halt_reg = 0x108c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x108c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cx_snoc_dvm_clk",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_GCC_GPU_SNOC_DVM_GFX_CLK,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_cxo_aon_clk = {
	.halt_reg = 0x1004,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cxo_aon_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_cxo_clk = {
	.halt_reg = 0x109c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x109c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cxo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_gx_cxo_clk = {
	.halt_reg = 0x1060,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1060,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_gx_cxo_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_gx_gfx3d_clk = {
	.halt_reg = 0x1054,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_gx_gfx3d_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpucc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_gx_gmu_clk = {
	.halt_reg = 0x1064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_gx_gmu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpucc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_sleep_clk = {
	.halt_reg = 0x1090,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x1090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc gpu_cx_gdsc = {
	.gdscr = 0x106c,
	.gds_hw_ctrl = 0x1540,
	.clk_dis_wait_val = 8,
	.pd = {
		.name = "gpu_cx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc gpu_gx_gdsc = {
	.gdscr = 0x100c,
	.clamp_io_ctrl = 0x1508,
	.resets = (unsigned int []){ GPU_GX_BCR, GPU_ACD_BCR, GPU_GX_ACD_MISC_BCR },
	.reset_count = 3,
	.pd = {
		.name = "gpu_gx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = CLAMP_IO | SW_RESET | AON_RESET,
};

static struct clk_regmap *gpucc_sm6375_clocks[] = {
	[GPU_CC_AHB_CLK] = &gpucc_ahb_clk.clkr,
	[GPU_CC_CX_GFX3D_CLK] = &gpucc_cx_gfx3d_clk.clkr,
	[GPU_CC_CX_GFX3D_SLV_CLK] = &gpucc_cx_gfx3d_slv_clk.clkr,
	[GPU_CC_CX_GMU_CLK] = &gpucc_cx_gmu_clk.clkr,
	[GPU_CC_CX_SNOC_DVM_CLK] = &gpucc_cx_snoc_dvm_clk.clkr,
	[GPU_CC_CXO_AON_CLK] = &gpucc_cxo_aon_clk.clkr,
	[GPU_CC_CXO_CLK] = &gpucc_cxo_clk.clkr,
	[GPU_CC_GMU_CLK_SRC] = &gpucc_gmu_clk_src.clkr,
	[GPU_CC_GX_CXO_CLK] = &gpucc_gx_cxo_clk.clkr,
	[GPU_CC_GX_GFX3D_CLK] = &gpucc_gx_gfx3d_clk.clkr,
	[GPU_CC_GX_GFX3D_CLK_SRC] = &gpucc_gx_gfx3d_clk_src.clkr,
	[GPU_CC_GX_GMU_CLK] = &gpucc_gx_gmu_clk.clkr,
	[GPU_CC_PLL0] = &gpucc_pll0.clkr,
	[GPU_CC_PLL1] = &gpucc_pll1.clkr,
	[GPU_CC_SLEEP_CLK] = &gpucc_sleep_clk.clkr,
};

static const struct qcom_reset_map gpucc_sm6375_resets[] = {
	[GPU_GX_BCR] = { 0x1008 },
	[GPU_ACD_BCR] = { 0x1160 },
	[GPU_GX_ACD_MISC_BCR] = { 0x8004 },
};

static struct gdsc *gpucc_sm6375_gdscs[] = {
	[GPU_CX_GDSC] = &gpu_cx_gdsc,
	[GPU_GX_GDSC] = &gpu_gx_gdsc,
};

static const struct regmap_config gpucc_sm6375_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x9000,
	.fast_io = true,
};

static const struct qcom_cc_desc gpucc_sm6375_desc = {
	.config = &gpucc_sm6375_regmap_config,
	.clks = gpucc_sm6375_clocks,
	.num_clks = ARRAY_SIZE(gpucc_sm6375_clocks),
	.resets = gpucc_sm6375_resets,
	.num_resets = ARRAY_SIZE(gpucc_sm6375_resets),
	.gdscs = gpucc_sm6375_gdscs,
	.num_gdscs = ARRAY_SIZE(gpucc_sm6375_gdscs),
};

static const struct of_device_id gpucc_sm6375_match_table[] = {
	{ .compatible = "qcom,sm6375-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpucc_sm6375_match_table);

static int gpucc_sm6375_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &gpucc_sm6375_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_lucid_pll_configure(&gpucc_pll0, regmap, &gpucc_pll0_config);
	clk_lucid_pll_configure(&gpucc_pll1, regmap, &gpucc_pll1_config);

	return qcom_cc_really_probe(pdev, &gpucc_sm6375_desc, regmap);
}

static struct platform_driver gpucc_sm6375_driver = {
	.probe = gpucc_sm6375_probe,
	.driver = {
		.name = "gpucc-sm6375",
		.of_match_table = gpucc_sm6375_match_table,
	},
};
module_platform_driver(gpucc_sm6375_driver);

MODULE_DESCRIPTION("QTI GPUCC SM6375 Driver");
MODULE_LICENSE("GPL");
