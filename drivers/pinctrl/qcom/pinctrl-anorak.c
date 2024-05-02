// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"
#include "pinctrl-anorak.h"

static const struct msm_pinctrl_soc_data anorak_pinctrl = {
	.pins = anorak_pins,
	.npins = ARRAY_SIZE(anorak_pins),
	.functions = anorak_functions,
	.nfunctions = ARRAY_SIZE(anorak_functions),
	.groups = anorak_groups,
	.ngroups = ARRAY_SIZE(anorak_groups),
	.ngpios = 225,
	.qup_regs = anorak_qup_regs,
	.nqup_regs = ARRAY_SIZE(anorak_qup_regs),
	.wakeirq_map = anorak_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(anorak_pdc_map),
};

static int anorak_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &anorak_pinctrl);
}

static const struct of_device_id anorak_pinctrl_of_match[] = {
	{ .compatible = "qcom,anorak-pinctrl", },
	{ },
};

static struct platform_driver anorak_pinctrl_driver = {
	.driver = {
		.name = "anorak-pinctrl",
		.of_match_table = anorak_pinctrl_of_match,
	},
	.probe = anorak_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init anorak_pinctrl_init(void)
{
	return platform_driver_register(&anorak_pinctrl_driver);
}
arch_initcall(anorak_pinctrl_init);

static void __exit anorak_pinctrl_exit(void)
{
	platform_driver_unregister(&anorak_pinctrl_driver);
}
module_exit(anorak_pinctrl_exit);

MODULE_DESCRIPTION("QTI anorak pinctrl driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, anorak_pinctrl_of_match);
