// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, Linaro Ltd.
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,turingcc-qcs404.h>

#include "clk-regmap.h"
#include "clk-branch.h"
#include "common.h"
#include "reset.h"

static struct clk_branch turing_wrapper_aon_cbcr = {
	.halt_reg = 0x5098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data) {
			.name = "turing_wrapper_aon_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch turing_q6ss_ahbm_aon_cbcr = {
	.halt_reg = 0x9000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data) {
			.name = "turing_q6ss_ahbm_aon_cbcr",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch turing_q6ss_q6_axim_clk = {
	.halt_reg = 0xb000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data) {
			.name = "turing_q6ss_q6_axim_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch turing_q6ss_ahbs_aon_cbcr = {
	.halt_reg = 0x10000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data) {
			.name = "turing_q6ss_ahbs_aon_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch turing_wrapper_qos_ahbs_aon_cbcr = {
	.halt_reg = 0x11014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data) {
			.name = "turing_wrapper_qos_ahbs_aon_clk",
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_regmap *turingcc_clocks[] = {
	[TURING_WRAPPER_AON_CLK] = &turing_wrapper_aon_cbcr.clkr,
	[TURING_Q6SS_AHBM_AON_CLK] = &turing_q6ss_ahbm_aon_cbcr.clkr,
	[TURING_Q6SS_Q6_AXIM_CLK] = &turing_q6ss_q6_axim_clk.clkr,
	[TURING_Q6SS_AHBS_AON_CLK] = &turing_q6ss_ahbs_aon_cbcr.clkr,
	[TURING_WRAPPER_QOS_AHBS_AON_CLK] = &turing_wrapper_qos_ahbs_aon_cbcr.clkr,
};

static const struct regmap_config turingcc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x23004,
	.fast_io	= true,
};

static const struct qcom_cc_desc turingcc_desc = {
	.config = &turingcc_regmap_config,
	.clks = turingcc_clocks,
	.num_clks = ARRAY_SIZE(turingcc_clocks),
};

static int turingcc_probe(struct platform_device *pdev)
{
	int ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = devm_pm_clk_create(&pdev->dev);
	if (ret)
		return ret;

	ret = pm_clk_add(&pdev->dev, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to acquire iface clock\n");
		return ret;
	}

	ret = qcom_cc_probe(pdev, &turingcc_desc);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct dev_pm_ops turingcc_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static const struct of_device_id turingcc_match_table[] = {
	{ .compatible = "qcom,qcs404-turingcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, turingcc_match_table);

static struct platform_driver turingcc_driver = {
	.probe		= turingcc_probe,
	.driver		= {
		.name	= "qcs404-turingcc",
		.of_match_table = turingcc_match_table,
		.pm = &turingcc_pm_ops,
	},
};

module_platform_driver(turingcc_driver);

MODULE_DESCRIPTION("Qualcomm QCS404 Turing Clock Controller");
MODULE_LICENSE("GPL v2");
