// SPDX-License-Identifier: GPL-2.0+
//
// Functions to access SY3686A power management chip voltages
//
// Copyright (C) 2019 reMarkable AS - http://www.remarkable.com/
//
// Authors: Lars Ivar Miljeteig <lars.ivar.miljeteig@remarkable.com>
//          Alistair Francis <alistair@alistair23.me>

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/mfd/sy7636a.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>

struct sy7636a_data {
	struct regmap *regmap;
	struct gpio_desc *pgood_gpio;
};

static int sy7636a_get_vcom_voltage_op(struct regulator_dev *rdev)
{
	int ret;
	unsigned int val, val_h;

	ret = regmap_read(rdev->regmap, SY7636A_REG_VCOM_ADJUST_CTRL_L, &val);
	if (ret)
		return ret;

	ret = regmap_read(rdev->regmap, SY7636A_REG_VCOM_ADJUST_CTRL_H, &val_h);
	if (ret)
		return ret;

	val |= (val_h << VCOM_ADJUST_CTRL_SHIFT);

	return (val & VCOM_ADJUST_CTRL_MASK) * VCOM_ADJUST_CTRL_SCAL;
}

static int sy7636a_get_status(struct regulator_dev *rdev)
{
	struct sy7636a_data *data = dev_get_drvdata(rdev->dev.parent);
	int ret = 0;

	ret = gpiod_get_value_cansleep(data->pgood_gpio);
	if (ret < 0)
		dev_err(&rdev->dev, "Failed to read pgood gpio: %d\n", ret);

	return ret;
}

static const struct regulator_ops sy7636a_vcom_volt_ops = {
	.get_voltage = sy7636a_get_vcom_voltage_op,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = sy7636a_get_status,
};

static const struct regulator_desc desc = {
	.name = "vcom",
	.id = 0,
	.ops = &sy7636a_vcom_volt_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.enable_reg = SY7636A_REG_OPERATION_MODE_CRL,
	.enable_mask = SY7636A_OPERATION_MODE_CRL_ONOFF,
	.regulators_node = of_match_ptr("regulators"),
	.of_match = of_match_ptr("vcom"),
};

static int sy7636a_regulator_probe(struct platform_device *pdev)
{
	struct regmap *regmap = dev_get_regmap(pdev->dev.parent, NULL);
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct gpio_desc *gdp;
	struct sy7636a_data *data;
	int ret;

	if (!regmap)
		return -EPROBE_DEFER;

	gdp = devm_gpiod_get(pdev->dev.parent, "epd-pwr-good", GPIOD_IN);
	if (IS_ERR(gdp)) {
		dev_err(pdev->dev.parent, "Power good GPIO fault %ld\n", PTR_ERR(gdp));
		return PTR_ERR(gdp);
	}

	data = devm_kzalloc(&pdev->dev, sizeof(struct sy7636a_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = regmap;
	data->pgood_gpio = gdp;

	platform_set_drvdata(pdev, data);

	ret = regmap_write(regmap, SY7636A_REG_POWER_ON_DELAY_TIME, 0x0);
	if (ret) {
		dev_err(pdev->dev.parent, "Failed to initialize regulator: %d\n", ret);
		return ret;
	}

	config.dev = &pdev->dev;
	config.dev->of_node = pdev->dev.parent->of_node;
	config.regmap = regmap;

	rdev = devm_regulator_register(&pdev->dev, &desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(pdev->dev.parent, "Failed to register %s regulator\n",
			pdev->name);
		return PTR_ERR(rdev);
	}

	return 0;
}

static const struct platform_device_id sy7636a_regulator_id_table[] = {
	{ "sy7636a-regulator", },
	{ }
};
MODULE_DEVICE_TABLE(platform, sy7636a_regulator_id_table);

static struct platform_driver sy7636a_regulator_driver = {
	.driver = {
		.name = "sy7636a-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = sy7636a_regulator_probe,
	.id_table = sy7636a_regulator_id_table,
};
module_platform_driver(sy7636a_regulator_driver);

MODULE_AUTHOR("Lars Ivar Miljeteig <lars.ivar.miljeteig@remarkable.com>");
MODULE_DESCRIPTION("SY7636A voltage regulator driver");
MODULE_LICENSE("GPL v2");
