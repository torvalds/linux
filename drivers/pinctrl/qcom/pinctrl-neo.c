// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"
#include "pinctrl-neo.h"

static const struct msm_pinctrl_soc_data neo_pinctrl = {
	.pins = neo_pins,
	.npins = ARRAY_SIZE(neo_pins),
	.functions = neo_functions,
	.nfunctions = ARRAY_SIZE(neo_functions),
	.groups = neo_groups,
	.ngroups = ARRAY_SIZE(neo_groups),
	.ngpios = 156,
	.qup_regs = neo_qup_regs,
	.nqup_regs = ARRAY_SIZE(neo_qup_regs),
	.wakeirq_map = neo_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(neo_pdc_map),
};

static int neo_pinctrl_probe(struct platform_device *pdev)
{
	const struct msm_pinctrl_soc_data *pinctrl_data;

	pinctrl_data = of_device_get_match_data(&pdev->dev);
	if (!pinctrl_data)
		return -EINVAL;

	return msm_pinctrl_probe(pdev, pinctrl_data);
}

static const struct of_device_id neo_pinctrl_of_match[] = {
	{ .compatible = "qcom,neo-pinctrl", .data = &neo_pinctrl},
	{ },
};

static struct platform_driver neo_pinctrl_driver = {
	.driver = {
		.name = "neo-pinctrl",
		.of_match_table = neo_pinctrl_of_match,
	},
	.probe = neo_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init neo_pinctrl_init(void)
{
	return platform_driver_register(&neo_pinctrl_driver);
}
arch_initcall(neo_pinctrl_init);

static void __exit neo_pinctrl_exit(void)
{
	platform_driver_unregister(&neo_pinctrl_driver);
}
module_exit(neo_pinctrl_exit);

MODULE_DESCRIPTION("QTI neo pinctrl driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, neo_pinctrl_of_match);
MODULE_SOFTDEP("pre: qcom_tlmm_vm_irqchip");
