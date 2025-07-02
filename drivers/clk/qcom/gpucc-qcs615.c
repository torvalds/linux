// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,qcs615-gpucc.h>

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
	P_GPU_CC_PLL0_2X_CLK,
	P_CRC_DIV_PLL0_OUT_AUX2,
	P_GPU_CC_PLL0_OUT_MAIN,
	P_GPU_CC_PLL1_OUT_AUX,
	P_CRC_DIV_PLL1_OUT_AUX2,
	P_GPU_CC_PLL1_OUT_MAIN,
};

static const struct pll_vco gpu_cc_pll0_vco[] = {
	{ 1000000000, 2100000000, 0 },
};

static struct pll_vco gpu_cc_pll1_vco[] = {
	{ 500000000,  1000000000, 2 },
};

/* 1020MHz configuration VCO - 0 */
static struct alpha_pll_config gpu_cc_pll0_config = {
	.l = 0x35,
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
	.alpha_hi = 0x20,
	.alpha = 0x00,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x0,
	.vco_mask = GENMASK(21, 20),
	.aux2_output_mask = BIT(2),
};

static struct clk_alpha_pll gpu_cc_pll0 = {
	.offset = 0x0,
	.config = &gpu_cc_pll0_config,
	.vco_table = gpu_cc_pll0_vco,
	.num_vco = ARRAY_SIZE(gpu_cc_pll0_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_slew_ops,
		},
	},
};

/* 930MHz configuration VCO - 2 */
static struct alpha_pll_config gpu_cc_pll1_config = {
	.l = 0x30,
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
	.alpha_hi = 0x70,
	.alpha = 0x00,
	.alpha_en_mask = BIT(24),
	.vco_val = BIT(21),
	.vco_mask = GENMASK(21, 20),
	.aux2_output_mask = BIT(2),
};

static struct clk_alpha_pll gpu_cc_pll1 = {
	.offset = 0x100,
	.config = &gpu_cc_pll1_config,
	.vco_table = gpu_cc_pll1_vco,
	.num_vco = ARRAY_SIZE(gpu_cc_pll1_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_pll1",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_slew_ops,
		},
	}
};

/* Clock Ramp Controller */
static struct clk_fixed_factor crc_div_pll0 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "crc_div_pll0",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpu_cc_pll0.clkr.hw,
			},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

/* Clock Ramp Controller */
static struct clk_fixed_factor crc_div_pll1 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "crc_div_pll1",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpu_cc_pll1.clkr.hw,
			},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static const struct parent_map gpu_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_MAIN, 1 },
	{ P_GPU_CC_PLL1_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .index = DT_GPLL0_OUT_MAIN },
	{ .index = DT_GPLL0_OUT_MAIN_DIV },
};

static const struct parent_map gpu_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_2X_CLK, 1 },
	{ P_CRC_DIV_PLL0_OUT_AUX2, 2 },
	{ P_GPU_CC_PLL1_OUT_AUX, 3 },
	{ P_CRC_DIV_PLL1_OUT_AUX2, 4 },
	{ P_GPLL0_OUT_MAIN, 5 },
};

static const struct clk_parent_data gpu_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .hw = &crc_div_pll0.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .hw = &crc_div_pll1.hw },
	{ .index = DT_GPLL0_OUT_MAIN },
};

static const struct freq_tbl ftbl_gpu_cc_gmu_clk_src[] = {
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gmu_clk_src = {
	.cmd_rcgr = 0x1120,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_0,
	.freq_tbl = ftbl_gpu_cc_gmu_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_gmu_clk_src",
		.parent_data = gpu_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gpu_cc_gx_gfx3d_clk_src[] = {
	F(290000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(350000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(435000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(500000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(550000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(650000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(700000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(745000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(845000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(895000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gx_gfx3d_clk_src = {
	.cmd_rcgr = 0x101c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_1,
	.freq_tbl = ftbl_gpu_cc_gx_gfx3d_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_gx_gfx3d_clk_src",
		.parent_data = gpu_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_branch gpu_cc_crc_ahb_clk = {
	.halt_reg = 0x107c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x107c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_crc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gfx3d_clk = {
	.halt_reg = 0x10a4,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x10a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cx_gfx3d_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gfx3d_slv_clk = {
	.halt_reg = 0x10a8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x10a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cx_gfx3d_slv_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gmu_clk = {
	.halt_reg = 0x1098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1098,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cx_gmu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_snoc_dvm_clk = {
	.halt_reg = 0x108c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x108c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cx_snoc_dvm_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cxo_aon_clk = {
	.halt_reg = 0x1004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x1004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cxo_aon_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cxo_clk = {
	.halt_reg = 0x109c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x109c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cxo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gx_gfx3d_clk = {
	.halt_reg = 0x1054,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x1054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_gx_gfx3d_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gx_gmu_clk = {
	.halt_reg = 0x1064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1064,
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
	.halt_reg = 0x5000,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x5000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_hlos1_vote_gpu_smmu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_sleep_clk = {
	.halt_reg = 0x1090,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x1090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_hw *gpu_cc_qcs615_hws[] = {
	[CRC_DIV_PLL0] = &crc_div_pll0.hw,
	[CRC_DIV_PLL1] = &crc_div_pll1.hw,
};

static struct gdsc cx_gdsc = {
	.gdscr = 0x106c,
	.gds_hw_ctrl = 0x1540,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x8,
	.pd = {
		.name = "cx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc gx_gdsc = {
	.gdscr = 0x100c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x2,
	.pd = {
		.name = "gx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct clk_regmap *gpu_cc_qcs615_clocks[] = {
	[GPU_CC_CRC_AHB_CLK] = &gpu_cc_crc_ahb_clk.clkr,
	[GPU_CC_CX_GFX3D_CLK] = &gpu_cc_cx_gfx3d_clk.clkr,
	[GPU_CC_CX_GFX3D_SLV_CLK] = &gpu_cc_cx_gfx3d_slv_clk.clkr,
	[GPU_CC_CX_GMU_CLK] = &gpu_cc_cx_gmu_clk.clkr,
	[GPU_CC_CX_SNOC_DVM_CLK] = &gpu_cc_cx_snoc_dvm_clk.clkr,
	[GPU_CC_CXO_AON_CLK] = &gpu_cc_cxo_aon_clk.clkr,
	[GPU_CC_CXO_CLK] = &gpu_cc_cxo_clk.clkr,
	[GPU_CC_GMU_CLK_SRC] = &gpu_cc_gmu_clk_src.clkr,
	[GPU_CC_GX_GFX3D_CLK] = &gpu_cc_gx_gfx3d_clk.clkr,
	[GPU_CC_GX_GFX3D_CLK_SRC] = &gpu_cc_gx_gfx3d_clk_src.clkr,
	[GPU_CC_GX_GMU_CLK] = &gpu_cc_gx_gmu_clk.clkr,
	[GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK] = &gpu_cc_hlos1_vote_gpu_smmu_clk.clkr,
	[GPU_CC_PLL0] = &gpu_cc_pll0.clkr,
	[GPU_CC_PLL1] = &gpu_cc_pll1.clkr,
	[GPU_CC_SLEEP_CLK] = &gpu_cc_sleep_clk.clkr,
};

static struct gdsc *gpu_cc_qcs615_gdscs[] = {
	[CX_GDSC] = &cx_gdsc,
	[GX_GDSC] = &gx_gdsc,
};

static const struct qcom_reset_map gpu_cc_qcs615_resets[] = {
	[GPU_CC_CX_BCR] = { 0x1068 },
	[GPU_CC_GFX3D_AON_BCR] = { 0x10a0 },
	[GPU_CC_GMU_BCR] = { 0x111c },
	[GPU_CC_GX_BCR] = { 0x1008 },
	[GPU_CC_XO_BCR] = { 0x1000 },
};

static struct clk_alpha_pll *gpu_cc_qcs615_plls[] = {
	&gpu_cc_pll0,
	&gpu_cc_pll1,
};

static u32 gpu_cc_qcs615_critical_cbcrs[] = {
	0x1078, /* GPU_CC_AHB_CLK */
};

static const struct regmap_config gpu_cc_qcs615_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x7008,
	.fast_io = true,
};

static void clk_qcs615_regs_crc_configure(struct device *dev, struct regmap *regmap)
{
	/* Recommended WAKEUP/SLEEP settings for the gpu_cc_cx_gmu_clk */
	regmap_update_bits(regmap, gpu_cc_cx_gmu_clk.clkr.enable_reg, 0xff0, 0xff0);

	/*
	 * After POR, Clock Ramp Controller(CRC) will be in bypass mode.
	 * Software needs to do the following operation to enable the CRC
	 * for GFX3D clock and divide the input clock by div by 2.
	 */
	regmap_update_bits(regmap, 0x1028, 0x00015011, 0x00015011);
	regmap_update_bits(regmap, 0x1024, 0x00800000, 0x00800000);
}

static struct qcom_cc_driver_data gpu_cc_qcs615_driver_data = {
	.alpha_plls = gpu_cc_qcs615_plls,
	.num_alpha_plls = ARRAY_SIZE(gpu_cc_qcs615_plls),
	.clk_cbcrs = gpu_cc_qcs615_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(gpu_cc_qcs615_critical_cbcrs),
	.clk_regs_configure = clk_qcs615_regs_crc_configure,
};

static const struct qcom_cc_desc gpu_cc_qcs615_desc = {
	.config = &gpu_cc_qcs615_regmap_config,
	.clks = gpu_cc_qcs615_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_qcs615_clocks),
	.clk_hws = gpu_cc_qcs615_hws,
	.num_clk_hws = ARRAY_SIZE(gpu_cc_qcs615_hws),
	.resets = gpu_cc_qcs615_resets,
	.num_resets = ARRAY_SIZE(gpu_cc_qcs615_resets),
	.gdscs = gpu_cc_qcs615_gdscs,
	.num_gdscs = ARRAY_SIZE(gpu_cc_qcs615_gdscs),
	.driver_data = &gpu_cc_qcs615_driver_data,
};

static const struct of_device_id gpu_cc_qcs615_match_table[] = {
	{ .compatible = "qcom,qcs615-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpu_cc_qcs615_match_table);

static int gpu_cc_qcs615_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gpu_cc_qcs615_desc);
}

static struct platform_driver gpu_cc_qcs615_driver = {
	.probe = gpu_cc_qcs615_probe,
	.driver = {
		.name = "gpucc-qcs615",
		.of_match_table = gpu_cc_qcs615_match_table,
	},
};

module_platform_driver(gpu_cc_qcs615_driver);

MODULE_DESCRIPTION("QTI GPUCC QCS615 Driver");
MODULE_LICENSE("GPL");
