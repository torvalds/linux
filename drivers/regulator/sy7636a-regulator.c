// SPDX-License-Identifier: GPL-2.0+
//
// Functions to access SY3686A power management chip voltages
//
// Copyright (C) 2019 reMarkable AS - http://www.remarkable.com/
//
// Authors: Lars Ivar Miljeteig <lars.ivar.miljeteig@remarkable.com>
//          Alistair Francis <alistair@alistair23.me>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/sy7636a.h>

#define SY7636A_POLL_ENABLED_TIME 500

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
	struct sy7636a *sy7636a = rdev_get_drvdata(rdev);
	int ret = 0;

	ret = gpiod_get_value_cansleep(sy7636a->pgood_gpio);
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
	.poll_enabled_time = SY7636A_POLL_ENABLED_TIME,
	.regulators_node = of_match_ptr("regulators"),
	.of_match = of_match_ptr("vcom"),
};

static int sy7636a_regulator_probe(struct platform_device *pdev)
{
	struct sy7636a *sy7636a = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct gpio_desc *gdp;
	int ret;

	if (!sy7636a)
		return -EPROBE_DEFER;

	platform_set_drvdata(pdev, sy7636a);

	gdp = devm_gpiod_get(sy7636a->dev, "epd-pwr-good", GPIOD_IN);
	if (IS_ERR(gdp)) {
		dev_err(sy7636a->dev, "Power good GPIO fault %ld\n", PTR_ERR(gdp));
		return PTR_ERR(gdp);
	}

	sy7636a->pgood_gpio = gdp;

	ret = regmap_write(sy7636a->regmap, SY7636A_REG_POWER_ON_DELAY_TIME, 0x0);
	if (ret) {
		dev_err(sy7636a->dev, "Failed to initialize regulator: %d\n", ret);
		return ret;
	}

	config.dev = &pdev->dev;
	config.dev->of_node = sy7636a->dev->of_node;
	config.driver_data = sy7636a;
	config.regmap = sy7636a->regmap;

	rdev = devm_regulator_register(&pdev->dev, &desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(sy7636a->dev, "Failed to register %s regulator\n",
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
	},
	.probe = sy7636a_regulator_probe,
	.id_table = sy7636a_regulator_id_table,
};
module_platform_driver(sy7636a_regulator_driver);

MODULE_AUTHOR("Lars Ivar Miljeteig <lars.ivar.miljeteig@remarkable.com>");
MODULE_DESCRIPTION("SY7636A voltage regulator driver");
MODULE_LICENSE("GPL v2");
