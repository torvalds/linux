// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,lpasscorecc-sc7280.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "gdsc.h"

enum {
	P_BI_TCXO,
	P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN,
	P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN_DIV_CLK_SRC,
	P_LPASS_CORE_CC_DIG_PLL_OUT_ODD,
};

static const struct pll_vco lucid_vco[] = {
	{ 249600000, 2000000000, 0 },
};

/* 614.4MHz configuration */
static const struct alpha_pll_config lpass_core_cc_dig_pll_config = {
	.l = 0x20,
	.alpha = 0x0,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002261,
	.config_ctl_hi1_val = 0xB2923BBC,
	.user_ctl_val = 0x00005100,
	.user_ctl_hi_val = 0x00050805,
	.user_ctl_hi1_val = 0x00000000,
};

static struct clk_alpha_pll lpass_core_cc_dig_pll = {
	.offset = 0x1000,
	.vco_table = lucid_vco,
	.num_vco = ARRAY_SIZE(lucid_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "lpass_core_cc_dig_pll",
			.parent_data = &(const struct clk_parent_data){
				.index = 0,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_ops,
		},
	},
};

static const struct clk_div_table post_div_table_lpass_core_cc_dig_pll_out_odd[] = {
	{ 0x5, 5 },
	{ }
};

static struct clk_alpha_pll_postdiv lpass_core_cc_dig_pll_out_odd = {
	.offset = 0x1000,
	.post_div_shift = 12,
	.post_div_table = post_div_table_lpass_core_cc_dig_pll_out_odd,
	.num_post_div = ARRAY_SIZE(post_div_table_lpass_core_cc_dig_pll_out_odd),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "lpass_core_cc_dig_pll_out_odd",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_core_cc_dig_pll.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_ops,
	},
};

static struct clk_regmap_div lpass_core_cc_dig_pll_out_main_div_clk_src = {
	.reg = 0x1054,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "lpass_core_cc_dig_pll_out_main_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_core_cc_dig_pll.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};


static const struct parent_map lpass_core_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 5 },
};

static const struct clk_parent_data lpass_core_cc_parent_data_0[] = {
	{ .index = 0 },
	{ .hw = &lpass_core_cc_dig_pll_out_odd.clkr.hw },
};

static const struct parent_map lpass_core_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN, 1 },
	{ P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN_DIV_CLK_SRC, 2 },
};

static const struct clk_parent_data lpass_core_cc_parent_data_ao_2[] = {
	{ .index = 1 },
	{ .hw = &lpass_core_cc_dig_pll.clkr.hw },
	{ .hw = &lpass_core_cc_dig_pll_out_main_div_clk_src.clkr.hw },
};

static const struct freq_tbl ftbl_lpass_core_cc_core_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(51200000, P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN_DIV_CLK_SRC, 6, 0, 0),
	F(102400000, P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN_DIV_CLK_SRC, 3, 0, 0),
	F(204800000, P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 lpass_core_cc_core_clk_src = {
	.cmd_rcgr = 0x1d000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_core_cc_parent_map_2,
	.freq_tbl = ftbl_lpass_core_cc_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_core_cc_core_clk_src",
		.parent_data = lpass_core_cc_parent_data_ao_2,
		.num_parents = ARRAY_SIZE(lpass_core_cc_parent_data_ao_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_lpass_core_cc_ext_if0_clk_src[] = {
	F(256000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 15, 1, 32),
	F(512000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 15, 1, 16),
	F(768000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 10, 1, 16),
	F(1024000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 15, 1, 8),
	F(1536000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 10, 1, 8),
	F(2048000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 15, 1, 4),
	F(3072000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 10, 1, 4),
	F(4096000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 15, 1, 2),
	F(6144000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 10, 1, 2),
	F(8192000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 15, 0, 0),
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(12288000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 10, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(24576000, P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 5, 0, 0),
	{ }
};

static struct clk_rcg2 lpass_core_cc_ext_if0_clk_src = {
	.cmd_rcgr = 0x10000,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = lpass_core_cc_parent_map_0,
	.freq_tbl = ftbl_lpass_core_cc_ext_if0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_core_cc_ext_if0_clk_src",
		.parent_data = lpass_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_core_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_core_cc_ext_if1_clk_src = {
	.cmd_rcgr = 0x11000,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = lpass_core_cc_parent_map_0,
	.freq_tbl = ftbl_lpass_core_cc_ext_if0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_core_cc_ext_if1_clk_src",
		.parent_data = lpass_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_core_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_core_cc_ext_mclk0_clk_src = {
	.cmd_rcgr = 0x20000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_core_cc_parent_map_0,
	.freq_tbl = ftbl_lpass_core_cc_ext_if0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_core_cc_ext_mclk0_clk_src",
		.parent_data = lpass_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_core_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch lpass_core_cc_core_clk = {
	.halt_reg = 0x1f000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1f000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1f000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_ext_if0_ibit_clk = {
	.halt_reg = 0x10018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_ext_if0_ibit_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_ext_if0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_ext_if1_ibit_clk = {
	.halt_reg = 0x11018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_ext_if1_ibit_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_ext_if1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_lpm_core_clk = {
	.halt_reg = 0x1e000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1e000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_lpm_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_lpm_mem0_core_clk = {
	.halt_reg = 0x1e004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1e004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_lpm_mem0_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_ext_mclk0_clk = {
	.halt_reg = 0x20014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x20014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_ext_mclk0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_ext_mclk0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_sysnoc_mport_core_clk = {
	.halt_reg = 0x23000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x23000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_sysnoc_mport_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc lpass_core_cc_lpass_core_hm_gdsc = {
	.gdscr = 0x0,
	.pd = {
		.name = "lpass_core_cc_lpass_core_hm_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct clk_regmap *lpass_core_cc_sc7280_clocks[] = {
	[LPASS_CORE_CC_CORE_CLK] = &lpass_core_cc_core_clk.clkr,
	[LPASS_CORE_CC_CORE_CLK_SRC] = &lpass_core_cc_core_clk_src.clkr,
	[LPASS_CORE_CC_DIG_PLL] = &lpass_core_cc_dig_pll.clkr,
	[LPASS_CORE_CC_DIG_PLL_OUT_MAIN_DIV_CLK_SRC] =
		&lpass_core_cc_dig_pll_out_main_div_clk_src.clkr,
	[LPASS_CORE_CC_DIG_PLL_OUT_ODD] = &lpass_core_cc_dig_pll_out_odd.clkr,
	[LPASS_CORE_CC_EXT_IF0_CLK_SRC] = &lpass_core_cc_ext_if0_clk_src.clkr,
	[LPASS_CORE_CC_EXT_IF0_IBIT_CLK] = &lpass_core_cc_ext_if0_ibit_clk.clkr,
	[LPASS_CORE_CC_EXT_IF1_CLK_SRC] = &lpass_core_cc_ext_if1_clk_src.clkr,
	[LPASS_CORE_CC_EXT_IF1_IBIT_CLK] = &lpass_core_cc_ext_if1_ibit_clk.clkr,
	[LPASS_CORE_CC_LPM_CORE_CLK] = &lpass_core_cc_lpm_core_clk.clkr,
	[LPASS_CORE_CC_LPM_MEM0_CORE_CLK] = &lpass_core_cc_lpm_mem0_core_clk.clkr,
	[LPASS_CORE_CC_SYSNOC_MPORT_CORE_CLK] = &lpass_core_cc_sysnoc_mport_core_clk.clkr,
	[LPASS_CORE_CC_EXT_MCLK0_CLK] = &lpass_core_cc_ext_mclk0_clk.clkr,
	[LPASS_CORE_CC_EXT_MCLK0_CLK_SRC] = &lpass_core_cc_ext_mclk0_clk_src.clkr,
};

static struct regmap_config lpass_core_cc_sc7280_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
};

static const struct qcom_cc_desc lpass_core_cc_sc7280_desc = {
	.config = &lpass_core_cc_sc7280_regmap_config,
	.clks = lpass_core_cc_sc7280_clocks,
	.num_clks = ARRAY_SIZE(lpass_core_cc_sc7280_clocks),
};

static const struct of_device_id lpass_core_cc_sc7280_match_table[] = {
	{ .compatible = "qcom,sc7280-lpasscorecc" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_core_cc_sc7280_match_table);

static struct gdsc *lpass_core_hm_sc7280_gdscs[] = {
	[LPASS_CORE_CC_LPASS_CORE_HM_GDSC] = &lpass_core_cc_lpass_core_hm_gdsc,
};

static const struct qcom_cc_desc lpass_core_hm_sc7280_desc = {
	.config = &lpass_core_cc_sc7280_regmap_config,
	.gdscs = lpass_core_hm_sc7280_gdscs,
	.num_gdscs = ARRAY_SIZE(lpass_core_hm_sc7280_gdscs),
};

static int lpass_core_cc_sc7280_probe(struct platform_device *pdev)
{
	const struct qcom_cc_desc *desc;
	struct regmap *regmap;

	lpass_core_cc_sc7280_regmap_config.name = "lpass_core_cc";
	lpass_core_cc_sc7280_regmap_config.max_register = 0x4f004;
	desc = &lpass_core_cc_sc7280_desc;

	regmap = qcom_cc_map(pdev, desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_lucid_pll_configure(&lpass_core_cc_dig_pll, regmap, &lpass_core_cc_dig_pll_config);

	return qcom_cc_really_probe(pdev, &lpass_core_cc_sc7280_desc, regmap);
}

static struct platform_driver lpass_core_cc_sc7280_driver = {
	.probe = lpass_core_cc_sc7280_probe,
	.driver = {
		.name = "lpass_core_cc-sc7280",
		.of_match_table = lpass_core_cc_sc7280_match_table,
	},
};

static int lpass_hm_core_probe(struct platform_device *pdev)
{
	const struct qcom_cc_desc *desc;

	lpass_core_cc_sc7280_regmap_config.name = "lpass_hm_core";
	lpass_core_cc_sc7280_regmap_config.max_register = 0x24;
	desc = &lpass_core_hm_sc7280_desc;

	return qcom_cc_probe_by_index(pdev, 0, desc);
}

static const struct of_device_id lpass_hm_sc7280_match_table[] = {
	{ .compatible = "qcom,sc7280-lpasshm" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_hm_sc7280_match_table);

static struct platform_driver lpass_hm_sc7280_driver = {
	.probe = lpass_hm_core_probe,
	.driver = {
		.name = "lpass_hm-sc7280",
		.of_match_table = lpass_hm_sc7280_match_table,
	},
};

static int __init lpass_core_cc_sc7280_init(void)
{
	int ret;

	ret = platform_driver_register(&lpass_hm_sc7280_driver);
	if (ret)
		return ret;

	return platform_driver_register(&lpass_core_cc_sc7280_driver);
}
subsys_initcall(lpass_core_cc_sc7280_init);

static void __exit lpass_core_cc_sc7280_exit(void)
{
	platform_driver_unregister(&lpass_core_cc_sc7280_driver);
	platform_driver_unregister(&lpass_hm_sc7280_driver);
}
module_exit(lpass_core_cc_sc7280_exit);

MODULE_DESCRIPTION("QTI LPASS_CORE_CC SC7280 Driver");
MODULE_LICENSE("GPL v2");
