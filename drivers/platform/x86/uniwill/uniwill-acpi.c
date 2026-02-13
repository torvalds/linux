// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux driver for Uniwill notebooks.
 *
 * Special thanks go to Pőcze Barnabás, Christoffer Sandberg and Werner Sembach
 * for supporting the development of this driver either through prior work or
 * by answering questions regarding the underlying ACPI and WMI interfaces.
 *
 * Copyright (C) 2025 Armin Wolf <W_Armin@gmx.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/device/driver.h>
#include <linux/dmi.h>
#include <linux/errno.h>
#include <linux/fixp-arith.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/leds.h>
#include <linux/led-class-multicolor.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/units.h>

#include <acpi/battery.h>

#include "uniwill-wmi.h"

#define EC_ADDR_BAT_POWER_UNIT_1	0x0400

#define EC_ADDR_BAT_POWER_UNIT_2	0x0401

#define EC_ADDR_BAT_DESIGN_CAPACITY_1	0x0402

#define EC_ADDR_BAT_DESIGN_CAPACITY_2	0x0403

#define EC_ADDR_BAT_FULL_CAPACITY_1	0x0404

#define EC_ADDR_BAT_FULL_CAPACITY_2	0x0405

#define EC_ADDR_BAT_DESIGN_VOLTAGE_1	0x0408

#define EC_ADDR_BAT_DESIGN_VOLTAGE_2	0x0409

#define EC_ADDR_BAT_STATUS_1		0x0432
#define BAT_DISCHARGING			BIT(0)

#define EC_ADDR_BAT_STATUS_2		0x0433

#define EC_ADDR_BAT_CURRENT_1		0x0434

#define EC_ADDR_BAT_CURRENT_2		0x0435

#define EC_ADDR_BAT_REMAIN_CAPACITY_1	0x0436

#define EC_ADDR_BAT_REMAIN_CAPACITY_2	0x0437

#define EC_ADDR_BAT_VOLTAGE_1		0x0438

#define EC_ADDR_BAT_VOLTAGE_2		0x0439

#define EC_ADDR_CPU_TEMP		0x043E

#define EC_ADDR_GPU_TEMP		0x044F

#define EC_ADDR_SYSTEM_ID		0x0456
#define HAS_GPU				BIT(7)

#define EC_ADDR_MAIN_FAN_RPM_1		0x0464

#define EC_ADDR_MAIN_FAN_RPM_2		0x0465

#define EC_ADDR_SECOND_FAN_RPM_1	0x046C

#define EC_ADDR_SECOND_FAN_RPM_2	0x046D

#define EC_ADDR_DEVICE_STATUS		0x047B
#define WIFI_STATUS_ON			BIT(7)
/* BIT(5) is also unset depending on the rfkill state (bluetooth?) */

#define EC_ADDR_BAT_ALERT		0x0494

#define EC_ADDR_BAT_CYCLE_COUNT_1	0x04A6

#define EC_ADDR_BAT_CYCLE_COUNT_2	0x04A7

#define EC_ADDR_PROJECT_ID		0x0740

#define EC_ADDR_AP_OEM			0x0741
#define	ENABLE_MANUAL_CTRL		BIT(0)
#define ITE_KBD_EFFECT_REACTIVE		BIT(3)
#define FAN_ABNORMAL			BIT(5)

#define EC_ADDR_SUPPORT_5		0x0742
#define FAN_TURBO_SUPPORTED		BIT(4)
#define FAN_SUPPORT			BIT(5)

#define EC_ADDR_CTGP_DB_CTRL		0x0743
#define CTGP_DB_GENERAL_ENABLE		BIT(0)
#define CTGP_DB_DB_ENABLE		BIT(1)
#define CTGP_DB_CTGP_ENABLE		BIT(2)

#define EC_ADDR_CTGP_DB_CTGP_OFFSET	0x0744

#define EC_ADDR_CTGP_DB_TPP_OFFSET	0x0745

#define EC_ADDR_CTGP_DB_DB_OFFSET	0x0746

#define EC_ADDR_LIGHTBAR_AC_CTRL	0x0748
#define LIGHTBAR_APP_EXISTS		BIT(0)
#define LIGHTBAR_POWER_SAVE		BIT(1)
#define LIGHTBAR_S0_OFF			BIT(2)
#define LIGHTBAR_S3_OFF			BIT(3)	// Breathing animation when suspended
#define LIGHTBAR_WELCOME		BIT(7)	// Rainbow animation

#define EC_ADDR_LIGHTBAR_AC_RED		0x0749

#define EC_ADDR_LIGHTBAR_AC_GREEN	0x074A

#define EC_ADDR_LIGHTBAR_AC_BLUE	0x074B

#define EC_ADDR_BIOS_OEM		0x074E
#define FN_LOCK_STATUS			BIT(4)

#define EC_ADDR_MANUAL_FAN_CTRL		0x0751
#define FAN_LEVEL_MASK			GENMASK(2, 0)
#define FAN_MODE_TURBO			BIT(4)
#define FAN_MODE_HIGH			BIT(5)
#define FAN_MODE_BOOST			BIT(6)
#define FAN_MODE_USER			BIT(7)

#define EC_ADDR_PWM_1			0x075B

#define EC_ADDR_PWM_2			0x075C

/* Unreliable */
#define EC_ADDR_SUPPORT_1		0x0765
#define AIRPLANE_MODE			BIT(0)
#define GPS_SWITCH			BIT(1)
#define OVERCLOCK			BIT(2)
#define MACRO_KEY			BIT(3)
#define SHORTCUT_KEY			BIT(4)
#define SUPER_KEY_LOCK			BIT(5)
#define LIGHTBAR			BIT(6)
#define FAN_BOOST			BIT(7)

#define EC_ADDR_SUPPORT_2		0x0766
#define SILENT_MODE			BIT(0)
#define USB_CHARGING			BIT(1)
#define RGB_KEYBOARD			BIT(2)
#define CHINA_MODE			BIT(5)
#define MY_BATTERY			BIT(6)

#define EC_ADDR_TRIGGER			0x0767
#define TRIGGER_SUPER_KEY_LOCK		BIT(0)
#define TRIGGER_LIGHTBAR		BIT(1)
#define TRIGGER_FAN_BOOST		BIT(2)
#define TRIGGER_SILENT_MODE		BIT(3)
#define TRIGGER_USB_CHARGING		BIT(4)
#define RGB_APPLY_COLOR			BIT(5)
#define RGB_LOGO_EFFECT			BIT(6)
#define RGB_RAINBOW_EFFECT		BIT(7)

#define EC_ADDR_SWITCH_STATUS		0x0768
#define SUPER_KEY_LOCK_STATUS		BIT(0)
#define LIGHTBAR_STATUS			BIT(1)
#define FAN_BOOST_STATUS		BIT(2)
#define MACRO_KEY_STATUS		BIT(3)
#define MY_BAT_POWER_BAT_STATUS		BIT(4)

#define EC_ADDR_RGB_RED			0x0769

#define EC_ADDR_RGB_GREEN		0x076A

#define EC_ADDR_RGB_BLUE		0x076B

#define EC_ADDR_ROMID_START		0x0770
#define ROMID_LENGTH			14

#define EC_ADDR_ROMID_EXTRA_1		0x077E

#define EC_ADDR_ROMID_EXTRA_2		0x077F

#define EC_ADDR_BIOS_OEM_2		0x0782
#define FAN_V2_NEW			BIT(0)
#define FAN_QKEY			BIT(1)
#define FAN_TABLE_OFFICE_MODE		BIT(2)
#define FAN_V3				BIT(3)
#define DEFAULT_MODE			BIT(4)

#define EC_ADDR_PL1_SETTING		0x0783

#define EC_ADDR_PL2_SETTING		0x0784

#define EC_ADDR_PL4_SETTING		0x0785

#define EC_ADDR_FAN_DEFAULT		0x0786
#define FAN_CURVE_LENGTH		5

#define EC_ADDR_KBD_STATUS		0x078C
#define KBD_WHITE_ONLY			BIT(0)	// ~single color
#define KBD_SINGLE_COLOR_OFF		BIT(1)
#define KBD_TURBO_LEVEL_MASK		GENMASK(3, 2)
#define KBD_APPLY			BIT(4)
#define KBD_BRIGHTNESS			GENMASK(7, 5)

#define EC_ADDR_FAN_CTRL		0x078E
#define FAN3P5				BIT(1)
#define CHARGING_PROFILE		BIT(3)
#define UNIVERSAL_FAN_CTRL		BIT(6)

#define EC_ADDR_BIOS_OEM_3		0x07A3
#define FAN_REDUCED_DURY_CYCLE		BIT(5)
#define FAN_ALWAYS_ON			BIT(6)

#define EC_ADDR_BIOS_BYTE		0x07A4
#define FN_LOCK_SWITCH			BIT(3)

#define EC_ADDR_OEM_3			0x07A5
#define POWER_LED_MASK			GENMASK(1, 0)
#define POWER_LED_LEFT			0x00
#define POWER_LED_BOTH			0x01
#define POWER_LED_NONE			0x02
#define FAN_QUIET			BIT(2)
#define OVERBOOST			BIT(4)
#define HIGH_POWER			BIT(7)

#define EC_ADDR_OEM_4			0x07A6
#define OVERBOOST_DYN_TEMP_OFF		BIT(1)
#define TOUCHPAD_TOGGLE_OFF		BIT(6)

#define EC_ADDR_CHARGE_CTRL		0x07B9
#define CHARGE_CTRL_MASK		GENMASK(6, 0)
#define CHARGE_CTRL_REACHED		BIT(7)

#define EC_ADDR_UNIVERSAL_FAN_CTRL	0x07C5
#define SPLIT_TABLES			BIT(7)

#define EC_ADDR_AP_OEM_6		0x07C6
#define ENABLE_UNIVERSAL_FAN_CTRL	BIT(2)
#define BATTERY_CHARGE_FULL_OVER_24H	BIT(3)
#define BATTERY_ERM_STATUS_REACHED	BIT(4)

#define EC_ADDR_CHARGE_PRIO		0x07CC
#define CHARGING_PERFORMANCE		BIT(7)

/* Same bits as EC_ADDR_LIGHTBAR_AC_CTRL except LIGHTBAR_S3_OFF */
#define EC_ADDR_LIGHTBAR_BAT_CTRL	0x07E2

#define EC_ADDR_LIGHTBAR_BAT_RED	0x07E3

#define EC_ADDR_LIGHTBAR_BAT_GREEN	0x07E4

#define EC_ADDR_LIGHTBAR_BAT_BLUE	0x07E5

#define EC_ADDR_CPU_TEMP_END_TABLE	0x0F00

#define EC_ADDR_CPU_TEMP_START_TABLE	0x0F10

#define EC_ADDR_CPU_FAN_SPEED_TABLE	0x0F20

#define EC_ADDR_GPU_TEMP_END_TABLE	0x0F30

#define EC_ADDR_GPU_TEMP_START_TABLE	0x0F40

#define EC_ADDR_GPU_FAN_SPEED_TABLE	0x0F50

/*
 * Those two registers technically allow for manual fan control,
 * but are unstable on some models and are likely not meant to
 * be used by applications as they are only accessible when using
 * the WMI interface.
 */
#define EC_ADDR_PWM_1_WRITEABLE		0x1804

#define EC_ADDR_PWM_2_WRITEABLE		0x1809

#define DRIVER_NAME	"uniwill"

/*
 * The OEM software always sleeps up to 6 ms after reading/writing EC
 * registers, so we emulate this behaviour for maximum compatibility.
 */
#define UNIWILL_EC_DELAY_US	6000

#define PWM_MAX			200
#define FAN_TABLE_LENGTH	16

#define LED_CHANNELS		3
#define LED_MAX_BRIGHTNESS	200

#define UNIWILL_FEATURE_FN_LOCK_TOGGLE		BIT(0)
#define UNIWILL_FEATURE_SUPER_KEY_TOGGLE	BIT(1)
#define UNIWILL_FEATURE_TOUCHPAD_TOGGLE		BIT(2)
#define UNIWILL_FEATURE_LIGHTBAR		BIT(3)
#define UNIWILL_FEATURE_BATTERY			BIT(4)
#define UNIWILL_FEATURE_HWMON			BIT(5)
#define UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL	BIT(6)

struct uniwill_data {
	struct device *dev;
	acpi_handle handle;
	struct regmap *regmap;
	unsigned int features;
	struct acpi_battery_hook hook;
	unsigned int last_charge_ctrl;
	struct mutex battery_lock;	/* Protects the list of currently registered batteries */
	unsigned int last_switch_status;
	struct mutex super_key_lock;	/* Protects the toggling of the super key lock state */
	struct list_head batteries;
	struct mutex led_lock;		/* Protects writes to the lightbar registers */
	struct led_classdev_mc led_mc_cdev;
	struct mc_subled led_mc_subled_info[LED_CHANNELS];
	struct mutex input_lock;	/* Protects input sequence during notify */
	struct input_dev *input_device;
	struct notifier_block nb;
};

struct uniwill_battery_entry {
	struct list_head head;
	struct power_supply *battery;
};

struct uniwill_device_descriptor {
	unsigned int features;
	/* Executed during driver probing */
	int (*probe)(struct uniwill_data *data);
};

static bool force;
module_param_unsafe(force, bool, 0);
MODULE_PARM_DESC(force, "Force loading without checking for supported devices\n");

/*
 * Contains device specific data like the feature bitmap since
 * the associated registers are not always reliable.
 */
static struct uniwill_device_descriptor device_descriptor __ro_after_init;

static const char * const uniwill_temp_labels[] = {
	"CPU",
	"GPU",
};

static const char * const uniwill_fan_labels[] = {
	"Main",
	"Secondary",
};

static const struct key_entry uniwill_keymap[] = {
	/* Reported via keyboard controller */
	{ KE_IGNORE,    UNIWILL_OSD_CAPSLOCK,                   { KEY_CAPSLOCK }},
	{ KE_IGNORE,    UNIWILL_OSD_NUMLOCK,                    { KEY_NUMLOCK }},

	/* Reported when the user locks/unlocks the super key */
	{ KE_IGNORE,    UNIWILL_OSD_SUPER_KEY_LOCK_ENABLE,      { KEY_UNKNOWN }},
	{ KE_IGNORE,    UNIWILL_OSD_SUPER_KEY_LOCK_DISABLE,     { KEY_UNKNOWN }},
	/* Optional, might not be reported by all devices */
	{ KE_IGNORE,	UNIWILL_OSD_SUPER_KEY_LOCK_CHANGED,	{ KEY_UNKNOWN }},

	/* Reported in manual mode when toggling the airplane mode status */
	{ KE_KEY,       UNIWILL_OSD_RFKILL,                     { KEY_RFKILL }},
	{ KE_IGNORE,    UNIWILL_OSD_RADIOON,                    { KEY_UNKNOWN }},
	{ KE_IGNORE,    UNIWILL_OSD_RADIOOFF,                   { KEY_UNKNOWN }},

	/* Reported when user wants to cycle the platform profile */
	{ KE_KEY,       UNIWILL_OSD_PERFORMANCE_MODE_TOGGLE,    { KEY_F14 }},

	/* Reported when the user wants to adjust the brightness of the keyboard */
	{ KE_KEY,       UNIWILL_OSD_KBDILLUMDOWN,               { KEY_KBDILLUMDOWN }},
	{ KE_KEY,       UNIWILL_OSD_KBDILLUMUP,                 { KEY_KBDILLUMUP }},

	/* Reported when the user wants to toggle the microphone mute status */
	{ KE_KEY,       UNIWILL_OSD_MIC_MUTE,                   { KEY_MICMUTE }},

	/* Reported when the user wants to toggle the mute status */
	{ KE_IGNORE,    UNIWILL_OSD_MUTE,                       { KEY_MUTE }},

	/* Reported when the user locks/unlocks the Fn key */
	{ KE_IGNORE,    UNIWILL_OSD_FN_LOCK,                    { KEY_FN_ESC }},

	/* Reported when the user wants to toggle the brightness of the keyboard */
	{ KE_KEY,       UNIWILL_OSD_KBDILLUMTOGGLE,             { KEY_KBDILLUMTOGGLE }},
	{ KE_KEY,       UNIWILL_OSD_KB_LED_LEVEL0,              { KEY_KBDILLUMTOGGLE }},
	{ KE_KEY,       UNIWILL_OSD_KB_LED_LEVEL1,              { KEY_KBDILLUMTOGGLE }},
	{ KE_KEY,       UNIWILL_OSD_KB_LED_LEVEL2,              { KEY_KBDILLUMTOGGLE }},
	{ KE_KEY,       UNIWILL_OSD_KB_LED_LEVEL3,              { KEY_KBDILLUMTOGGLE }},
	{ KE_KEY,       UNIWILL_OSD_KB_LED_LEVEL4,              { KEY_KBDILLUMTOGGLE }},

	/* FIXME: find out the exact meaning of those events */
	{ KE_IGNORE,    UNIWILL_OSD_BAT_CHARGE_FULL_24_H,       { KEY_UNKNOWN }},
	{ KE_IGNORE,    UNIWILL_OSD_BAT_ERM_UPDATE,             { KEY_UNKNOWN }},

	/* Reported when the user wants to toggle the benchmark mode status */
	{ KE_IGNORE,    UNIWILL_OSD_BENCHMARK_MODE_TOGGLE,      { KEY_UNKNOWN }},

	/* Reported when the user wants to toggle the webcam */
	{ KE_IGNORE,    UNIWILL_OSD_WEBCAM_TOGGLE,              { KEY_UNKNOWN }},

	{ KE_END }
};

static inline bool uniwill_device_supports(struct uniwill_data *data,
					   unsigned int features)
{
	return (data->features & features) == features;
}

static int uniwill_ec_reg_write(void *context, unsigned int reg, unsigned int val)
{
	union acpi_object params[2] = {
		{
			.integer = {
				.type = ACPI_TYPE_INTEGER,
				.value = reg,
			},
		},
		{
			.integer = {
				.type = ACPI_TYPE_INTEGER,
				.value = val,
			},
		},
	};
	struct uniwill_data *data = context;
	struct acpi_object_list input = {
		.count = ARRAY_SIZE(params),
		.pointer = params,
	};
	acpi_status status;

	status = acpi_evaluate_object(data->handle, "ECRW", &input, NULL);
	if (ACPI_FAILURE(status))
		return -EIO;

	usleep_range(UNIWILL_EC_DELAY_US, UNIWILL_EC_DELAY_US * 2);

	return 0;
}

static int uniwill_ec_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	union acpi_object params[1] = {
		{
			.integer = {
				.type = ACPI_TYPE_INTEGER,
				.value = reg,
			},
		},
	};
	struct uniwill_data *data = context;
	struct acpi_object_list input = {
		.count = ARRAY_SIZE(params),
		.pointer = params,
	};
	unsigned long long output;
	acpi_status status;

	status = acpi_evaluate_integer(data->handle, "ECRR", &input, &output);
	if (ACPI_FAILURE(status))
		return -EIO;

	if (output > U8_MAX)
		return -ENXIO;

	usleep_range(UNIWILL_EC_DELAY_US, UNIWILL_EC_DELAY_US * 2);

	*val = output;

	return 0;
}

static const struct regmap_bus uniwill_ec_bus = {
	.reg_write = uniwill_ec_reg_write,
	.reg_read = uniwill_ec_reg_read,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static bool uniwill_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case EC_ADDR_AP_OEM:
	case EC_ADDR_LIGHTBAR_AC_CTRL:
	case EC_ADDR_LIGHTBAR_AC_RED:
	case EC_ADDR_LIGHTBAR_AC_GREEN:
	case EC_ADDR_LIGHTBAR_AC_BLUE:
	case EC_ADDR_BIOS_OEM:
	case EC_ADDR_TRIGGER:
	case EC_ADDR_OEM_4:
	case EC_ADDR_CHARGE_CTRL:
	case EC_ADDR_LIGHTBAR_BAT_CTRL:
	case EC_ADDR_LIGHTBAR_BAT_RED:
	case EC_ADDR_LIGHTBAR_BAT_GREEN:
	case EC_ADDR_LIGHTBAR_BAT_BLUE:
	case EC_ADDR_CTGP_DB_CTRL:
	case EC_ADDR_CTGP_DB_CTGP_OFFSET:
	case EC_ADDR_CTGP_DB_TPP_OFFSET:
	case EC_ADDR_CTGP_DB_DB_OFFSET:
		return true;
	default:
		return false;
	}
}

static bool uniwill_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case EC_ADDR_CPU_TEMP:
	case EC_ADDR_GPU_TEMP:
	case EC_ADDR_MAIN_FAN_RPM_1:
	case EC_ADDR_MAIN_FAN_RPM_2:
	case EC_ADDR_SECOND_FAN_RPM_1:
	case EC_ADDR_SECOND_FAN_RPM_2:
	case EC_ADDR_BAT_ALERT:
	case EC_ADDR_PROJECT_ID:
	case EC_ADDR_AP_OEM:
	case EC_ADDR_LIGHTBAR_AC_CTRL:
	case EC_ADDR_LIGHTBAR_AC_RED:
	case EC_ADDR_LIGHTBAR_AC_GREEN:
	case EC_ADDR_LIGHTBAR_AC_BLUE:
	case EC_ADDR_BIOS_OEM:
	case EC_ADDR_PWM_1:
	case EC_ADDR_PWM_2:
	case EC_ADDR_TRIGGER:
	case EC_ADDR_SWITCH_STATUS:
	case EC_ADDR_OEM_4:
	case EC_ADDR_CHARGE_CTRL:
	case EC_ADDR_LIGHTBAR_BAT_CTRL:
	case EC_ADDR_LIGHTBAR_BAT_RED:
	case EC_ADDR_LIGHTBAR_BAT_GREEN:
	case EC_ADDR_LIGHTBAR_BAT_BLUE:
	case EC_ADDR_SYSTEM_ID:
	case EC_ADDR_CTGP_DB_CTRL:
	case EC_ADDR_CTGP_DB_CTGP_OFFSET:
	case EC_ADDR_CTGP_DB_TPP_OFFSET:
	case EC_ADDR_CTGP_DB_DB_OFFSET:
		return true;
	default:
		return false;
	}
}

static bool uniwill_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case EC_ADDR_CPU_TEMP:
	case EC_ADDR_GPU_TEMP:
	case EC_ADDR_MAIN_FAN_RPM_1:
	case EC_ADDR_MAIN_FAN_RPM_2:
	case EC_ADDR_SECOND_FAN_RPM_1:
	case EC_ADDR_SECOND_FAN_RPM_2:
	case EC_ADDR_BAT_ALERT:
	case EC_ADDR_PWM_1:
	case EC_ADDR_PWM_2:
	case EC_ADDR_TRIGGER:
	case EC_ADDR_SWITCH_STATUS:
	case EC_ADDR_CHARGE_CTRL:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config uniwill_ec_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.writeable_reg = uniwill_writeable_reg,
	.readable_reg = uniwill_readable_reg,
	.volatile_reg = uniwill_volatile_reg,
	.can_sleep = true,
	.max_register = 0xFFF,
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static ssize_t fn_lock_toggle_enable_store(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret < 0)
		return ret;

	if (enable)
		value = FN_LOCK_STATUS;
	else
		value = 0;

	ret = regmap_update_bits(data->regmap, EC_ADDR_BIOS_OEM, FN_LOCK_STATUS, value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t fn_lock_toggle_enable_show(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_BIOS_OEM, &value);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", !!(value & FN_LOCK_STATUS));
}

static DEVICE_ATTR_RW(fn_lock_toggle_enable);

static ssize_t super_key_toggle_enable_store(struct device *dev, struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret < 0)
		return ret;

	guard(mutex)(&data->super_key_lock);

	ret = regmap_read(data->regmap, EC_ADDR_SWITCH_STATUS, &value);
	if (ret < 0)
		return ret;

	/*
	 * We can only toggle the super key lock, so we return early if the setting
	 * is already in the correct state.
	 */
	if (enable == !(value & SUPER_KEY_LOCK_STATUS))
		return count;

	ret = regmap_write_bits(data->regmap, EC_ADDR_TRIGGER, TRIGGER_SUPER_KEY_LOCK,
				TRIGGER_SUPER_KEY_LOCK);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t super_key_toggle_enable_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_SWITCH_STATUS, &value);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", !(value & SUPER_KEY_LOCK_STATUS));
}

static DEVICE_ATTR_RW(super_key_toggle_enable);

static ssize_t touchpad_toggle_enable_store(struct device *dev, struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret < 0)
		return ret;

	if (enable)
		value = 0;
	else
		value = TOUCHPAD_TOGGLE_OFF;

	ret = regmap_update_bits(data->regmap, EC_ADDR_OEM_4, TOUCHPAD_TOGGLE_OFF, value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t touchpad_toggle_enable_show(struct device *dev, struct device_attribute *attr,
					   char *buf)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_OEM_4, &value);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", !(value & TOUCHPAD_TOGGLE_OFF));
}

static DEVICE_ATTR_RW(touchpad_toggle_enable);

static ssize_t rainbow_animation_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret < 0)
		return ret;

	if (enable)
		value = LIGHTBAR_WELCOME;
	else
		value = 0;

	guard(mutex)(&data->led_lock);

	ret = regmap_update_bits(data->regmap, EC_ADDR_LIGHTBAR_AC_CTRL, LIGHTBAR_WELCOME, value);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(data->regmap, EC_ADDR_LIGHTBAR_BAT_CTRL, LIGHTBAR_WELCOME, value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t rainbow_animation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_LIGHTBAR_AC_CTRL, &value);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", !!(value & LIGHTBAR_WELCOME));
}

static DEVICE_ATTR_RW(rainbow_animation);

static ssize_t breathing_in_suspend_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret < 0)
		return ret;

	if (enable)
		value = 0;
	else
		value = LIGHTBAR_S3_OFF;

	/* We only access a single register here, so we do not need to use data->led_lock */
	ret = regmap_update_bits(data->regmap, EC_ADDR_LIGHTBAR_AC_CTRL, LIGHTBAR_S3_OFF, value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t breathing_in_suspend_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_LIGHTBAR_AC_CTRL, &value);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", !(value & LIGHTBAR_S3_OFF));
}

static DEVICE_ATTR_RW(breathing_in_suspend);

static ssize_t ctgp_offset_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret < 0)
		return ret;

	if (value > U8_MAX)
		return -EINVAL;

	ret = regmap_write(data->regmap, EC_ADDR_CTGP_DB_CTGP_OFFSET, value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t ctgp_offset_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_CTGP_DB_CTGP_OFFSET, &value);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%u\n", value);
}

static DEVICE_ATTR_RW(ctgp_offset);

static int uniwill_nvidia_ctgp_init(struct uniwill_data *data)
{
	int ret;

	if (!uniwill_device_supports(data, UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL))
		return 0;

	ret = regmap_write(data->regmap, EC_ADDR_CTGP_DB_CTGP_OFFSET, 0);
	if (ret < 0)
		return ret;

	ret = regmap_write(data->regmap, EC_ADDR_CTGP_DB_TPP_OFFSET, 255);
	if (ret < 0)
		return ret;

	ret = regmap_write(data->regmap, EC_ADDR_CTGP_DB_DB_OFFSET, 25);
	if (ret < 0)
		return ret;

	ret = regmap_set_bits(data->regmap, EC_ADDR_CTGP_DB_CTRL,
			      CTGP_DB_GENERAL_ENABLE | CTGP_DB_DB_ENABLE | CTGP_DB_CTGP_ENABLE);
	if (ret < 0)
		return ret;

	return 0;
}

static struct attribute *uniwill_attrs[] = {
	/* Keyboard-related */
	&dev_attr_fn_lock_toggle_enable.attr,
	&dev_attr_super_key_toggle_enable.attr,
	&dev_attr_touchpad_toggle_enable.attr,
	/* Lightbar-related */
	&dev_attr_rainbow_animation.attr,
	&dev_attr_breathing_in_suspend.attr,
	/* Power-management-related */
	&dev_attr_ctgp_offset.attr,
	NULL
};

static umode_t uniwill_attr_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct uniwill_data *data = dev_get_drvdata(dev);

	if (attr == &dev_attr_fn_lock_toggle_enable.attr) {
		if (uniwill_device_supports(data, UNIWILL_FEATURE_FN_LOCK_TOGGLE))
			return attr->mode;
	}

	if (attr == &dev_attr_super_key_toggle_enable.attr) {
		if (uniwill_device_supports(data, UNIWILL_FEATURE_SUPER_KEY_TOGGLE))
			return attr->mode;
	}

	if (attr == &dev_attr_touchpad_toggle_enable.attr) {
		if (uniwill_device_supports(data, UNIWILL_FEATURE_TOUCHPAD_TOGGLE))
			return attr->mode;
	}

	if (attr == &dev_attr_rainbow_animation.attr ||
	    attr == &dev_attr_breathing_in_suspend.attr) {
		if (uniwill_device_supports(data, UNIWILL_FEATURE_LIGHTBAR))
			return attr->mode;
	}

	if (attr == &dev_attr_ctgp_offset.attr) {
		if (uniwill_device_supports(data, UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL))
			return attr->mode;
	}

	return 0;
}

static const struct attribute_group uniwill_group = {
	.is_visible = uniwill_attr_is_visible,
	.attrs = uniwill_attrs,
};

static const struct attribute_group *uniwill_groups[] = {
	&uniwill_group,
	NULL
};

static int uniwill_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			long *val)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	__be16 rpm;
	int ret;

	switch (type) {
	case hwmon_temp:
		switch (channel) {
		case 0:
			ret = regmap_read(data->regmap, EC_ADDR_CPU_TEMP, &value);
			break;
		case 1:
			ret = regmap_read(data->regmap, EC_ADDR_GPU_TEMP, &value);
			break;
		default:
			return -EOPNOTSUPP;
		}

		if (ret < 0)
			return ret;

		*val = value * MILLIDEGREE_PER_DEGREE;
		return 0;
	case hwmon_fan:
		switch (channel) {
		case 0:
			ret = regmap_bulk_read(data->regmap, EC_ADDR_MAIN_FAN_RPM_1, &rpm,
					       sizeof(rpm));
			break;
		case 1:
			ret = regmap_bulk_read(data->regmap, EC_ADDR_SECOND_FAN_RPM_1, &rpm,
					       sizeof(rpm));
			break;
		default:
			return -EOPNOTSUPP;
		}

		if (ret < 0)
			return ret;

		*val = be16_to_cpu(rpm);
		return 0;
	case hwmon_pwm:
		switch (channel) {
		case 0:
			ret = regmap_read(data->regmap, EC_ADDR_PWM_1, &value);
			break;
		case 1:
			ret = regmap_read(data->regmap, EC_ADDR_PWM_2, &value);
			break;
		default:
			return -EOPNOTSUPP;
		}

		if (ret < 0)
			return ret;

		*val = fixp_linear_interpolate(0, 0, PWM_MAX, U8_MAX, value);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int uniwill_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			       int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		*str = uniwill_temp_labels[channel];
		return 0;
	case hwmon_fan:
		*str = uniwill_fan_labels[channel];
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops uniwill_ops = {
	.visible = 0444,
	.read = uniwill_read,
	.read_string = uniwill_read_string,
};

static const struct hwmon_channel_info * const uniwill_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_chip_info uniwill_chip_info = {
	.ops = &uniwill_ops,
	.info = uniwill_info,
};

static int uniwill_hwmon_init(struct uniwill_data *data)
{
	struct device *hdev;

	if (!uniwill_device_supports(data, UNIWILL_FEATURE_HWMON))
		return 0;

	hdev = devm_hwmon_device_register_with_info(data->dev, "uniwill", data,
						    &uniwill_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hdev);
}

static const unsigned int uniwill_led_channel_to_bat_reg[LED_CHANNELS] = {
	EC_ADDR_LIGHTBAR_BAT_RED,
	EC_ADDR_LIGHTBAR_BAT_GREEN,
	EC_ADDR_LIGHTBAR_BAT_BLUE,
};

static const unsigned int uniwill_led_channel_to_ac_reg[LED_CHANNELS] = {
	EC_ADDR_LIGHTBAR_AC_RED,
	EC_ADDR_LIGHTBAR_AC_GREEN,
	EC_ADDR_LIGHTBAR_AC_BLUE,
};

static int uniwill_led_brightness_set(struct led_classdev *led_cdev, enum led_brightness brightness)
{
	struct led_classdev_mc *led_mc_cdev = lcdev_to_mccdev(led_cdev);
	struct uniwill_data *data = container_of(led_mc_cdev, struct uniwill_data, led_mc_cdev);
	unsigned int value;
	int ret;

	ret = led_mc_calc_color_components(led_mc_cdev, brightness);
	if (ret < 0)
		return ret;

	guard(mutex)(&data->led_lock);

	for (int i = 0; i < LED_CHANNELS; i++) {
		/* Prevent the brightness values from overflowing */
		value = min(LED_MAX_BRIGHTNESS, data->led_mc_subled_info[i].brightness);
		ret = regmap_write(data->regmap, uniwill_led_channel_to_ac_reg[i], value);
		if (ret < 0)
			return ret;

		ret = regmap_write(data->regmap, uniwill_led_channel_to_bat_reg[i], value);
		if (ret < 0)
			return ret;
	}

	if (brightness)
		value = 0;
	else
		value = LIGHTBAR_S0_OFF;

	ret = regmap_update_bits(data->regmap, EC_ADDR_LIGHTBAR_AC_CTRL, LIGHTBAR_S0_OFF, value);
	if (ret < 0)
		return ret;

	return regmap_update_bits(data->regmap, EC_ADDR_LIGHTBAR_BAT_CTRL, LIGHTBAR_S0_OFF, value);
}

#define LIGHTBAR_MASK	(LIGHTBAR_APP_EXISTS | LIGHTBAR_S0_OFF | LIGHTBAR_S3_OFF | LIGHTBAR_WELCOME)

static int uniwill_led_init(struct uniwill_data *data)
{
	struct led_init_data init_data = {
		.devicename = DRIVER_NAME,
		.default_label = "multicolor:" LED_FUNCTION_STATUS,
		.devname_mandatory = true,
	};
	unsigned int color_indices[3] = {
		LED_COLOR_ID_RED,
		LED_COLOR_ID_GREEN,
		LED_COLOR_ID_BLUE,
	};
	unsigned int value;
	int ret;

	if (!uniwill_device_supports(data, UNIWILL_FEATURE_LIGHTBAR))
		return 0;

	ret = devm_mutex_init(data->dev, &data->led_lock);
	if (ret < 0)
		return ret;

	/*
	 * The EC has separate lightbar settings for AC and battery mode,
	 * so we have to ensure that both settings are the same.
	 */
	ret = regmap_read(data->regmap, EC_ADDR_LIGHTBAR_AC_CTRL, &value);
	if (ret < 0)
		return ret;

	value |= LIGHTBAR_APP_EXISTS;
	ret = regmap_write(data->regmap, EC_ADDR_LIGHTBAR_AC_CTRL, value);
	if (ret < 0)
		return ret;

	/*
	 * The breathing animation during suspend is not supported when
	 * running on battery power.
	 */
	value |= LIGHTBAR_S3_OFF;
	ret = regmap_update_bits(data->regmap, EC_ADDR_LIGHTBAR_BAT_CTRL, LIGHTBAR_MASK, value);
	if (ret < 0)
		return ret;

	data->led_mc_cdev.led_cdev.color = LED_COLOR_ID_MULTI;
	data->led_mc_cdev.led_cdev.max_brightness = LED_MAX_BRIGHTNESS;
	data->led_mc_cdev.led_cdev.flags = LED_REJECT_NAME_CONFLICT;
	data->led_mc_cdev.led_cdev.brightness_set_blocking = uniwill_led_brightness_set;

	if (value & LIGHTBAR_S0_OFF)
		data->led_mc_cdev.led_cdev.brightness = 0;
	else
		data->led_mc_cdev.led_cdev.brightness = LED_MAX_BRIGHTNESS;

	for (int i = 0; i < LED_CHANNELS; i++) {
		data->led_mc_subled_info[i].color_index = color_indices[i];

		ret = regmap_read(data->regmap, uniwill_led_channel_to_ac_reg[i], &value);
		if (ret < 0)
			return ret;

		/*
		 * Make sure that the initial intensity value is not greater than
		 * the maximum brightness.
		 */
		value = min(LED_MAX_BRIGHTNESS, value);
		ret = regmap_write(data->regmap, uniwill_led_channel_to_ac_reg[i], value);
		if (ret < 0)
			return ret;

		ret = regmap_write(data->regmap, uniwill_led_channel_to_bat_reg[i], value);
		if (ret < 0)
			return ret;

		data->led_mc_subled_info[i].intensity = value;
		data->led_mc_subled_info[i].channel = i;
	}

	data->led_mc_cdev.subled_info = data->led_mc_subled_info;
	data->led_mc_cdev.num_colors = LED_CHANNELS;

	return devm_led_classdev_multicolor_register_ext(data->dev, &data->led_mc_cdev,
							 &init_data);
}

static int uniwill_get_property(struct power_supply *psy, const struct power_supply_ext *ext,
				void *drvdata, enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct uniwill_data *data = drvdata;
	union power_supply_propval prop;
	unsigned int regval;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		ret = power_supply_get_property_direct(psy, POWER_SUPPLY_PROP_PRESENT, &prop);
		if (ret < 0)
			return ret;

		if (!prop.intval) {
			val->intval = POWER_SUPPLY_HEALTH_NO_BATTERY;
			return 0;
		}

		ret = power_supply_get_property_direct(psy, POWER_SUPPLY_PROP_STATUS, &prop);
		if (ret < 0)
			return ret;

		if (prop.intval == POWER_SUPPLY_STATUS_UNKNOWN) {
			val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
			return 0;
		}

		ret = regmap_read(data->regmap, EC_ADDR_BAT_ALERT, &regval);
		if (ret < 0)
			return ret;

		if (regval) {
			/* Charging issue */
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			return 0;
		}

		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		ret = regmap_read(data->regmap, EC_ADDR_CHARGE_CTRL, &regval);
		if (ret < 0)
			return ret;

		val->intval = clamp_val(FIELD_GET(CHARGE_CTRL_MASK, regval), 0, 100);
		return 0;
	default:
		return -EINVAL;
	}
}

static int uniwill_set_property(struct power_supply *psy, const struct power_supply_ext *ext,
				void *drvdata, enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct uniwill_data *data = drvdata;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		if (val->intval < 1 || val->intval > 100)
			return -EINVAL;

		return regmap_update_bits(data->regmap, EC_ADDR_CHARGE_CTRL, CHARGE_CTRL_MASK,
					  val->intval);
	default:
		return -EINVAL;
	}
}

static int uniwill_property_is_writeable(struct power_supply *psy,
					 const struct power_supply_ext *ext, void *drvdata,
					 enum power_supply_property psp)
{
	if (psp == POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD)
		return true;

	return false;
}

static const enum power_supply_property uniwill_properties[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
};

static const struct power_supply_ext uniwill_extension = {
	.name = DRIVER_NAME,
	.properties = uniwill_properties,
	.num_properties = ARRAY_SIZE(uniwill_properties),
	.get_property = uniwill_get_property,
	.set_property = uniwill_set_property,
	.property_is_writeable = uniwill_property_is_writeable,
};

static int uniwill_add_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct uniwill_data *data = container_of(hook, struct uniwill_data, hook);
	struct uniwill_battery_entry *entry;
	int ret;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	ret = power_supply_register_extension(battery, &uniwill_extension, data->dev, data);
	if (ret < 0) {
		kfree(entry);
		return ret;
	}

	guard(mutex)(&data->battery_lock);

	entry->battery = battery;
	list_add(&entry->head, &data->batteries);

	return 0;
}

static int uniwill_remove_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct uniwill_data *data = container_of(hook, struct uniwill_data, hook);
	struct uniwill_battery_entry *entry, *tmp;

	scoped_guard(mutex, &data->battery_lock) {
		list_for_each_entry_safe(entry, tmp, &data->batteries, head) {
			if (entry->battery == battery) {
				list_del(&entry->head);
				kfree(entry);
				break;
			}
		}
	}

	power_supply_unregister_extension(battery, &uniwill_extension);

	return 0;
}

static int uniwill_battery_init(struct uniwill_data *data)
{
	int ret;

	if (!uniwill_device_supports(data, UNIWILL_FEATURE_BATTERY))
		return 0;

	ret = devm_mutex_init(data->dev, &data->battery_lock);
	if (ret < 0)
		return ret;

	INIT_LIST_HEAD(&data->batteries);
	data->hook.name = "Uniwill Battery Extension";
	data->hook.add_battery = uniwill_add_battery;
	data->hook.remove_battery = uniwill_remove_battery;

	return devm_battery_hook_register(data->dev, &data->hook);
}

static int uniwill_notifier_call(struct notifier_block *nb, unsigned long action, void *dummy)
{
	struct uniwill_data *data = container_of(nb, struct uniwill_data, nb);
	struct uniwill_battery_entry *entry;

	switch (action) {
	case UNIWILL_OSD_BATTERY_ALERT:
		mutex_lock(&data->battery_lock);
		list_for_each_entry(entry, &data->batteries, head) {
			power_supply_changed(entry->battery);
		}
		mutex_unlock(&data->battery_lock);

		return NOTIFY_OK;
	case UNIWILL_OSD_DC_ADAPTER_CHANGED:
		/* noop for the time being, will change once charging priority
		 * gets implemented.
		 */

		return NOTIFY_OK;
	default:
		mutex_lock(&data->input_lock);
		sparse_keymap_report_event(data->input_device, action, 1, true);
		mutex_unlock(&data->input_lock);

		return NOTIFY_OK;
	}
}

static int uniwill_input_init(struct uniwill_data *data)
{
	int ret;

	ret = devm_mutex_init(data->dev, &data->input_lock);
	if (ret < 0)
		return ret;

	data->input_device = devm_input_allocate_device(data->dev);
	if (!data->input_device)
		return -ENOMEM;

	ret = sparse_keymap_setup(data->input_device, uniwill_keymap, NULL);
	if (ret < 0)
		return ret;

	data->input_device->name = "Uniwill WMI hotkeys";
	data->input_device->phys = "wmi/input0";
	data->input_device->id.bustype = BUS_HOST;
	ret = input_register_device(data->input_device);
	if (ret < 0)
		return ret;

	data->nb.notifier_call = uniwill_notifier_call;

	return devm_uniwill_wmi_register_notifier(data->dev, &data->nb);
}

static void uniwill_disable_manual_control(void *context)
{
	struct uniwill_data *data = context;

	regmap_clear_bits(data->regmap, EC_ADDR_AP_OEM, ENABLE_MANUAL_CTRL);
}

static int uniwill_ec_init(struct uniwill_data *data)
{
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_PROJECT_ID, &value);
	if (ret < 0)
		return ret;

	dev_dbg(data->dev, "Project ID: %u\n", value);

	ret = regmap_set_bits(data->regmap, EC_ADDR_AP_OEM, ENABLE_MANUAL_CTRL);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(data->dev, uniwill_disable_manual_control, data);
}

static int uniwill_probe(struct platform_device *pdev)
{
	struct uniwill_data *data;
	struct regmap *regmap;
	acpi_handle handle;
	int ret;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &pdev->dev;
	data->handle = handle;
	platform_set_drvdata(pdev, data);

	regmap = devm_regmap_init(&pdev->dev, &uniwill_ec_bus, data, &uniwill_ec_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	data->regmap = regmap;
	ret = devm_mutex_init(&pdev->dev, &data->super_key_lock);
	if (ret < 0)
		return ret;

	ret = uniwill_ec_init(data);
	if (ret < 0)
		return ret;

	data->features = device_descriptor.features;

	/*
	 * Some devices might need to perform some device-specific initialization steps
	 * before the supported features are initialized. Because of this we have to call
	 * this callback just after the EC itself was initialized.
	 */
	if (device_descriptor.probe) {
		ret = device_descriptor.probe(data);
		if (ret < 0)
			return ret;
	}

	ret = uniwill_battery_init(data);
	if (ret < 0)
		return ret;

	ret = uniwill_led_init(data);
	if (ret < 0)
		return ret;

	ret = uniwill_hwmon_init(data);
	if (ret < 0)
		return ret;

	ret = uniwill_nvidia_ctgp_init(data);
	if (ret < 0)
		return ret;

	return uniwill_input_init(data);
}

static void uniwill_shutdown(struct platform_device *pdev)
{
	struct uniwill_data *data = platform_get_drvdata(pdev);

	regmap_clear_bits(data->regmap, EC_ADDR_AP_OEM, ENABLE_MANUAL_CTRL);
}

static int uniwill_suspend_keyboard(struct uniwill_data *data)
{
	if (!uniwill_device_supports(data, UNIWILL_FEATURE_SUPER_KEY_TOGGLE))
		return 0;

	/*
	 * The EC_ADDR_SWITCH_STATUS is marked as volatile, so we have to restore it
	 * ourselves.
	 */
	return regmap_read(data->regmap, EC_ADDR_SWITCH_STATUS, &data->last_switch_status);
}

static int uniwill_suspend_battery(struct uniwill_data *data)
{
	if (!uniwill_device_supports(data, UNIWILL_FEATURE_BATTERY))
		return 0;

	/*
	 * Save the current charge limit in order to restore it during resume.
	 * We cannot use the regmap code for that since this register needs to
	 * be declared as volatile due to CHARGE_CTRL_REACHED.
	 */
	return regmap_read(data->regmap, EC_ADDR_CHARGE_CTRL, &data->last_charge_ctrl);
}

static int uniwill_suspend_nvidia_ctgp(struct uniwill_data *data)
{
	if (!uniwill_device_supports(data, UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL))
		return 0;

	return regmap_clear_bits(data->regmap, EC_ADDR_CTGP_DB_CTRL,
				 CTGP_DB_DB_ENABLE | CTGP_DB_CTGP_ENABLE);
}

static int uniwill_suspend(struct device *dev)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	int ret;

	ret = uniwill_suspend_keyboard(data);
	if (ret < 0)
		return ret;

	ret = uniwill_suspend_battery(data);
	if (ret < 0)
		return ret;

	ret = uniwill_suspend_nvidia_ctgp(data);
	if (ret < 0)
		return ret;

	regcache_cache_only(data->regmap, true);
	regcache_mark_dirty(data->regmap);

	return 0;
}

static int uniwill_resume_keyboard(struct uniwill_data *data)
{
	unsigned int value;
	int ret;

	if (!uniwill_device_supports(data, UNIWILL_FEATURE_SUPER_KEY_TOGGLE))
		return 0;

	ret = regmap_read(data->regmap, EC_ADDR_SWITCH_STATUS, &value);
	if (ret < 0)
		return ret;

	if ((data->last_switch_status & SUPER_KEY_LOCK_STATUS) == (value & SUPER_KEY_LOCK_STATUS))
		return 0;

	return regmap_write_bits(data->regmap, EC_ADDR_TRIGGER, TRIGGER_SUPER_KEY_LOCK,
				 TRIGGER_SUPER_KEY_LOCK);
}

static int uniwill_resume_battery(struct uniwill_data *data)
{
	if (!uniwill_device_supports(data, UNIWILL_FEATURE_BATTERY))
		return 0;

	return regmap_update_bits(data->regmap, EC_ADDR_CHARGE_CTRL, CHARGE_CTRL_MASK,
				  data->last_charge_ctrl);
}

static int uniwill_resume_nvidia_ctgp(struct uniwill_data *data)
{
	if (!uniwill_device_supports(data, UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL))
		return 0;

	return regmap_set_bits(data->regmap, EC_ADDR_CTGP_DB_CTRL,
			       CTGP_DB_DB_ENABLE | CTGP_DB_CTGP_ENABLE);
}

static int uniwill_resume(struct device *dev)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	int ret;

	regcache_cache_only(data->regmap, false);

	ret = regcache_sync(data->regmap);
	if (ret < 0)
		return ret;

	ret = uniwill_resume_keyboard(data);
	if (ret < 0)
		return ret;

	ret = uniwill_resume_battery(data);
	if (ret < 0)
		return ret;

	return uniwill_resume_nvidia_ctgp(data);
}

static DEFINE_SIMPLE_DEV_PM_OPS(uniwill_pm_ops, uniwill_suspend, uniwill_resume);

/*
 * We only use the DMI table for auoloading because the ACPI device itself
 * does not guarantee that the underlying EC implementation is supported.
 */
static const struct acpi_device_id uniwill_id_table[] = {
	{ "INOU0000" },
	{ },
};

static struct platform_driver uniwill_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.dev_groups = uniwill_groups,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.acpi_match_table = uniwill_id_table,
		.pm = pm_sleep_ptr(&uniwill_pm_ops),
	},
	.probe = uniwill_probe,
	.shutdown = uniwill_shutdown,
};

static struct uniwill_device_descriptor lapac71h_descriptor __initdata = {
	.features = UNIWILL_FEATURE_FN_LOCK_TOGGLE |
		    UNIWILL_FEATURE_SUPER_KEY_TOGGLE |
		    UNIWILL_FEATURE_TOUCHPAD_TOGGLE |
		    UNIWILL_FEATURE_BATTERY |
		    UNIWILL_FEATURE_HWMON,
};

static struct uniwill_device_descriptor lapkc71f_descriptor __initdata = {
	.features = UNIWILL_FEATURE_FN_LOCK_TOGGLE |
		    UNIWILL_FEATURE_SUPER_KEY_TOGGLE |
		    UNIWILL_FEATURE_TOUCHPAD_TOGGLE |
		    UNIWILL_FEATURE_LIGHTBAR |
		    UNIWILL_FEATURE_BATTERY |
		    UNIWILL_FEATURE_HWMON,
};

static int phxarx1_phxaqf1_probe(struct uniwill_data *data)
{
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_SYSTEM_ID, &value);
	if (ret < 0)
		return ret;

	if (value & HAS_GPU)
		data->features |= UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL;

	return 0;
};

static struct uniwill_device_descriptor phxarx1_phxaqf1_descriptor __initdata = {
	.probe = phxarx1_phxaqf1_probe,
};

static struct uniwill_device_descriptor tux_featureset_1_descriptor __initdata = {
	.features = UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL,
};

static struct uniwill_device_descriptor empty_descriptor __initdata = {};

static const struct dmi_system_id uniwill_dmi_table[] __initconst = {
	{
		.ident = "XMG FUSION 15",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SchenkerTechnologiesGmbH"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "LAPQC71A"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "XMG FUSION 15",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SchenkerTechnologiesGmbH"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "LAPQC71B"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "Intel NUC x15",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel(R) Client Systems"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "LAPAC71H"),
		},
		.driver_data = &lapac71h_descriptor,
	},
	{
		.ident = "Intel NUC x15",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel(R) Client Systems"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "LAPKC71F"),
		},
		.driver_data = &lapkc71f_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14 Gen6 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PHxTxX1"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14 Gen6 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PHxTQx1"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/16 Gen7 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PHxARX1_PHxAQF1"),
		},
		.driver_data = &phxarx1_phxaqf1_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 16 Gen7 Intel/Commodore Omnia-Book Pro Gen 7",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PH6AG01_PH6AQ71_PH6AQI1"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/16 Gen8 Intel/Commodore Omnia-Book Pro Gen 8",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PH4PRX1_PH6PRX1"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14 Gen8 Intel/Commodore Omnia-Book Pro Gen 8",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PH4PG31"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 16 Gen8 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PH6PG01_PH6PG71"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/15 Gen9 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GXxHRXx"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/15 Gen9 Intel/Commodore Omnia-Book 15 Gen9",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GXxMRXx"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/15 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "XxHP4NAx"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/15 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "XxKK4NAx_XxSP4NAx"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 15 Gen10 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "XxAR4NAx"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Max 15 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X5KK45xS_X5SP45xS"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Max 16 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6HP45xU"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Max 16 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6KK45xU_X6SP45xU"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Max 15 Gen10 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X5AR45xS"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Max 16 Gen10 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6AR55xU"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1501A1650TI"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1501A2060"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 17 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1701A1650TI"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 17 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1701A2060"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15 Gen1 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1501I1650TI"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15 Gen1 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1501I2060"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 17 Gen1 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1701I1650TI"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 17 Gen1 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1701I2060"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Trinity 15 Intel Gen1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "TRINITY1501I"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Trinity 17 Intel Gen1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "TRINITY1701I"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15/17 Gen2 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxMGxx"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15/17 Gen2 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxNGxx"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris/Polaris 15/17 Gen3 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxZGxx"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris/Polaris 15/17 Gen3 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxTGxx"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris/Polaris 15/17 Gen4 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxRGxx"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 15 Gen4 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxAGxx"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15/17 Gen5 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxXGxx"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen5 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GM6XGxX"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16/17 Gen5 Intel/Commodore ORION Gen 5",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxPXxx"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris Slim 15 Gen6 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxHGxx"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris Slim 15 Gen6 Intel/Commodore ORION Slim 15 Gen6",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GM5IXxA"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen6 Intel/Commodore ORION 16 Gen6",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GM6IXxB_MB1"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen6 Intel/Commodore ORION 16 Gen6",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GM6IXxB_MB2"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 17 Gen6 Intel/Commodore ORION 17 Gen6",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GM7IXxN"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen7 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6FR5xxY"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen7 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6AR5xxY"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen7 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6AR5xxY_mLED"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Book BA15 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PF5PU1G"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Pulse 14 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PULSE1401"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Pulse 15 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PULSE1501"),
		},
		.driver_data = &empty_descriptor,
	},
	{
		.ident = "TUXEDO Pulse 15 Gen2 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PF5LUXG"),
		},
		.driver_data = &empty_descriptor,
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, uniwill_dmi_table);

static int __init uniwill_init(void)
{
	const struct uniwill_device_descriptor *descriptor;
	const struct dmi_system_id *id;
	int ret;

	id = dmi_first_match(uniwill_dmi_table);
	if (!id) {
		if (!force)
			return -ENODEV;

		/* Assume that the device supports all features */
		device_descriptor.features = UINT_MAX;
		pr_warn("Loading on a potentially unsupported device\n");
	} else {
		/*
		 * Some devices might support additional features depending on
		 * the BIOS version/date, so we call this callback to let them
		 * modify their device descriptor accordingly.
		 */
		if (id->callback) {
			ret = id->callback(id);
			if (ret < 0)
				return ret;
		}

		descriptor = id->driver_data;
		device_descriptor = *descriptor;
	}

	ret = platform_driver_register(&uniwill_driver);
	if (ret < 0)
		return ret;

	ret = uniwill_wmi_register_driver();
	if (ret < 0) {
		platform_driver_unregister(&uniwill_driver);
		return ret;
	}

	return 0;
}
module_init(uniwill_init);

static void __exit uniwill_exit(void)
{
	uniwill_wmi_unregister_driver();
	platform_driver_unregister(&uniwill_driver);
}
module_exit(uniwill_exit);

MODULE_AUTHOR("Armin Wolf <W_Armin@gmx.de>");
MODULE_DESCRIPTION("Uniwill notebook driver");
MODULE_LICENSE("GPL");
