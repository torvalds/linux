// SPDX-License-Identifier: GPL-2.0
/*
 *  MAX77705 voltage and current hwmon driver.
 *
 *  Copyright (C) 2025 Dzmitry Sankouski <dsankouski@gmail.com>
 */

#include <linux/err.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/mfd/max77705-private.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

struct channel_desc {
	u8 reg;
	u8 avg_reg;
	const char *const label;
	// register resolution. nano Volts for voltage, nano Amperes for current
	u32 resolution;
};

static const struct channel_desc current_channel_desc[] = {
	{
		.reg = IIN_REG,
		.label = "IIN_REG",
		.resolution = 125000
	},
	{
		.reg = ISYS_REG,
		.avg_reg = AVGISYS_REG,
		.label = "ISYS_REG",
		.resolution = 312500
	}
};

static const struct channel_desc voltage_channel_desc[] = {
	{
		.reg = VBYP_REG,
		.label = "VBYP_REG",
		.resolution = 427246
	},
	{
		.reg = VSYS_REG,
		.label = "VSYS_REG",
		.resolution = 156250
	}
};

static int max77705_read_and_convert(struct regmap *regmap, u8 reg, u32 res,
				     bool is_signed, long *val)
{
	int ret;
	u32 regval;

	ret = regmap_read(regmap, reg, &regval);
	if (ret < 0)
		return ret;

	if (is_signed)
		*val = mult_frac((long)sign_extend32(regval, 15), res, 1000000);
	else
		*val = mult_frac((long)regval, res, 1000000);

	return 0;
}

static umode_t max77705_is_visible(const void *data,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
		case hwmon_in_label:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
		case hwmon_in_label:
			return 0444;
		case hwmon_curr_average:
			if (current_channel_desc[channel].avg_reg)
				return 0444;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int max77705_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
				int channel, const char **buf)
{
	switch (type) {
	case hwmon_curr:
		switch (attr) {
		case hwmon_in_label:
			*buf = current_channel_desc[channel].label;
			return 0;
		default:
			return -EOPNOTSUPP;
		}

	case hwmon_in:
		switch (attr) {
		case hwmon_in_label:
			*buf = voltage_channel_desc[channel].label;
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static int max77705_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *val)
{
	struct regmap *regmap = dev_get_drvdata(dev);
	u8 reg;
	u32 res;

	switch (type) {
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			reg = current_channel_desc[channel].reg;
			res = current_channel_desc[channel].resolution;

			return max77705_read_and_convert(regmap, reg, res, true, val);
		case hwmon_curr_average:
			reg = current_channel_desc[channel].avg_reg;
			res = current_channel_desc[channel].resolution;

			return max77705_read_and_convert(regmap, reg, res, true, val);
		default:
			return -EOPNOTSUPP;
		}

	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			reg = voltage_channel_desc[channel].reg;
			res = voltage_channel_desc[channel].resolution;

			return max77705_read_and_convert(regmap, reg, res, false, val);
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct hwmon_ops max77705_hwmon_ops = {
	.is_visible = max77705_is_visible,
	.read = max77705_read,
	.read_string = max77705_read_string,
};

static const struct hwmon_channel_info *max77705_info[] = {
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL
			),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_AVERAGE | HWMON_C_LABEL
			),
	NULL
};

static const struct hwmon_chip_info max77705_chip_info = {
	.ops = &max77705_hwmon_ops,
	.info = max77705_info,
};

static int max77705_hwmon_probe(struct platform_device *pdev)
{
	struct device *hwmon_dev;
	struct regmap *regmap;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap)
		return -ENODEV;

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev, "max77705", regmap,
							 &max77705_chip_info, NULL);
	if (IS_ERR(hwmon_dev))
		return dev_err_probe(&pdev->dev, PTR_ERR(hwmon_dev),
				"Unable to register hwmon device\n");

	return 0;
};

static struct platform_driver max77705_hwmon_driver = {
	.driver = {
		.name = "max77705-hwmon",
	},
	.probe = max77705_hwmon_probe,
};

module_platform_driver(max77705_hwmon_driver);

MODULE_AUTHOR("Dzmitry Sankouski <dsankouski@gmail.com>");
MODULE_DESCRIPTION("MAX77705 monitor driver");
MODULE_LICENSE("GPL");
