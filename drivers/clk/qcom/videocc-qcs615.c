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

#include <dt-bindings/clock/qcom,qcs615-videocc.h>

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
	DT_SLEEP_CLK,
};

enum {
	P_BI_TCXO,
	P_SLEEP_CLK,
	P_VIDEO_PLL0_OUT_AUX,
	P_VIDEO_PLL0_OUT_AUX2,
	P_VIDEO_PLL0_OUT_MAIN,
};

static const struct pll_vco video_cc_pll0_vco[] = {
	{ 500000000, 1000000000, 2 },
};

/* 600MHz configuration VCO - 2 */
static struct alpha_pll_config video_pll0_config = {
	.l = 0x1f,
	.alpha_hi = 0x40,
	.alpha = 0x00,
	.alpha_en_mask = BIT(24),
	.vco_val = BIT(21),
	.vco_mask = GENMASK(21, 20),
	.main_output_mask = BIT(0),
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
};

static struct clk_alpha_pll video_pll0 = {
	.offset = 0x42c,
	.config = &video_pll0_config,
	.vco_table = video_cc_pll0_vco,
	.num_vco = ARRAY_SIZE(video_cc_pll0_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_slew_ops,
		},
	},
};

static const struct parent_map video_cc_parent_map_0[] = {
	{ P_SLEEP_CLK, 0 },
};

static const struct clk_parent_data video_cc_parent_data_0_ao[] = {
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map video_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_PLL0_OUT_MAIN, 1 },
	{ P_VIDEO_PLL0_OUT_AUX, 2 },
	{ P_VIDEO_PLL0_OUT_AUX2, 3 },
};

static const struct clk_parent_data video_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &video_pll0.clkr.hw },
	{ .hw = &video_pll0.clkr.hw },
	{ .hw = &video_pll0.clkr.hw },
};

static const struct freq_tbl ftbl_video_cc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_sleep_clk_src = {
	.cmd_rcgr = 0xaf8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_sleep_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_sleep_clk_src",
		.parent_data = video_cc_parent_data_0_ao,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0_ao),
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_venus_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(133333333, P_VIDEO_PLL0_OUT_MAIN, 4.5, 0, 0),
	F(240000000, P_VIDEO_PLL0_OUT_MAIN, 2.5, 0, 0),
	F(300000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	F(380000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	F(410000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	F(460000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_venus_clk_src = {
	.cmd_rcgr = 0x7f0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_1,
	.freq_tbl = ftbl_video_cc_venus_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_venus_clk_src",
		.parent_data = video_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_branch video_cc_sleep_clk = {
	.halt_reg = 0xb18,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb18,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_sleep_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &video_cc_sleep_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_vcodec0_axi_clk = {
	.halt_reg = 0x8f0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8f0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_vcodec0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_vcodec0_core_clk = {
	.halt_reg = 0x890,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x890,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_vcodec0_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_venus_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_venus_ahb_clk = {
	.halt_reg = 0x9b0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9b0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_venus_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_venus_ctl_axi_clk = {
	.halt_reg = 0x8d0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8d0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
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
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_venus_ctl_core_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_venus_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc vcodec0_gdsc = {
	.gdscr = 0x874,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "vcodec0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = HW_CTRL_TRIGGER | POLL_CFG_GDSCR,
};

static struct gdsc venus_gdsc = {
	.gdscr = 0x814,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "venus_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct clk_regmap *video_cc_qcs615_clocks[] = {
	[VIDEO_CC_SLEEP_CLK] = &video_cc_sleep_clk.clkr,
	[VIDEO_CC_SLEEP_CLK_SRC] = &video_cc_sleep_clk_src.clkr,
	[VIDEO_CC_VCODEC0_AXI_CLK] = &video_cc_vcodec0_axi_clk.clkr,
	[VIDEO_CC_VCODEC0_CORE_CLK] = &video_cc_vcodec0_core_clk.clkr,
	[VIDEO_CC_VENUS_AHB_CLK] = &video_cc_venus_ahb_clk.clkr,
	[VIDEO_CC_VENUS_CLK_SRC] = &video_cc_venus_clk_src.clkr,
	[VIDEO_CC_VENUS_CTL_AXI_CLK] = &video_cc_venus_ctl_axi_clk.clkr,
	[VIDEO_CC_VENUS_CTL_CORE_CLK] = &video_cc_venus_ctl_core_clk.clkr,
	[VIDEO_PLL0] = &video_pll0.clkr,
};

static struct gdsc *video_cc_qcs615_gdscs[] = {
	[VCODEC0_GDSC] = &vcodec0_gdsc,
	[VENUS_GDSC] = &venus_gdsc,
};

static const struct qcom_reset_map video_cc_qcs615_resets[] = {
	[VIDEO_CC_INTERFACE_BCR] = { 0x8b0 },
	[VIDEO_CC_VCODEC0_BCR] = { 0x870 },
	[VIDEO_CC_VENUS_BCR] = { 0x810 },
};

static struct clk_alpha_pll *video_cc_qcs615_plls[] = {
	&video_pll0,
};

static u32 video_cc_qcs615_critical_cbcrs[] = {
	0xab8, /* VIDEO_CC_XO_CLK */
};

static const struct regmap_config video_cc_qcs615_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xb94,
	.fast_io = true,
};

static struct qcom_cc_driver_data video_cc_qcs615_driver_data = {
	.alpha_plls = video_cc_qcs615_plls,
	.num_alpha_plls = ARRAY_SIZE(video_cc_qcs615_plls),
	.clk_cbcrs = video_cc_qcs615_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(video_cc_qcs615_critical_cbcrs),
};

static const struct qcom_cc_desc video_cc_qcs615_desc = {
	.config = &video_cc_qcs615_regmap_config,
	.clks = video_cc_qcs615_clocks,
	.num_clks = ARRAY_SIZE(video_cc_qcs615_clocks),
	.resets = video_cc_qcs615_resets,
	.num_resets = ARRAY_SIZE(video_cc_qcs615_resets),
	.gdscs = video_cc_qcs615_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_qcs615_gdscs),
	.driver_data = &video_cc_qcs615_driver_data,
};

static const struct of_device_id video_cc_qcs615_match_table[] = {
	{ .compatible = "qcom,qcs615-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_qcs615_match_table);

static int video_cc_qcs615_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &video_cc_qcs615_desc);
}

static struct platform_driver video_cc_qcs615_driver = {
	.probe = video_cc_qcs615_probe,
	.driver = {
		.name = "videocc-qcs615",
		.of_match_table = video_cc_qcs615_match_table,
	},
};

module_platform_driver(video_cc_qcs615_driver);

MODULE_DESCRIPTION("QTI VIDEOCC QCS615 Driver");
MODULE_LICENSE("GPL");
