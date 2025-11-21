// SPDX-License-Identifier: GPL-2.0+
/*
 * Functions to access SY3686A power management chip temperature
 *
 * Copyright (C) 2021 reMarkable AS - http://www.remarkable.com/
 *
 * Authors: Lars Ivar Miljeteig <lars.ivar.miljeteig@remarkable.com>
 *          Alistair Francis <alistair@alistair23.me>
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/machine.h>

#include <linux/mfd/sy7636a.h>

static int sy7636a_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *temp)
{
	struct regmap *regmap = dev_get_drvdata(dev);
	int ret, reg_val;

	ret = regmap_read(regmap,
			  SY7636A_REG_TERMISTOR_READOUT, &reg_val);
	if (ret)
		return ret;

	*temp = reg_val * 1000;

	return 0;
}

static umode_t sy7636a_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	if (attr != hwmon_temp_input)
		return 0;

	return 0444;
}

static const struct hwmon_ops sy7636a_hwmon_ops = {
	.is_visible = sy7636a_is_visible,
	.read = sy7636a_read,
};

static const struct hwmon_channel_info * const sy7636a_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static const struct hwmon_chip_info sy7636a_chip_info = {
	.ops = &sy7636a_hwmon_ops,
	.info = sy7636a_info,
};

static int sy7636a_sensor_probe(struct platform_device *pdev)
{
	struct regmap *regmap = dev_get_regmap(pdev->dev.parent, NULL);
	struct regulator *regulator;
	struct device *hwmon_dev;
	int err;

	if (!regmap)
		return -EPROBE_DEFER;

	regulator = devm_regulator_get(&pdev->dev, "vcom");
	if (IS_ERR(regulator))
		return PTR_ERR(regulator);

	err = regulator_enable(regulator);
	if (err)
		return err;

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
							 "sy7636a_temperature", regmap,
							 &sy7636a_chip_info, NULL);

	if (IS_ERR(hwmon_dev)) {
		err = PTR_ERR(hwmon_dev);
		dev_err(&pdev->dev, "Unable to register hwmon device, returned %d\n", err);
		return err;
	}

	return 0;
}

static struct platform_driver sy7636a_sensor_driver = {
	.probe = sy7636a_sensor_probe,
	.driver = {
		.name = "sy7636a-temperature",
	},
};
module_platform_driver(sy7636a_sensor_driver);

MODULE_DESCRIPTION("SY7636A sensor driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sy7636a-temperature");
