// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform driver for the Embedded Controller (EC) of Ayaneo devices. Handles
 * hwmon (fan speed, fan control), battery charge limits, and magic module
 * control (connected modules, controller disconnection).
 *
 * Copyright (C) 2025 Antheas Kapenekakis <lkml@antheas.dev>
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define AYANEO_PWM_ENABLE_REG	 0x4A
#define AYANEO_PWM_REG		 0x4B
#define AYANEO_PWM_MODE_AUTO	 0x00
#define AYANEO_PWM_MODE_MANUAL	 0x01

#define AYANEO_FAN_REG		 0x76

struct ayaneo_ec_quirk {
	bool has_fan_control;
};

struct ayaneo_ec_platform_data {
	struct platform_device *pdev;
	struct ayaneo_ec_quirk *quirks;
};

static const struct ayaneo_ec_quirk quirk_ayaneo3 = {
	.has_fan_control = true,
};

static const struct dmi_system_id dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AYANEO 3"),
		},
		.driver_data = (void *)&quirk_ayaneo3,
	},
	{},
};

/* Callbacks for hwmon interface */
static umode_t ayaneo_ec_hwmon_is_visible(const void *drvdata,
					  enum hwmon_sensor_types type, u32 attr,
					  int channel)
{
	switch (type) {
	case hwmon_fan:
		return 0444;
	case hwmon_pwm:
		return 0644;
	default:
		return 0;
	}
}

static int ayaneo_ec_read(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long *val)
{
	u8 tmp;
	int ret;

	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			ret = ec_read(AYANEO_FAN_REG, &tmp);
			if (ret)
				return ret;
			*val = tmp << 8;
			ret = ec_read(AYANEO_FAN_REG + 1, &tmp);
			if (ret)
				return ret;
			*val |= tmp;
			return 0;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			ret = ec_read(AYANEO_PWM_REG, &tmp);
			if (ret)
				return ret;
			if (tmp > 100)
				return -EIO;
			*val = (255 * tmp) / 100;
			return 0;
		case hwmon_pwm_enable:
			ret = ec_read(AYANEO_PWM_ENABLE_REG, &tmp);
			if (ret)
				return ret;
			if (tmp == AYANEO_PWM_MODE_MANUAL)
				*val = 1;
			else if (tmp == AYANEO_PWM_MODE_AUTO)
				*val = 2;
			else
				return -EIO;
			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return -EOPNOTSUPP;
}

static int ayaneo_ec_write(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_enable:
			switch (val) {
			case 1:
				return ec_write(AYANEO_PWM_ENABLE_REG,
						AYANEO_PWM_MODE_MANUAL);
			case 2:
				return ec_write(AYANEO_PWM_ENABLE_REG,
						AYANEO_PWM_MODE_AUTO);
			default:
				return -EINVAL;
			}
		case hwmon_pwm_input:
			if (val < 0 || val > 255)
				return -EINVAL;
			return ec_write(AYANEO_PWM_REG, (val * 100) / 255);
		default:
			break;
		}
		break;
	default:
		break;
	}
	return -EOPNOTSUPP;
}

static const struct hwmon_ops ayaneo_ec_hwmon_ops = {
	.is_visible = ayaneo_ec_hwmon_is_visible,
	.read = ayaneo_ec_read,
	.write = ayaneo_ec_write,
};

static const struct hwmon_channel_info *const ayaneo_ec_sensors[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	NULL,
};

static const struct hwmon_chip_info ayaneo_ec_chip_info = {
	.ops = &ayaneo_ec_hwmon_ops,
	.info = ayaneo_ec_sensors,
};

static int ayaneo_ec_probe(struct platform_device *pdev)
{
	const struct dmi_system_id *dmi_entry;
	struct ayaneo_ec_platform_data *data;
	struct device *hwdev;

	dmi_entry = dmi_first_match(dmi_table);
	if (!dmi_entry)
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;
	data->quirks = dmi_entry->driver_data;
	platform_set_drvdata(pdev, data);

	if (data->quirks->has_fan_control) {
		hwdev = devm_hwmon_device_register_with_info(&pdev->dev,
			"ayaneo_ec", NULL, &ayaneo_ec_chip_info, NULL);
		if (IS_ERR(hwdev))
			return PTR_ERR(hwdev);
	}

	return 0;
}

static struct platform_driver ayaneo_platform_driver = {
	.driver = {
		.name = "ayaneo-ec",
	},
	.probe = ayaneo_ec_probe,
};

static struct platform_device *ayaneo_platform_device;

static int __init ayaneo_ec_init(void)
{
	ayaneo_platform_device =
		platform_create_bundle(&ayaneo_platform_driver,
				       ayaneo_ec_probe, NULL, 0, NULL, 0);

	return PTR_ERR_OR_ZERO(ayaneo_platform_device);
}

static void __exit ayaneo_ec_exit(void)
{
	platform_device_unregister(ayaneo_platform_device);
	platform_driver_unregister(&ayaneo_platform_driver);
}

MODULE_DEVICE_TABLE(dmi, dmi_table);

module_init(ayaneo_ec_init);
module_exit(ayaneo_ec_exit);

MODULE_AUTHOR("Antheas Kapenekakis <lkml@antheas.dev>");
MODULE_DESCRIPTION("Ayaneo Embedded Controller (EC) platform features");
MODULE_LICENSE("GPL");
