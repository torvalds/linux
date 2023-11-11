// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020, AngeloGioacchino Del Regno
 *                     <angelogioacchino.delregno@somainline.org>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <dt-bindings/clock/qcom,gpucc-sdm660.h>

#include "clk-alpha-pll.h"
#include "common.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "gdsc.h"
#include "reset.h"

enum {
	P_GPU_XO,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_MAIN_DIV,
	P_GPU_PLL0_PLL_OUT_MAIN,
	P_GPU_PLL1_PLL_OUT_MAIN,
};

static struct clk_branch gpucc_cxo_clk = {
	.halt_reg = 0x1020,
	.clkr = {
		.enable_reg = 0x1020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cxo_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo"
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_IS_CRITICAL,
		},
	},
};

static struct pll_vco gpu_vco[] = {
	{ 1000000000, 2000000000, 0 },
	{ 500000000,  1000000000, 2 },
	{ 250000000,   500000000, 3 },
};

static struct clk_alpha_pll gpu_pll0_pll_out_main = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = gpu_vco,
	.num_vco = ARRAY_SIZE(gpu_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpu_pll0_pll_out_main",
		.parent_hws = (const struct clk_hw*[]){
			&gpucc_cxo_clk.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
	},
};

static struct clk_alpha_pll gpu_pll1_pll_out_main = {
	.offset = 0x40,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.vco_table = gpu_vco,
	.num_vco = ARRAY_SIZE(gpu_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpu_pll1_pll_out_main",
		.parent_hws = (const struct clk_hw*[]){
			&gpucc_cxo_clk.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_ops,
	},
};

static const struct parent_map gpucc_parent_map_1[] = {
	{ P_GPU_XO, 0 },
	{ P_GPU_PLL0_PLL_OUT_MAIN, 1 },
	{ P_GPU_PLL1_PLL_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
};

static const struct clk_parent_data gpucc_parent_data_1[] = {
	{ .hw = &gpucc_cxo_clk.clkr.hw },
	{ .hw = &gpu_pll0_pll_out_main.clkr.hw },
	{ .hw = &gpu_pll1_pll_out_main.clkr.hw },
	{ .fw_name = "gcc_gpu_gpll0_clk" },
};

static struct clk_rcg2_gfx3d gfx3d_clk_src = {
	.div = 2,
	.rcg = {
		.cmd_rcgr = 0x1070,
		.mnd_width = 0,
		.hid_width = 5,
		.parent_map = gpucc_parent_map_1,
		.clkr.hw.init = &(struct clk_init_data){
			.name = "gfx3d_clk_src",
			.parent_data = gpucc_parent_data_1,
			.num_parents = ARRAY_SIZE(gpucc_parent_data_1),
			.ops = &clk_gfx3d_ops,
			.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		},
	},
	.hws = (struct clk_hw*[]){
		&gpucc_cxo_clk.clkr.hw,
		&gpu_pll0_pll_out_main.clkr.hw,
		&gpu_pll1_pll_out_main.clkr.hw,
	}
};

static struct clk_branch gpucc_gfx3d_clk = {
	.halt_reg = 0x1098,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1098,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_gfx3d_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gfx3d_clk_src.rcg.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct parent_map gpucc_parent_map_0[] = {
	{ P_GPU_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
};

static const struct clk_parent_data gpucc_parent_data_0[] = {
	{ .hw = &gpucc_cxo_clk.clkr.hw },
	{ .fw_name = "gcc_gpu_gpll0_clk" },
	{ .fw_name = "gcc_gpu_gpll0_div_clk" },
};

static const struct freq_tbl ftbl_rbbmtimer_clk_src[] = {
	F(19200000, P_GPU_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 rbbmtimer_clk_src = {
	.cmd_rcgr = 0x10b0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpucc_parent_map_0,
	.freq_tbl = ftbl_rbbmtimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "rbbmtimer_clk_src",
		.parent_data = gpucc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpucc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_rbcpr_clk_src[] = {
	F(19200000, P_GPU_XO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_MAIN_DIV, 6, 0, 0),
	{ }
};

static struct clk_rcg2 rbcpr_clk_src = {
	.cmd_rcgr = 0x1030,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpucc_parent_map_0,
	.freq_tbl = ftbl_rbcpr_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "rbcpr_clk_src",
		.parent_data = gpucc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpucc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gpucc_rbbmtimer_clk = {
	.halt_reg = 0x10d0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10d0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_rbbmtimer_clk",
			.parent_hws = (const struct clk_hw*[]){
				&rbbmtimer_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_rbcpr_clk = {
	.halt_reg = 0x1054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_rbcpr_clk",
			.parent_hws = (const struct clk_hw*[]){
				&rbcpr_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc gpu_cx_gdsc = {
	.gdscr = 0x1004,
	.gds_hw_ctrl = 0x1008,
	.pd = {
		.name = "gpu_cx",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc gpu_gx_gdsc = {
	.gdscr = 0x1094,
	.clamp_io_ctrl = 0x130,
	.resets = (unsigned int []){ GPU_GX_BCR },
	.reset_count = 1,
	.cxcs = (unsigned int []){ 0x1098 },
	.cxc_count = 1,
	.pd = {
		.name = "gpu_gx",
	},
	.parent = &gpu_cx_gdsc.pd,
	.pwrsts = PWRSTS_OFF | PWRSTS_ON | PWRSTS_RET,
	.flags = CLAMP_IO | SW_RESET | AON_RESET | NO_RET_PERIPH,
};

static struct gdsc *gpucc_sdm660_gdscs[] = {
	[GPU_CX_GDSC] = &gpu_cx_gdsc,
	[GPU_GX_GDSC] = &gpu_gx_gdsc,
};

static const struct qcom_reset_map gpucc_sdm660_resets[] = {
	[GPU_CX_BCR] = { 0x1000 },
	[RBCPR_BCR] = { 0x1050 },
	[GPU_GX_BCR] = { 0x1090 },
	[SPDM_BCR] = { 0x10E0 },
};

static struct clk_regmap *gpucc_sdm660_clocks[] = {
	[GPUCC_CXO_CLK] = &gpucc_cxo_clk.clkr,
	[GPU_PLL0_PLL] = &gpu_pll0_pll_out_main.clkr,
	[GPU_PLL1_PLL] = &gpu_pll1_pll_out_main.clkr,
	[GFX3D_CLK_SRC] = &gfx3d_clk_src.rcg.clkr,
	[RBCPR_CLK_SRC] = &rbcpr_clk_src.clkr,
	[RBBMTIMER_CLK_SRC] = &rbbmtimer_clk_src.clkr,
	[GPUCC_RBCPR_CLK] = &gpucc_rbcpr_clk.clkr,
	[GPUCC_GFX3D_CLK] = &gpucc_gfx3d_clk.clkr,
	[GPUCC_RBBMTIMER_CLK] = &gpucc_rbbmtimer_clk.clkr,
};

static const struct regmap_config gpucc_660_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x9034,
	.fast_io	= true,
};

static const struct qcom_cc_desc gpucc_sdm660_desc = {
	.config = &gpucc_660_regmap_config,
	.clks = gpucc_sdm660_clocks,
	.num_clks = ARRAY_SIZE(gpucc_sdm660_clocks),
	.resets = gpucc_sdm660_resets,
	.num_resets = ARRAY_SIZE(gpucc_sdm660_resets),
	.gdscs = gpucc_sdm660_gdscs,
	.num_gdscs = ARRAY_SIZE(gpucc_sdm660_gdscs),
};

static const struct of_device_id gpucc_sdm660_match_table[] = {
	{ .compatible = "qcom,gpucc-sdm660" },
	{ .compatible = "qcom,gpucc-sdm630" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpucc_sdm660_match_table);

static int gpucc_sdm660_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	struct alpha_pll_config gpu_pll_config = {
		.config_ctl_val = 0x4001055b,
		.alpha = 0xaaaaab00,
		.alpha_en_mask = BIT(24),
		.vco_val = 0x2 << 20,
		.vco_mask = 0x3 << 20,
		.main_output_mask = 0x1,
	};

	regmap = qcom_cc_map(pdev, &gpucc_sdm660_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* 800MHz configuration for GPU PLL0 */
	gpu_pll_config.l = 0x29;
	gpu_pll_config.alpha_hi = 0xaa;
	clk_alpha_pll_configure(&gpu_pll0_pll_out_main, regmap, &gpu_pll_config);

	/* 740MHz configuration for GPU PLL1 */
	gpu_pll_config.l = 0x26;
	gpu_pll_config.alpha_hi = 0x8a;
	clk_alpha_pll_configure(&gpu_pll1_pll_out_main, regmap, &gpu_pll_config);

	return qcom_cc_really_probe(pdev, &gpucc_sdm660_desc, regmap);
}

static struct platform_driver gpucc_sdm660_driver = {
	.probe		= gpucc_sdm660_probe,
	.driver		= {
		.name	= "gpucc-sdm660",
		.of_match_table = gpucc_sdm660_match_table,
	},
};
module_platform_driver(gpucc_sdm660_driver);

MODULE_DESCRIPTION("Qualcomm SDM630/SDM660 GPUCC Driver");
MODULE_LICENSE("GPL v2");
