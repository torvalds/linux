// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform driver for OneXPlayer, AOK ZOE, and Aya Neo Handhelds that expose
 * fan reading and control via hwmon sysfs.
 *
 * Old OXP boards have the same DMI strings and they are told apart by
 * the boot cpu vendor (Intel/AMD). Currently only AMD boards are
 * supported but the code is made to be simple to add other handheld
 * boards in the future.
 * Fan control is provided via pwm interface in the range [0-255].
 * Old AMD boards use [0-100] as range in the EC, the written value is
 * scaled to accommodate for that. Newer boards like the mini PRO and
 * AOK ZOE are not scaled but have the same EC layout.
 *
 * Copyright (C) 2022 Joaquín I. Aramendía <samsagax@gmail.com>
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
	aya_neo_air_pro,
	aya_neo_geek,
	oxp_mini_amd,
	oxp_mini_amd_a07,
	oxp_mini_amd_pro,
};

static enum oxp_board board;

/* Fan reading and PWM */
#define OXP_SENSOR_FAN_REG		0x76 /* Fan reading is 2 registers long */
#define OXP_SENSOR_PWM_ENABLE_REG	0x4A /* PWM enable is 1 register long */
#define OXP_SENSOR_PWM_REG		0x4B /* PWM reading is 1 register long */

/* Turbo button takeover function
 * Older boards have different values and EC registers
 * for the same function
 */
#define OXP_OLD_TURBO_SWITCH_REG	0x1E
#define OXP_OLD_TURBO_TAKE_VAL		0x01
#define OXP_OLD_TURBO_RETURN_VAL	0x00

#define OXP_TURBO_SWITCH_REG		0xF1
#define OXP_TURBO_TAKE_VAL		0x40
#define OXP_TURBO_RETURN_VAL		0x00

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
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AYANEO 2"),
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
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AIR Pro"),
		},
		.driver_data = (void *)aya_neo_air_pro,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GEEK"),
		},
		.driver_data = (void *)aya_neo_geek,
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
		reg = OXP_OLD_TURBO_SWITCH_REG;
		val = OXP_OLD_TURBO_TAKE_VAL;
		break;
	case oxp_mini_amd_pro:
	case aok_zoe_a1:
		reg = OXP_TURBO_SWITCH_REG;
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
		reg = OXP_OLD_TURBO_SWITCH_REG;
		val = OXP_OLD_TURBO_RETURN_VAL;
		break;
	case oxp_mini_amd_pro:
	case aok_zoe_a1:
		reg = OXP_TURBO_SWITCH_REG;
		val = OXP_TURBO_RETURN_VAL;
		break;
	default:
		return -EINVAL;
	}
	return write_to_ec(reg, val);
}

/* Callbacks for turbo toggle attribute */
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
		reg = OXP_OLD_TURBO_SWITCH_REG;
		break;
	case oxp_mini_amd_pro:
	case aok_zoe_a1:
		reg = OXP_TURBO_SWITCH_REG;
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
	return write_to_ec(OXP_SENSOR_PWM_ENABLE_REG, 0x01);
}

static int oxp_pwm_disable(void)
{
	return write_to_ec(OXP_SENSOR_PWM_ENABLE_REG, 0x00);
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
			return read_from_ec(OXP_SENSOR_FAN_REG, 2, val);
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			ret = read_from_ec(OXP_SENSOR_PWM_REG, 1, val);
			if (ret)
				return ret;
			switch (board) {
			case aya_neo_2:
			case aya_neo_air:
			case aya_neo_air_pro:
			case aya_neo_geek:
			case oxp_mini_amd:
			case oxp_mini_amd_a07:
				*val = (*val * 255) / 100;
				break;
			case oxp_mini_amd_pro:
			case aok_zoe_a1:
			default:
				break;
			}
			return 0;
		case hwmon_pwm_enable:
			return read_from_ec(OXP_SENSOR_PWM_ENABLE_REG, 1, val);
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
			case aya_neo_2:
			case aya_neo_air:
			case aya_neo_air_pro:
			case aya_neo_geek:
			case oxp_mini_amd:
			case oxp_mini_amd_a07:
				val = (val * 100) / 255;
				break;
			case aok_zoe_a1:
			case oxp_mini_amd_pro:
			default:
				break;
			}
			return write_to_ec(OXP_SENSOR_PWM_REG, val);
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

ATTRIBUTE_GROUPS(oxp_ec);

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
	const struct dmi_system_id *dmi_entry;
	struct device *dev = &pdev->dev;
	struct device *hwdev;
	int ret;

	/*
	 * Have to check for AMD processor here because DMI strings are the
	 * same between Intel and AMD boards, the only way to tell them apart
	 * is the CPU.
	 * Intel boards seem to have different EC registers and values to
	 * read/write.
	 */
	dmi_entry = dmi_first_match(dmi_table);
	if (!dmi_entry || boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return -ENODEV;

	board = (enum oxp_board)(unsigned long)dmi_entry->driver_data;

	switch (board) {
	case aok_zoe_a1:
	case oxp_mini_amd_a07:
	case oxp_mini_amd_pro:
		ret = devm_device_add_groups(dev, oxp_ec_groups);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	hwdev = devm_hwmon_device_register_with_info(dev, "oxpec", NULL,
						     &oxp_ec_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwdev);
}

static struct platform_driver oxp_platform_driver = {
	.driver = {
		.name = "oxp-platform",
	},
	.probe = oxp_platform_probe,
};

static struct platform_device *oxp_platform_device;

static int __init oxp_platform_init(void)
{
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
