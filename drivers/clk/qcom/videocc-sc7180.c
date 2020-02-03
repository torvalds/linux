// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,videocc-sc7180.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "gdsc.h"

enum {
	P_BI_TCXO,
	P_CHIP_SLEEP_CLK,
	P_CORE_BI_PLL_TEST_SE,
	P_VIDEO_PLL0_OUT_EVEN,
	P_VIDEO_PLL0_OUT_MAIN,
	P_VIDEO_PLL0_OUT_ODD,
};

static const struct pll_vco fabia_vco[] = {
	{ 249600000, 2000000000, 0 },
};

static struct clk_alpha_pll video_pll0 = {
	.offset = 0x42c,
	.vco_table = fabia_vco,
	.num_vco = ARRAY_SIZE(fabia_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "video_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fabia_ops,
		},
	},
};

static const struct parent_map video_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_PLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &video_pll0.clkr.hw },
};

static const struct freq_tbl ftbl_video_cc_venus_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(150000000, P_VIDEO_PLL0_OUT_MAIN, 4, 0, 0),
	F(270000000, P_VIDEO_PLL0_OUT_MAIN, 2.5, 0, 0),
	F(340000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	F(434000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	F(500000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_venus_clk_src = {
	.cmd_rcgr = 0x7f0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_1,
	.freq_tbl = ftbl_video_cc_venus_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_venus_clk_src",
		.parent_data = video_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_branch video_cc_vcodec0_axi_clk = {
	.halt_reg = 0x9ec,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9ec,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_vcodec0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_vcodec0_core_clk = {
	.halt_reg = 0x890,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x890,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_vcodec0_core_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &video_cc_venus_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_venus_ahb_clk = {
	.halt_reg = 0xa4c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa4c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_venus_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_venus_ctl_axi_clk = {
	.halt_reg = 0x9cc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9cc,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_venus_ctl_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_venus_ctl_core_clk = {
	.halt_reg = 0x850,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x850,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_venus_ctl_core_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &video_cc_venus_clk_src.clkr.hw,
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

static struct clk_regmap *video_cc_sc7180_clocks[] = {
	[VIDEO_CC_VCODEC0_AXI_CLK] = &video_cc_vcodec0_axi_clk.clkr,
	[VIDEO_CC_VCODEC0_CORE_CLK] = &video_cc_vcodec0_core_clk.clkr,
	[VIDEO_CC_VENUS_AHB_CLK] = &video_cc_venus_ahb_clk.clkr,
	[VIDEO_CC_VENUS_CLK_SRC] = &video_cc_venus_clk_src.clkr,
	[VIDEO_CC_VENUS_CTL_AXI_CLK] = &video_cc_venus_ctl_axi_clk.clkr,
	[VIDEO_CC_VENUS_CTL_CORE_CLK] = &video_cc_venus_ctl_core_clk.clkr,
	[VIDEO_PLL0] = &video_pll0.clkr,
};

static struct gdsc *video_cc_sc7180_gdscs[] = {
	[VENUS_GDSC] = &venus_gdsc,
	[VCODEC0_GDSC] = &vcodec0_gdsc,
};

static const struct regmap_config video_cc_sc7180_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xb94,
	.fast_io = true,
};

static const struct qcom_cc_desc video_cc_sc7180_desc = {
	.config = &video_cc_sc7180_regmap_config,
	.clks = video_cc_sc7180_clocks,
	.num_clks = ARRAY_SIZE(video_cc_sc7180_clocks),
	.gdscs = video_cc_sc7180_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_sc7180_gdscs),
};

static const struct of_device_id video_cc_sc7180_match_table[] = {
	{ .compatible = "qcom,sc7180-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_sc7180_match_table);

static int video_cc_sc7180_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	struct alpha_pll_config video_pll0_config = {};

	regmap = qcom_cc_map(pdev, &video_cc_sc7180_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	video_pll0_config.l = 0x1f;
	video_pll0_config.alpha = 0x4000;
	video_pll0_config.user_ctl_val = 0x00000001;
	video_pll0_config.user_ctl_hi_val = 0x00004805;

	clk_fabia_pll_configure(&video_pll0, regmap, &video_pll0_config);

	/* Keep VIDEO_CC_XO_CLK ALWAYS-ON */
	regmap_update_bits(regmap, 0x984, 0x1, 0x1);

	return qcom_cc_really_probe(pdev, &video_cc_sc7180_desc, regmap);
}

static struct platform_driver video_cc_sc7180_driver = {
	.probe = video_cc_sc7180_probe,
	.driver = {
		.name = "sc7180-videocc",
		.of_match_table = video_cc_sc7180_match_table,
	},
};

static int __init video_cc_sc7180_init(void)
{
	return platform_driver_register(&video_cc_sc7180_driver);
}
subsys_initcall(video_cc_sc7180_init);

static void __exit video_cc_sc7180_exit(void)
{
	platform_driver_unregister(&video_cc_sc7180_driver);
}
module_exit(video_cc_sc7180_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QTI VIDEOCC SC7180 Driver");
