/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spmi.h>
#include <linux/regmap.h>
#include <linux/of_platform.h>

static const struct regmap_config spmi_regmap_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xffff,
	.fast_io	= true,
};

static int pmic_spmi_probe(struct spmi_device *sdev)
{
	struct device_node *root = sdev->dev.of_node;
	struct regmap *regmap;

	regmap = devm_regmap_init_spmi_ext(sdev, &spmi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return of_platform_populate(root, NULL, NULL, &sdev->dev);
}

static void pmic_spmi_remove(struct spmi_device *sdev)
{
	of_platform_depopulate(&sdev->dev);
}

static const struct of_device_id pmic_spmi_id_table[] = {
	{ .compatible = "qcom,spmi-pmic" },
	{ .compatible = "qcom,pm8941" },
	{ .compatible = "qcom,pm8841" },
	{ .compatible = "qcom,pma8084" },
	{ }
};
MODULE_DEVICE_TABLE(of, pmic_spmi_id_table);

static struct spmi_driver pmic_spmi_driver = {
	.probe = pmic_spmi_probe,
	.remove = pmic_spmi_remove,
	.driver = {
		.name = "pmic-spmi",
		.of_match_table = pmic_spmi_id_table,
	},
};
module_spmi_driver(pmic_spmi_driver);

MODULE_DESCRIPTION("Qualcomm SPMI PMIC driver");
MODULE_ALIAS("spmi:spmi-pmic");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Josh Cartwright <joshc@codeaurora.org>");
MODULE_AUTHOR("Stanimir Varbanov <svarbanov@mm-sol.com>");
