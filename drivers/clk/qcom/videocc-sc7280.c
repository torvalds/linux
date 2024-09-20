// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,videocc-sc7280.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "common.h"
#include "reset.h"
#include "gdsc.h"

enum {
	P_BI_TCXO,
	P_SLEEP_CLK,
	P_VIDEO_PLL0_OUT_EVEN,
};

static const struct pll_vco lucid_vco[] = {
	{ 249600000, 2000000000, 0 },
};

/* 400MHz Configuration */
static const struct alpha_pll_config video_pll0_config = {
	.l = 0x14,
	.alpha = 0xD555,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002261,
	.config_ctl_hi1_val = 0x329A299C,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x00000000,
};

static struct clk_alpha_pll video_pll0 = {
	.offset = 0x0,
	.vco_table = lucid_vco,
	.num_vco = ARRAY_SIZE(lucid_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "video_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_ops,
		},
	},
};

static const struct parent_map video_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_PLL0_OUT_EVEN, 3 },
};

static const struct clk_parent_data video_cc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &video_pll0.clkr.hw },
};

static const struct parent_map video_cc_parent_map_1[] = {
	{ P_SLEEP_CLK, 0 },
};

static const struct clk_parent_data video_cc_parent_data_1[] = {
	{ .fw_name = "sleep_clk" },
};

static const struct freq_tbl ftbl_video_cc_iris_clk_src[] = {
	F(133333333, P_VIDEO_PLL0_OUT_EVEN, 3, 0, 0),
	F(240000000, P_VIDEO_PLL0_OUT_EVEN, 2, 0, 0),
	F(335000000, P_VIDEO_PLL0_OUT_EVEN, 2, 0, 0),
	F(424000000, P_VIDEO_PLL0_OUT_EVEN, 2, 0, 0),
	F(460000000, P_VIDEO_PLL0_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_iris_clk_src = {
	.cmd_rcgr = 0x1000,
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

static const struct freq_tbl ftbl_video_cc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_sleep_clk_src = {
	.cmd_rcgr = 0x701c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_1,
	.freq_tbl = ftbl_video_cc_sleep_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_sleep_clk_src",
		.parent_data = video_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch video_cc_iris_ahb_clk = {
	.halt_reg = 0x5004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5004,
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

static struct clk_branch video_cc_mvs0_axi_clk = {
	.halt_reg = 0x800c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x800c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvs0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0_core_clk = {
	.halt_reg = 0x3010,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x3010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3010,
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

static struct clk_branch video_cc_mvsc_core_clk = {
	.halt_reg = 0x2014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2014,
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

static struct clk_branch video_cc_mvsc_ctl_axi_clk = {
	.halt_reg = 0x8004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvsc_ctl_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_sleep_clk = {
	.halt_reg = 0x7034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x7034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_sleep_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_sleep_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_venus_ahb_clk = {
	.halt_reg = 0x801c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x801c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_venus_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc mvs0_gdsc = {
	.gdscr = 0x3004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "mvs0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = HW_CTRL_TRIGGER | RETAIN_FF_ENABLE,
};

static struct gdsc mvsc_gdsc = {
	.gdscr = 0x2004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "mvsc_gdsc",
	},
	.flags = RETAIN_FF_ENABLE,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct clk_regmap *video_cc_sc7280_clocks[] = {
	[VIDEO_CC_IRIS_AHB_CLK] = &video_cc_iris_ahb_clk.clkr,
	[VIDEO_CC_IRIS_CLK_SRC] = &video_cc_iris_clk_src.clkr,
	[VIDEO_CC_MVS0_AXI_CLK] = &video_cc_mvs0_axi_clk.clkr,
	[VIDEO_CC_MVS0_CORE_CLK] = &video_cc_mvs0_core_clk.clkr,
	[VIDEO_CC_MVSC_CORE_CLK] = &video_cc_mvsc_core_clk.clkr,
	[VIDEO_CC_MVSC_CTL_AXI_CLK] = &video_cc_mvsc_ctl_axi_clk.clkr,
	[VIDEO_CC_SLEEP_CLK] = &video_cc_sleep_clk.clkr,
	[VIDEO_CC_SLEEP_CLK_SRC] = &video_cc_sleep_clk_src.clkr,
	[VIDEO_CC_VENUS_AHB_CLK] = &video_cc_venus_ahb_clk.clkr,
	[VIDEO_PLL0] = &video_pll0.clkr,
};

static struct gdsc *video_cc_sc7280_gdscs[] = {
	[MVS0_GDSC] = &mvs0_gdsc,
	[MVSC_GDSC] = &mvsc_gdsc,
};

static const struct regmap_config video_cc_sc7280_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xb000,
	.fast_io = true,
};

static const struct qcom_cc_desc video_cc_sc7280_desc = {
	.config = &video_cc_sc7280_regmap_config,
	.clks = video_cc_sc7280_clocks,
	.num_clks = ARRAY_SIZE(video_cc_sc7280_clocks),
	.gdscs = video_cc_sc7280_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_sc7280_gdscs),
};

static const struct of_device_id video_cc_sc7280_match_table[] = {
	{ .compatible = "qcom,sc7280-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_sc7280_match_table);

static int video_cc_sc7280_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &video_cc_sc7280_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_lucid_pll_configure(&video_pll0, regmap, &video_pll0_config);

	return qcom_cc_really_probe(&pdev->dev, &video_cc_sc7280_desc, regmap);
}

static struct platform_driver video_cc_sc7280_driver = {
	.probe = video_cc_sc7280_probe,
	.driver = {
		.name = "video_cc-sc7280",
		.of_match_table = video_cc_sc7280_match_table,
	},
};

module_platform_driver(video_cc_sc7280_driver);

MODULE_DESCRIPTION("QTI VIDEO_CC sc7280 Driver");
MODULE_LICENSE("GPL v2");
