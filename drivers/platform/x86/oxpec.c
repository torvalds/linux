// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform driver for OneXPlayer and AOKZOE devices. For the time being,
 * it also exposes fan controls for AYANEO, and OrangePi Handhelds via
 * hwmon sysfs.
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
 * Copyright (C) 2025 Antheas Kapenekakis <lkml@antheas.dev>
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/processor.h>
#include <acpi/battery.h>

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
	oxp_g1_i,
	oxp_g1_a,
};

static enum oxp_board board;
static struct device *oxp_dev;

/* Fan reading and PWM */
#define OXP_SENSOR_FAN_REG		0x76 /* Fan reading is 2 registers long */
#define OXP_2_SENSOR_FAN_REG		0x58 /* Fan reading is 2 registers long */
#define OXP_SENSOR_PWM_ENABLE_REG	0x4A /* PWM enable is 1 register long */
#define OXP_SENSOR_PWM_REG		0x4B /* PWM reading is 1 register long */
#define PWM_MODE_AUTO			0x00
#define PWM_MODE_MANUAL			0x01

/* OrangePi fan reading and PWM */
#define ORANGEPI_SENSOR_FAN_REG		0x78 /* Fan reading is 2 registers long */
#define ORANGEPI_SENSOR_PWM_ENABLE_REG	0x40 /* PWM enable is 1 register long */
#define ORANGEPI_SENSOR_PWM_REG		0x38 /* PWM reading is 1 register long */

/* Turbo button takeover function
 * Different boards have different values and EC registers
 * for the same function
 */
#define OXP_TURBO_SWITCH_REG		0xF1 /* Mini Pro, OneXFly, AOKZOE */
#define OXP_2_TURBO_SWITCH_REG		0xEB /* OXP2 and X1 */
#define OXP_MINI_TURBO_SWITCH_REG	0x1E /* Mini AO7 */

#define OXP_MINI_TURBO_TAKE_VAL		0x01 /* Mini AO7 */
#define OXP_TURBO_TAKE_VAL		0x40 /* All other models */

/* X1 Turbo LED */
#define OXP_X1_TURBO_LED_REG		0x57

#define OXP_X1_TURBO_LED_OFF		0x01
#define OXP_X1_TURBO_LED_ON		0x02

/* Battery extension settings */
#define EC_CHARGE_CONTROL_BEHAVIOURS	(BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) |		\
					 BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE) |	\
					 BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE_AWAKE))

#define OXP_X1_CHARGE_LIMIT_REG		0xA3 /* X1 charge limit (%) */
#define OXP_X1_CHARGE_INHIBIT_REG	0xA4 /* X1 bypass charging */

#define OXP_X1_CHARGE_INHIBIT_MASK_AWAKE	0x01
/* X1 Mask is 0x0A, F1Pro is 0x02 but the extra bit on the X1 does nothing. */
#define OXP_X1_CHARGE_INHIBIT_MASK_OFF		0x02
#define OXP_X1_CHARGE_INHIBIT_MASK_ALWAYS	(OXP_X1_CHARGE_INHIBIT_MASK_AWAKE | \
						 OXP_X1_CHARGE_INHIBIT_MASK_OFF)

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
			DMI_MATCH(DMI_BOARD_VENDOR, "AOKZOE"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AOKZOE A1X"),
		},
		.driver_data = (void *)oxp_fly,
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
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER F1 EVA-01"),
		},
		.driver_data = (void *)oxp_fly,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER F1 OLED"),
		},
		.driver_data = (void *)oxp_fly,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER F1L"),
		},
		.driver_data = (void *)oxp_fly,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER F1Pro"),
		},
		.driver_data = (void *)oxp_fly,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER F1 EVA-02"),
		},
		.driver_data = (void *)oxp_fly,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER G1 A"),
		},
		.driver_data = (void *)oxp_g1_a,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER G1 i"),
		},
		.driver_data = (void *)oxp_g1_i,
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
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER X1 A"),
		},
		.driver_data = (void *)oxp_x1,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER X1 i"),
		},
		.driver_data = (void *)oxp_x1,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER X1 mini"),
		},
		.driver_data = (void *)oxp_x1,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER X1Mini Pro"),
		},
		.driver_data = (void *)oxp_x1,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER X1Pro"),
		},
		.driver_data = (void *)oxp_x1,
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ONE-NETBOOK"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONEXPLAYER X1Pro EVA-02"),
		},
		.driver_data = (void *)oxp_x1,
	},
	{},
};

/* Helper functions to handle EC read/write */
static int read_from_ec(u8 reg, int size, long *val)
{
	u8 buffer;
	int ret;
	int i;

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
	case oxp_g1_i:
	case oxp_g1_a:
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
	u8 reg, mask, val;
	long raw_val;
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;

	switch (board) {
	case oxp_mini_amd_a07:
		reg = OXP_MINI_TURBO_SWITCH_REG;
		mask = OXP_MINI_TURBO_TAKE_VAL;
		break;
	case aok_zoe_a1:
	case oxp_fly:
	case oxp_mini_amd_pro:
	case oxp_g1_a:
		reg = OXP_TURBO_SWITCH_REG;
		mask = OXP_TURBO_TAKE_VAL;
		break;
	case oxp_2:
	case oxp_x1:
	case oxp_g1_i:
		reg = OXP_2_TURBO_SWITCH_REG;
		mask = OXP_TURBO_TAKE_VAL;
		break;
	default:
		return -EINVAL;
	}

	ret = read_from_ec(reg, 1, &raw_val);
	if (ret)
		return ret;

	val = raw_val;
	if (enable)
		val |= mask;
	else
		val &= ~mask;

	ret = write_to_ec(reg, val);
	if (ret)
		return ret;

	return count;
}

static ssize_t tt_toggle_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	u8 reg, mask;
	int retval;
	long val;

	switch (board) {
	case oxp_mini_amd_a07:
		reg = OXP_MINI_TURBO_SWITCH_REG;
		mask = OXP_MINI_TURBO_TAKE_VAL;
		break;
	case aok_zoe_a1:
	case oxp_fly:
	case oxp_mini_amd_pro:
	case oxp_g1_a:
		reg = OXP_TURBO_SWITCH_REG;
		mask = OXP_TURBO_TAKE_VAL;
		break;
	case oxp_2:
	case oxp_x1:
	case oxp_g1_i:
		reg = OXP_2_TURBO_SWITCH_REG;
		mask = OXP_TURBO_TAKE_VAL;
		break;
	default:
		return -EINVAL;
	}

	retval = read_from_ec(reg, 1, &val);
	if (retval)
		return retval;

	return sysfs_emit(buf, "%d\n", (val & mask) == mask);
}

static DEVICE_ATTR_RW(tt_toggle);

/* Callbacks for turbo LED attribute */
static umode_t tt_led_is_visible(struct kobject *kobj,
				 struct attribute *attr, int n)
{
	switch (board) {
	case oxp_x1:
		return attr->mode;
	default:
		break;
	}
	return 0;
}

static ssize_t tt_led_store(struct device *dev,
			    struct device_attribute *attr, const char *buf,
			    size_t count)
{
	u8 reg, val;
	bool value;
	int ret;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	switch (board) {
	case oxp_x1:
		reg = OXP_X1_TURBO_LED_REG;
		val = value ? OXP_X1_TURBO_LED_ON : OXP_X1_TURBO_LED_OFF;
		break;
	default:
		return -EINVAL;
	}

	ret = write_to_ec(reg, val);
	if (ret)
		return ret;

	return count;
}

static ssize_t tt_led_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	long enval;
	long val;
	int ret;
	u8 reg;

	switch (board) {
	case oxp_x1:
		reg = OXP_X1_TURBO_LED_REG;
		enval = OXP_X1_TURBO_LED_ON;
		break;
	default:
		return -EINVAL;
	}

	ret = read_from_ec(reg, 1, &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", val == enval);
}

static DEVICE_ATTR_RW(tt_led);

/* Callbacks for charge behaviour attributes */
static bool oxp_psy_ext_supported(void)
{
	switch (board) {
	case oxp_x1:
	case oxp_g1_i:
	case oxp_g1_a:
	case oxp_fly:
		return true;
	default:
		break;
	}
	return false;
}

static int oxp_psy_ext_get_prop(struct power_supply *psy,
				const struct power_supply_ext *ext,
				void *data,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	long raw_val;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		ret = read_from_ec(OXP_X1_CHARGE_LIMIT_REG, 1, &raw_val);
		if (ret)
			return ret;
		if (raw_val < 0 || raw_val > 100)
			return -EINVAL;
		val->intval = raw_val;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		ret = read_from_ec(OXP_X1_CHARGE_INHIBIT_REG, 1, &raw_val);
		if (ret)
			return ret;
		if ((raw_val & OXP_X1_CHARGE_INHIBIT_MASK_ALWAYS) ==
		    OXP_X1_CHARGE_INHIBIT_MASK_ALWAYS)
			val->intval = POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
		else if ((raw_val & OXP_X1_CHARGE_INHIBIT_MASK_AWAKE) ==
			 OXP_X1_CHARGE_INHIBIT_MASK_AWAKE)
			val->intval = POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE_AWAKE;
		else
			val->intval = POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
		return 0;
	default:
		return -EINVAL;
	}
}

static int oxp_psy_ext_set_prop(struct power_supply *psy,
				const struct power_supply_ext *ext,
				void *data,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	long raw_val;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		if (val->intval < 0 || val->intval > 100)
			return -EINVAL;
		return write_to_ec(OXP_X1_CHARGE_LIMIT_REG, val->intval);
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		switch (val->intval) {
		case POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO:
			raw_val = 0;
			break;
		case POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE_AWAKE:
			raw_val = OXP_X1_CHARGE_INHIBIT_MASK_AWAKE;
			break;
		case POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE:
			raw_val = OXP_X1_CHARGE_INHIBIT_MASK_ALWAYS;
			break;
		default:
			return -EINVAL;
		}

		return write_to_ec(OXP_X1_CHARGE_INHIBIT_REG, raw_val);
	default:
		return -EINVAL;
	}
}

static int oxp_psy_prop_is_writeable(struct power_supply *psy,
				     const struct power_supply_ext *ext,
				     void *data,
				     enum power_supply_property psp)
{
	return true;
}

static const enum power_supply_property oxp_psy_ext_props[] = {
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
};

static const struct power_supply_ext oxp_psy_ext = {
	.name			= "oxp-charge-control",
	.properties		= oxp_psy_ext_props,
	.num_properties		= ARRAY_SIZE(oxp_psy_ext_props),
	.charge_behaviours	= EC_CHARGE_CONTROL_BEHAVIOURS,
	.get_property		= oxp_psy_ext_get_prop,
	.set_property		= oxp_psy_ext_set_prop,
	.property_is_writeable	= oxp_psy_prop_is_writeable,
};

static int oxp_add_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	return power_supply_register_extension(battery, &oxp_psy_ext, oxp_dev, NULL);
}

static int oxp_remove_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	power_supply_unregister_extension(battery, &oxp_psy_ext);
	return 0;
}

static struct acpi_battery_hook battery_hook = {
	.add_battery	= oxp_add_battery,
	.remove_battery	= oxp_remove_battery,
	.name		= "OneXPlayer Battery",
};

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
	case oxp_g1_i:
	case oxp_g1_a:
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
	case oxp_g1_i:
	case oxp_g1_a:
		return write_to_ec(OXP_SENSOR_PWM_ENABLE_REG, PWM_MODE_AUTO);
	default:
		return -EINVAL;
	}
}

static int oxp_pwm_read(long *val)
{
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
	case oxp_g1_i:
	case oxp_g1_a:
		return read_from_ec(OXP_SENSOR_PWM_ENABLE_REG, 1, val);
	default:
		return -EOPNOTSUPP;
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

/* Fan speed read function */
static int oxp_pwm_fan_speed(long *val)
{
	switch (board) {
	case orange_pi_neo:
		return read_from_ec(ORANGEPI_SENSOR_FAN_REG, 2, val);
	case oxp_2:
	case oxp_x1:
	case oxp_g1_i:
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
	case oxp_g1_a:
		return read_from_ec(OXP_SENSOR_FAN_REG, 2, val);
	default:
		return -EOPNOTSUPP;
	}
}

/* PWM input read/write functions */
static int oxp_pwm_input_write(long val)
{
	if (val < 0 || val > 255)
		return -EINVAL;

	switch (board) {
	case orange_pi_neo:
		/* scale to range [1-244] */
		val = ((val - 1) * 243 / 254) + 1;
		return write_to_ec(ORANGEPI_SENSOR_PWM_REG, val);
	case oxp_2:
	case oxp_x1:
	case oxp_g1_i:
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
	case oxp_g1_a:
		return write_to_ec(OXP_SENSOR_PWM_REG, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int oxp_pwm_input_read(long *val)
{
	int ret;

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
	case oxp_g1_i:
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
	case oxp_g1_a:
	default:
		ret = read_from_ec(OXP_SENSOR_PWM_REG, 1, val);
		if (ret)
			return ret;
		break;
	}
	return 0;
}

static int oxp_platform_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	int ret;

	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			return oxp_pwm_fan_speed(val);
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			return oxp_pwm_input_read(val);
		case hwmon_pwm_enable:
			ret = oxp_pwm_read(val);
			if (ret)
				return ret;

			/* Check for auto and return 2 */
			if (!*val) {
				*val = 2;
				return 0;
			}

			/* Return 0 if at full fan speed, 1 otherwise */
			ret = oxp_pwm_fan_speed(val);
			if (ret)
				return ret;

			if (*val == 255)
				*val = 0;
			else
				*val = 1;

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

static int oxp_platform_write(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long val)
{
	int ret;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_enable:
			if (val == 1)
				return oxp_pwm_enable();
			else if (val == 2)
				return oxp_pwm_disable();
			else if (val != 0)
				return -EINVAL;

			/* Enable PWM and set to max speed */
			ret = oxp_pwm_enable();
			if (ret)
				return ret;
			return oxp_pwm_input_write(255);
		case hwmon_pwm_input:
			return oxp_pwm_input_write(val);
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

static struct attribute *oxp_tt_toggle_attrs[] = {
	&dev_attr_tt_toggle.attr,
	NULL
};

static const struct attribute_group oxp_tt_toggle_attribute_group = {
	.is_visible = tt_toggle_is_visible,
	.attrs = oxp_tt_toggle_attrs,
};

static struct attribute *oxp_tt_led_attrs[] = {
	&dev_attr_tt_led.attr,
	NULL
};

static const struct attribute_group oxp_tt_led_attribute_group = {
	.is_visible = tt_led_is_visible,
	.attrs = oxp_tt_led_attrs,
};

static const struct attribute_group *oxp_ec_groups[] = {
	&oxp_tt_toggle_attribute_group,
	&oxp_tt_led_attribute_group,
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
	int ret;

	oxp_dev = dev;
	hwdev = devm_hwmon_device_register_with_info(dev, "oxp_ec", NULL,
						     &oxp_ec_chip_info, NULL);

	if (IS_ERR(hwdev))
		return PTR_ERR(hwdev);

	if (oxp_psy_ext_supported()) {
		ret = devm_battery_hook_register(dev, &battery_hook);
		if (ret)
			return ret;
	}

	return 0;
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
