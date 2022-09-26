// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,videocc-sm8150.h>

#include "common.h"
#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "reset.h"
#include "gdsc.h"
#include "vdd-level-sm8150.h"
#include "clk-pm.h"

static DEFINE_VDD_REGULATORS(vdd_mm, VDD_HIGH + 1, 1, vdd_corner);

static struct clk_vdd_class *video_cc_sm8150_regulators[] = {
	&vdd_mm,
};

enum {
	P_BI_TCXO,
	P_VIDEO_PLL0_OUT_MAIN,
};

static struct pll_vco trion_vco[] = {
	{ 249600000, 2000000000, 0 },
};

/* 400 MHz configuration */
static struct alpha_pll_config video_pll0_config = {
	.l = 0x14,
	.alpha = 0xD555,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000000,
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
	.config = &video_pll0_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "video_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_trion_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mm,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
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
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_iris_clk_src",
		.parent_data = video_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 200000000,
			[VDD_LOWER] = 240000000,
			[VDD_LOW] = 338000000,
			[VDD_LOW_L1] = 365000000,
			[VDD_NOMINAL] = 444000000,
			[VDD_HIGH] = 533000000},
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
	.flags = 0,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc vcodec1_gdsc = {
	.gdscr = 0x8b4,
	.pd = {
		.name = "vcodec1_gdsc",
	},
	.flags = 0,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct critical_clk_offset critical_clk_list[] = {
	{ .offset = 0x984, .mask = BIT(1) },
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
	[VIDEO_CC_INTERFACE_BCR] = { 0x8f0 },
	[VIDEO_CC_MVS0_BCR] = { 0x870 },
	[VIDEO_CC_MVS1_BCR] = { 0x8b0 },
	[VIDEO_CC_MVSC_BCR] = { 0x810 },
	[VIDEO_CC_MVSC_CORE_CLK_BCR] = { 0x850, 2 },
};

static struct qcom_cc_desc video_cc_sm8150_desc = {
	.config = &video_cc_sm8150_regmap_config,
	.clks = video_cc_sm8150_clocks,
	.num_clks = ARRAY_SIZE(video_cc_sm8150_clocks),
	.resets = video_cc_sm8150_resets,
	.num_resets = ARRAY_SIZE(video_cc_sm8150_resets),
	.clk_regulators = video_cc_sm8150_regulators,
	.num_clk_regulators = ARRAY_SIZE(video_cc_sm8150_regulators),
	.gdscs = video_cc_sm8150_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_sm8150_gdscs),
	.critical_clk_en = critical_clk_list,
	.num_critical_clk = ARRAY_SIZE(critical_clk_list),
};

static const struct of_device_id video_cc_sm8150_match_table[] = {
	{ .compatible = "qcom,sm8150-videocc" },
	{ .compatible = "qcom,sa8155-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_sm8150_match_table);

static int video_cc_sm8150_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &video_cc_sm8150_desc);
	if (IS_ERR(regmap)) {
		pr_err("Failed to map the Video CC registers\n");
		return PTR_ERR(regmap);
	}

	clk_trion_pll_configure(&video_pll0, regmap, video_pll0.config);

	/* Keep VIDEO_CC_XO_CLK ALWAYS-ON */
	regmap_update_bits(regmap, 0x984, 0x1, 0x1);

	ret = qcom_cc_really_probe(pdev, &video_cc_sm8150_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register VIDEO CC clocks\n");
		return ret;
	}

	ret = register_qcom_clks_pm(pdev, false, &video_cc_sm8150_desc);
	if (ret)
		dev_err(&pdev->dev, "Failed to register for pm ops\n");

	dev_info(&pdev->dev, "Registered VIDEO CC clocks\n");

	return 0;
}

static void video_cc_sm8150_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &video_cc_sm8150_desc);
}

static struct platform_driver video_cc_sm8150_driver = {
	.probe = video_cc_sm8150_probe,
	.driver = {
		.name	= "video_cc-sm8150",
		.of_match_table = video_cc_sm8150_match_table,
		.sync_state = video_cc_sm8150_sync_state,
	},
};

static int __init video_cc_sm8150_init(void)
{
	return platform_driver_register(&video_cc_sm8150_driver);
}
subsys_initcall(video_cc_sm8150_init);

static void __exit video_cc_sm8150_exit(void)
{
	platform_driver_unregister(&video_cc_sm8150_driver);
}
module_exit(video_cc_sm8150_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QTI VIDEOCC SM8150 Driver");
