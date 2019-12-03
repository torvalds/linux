// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gpucc-sdm845.h>

#include "common.h"
#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "gdsc.h"

#define CX_GMU_CBCR_SLEEP_MASK		0xf
#define CX_GMU_CBCR_SLEEP_SHIFT		4
#define CX_GMU_CBCR_WAKE_MASK		0xf
#define CX_GMU_CBCR_WAKE_SHIFT		8
#define CLK_DIS_WAIT_SHIFT		12
#define CLK_DIS_WAIT_MASK		(0xf << CLK_DIS_WAIT_SHIFT)

enum {
	P_BI_TCXO,
	P_CORE_BI_PLL_TEST_SE,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_MAIN_DIV,
	P_GPU_CC_PLL1_OUT_EVEN,
	P_GPU_CC_PLL1_OUT_MAIN,
	P_GPU_CC_PLL1_OUT_ODD,
};

static const struct parent_map gpu_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL1_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gpu_cc_parent_names_0[] = {
	"bi_tcxo",
	"gpu_cc_pll1",
	"gcc_gpu_gpll0_clk_src",
	"gcc_gpu_gpll0_div_clk_src",
	"core_bi_pll_test_se",
};

static const struct alpha_pll_config gpu_cc_pll1_config = {
	.l = 0x1a,
	.alpha = 0xaab,
};

static struct clk_alpha_pll gpu_cc_pll1 = {
	.offset = 0x100,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_pll1",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_fabia_ops,
		},
	},
};

static const struct freq_tbl ftbl_gpu_cc_gmu_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN_DIV, 1.5, 0, 0),
	F(500000000, P_GPU_CC_PLL1_OUT_MAIN, 1, 0, 0),
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
		.parent_names = gpu_cc_parent_names_0,
		.num_parents = 5,
		.ops = &clk_rcg2_shared_ops,
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
			.parent_names = (const char *[]){
				"gpu_cc_gmu_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
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

static struct gdsc gpu_cx_gdsc = {
	.gdscr = 0x106c,
	.gds_hw_ctrl = 0x1540,
	.pd = {
		.name = "gpu_cx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

/*
 * On SDM845 the GPU GX domain is *almost* entirely controlled by the GMU
 * running in the CX domain so the CPU doesn't need to know anything about the
 * GX domain EXCEPT....
 *
 * Hardware constraints dictate that the GX be powered down before the CX. If
 * the GMU crashes it could leave the GX on. In order to successfully bring back
 * the device the CPU needs to disable the GX headswitch. There being no sane
 * way to reach in and touch that register from deep inside the GPU driver we
 * need to set up the infrastructure to be able to ensure that the GPU can
 * ensure that the GX is off during this super special case. We do this by
 * defining a GX gdsc with a dummy enable function and a "default" disable
 * function.
 *
 * This allows us to attach with genpd_dev_pm_attach_by_name() in the GPU
 * driver. During power up, nothing will happen from the CPU (and the GMU will
 * power up normally but during power down this will ensure that the GX domain
 * is *really* off - this gives us a semi standard way of doing what we need.
 */
static int gx_gdsc_enable(struct generic_pm_domain *domain)
{
	/* Do nothing but give genpd the impression that we were successful */
	return 0;
}

static struct gdsc gpu_gx_gdsc = {
	.gdscr = 0x100c,
	.clamp_io_ctrl = 0x1508,
	.pd = {
		.name = "gpu_gx_gdsc",
		.power_on = gx_gdsc_enable,
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = CLAMP_IO | AON_RESET | POLL_CFG_GDSCR,
};

static struct clk_regmap *gpu_cc_sdm845_clocks[] = {
	[GPU_CC_CXO_CLK] = &gpu_cc_cxo_clk.clkr,
	[GPU_CC_CX_GMU_CLK] = &gpu_cc_cx_gmu_clk.clkr,
	[GPU_CC_GMU_CLK_SRC] = &gpu_cc_gmu_clk_src.clkr,
	[GPU_CC_PLL1] = &gpu_cc_pll1.clkr,
};

static struct gdsc *gpu_cc_sdm845_gdscs[] = {
	[GPU_CX_GDSC] = &gpu_cx_gdsc,
	[GPU_GX_GDSC] = &gpu_gx_gdsc,
};

static const struct regmap_config gpu_cc_sdm845_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x8008,
	.fast_io	= true,
};

static const struct qcom_cc_desc gpu_cc_sdm845_desc = {
	.config = &gpu_cc_sdm845_regmap_config,
	.clks = gpu_cc_sdm845_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_sdm845_clocks),
	.gdscs = gpu_cc_sdm845_gdscs,
	.num_gdscs = ARRAY_SIZE(gpu_cc_sdm845_gdscs),
};

static const struct of_device_id gpu_cc_sdm845_match_table[] = {
	{ .compatible = "qcom,sdm845-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpu_cc_sdm845_match_table);

static int gpu_cc_sdm845_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	unsigned int value, mask;

	regmap = qcom_cc_map(pdev, &gpu_cc_sdm845_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_fabia_pll_configure(&gpu_cc_pll1, regmap, &gpu_cc_pll1_config);

	/*
	 * Configure gpu_cc_cx_gmu_clk with recommended
	 * wakeup/sleep settings
	 */
	mask = CX_GMU_CBCR_WAKE_MASK << CX_GMU_CBCR_WAKE_SHIFT;
	mask |= CX_GMU_CBCR_SLEEP_MASK << CX_GMU_CBCR_SLEEP_SHIFT;
	value = 0xf << CX_GMU_CBCR_WAKE_SHIFT | 0xf << CX_GMU_CBCR_SLEEP_SHIFT;
	regmap_update_bits(regmap, 0x1098, mask, value);

	/* Configure clk_dis_wait for gpu_cx_gdsc */
	regmap_update_bits(regmap, 0x106c, CLK_DIS_WAIT_MASK,
						8 << CLK_DIS_WAIT_SHIFT);

	return qcom_cc_really_probe(pdev, &gpu_cc_sdm845_desc, regmap);
}

static struct platform_driver gpu_cc_sdm845_driver = {
	.probe = gpu_cc_sdm845_probe,
	.driver = {
		.name = "sdm845-gpucc",
		.of_match_table = gpu_cc_sdm845_match_table,
		.sync_state = clk_sync_state,
	},
};

static int __init gpu_cc_sdm845_init(void)
{
	return platform_driver_register(&gpu_cc_sdm845_driver);
}
subsys_initcall(gpu_cc_sdm845_init);

static void __exit gpu_cc_sdm845_exit(void)
{
	platform_driver_unregister(&gpu_cc_sdm845_driver);
}
module_exit(gpu_cc_sdm845_exit);

MODULE_DESCRIPTION("QTI GPUCC SDM845 Driver");
MODULE_LICENSE("GPL v2");
