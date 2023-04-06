// SPDX-License-Identifier: GPL-2.0-only
/*
 * sl28cpld hardware monitoring driver
 *
 * Copyright 2020 Kontron Europe GmbH
 */

#include <linux/bitfield.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define FAN_INPUT		0x00
#define   FAN_SCALE_X8		BIT(7)
#define   FAN_VALUE_MASK	GENMASK(6, 0)

struct sl28cpld_hwmon {
	struct regmap *regmap;
	u32 offset;
};

static umode_t sl28cpld_hwmon_is_visible(const void *data,
					 enum hwmon_sensor_types type,
					 u32 attr, int channel)
{
	return 0444;
}

static int sl28cpld_hwmon_read(struct device *dev,
			       enum hwmon_sensor_types type, u32 attr,
			       int channel, long *input)
{
	struct sl28cpld_hwmon *hwmon = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	switch (attr) {
	case hwmon_fan_input:
		ret = regmap_read(hwmon->regmap, hwmon->offset + FAN_INPUT,
				  &value);
		if (ret)
			return ret;
		/*
		 * The register has a 7 bit value and 1 bit which indicates the
		 * scale. If the MSB is set, then the lower 7 bit has to be
		 * multiplied by 8, to get the correct reading.
		 */
		if (value & FAN_SCALE_X8)
			value = FIELD_GET(FAN_VALUE_MASK, value) << 3;

		/*
		 * The counter period is 1000ms and the sysfs specification
		 * says we should assume 2 pulses per revolution.
		 */
		value *= 60 / 2;

		break;
	default:
		return -EOPNOTSUPP;
	}

	*input = value;
	return 0;
}

static const struct hwmon_channel_info * const sl28cpld_hwmon_info[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
	NULL
};

static const struct hwmon_ops sl28cpld_hwmon_ops = {
	.is_visible = sl28cpld_hwmon_is_visible,
	.read = sl28cpld_hwmon_read,
};

static const struct hwmon_chip_info sl28cpld_hwmon_chip_info = {
	.ops = &sl28cpld_hwmon_ops,
	.info = sl28cpld_hwmon_info,
};

static int sl28cpld_hwmon_probe(struct platform_device *pdev)
{
	struct sl28cpld_hwmon *hwmon;
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
				"sl28cpld_hwmon", hwmon,
				&sl28cpld_hwmon_chip_info, NULL);
	if (IS_ERR(hwmon_dev))
		dev_err(&pdev->dev, "failed to register as hwmon device");

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id sl28cpld_hwmon_of_match[] = {
	{ .compatible = "kontron,sl28cpld-fan" },
	{}
};
MODULE_DEVICE_TABLE(of, sl28cpld_hwmon_of_match);

static struct platform_driver sl28cpld_hwmon_driver = {
	.probe = sl28cpld_hwmon_probe,
	.driver = {
		.name = "sl28cpld-fan",
		.of_match_table = sl28cpld_hwmon_of_match,
	},
};
module_platform_driver(sl28cpld_hwmon_driver);

MODULE_DESCRIPTION("sl28cpld Hardware Monitoring Driver");
MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
MODULE_LICENSE("GPL");
