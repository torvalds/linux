/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2012 ARM Limited
 */

#define DRVNAME "vexpress-regulator"
#define pr_fmt(fmt) DRVNAME ": " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/vexpress.h>

struct vexpress_regulator {
	struct regulator_desc desc;
	struct regulator_dev *regdev;
	struct regmap *regmap;
};

static int vexpress_regulator_get_voltage(struct regulator_dev *regdev)
{
	struct vexpress_regulator *reg = rdev_get_drvdata(regdev);
	u32 uV;
	int err = regmap_read(reg->regmap, 0, &uV);

	return err ? err : uV;
}

static int vexpress_regulator_set_voltage(struct regulator_dev *regdev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct vexpress_regulator *reg = rdev_get_drvdata(regdev);

	return regmap_write(reg->regmap, 0, min_uV);
}

static struct regulator_ops vexpress_regulator_ops_ro = {
	.get_voltage = vexpress_regulator_get_voltage,
};

static struct regulator_ops vexpress_regulator_ops = {
	.get_voltage = vexpress_regulator_get_voltage,
	.set_voltage = vexpress_regulator_set_voltage,
};

static int vexpress_regulator_probe(struct platform_device *pdev)
{
	struct vexpress_regulator *reg;
	struct regulator_init_data *init_data;
	struct regulator_config config = { };

	reg = devm_kzalloc(&pdev->dev, sizeof(*reg), GFP_KERNEL);
	if (!reg)
		return -ENOMEM;

	reg->regmap = devm_regmap_init_vexpress_config(&pdev->dev);
	if (IS_ERR(reg->regmap))
		return PTR_ERR(reg->regmap);

	reg->desc.name = dev_name(&pdev->dev);
	reg->desc.type = REGULATOR_VOLTAGE;
	reg->desc.owner = THIS_MODULE;
	reg->desc.continuous_voltage_range = true;

	init_data = of_get_regulator_init_data(&pdev->dev, pdev->dev.of_node,
					       &reg->desc);
	if (!init_data)
		return -EINVAL;

	init_data->constraints.apply_uV = 0;
	if (init_data->constraints.min_uV && init_data->constraints.max_uV)
		reg->desc.ops = &vexpress_regulator_ops;
	else
		reg->desc.ops = &vexpress_regulator_ops_ro;

	config.dev = &pdev->dev;
	config.init_data = init_data;
	config.driver_data = reg;
	config.of_node = pdev->dev.of_node;

	reg->regdev = devm_regulator_register(&pdev->dev, &reg->desc, &config);
	if (IS_ERR(reg->regdev))
		return PTR_ERR(reg->regdev);

	platform_set_drvdata(pdev, reg);

	return 0;
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
		.of_match_table = vexpress_regulator_of_match,
	},
};

module_platform_driver(vexpress_regulator_driver);

MODULE_AUTHOR("Pawel Moll <pawel.moll@arm.com>");
MODULE_DESCRIPTION("Versatile Express regulator");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:vexpress-regulator");
