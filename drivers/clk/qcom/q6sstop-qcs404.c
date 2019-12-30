// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,q6sstopcc-qcs404.h>

#include "clk-regmap.h"
#include "clk-branch.h"
#include "common.h"
#include "reset.h"

static struct clk_branch lcc_ahbfabric_cbc_clk = {
	.halt_reg = 0x1b004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1b004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lcc_ahbfabric_cbc_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lcc_q6ss_ahbs_cbc_clk = {
	.halt_reg = 0x22000,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x22000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lcc_q6ss_ahbs_cbc_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lcc_q6ss_tcm_slave_cbc_clk = {
	.halt_reg = 0x1c000,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x1c000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lcc_q6ss_tcm_slave_cbc_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lcc_q6ss_ahbm_cbc_clk = {
	.halt_reg = 0x22004,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x22004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lcc_q6ss_ahbm_cbc_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lcc_q6ss_axim_cbc_clk = {
	.halt_reg = 0x1c004,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x1c004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lcc_q6ss_axim_cbc_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lcc_q6ss_bcr_sleep_clk = {
	.halt_reg = 0x6004,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x6004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "lcc_q6ss_bcr_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

/* TCSR clock */
static struct clk_branch tcsr_lcc_csr_cbcr_clk = {
	.halt_reg = 0x8008,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x8008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "tcsr_lcc_csr_cbcr_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct regmap_config q6sstop_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.fast_io	= true,
};

static struct clk_regmap *q6sstop_qcs404_clocks[] = {
	[LCC_AHBFABRIC_CBC_CLK] = &lcc_ahbfabric_cbc_clk.clkr,
	[LCC_Q6SS_AHBS_CBC_CLK] = &lcc_q6ss_ahbs_cbc_clk.clkr,
	[LCC_Q6SS_TCM_SLAVE_CBC_CLK] = &lcc_q6ss_tcm_slave_cbc_clk.clkr,
	[LCC_Q6SS_AHBM_CBC_CLK] = &lcc_q6ss_ahbm_cbc_clk.clkr,
	[LCC_Q6SS_AXIM_CBC_CLK] = &lcc_q6ss_axim_cbc_clk.clkr,
	[LCC_Q6SS_BCR_SLEEP_CLK] = &lcc_q6ss_bcr_sleep_clk.clkr,
};

static const struct qcom_reset_map q6sstop_qcs404_resets[] = {
	[Q6SSTOP_BCR_RESET] = { 0x6000 },
};

static const struct qcom_cc_desc q6sstop_qcs404_desc = {
	.config = &q6sstop_regmap_config,
	.clks = q6sstop_qcs404_clocks,
	.num_clks = ARRAY_SIZE(q6sstop_qcs404_clocks),
	.resets = q6sstop_qcs404_resets,
	.num_resets = ARRAY_SIZE(q6sstop_qcs404_resets),
};

static struct clk_regmap *tcsr_qcs404_clocks[] = {
	[TCSR_Q6SS_LCC_CBCR_CLK] = &tcsr_lcc_csr_cbcr_clk.clkr,
};

static const struct qcom_cc_desc tcsr_qcs404_desc = {
	.config = &q6sstop_regmap_config,
	.clks = tcsr_qcs404_clocks,
	.num_clks = ARRAY_SIZE(tcsr_qcs404_clocks),
};

static const struct of_device_id q6sstopcc_qcs404_match_table[] = {
	{ .compatible = "qcom,qcs404-q6sstopcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, q6sstopcc_qcs404_match_table);

static int q6sstopcc_qcs404_probe(struct platform_device *pdev)
{
	const struct qcom_cc_desc *desc;
	int ret;

	pm_runtime_enable(&pdev->dev);
	ret = pm_clk_create(&pdev->dev);
	if (ret)
		goto disable_pm_runtime;

	ret = pm_clk_add(&pdev->dev, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to acquire iface clock\n");
		goto destroy_pm_clk;
	}

	q6sstop_regmap_config.name = "q6sstop_tcsr";
	desc = &tcsr_qcs404_desc;

	ret = qcom_cc_probe_by_index(pdev, 1, desc);
	if (ret)
		goto destroy_pm_clk;

	q6sstop_regmap_config.name = "q6sstop_cc";
	desc = &q6sstop_qcs404_desc;

	ret = qcom_cc_probe_by_index(pdev, 0, desc);
	if (ret)
		goto destroy_pm_clk;

	return 0;

destroy_pm_clk:
	pm_clk_destroy(&pdev->dev);

disable_pm_runtime:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int q6sstopcc_qcs404_remove(struct platform_device *pdev)
{
	pm_clk_destroy(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops q6sstopcc_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static struct platform_driver q6sstopcc_qcs404_driver = {
	.probe		= q6sstopcc_qcs404_probe,
	.remove		= q6sstopcc_qcs404_remove,
	.driver		= {
		.name	= "qcs404-q6sstopcc",
		.of_match_table = q6sstopcc_qcs404_match_table,
		.pm = &q6sstopcc_pm_ops,
	},
};

module_platform_driver(q6sstopcc_qcs404_driver);

MODULE_DESCRIPTION("QTI QCS404 Q6SSTOP Clock Controller Driver");
MODULE_LICENSE("GPL v2");
