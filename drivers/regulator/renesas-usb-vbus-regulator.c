// SPDX-License-Identifier: GPL-2.0
//
// Renesas USB VBUS output regulator driver
//
// Copyright (C) 2024 Renesas Electronics Corporation
//

#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

static const struct regulator_ops rzg2l_usb_vbus_reg_ops = {
	.enable     = regulator_enable_regmap,
	.disable    = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_desc rzg2l_usb_vbus_rdesc = {
	.name = "vbus",
	.of_match = of_match_ptr("regulator-vbus"),
	.ops = &rzg2l_usb_vbus_reg_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.enable_reg  = 0,
	.enable_mask = BIT(0),
	.enable_is_inverted = true,
	.fixed_uV	= 5000000,
	.n_voltages	= 1,
};

static int rzg2l_usb_vbus_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config config = { };
	struct device *dev = &pdev->dev;
	struct regulator_dev *rdev;

	config.regmap = dev_get_regmap(dev->parent, NULL);
	if (!config.regmap)
		return dev_err_probe(dev, -ENOENT, "Failed to get regmap\n");

	config.dev = dev;
	config.of_node = of_get_child_by_name(dev->parent->of_node, "regulator-vbus");
	if (!config.of_node)
		return dev_err_probe(dev, -ENODEV, "regulator node not found\n");

	rdev = devm_regulator_register(dev, &rzg2l_usb_vbus_rdesc, &config);
	if (IS_ERR(rdev)) {
		of_node_put(config.of_node);
		return dev_err_probe(dev, PTR_ERR(rdev),
				     "not able to register vbus regulator\n");
	}

	of_node_put(config.of_node);

	return 0;
}

static struct platform_driver rzg2l_usb_vbus_regulator_driver = {
	.probe = rzg2l_usb_vbus_regulator_probe,
	.driver	= {
		.name = "rzg2l-usb-vbus-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_platform_driver(rzg2l_usb_vbus_regulator_driver);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L USB Vbus Regulator Driver");
MODULE_LICENSE("GPL");
