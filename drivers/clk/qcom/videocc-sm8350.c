// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sm8350-videocc.h>
#include <dt-bindings/reset/qcom,sm8350-videocc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "reset.h"
#include "gdsc.h"

enum {
	DT_BI_TCXO,
	DT_BI_TCXO_AO,
	DT_SLEEP_CLK,
};

enum {
	P_BI_TCXO,
	P_BI_TCXO_AO,
	P_SLEEP_CLK,
	P_VIDEO_PLL0_OUT_MAIN,
	P_VIDEO_PLL1_OUT_MAIN,
};

static const struct pll_vco lucid_5lpe_vco[] = {
	{ 249600000, 1750000000, 0 },
};

static const struct pll_vco lucid_5lpe_vco_8280xp[] = {
	{ 249600000, 1800000000, 0 },
};

static const struct alpha_pll_config video_pll0_config = {
	.l = 0x25,
	.alpha = 0x8000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002261,
	.config_ctl_hi1_val = 0x2a9a699c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000000,
	.test_ctl_hi1_val = 0x01800000,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x00000000,
};

static struct clk_alpha_pll video_pll0 = {
	.offset = 0x42c,
	.vco_table = lucid_5lpe_vco,
	.num_vco = ARRAY_SIZE(lucid_5lpe_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_pll0",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_5lpe_ops,
		},
	},
};

static const struct alpha_pll_config video_pll1_config = {
	.l = 0x2b,
	.alpha = 0xc000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002261,
	.config_ctl_hi1_val = 0x2a9a699c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000000,
	.test_ctl_hi1_val = 0x01800000,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x00000000,
};

static struct clk_alpha_pll video_pll1 = {
	.offset = 0x7d0,
	.vco_table = lucid_5lpe_vco,
	.num_vco = ARRAY_SIZE(lucid_5lpe_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_pll1",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_5lpe_ops,
		},
	},
};

static const struct parent_map video_cc_parent_map_0[] = {
	{ P_BI_TCXO_AO, 0 },
};

static const struct clk_parent_data video_cc_parent_data_0[] = {
	{ .index = DT_BI_TCXO_AO },
};

static const struct parent_map video_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_PLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &video_pll0.clkr.hw },
};

static const struct parent_map video_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_PLL1_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &video_pll1.clkr.hw },
};

static const struct freq_tbl ftbl_video_cc_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_ahb_clk_src = {
	.cmd_rcgr = 0xbd4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_ahb_clk_src",
		.parent_data = video_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_mvs0_clk_src[] = {
	F(720000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(1014000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(1098000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(1332000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_video_cc_mvs0_clk_src_8280xp[] = {
	F(720000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(1014000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(1098000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(1332000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(1599000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(1680000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs0_clk_src = {
	.cmd_rcgr = 0xb94,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_1,
	.freq_tbl = ftbl_video_cc_mvs0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0_clk_src",
		.parent_data = video_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_mvs1_clk_src[] = {
	F(840000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	F(1098000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	F(1332000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_video_cc_mvs1_clk_src_8280xp[] = {
	F(840000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	F(1098000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	F(1332000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	F(1600000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	F(1800000000, P_VIDEO_PLL1_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_mvs1_clk_src = {
	.cmd_rcgr = 0xbb4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_2,
	.freq_tbl = ftbl_video_cc_mvs1_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs1_clk_src",
		.parent_data = video_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_sleep_clk_src = {
	.cmd_rcgr = 0xef0,
	.mnd_width = 0,
	.hid_width = 5,
	.freq_tbl = ftbl_video_cc_sleep_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_sleep_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.index = DT_SLEEP_CLK,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 video_cc_xo_clk_src = {
	.cmd_rcgr = 0xecc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_xo_clk_src",
		.parent_data = video_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div video_cc_mvs0_div_clk_src = {
	.reg = 0xd54,
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
	.reg = 0xc54,
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
	.reg = 0xdd4,
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
	.reg = 0xcf4,
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
	.halt_reg = 0xd34,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xd34,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xd34,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
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
	.halt_reg = 0xc34,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xc34,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
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
	.halt_reg = 0xdb4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xdb4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xdb4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
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

static struct clk_branch video_cc_mvs1_div2_clk = {
	.halt_reg = 0xdf4,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0xdf4,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0xdf4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs1_div2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_mvs1c_div2_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs1c_clk = {
	.halt_reg = 0xcd4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xcd4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
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
	.halt_reg = 0xf10,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xf10,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
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

static struct gdsc mvs0c_gdsc = {
	.gdscr = 0xbf8,
	.pd = {
		.name = "mvs0c_gdsc",
	},
	.flags = RETAIN_FF_ENABLE,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc mvs1c_gdsc = {
	.gdscr = 0xc98,
	.pd = {
		.name = "mvs1c_gdsc",
	},
	.flags = RETAIN_FF_ENABLE,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc mvs0_gdsc = {
	.gdscr = 0xd18,
	.pd = {
		.name = "mvs0_gdsc",
	},
	.flags = HW_CTRL | RETAIN_FF_ENABLE,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc mvs1_gdsc = {
	.gdscr = 0xd98,
	.pd = {
		.name = "mvs1_gdsc",
	},
	.flags = HW_CTRL | RETAIN_FF_ENABLE,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct clk_regmap *video_cc_sm8350_clocks[] = {
	[VIDEO_CC_AHB_CLK_SRC] = &video_cc_ahb_clk_src.clkr,
	[VIDEO_CC_MVS0_CLK] = &video_cc_mvs0_clk.clkr,
	[VIDEO_CC_MVS0_CLK_SRC] = &video_cc_mvs0_clk_src.clkr,
	[VIDEO_CC_MVS0_DIV_CLK_SRC] = &video_cc_mvs0_div_clk_src.clkr,
	[VIDEO_CC_MVS0C_CLK] = &video_cc_mvs0c_clk.clkr,
	[VIDEO_CC_MVS0C_DIV2_DIV_CLK_SRC] = &video_cc_mvs0c_div2_div_clk_src.clkr,
	[VIDEO_CC_MVS1_CLK] = &video_cc_mvs1_clk.clkr,
	[VIDEO_CC_MVS1_CLK_SRC] = &video_cc_mvs1_clk_src.clkr,
	[VIDEO_CC_MVS1_DIV2_CLK] = &video_cc_mvs1_div2_clk.clkr,
	[VIDEO_CC_MVS1_DIV_CLK_SRC] = &video_cc_mvs1_div_clk_src.clkr,
	[VIDEO_CC_MVS1C_CLK] = &video_cc_mvs1c_clk.clkr,
	[VIDEO_CC_MVS1C_DIV2_DIV_CLK_SRC] = &video_cc_mvs1c_div2_div_clk_src.clkr,
	[VIDEO_CC_SLEEP_CLK] = &video_cc_sleep_clk.clkr,
	[VIDEO_CC_SLEEP_CLK_SRC] = &video_cc_sleep_clk_src.clkr,
	[VIDEO_CC_XO_CLK_SRC] = &video_cc_xo_clk_src.clkr,
	[VIDEO_PLL0] = &video_pll0.clkr,
	[VIDEO_PLL1] = &video_pll1.clkr,
};

static const struct qcom_reset_map video_cc_sm8350_resets[] = {
	[VIDEO_CC_CVP_INTERFACE_BCR] = { 0xe54 },
	[VIDEO_CC_CVP_MVS0_BCR] = { 0xd14 },
	[VIDEO_CC_MVS0C_CLK_ARES] = { 0xc34, 2 },
	[VIDEO_CC_CVP_MVS0C_BCR] = { 0xbf4 },
	[VIDEO_CC_CVP_MVS1_BCR] = { 0xd94 },
	[VIDEO_CC_MVS1C_CLK_ARES] = { 0xcd4, 2 },
	[VIDEO_CC_CVP_MVS1C_BCR] = { 0xc94 },
};

static struct gdsc *video_cc_sm8350_gdscs[] = {
	[MVS0C_GDSC] = &mvs0c_gdsc,
	[MVS1C_GDSC] = &mvs1c_gdsc,
	[MVS0_GDSC] = &mvs0_gdsc,
	[MVS1_GDSC] = &mvs1_gdsc,
};

static const struct regmap_config video_cc_sm8350_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x10000,
	.fast_io = true,
};

static struct qcom_cc_desc video_cc_sm8350_desc = {
	.config = &video_cc_sm8350_regmap_config,
	.clks = video_cc_sm8350_clocks,
	.num_clks = ARRAY_SIZE(video_cc_sm8350_clocks),
	.resets = video_cc_sm8350_resets,
	.num_resets = ARRAY_SIZE(video_cc_sm8350_resets),
	.gdscs = video_cc_sm8350_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_sm8350_gdscs),
};

static int video_cc_sm8350_probe(struct platform_device *pdev)
{
	u32 video_cc_xo_clk_cbcr = 0xeec;
	struct regmap *regmap;
	int ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		return ret;

	if (of_device_is_compatible(pdev->dev.of_node, "qcom,sc8280xp-videocc")) {
		video_cc_sleep_clk_src.cmd_rcgr = 0xf38;
		video_cc_sleep_clk.halt_reg = 0xf58;
		video_cc_sleep_clk.clkr.enable_reg = 0xf58;
		video_cc_xo_clk_src.cmd_rcgr = 0xf14;
		video_cc_xo_clk_cbcr = 0xf34;

		video_pll0.vco_table = video_pll1.vco_table = lucid_5lpe_vco_8280xp;
		/* No change, but assign it for completeness */
		video_pll0.num_vco = video_pll1.num_vco = ARRAY_SIZE(lucid_5lpe_vco_8280xp);

		video_cc_mvs0_clk_src.freq_tbl = ftbl_video_cc_mvs0_clk_src_8280xp;
		video_cc_mvs1_clk_src.freq_tbl = ftbl_video_cc_mvs1_clk_src_8280xp;
	}

	regmap = qcom_cc_map(pdev, &video_cc_sm8350_desc);
	if (IS_ERR(regmap)) {
		pm_runtime_put(&pdev->dev);
		return PTR_ERR(regmap);
	}

	clk_lucid_pll_configure(&video_pll0, regmap, &video_pll0_config);
	clk_lucid_pll_configure(&video_pll1, regmap, &video_pll1_config);

	/*
	 * Keep clocks always enabled:
	 *      video_cc_ahb_clk
	 *      video_cc_xo_clk
	 */
	regmap_update_bits(regmap, 0xe58, BIT(0), BIT(0));
	regmap_update_bits(regmap, video_cc_xo_clk_cbcr, BIT(0), BIT(0));

	ret = qcom_cc_really_probe(pdev, &video_cc_sm8350_desc, regmap);
	pm_runtime_put(&pdev->dev);

	return ret;
}

static const struct of_device_id video_cc_sm8350_match_table[] = {
	{ .compatible = "qcom,sc8280xp-videocc" },
	{ .compatible = "qcom,sm8350-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_sm8350_match_table);

static struct platform_driver video_cc_sm8350_driver = {
	.probe = video_cc_sm8350_probe,
	.driver = {
		.name = "sm8350-videocc",
		.of_match_table = video_cc_sm8350_match_table,
	},
};
module_platform_driver(video_cc_sm8350_driver);

MODULE_DESCRIPTION("QTI SM8350 VIDEOCC Driver");
MODULE_LICENSE("GPL");
