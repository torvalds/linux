// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, Jeffrey Hugo
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

#include <dt-bindings/clock/qcom,gpucc-msm8998.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-alpha-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"
#include "gdsc.h"

enum {
	P_XO,
	P_GPLL0,
	P_GPUPLL0_OUT_EVEN,
};

/* Instead of going directly to the block, XO is routed through this branch */
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

static struct pll_vco fabia_vco[] = {
	{ 249600000, 2000000000, 0 },
	{ 125000000, 1000000000, 1 },
};

static const struct clk_div_table post_div_table_fabia_even[] = {
	{ 0x0, 1 },
	{ 0x1, 2 },
	{ 0x3, 4 },
	{ 0x7, 8 },
	{ }
};

static struct clk_alpha_pll gpupll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.vco_table = fabia_vco,
	.num_vco = ARRAY_SIZE(fabia_vco),
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpupll0",
		.parent_hws = (const struct clk_hw *[]){ &gpucc_cxo_clk.clkr.hw },
		.num_parents = 1,
		.ops = &clk_alpha_pll_fabia_ops,
	},
};

static struct clk_alpha_pll_postdiv gpupll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_fabia_even,
	.num_post_div = ARRAY_SIZE(post_div_table_fabia_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpupll0_out_even",
		.parent_hws = (const struct clk_hw *[]){ &gpupll0.clkr.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_fabia_ops,
	},
};

static const struct parent_map gpu_xo_gpll0_map[] = {
	{ P_XO, 0 },
	{ P_GPLL0, 5 },
};

static const struct clk_parent_data gpu_xo_gpll0[] = {
	{ .hw = &gpucc_cxo_clk.clkr.hw },
	{ .fw_name = "gpll0" },
};

static const struct parent_map gpu_xo_gpupll0_map[] = {
	{ P_XO, 0 },
	{ P_GPUPLL0_OUT_EVEN, 1 },
};

static const struct clk_hw *gpu_xo_gpupll0[] = {
	&gpucc_cxo_clk.clkr.hw,
	&gpupll0_out_even.clkr.hw,
};

static const struct freq_tbl ftbl_rbcpr_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0, 12, 0, 0),
	{ }
};

static struct clk_rcg2 rbcpr_clk_src = {
	.cmd_rcgr = 0x1030,
	.hid_width = 5,
	.parent_map = gpu_xo_gpll0_map,
	.freq_tbl = ftbl_rbcpr_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "rbcpr_clk_src",
		.parent_data = gpu_xo_gpll0,
		.num_parents = ARRAY_SIZE(gpu_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gfx3d_clk_src[] = {
	{ .src = P_GPUPLL0_OUT_EVEN, .pre_div = 3 },
	{ }
};

static struct clk_rcg2 gfx3d_clk_src = {
	.cmd_rcgr = 0x1070,
	.hid_width = 5,
	.parent_map = gpu_xo_gpupll0_map,
	.freq_tbl = ftbl_gfx3d_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gfx3d_clk_src",
		.parent_hws = gpu_xo_gpupll0,
		.num_parents = ARRAY_SIZE(gpu_xo_gpupll0),
		.ops = &clk_rcg2_ops,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
	},
};

static const struct freq_tbl ftbl_rbbmtimer_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 rbbmtimer_clk_src = {
	.cmd_rcgr = 0x10b0,
	.hid_width = 5,
	.parent_map = gpu_xo_gpll0_map,
	.freq_tbl = ftbl_rbbmtimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "rbbmtimer_clk_src",
		.parent_data = gpu_xo_gpll0,
		.num_parents = ARRAY_SIZE(gpu_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_gfx3d_isense_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(40000000, P_GPLL0, 15, 0, 0),
	F(200000000, P_GPLL0, 3, 0, 0),
	F(300000000, P_GPLL0, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gfx3d_isense_clk_src = {
	.cmd_rcgr = 0x1100,
	.hid_width = 5,
	.parent_map = gpu_xo_gpll0_map,
	.freq_tbl = ftbl_gfx3d_isense_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gfx3d_isense_clk_src",
		.parent_data = gpu_xo_gpll0,
		.num_parents = ARRAY_SIZE(gpu_xo_gpll0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch rbcpr_clk = {
	.halt_reg = 0x1054,
	.clkr = {
		.enable_reg = 0x1054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "rbcpr_clk",
			.parent_hws = (const struct clk_hw *[]){ &rbcpr_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gfx3d_clk = {
	.halt_reg = 0x1098,
	.clkr = {
		.enable_reg = 0x1098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gfx3d_clk",
			.parent_hws = (const struct clk_hw *[]){ &gfx3d_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch rbbmtimer_clk = {
	.halt_reg = 0x10d0,
	.clkr = {
		.enable_reg = 0x10d0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "rbbmtimer_clk",
			.parent_hws = (const struct clk_hw *[]){ &rbbmtimer_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gfx3d_isense_clk = {
	.halt_reg = 0x1124,
	.clkr = {
		.enable_reg = 0x1124,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gfx3d_isense_clk",
			.parent_hws = (const struct clk_hw *[]){ &gfx3d_isense_clk_src.clkr.hw },
			.num_parents = 1,
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
	.pwrsts = PWRSTS_OFF_ON | PWRSTS_RET,
	.flags = CLAMP_IO | SW_RESET | AON_RESET | NO_RET_PERIPH,
};

static struct clk_regmap *gpucc_msm8998_clocks[] = {
	[GPUPLL0] = &gpupll0.clkr,
	[GPUPLL0_OUT_EVEN] = &gpupll0_out_even.clkr,
	[RBCPR_CLK_SRC] = &rbcpr_clk_src.clkr,
	[GFX3D_CLK_SRC] = &gfx3d_clk_src.clkr,
	[RBBMTIMER_CLK_SRC] = &rbbmtimer_clk_src.clkr,
	[GFX3D_ISENSE_CLK_SRC] = &gfx3d_isense_clk_src.clkr,
	[RBCPR_CLK] = &rbcpr_clk.clkr,
	[GFX3D_CLK] = &gfx3d_clk.clkr,
	[RBBMTIMER_CLK] = &rbbmtimer_clk.clkr,
	[GFX3D_ISENSE_CLK] = &gfx3d_isense_clk.clkr,
	[GPUCC_CXO_CLK] = &gpucc_cxo_clk.clkr,
};

static struct gdsc *gpucc_msm8998_gdscs[] = {
	[GPU_CX_GDSC] = &gpu_cx_gdsc,
	[GPU_GX_GDSC] = &gpu_gx_gdsc,
};

static const struct qcom_reset_map gpucc_msm8998_resets[] = {
	[GPU_CX_BCR] = { 0x1000 },
	[RBCPR_BCR] = { 0x1050 },
	[GPU_GX_BCR] = { 0x1090 },
	[GPU_ISENSE_BCR] = { 0x1120 },
};

static const struct regmap_config gpucc_msm8998_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x9000,
	.fast_io	= true,
};

static const struct qcom_cc_desc gpucc_msm8998_desc = {
	.config = &gpucc_msm8998_regmap_config,
	.clks = gpucc_msm8998_clocks,
	.num_clks = ARRAY_SIZE(gpucc_msm8998_clocks),
	.resets = gpucc_msm8998_resets,
	.num_resets = ARRAY_SIZE(gpucc_msm8998_resets),
	.gdscs = gpucc_msm8998_gdscs,
	.num_gdscs = ARRAY_SIZE(gpucc_msm8998_gdscs),
};

static const struct of_device_id gpucc_msm8998_match_table[] = {
	{ .compatible = "qcom,msm8998-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpucc_msm8998_match_table);

static int gpucc_msm8998_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &gpucc_msm8998_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* force periph logic on to avoid perf counter corruption */
	regmap_write_bits(regmap, gfx3d_clk.clkr.enable_reg, BIT(13), BIT(13));
	/* tweak droop detector (GPUCC_GPU_DD_WRAP_CTRL) to reduce leakage */
	regmap_write_bits(regmap, gfx3d_clk.clkr.enable_reg, BIT(0), BIT(0));

	return qcom_cc_really_probe(pdev, &gpucc_msm8998_desc, regmap);
}

static struct platform_driver gpucc_msm8998_driver = {
	.probe		= gpucc_msm8998_probe,
	.driver		= {
		.name	= "gpucc-msm8998",
		.of_match_table = gpucc_msm8998_match_table,
	},
};
module_platform_driver(gpucc_msm8998_driver);

MODULE_DESCRIPTION("QCOM GPUCC MSM8998 Driver");
MODULE_LICENSE("GPL v2");
