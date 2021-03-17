// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,videocc-sdm845.h>

#include "common.h"
#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "gdsc.h"

enum {
	P_BI_TCXO,
	P_CORE_BI_PLL_TEST_SE,
	P_VIDEO_PLL0_OUT_EVEN,
	P_VIDEO_PLL0_OUT_MAIN,
	P_VIDEO_PLL0_OUT_ODD,
};

static const struct parent_map video_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_PLL0_OUT_MAIN, 1 },
	{ P_VIDEO_PLL0_OUT_EVEN, 2 },
	{ P_VIDEO_PLL0_OUT_ODD, 3 },
	{ P_CORE_BI_PLL_TEST_SE, 4 },
};

static const char * const video_cc_parent_names_0[] = {
	"bi_tcxo",
	"video_pll0",
	"video_pll0_out_even",
	"video_pll0_out_odd",
	"core_bi_pll_test_se",
};

static const struct alpha_pll_config video_pll0_config = {
	.l = 0x10,
	.alpha = 0xaaab,
};

static struct clk_alpha_pll video_pll0 = {
	.offset = 0x42c,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "video_pll0",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_fabia_ops,
		},
	},
};

static const struct freq_tbl ftbl_video_cc_venus_clk_src[] = {
	F(100000000, P_VIDEO_PLL0_OUT_MAIN, 4, 0, 0),
	F(200000000, P_VIDEO_PLL0_OUT_MAIN, 2, 0, 0),
	F(330000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(404000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(444000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	F(533000000, P_VIDEO_PLL0_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_venus_clk_src = {
	.cmd_rcgr = 0x7f0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_venus_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_venus_clk_src",
		.parent_names = video_cc_parent_names_0,
		.num_parents = 5,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_branch video_cc_apb_clk = {
	.halt_reg = 0x990,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x990,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_apb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_at_clk = {
	.halt_reg = 0x9f0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9f0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_at_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_qdss_trig_clk = {
	.halt_reg = 0x970,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x970,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_qdss_trig_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_qdss_tsctr_div8_clk = {
	.halt_reg = 0x9d0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9d0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_qdss_tsctr_div8_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_vcodec0_axi_clk = {
	.halt_reg = 0x930,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x930,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_vcodec0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_vcodec0_core_clk = {
	.halt_reg = 0x890,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x890,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_vcodec0_core_clk",
			.parent_names = (const char *[]){
				"video_cc_venus_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_vcodec1_axi_clk = {
	.halt_reg = 0x950,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x950,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_vcodec1_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_vcodec1_core_clk = {
	.halt_reg = 0x8d0,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x8d0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_vcodec1_core_clk",
			.parent_names = (const char *[]){
				"video_cc_venus_clk_src",
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
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_venus_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_venus_ctl_axi_clk = {
	.halt_reg = 0x910,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x910,
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
			.parent_names = (const char *[]){
				"video_cc_venus_clk_src",
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
	.cxcs = (unsigned int []){ 0x850, 0x910 },
	.cxc_count = 2,
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc vcodec0_gdsc = {
	.gdscr = 0x874,
	.pd = {
		.name = "vcodec0_gdsc",
	},
	.cxcs = (unsigned int []){ 0x890, 0x930 },
	.cxc_count = 2,
	.flags = HW_CTRL | POLL_CFG_GDSCR,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc vcodec1_gdsc = {
	.gdscr = 0x8b4,
	.pd = {
		.name = "vcodec1_gdsc",
	},
	.cxcs = (unsigned int []){ 0x8d0, 0x950 },
	.cxc_count = 2,
	.flags = HW_CTRL | POLL_CFG_GDSCR,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct clk_regmap *video_cc_sdm845_clocks[] = {
	[VIDEO_CC_APB_CLK] = &video_cc_apb_clk.clkr,
	[VIDEO_CC_AT_CLK] = &video_cc_at_clk.clkr,
	[VIDEO_CC_QDSS_TRIG_CLK] = &video_cc_qdss_trig_clk.clkr,
	[VIDEO_CC_QDSS_TSCTR_DIV8_CLK] = &video_cc_qdss_tsctr_div8_clk.clkr,
	[VIDEO_CC_VCODEC0_AXI_CLK] = &video_cc_vcodec0_axi_clk.clkr,
	[VIDEO_CC_VCODEC0_CORE_CLK] = &video_cc_vcodec0_core_clk.clkr,
	[VIDEO_CC_VCODEC1_AXI_CLK] = &video_cc_vcodec1_axi_clk.clkr,
	[VIDEO_CC_VCODEC1_CORE_CLK] = &video_cc_vcodec1_core_clk.clkr,
	[VIDEO_CC_VENUS_AHB_CLK] = &video_cc_venus_ahb_clk.clkr,
	[VIDEO_CC_VENUS_CLK_SRC] = &video_cc_venus_clk_src.clkr,
	[VIDEO_CC_VENUS_CTL_AXI_CLK] = &video_cc_venus_ctl_axi_clk.clkr,
	[VIDEO_CC_VENUS_CTL_CORE_CLK] = &video_cc_venus_ctl_core_clk.clkr,
	[VIDEO_PLL0] = &video_pll0.clkr,
};

static struct gdsc *video_cc_sdm845_gdscs[] = {
	[VENUS_GDSC] = &venus_gdsc,
	[VCODEC0_GDSC] = &vcodec0_gdsc,
	[VCODEC1_GDSC] = &vcodec1_gdsc,
};

static const struct regmap_config video_cc_sdm845_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xb90,
	.fast_io	= true,
};

static const struct qcom_cc_desc video_cc_sdm845_desc = {
	.config = &video_cc_sdm845_regmap_config,
	.clks = video_cc_sdm845_clocks,
	.num_clks = ARRAY_SIZE(video_cc_sdm845_clocks),
	.gdscs = video_cc_sdm845_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_sdm845_gdscs),
};

static const struct of_device_id video_cc_sdm845_match_table[] = {
	{ .compatible = "qcom,sdm845-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_sdm845_match_table);

static int video_cc_sdm845_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &video_cc_sdm845_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_fabia_pll_configure(&video_pll0, regmap, &video_pll0_config);

	return qcom_cc_really_probe(pdev, &video_cc_sdm845_desc, regmap);
}

static struct platform_driver video_cc_sdm845_driver = {
	.probe		= video_cc_sdm845_probe,
	.driver		= {
		.name	= "sdm845-videocc",
		.of_match_table = video_cc_sdm845_match_table,
		.sync_state = clk_sync_state,
	},
};

static int __init video_cc_sdm845_init(void)
{
	return platform_driver_register(&video_cc_sdm845_driver);
}
subsys_initcall(video_cc_sdm845_init);

static void __exit video_cc_sdm845_exit(void)
{
	platform_driver_unregister(&video_cc_sdm845_driver);
}
module_exit(video_cc_sdm845_exit);

MODULE_LICENSE("GPL v2");
