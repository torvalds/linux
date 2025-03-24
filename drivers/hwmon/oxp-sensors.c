// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform driver for OneXPlayer, AOKZOE, AYANEO, and OrangePi Handhelds
 * that expose fan reading and control via hwmon sysfs.
 *
 * Old OXP boards have the same DMI strings and they are told apart by
 * the boot cpu vendor (Intel/AMD). Of these older models only AMD is
 * supported.
 *
 * Fan control is provided via pwm interface in the range [0-255].
 * Old AMD boards use [0-100] as range in the EC, the written value is
 * scaled to accommodate for that. Newer boards like the mini PRO and
 * AOKZOE are not scaled but have the same EC layout. Newer models
 * like the 2 and X1 are [0-184] and are scaled to 0-255. OrangePi
 * are [1-244] and scaled to 0-255.
 *
 * Copyright (C) 2022 Joaquín I. Aramendía <samsagax@gmail.com>
 * Copyright (C) 2024 Derek J. Clark <derekjohn.clark@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/processor.h>

/* Handle ACPI lock mechanism */
static u32 oxp_mutex;

#define ACPI_LOCK_DELAY_MS	500

static bool lock_global_acpi_lock(void)
{
	return ACPI_SUCCESS(acpi_acquire_global_lock(ACPI_LOCK_DELAY_MS, &oxp_mutex));
}

static bool unlock_global_acpi_lock(void)
{
	return ACPI_SUCCESS(acpi_release_global_lock(oxp_mutex));
}

enum oxp_board {
	aok_zoe_a1 = 1,
	aya_neo_2,
	aya_neo_air,
	aya_neo_air_1s,
	aya_neo_air_plus_mendo,
	aya_neo_air_pro,
	aya_neo_flip,
	aya_neo_geek,
	aya_neo_kun,
	orange_pi_neo,
	oxp_2,
	oxp_fly,
	oxp_mini_amd,
	oxp_mini_amd_a07,
	oxp_mini_amd_pro,
	oxp_x1,
};

static enum oxp_board board;

/* Fan reading and PWM */
#define OXP_SENSOR_FAN_REG             0x76 /* Fan reading is 2 registers long */
#define OXP_2_SENSOR_FAN_REG           0x58 /* Fan reading is 2 registers long */
#define OXP_SENSOR_PWM_ENABLE_REG      0x4A /* PWM enable is 1 register long */
#define OXP_SENSOR_PWM_REG             0x4B /* PWM reading is 1 register long */
#define PWM_MODE_AUTO                  0x00
#define PWM_MODE_MANUAL                0x01

/* OrangePi fan reading and PWM */
#define ORANGEPI_SENSOR_FAN_REG        0x78 /* Fan reading is 2 registers long */
#define ORANGEPI_SENSOR_PWM_ENABLE_REG 0x40 /* PWM enable is 1 register long */
#define ORANGEPI_SENSOR_PWM_REG        0x38 /* PWM reading is 1 register long */

/* Turbo button takeover function
 * Different boards have different values and EC registers
 * for the same function
 */
#define OXP_TURBO_SWITCH_REG           0xF1 /* Mini Pro, OneXFly, AOKZOE */
#define OXP_2_TURBO_SWITCH_REG         0xEB /* OXP2 and X1 */
#define OXP_MINI_TURBO_SWITCH_REG      0x1E /* Mini AO7 */

#define OXP_MINI_TURBO_TAKE_VAL        0x01 /* Mini AO7 */
#define OXP_TURBO_TAKE_VAL             0x40 /* All other models */

#define OXP_TURBO_RETURN_VAL           0x00 /* Common return val */

static const struct dmi_system_id dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AOKZOE"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AOKZOE A1 AR07"),
		},
		.driver_data = (void *)aok_zoe_a1,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AOKZOE"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AOKZOE A1 Pro"),
		},
		.driver_data = (void *)aok_zoe_a1,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_MATCH(DMI_BOARD_NAME, "AYANEO 2"),
		},
		.driver_data = (void *)aya_neo_2,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AIR"),
		},
		.driver_data = (void *)aya_neo_air,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AIR 1S"),
		},
		.driver_data = (void *)aya_neo_air_1s,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AB05-Mendocino"),
		},
		.driver_data = (void *)aya_neo_air_plus_mendo,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AIR Pro"),
		},
		.driver_data = (void *)aya_neo_air_pro,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_MATCH(DMI_BOARD_NAME, "FLIP"),
		},
		.driver_data = (void *)aya_neo_flip,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_MATCH(DMI_BOARD_NAME, "GEEK"),
		},
		.driver_data = (void *)aya_neo_geek,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "KUN"),
		},
		.driver_data = (void *)aya_neo_kun,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "OrangePi"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "NEO-01"),
		},
		.driver_data = (void *)orange_pi_neo,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONE XPLAYER"),
		},
		.driver_data = (void *)oxp_mini_amd,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_MATCH(DMI_BOARD_NAME, "ONEXPLAYER 2"),
		},
		.driver_data = (void *)oxp_2,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER F1"),
		},
		.driver_data = (void *)oxp_fly,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER mini A07"),
		},
		.driver_data = (void *)oxp_mini_amd_a07,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER Mini Pro"),
		},
		.driver_data = (void *)oxp_mini_amd_pro,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_MATCH(DMI_BOARD_NAME, "ONEXPLAYER X1"),
		},
		.driver_data = (void *)oxp_x1,
	},
	{},
};

/* Helper functions to handle EC read/write */
static int read_from_ec(u8 reg, int size, long *val)
{
	int i;
	int ret;
	u8 buffer;

	if (!lock_global_acpi_lock())
		return -EBUSY;

	*val = 0;
	for (i = 0; i < size; i++) {
		ret = ec_read(reg + i, &buffer);
		if (ret)
			return ret;
		*val <<= i * 8;
		*val += buffer;
	}

	if (!unlock_global_acpi_lock())
		return -EBUSY;

	return 0;
}

static int write_to_ec(u8 reg, u8 value)
{
	int ret;

	if (!lock_global_acpi_lock())
		return -EBUSY;

	ret = ec_write(reg, value);

	if (!unlock_global_acpi_lock())
		return -EBUSY;

	return ret;
}

/* Turbo button toggle functions */
static int tt_toggle_enable(void)
{
	u8 reg;
	u8 val;

	switch (board) {
	case oxp_mini_amd_a07:
		reg = OXP_MINI_TURBO_SWITCH_REG;
		val = OXP_MINI_TURBO_TAKE_VAL;
		break;
	case aok_zoe_a1:
	case oxp_fly:
	case oxp_mini_amd_pro:
		reg = OXP_TURBO_SWITCH_REG;
		val = OXP_TURBO_TAKE_VAL;
		break;
	case oxp_2:
	case oxp_x1:
		reg = OXP_2_TURBO_SWITCH_REG;
		val = OXP_TURBO_TAKE_VAL;
		break;
	default:
		return -EINVAL;
	}
	return write_to_ec(reg, val);
}

static int tt_toggle_disable(void)
{
	u8 reg;
	u8 val;

	switch (board) {
	case oxp_mini_amd_a07:
		reg = OXP_MINI_TURBO_SWITCH_REG;
		val = OXP_TURBO_RETURN_VAL;
		break;
	case aok_zoe_a1:
	case oxp_fly:
	case oxp_mini_amd_pro:
		reg = OXP_TURBO_SWITCH_REG;
		val = OXP_TURBO_RETURN_VAL;
		break;
	case oxp_2:
	case oxp_x1:
		reg = OXP_2_TURBO_SWITCH_REG;
		val = OXP_TURBO_RETURN_VAL;
		break;
	default:
		return -EINVAL;
	}
	return write_to_ec(reg, val);
}

/* Callbacks for turbo toggle attribute */
static umode_t tt_toggle_is_visible(struct kobject *kobj,
				    struct attribute *attr, int n)
{
	switch (board) {
	case aok_zoe_a1:
	case oxp_2:
	case oxp_fly:
	case oxp_mini_amd_a07:
	case oxp_mini_amd_pro:
	case oxp_x1:
		return attr->mode;
	default:
		break;
	}
	return 0;
}

static ssize_t tt_toggle_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	int rval;
	bool value;

	rval = kstrtobool(buf, &value);
	if (rval)
		return rval;

	if (value) {
		rval = tt_toggle_enable();
	} else {
		rval = tt_toggle_disable();
	}
	if (rval)
		return rval;

	return count;
}

static ssize_t tt_toggle_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	int retval;
	u8 reg;
	long val;

	switch (board) {
	case oxp_mini_amd_a07:
		reg = OXP_MINI_TURBO_SWITCH_REG;
		break;
	case aok_zoe_a1:
	case oxp_fly:
	case oxp_mini_amd_pro:
		reg = OXP_TURBO_SWITCH_REG;
		break;
	case oxp_2:
	case oxp_x1:
		reg = OXP_2_TURBO_SWITCH_REG;
		break;
	default:
		return -EINVAL;
	}

	retval = read_from_ec(reg, 1, &val);
	if (retval)
		return retval;

	return sysfs_emit(buf, "%d\n", !!val);
}

static DEVICE_ATTR_RW(tt_toggle);

/* PWM enable/disable functions */
static int oxp_pwm_enable(void)
{
	switch (board) {
	case orange_pi_neo:
		return write_to_ec(ORANGEPI_SENSOR_PWM_ENABLE_REG, PWM_MODE_MANUAL);
	case aok_zoe_a1:
	case aya_neo_2:
	case aya_neo_air:
	case aya_neo_air_plus_mendo:
	case aya_neo_air_pro:
	case aya_neo_flip:
	case aya_neo_geek:
	case aya_neo_kun:
	case oxp_2:
	case oxp_fly:
	case oxp_mini_amd:
	case oxp_mini_amd_a07:
	case oxp_mini_amd_pro:
	case oxp_x1:
		return write_to_ec(OXP_SENSOR_PWM_ENABLE_REG, PWM_MODE_MANUAL);
	default:
		return -EINVAL;
	}
}

static int oxp_pwm_disable(void)
{
	switch (board) {
	case orange_pi_neo:
		return write_to_ec(ORANGEPI_SENSOR_PWM_ENABLE_REG, PWM_MODE_AUTO);
	case aok_zoe_a1:
	case aya_neo_2:
	case aya_neo_air:
	case aya_neo_air_1s:
	case aya_neo_air_plus_mendo:
	case aya_neo_air_pro:
	case aya_neo_flip:
	case aya_neo_geek:
	case aya_neo_kun:
	case oxp_2:
	case oxp_fly:
	case oxp_mini_amd:
	case oxp_mini_amd_a07:
	case oxp_mini_amd_pro:
	case oxp_x1:
		return write_to_ec(OXP_SENSOR_PWM_ENABLE_REG, PWM_MODE_AUTO);
	default:
		return -EINVAL;
	}
}

/* Callbacks for hwmon interface */
static umode_t oxp_ec_hwmon_is_visible(const void *drvdata,
				       enum hwmon_sensor_types type, u32 attr, int channel)
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

static int oxp_platform_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	int ret;

	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			switch (board) {
			case orange_pi_neo:
				return read_from_ec(ORANGEPI_SENSOR_FAN_REG, 2, val);
			case oxp_2:
			case oxp_x1:
				return read_from_ec(OXP_2_SENSOR_FAN_REG, 2, val);
			case aok_zoe_a1:
			case aya_neo_2:
			case aya_neo_air:
			case aya_neo_air_1s:
			case aya_neo_air_plus_mendo:
			case aya_neo_air_pro:
			case aya_neo_flip:
			case aya_neo_geek:
			case aya_neo_kun:
			case oxp_fly:
			case oxp_mini_amd:
			case oxp_mini_amd_a07:
			case oxp_mini_amd_pro:
				return read_from_ec(OXP_SENSOR_FAN_REG, 2, val);
			default:
				break;
			}
			break;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			switch (board) {
			case orange_pi_neo:
				ret = read_from_ec(ORANGEPI_SENSOR_PWM_REG, 1, val);
				if (ret)
					return ret;
				/* scale from range [1-244] */
				*val = ((*val - 1) * 254 / 243) + 1;
				break;
			case oxp_2:
			case oxp_x1:
				ret = read_from_ec(OXP_SENSOR_PWM_REG, 1, val);
				if (ret)
					return ret;
				/* scale from range [0-184] */
				*val = (*val * 255) / 184;
				break;
			case aya_neo_2:
			case aya_neo_air:
			case aya_neo_air_1s:
			case aya_neo_air_plus_mendo:
			case aya_neo_air_pro:
			case aya_neo_flip:
			case aya_neo_geek:
			case aya_neo_kun:
			case oxp_mini_amd:
			case oxp_mini_amd_a07:
				ret = read_from_ec(OXP_SENSOR_PWM_REG, 1, val);
				if (ret)
					return ret;
				/* scale from range [0-100] */
				*val = (*val * 255) / 100;
				break;
			case aok_zoe_a1:
			case oxp_fly:
			case oxp_mini_amd_pro:
			default:
				ret = read_from_ec(OXP_SENSOR_PWM_REG, 1, val);
				if (ret)
					return ret;
				break;
			}
			return 0;
		case hwmon_pwm_enable:
			switch (board) {
			case orange_pi_neo:
				return read_from_ec(ORANGEPI_SENSOR_PWM_ENABLE_REG, 1, val);
			case aok_zoe_a1:
			case aya_neo_2:
			case aya_neo_air:
			case aya_neo_air_1s:
			case aya_neo_air_plus_mendo:
			case aya_neo_air_pro:
			case aya_neo_flip:
			case aya_neo_geek:
			case aya_neo_kun:
			case oxp_2:
			case oxp_fly:
			case oxp_mini_amd:
			case oxp_mini_amd_a07:
			case oxp_mini_amd_pro:
			case oxp_x1:
				return read_from_ec(OXP_SENSOR_PWM_ENABLE_REG, 1, val);
			default:
				break;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return -EOPNOTSUPP;
}

static int oxp_platform_write(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_enable:
			if (val == 1)
				return oxp_pwm_enable();
			else if (val == 0)
				return oxp_pwm_disable();
			return -EINVAL;
		case hwmon_pwm_input:
			if (val < 0 || val > 255)
				return -EINVAL;
			switch (board) {
			case orange_pi_neo:
				/* scale to range [1-244] */
				val = ((val - 1) * 243 / 254) + 1;
				return write_to_ec(ORANGEPI_SENSOR_PWM_REG, val);
			case oxp_2:
			case oxp_x1:
				/* scale to range [0-184] */
				val = (val * 184) / 255;
				return write_to_ec(OXP_SENSOR_PWM_REG, val);
			case aya_neo_2:
			case aya_neo_air:
			case aya_neo_air_1s:
			case aya_neo_air_plus_mendo:
			case aya_neo_air_pro:
			case aya_neo_flip:
			case aya_neo_geek:
			case aya_neo_kun:
			case oxp_mini_amd:
			case oxp_mini_amd_a07:
				/* scale to range [0-100] */
				val = (val * 100) / 255;
				return write_to_ec(OXP_SENSOR_PWM_REG, val);
			case aok_zoe_a1:
			case oxp_fly:
			case oxp_mini_amd_pro:
				return write_to_ec(OXP_SENSOR_PWM_REG, val);
			default:
				break;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return -EOPNOTSUPP;
}

/* Known sensors in the OXP EC controllers */
static const struct hwmon_channel_info * const oxp_platform_sensors[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	NULL,
};

static struct attribute *oxp_ec_attrs[] = {
	&dev_attr_tt_toggle.attr,
	NULL
};

static struct attribute_group oxp_ec_attribute_group = {
	.is_visible = tt_toggle_is_visible,
	.attrs = oxp_ec_attrs,
};

static const struct attribute_group *oxp_ec_groups[] = {
	&oxp_ec_attribute_group,
	NULL
};

static const struct hwmon_ops oxp_ec_hwmon_ops = {
	.is_visible = oxp_ec_hwmon_is_visible,
	.read = oxp_platform_read,
	.write = oxp_platform_write,
};

static const struct hwmon_chip_info oxp_ec_chip_info = {
	.ops = &oxp_ec_hwmon_ops,
	.info = oxp_platform_sensors,
};

/* Initialization logic */
static int oxp_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *hwdev;

	hwdev = devm_hwmon_device_register_with_info(dev, "oxpec", NULL,
						     &oxp_ec_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwdev);
}

static struct platform_driver oxp_platform_driver = {
	.driver = {
		.name = "oxp-platform",
		.dev_groups = oxp_ec_groups,
	},
	.probe = oxp_platform_probe,
};

static struct platform_device *oxp_platform_device;

static int __init oxp_platform_init(void)
{
	const struct dmi_system_id *dmi_entry;

	dmi_entry = dmi_first_match(dmi_table);
	if (!dmi_entry)
		return -ENODEV;

	board = (enum oxp_board)(unsigned long)dmi_entry->driver_data;

	/*
	 * Have to check for AMD processor here because DMI strings are the same
	 * between Intel and AMD boards on older OneXPlayer devices, the only way
	 * to tell them apart is the CPU. Old Intel boards have an unsupported EC.
	 */
	if (board == oxp_mini_amd && boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return -ENODEV;

	oxp_platform_device =
		platform_create_bundle(&oxp_platform_driver,
				       oxp_platform_probe, NULL, 0, NULL, 0);

	return PTR_ERR_OR_ZERO(oxp_platform_device);
}

static void __exit oxp_platform_exit(void)
{
	platform_device_unregister(oxp_platform_device);
	platform_driver_unregister(&oxp_platform_driver);
}

MODULE_DEVICE_TABLE(dmi, dmi_table);

module_init(oxp_platform_init);
module_exit(oxp_platform_exit);

MODULE_AUTHOR("Joaquín Ignacio Aramendía <samsagax@gmail.com>");
MODULE_DESCRIPTION("Platform driver that handles EC sensors of OneXPlayer devices");
MODULE_LICENSE("GPL");
