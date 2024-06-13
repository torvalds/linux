// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gpucc-sc8280xp.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "reset.h"
#include "gdsc.h"

/* Need to match the order of clocks in DT binding */
enum {
	DT_BI_TCXO,
	DT_GCC_GPU_GPLL0_CLK_SRC,
	DT_GCC_GPU_GPLL0_DIV_CLK_SRC,
};

enum {
	P_BI_TCXO,
	P_GCC_GPU_GPLL0_CLK_SRC,
	P_GCC_GPU_GPLL0_DIV_CLK_SRC,
	P_GPU_CC_PLL0_OUT_MAIN,
	P_GPU_CC_PLL1_OUT_MAIN,
};

static const struct clk_parent_data parent_data_tcxo = { .index = DT_BI_TCXO };

static const struct pll_vco lucid_5lpe_vco[] = {
	{ 249600000, 1800000000, 0 },
};

static struct alpha_pll_config gpu_cc_pll0_config = {
	.l = 0x1c,
	.alpha = 0xa555,
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

static struct clk_alpha_pll gpu_cc_pll0 = {
	.offset = 0x0,
	.vco_table = lucid_5lpe_vco,
	.num_vco = ARRAY_SIZE(lucid_5lpe_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_pll0",
			.parent_data = &parent_data_tcxo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_5lpe_ops,
		},
	},
};

static struct alpha_pll_config gpu_cc_pll1_config = {
	.l = 0x1A,
	.alpha = 0xaaa,
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

static struct clk_alpha_pll gpu_cc_pll1 = {
	.offset = 0x100,
	.vco_table = lucid_5lpe_vco,
	.num_vco = ARRAY_SIZE(lucid_5lpe_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_pll1",
			.parent_data = &parent_data_tcxo,
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_5lpe_ops,
		},
	},
};

static const struct parent_map gpu_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_MAIN, 1 },
	{ P_GPU_CC_PLL1_OUT_MAIN, 3 },
	{ P_GCC_GPU_GPLL0_CLK_SRC, 5 },
	{ P_GCC_GPU_GPLL0_DIV_CLK_SRC, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .index = DT_GCC_GPU_GPLL0_CLK_SRC },
	{ .index = DT_GCC_GPU_GPLL0_DIV_CLK_SRC },
};

static const struct parent_map gpu_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL1_OUT_MAIN, 3 },
	{ P_GCC_GPU_GPLL0_CLK_SRC, 5 },
	{ P_GCC_GPU_GPLL0_DIV_CLK_SRC, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .index = DT_GCC_GPU_GPLL0_CLK_SRC },
	{ .index = DT_GCC_GPU_GPLL0_DIV_CLK_SRC },
};

static const struct freq_tbl ftbl_gpu_cc_gmu_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(200000000, P_GCC_GPU_GPLL0_DIV_CLK_SRC, 1.5, 0, 0),
	F(500000000, P_GPU_CC_PLL1_OUT_MAIN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gmu_clk_src = {
	.cmd_rcgr = 0x1120,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_0,
	.freq_tbl = ftbl_gpu_cc_gmu_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gpu_cc_gmu_clk_src",
		.parent_data = gpu_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_0),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gpu_cc_hub_clk_src[] = {
	F(200000000, P_GCC_GPU_GPLL0_CLK_SRC, 3, 0, 0),
	F(300000000, P_GCC_GPU_GPLL0_CLK_SRC, 2, 0, 0),
	F(400000000, P_GCC_GPU_GPLL0_CLK_SRC, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_hub_clk_src = {
	.cmd_rcgr = 0x117c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_1,
	.freq_tbl = ftbl_gpu_cc_hub_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gpu_cc_hub_clk_src",
		.parent_data = gpu_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_1),
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_regmap_div gpu_cc_hub_ahb_div_clk_src = {
	.reg = 0x11c0,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_hub_ahb_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gpu_cc_hub_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div gpu_cc_hub_cx_int_div_clk_src = {
	.reg = 0x11bc,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_hub_cx_int_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&gpu_cc_hub_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gpu_cc_ahb_clk = {
	.halt_reg = 0x1078,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1078,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gpu_cc_hub_ahb_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_crc_ahb_clk = {
	.halt_reg = 0x107c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x107c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_crc_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gpu_cc_hub_ahb_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gmu_clk = {
	.halt_reg = 0x1098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1098,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_cx_gmu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gpu_cc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_snoc_dvm_clk = {
	.halt_reg = 0x108c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x108c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_cx_snoc_dvm_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cxo_aon_clk = {
	.halt_reg = 0x1004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x1004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_cxo_aon_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gx_gmu_clk = {
	.halt_reg = 0x1064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_gx_gmu_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gpu_cc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_hlos1_vote_gpu_smmu_clk = {
	.halt_reg = 0x5000,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x5000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_hlos1_vote_gpu_smmu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_hub_aon_clk = {
	.halt_reg = 0x1178,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1178,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_hub_aon_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gpu_cc_hub_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gpu_cc_hub_cx_int_clk = {
	.halt_reg = 0x1204,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1204,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_hub_cx_int_clk",
			.parent_hws = (const struct clk_hw*[]){
				&gpu_cc_hub_cx_int_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch gpu_cc_sleep_clk = {
	.halt_reg = 0x1090,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x1090,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *gpu_cc_sc8280xp_clocks[] = {
	[GPU_CC_AHB_CLK] = &gpu_cc_ahb_clk.clkr,
	[GPU_CC_CRC_AHB_CLK] = &gpu_cc_crc_ahb_clk.clkr,
	[GPU_CC_CX_GMU_CLK] = &gpu_cc_cx_gmu_clk.clkr,
	[GPU_CC_CX_SNOC_DVM_CLK] = &gpu_cc_cx_snoc_dvm_clk.clkr,
	[GPU_CC_CXO_AON_CLK] = &gpu_cc_cxo_aon_clk.clkr,
	[GPU_CC_GMU_CLK_SRC] = &gpu_cc_gmu_clk_src.clkr,
	[GPU_CC_GX_GMU_CLK] = &gpu_cc_gx_gmu_clk.clkr,
	[GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK] = &gpu_cc_hlos1_vote_gpu_smmu_clk.clkr,
	[GPU_CC_HUB_AHB_DIV_CLK_SRC] = &gpu_cc_hub_ahb_div_clk_src.clkr,
	[GPU_CC_HUB_AON_CLK] = &gpu_cc_hub_aon_clk.clkr,
	[GPU_CC_HUB_CLK_SRC] = &gpu_cc_hub_clk_src.clkr,
	[GPU_CC_HUB_CX_INT_CLK] = &gpu_cc_hub_cx_int_clk.clkr,
	[GPU_CC_HUB_CX_INT_DIV_CLK_SRC] = &gpu_cc_hub_cx_int_div_clk_src.clkr,
	[GPU_CC_PLL0] = &gpu_cc_pll0.clkr,
	[GPU_CC_PLL1] = &gpu_cc_pll1.clkr,
	[GPU_CC_SLEEP_CLK] = &gpu_cc_sleep_clk.clkr,
};

static struct gdsc cx_gdsc = {
	.gdscr = 0x106c,
	.gds_hw_ctrl = 0x1540,
	.pd = {
		.name = "cx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE | RETAIN_FF_ENABLE,
};

static struct gdsc gx_gdsc = {
	.gdscr = 0x100c,
	.clamp_io_ctrl = 0x1508,
	.pd = {
		.name = "gx_gdsc",
		.power_on = gdsc_gx_do_nothing_enable,
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = CLAMP_IO | RETAIN_FF_ENABLE,
};

static struct gdsc *gpu_cc_sc8280xp_gdscs[] = {
	[GPU_CC_CX_GDSC] = &cx_gdsc,
	[GPU_CC_GX_GDSC] = &gx_gdsc,
};

static const struct regmap_config gpu_cc_sc8280xp_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x8030,
	.fast_io = true,
};

static struct qcom_cc_desc gpu_cc_sc8280xp_desc = {
	.config = &gpu_cc_sc8280xp_regmap_config,
	.clks = gpu_cc_sc8280xp_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_sc8280xp_clocks),
	.gdscs = gpu_cc_sc8280xp_gdscs,
	.num_gdscs = ARRAY_SIZE(gpu_cc_sc8280xp_gdscs),
};

static int gpu_cc_sc8280xp_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &gpu_cc_sc8280xp_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_lucid_pll_configure(&gpu_cc_pll0, regmap, &gpu_cc_pll0_config);
	clk_lucid_pll_configure(&gpu_cc_pll1, regmap, &gpu_cc_pll1_config);

	/*
	 * Keep the clocks always-ON
	 * GPU_CC_CB_CLK, GPU_CC_CXO_CLK
	 */
	regmap_update_bits(regmap, 0x1170, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x109c, BIT(0), BIT(0));

	return qcom_cc_really_probe(pdev, &gpu_cc_sc8280xp_desc, regmap);
}

static const struct of_device_id gpu_cc_sc8280xp_match_table[] = {
	{ .compatible = "qcom,sc8280xp-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpu_cc_sc8280xp_match_table);

static struct platform_driver gpu_cc_sc8280xp_driver = {
	.probe = gpu_cc_sc8280xp_probe,
	.driver = {
		.name = "gpu_cc-sc8280xp",
		.of_match_table = gpu_cc_sc8280xp_match_table,
	},
};
module_platform_driver(gpu_cc_sc8280xp_driver);

MODULE_DESCRIPTION("Qualcomm SC8280XP GPU clock controller");
MODULE_LICENSE("GPL");
