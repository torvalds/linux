// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2012 ARM Limited

#define DRVNAME "vexpress-regulator"
#define pr_fmt(fmt) DRVNAME ": " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/vexpress.h>

static int vexpress_regulator_get_voltage(struct regulator_dev *regdev)
{
	unsigned int uV;
	int err = regmap_read(regdev->regmap, 0, &uV);

	return err ? err : uV;
}

static int vexpress_regulator_set_voltage(struct regulator_dev *regdev,
		int min_uV, int max_uV, unsigned *selector)
{
	return regmap_write(regdev->regmap, 0, min_uV);
}

static const struct regulator_ops vexpress_regulator_ops_ro = {
	.get_voltage = vexpress_regulator_get_voltage,
};

static const struct regulator_ops vexpress_regulator_ops = {
	.get_voltage = vexpress_regulator_get_voltage,
	.set_voltage = vexpress_regulator_set_voltage,
};

static int vexpress_regulator_probe(struct platform_device *pdev)
{
	struct regulator_desc *desc;
	struct regulator_init_data *init_data;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct regmap *regmap;

	desc = devm_kzalloc(&pdev->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	regmap = devm_regmap_init_vexpress_config(&pdev->dev);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	desc->name = dev_name(&pdev->dev);
	desc->type = REGULATOR_VOLTAGE;
	desc->owner = THIS_MODULE;
	desc->continuous_voltage_range = true;

	init_data = of_get_regulator_init_data(&pdev->dev, pdev->dev.of_node,
					       desc);
	if (!init_data)
		return -EINVAL;

	init_data->constraints.apply_uV = 0;
	if (init_data->constraints.min_uV && init_data->constraints.max_uV)
		desc->ops = &vexpress_regulator_ops;
	else
		desc->ops = &vexpress_regulator_ops_ro;

	config.regmap = regmap;
	config.dev = &pdev->dev;
	config.init_data = init_data;
	config.of_node = pdev->dev.of_node;

	rdev = devm_regulator_register(&pdev->dev, desc, &config);
	return PTR_ERR_OR_ZERO(rdev);
}

static const struct of_device_id vexpress_regulator_of_match[] = {
	{ .compatible = "arm,vexpress-volt", },
	{ }
};
MODULE_DEVICE_TABLE(of, vexpress_regulator_of_match);

static struct platform_driver vexpress_regulator_driver = {
	.probe = vexpress_regulator_probe,
	.driver	= {
		.name = DRVNAME,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = vexpress_regulator_of_match,
	},
};

module_platform_driver(vexpress_regulator_driver);

MODULE_AUTHOR("Pawel Moll <pawel.moll@arm.com>");
MODULE_DESCRIPTION("Versatile Express regulator");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:vexpress-regulator");
