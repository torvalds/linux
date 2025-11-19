// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform driver for the Embedded Controller (EC) of Ayaneo devices. Handles
 * hwmon (fan speed, fan control), battery charge limits, and magic module
 * control (connected modules, controller disconnection).
 *
 * Copyright (C) 2025 Antheas Kapenekakis <lkml@antheas.dev>
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/sysfs.h>
#include <acpi/battery.h>

#define AYANEO_PWM_ENABLE_REG	 0x4A
#define AYANEO_PWM_REG		 0x4B
#define AYANEO_PWM_MODE_AUTO	 0x00
#define AYANEO_PWM_MODE_MANUAL	 0x01

#define AYANEO_FAN_REG		 0x76

#define EC_CHARGE_CONTROL_BEHAVIOURS                         \
	(BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) |           \
	 BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE))
#define AYANEO_CHARGE_REG		0x1e
#define AYANEO_CHARGE_VAL_AUTO		0xaa
#define AYANEO_CHARGE_VAL_INHIBIT	0x55

#define AYANEO_POWER_REG	0x2d
#define AYANEO_POWER_OFF	0xfe
#define AYANEO_POWER_ON		0xff
#define AYANEO_MODULE_REG	0x2f
#define AYANEO_MODULE_LEFT	BIT(0)
#define AYANEO_MODULE_RIGHT	BIT(1)
#define AYANEO_MODULE_MASK	(AYANEO_MODULE_LEFT | AYANEO_MODULE_RIGHT)

struct ayaneo_ec_quirk {
	bool has_fan_control;
	bool has_charge_control;
	bool has_magic_modules;
};

struct ayaneo_ec_platform_data {
	struct platform_device *pdev;
	struct ayaneo_ec_quirk *quirks;
	struct acpi_battery_hook battery_hook;

	// Protects access to restore_pwm
	struct mutex hwmon_lock;
	bool restore_charge_limit;
	bool restore_pwm;
};

static const struct ayaneo_ec_quirk quirk_fan = {
	.has_fan_control = true,
};

static const struct ayaneo_ec_quirk quirk_charge_limit = {
	.has_fan_control = true,
	.has_charge_control = true,
};

static const struct ayaneo_ec_quirk quirk_ayaneo3 = {
	.has_fan_control = true,
	.has_charge_control = true,
	.has_magic_modules = true,
};

static const struct dmi_system_id dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_MATCH(DMI_BOARD_NAME, "AYANEO 2"),
		},
		.driver_data = (void *)&quirk_fan,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_MATCH(DMI_BOARD_NAME, "FLIP"),
		},
		.driver_data = (void *)&quirk_fan,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_MATCH(DMI_BOARD_NAME, "GEEK"),
		},
		.driver_data = (void *)&quirk_fan,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AIR"),
		},
		.driver_data = (void *)&quirk_charge_limit,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AIR 1S"),
		},
		.driver_data = (void *)&quirk_charge_limit,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AB05-Mendocino"),
		},
		.driver_data = (void *)&quirk_charge_limit,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AIR Pro"),
		},
		.driver_data = (void *)&quirk_charge_limit,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "KUN"),
		},
		.driver_data = (void *)&quirk_charge_limit,
	},
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
	struct ayaneo_ec_platform_data *data = dev_get_drvdata(dev);
	int ret;

	guard(mutex)(&data->hwmon_lock);

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_enable:
			data->restore_pwm = false;
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
			if (data->restore_pwm) {
				/*
				 * Defer restoring PWM control to after
				 * userspace resumes successfully
				 */
				ret = ec_write(AYANEO_PWM_ENABLE_REG,
					       AYANEO_PWM_MODE_MANUAL);
				if (ret)
					return ret;
				data->restore_pwm = false;
			}
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

static int ayaneo_psy_ext_get_prop(struct power_supply *psy,
				   const struct power_supply_ext *ext,
				   void *data,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	int ret;
	u8 tmp;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		ret = ec_read(AYANEO_CHARGE_REG, &tmp);
		if (ret)
			return ret;

		if (tmp == AYANEO_CHARGE_VAL_INHIBIT)
			val->intval = POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
		else
			val->intval = POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
		return 0;
	default:
		return -EINVAL;
	}
}

static int ayaneo_psy_ext_set_prop(struct power_supply *psy,
				   const struct power_supply_ext *ext,
				   void *data,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	u8 raw_val;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		switch (val->intval) {
		case POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO:
			raw_val = AYANEO_CHARGE_VAL_AUTO;
			break;
		case POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE:
			raw_val = AYANEO_CHARGE_VAL_INHIBIT;
			break;
		default:
			return -EINVAL;
		}
		return ec_write(AYANEO_CHARGE_REG, raw_val);
	default:
		return -EINVAL;
	}
}

static int ayaneo_psy_prop_is_writeable(struct power_supply *psy,
					const struct power_supply_ext *ext,
					void *data,
					enum power_supply_property psp)
{
	return true;
}

static const enum power_supply_property ayaneo_psy_ext_props[] = {
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
};

static const struct power_supply_ext ayaneo_psy_ext = {
	.name			= "ayaneo-charge-control",
	.properties		= ayaneo_psy_ext_props,
	.num_properties		= ARRAY_SIZE(ayaneo_psy_ext_props),
	.charge_behaviours	= EC_CHARGE_CONTROL_BEHAVIOURS,
	.get_property		= ayaneo_psy_ext_get_prop,
	.set_property		= ayaneo_psy_ext_set_prop,
	.property_is_writeable	= ayaneo_psy_prop_is_writeable,
};

static int ayaneo_add_battery(struct power_supply *battery,
			      struct acpi_battery_hook *hook)
{
	struct ayaneo_ec_platform_data *data =
		container_of(hook, struct ayaneo_ec_platform_data, battery_hook);

	return power_supply_register_extension(battery, &ayaneo_psy_ext,
					       &data->pdev->dev, NULL);
}

static int ayaneo_remove_battery(struct power_supply *battery,
				 struct acpi_battery_hook *hook)
{
	power_supply_unregister_extension(battery, &ayaneo_psy_ext);
	return 0;
}

static ssize_t controller_power_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	bool value;
	int ret;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	ret = ec_write(AYANEO_POWER_REG, value ? AYANEO_POWER_ON : AYANEO_POWER_OFF);
	if (ret)
		return ret;

	return count;
}

static ssize_t controller_power_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	int ret;
	u8 val;

	ret = ec_read(AYANEO_POWER_REG, &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", val == AYANEO_POWER_ON);
}

static DEVICE_ATTR_RW(controller_power);

static ssize_t controller_modules_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	u8 unconnected_modules;
	char *out;
	int ret;

	ret = ec_read(AYANEO_MODULE_REG, &unconnected_modules);
	if (ret)
		return ret;

	switch (~unconnected_modules & AYANEO_MODULE_MASK) {
	case AYANEO_MODULE_LEFT | AYANEO_MODULE_RIGHT:
		out = "both";
		break;
	case AYANEO_MODULE_LEFT:
		out = "left";
		break;
	case AYANEO_MODULE_RIGHT:
		out = "right";
		break;
	default:
		out = "none";
		break;
	}

	return sysfs_emit(buf, "%s\n", out);
}

static DEVICE_ATTR_RO(controller_modules);

static struct attribute *aya_mm_attrs[] = {
	&dev_attr_controller_power.attr,
	&dev_attr_controller_modules.attr,
	NULL
};

static umode_t aya_mm_is_visible(struct kobject *kobj,
				 struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct platform_device *pdev = to_platform_device(dev);
	struct ayaneo_ec_platform_data *data = platform_get_drvdata(pdev);

	if (data->quirks->has_magic_modules)
		return attr->mode;
	return 0;
}

static const struct attribute_group aya_mm_attribute_group = {
	.is_visible = aya_mm_is_visible,
	.attrs = aya_mm_attrs,
};

static const struct attribute_group *ayaneo_ec_groups[] = {
	&aya_mm_attribute_group,
	NULL
};

static int ayaneo_ec_probe(struct platform_device *pdev)
{
	const struct dmi_system_id *dmi_entry;
	struct ayaneo_ec_platform_data *data;
	struct device *hwdev;
	int ret;

	dmi_entry = dmi_first_match(dmi_table);
	if (!dmi_entry)
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;
	data->quirks = dmi_entry->driver_data;
	ret = devm_mutex_init(&pdev->dev, &data->hwmon_lock);
	if (ret)
		return ret;
	platform_set_drvdata(pdev, data);

	if (data->quirks->has_fan_control) {
		hwdev = devm_hwmon_device_register_with_info(&pdev->dev,
			"ayaneo_ec", data, &ayaneo_ec_chip_info, NULL);
		if (IS_ERR(hwdev))
			return PTR_ERR(hwdev);
	}

	if (data->quirks->has_charge_control) {
		data->battery_hook.add_battery = ayaneo_add_battery;
		data->battery_hook.remove_battery = ayaneo_remove_battery;
		data->battery_hook.name = "Ayaneo Battery";
		ret = devm_battery_hook_register(&pdev->dev, &data->battery_hook);
		if (ret)
			return ret;
	}

	return 0;
}

static int ayaneo_freeze(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ayaneo_ec_platform_data *data = platform_get_drvdata(pdev);
	int ret;
	u8 tmp;

	if (data->quirks->has_charge_control) {
		ret = ec_read(AYANEO_CHARGE_REG, &tmp);
		if (ret)
			return ret;

		data->restore_charge_limit = tmp == AYANEO_CHARGE_VAL_INHIBIT;
	}

	if (data->quirks->has_fan_control) {
		ret = ec_read(AYANEO_PWM_ENABLE_REG, &tmp);
		if (ret)
			return ret;

		data->restore_pwm = tmp == AYANEO_PWM_MODE_MANUAL;

		/*
		 * Release the fan when entering hibernation to avoid
		 * overheating if hibernation fails and hangs.
		 */
		if (data->restore_pwm) {
			ret = ec_write(AYANEO_PWM_ENABLE_REG, AYANEO_PWM_MODE_AUTO);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int ayaneo_restore(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ayaneo_ec_platform_data *data = platform_get_drvdata(pdev);
	int ret;

	if (data->quirks->has_charge_control && data->restore_charge_limit) {
		ret = ec_write(AYANEO_CHARGE_REG, AYANEO_CHARGE_VAL_INHIBIT);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct dev_pm_ops ayaneo_pm_ops = {
	.freeze = ayaneo_freeze,
	.restore = ayaneo_restore,
};

static struct platform_driver ayaneo_platform_driver = {
	.driver = {
		.name = "ayaneo-ec",
		.dev_groups = ayaneo_ec_groups,
		.pm = pm_sleep_ptr(&ayaneo_pm_ops),
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
