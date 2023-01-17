// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include <dt-bindings/clock/qcom,videocc-lemans.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "reset.h"
#include "vdd-level-sm8150.h"

static DEFINE_VDD_REGULATORS(vdd_mm, VDD_HIGH_L1 + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mx, VDD_HIGH_L1 + 1, 1, vdd_corner);

static struct clk_vdd_class *video_cc_lemans_regulators[] = {
	&vdd_mm,
	&vdd_mx,
};

enum {
	P_BI_TCXO,
	P_SLEEP_CLK,
	P_VIDEO_PLL0_OUT_MAIN,
	P_VIDEO_PLL1_OUT_MAIN,
};

static const struct pll_vco lucid_evo_vco[] = {
	{ 249600000, 2020000000, 0 },
};

/* 1098MHz configuration */
static struct alpha_pll_config video_pll0_config = {
	.l = 0x39,
	.cal_l = 0x44,
	.alpha = 0x3000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32AA299C,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00400805,
};

static struct clk_alpha_pll video_pll0 = {
	.offset = 0x0,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.config = &video_pll0_config,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "video_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2020000000},
		},
	},
};

/* 1098MHz configuration */
static struct alpha_pll_config video_pll1_config = {
	.l = 0x39,
	.cal_l = 0x44,
	.alpha = 0x3000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32AA299C,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00400805,
};

static struct clk_alpha_pll video_pll1 = {
	.offset = 0x1000,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.config = &video_pll1_config,
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "video_pll1",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2020000000},
		},
	},
};

static const struct parent_map video_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data video_cc_parent_data_0_ao[] = {
	{ .fw_name = "bi_tcxo_ao" },
};

static const struct parent_map video_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_PLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &video_pll0.clkr.hw },
};

static const struct parent_map video_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_PLL1_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &video_pll1.clkr.hw },
};

static const struct parent_map video_cc_parent_map_3[] = {
	{ P_SLEEP_CLK, 0 },
};

static const struct clk_parent_data video_cc_parent_data_3[] = {
	{ .fw_name = "sleep_clk" },
};

static const struct freq_tbl ftbl_video_cc_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_ahb_clk_src = {
	.cmd_rcgr = 0x8030,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "video_cc_ahb_clk_src",
		.parent_data = video_cc_parent_data_0_ao,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0_ao),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_mvs0_clk_src[] = {
	F(1098000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(1332000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(1599000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(1680000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs0_clk_src = {
	.cmd_rcgr = 0x8000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_1,
	.freq_tbl = ftbl_video_cc_mvs0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "video_cc_mvs0_clk_src",
		.parent_data = video_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = video_cc_lemans_regulators,
		.num_vdd_classes = ARRAY_SIZE(video_cc_lemans_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 1098000000,
			[VDD_NOMINAL] = 1332000000,
			[VDD_HIGH] = 1599000000,
			[VDD_HIGH_L1] = 1680000000},
	},
};

static const struct freq_tbl ftbl_video_cc_mvs1_clk_src[] = {
	F(1098000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	F(1332000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	F(1600000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	F(1800000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs1_clk_src = {
	.cmd_rcgr = 0x8018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_2,
	.freq_tbl = ftbl_video_cc_mvs1_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "video_cc_mvs1_clk_src",
		.parent_data = video_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = video_cc_lemans_regulators,
		.num_vdd_classes = ARRAY_SIZE(video_cc_lemans_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 1098000000,
			[VDD_NOMINAL] = 1332000000,
			[VDD_HIGH] = 1600000000,
			[VDD_HIGH_L1] = 1800000000},
	},
};

static const struct freq_tbl ftbl_video_cc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_sleep_clk_src = {
	.cmd_rcgr = 0x812c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_3,
	.freq_tbl = ftbl_video_cc_sleep_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "video_cc_sleep_clk_src",
		.parent_data = video_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_3),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOW_L1] = 32000},
	},
};

static struct clk_rcg2 video_cc_xo_clk_src = {
	.cmd_rcgr = 0x8110,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "video_cc_xo_clk_src",
		.parent_data = video_cc_parent_data_0_ao,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0_ao),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div video_cc_mvs0_div_clk_src = {
	.reg = 0x80b8,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&video_cc_mvs0_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div video_cc_mvs0c_div2_div_clk_src = {
	.reg = 0x806c,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0c_div2_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&video_cc_mvs0_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div video_cc_mvs1_div_clk_src = {
	.reg = 0x80dc,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs1_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&video_cc_mvs1_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div video_cc_mvs1c_div2_div_clk_src = {
	.reg = 0x8094,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs1c_div2_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&video_cc_mvs1_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch video_cc_mvs0_clk = {
	.halt_reg = 0x80b0,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80b0,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80b0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "video_cc_mvs0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_mvs0_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0c_clk = {
	.halt_reg = 0x8064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "video_cc_mvs0c_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_mvs0c_div2_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs1_clk = {
	.halt_reg = 0x80d4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80d4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "video_cc_mvs1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_mvs1_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs1c_clk = {
	.halt_reg = 0x808c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x808c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "video_cc_mvs1c_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_mvs1c_div2_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_sleep_clk = {
	.halt_reg = 0x8144,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8144,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
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

static struct clk_regmap *video_cc_lemans_clocks[] = {
	[VIDEO_CC_AHB_CLK_SRC] = &video_cc_ahb_clk_src.clkr,
	[VIDEO_CC_MVS0_CLK] = &video_cc_mvs0_clk.clkr,
	[VIDEO_CC_MVS0_CLK_SRC] = &video_cc_mvs0_clk_src.clkr,
	[VIDEO_CC_MVS0_DIV_CLK_SRC] = &video_cc_mvs0_div_clk_src.clkr,
	[VIDEO_CC_MVS0C_CLK] = &video_cc_mvs0c_clk.clkr,
	[VIDEO_CC_MVS0C_DIV2_DIV_CLK_SRC] = &video_cc_mvs0c_div2_div_clk_src.clkr,
	[VIDEO_CC_MVS1_CLK] = &video_cc_mvs1_clk.clkr,
	[VIDEO_CC_MVS1_CLK_SRC] = &video_cc_mvs1_clk_src.clkr,
	[VIDEO_CC_MVS1_DIV_CLK_SRC] = &video_cc_mvs1_div_clk_src.clkr,
	[VIDEO_CC_MVS1C_CLK] = &video_cc_mvs1c_clk.clkr,
	[VIDEO_CC_MVS1C_DIV2_DIV_CLK_SRC] = &video_cc_mvs1c_div2_div_clk_src.clkr,
	[VIDEO_CC_SLEEP_CLK] = &video_cc_sleep_clk.clkr,
	[VIDEO_CC_SLEEP_CLK_SRC] = &video_cc_sleep_clk_src.clkr,
	[VIDEO_CC_XO_CLK_SRC] = &video_cc_xo_clk_src.clkr,
	[VIDEO_PLL0] = &video_pll0.clkr,
	[VIDEO_PLL1] = &video_pll1.clkr,
};

static const struct qcom_reset_map video_cc_lemans_resets[] = {
	[CVP_VIDEO_CC_INTERFACE_BCR] = { 0x80e8 },
	[CVP_VIDEO_CC_MVS0_BCR] = { 0x8098 },
	[VIDEO_CC_MVS0C_CLK_ARES] = { 0x8064, 2 },
	[CVP_VIDEO_CC_MVS0C_BCR] = { 0x8048 },
	[CVP_VIDEO_CC_MVS1_BCR] = { 0x80bc },
	[VIDEO_CC_MVS1C_CLK_ARES] = { 0x808c, 2 },
	[CVP_VIDEO_CC_MVS1C_BCR] = { 0x8070 },
};

static const struct regmap_config video_cc_lemans_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xb000,
	.fast_io = true,
};

static struct qcom_cc_desc video_cc_lemans_desc = {
	.config = &video_cc_lemans_regmap_config,
	.clks = video_cc_lemans_clocks,
	.num_clks = ARRAY_SIZE(video_cc_lemans_clocks),
	.resets = video_cc_lemans_resets,
	.num_resets = ARRAY_SIZE(video_cc_lemans_resets),
	.clk_regulators = video_cc_lemans_regulators,
	.num_clk_regulators = ARRAY_SIZE(video_cc_lemans_regulators),
};

static const struct of_device_id video_cc_lemans_match_table[] = {
	{ .compatible = "qcom,lemans-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_lemans_match_table);

static int video_cc_lemans_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &video_cc_lemans_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = qcom_cc_runtime_init(pdev, &video_cc_lemans_desc);
	if (ret)
		return ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret)
		return ret;

	clk_lucid_evo_pll_configure(&video_pll0, regmap, video_pll0.config);
	clk_lucid_evo_pll_configure(&video_pll1, regmap, video_pll1.config);

	/*
	 * Keep clocks always enabled:
	 *	video_cc_ahb_clk
	 *	video_cc_xo_clk
	 */
	regmap_update_bits(regmap, 0x80ec, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x8128, BIT(0), BIT(0));

	ret = qcom_cc_really_probe(pdev, &video_cc_lemans_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register VIDEO CC clocks\n");
		return ret;
	}

	pm_runtime_put_sync(&pdev->dev);
	dev_info(&pdev->dev, "Registered VIDEO CC clocks\n");

	return ret;
}

static void video_cc_lemans_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &video_cc_lemans_desc);
}

static const struct dev_pm_ops video_cc_lemans_pm_ops = {
	SET_RUNTIME_PM_OPS(qcom_cc_runtime_suspend, qcom_cc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver video_cc_lemans_driver = {
	.probe = video_cc_lemans_probe,
	.driver = {
		.name = "video_cc-lemans",
		.of_match_table = video_cc_lemans_match_table,
		.sync_state = video_cc_lemans_sync_state,
		.pm = &video_cc_lemans_pm_ops,
	},
};

static int __init video_cc_lemans_init(void)
{
	return platform_driver_register(&video_cc_lemans_driver);
}
subsys_initcall(video_cc_lemans_init);

static void __exit video_cc_lemans_exit(void)
{
	platform_driver_unregister(&video_cc_lemans_driver);
}
module_exit(video_cc_lemans_exit);

MODULE_DESCRIPTION("QTI VIDEO_CC LEMANS Driver");
MODULE_LICENSE("GPL");
