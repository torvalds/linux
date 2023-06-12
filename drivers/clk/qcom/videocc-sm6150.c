// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,videocc-sm6150.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pm.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "vdd-level-sm6150.h"

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_NUM, 1, vdd_corner);

enum {
	P_BI_TCXO,
	P_SLEEP_CLK,
	P_VIDEO_PLL0_OUT_AUX,
	P_VIDEO_PLL0_OUT_AUX2,
	P_VIDEO_PLL0_OUT_MAIN,
};

static struct pll_vco spark_vco[] = {
	{ 500000000, 1000000000, 2 },
};

/* 600MHz configuration */
static struct alpha_pll_config video_pll0_config = {
	.l = 0x1F,
	.alpha_hi = 0x40,
	.alpha = 0x00,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = 0x3 << 20,
	.main_output_mask = BIT(0),
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
};

static struct clk_init_data video_pll0_sa6155 = {
	.name = "video_pll0",
	.parent_data = &(const struct clk_parent_data){
		.fw_name = "bi_tcxo",
	},
	.num_parents = 1,
	.ops = &clk_alpha_pll_slew_ops,
};

static struct clk_alpha_pll video_pll0 = {
	.offset = 0x42c,
	.vco_table = spark_vco,
	.num_vco = ARRAY_SIZE(spark_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.config = &video_pll0_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "video_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 1100000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

static const struct parent_map video_cc_parent_map_0[] = {
	{ P_SLEEP_CLK, 0 },
};

static const struct clk_parent_data video_cc_parent_data_0[] = {
	{ .fw_name = "sleep_clk"},
};

static const struct parent_map video_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_PLL0_OUT_MAIN, 1 },
	{ P_VIDEO_PLL0_OUT_AUX, 2 },
	{ P_VIDEO_PLL0_OUT_AUX2, 3 },
};

static const struct clk_parent_data video_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo"},
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
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_sleep_clk_src",
		.parent_data = video_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 32000},
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
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_venus_clk_src",
		.parent_data = video_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 133333333,
			[VDD_LOW] = 240000000,
			[VDD_LOW_L1] = 300000000,
			[VDD_NOMINAL] = 380000000,
			[VDD_NOMINAL_L1] = 410000000,
			[VDD_HIGH] = 460000000},
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
		.hw.init = &(struct clk_init_data){
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
	.halt_reg = 0x8d0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8d0,
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

static struct clk_regmap *video_cc_sm6150_clocks[] = {
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

static const struct regmap_config video_cc_sm6150_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xb94,
	.fast_io = true,
};

static struct critical_clk_offset critical_clk_list[] = {
	{ .offset = 0xab8, .mask = BIT(0) },
};

static struct qcom_cc_desc video_cc_sm6150_desc = {
	.config = &video_cc_sm6150_regmap_config,
	.clks = video_cc_sm6150_clocks,
	.num_clks = ARRAY_SIZE(video_cc_sm6150_clocks),
	.critical_clk_en = critical_clk_list,
	.num_critical_clk = ARRAY_SIZE(critical_clk_list),
};

static const struct of_device_id video_cc_sm6150_match_table[] = {
	{ .compatible = "qcom,sm6150-videocc" },
	{ .compatible = "qcom,sa6155-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_sm6150_match_table);

static void videocc_sm6150_fixup_sa6155(struct platform_device *pdev)
{
	vdd_cx.num_levels = VDD_NUM_SA6155;
	vdd_cx.cur_level = VDD_NUM_SA6155;

	video_pll0.clkr.hw.init = &video_pll0_sa6155;
}

static int video_cc_sm6150_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret, is_sa6155;

	vdd_cx.regulator[0] = devm_regulator_get(&pdev->dev, "vdd_cx");
	if (IS_ERR(vdd_cx.regulator[0])) {
		if (!(PTR_ERR(vdd_cx.regulator[0]) == -EPROBE_DEFER))
			dev_err(&pdev->dev,
				"Unable to get vdd_cx regulator\n");
		return PTR_ERR(vdd_cx.regulator[0]);
	}

	is_sa6155 = of_device_is_compatible(pdev->dev.of_node,
						"qcom,sa6155-videocc");
	if (is_sa6155)
		videocc_sm6150_fixup_sa6155(pdev);

	regmap = qcom_cc_map(pdev, &video_cc_sm6150_desc);
	if (IS_ERR(regmap)) {
		pr_err("Failed to map the video_cc registers\n");
		return PTR_ERR(regmap);
	}

	clk_alpha_pll_configure(&video_pll0, regmap, video_pll0.config);

	/*
	 * Keep clocks always enabled:
	 *	video_cc_xo_clk
	 */
	regmap_update_bits(regmap, 0xab8, BIT(0), BIT(0));

	ret = qcom_cc_really_probe(pdev, &video_cc_sm6150_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register VIDEO CC clocks\n");
		return ret;
	}

	ret = register_qcom_clks_pm(pdev, false, &video_cc_sm6150_desc);
	if (ret)
		dev_err(&pdev->dev, "VIDEO CC failed to register for pm ops\n");

	dev_info(&pdev->dev, "Registered VIDEO CC clocks\n");

	return ret;
}

static void video_cc_sm6150_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &video_cc_sm6150_desc);
}

static struct platform_driver video_cc_sm6150_driver = {
	.probe = video_cc_sm6150_probe,
	.driver = {
		.name = "video_cc-sm6150",
		.of_match_table = video_cc_sm6150_match_table,
		.sync_state = video_cc_sm6150_sync_state,
	},
};

static int __init video_cc_sm6150_init(void)
{
	return platform_driver_register(&video_cc_sm6150_driver);
}
subsys_initcall(video_cc_sm6150_init);

static void __exit video_cc_sm6150_exit(void)
{
	platform_driver_unregister(&video_cc_sm6150_driver);
}
module_exit(video_cc_sm6150_exit);

MODULE_DESCRIPTION("QTI VIDEO_CC SM6150 Driver");
MODULE_LICENSE("GPL");
