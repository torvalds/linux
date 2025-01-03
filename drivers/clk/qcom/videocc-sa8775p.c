// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sa8775p-videocc.h>

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
	DT_IFACE,
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

static const struct pll_vco lucid_evo_vco[] = {
	{ 249600000, 2020000000, 0 },
};

static const struct alpha_pll_config video_pll0_config = {
	.l = 0x39,
	.alpha = 0x3000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32aa299c,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00400805,
};

static struct clk_alpha_pll video_pll0 = {
	.offset = 0x0,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

static const struct alpha_pll_config video_pll1_config = {
	.l = 0x39,
	.alpha = 0x3000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32aa299c,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00400805,
};

static struct clk_alpha_pll video_pll1 = {
	.offset = 0x1000,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_pll1",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

static const struct parent_map video_cc_parent_map_0_ao[] = {
	{ P_BI_TCXO_AO, 0 },
};

static const struct clk_parent_data video_cc_parent_data_0_ao[] = {
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

static const struct parent_map video_cc_parent_map_3[] = {
	{ P_SLEEP_CLK, 0 },
};

static const struct clk_parent_data video_cc_parent_data_3[] = {
	{ .index = DT_SLEEP_CLK },
};

static const struct freq_tbl ftbl_video_cc_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO_AO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_ahb_clk_src = {
	.cmd_rcgr = 0x8030,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0_ao,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_ahb_clk_src",
		.parent_data = video_cc_parent_data_0_ao,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0_ao),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
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
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0_clk_src",
		.parent_data = video_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
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
	.cmd_rcgr = 0x812c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_3,
	.freq_tbl = ftbl_video_cc_sleep_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_sleep_clk_src",
		.parent_data = video_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 video_cc_xo_clk_src = {
	.cmd_rcgr = 0x8110,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0_ao,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_xo_clk_src",
		.parent_data = video_cc_parent_data_0_ao,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0_ao),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div video_cc_mvs0_div_clk_src = {
	.reg = 0x80b8,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_mvs0_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
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
		.parent_hws = (const struct clk_hw*[]) {
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
		.parent_hws = (const struct clk_hw*[]) {
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
		.parent_hws = (const struct clk_hw*[]) {
			&video_cc_mvs1_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div video_cc_sm_div_clk_src = {
	.reg = 0x8108,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_sm_div_clk_src",
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
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0c_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs1_clk",
			.parent_hws = (const struct clk_hw*[]) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs1c_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs1c_div2_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_pll_lock_monitor_clk = {
	.halt_reg = 0x9000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll_lock_monitor_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_sm_obs_clk = {
	.halt_reg = 0x810c,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x810c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_sm_obs_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_sm_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc video_cc_mvs0c_gdsc = {
	.gdscr = 0x804c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "video_cc_mvs0c_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE | POLL_CFG_GDSCR,
};

static struct gdsc video_cc_mvs0_gdsc = {
	.gdscr = 0x809c,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "video_cc_mvs0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &video_cc_mvs0c_gdsc.pd,
	.flags = RETAIN_FF_ENABLE | POLL_CFG_GDSCR | HW_CTRL_TRIGGER,
};

static struct gdsc video_cc_mvs1c_gdsc = {
	.gdscr = 0x8074,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "video_cc_mvs1c_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE | POLL_CFG_GDSCR,
};

static struct gdsc video_cc_mvs1_gdsc = {
	.gdscr = 0x80c0,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "video_cc_mvs1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &video_cc_mvs1c_gdsc.pd,
	.flags = RETAIN_FF_ENABLE | POLL_CFG_GDSCR | HW_CTRL_TRIGGER,
};

static struct clk_regmap *video_cc_sa8775p_clocks[] = {
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
	[VIDEO_CC_PLL_LOCK_MONITOR_CLK] = &video_cc_pll_lock_monitor_clk.clkr,
	[VIDEO_CC_SLEEP_CLK_SRC] = &video_cc_sleep_clk_src.clkr,
	[VIDEO_CC_SM_DIV_CLK_SRC] = &video_cc_sm_div_clk_src.clkr,
	[VIDEO_CC_SM_OBS_CLK] = &video_cc_sm_obs_clk.clkr,
	[VIDEO_CC_XO_CLK_SRC] = &video_cc_xo_clk_src.clkr,
	[VIDEO_PLL0] = &video_pll0.clkr,
	[VIDEO_PLL1] = &video_pll1.clkr,
};

static struct gdsc *video_cc_sa8775p_gdscs[] = {
	[VIDEO_CC_MVS0_GDSC] = &video_cc_mvs0_gdsc,
	[VIDEO_CC_MVS0C_GDSC] = &video_cc_mvs0c_gdsc,
	[VIDEO_CC_MVS1_GDSC] = &video_cc_mvs1_gdsc,
	[VIDEO_CC_MVS1C_GDSC] = &video_cc_mvs1c_gdsc,
};

static const struct qcom_reset_map video_cc_sa8775p_resets[] = {
	[VIDEO_CC_INTERFACE_BCR] = { 0x80e8 },
	[VIDEO_CC_MVS0_BCR] = { 0x8098 },
	[VIDEO_CC_MVS0C_CLK_ARES] = { 0x8064, 2 },
	[VIDEO_CC_MVS0C_BCR] = { 0x8048 },
	[VIDEO_CC_MVS1_BCR] = { 0x80bc },
	[VIDEO_CC_MVS1C_CLK_ARES] = { 0x808c, 2 },
	[VIDEO_CC_MVS1C_BCR] = { 0x8070 },
};

static const struct regmap_config video_cc_sa8775p_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xb000,
	.fast_io = true,
};

static struct qcom_cc_desc video_cc_sa8775p_desc = {
	.config = &video_cc_sa8775p_regmap_config,
	.clks = video_cc_sa8775p_clocks,
	.num_clks = ARRAY_SIZE(video_cc_sa8775p_clocks),
	.resets = video_cc_sa8775p_resets,
	.num_resets = ARRAY_SIZE(video_cc_sa8775p_resets),
	.gdscs = video_cc_sa8775p_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_sa8775p_gdscs),
};

static const struct of_device_id video_cc_sa8775p_match_table[] = {
	{ .compatible = "qcom,sa8775p-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_sa8775p_match_table);

static int video_cc_sa8775p_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		return ret;

	regmap = qcom_cc_map(pdev, &video_cc_sa8775p_desc);
	if (IS_ERR(regmap)) {
		pm_runtime_put(&pdev->dev);
		return PTR_ERR(regmap);
	}

	clk_lucid_evo_pll_configure(&video_pll0, regmap, &video_pll0_config);
	clk_lucid_evo_pll_configure(&video_pll1, regmap, &video_pll1_config);

	/* Keep some clocks always enabled */
	qcom_branch_set_clk_en(regmap, 0x80ec); /* VIDEO_CC_AHB_CLK */
	qcom_branch_set_clk_en(regmap, 0x8144); /* VIDEO_CC_SLEEP_CLK */
	qcom_branch_set_clk_en(regmap, 0x8128); /* VIDEO_CC_XO_CLK */

	ret = qcom_cc_really_probe(&pdev->dev, &video_cc_sa8775p_desc, regmap);

	pm_runtime_put(&pdev->dev);

	return ret;
}

static struct platform_driver video_cc_sa8775p_driver = {
	.probe = video_cc_sa8775p_probe,
	.driver = {
		.name = "videocc-sa8775p",
		.of_match_table = video_cc_sa8775p_match_table,
	},
};

module_platform_driver(video_cc_sa8775p_driver);

MODULE_DESCRIPTION("QTI VIDEOCC SA8775P Driver");
MODULE_LICENSE("GPL");
