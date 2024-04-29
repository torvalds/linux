// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gpucc-pitti.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "reset.h"
#include "vdd-level-holi.h"

#define CX_GMU_CBCR_SLEEP_MASK		0xf
#define CX_GMU_CBCR_SLEEP_SHIFT		4
#define CX_GMU_CBCR_WAKE_MASK		0xf
#define CX_GMU_CBCR_WAKE_SHIFT		8

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_HIGH_L1 + 1, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mx, VDD_HIGH_L1 + 1, 1, vdd_corner);

static struct clk_vdd_class *gpu_cc_pitti_regulators[] = {
	&vdd_cx,
	&vdd_mx,
};

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_MAIN_DIV,
	P_GPU_CC_PLL0_2X_DIV_CLK_SRC,
	P_GPU_CC_PLL0_OUT_EVEN,
	P_GPU_CC_PLL1_OUT_EVEN,
	P_GPU_CC_PLL1_OUT_MAIN,
	P_GPU_CC_PLL1_OUT_ODD,
};

static const struct pll_vco lucid_evo_vco[] = {
	{ 249600000, 2020000000, 0 },
};

static const struct pll_vco zonda_evo_vco[] = {
	{ 600000000, 3600000000, 0 },
};

/* 680MHz configuration */
static const struct alpha_pll_config gpu_cc_pll0_config = {
	.l = 0x23,
	.alpha = 0x6aaa,
	.config_ctl_val = 0x08200800,
	.config_ctl_hi_val = 0x05028001,
	.config_ctl_hi1_val = 0x00000000,
	.user_ctl_hi_val = 0x02001000,
};

static struct clk_alpha_pll gpu_cc_pll0 = {
	.offset = 0x0,
	.vco_table = zonda_evo_vco,
	.num_vco = ARRAY_SIZE(zonda_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_ZONDA_EVO],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_zonda_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER] = 1450000000,
				[VDD_LOW] = 2000000000,
				[VDD_NOMINAL] = 2900000000,
				[VDD_HIGH] = 3600000000},
		},
	},
};

static struct clk_fixed_factor gpu_cc_pll0_out_even = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "gpu_cc_pll0_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&gpu_cc_pll0.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

/* 690MHz configuration */
static const struct alpha_pll_config gpu_cc_pll1_config = {
	.l = 0x23,
	.cal_l = 0x44,
	.alpha = 0xf000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x32aa299c,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000805,
};

static struct clk_alpha_pll gpu_cc_pll1 = {
	.offset = 0x1000,
	.vco_table = lucid_evo_vco,
	.num_vco = ARRAY_SIZE(lucid_evo_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_EVO],
	.clkr = {
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_pll1",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER_D1] = 500000000,
				[VDD_LOWER] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1500000000,
				[VDD_NOMINAL] = 1800000000,
				[VDD_HIGH] = 2020000000},
		},
	},
};

static const struct parent_map gpu_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_EVEN, 1 },
	{ P_GPU_CC_PLL1_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpu_cc_pll0_out_even.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .fw_name = "gpll0_out_main" },
	{ .fw_name = "gpll0_out_main_div" },
};

static const struct parent_map gpu_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_EVEN, 1 },
	{ P_GPU_CC_PLL0_2X_DIV_CLK_SRC, 2 },
	{ P_GPU_CC_PLL1_OUT_EVEN, 3 },
	{ P_GPU_CC_PLL1_OUT_ODD, 4 },
	{ P_GPLL0_OUT_MAIN, 5 },
};

static const struct clk_parent_data gpu_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpu_cc_pll0_out_even.hw },
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .fw_name = "gpll0_out_main" },
};

static const struct parent_map gpu_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data gpu_cc_parent_data_2_ao[] = {
	{ .fw_name = "bi_tcxo_ao" },
};

static const struct freq_tbl ftbl_gpu_cc_gmu_clk_src[] = {
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gmu_clk_src = {
	.cmd_rcgr = 0x9160,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_0,
	.freq_tbl = ftbl_gpu_cc_gmu_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_gmu_clk_src",
		.parent_data = gpu_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gpu_cc_pitti_regulators,
		.num_vdd_classes = ARRAY_SIZE(gpu_cc_pitti_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 200000000},
	},
};

static const struct freq_tbl ftbl_gpu_cc_gx_gfx3d_clk_src[] = {
	F(340000000, P_GPU_CC_PLL0_OUT_EVEN, 1, 0, 0),
	F(500000000, P_GPU_CC_PLL0_OUT_EVEN, 1, 0, 0),
	F(605000000, P_GPU_CC_PLL0_OUT_EVEN, 1, 0, 0),
	F(765000000, P_GPU_CC_PLL0_OUT_EVEN, 1, 0, 0),
	F(875000000, P_GPU_CC_PLL0_OUT_EVEN, 1, 0, 0),
	F(975000000, P_GPU_CC_PLL0_OUT_EVEN, 1, 0, 0),
	F(1115000000, P_GPU_CC_PLL0_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gx_gfx3d_clk_src = {
	.cmd_rcgr = 0x906c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_1,
	.freq_tbl = ftbl_gpu_cc_gx_gfx3d_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_gx_gfx3d_clk_src",
		.parent_data = gpu_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_classes = gpu_cc_pitti_regulators,
		.num_vdd_classes = ARRAY_SIZE(gpu_cc_pitti_regulators),
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 340000000,
			[VDD_LOW] = 500000000,
			[VDD_LOW_L1] = 605000000,
			[VDD_NOMINAL] = 765024000,
			[VDD_NOMINAL_L1] = 875000000,
			[VDD_HIGH] = 975072000,
			[VDD_HIGH_L1] = 1115000000},
	},
};

static const struct freq_tbl ftbl_gpu_cc_xo_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_xo_clk_src = {
	.cmd_rcgr = 0x9008,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_2,
	.freq_tbl = ftbl_gpu_cc_xo_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_xo_clk_src",
		.parent_data = gpu_cc_parent_data_2_ao,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_2_ao),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div gpu_cc_demet_div_clk_src = {
	.reg = 0x9048,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "gpu_cc_demet_div_clk_src",
		.parent_hws = (const struct clk_hw*[]) {
			&gpu_cc_xo_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch gpu_cc_crc_ahb_clk = {
	.halt_reg = 0x90e8,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x90e8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_crc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_accu_shift_clk = {
	.halt_reg = 0x9124,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9124,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cx_accu_shift_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gfx3d_clk = {
	.halt_reg = 0x9130,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x9130,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cx_gfx3d_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gmu_clk = {
	.halt_reg = 0x9104,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x9104,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cx_gmu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cxo_clk = {
	.halt_reg = 0x910c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x910c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_cxo_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_DONT_HOLD_STATE | CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gx_accu_shift_clk = {
	.halt_reg = 0x90c4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x90c4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_gx_accu_shift_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_xo_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gx_gfx3d_clk = {
	.halt_reg = 0x90a4,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x90a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_gx_gfx3d_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_hlos1_vote_gpu_smmu_clk = {
	.halt_reg = 0x7000,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x7000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_hlos1_vote_gpu_smmu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_memnoc_gfx_clk = {
	.halt_reg = 0x9114,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9114,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "gpu_cc_memnoc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

struct clk_hw *gpu_cc_pitti_hws[] = {
	[GPU_CC_PLL0_OUT_EVEN] = &gpu_cc_pll0_out_even.hw,
};

static struct clk_regmap *gpu_cc_pitti_clocks[] = {
	[GPU_CC_CRC_AHB_CLK] = &gpu_cc_crc_ahb_clk.clkr,
	[GPU_CC_CX_ACCU_SHIFT_CLK] = &gpu_cc_cx_accu_shift_clk.clkr,
	[GPU_CC_CX_GFX3D_CLK] = &gpu_cc_cx_gfx3d_clk.clkr,
	[GPU_CC_CX_GMU_CLK] = &gpu_cc_cx_gmu_clk.clkr,
	[GPU_CC_CXO_CLK] = &gpu_cc_cxo_clk.clkr,
	[GPU_CC_DEMET_DIV_CLK_SRC] = &gpu_cc_demet_div_clk_src.clkr,
	[GPU_CC_GMU_CLK_SRC] = &gpu_cc_gmu_clk_src.clkr,
	[GPU_CC_GX_ACCU_SHIFT_CLK] = &gpu_cc_gx_accu_shift_clk.clkr,
	[GPU_CC_GX_GFX3D_CLK] = &gpu_cc_gx_gfx3d_clk.clkr,
	[GPU_CC_GX_GFX3D_CLK_SRC] = &gpu_cc_gx_gfx3d_clk_src.clkr,
	[GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK] = &gpu_cc_hlos1_vote_gpu_smmu_clk.clkr,
	[GPU_CC_MEMNOC_GFX_CLK] = &gpu_cc_memnoc_gfx_clk.clkr,
	[GPU_CC_PLL0] = &gpu_cc_pll0.clkr,
	[GPU_CC_PLL1] = &gpu_cc_pll1.clkr,
	[GPU_CC_XO_CLK_SRC] = &gpu_cc_xo_clk_src.clkr,
};

static const struct qcom_reset_map gpu_cc_pitti_resets[] = {
	[GPUCC_GPU_CC_CX_BCR] = { 0x90cc },
	[GPUCC_GPU_CC_GFX3D_AON_BCR] = { 0x912c },
	[GPUCC_GPU_CC_GMU_BCR] = { 0x915c },
	[GPUCC_GPU_CC_GX_BCR] = { 0x9050 },
	[GPUCC_GPU_CC_XO_BCR] = { 0x9000 },
	[GPU_CC_FREQUENCY_LIMITER_IRQ_CLEAR] = { 0x91b0 },
};

static const struct regmap_config gpu_cc_pitti_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x9240,
	.fast_io = true,
};

static const struct qcom_cc_desc gpu_cc_pitti_desc = {
	.config = &gpu_cc_pitti_regmap_config,
	.clks = gpu_cc_pitti_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_pitti_clocks),
	.clk_hws = gpu_cc_pitti_hws,
	.num_clk_hws = ARRAY_SIZE(gpu_cc_pitti_hws),
	.resets = gpu_cc_pitti_resets,
	.num_resets = ARRAY_SIZE(gpu_cc_pitti_resets),
	.clk_regulators = gpu_cc_pitti_regulators,
	.num_clk_regulators = ARRAY_SIZE(gpu_cc_pitti_regulators),
};

static const struct of_device_id gpu_cc_pitti_match_table[] = {
	{ .compatible = "qcom,pitti-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpu_cc_pitti_match_table);

static int gpu_cc_pitti_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	unsigned int value, mask;
	int ret;

	regmap = qcom_cc_map(pdev, &gpu_cc_pitti_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_zonda_evo_pll_configure(&gpu_cc_pll0, regmap, &gpu_cc_pll0_config);
	clk_lucid_evo_pll_configure(&gpu_cc_pll1, regmap, &gpu_cc_pll1_config);

	/* Enable frequency limiter irq */
	regmap_write(regmap, 0x91ac, 0x0);

	/*
	 * Keep clocks always enabled:
	 *	gpu_cc_ahb_clk
	 *	gpu_cc_cxo_aon_clk
	 *	gpu_cc_demet_clk
	 *	gpu_cc_gx_cxo_clk
	 *	gpu_cc_sleep_clk
	 */
	regmap_update_bits(regmap, 0x90e4, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x9004, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x904c, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x90b4, BIT(0), BIT(0));
	regmap_update_bits(regmap, 0x90fc, BIT(0), BIT(0));

	/* Recommended WAKEUP/SLEEP settings for the gpu_cc_cx_gmu_clk */
	mask = CX_GMU_CBCR_WAKE_MASK << CX_GMU_CBCR_WAKE_SHIFT;
	mask |= CX_GMU_CBCR_SLEEP_MASK << CX_GMU_CBCR_SLEEP_SHIFT;
	value = 0xf << CX_GMU_CBCR_WAKE_SHIFT | 0xf << CX_GMU_CBCR_SLEEP_SHIFT;
	regmap_update_bits(regmap, gpu_cc_cx_gmu_clk.clkr.enable_reg,
								mask, value);


	ret = qcom_cc_really_probe(pdev, &gpu_cc_pitti_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GPU CC clocks\n");
		return ret;
	}

	dev_info(&pdev->dev, "Registered GPU CC clocks\n");

	return ret;
}

static void gpu_cc_pitti_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &gpu_cc_pitti_desc);
}

static struct platform_driver gpu_cc_pitti_driver = {
	.probe = gpu_cc_pitti_probe,
	.driver = {
		.name = "gpu_cc-pitti",
		.of_match_table = gpu_cc_pitti_match_table,
		.sync_state = gpu_cc_pitti_sync_state,
	},
};

static int __init gpu_cc_pitti_init(void)
{
	return platform_driver_register(&gpu_cc_pitti_driver);
}
subsys_initcall(gpu_cc_pitti_init);

static void __exit gpu_cc_pitti_exit(void)
{
	platform_driver_unregister(&gpu_cc_pitti_driver);
}
module_exit(gpu_cc_pitti_exit);

MODULE_DESCRIPTION("QTI GPU_CC PITTI Driver");
MODULE_LICENSE("GPL");
