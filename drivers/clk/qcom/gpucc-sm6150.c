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

#include <dt-bindings/clock/qcom,gpucc-sm6150.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pm.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "vdd-level-sm6150.h"

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_NUM, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mx, VDD_NUM, 1, vdd_corner);

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_MAIN_DIV,
	P_GPU_CC_PLL0_2X_CLK,
	P_CRC_DIV_PLL0_OUT_AUX2,
	P_GPU_CC_PLL0_OUT_MAIN,
	P_GPU_CC_PLL1_OUT_AUX,
	P_CRC_DIV_PLL1_OUT_AUX2,
	P_GPU_CC_PLL1_OUT_MAIN,
};

static struct pll_vco gpu_cc_pll0_vco[] = {
	{ 1000000000, 2000000000, 0 },
};

static struct pll_vco gpu_cc_pll1_vco[] = {
	{ 500000000,  1000000000, 2 },
};

/* 1020MHz configuration */
static struct alpha_pll_config gpu_pll0_config = {
	.l = 0x35,
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
	.alpha_hi = 0x20,
	.alpha = 0x00,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x0 << 20,
	.vco_mask = 0x3 << 20,
	.aux2_output_mask = BIT(2),
};

static struct clk_init_data gpu_cc_pll0_sa6155 = {
	.name = "gpu_cc_pll0",
	.parent_data = &(const struct clk_parent_data){
		.fw_name = "bi_tcxo",
	},
	.num_parents = 1,
	.ops = &clk_alpha_pll_slew_ops,
};

static struct clk_alpha_pll gpu_cc_pll0 = {
	.offset = 0x0,
	.vco_table = gpu_cc_pll0_vco,
	.num_vco = ARRAY_SIZE(gpu_cc_pll0_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.config = &gpu_pll0_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 1100000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

/* 930MHz configuration */
static struct alpha_pll_config gpu_pll1_config = {
	.l = 0x30,
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
	.alpha_hi = 0x70,
	.alpha = 0x00,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = 0x3 << 20,
	.aux2_output_mask = BIT(2),
};

static struct clk_init_data gpu_cc_pll1_sa6155 = {
	.name = "gpu_cc_pll1",
	.parent_data = &(const struct clk_parent_data){
		.fw_name = "bi_tcxo",
	},
	.num_parents = 1,
	.ops = &clk_alpha_pll_slew_ops,
};

static struct clk_alpha_pll gpu_cc_pll1 = {
	.offset = 0x100,
	.vco_table = gpu_cc_pll1_vco,
	.num_vco = ARRAY_SIZE(gpu_cc_pll1_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.config = &gpu_pll1_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_pll1",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 1100000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

static struct clk_fixed_factor crc_div_pll0 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "crc_div_pll0",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpu_cc_pll0.clkr.hw,
			},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_fixed_factor crc_div_pll1 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "crc_div_pll1",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpu_cc_pll1.clkr.hw,
			},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static const struct parent_map gpu_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_MAIN, 1 },
	{ P_GPU_CC_PLL1_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo"},
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .fw_name = "gpll0"},
	{ .fw_name = "gpll0_out_main_div"},
};

static const struct parent_map gpu_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_2X_CLK, 1 },
	{ P_CRC_DIV_PLL0_OUT_AUX2, 2 },
	{ P_GPU_CC_PLL1_OUT_AUX, 3 },
	{ P_CRC_DIV_PLL1_OUT_AUX2, 4 },
	{ P_GPLL0_OUT_MAIN, 5 },
};

static const struct clk_parent_data gpu_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo"},
	{ .hw = &gpu_cc_pll0.clkr.hw },
	{ .hw = &crc_div_pll0.hw },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .hw = &crc_div_pll1.hw },
	{ .fw_name = "gpll0"},
};

static const struct freq_tbl ftbl_gpu_cc_gmu_clk_src[] = {
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gmu_clk_src = {
	.cmd_rcgr = 0x1120,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_0,
	.freq_tbl = ftbl_gpu_cc_gmu_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpu_cc_gmu_clk_src",
		.parent_data = gpu_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_0),
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
	F(290000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(350000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(435000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(500000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(550000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(650000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(700000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(745000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(845000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(895000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gx_gfx3d_clk_src = {
	.cmd_rcgr = 0x101c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_1,
	.freq_tbl = ftbl_gpu_cc_gx_gfx3d_clk_src,
	.enable_safe_config = true,
	.flags = FORCE_ENABLE_RCG,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpu_cc_gx_gfx3d_clk_src",
		.parent_data = gpu_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 290000000,
			[VDD_LOW] = 435000000,
			[VDD_LOW_L1] = 550000000,
			[VDD_NOMINAL] = 700000000,
			[VDD_NOMINAL_L1] = 745000000,
			[VDD_HIGH] = 845000000,
			[VDD_HIGH_L1] = 895000000},
	},
};

static struct clk_branch gpu_cc_crc_ahb_clk = {
	.halt_reg = 0x107c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x107c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_crc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gfx3d_clk = {
	.halt_reg = 0x10a4,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x10a4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10a8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x108c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cxo_aon_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* CLK_DONT_HOLD_STATE flag is needed due to sync_state */
static struct clk_branch gpu_cc_cxo_clk = {
	.halt_reg = 0x109c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x109c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cxo_clk",
			.flags = CLK_DONT_HOLD_STATE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gx_gfx3d_clk = {
	.halt_reg = 0x1054,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x1054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
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

static struct clk_branch gpu_cc_hlos1_vote_gpu_smmu_clk = {
	.halt_reg = 0x5000,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x5000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_hlos1_vote_gpu_smmu_clk",
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
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

struct clk_hw *gpu_cc_sm6150_hws[] = {
	[CRC_DIV_PLL0] = &crc_div_pll0.hw,
	[CRC_DIV_PLL1] = &crc_div_pll1.hw,
};

static struct clk_regmap *gpu_cc_sm6150_clocks[] = {
	[GPU_CC_CRC_AHB_CLK] = &gpu_cc_crc_ahb_clk.clkr,
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
	[GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK] = &gpu_cc_hlos1_vote_gpu_smmu_clk.clkr,
	[GPU_CC_PLL0] = &gpu_cc_pll0.clkr,
	[GPU_CC_PLL1] = &gpu_cc_pll1.clkr,
	[GPU_CC_SLEEP_CLK] = &gpu_cc_sleep_clk.clkr,
};

static const struct regmap_config gpu_cc_sm6150_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x7008,
	.fast_io = true,
};

static struct critical_clk_offset critical_clk_list[] = {
	{ .offset = 0x1078, .mask = 0x1 },
	{ .offset = 0x1098, .mask = 0xff0 },
	{ .offset = 0x1028, .mask = 0x00015011 },
	{ .offset = 0x1024, .mask = 0x00800000 },
};

static struct qcom_cc_desc gpu_cc_sm6150_desc = {
	.config = &gpu_cc_sm6150_regmap_config,
	.clks = gpu_cc_sm6150_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_sm6150_clocks),
	.clk_hws = gpu_cc_sm6150_hws,
	.num_clk_hws = ARRAY_SIZE(gpu_cc_sm6150_hws),
	.critical_clk_en = critical_clk_list,
	.num_critical_clk = ARRAY_SIZE(critical_clk_list),
};

static const struct of_device_id gpu_cc_sm6150_match_table[] = {
	{ .compatible = "qcom,sm6150-gpucc" },
	{ .compatible = "qcom,sa6155-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpu_cc_sm6150_match_table);

static void configure_crc(struct regmap *regmap)
{
	unsigned int value, mask;

	/* Recommended WAKEUP/SLEEP settings for the gpu_cc_cx_gmu_clk */
	mask = 0xf << 8;
	mask |= 0xf << 4;
	value = 0xf << 8 | 0xf << 4;
	regmap_update_bits(regmap, gpu_cc_cx_gmu_clk.clkr.enable_reg,
							mask, value);

	/*
	 * After POR, Clock Ramp Controller(CRC) will be in bypass mode.
	 * Software needs to do the following operation to enable the CRC
	 * for GFX3D clock and divide the input clock by div by 2.
	 */
	regmap_update_bits(regmap, 0x1028, 0x00015011, 0x00015011);
	regmap_update_bits(regmap, 0x1024, 0x00800000, 0x00800000);
}

static void gpucc_sm6150_fixup_sa6155(struct platform_device *pdev)
{
	vdd_cx.num_levels = VDD_NUM_SA6155;
	vdd_cx.cur_level = VDD_NUM_SA6155;
	vdd_mx.num_levels = VDD_NUM_SA6155;
	vdd_mx.cur_level = VDD_NUM_SA6155;
	gpu_cc_gx_gfx3d_clk_src.clkr.vdd_data.rate_max[VDD_HIGH_L1] = 0;

	gpu_cc_pll0.clkr.hw.init = &gpu_cc_pll0_sa6155;
	gpu_cc_pll1.clkr.hw.init = &gpu_cc_pll1_sa6155;
}

static int gpu_cc_sm6150_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret, is_sa6155;

	/* Get CX voltage regulator for CX and GMU clocks. */
	vdd_cx.regulator[0] = devm_regulator_get(&pdev->dev, "vdd_cx");
	if (IS_ERR(vdd_cx.regulator[0])) {
		if (!(PTR_ERR(vdd_cx.regulator[0]) == -EPROBE_DEFER))
			dev_err(&pdev->dev,
				"Unable to get vdd_cx regulator\n");
		return PTR_ERR(vdd_cx.regulator[0]);
	}

	/* Get MX voltage regulator for GPU PLL graphic clock. */
	vdd_mx.regulator[0] = devm_regulator_get(&pdev->dev, "vdd_mx");
	if (IS_ERR(vdd_mx.regulator[0])) {
		if (!(PTR_ERR(vdd_mx.regulator[0]) == -EPROBE_DEFER))
			dev_err(&pdev->dev,
				"Unable to get vdd_mx regulator\n");
		return PTR_ERR(vdd_mx.regulator[0]);
	}

	is_sa6155 = of_device_is_compatible(pdev->dev.of_node,
						"qcom,sa6155-gpucc");
	if (is_sa6155)
		gpucc_sm6150_fixup_sa6155(pdev);

	regmap = qcom_cc_map(pdev, &gpu_cc_sm6150_desc);
	if (IS_ERR(regmap)) {
		pr_err("Failed to map the gpu_cc registers\n");
		return PTR_ERR(regmap);
	}

	clk_alpha_pll_configure(&gpu_cc_pll0, regmap, gpu_cc_pll0.config);
	clk_alpha_pll_configure(&gpu_cc_pll1, regmap, gpu_cc_pll1.config);

	/*
	 * Keep clocks always enabled:
	 *	gpu_cc_ahb_clk
	 */
	regmap_update_bits(regmap, 0x1078, BIT(0), BIT(0));

	ret = qcom_cc_really_probe(pdev, &gpu_cc_sm6150_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GPU CC clocks\n");
		return ret;
	}

	ret = register_qcom_clks_pm(pdev, false, &gpu_cc_sm6150_desc);
	if (ret)
		dev_err(&pdev->dev, "GPU CC failed to register for pm ops\n");

	configure_crc(regmap);

	dev_info(&pdev->dev, "Registered GPU CC clocks\n");

	return ret;
}

static void gpu_cc_sm6150_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &gpu_cc_sm6150_desc);
}

static struct platform_driver gpu_cc_sm6150_driver = {
	.probe = gpu_cc_sm6150_probe,
	.driver = {
		.name = "gpu_cc-sm6150",
		.of_match_table = gpu_cc_sm6150_match_table,
		.sync_state = gpu_cc_sm6150_sync_state,
	},
};

static int __init gpu_cc_sm6150_init(void)
{
	return platform_driver_register(&gpu_cc_sm6150_driver);
}
subsys_initcall(gpu_cc_sm6150_init);

static void __exit gpu_cc_sm6150_exit(void)
{
	platform_driver_unregister(&gpu_cc_sm6150_driver);
}
module_exit(gpu_cc_sm6150_exit);

MODULE_DESCRIPTION("QTI GPU_CC SM6150 Driver");
MODULE_LICENSE("GPL");
