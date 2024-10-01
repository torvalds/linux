// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,videocc-sm8150.h>

#include "common.h"
#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "reset.h"
#include "gdsc.h"

enum {
	P_BI_TCXO,
	P_VIDEO_PLL0_OUT_MAIN,
};

static const struct pll_vco trion_vco[] = {
	{ 249600000, 2000000000, 0 },
};

static struct alpha_pll_config video_pll0_config = {
	.l = 0x14,
	.alpha = 0xD555,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.test_ctl_hi1_val = 0x00000020,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x000000D0,
};

static struct clk_alpha_pll video_pll0 = {
	.offset = 0x42c,
	.vco_table = trion_vco,
	.num_vco = ARRAY_SIZE(trion_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "video_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_trion_ops,
		},
	},
};

static const struct parent_map video_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_PLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &video_pll0.clkr.hw },
};

static const struct freq_tbl ftbl_video_cc_iris_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(200000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	F(240000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	F(338000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	F(365000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	F(444000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	F(533000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_iris_clk_src = {
	.cmd_rcgr = 0x7f0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_iris_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_iris_clk_src",
		.parent_data = video_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_branch video_cc_iris_ahb_clk = {
	.halt_reg = 0x8f4,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x8f4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_iris_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_iris_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0_core_clk = {
	.halt_reg = 0x890,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x890,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvs0_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_iris_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs1_core_clk = {
	.halt_reg = 0x8d0,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x8d0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvs1_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_iris_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvsc_core_clk = {
	.halt_reg = 0x850,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x850,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvsc_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_iris_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc venus_gdsc = {
	.gdscr = 0x814,
	.pd = {
		.name = "venus_gdsc",
	},
	.flags = 0,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc vcodec0_gdsc = {
	.gdscr = 0x874,
	.pd = {
		.name = "vcodec0_gdsc",
	},
	.flags = HW_CTRL,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc vcodec1_gdsc = {
	.gdscr = 0x8b4,
	.pd = {
		.name = "vcodec1_gdsc",
	},
	.flags = HW_CTRL,
	.pwrsts = PWRSTS_OFF_ON,
};
static struct clk_regmap *video_cc_sm8150_clocks[] = {
	[VIDEO_CC_IRIS_AHB_CLK] = &video_cc_iris_ahb_clk.clkr,
	[VIDEO_CC_IRIS_CLK_SRC] = &video_cc_iris_clk_src.clkr,
	[VIDEO_CC_MVS0_CORE_CLK] = &video_cc_mvs0_core_clk.clkr,
	[VIDEO_CC_MVS1_CORE_CLK] = &video_cc_mvs1_core_clk.clkr,
	[VIDEO_CC_MVSC_CORE_CLK] = &video_cc_mvsc_core_clk.clkr,
	[VIDEO_CC_PLL0] = &video_pll0.clkr,
};

static struct gdsc *video_cc_sm8150_gdscs[] = {
	[VENUS_GDSC] = &venus_gdsc,
	[VCODEC0_GDSC] = &vcodec0_gdsc,
	[VCODEC1_GDSC] = &vcodec1_gdsc,
};

static const struct regmap_config video_cc_sm8150_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xb94,
	.fast_io	= true,
};

static const struct qcom_reset_map video_cc_sm8150_resets[] = {
	[VIDEO_CC_MVSC_CORE_CLK_BCR] = { .reg = 0x850, .bit = 2, .udelay = 150 },
	[VIDEO_CC_INTERFACE_BCR] = { 0x8f0 },
	[VIDEO_CC_MVS0_BCR] = { 0x870 },
	[VIDEO_CC_MVS1_BCR] = { 0x8b0 },
	[VIDEO_CC_MVSC_BCR] = { 0x810 },
};

static const struct qcom_cc_desc video_cc_sm8150_desc = {
	.config = &video_cc_sm8150_regmap_config,
	.clks = video_cc_sm8150_clocks,
	.num_clks = ARRAY_SIZE(video_cc_sm8150_clocks),
	.resets = video_cc_sm8150_resets,
	.num_resets = ARRAY_SIZE(video_cc_sm8150_resets),
	.gdscs = video_cc_sm8150_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_sm8150_gdscs),
};

static const struct of_device_id video_cc_sm8150_match_table[] = {
	{ .compatible = "qcom,sm8150-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_sm8150_match_table);

static int video_cc_sm8150_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		return ret;

	regmap = qcom_cc_map(pdev, &video_cc_sm8150_desc);
	if (IS_ERR(regmap)) {
		pm_runtime_put_sync(&pdev->dev);
		return PTR_ERR(regmap);
	}

	clk_trion_pll_configure(&video_pll0, regmap, &video_pll0_config);

	/* Keep VIDEO_CC_XO_CLK ALWAYS-ON */
	regmap_update_bits(regmap, 0x984, 0x1, 0x1);

	ret = qcom_cc_really_probe(&pdev->dev, &video_cc_sm8150_desc, regmap);

	pm_runtime_put_sync(&pdev->dev);

	return ret;
}

static struct platform_driver video_cc_sm8150_driver = {
	.probe = video_cc_sm8150_probe,
	.driver = {
		.name	= "video_cc-sm8150",
		.of_match_table = video_cc_sm8150_match_table,
	},
};

module_platform_driver(video_cc_sm8150_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QTI VIDEOCC SM8150 Driver");
