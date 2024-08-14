// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Linaro Limited
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,qcm2290-gpucc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-phy-mux.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_GCC_AHB_CLK,
	DT_BI_TCXO,
	DT_GCC_GPU_GPLL0_CLK_SRC,
	DT_GCC_GPU_GPLL0_DIV_CLK_SRC,
};

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_MAIN_DIV,
	P_GPU_CC_PLL0_2X_DIV_CLK_SRC,
	P_GPU_CC_PLL0_OUT_AUX,
	P_GPU_CC_PLL0_OUT_AUX2,
	P_GPU_CC_PLL0_OUT_MAIN,
};

static const struct pll_vco huayra_vco[] = {
	{ 600000000, 3300000000, 0 },
	{ 600000000, 2200000000, 1 },
};

static const struct alpha_pll_config gpu_cc_pll0_config = {
	.l = 0x25,
	.config_ctl_val = 0x200d4828,
	.config_ctl_hi_val = 0x6,
	.test_ctl_val = GENMASK(28, 26),
	.test_ctl_hi_val = BIT(14),
	.user_ctl_val = 0xf,
};

static struct clk_alpha_pll gpu_cc_pll0 = {
	.offset = 0x0,
	.vco_table = huayra_vco,
	.num_vco = ARRAY_SIZE(huayra_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_HUAYRA_2290],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_pll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_huayra_ops,
		},
	},
};

static const struct parent_map gpu_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_MAIN, 1 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_0[] = {
	{ .index = DT_BI_TCXO, },
	{ .hw = &gpu_cc_pll0.clkr.hw, },
	{ .index = DT_GCC_GPU_GPLL0_CLK_SRC, },
	{ .index = DT_GCC_GPU_GPLL0_DIV_CLK_SRC, },
};

static const struct parent_map gpu_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_2X_DIV_CLK_SRC, 1 },
	{ P_GPU_CC_PLL0_OUT_AUX2, 2 },
	{ P_GPU_CC_PLL0_OUT_AUX, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
};

static const struct clk_parent_data gpu_cc_parent_data_1[] = {
	{ .index = DT_BI_TCXO, },
	{ .hw = &gpu_cc_pll0.clkr.hw, },
	{ .hw = &gpu_cc_pll0.clkr.hw, },
	{ .hw = &gpu_cc_pll0.clkr.hw, },
	{ .index = DT_GCC_GPU_GPLL0_CLK_SRC, },
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
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpu_cc_gmu_clk_src",
		.parent_data = gpu_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gpu_cc_gx_gfx3d_clk_src[] = {
	F(355200000, P_GPU_CC_PLL0_OUT_AUX, 2, 0, 0),
	F(537600000, P_GPU_CC_PLL0_OUT_AUX2, 2, 0, 0),
	F(672000000, P_GPU_CC_PLL0_OUT_AUX2, 2, 0, 0),
	F(844800000, P_GPU_CC_PLL0_OUT_AUX2, 2, 0, 0),
	F(921600000, P_GPU_CC_PLL0_OUT_AUX2, 2, 0, 0),
	F(1017600000, P_GPU_CC_PLL0_OUT_AUX2, 2, 0, 0),
	F(1123200000, P_GPU_CC_PLL0_OUT_AUX2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gx_gfx3d_clk_src = {
	.cmd_rcgr = 0x101c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_1,
	.freq_tbl = ftbl_gpu_cc_gx_gfx3d_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpu_cc_gx_gfx3d_clk_src",
		.parent_data = gpu_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gpu_cc_ahb_clk = {
	.halt_reg = 0x1078,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1078,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_crc_ahb_clk = {
	.halt_reg = 0x107c,
	.halt_check = BRANCH_HALT_DELAY,
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
	.halt_check = BRANCH_HALT_DELAY,
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
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
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
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cxo_clk",
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

static struct clk_branch gpu_cc_sleep_clk = {
	.halt_reg = 0x1090,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x1090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_sleep_clk",
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

static struct gdsc gpu_cx_gdsc = {
	.gdscr = 0x106c,
	.gds_hw_ctrl = 0x1540,
	.pd = {
		.name = "gpu_cx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc gpu_gx_gdsc = {
	.gdscr = 0x100c,
	.clamp_io_ctrl = 0x1508,
	.resets = (unsigned int []){ GPU_GX_BCR },
	.reset_count = 1,
	.pd = {
		.name = "gpu_gx_gdsc",
	},
	.parent = &gpu_cx_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
	.flags = CLAMP_IO | AON_RESET | SW_RESET,
};

static struct clk_regmap *gpu_cc_qcm2290_clocks[] = {
	[GPU_CC_AHB_CLK] = &gpu_cc_ahb_clk.clkr,
	[GPU_CC_CRC_AHB_CLK] = &gpu_cc_crc_ahb_clk.clkr,
	[GPU_CC_CX_GFX3D_CLK] = &gpu_cc_cx_gfx3d_clk.clkr,
	[GPU_CC_CX_GMU_CLK] = &gpu_cc_cx_gmu_clk.clkr,
	[GPU_CC_CX_SNOC_DVM_CLK] = &gpu_cc_cx_snoc_dvm_clk.clkr,
	[GPU_CC_CXO_AON_CLK] = &gpu_cc_cxo_aon_clk.clkr,
	[GPU_CC_CXO_CLK] = &gpu_cc_cxo_clk.clkr,
	[GPU_CC_GMU_CLK_SRC] = &gpu_cc_gmu_clk_src.clkr,
	[GPU_CC_GX_GFX3D_CLK] = &gpu_cc_gx_gfx3d_clk.clkr,
	[GPU_CC_GX_GFX3D_CLK_SRC] = &gpu_cc_gx_gfx3d_clk_src.clkr,
	[GPU_CC_PLL0] = &gpu_cc_pll0.clkr,
	[GPU_CC_SLEEP_CLK] = &gpu_cc_sleep_clk.clkr,
	[GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK] = &gpu_cc_hlos1_vote_gpu_smmu_clk.clkr,
};

static const struct qcom_reset_map gpu_cc_qcm2290_resets[] = {
	[GPU_GX_BCR] = { 0x1008 },
};

static struct gdsc *gpu_cc_qcm2290_gdscs[] = {
	[GPU_CX_GDSC] = &gpu_cx_gdsc,
	[GPU_GX_GDSC] = &gpu_gx_gdsc,
};

static const struct regmap_config gpu_cc_qcm2290_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x9000,
	.fast_io = true,
};


static const struct qcom_cc_desc gpu_cc_qcm2290_desc = {
	.config = &gpu_cc_qcm2290_regmap_config,
	.clks = gpu_cc_qcm2290_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_qcm2290_clocks),
	.resets = gpu_cc_qcm2290_resets,
	.num_resets = ARRAY_SIZE(gpu_cc_qcm2290_resets),
	.gdscs = gpu_cc_qcm2290_gdscs,
	.num_gdscs = ARRAY_SIZE(gpu_cc_qcm2290_gdscs),
};

static const struct of_device_id gpu_cc_qcm2290_match_table[] = {
	{ .compatible = "qcom,qcm2290-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpu_cc_qcm2290_match_table);

static int gpu_cc_qcm2290_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gpu_cc_qcm2290_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = devm_pm_clk_create(&pdev->dev);
	if (ret)
		return ret;

	ret = pm_clk_add(&pdev->dev, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to acquire ahb clock\n");
		return ret;
	}

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		return ret;

	clk_huayra_2290_pll_configure(&gpu_cc_pll0, regmap, &gpu_cc_pll0_config);

	regmap_update_bits(regmap, 0x1060, BIT(0), BIT(0)); /* GPU_CC_GX_CXO_CLK */

	ret = qcom_cc_really_probe(&pdev->dev, &gpu_cc_qcm2290_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register display clock controller\n");
		goto out_pm_runtime_put;
	}

out_pm_runtime_put:
	pm_runtime_put_sync(&pdev->dev);

	return 0;
}

static struct platform_driver gpu_cc_qcm2290_driver = {
	.probe = gpu_cc_qcm2290_probe,
	.driver = {
		.name = "gpucc-qcm2290",
		.of_match_table = gpu_cc_qcm2290_match_table,
	},
};
module_platform_driver(gpu_cc_qcm2290_driver);

MODULE_DESCRIPTION("QTI QCM2290 GPU clock controller driver");
MODULE_LICENSE("GPL");
