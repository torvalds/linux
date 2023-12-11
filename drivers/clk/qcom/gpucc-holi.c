// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gpucc-holi.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "reset.h"
#include "vdd-level-holi.h"

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_HIGH_L1 + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mx, VDD_HIGH_L1 + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_gx, VDD_HIGH_L1 + 1, 1, vdd_corner);

static struct clk_vdd_class *gpu_cc_holi_regulators[] = {
	&vdd_cx,
	&vdd_mx,
	&vdd_gx,
};

enum {
	P_BI_TCXO,
	P_GCC_GPU_GPLL0_CLK_SRC,
	P_GCC_GPU_GPLL0_DIV_CLK_SRC,
	P_GPU_CC_PLL0_OUT_EVEN,
	P_GPU_CC_PLL0_OUT_MAIN,
	P_GPU_CC_PLL0_OUT_ODD,
	P_GPU_CC_PLL1_OUT_EVEN,
	P_GPU_CC_PLL1_OUT_MAIN,
	P_GPU_CC_PLL1_OUT_ODD,
};

static struct pll_vco fabia_vco[] = {
	{ 249600000, 2000000000, 0 },
};

/* 506MHz configuration */
static const struct alpha_pll_config gpu_cc_pll0_config = {
	.l = 0x1A,
	.cal_l = 0x3F,
	.alpha = 0x5AAA,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002067,
	.test_ctl_val = 0x40000000,
	.test_ctl_hi_val = 0x00000002,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00004805,
};

static struct clk_alpha_pll gpu_cc_pll0 = {
	.offset = 0x0,
	.vco_table = fabia_vco,
	.num_vco = ARRAY_SIZE(fabia_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fabia_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

/* 514MHz configuration */
static const struct alpha_pll_config gpu_cc_pll1_config = {
	.l = 0x1A,
	.cal_l = 0x3D,
	.alpha = 0xC555,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002067,
	.test_ctl_val = 0x40000000,
	.test_ctl_hi_val = 0x00000002,
	.user_ctl_val = 0x00000001,
	.user_ctl_hi_val = 0x00004805,
};

static struct clk_alpha_pll gpu_cc_pll1 = {
	.offset = 0x100,
	.vco_table = fabia_vco,
	.num_vco = ARRAY_SIZE(fabia_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_pll1",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fabia_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
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
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .fw_name = "gcc_gpu_gpll0_clk_src" },
	{ .fw_name = "gcc_gpu_gpll0_div_clk_src" },
};

static const struct parent_map gpu_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_EVEN, 1 },
	{ P_GPU_CC_PLL0_OUT_ODD, 2 },
	{ P_GPU_CC_PLL1_OUT_EVEN, 3 },
	{ P_GPU_CC_PLL1_OUT_ODD, 4 },
	{ P_GCC_GPU_GPLL0_CLK_SRC, 5 },
};

static const struct clk_parent_data gpu_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .fw_name = "gcc_gpu_gpll0_clk_src" },
};

static const struct parent_map gpu_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPU_GPLL0_CLK_SRC, 5 },
	{ P_GCC_GPU_GPLL0_DIV_CLK_SRC, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_2[] = {
	{ .fw_name = "bi_tcxo" },
	{ .fw_name = "gcc_gpu_gpll0_clk_src" },
	{ .fw_name = "gcc_gpu_gpll0_div_clk_src" },
};

static const struct freq_tbl ftbl_gpu_cc_gmu_clk_src[] = {
	F(200000000, P_GCC_GPU_GPLL0_DIV_CLK_SRC, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gmu_clk_src = {
	.cmd_rcgr = 0x1120,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_0,
	.freq_tbl = ftbl_gpu_cc_gmu_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gpu_cc_gmu_clk_src",
		.parent_data = gpu_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 200000000},
	},
};

static const struct freq_tbl ftbl_gpu_cc_gx_gfx3d_clk_src[] = {
	F(253000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(355000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(430000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(565000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(650000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(800000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	F(875000000, P_GPU_CC_PLL0_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gx_gfx3d_clk_src = {
	.cmd_rcgr = 0x101c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_1,
	.freq_tbl = ftbl_gpu_cc_gx_gfx3d_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "gpu_cc_gx_gfx3d_clk_src",
		.parent_data = gpu_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gpu_cc_holi_regulators,
		.num_vdd_classes = ARRAY_SIZE(gpu_cc_holi_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 253000000,
			[VDD_LOW] = 355000000,
			[VDD_LOW_L1] = 430000000,
			[VDD_NOMINAL] = 565000000,
			[VDD_NOMINAL_L1] = 650000000,
			[VDD_HIGH] = 800000000,
			[VDD_HIGH_L1] = 875000000},
	},
};

static struct clk_branch gpu_cc_cx_gfx3d_clk = {
	.halt_reg = 0x10a4,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x10a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_cx_gfx3d_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpu_cc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gfx3d_slv_clk = {
	.halt_reg = 0x10a8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x10a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_cx_gfx3d_slv_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpu_cc_gx_gfx3d_clk_src.clkr.hw,
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
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpu_cc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_snoc_dvm_clk = {
	.halt_reg = 0x108c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x108c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_cx_snoc_dvm_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "gcc_gpu_snoc_dvm_gfx_clk",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cxo_aon_clk = {
	.halt_reg = 0x1004,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_cxo_aon_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cxo_clk = {
	.halt_reg = 0x109c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x109c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_cxo_clk",
			.flags = CLK_DONT_HOLD_STATE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gx_gfx3d_clk = {
	.halt_reg = 0x1054,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1054,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "gpu_cc_gx_gfx3d_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpu_cc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpu_cc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
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

static struct clk_regmap *gpu_cc_holi_clocks[] = {
	[GPU_CC_CX_GFX3D_CLK] = &gpu_cc_cx_gfx3d_clk.clkr,
	[GPU_CC_CX_GFX3D_SLV_CLK] = &gpu_cc_cx_gfx3d_slv_clk.clkr,
	[GPU_CC_CX_GMU_CLK] = &gpu_cc_cx_gmu_clk.clkr,
	[GPU_CC_CX_SNOC_DVM_CLK] = &gpu_cc_cx_snoc_dvm_clk.clkr,
	[GPU_CC_CXO_AON_CLK] = &gpu_cc_cxo_aon_clk.clkr,
	[GPU_CC_CXO_CLK] = &gpu_cc_cxo_clk.clkr,
	[GPU_CC_GMU_CLK_SRC] = &gpu_cc_gmu_clk_src.clkr,
	[GPU_CC_GX_GFX3D_CLK] = &gpu_cc_gx_gfx3d_clk.clkr,
	[GPU_CC_GX_GFX3D_CLK_SRC] = &gpu_cc_gx_gfx3d_clk_src.clkr,
	[GPU_CC_GX_GMU_CLK] = &gpu_cc_gx_gmu_clk.clkr,
	[GPU_CC_PLL0] = &gpu_cc_pll0.clkr,
	[GPU_CC_PLL1] = &gpu_cc_pll1.clkr,
	[GPU_CC_SLEEP_CLK] = &gpu_cc_sleep_clk.clkr,
};

static const struct qcom_reset_map gpu_cc_holi_resets[] = {
	[GPUCC_GPU_CC_FREQUENCY_LIMITER_IRQ_CLEAR] = { 0x153C, 0 },
};

static const struct regmap_config gpu_cc_holi_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x8008,
	.fast_io = true,
};

static const struct qcom_cc_desc gpu_cc_holi_desc = {
	.config = &gpu_cc_holi_regmap_config,
	.clks = gpu_cc_holi_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_holi_clocks),
	.resets = gpu_cc_holi_resets,
	.num_resets = ARRAY_SIZE(gpu_cc_holi_resets),
	.clk_regulators = gpu_cc_holi_regulators,
	.num_clk_regulators = ARRAY_SIZE(gpu_cc_holi_regulators),
};

static const struct of_device_id gpu_cc_holi_match_table[] = {
	{ .compatible = "qcom,holi-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpu_cc_holi_match_table);

static int gpu_cc_holi_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gpu_cc_holi_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/*
	 * Keep the clocks always-ON
	 * GPU_CC_AHB_CLK, GPU_CC_GX_CXO_CLK
	 */
	regmap_update_bits(regmap, 0x1078, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x1060, BIT(0), BIT(0));

	clk_fabia_pll_configure(&gpu_cc_pll0, regmap, &gpu_cc_pll0_config);
	clk_fabia_pll_configure(&gpu_cc_pll1, regmap, &gpu_cc_pll1_config);

	regmap_write(regmap, 0x1538, 0x0);

	ret = qcom_cc_really_probe(pdev, &gpu_cc_holi_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GPU CC clocks\n");
		return ret;
	}

	dev_info(&pdev->dev, "Registered GPU CC clocks\n");

	return ret;
}

static void gpucc_holi_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &gpu_cc_holi_desc);
}

static struct platform_driver gpu_cc_holi_driver = {
	.probe = gpu_cc_holi_probe,
	.driver = {
		.name = "gpu_cc-holi",
		.of_match_table = gpu_cc_holi_match_table,
		.sync_state = gpucc_holi_sync_state,
	},
};

static int __init gpu_cc_holi_init(void)
{
	return platform_driver_register(&gpu_cc_holi_driver);
}
subsys_initcall(gpu_cc_holi_init);

static void __exit gpu_cc_holi_exit(void)
{
	platform_driver_unregister(&gpu_cc_holi_driver);
}
module_exit(gpu_cc_holi_exit);

MODULE_DESCRIPTION("QTI GPU_CC HOLI Driver");
MODULE_LICENSE("GPL");
