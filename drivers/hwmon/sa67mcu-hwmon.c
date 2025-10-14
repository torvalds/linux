// SPDX-License-Identifier: GPL-2.0-only
/*
 * sl67mcu hardware monitoring driver
 *
 * Copyright 2025 Kontron Europe GmbH
 */

#include <linux/bitfield.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define SA67MCU_VOLTAGE(n)	(0x00 + ((n) * 2))
#define SA67MCU_TEMP(n)		(0x04 + ((n) * 2))

struct sa67mcu_hwmon {
	struct regmap *regmap;
	u32 offset;
};

static int sa67mcu_hwmon_read(struct device *dev,
			      enum hwmon_sensor_types type, u32 attr,
			      int channel, long *input)
{
	struct sa67mcu_hwmon *hwmon = dev_get_drvdata(dev);
	unsigned int offset;
	u8 reg[2];
	int ret;

	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			offset = hwmon->offset + SA67MCU_VOLTAGE(channel);
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			offset = hwmon->offset + SA67MCU_TEMP(channel);
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Reading the low byte will capture the value */
	ret = regmap_bulk_read(hwmon->regmap, offset, reg, ARRAY_SIZE(reg));
	if (ret)
		return ret;

	*input = reg[1] << 8 | reg[0];

	/* Temperatures are s16 and in 0.1degC steps. */
	if (type == hwmon_temp)
		*input = sign_extend32(*input, 15) * 100;

	return 0;
}

static const struct hwmon_channel_info * const sa67mcu_hwmon_info[] = {
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static const char *const sa67mcu_hwmon_in_labels[] = {
	"VDDIN",
	"VDD_RTC",
};

static int sa67mcu_hwmon_read_string(struct device *dev,
				     enum hwmon_sensor_types type, u32 attr,
				     int channel, const char **str)
{
	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_label:
			*str = sa67mcu_hwmon_in_labels[channel];
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops sa67mcu_hwmon_ops = {
	.visible = 0444,
	.read = sa67mcu_hwmon_read,
	.read_string = sa67mcu_hwmon_read_string,
};

static const struct hwmon_chip_info sa67mcu_hwmon_chip_info = {
	.ops = &sa67mcu_hwmon_ops,
	.info = sa67mcu_hwmon_info,
};

static int sa67mcu_hwmon_probe(struct platform_device *pdev)
{
	struct sa67mcu_hwmon *hwmon;
	struct device *hwmon_dev;
	int ret;

	if (!pdev->dev.parent)
		return -ENODEV;

	hwmon = devm_kzalloc(&pdev->dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	hwmon->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!hwmon->regmap)
		return -ENODEV;

	ret = device_property_read_u32(&pdev->dev, "reg", &hwmon->offset);
	if (ret)
		return -EINVAL;

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
							 "sa67mcu_hwmon", hwmon,
							 &sa67mcu_hwmon_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev))
		dev_err(&pdev->dev, "failed to register as hwmon device");

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id sa67mcu_hwmon_of_match[] = {
	{ .compatible = "kontron,sa67mcu-hwmon", },
	{}
};
MODULE_DEVICE_TABLE(of, sa67mcu_hwmon_of_match);

static struct platform_driver sa67mcu_hwmon_driver = {
	.probe = sa67mcu_hwmon_probe,
	.driver = {
		.name = "sa67mcu-hwmon",
		.of_match_table = sa67mcu_hwmon_of_match,
	},
};
module_platform_driver(sa67mcu_hwmon_driver);

MODULE_DESCRIPTION("sa67mcu Hardware Monitoring Driver");
MODULE_AUTHOR("Michael Walle <mwalle@kernel.org>");
MODULE_LICENSE("GPL");
