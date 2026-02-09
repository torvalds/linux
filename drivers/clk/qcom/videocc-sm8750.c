// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sm8750-videocc.h>

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
	DT_BI_TCXO_AO,
	DT_SLEEP_CLK,
};

enum {
	P_BI_TCXO,
	P_SLEEP_CLK,
	P_VIDEO_CC_PLL0_OUT_MAIN,
};

static const struct pll_vco taycan_elu_vco[] = {
	{ 249600000, 2500000000, 0 },
};

static const struct alpha_pll_config video_cc_pll0_config = {
	.l = 0x25,
	.alpha = 0x8000,
	.config_ctl_val = 0x19660387,
	.config_ctl_hi_val = 0x098060a0,
	.config_ctl_hi1_val = 0xb416cb20,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000002,
};

static struct clk_alpha_pll video_cc_pll0 = {
	.offset = 0x0,
	.config = &video_cc_pll0_config,
	.vco_table = taycan_elu_vco,
	.num_vco = ARRAY_SIZE(taycan_elu_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TAYCAN_ELU],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_taycan_elu_ops,
		},
	},
};

static const struct parent_map video_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data video_cc_parent_data_0_ao[] = {
	{ .index = DT_BI_TCXO_AO },
};

static const struct parent_map video_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_CC_PLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data video_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &video_cc_pll0.clkr.hw },
};

static const struct parent_map video_cc_parent_map_2[] = {
	{ P_SLEEP_CLK, 0 },
};

static const struct clk_parent_data video_cc_parent_data_2_ao[] = {
	{ .index = DT_SLEEP_CLK },
};

static const struct freq_tbl ftbl_video_cc_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_ahb_clk_src = {
	.cmd_rcgr = 0x8018,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_ahb_clk_src",
		.parent_data = video_cc_parent_data_0_ao,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0_ao),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_mvs0_clk_src[] = {
	F(720000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1014000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1260000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1332000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1600000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1710000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
	F(1890000000, P_VIDEO_CC_PLL0_OUT_MAIN, 1, 0, 0),
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

static const struct freq_tbl ftbl_video_cc_sleep_clk_src[] = {
	F(32000, P_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_sleep_clk_src = {
	.cmd_rcgr = 0x80e0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_2,
	.freq_tbl = ftbl_video_cc_sleep_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_sleep_clk_src",
		.parent_data = video_cc_parent_data_2_ao,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_2_ao),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 video_cc_xo_clk_src = {
	.cmd_rcgr = 0x80bc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_ahb_clk_src,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "video_cc_xo_clk_src",
		.parent_data = video_cc_parent_data_0_ao,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0_ao),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div video_cc_mvs0_div_clk_src = {
	.reg = 0x809c,
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
	.reg = 0x8060,
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

static struct clk_branch video_cc_mvs0_clk = {
	.halt_reg = 0x807c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x807c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x807c,
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

static struct clk_mem_branch video_cc_mvs0_freerun_clk = {
	.mem_enable_reg = 0x8090,
	.mem_ack_reg =  0x8090,
	.mem_enable_mask = BIT(3),
	.mem_enable_ack_mask = GENMASK(11, 10),
	.mem_enable_invert = true,
	.branch = {
		.halt_reg = 0x808c,
		.halt_check = BRANCH_HALT,
		.clkr = {
			.enable_reg = 0x808c,
			.enable_mask = BIT(0),
			.hw.init = &(const struct clk_init_data) {
				.name = "video_cc_mvs0_freerun_clk",
				.parent_hws = (const struct clk_hw*[]) {
					&video_cc_mvs0_div_clk_src.clkr.hw,
				},
				.num_parents = 1,
				.flags = CLK_SET_RATE_PARENT,
				.ops = &clk_branch2_mem_ops,
			},
		},
	},
};

static struct clk_branch video_cc_mvs0_shift_clk = {
	.halt_reg = 0x80d8,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80d8,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80d8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0_shift_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0c_clk = {
	.halt_reg = 0x804c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x804c,
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

static struct clk_branch video_cc_mvs0c_freerun_clk = {
	.halt_reg = 0x805c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x805c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0c_freerun_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_mvs0c_div2_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0c_shift_clk = {
	.halt_reg = 0x80dc,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x80dc,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x80dc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "video_cc_mvs0c_shift_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&video_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc video_cc_mvs0c_gdsc = {
	.gdscr = 0x8034,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "video_cc_mvs0c_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc video_cc_mvs0_gdsc = {
	.gdscr = 0x8068,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0x6,
	.pd = {
		.name = "video_cc_mvs0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.parent = &video_cc_mvs0c_gdsc.pd,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE | HW_CTRL_TRIGGER,
};

static struct clk_regmap *video_cc_sm8750_clocks[] = {
	[VIDEO_CC_AHB_CLK_SRC] = &video_cc_ahb_clk_src.clkr,
	[VIDEO_CC_MVS0_CLK] = &video_cc_mvs0_clk.clkr,
	[VIDEO_CC_MVS0_CLK_SRC] = &video_cc_mvs0_clk_src.clkr,
	[VIDEO_CC_MVS0_DIV_CLK_SRC] = &video_cc_mvs0_div_clk_src.clkr,
	[VIDEO_CC_MVS0_FREERUN_CLK] = &video_cc_mvs0_freerun_clk.branch.clkr,
	[VIDEO_CC_MVS0_SHIFT_CLK] = &video_cc_mvs0_shift_clk.clkr,
	[VIDEO_CC_MVS0C_CLK] = &video_cc_mvs0c_clk.clkr,
	[VIDEO_CC_MVS0C_DIV2_DIV_CLK_SRC] = &video_cc_mvs0c_div2_div_clk_src.clkr,
	[VIDEO_CC_MVS0C_FREERUN_CLK] = &video_cc_mvs0c_freerun_clk.clkr,
	[VIDEO_CC_MVS0C_SHIFT_CLK] = &video_cc_mvs0c_shift_clk.clkr,
	[VIDEO_CC_PLL0] = &video_cc_pll0.clkr,
	[VIDEO_CC_SLEEP_CLK_SRC] = &video_cc_sleep_clk_src.clkr,
	[VIDEO_CC_XO_CLK_SRC] = &video_cc_xo_clk_src.clkr,
};

static struct gdsc *video_cc_sm8750_gdscs[] = {
	[VIDEO_CC_MVS0_GDSC] = &video_cc_mvs0_gdsc,
	[VIDEO_CC_MVS0C_GDSC] = &video_cc_mvs0c_gdsc,
};

static const struct qcom_reset_map video_cc_sm8750_resets[] = {
	[VIDEO_CC_INTERFACE_BCR] = { 0x80a0 },
	[VIDEO_CC_MVS0_BCR] = { 0x8064 },
	[VIDEO_CC_MVS0C_CLK_ARES] = { 0x804c, 2 },
	[VIDEO_CC_MVS0C_BCR] = { 0x8030 },
	[VIDEO_CC_MVS0_FREERUN_CLK_ARES] = { 0x808c, 2 },
	[VIDEO_CC_MVS0C_FREERUN_CLK_ARES] = { 0x805c, 2 },
	[VIDEO_CC_XO_CLK_ARES] = { 0x80d4, 2 },
};

static const struct regmap_config video_cc_sm8750_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x9f4c,
	.fast_io = true,
};

static struct clk_alpha_pll *video_cc_sm8750_plls[] = {
	&video_cc_pll0,
};

static u32 video_cc_sm8750_critical_cbcrs[] = {
	0x80a4, /* VIDEO_CC_AHB_CLK */
	0x80f8, /* VIDEO_CC_SLEEP_CLK */
	0x80d4, /* VIDEO_CC_XO_CLK */
};

static void clk_sm8750_regs_configure(struct device *dev, struct regmap *regmap)
{
	/* Update DLY_ACCU_RED_SHIFTER_DONE to 0xF for mvs0, mvs0c */
	regmap_update_bits(regmap, 0x8074, GENMASK(25, 21), GENMASK(25, 21));
	regmap_update_bits(regmap, 0x8040, GENMASK(25, 21), GENMASK(25, 21));

	regmap_update_bits(regmap, 0x9f24, BIT(0), BIT(0));
}

static struct qcom_cc_driver_data video_cc_sm8750_driver_data = {
	.alpha_plls = video_cc_sm8750_plls,
	.num_alpha_plls = ARRAY_SIZE(video_cc_sm8750_plls),
	.clk_cbcrs = video_cc_sm8750_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(video_cc_sm8750_critical_cbcrs),
	.clk_regs_configure = clk_sm8750_regs_configure,
};

static struct qcom_cc_desc video_cc_sm8750_desc = {
	.config = &video_cc_sm8750_regmap_config,
	.clks = video_cc_sm8750_clocks,
	.num_clks = ARRAY_SIZE(video_cc_sm8750_clocks),
	.resets = video_cc_sm8750_resets,
	.num_resets = ARRAY_SIZE(video_cc_sm8750_resets),
	.gdscs = video_cc_sm8750_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_sm8750_gdscs),
	.use_rpm = true,
	.driver_data = &video_cc_sm8750_driver_data,
};

static const struct of_device_id video_cc_sm8750_match_table[] = {
	{ .compatible = "qcom,sm8750-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_sm8750_match_table);

static int video_cc_sm8750_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &video_cc_sm8750_desc);
}

static struct platform_driver video_cc_sm8750_driver = {
	.probe = video_cc_sm8750_probe,
	.driver = {
		.name = "video_cc-sm8750",
		.of_match_table = video_cc_sm8750_match_table,
	},
};

static int __init video_cc_sm8750_init(void)
{
	return platform_driver_register(&video_cc_sm8750_driver);
}
subsys_initcall(video_cc_sm8750_init);

static void __exit video_cc_sm8750_exit(void)
{
	platform_driver_unregister(&video_cc_sm8750_driver);
}
module_exit(video_cc_sm8750_exit);

MODULE_DESCRIPTION("QTI VIDEO_CC SM8750 Driver");
MODULE_LICENSE("GPL");
