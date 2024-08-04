// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Danila Tikhonov <danila@jiaxyga.com>
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sm7150-videocc.h>

#include "common.h"
#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "gdsc.h"

enum {
	DT_BI_TCXO,
	DT_BI_TCXO_AO,
};

enum {
	P_BI_TCXO,
	P_VIDEOCC_PLL0_OUT_EVEN,
	P_VIDEOCC_PLL0_OUT_MAIN,
	P_VIDEOCC_PLL0_OUT_ODD,
};

static const struct pll_vco fabia_vco[] = {
	{ 249600000, 2000000000, 0 },
	{ 125000000, 1000000000, 1 },
};

static struct alpha_pll_config videocc_pll0_config = {
	.l = 0x19,
	.alpha = 0x0,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002067,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00004805,
	.test_ctl_hi_val = 0x40000000,
};

static struct clk_alpha_pll videocc_pll0 = {
	.offset = 0x42c,
	.vco_table = fabia_vco,
	.num_vco = ARRAY_SIZE(fabia_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "videocc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fabia_ops,
		},
	},
};

static const struct parent_map videocc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEOCC_PLL0_OUT_MAIN, 1 },
	{ P_VIDEOCC_PLL0_OUT_EVEN, 2 },
	{ P_VIDEOCC_PLL0_OUT_ODD, 3 },
};

static const struct clk_parent_data videocc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &videocc_pll0.clkr.hw },
	{ .hw = &videocc_pll0.clkr.hw },
	{ .hw = &videocc_pll0.clkr.hw },
};

static const struct parent_map videocc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data videocc_parent_data_1[] = {
	{ .index = DT_BI_TCXO_AO },
};

static const struct freq_tbl ftbl_videocc_iris_clk_src[] = {
	F(240000000, P_VIDEOCC_PLL0_OUT_MAIN, 2, 0, 0),
	F(338000000, P_VIDEOCC_PLL0_OUT_MAIN, 2, 0, 0),
	F(365000000, P_VIDEOCC_PLL0_OUT_MAIN, 2, 0, 0),
	F(444000000, P_VIDEOCC_PLL0_OUT_MAIN, 2, 0, 0),
	F(533000000, P_VIDEOCC_PLL0_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 videocc_iris_clk_src = {
	.cmd_rcgr = 0x7f0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = videocc_parent_map_0,
	.freq_tbl = ftbl_videocc_iris_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "videocc_iris_clk_src",
		.parent_data = videocc_parent_data_0,
		.num_parents = ARRAY_SIZE(videocc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_videocc_xo_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 videocc_xo_clk_src = {
	.cmd_rcgr = 0xa98,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = videocc_parent_map_1,
	.freq_tbl = ftbl_videocc_xo_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "videocc_xo_clk_src",
		.parent_data = videocc_parent_data_1,
		.num_parents = ARRAY_SIZE(videocc_parent_data_1),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch videocc_iris_ahb_clk = {
	.halt_reg = 0x8f4,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x8f4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "videocc_iris_ahb_clk",
			.parent_data = &(const struct clk_parent_data) {
				.hw = &videocc_iris_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch videocc_mvs0_axi_clk = {
	.halt_reg = 0x9ec,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9ec,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "videocc_mvs0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch videocc_mvs0_core_clk = {
	.halt_reg = 0x890,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x890,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "videocc_mvs0_core_clk",
			.parent_data = &(const struct clk_parent_data) {
				.hw = &videocc_iris_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch videocc_mvs1_axi_clk = {
	.halt_reg = 0xa0c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa0c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "videocc_mvs1_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch videocc_mvs1_core_clk = {
	.halt_reg = 0x8d0,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x8d0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "videocc_mvs1_core_clk",
			.parent_data = &(const struct clk_parent_data) {
				.hw = &videocc_iris_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch videocc_mvsc_core_clk = {
	.halt_reg = 0x850,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x850,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "videocc_mvsc_core_clk",
			.parent_data = &(const struct clk_parent_data) {
				.hw = &videocc_iris_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch videocc_mvsc_ctl_axi_clk = {
	.halt_reg = 0x9cc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9cc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "videocc_mvsc_ctl_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch videocc_venus_ahb_clk = {
	.halt_reg = 0xa6c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa6c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "videocc_venus_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc venus_gdsc = {
	.gdscr = 0x814,
	.pd = {
		.name = "venus_gdsc",
	},
	.cxcs = (unsigned int []){ 0x850, 0x9cc },
	.cxc_count = 2,
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR,
};

static struct gdsc vcodec0_gdsc = {
	.gdscr = 0x874,
	.pd = {
		.name = "vcodec0_gdsc",
	},
	.cxcs = (unsigned int []){ 0x890, 0x9ec },
	.cxc_count = 2,
	.flags = HW_CTRL | POLL_CFG_GDSCR,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc vcodec1_gdsc = {
	.gdscr = 0x8b4,
	.pd = {
		.name = "vcodec1_gdsc",
	},
	.cxcs = (unsigned int []){ 0x8d0, 0xa0c },
	.cxc_count = 2,
	.flags = HW_CTRL | POLL_CFG_GDSCR,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct clk_regmap *videocc_sm7150_clocks[] = {
	[VIDEOCC_PLL0] = &videocc_pll0.clkr,
	[VIDEOCC_IRIS_AHB_CLK] = &videocc_iris_ahb_clk.clkr,
	[VIDEOCC_IRIS_CLK_SRC] = &videocc_iris_clk_src.clkr,
	[VIDEOCC_MVS0_AXI_CLK] = &videocc_mvs0_axi_clk.clkr,
	[VIDEOCC_MVS0_CORE_CLK] = &videocc_mvs0_core_clk.clkr,
	[VIDEOCC_MVS1_AXI_CLK] = &videocc_mvs1_axi_clk.clkr,
	[VIDEOCC_MVS1_CORE_CLK] = &videocc_mvs1_core_clk.clkr,
	[VIDEOCC_MVSC_CORE_CLK] = &videocc_mvsc_core_clk.clkr,
	[VIDEOCC_MVSC_CTL_AXI_CLK] = &videocc_mvsc_ctl_axi_clk.clkr,
	[VIDEOCC_VENUS_AHB_CLK] = &videocc_venus_ahb_clk.clkr,
	[VIDEOCC_XO_CLK_SRC] = &videocc_xo_clk_src.clkr,
};

static struct gdsc *videocc_sm7150_gdscs[] = {
	[VENUS_GDSC] = &venus_gdsc,
	[VCODEC0_GDSC] = &vcodec0_gdsc,
	[VCODEC1_GDSC] = &vcodec1_gdsc,
};

static const struct regmap_config videocc_sm7150_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xb94,
	.fast_io	= true,
};

static const struct qcom_cc_desc videocc_sm7150_desc = {
	.config = &videocc_sm7150_regmap_config,
	.clks = videocc_sm7150_clocks,
	.num_clks = ARRAY_SIZE(videocc_sm7150_clocks),
	.gdscs = videocc_sm7150_gdscs,
	.num_gdscs = ARRAY_SIZE(videocc_sm7150_gdscs),
};

static const struct of_device_id videocc_sm7150_match_table[] = {
	{ .compatible = "qcom,sm7150-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, videocc_sm7150_match_table);

static int videocc_sm7150_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &videocc_sm7150_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_fabia_pll_configure(&videocc_pll0, regmap, &videocc_pll0_config);

	/* Keep some clocks always-on */
	qcom_branch_set_clk_en(regmap, 0x984); /* VIDEOCC_XO_CLK */

	return qcom_cc_really_probe(&pdev->dev, &videocc_sm7150_desc, regmap);
}

static struct platform_driver videocc_sm7150_driver = {
	.probe = videocc_sm7150_probe,
	.driver = {
		.name = "videocc-sm7150",
		.of_match_table = videocc_sm7150_match_table,
	},
};
module_platform_driver(videocc_sm7150_driver);

MODULE_DESCRIPTION("Qualcomm SM7150 Video Clock Controller");
MODULE_LICENSE("GPL");
