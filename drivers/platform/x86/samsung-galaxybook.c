// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Samsung Galaxy Book driver
 *
 * Copyright (c) 2025 Joshua Grisham <josh@joshuagrisham.com>
 *
 * With contributions to the SCAI ACPI device interface:
 * Copyright (c) 2024 Giulio Girardi <giulio.girardi@protechgroup.it>
 *
 * Implementation inspired by existing x86 platform drivers.
 * Thank you to the authors!
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/i8042.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/platform_profile.h>
#include <linux/serio.h>
#include <linux/sysfs.h>
#include <linux/uuid.h>
#include <linux/workqueue.h>
#include <acpi/battery.h>
#include "firmware_attributes_class.h"

#define DRIVER_NAME "samsung-galaxybook"

struct samsung_galaxybook {
	struct platform_device *platform;
	struct acpi_device *acpi;

	struct device *fw_attrs_dev;
	struct kset *fw_attrs_kset;
	/* block in case firmware attributes are updated in multiple threads */
	struct mutex fw_attr_lock;

	bool has_kbd_backlight;
	bool has_block_recording;
	bool has_performance_mode;

	struct led_classdev kbd_backlight;
	struct work_struct kbd_backlight_hotkey_work;
	/* block in case brightness updated using hotkey and another thread */
	struct mutex kbd_backlight_lock;

	void *i8042_filter_ptr;

	struct work_struct block_recording_hotkey_work;
	struct input_dev *camera_lens_cover_switch;

	struct acpi_battery_hook battery_hook;

	u8 profile_performance_modes[PLATFORM_PROFILE_LAST];
};

enum galaxybook_fw_attr_id {
	GB_ATTR_POWER_ON_LID_OPEN,
	GB_ATTR_USB_CHARGING,
	GB_ATTR_BLOCK_RECORDING,
};

static const char * const galaxybook_fw_attr_name[] = {
	[GB_ATTR_POWER_ON_LID_OPEN] = "power_on_lid_open",
	[GB_ATTR_USB_CHARGING]      = "usb_charging",
	[GB_ATTR_BLOCK_RECORDING]   = "block_recording",
};

static const char * const galaxybook_fw_attr_desc[] = {
	[GB_ATTR_POWER_ON_LID_OPEN] = "Power On Lid Open",
	[GB_ATTR_USB_CHARGING]      = "USB Charging",
	[GB_ATTR_BLOCK_RECORDING]   = "Block Recording",
};

#define GB_ATTR_LANGUAGE_CODE "en_US.UTF-8"

struct galaxybook_fw_attr {
	struct samsung_galaxybook *galaxybook;
	enum galaxybook_fw_attr_id fw_attr_id;
	struct attribute_group attr_group;
	struct kobj_attribute display_name;
	struct kobj_attribute current_value;
	int (*get_value)(struct samsung_galaxybook *galaxybook, bool *value);
	int (*set_value)(struct samsung_galaxybook *galaxybook, const bool value);
};

struct sawb {
	u16 safn;
	u16 sasb;
	u8 rflg;
	union {
		struct {
			u8 gunm;
			u8 guds[250];
		} __packed;
		struct {
			u8 caid[16];
			u8 fncn;
			u8 subn;
			u8 iob0;
			u8 iob1;
			u8 iob2;
			u8 iob3;
			u8 iob4;
			u8 iob5;
			u8 iob6;
			u8 iob7;
			u8 iob8;
			u8 iob9;
		} __packed;
		struct {
			u8 iob_prefix[18];
			u8 iobs[10];
		} __packed;
	} __packed;
} __packed;

#define GB_SAWB_LEN_SETTINGS          0x15
#define GB_SAWB_LEN_PERFORMANCE_MODE  0x100

#define GB_SAFN  0x5843

#define GB_SASB_KBD_BACKLIGHT     0x78
#define GB_SASB_POWER_MANAGEMENT  0x7a
#define GB_SASB_USB_CHARGING_GET  0x67
#define GB_SASB_USB_CHARGING_SET  0x68
#define GB_SASB_NOTIFICATIONS     0x86
#define GB_SASB_BLOCK_RECORDING   0x8a
#define GB_SASB_PERFORMANCE_MODE  0x91

#define GB_SAWB_RFLG_POS     4
#define GB_SAWB_GB_GUNM_POS  5

#define GB_RFLG_SUCCESS  0xaa
#define GB_GUNM_FAIL     0xff

#define GB_GUNM_FEATURE_ENABLE          0xbb
#define GB_GUNM_FEATURE_ENABLE_SUCCESS  0xdd
#define GB_GUDS_FEATURE_ENABLE          0xaa
#define GB_GUDS_FEATURE_ENABLE_SUCCESS  0xcc

#define GB_GUNM_GET  0x81
#define GB_GUNM_SET  0x82

#define GB_GUNM_POWER_MANAGEMENT  0x82

#define GB_GUNM_USB_CHARGING_GET            0x80
#define GB_GUNM_USB_CHARGING_ON             0x81
#define GB_GUNM_USB_CHARGING_OFF            0x80
#define GB_GUDS_POWER_ON_LID_OPEN           0xa3
#define GB_GUDS_POWER_ON_LID_OPEN_GET       0x81
#define GB_GUDS_POWER_ON_LID_OPEN_SET       0x80
#define GB_GUDS_BATTERY_CHARGE_CONTROL      0xe9
#define GB_GUDS_BATTERY_CHARGE_CONTROL_GET  0x91
#define GB_GUDS_BATTERY_CHARGE_CONTROL_SET  0x90
#define GB_GUNM_ACPI_NOTIFY_ENABLE          0x80
#define GB_GUDS_ACPI_NOTIFY_ENABLE          0x02

#define GB_BLOCK_RECORDING_ON   0x0
#define GB_BLOCK_RECORDING_OFF  0x1

#define GB_FNCN_PERFORMANCE_MODE       0x51
#define GB_SUBN_PERFORMANCE_MODE_LIST  0x01
#define GB_SUBN_PERFORMANCE_MODE_GET   0x02
#define GB_SUBN_PERFORMANCE_MODE_SET   0x03

/* guid 8246028d-8bca-4a55-ba0f-6f1e6b921b8f */
static const guid_t performance_mode_guid =
	GUID_INIT(0x8246028d, 0x8bca, 0x4a55, 0xba, 0x0f, 0x6f, 0x1e, 0x6b, 0x92, 0x1b, 0x8f);
#define GB_PERFORMANCE_MODE_GUID performance_mode_guid

#define GB_PERFORMANCE_MODE_FANOFF          0xb
#define GB_PERFORMANCE_MODE_LOWNOISE        0xa
#define GB_PERFORMANCE_MODE_OPTIMIZED       0x0
#define GB_PERFORMANCE_MODE_OPTIMIZED_V2    0x2
#define GB_PERFORMANCE_MODE_PERFORMANCE     0x1
#define GB_PERFORMANCE_MODE_PERFORMANCE_V2  0x15
#define GB_PERFORMANCE_MODE_ULTRA           0x16
#define GB_PERFORMANCE_MODE_IGNORE1         0x14
#define GB_PERFORMANCE_MODE_IGNORE2         0xc

#define GB_ACPI_METHOD_ENABLE            "SDLS"
#define GB_ACPI_METHOD_ENABLE_ON         1
#define GB_ACPI_METHOD_ENABLE_OFF        0
#define GB_ACPI_METHOD_SETTINGS          "CSFI"
#define GB_ACPI_METHOD_PERFORMANCE_MODE  "CSXI"

#define GB_KBD_BACKLIGHT_MAX_BRIGHTNESS  3

#define GB_ACPI_NOTIFY_BATTERY_STATE_CHANGED    0x61
#define GB_ACPI_NOTIFY_DEVICE_ON_TABLE          0x6c
#define GB_ACPI_NOTIFY_DEVICE_OFF_TABLE         0x6d
#define GB_ACPI_NOTIFY_HOTKEY_PERFORMANCE_MODE  0x70

#define GB_KEY_KBD_BACKLIGHT_KEYDOWN    0x2c
#define GB_KEY_KBD_BACKLIGHT_KEYUP      0xac
#define GB_KEY_BLOCK_RECORDING_KEYDOWN  0x1f
#define GB_KEY_BLOCK_RECORDING_KEYUP    0x9f
#define GB_KEY_BATTERY_NOTIFY_KEYUP     0xf
#define GB_KEY_BATTERY_NOTIFY_KEYDOWN   0x8f

/*
 * Optional features which have been determined as not supported on a particular
 * device will return GB_NOT_SUPPORTED from their init function. Positive
 * EOPNOTSUPP is used as the underlying value instead of negative to
 * differentiate this return code from valid upstream failures.
 */
#define GB_NOT_SUPPORTED EOPNOTSUPP /* Galaxy Book feature not supported */

/*
 * ACPI method handling
 */

static int galaxybook_acpi_method(struct samsung_galaxybook *galaxybook, acpi_string method,
				  struct sawb *buf, size_t len)
{
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object in_obj, *out_obj;
	struct acpi_object_list input;
	acpi_status status;
	int err;

	in_obj.type = ACPI_TYPE_BUFFER;
	in_obj.buffer.length = len;
	in_obj.buffer.pointer = (u8 *)buf;

	input.count = 1;
	input.pointer = &in_obj;

	status = acpi_evaluate_object_typed(galaxybook->acpi->handle, method, &input, &output,
					    ACPI_TYPE_BUFFER);

	if (ACPI_FAILURE(status)) {
		dev_err(&galaxybook->acpi->dev, "failed to execute method %s; got %s\n",
			method, acpi_format_exception(status));
		return -EIO;
	}

	out_obj = output.pointer;

	if (out_obj->buffer.length != len || out_obj->buffer.length < GB_SAWB_GB_GUNM_POS + 1) {
		dev_err(&galaxybook->acpi->dev,
			"failed to execute %s; response length mismatch\n",
			method);
		err = -EPROTO;
		goto out_free;
	}
	if (out_obj->buffer.pointer[GB_SAWB_RFLG_POS] != GB_RFLG_SUCCESS) {
		dev_err(&galaxybook->acpi->dev,
			"failed to execute %s; device did not respond with success code 0x%x\n",
			method, GB_RFLG_SUCCESS);
		err = -ENXIO;
		goto out_free;
	}
	if (out_obj->buffer.pointer[GB_SAWB_GB_GUNM_POS] == GB_GUNM_FAIL) {
		dev_err(&galaxybook->acpi->dev,
			"failed to execute %s; device responded with failure code 0x%x\n",
			method, GB_GUNM_FAIL);
		err = -ENXIO;
		goto out_free;
	}

	memcpy(buf, out_obj->buffer.pointer, len);
	err = 0;

out_free:
	kfree(out_obj);
	return err;
}

static int galaxybook_enable_acpi_feature(struct samsung_galaxybook *galaxybook, const u16 sasb)
{
	struct sawb buf = {};
	int err;

	buf.safn = GB_SAFN;
	buf.sasb = sasb;
	buf.gunm = GB_GUNM_FEATURE_ENABLE;
	buf.guds[0] = GB_GUDS_FEATURE_ENABLE;

	err = galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				     &buf, GB_SAWB_LEN_SETTINGS);
	if (err)
		return err;

	if (buf.gunm != GB_GUNM_FEATURE_ENABLE_SUCCESS &&
	    buf.guds[0] != GB_GUDS_FEATURE_ENABLE_SUCCESS)
		return -ENODEV;

	return 0;
}

/*
 * Keyboard Backlight
 */

static int kbd_backlight_acpi_get(struct samsung_galaxybook *galaxybook,
				  enum led_brightness *brightness)
{
	struct sawb buf = {};
	int err;

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_KBD_BACKLIGHT;
	buf.gunm = GB_GUNM_GET;

	err = galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				     &buf, GB_SAWB_LEN_SETTINGS);
	if (err)
		return err;

	*brightness = buf.gunm;

	return 0;
}

static int kbd_backlight_acpi_set(struct samsung_galaxybook *galaxybook,
				  const enum led_brightness brightness)
{
	struct sawb buf = {};

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_KBD_BACKLIGHT;
	buf.gunm = GB_GUNM_SET;

	buf.guds[0] = brightness;

	return galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				      &buf, GB_SAWB_LEN_SETTINGS);
}

static enum led_brightness kbd_backlight_show(struct led_classdev *led)
{
	struct samsung_galaxybook *galaxybook =
		container_of(led, struct samsung_galaxybook, kbd_backlight);
	enum led_brightness brightness;
	int err;

	err = kbd_backlight_acpi_get(galaxybook, &brightness);
	if (err)
		return err;

	return brightness;
}

static int kbd_backlight_store(struct led_classdev *led,
			       const enum led_brightness brightness)
{
	struct samsung_galaxybook *galaxybook =
		container_of_const(led, struct samsung_galaxybook, kbd_backlight);

	return kbd_backlight_acpi_set(galaxybook, brightness);
}

static int galaxybook_kbd_backlight_init(struct samsung_galaxybook *galaxybook)
{
	struct led_init_data init_data = {};
	enum led_brightness brightness;
	int err;

	err = devm_mutex_init(&galaxybook->platform->dev, &galaxybook->kbd_backlight_lock);
	if (err)
		return err;

	err = galaxybook_enable_acpi_feature(galaxybook, GB_SASB_KBD_BACKLIGHT);
	if (err) {
		dev_dbg(&galaxybook->platform->dev,
			"failed to enable kbd_backlight feature, error %d\n", err);
		return GB_NOT_SUPPORTED;
	}

	err = kbd_backlight_acpi_get(galaxybook, &brightness);
	if (err) {
		dev_dbg(&galaxybook->platform->dev,
			"failed to get initial kbd_backlight brightness, error %d\n", err);
		return GB_NOT_SUPPORTED;
	}

	init_data.devicename = DRIVER_NAME;
	init_data.default_label = ":" LED_FUNCTION_KBD_BACKLIGHT;
	init_data.devname_mandatory = true;

	galaxybook->kbd_backlight.brightness_get = kbd_backlight_show;
	galaxybook->kbd_backlight.brightness_set_blocking = kbd_backlight_store;
	galaxybook->kbd_backlight.flags = LED_BRIGHT_HW_CHANGED;
	galaxybook->kbd_backlight.max_brightness = GB_KBD_BACKLIGHT_MAX_BRIGHTNESS;

	return devm_led_classdev_register_ext(&galaxybook->platform->dev,
					      &galaxybook->kbd_backlight, &init_data);
}

/*
 * Battery Extension (adds charge_control_end_threshold to the battery device)
 */

static int charge_control_end_threshold_acpi_get(struct samsung_galaxybook *galaxybook, u8 *value)
{
	struct sawb buf = {};
	int err;

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_POWER_MANAGEMENT;
	buf.gunm = GB_GUNM_POWER_MANAGEMENT;
	buf.guds[0] = GB_GUDS_BATTERY_CHARGE_CONTROL;
	buf.guds[1] = GB_GUDS_BATTERY_CHARGE_CONTROL_GET;

	err = galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				     &buf, GB_SAWB_LEN_SETTINGS);
	if (err)
		return err;

	*value = buf.guds[1];

	return 0;
}

static int charge_control_end_threshold_acpi_set(struct samsung_galaxybook *galaxybook, u8 value)
{
	struct sawb buf = {};

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_POWER_MANAGEMENT;
	buf.gunm = GB_GUNM_POWER_MANAGEMENT;
	buf.guds[0] = GB_GUDS_BATTERY_CHARGE_CONTROL;
	buf.guds[1] = GB_GUDS_BATTERY_CHARGE_CONTROL_SET;
	buf.guds[2] = value;

	return galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				      &buf, GB_SAWB_LEN_SETTINGS);
}

static int galaxybook_battery_ext_property_get(struct power_supply *psy,
					       const struct power_supply_ext *ext,
					       void *ext_data,
					       enum power_supply_property psp,
					       union power_supply_propval *val)
{
	struct samsung_galaxybook *galaxybook = ext_data;
	int err;

	if (psp != POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD)
		return -EINVAL;

	err = charge_control_end_threshold_acpi_get(galaxybook, (u8 *)&val->intval);
	if (err)
		return err;

	/*
	 * device stores "no end threshold" as 0 instead of 100;
	 * if device has 0, report 100
	 */
	if (val->intval == 0)
		val->intval = 100;

	return 0;
}

static int galaxybook_battery_ext_property_set(struct power_supply *psy,
					       const struct power_supply_ext *ext,
					       void *ext_data,
					       enum power_supply_property psp,
					       const union power_supply_propval *val)
{
	struct samsung_galaxybook *galaxybook = ext_data;
	u8 value;

	if (psp != POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD)
		return -EINVAL;

	value = val->intval;

	if (value < 1 || value > 100)
		return -EINVAL;

	/*
	 * device stores "no end threshold" as 0 instead of 100;
	 * if setting to 100, send 0
	 */
	if (value == 100)
		value = 0;

	return charge_control_end_threshold_acpi_set(galaxybook, value);
}

static int galaxybook_battery_ext_property_is_writeable(struct power_supply *psy,
							const struct power_supply_ext *ext,
							void *ext_data,
							enum power_supply_property psp)
{
	if (psp == POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD)
		return true;

	return false;
}

static const enum power_supply_property galaxybook_battery_properties[] = {
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
};

static const struct power_supply_ext galaxybook_battery_ext = {
	.name			= DRIVER_NAME,
	.properties		= galaxybook_battery_properties,
	.num_properties		= ARRAY_SIZE(galaxybook_battery_properties),
	.get_property		= galaxybook_battery_ext_property_get,
	.set_property		= galaxybook_battery_ext_property_set,
	.property_is_writeable	= galaxybook_battery_ext_property_is_writeable,
};

static int galaxybook_battery_add(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct samsung_galaxybook *galaxybook =
		container_of(hook, struct samsung_galaxybook, battery_hook);

	return power_supply_register_extension(battery, &galaxybook_battery_ext,
					       &battery->dev, galaxybook);
}

static int galaxybook_battery_remove(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	power_supply_unregister_extension(battery, &galaxybook_battery_ext);
	return 0;
}

static int galaxybook_battery_threshold_init(struct samsung_galaxybook *galaxybook)
{
	u8 value;
	int err;

	err = charge_control_end_threshold_acpi_get(galaxybook, &value);
	if (err) {
		dev_dbg(&galaxybook->platform->dev,
			"failed to get initial battery charge end threshold, error %d\n", err);
		return 0;
	}

	galaxybook->battery_hook.add_battery = galaxybook_battery_add;
	galaxybook->battery_hook.remove_battery = galaxybook_battery_remove;
	galaxybook->battery_hook.name = "Samsung Galaxy Book Battery Extension";

	return devm_battery_hook_register(&galaxybook->platform->dev, &galaxybook->battery_hook);
}

/*
 * Platform Profile / Performance mode
 */

static int performance_mode_acpi_get(struct samsung_galaxybook *galaxybook, u8 *performance_mode)
{
	struct sawb buf = {};
	int err;

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_PERFORMANCE_MODE;
	export_guid(buf.caid, &GB_PERFORMANCE_MODE_GUID);
	buf.fncn = GB_FNCN_PERFORMANCE_MODE;
	buf.subn = GB_SUBN_PERFORMANCE_MODE_GET;

	err = galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_PERFORMANCE_MODE,
				     &buf, GB_SAWB_LEN_PERFORMANCE_MODE);
	if (err)
		return err;

	*performance_mode = buf.iob0;

	return 0;
}

static int performance_mode_acpi_set(struct samsung_galaxybook *galaxybook,
				     const u8 performance_mode)
{
	struct sawb buf = {};

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_PERFORMANCE_MODE;
	export_guid(buf.caid, &GB_PERFORMANCE_MODE_GUID);
	buf.fncn = GB_FNCN_PERFORMANCE_MODE;
	buf.subn = GB_SUBN_PERFORMANCE_MODE_SET;
	buf.iob0 = performance_mode;

	return galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_PERFORMANCE_MODE,
				      &buf, GB_SAWB_LEN_PERFORMANCE_MODE);
}

static int get_performance_mode_profile(struct samsung_galaxybook *galaxybook,
					const u8 performance_mode,
					enum platform_profile_option *profile)
{
	switch (performance_mode) {
	case GB_PERFORMANCE_MODE_FANOFF:
		*profile = PLATFORM_PROFILE_LOW_POWER;
		break;
	case GB_PERFORMANCE_MODE_LOWNOISE:
		*profile = PLATFORM_PROFILE_QUIET;
		break;
	case GB_PERFORMANCE_MODE_OPTIMIZED:
	case GB_PERFORMANCE_MODE_OPTIMIZED_V2:
		*profile = PLATFORM_PROFILE_BALANCED;
		break;
	case GB_PERFORMANCE_MODE_PERFORMANCE:
	case GB_PERFORMANCE_MODE_PERFORMANCE_V2:
	case GB_PERFORMANCE_MODE_ULTRA:
		*profile = PLATFORM_PROFILE_PERFORMANCE;
		break;
	case GB_PERFORMANCE_MODE_IGNORE1:
	case GB_PERFORMANCE_MODE_IGNORE2:
		return -EOPNOTSUPP;
	default:
		dev_warn(&galaxybook->platform->dev,
			 "unrecognized performance mode 0x%x\n", performance_mode);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int galaxybook_platform_profile_get(struct device *dev,
					   enum platform_profile_option *profile)
{
	struct samsung_galaxybook *galaxybook = dev_get_drvdata(dev);
	u8 performance_mode;
	int err;

	err = performance_mode_acpi_get(galaxybook, &performance_mode);
	if (err)
		return err;

	return get_performance_mode_profile(galaxybook, performance_mode, profile);
}

static int galaxybook_platform_profile_set(struct device *dev,
					   enum platform_profile_option profile)
{
	struct samsung_galaxybook *galaxybook = dev_get_drvdata(dev);

	return performance_mode_acpi_set(galaxybook,
					 galaxybook->profile_performance_modes[profile]);
}

static int galaxybook_platform_profile_probe(void *drvdata, unsigned long *choices)
{
	struct samsung_galaxybook *galaxybook = drvdata;
	u8 *perfmodes = galaxybook->profile_performance_modes;
	enum platform_profile_option profile;
	struct sawb buf = {};
	unsigned int i;
	int err;

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_PERFORMANCE_MODE;
	export_guid(buf.caid, &GB_PERFORMANCE_MODE_GUID);
	buf.fncn = GB_FNCN_PERFORMANCE_MODE;
	buf.subn = GB_SUBN_PERFORMANCE_MODE_LIST;

	err = galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_PERFORMANCE_MODE,
				     &buf, GB_SAWB_LEN_PERFORMANCE_MODE);
	if (err) {
		dev_dbg(&galaxybook->platform->dev,
			"failed to get supported performance modes, error %d\n", err);
		return err;
	}

	/* set initial default profile performance mode values */
	perfmodes[PLATFORM_PROFILE_LOW_POWER] = GB_PERFORMANCE_MODE_FANOFF;
	perfmodes[PLATFORM_PROFILE_QUIET] = GB_PERFORMANCE_MODE_LOWNOISE;
	perfmodes[PLATFORM_PROFILE_BALANCED] = GB_PERFORMANCE_MODE_OPTIMIZED;
	perfmodes[PLATFORM_PROFILE_PERFORMANCE] = GB_PERFORMANCE_MODE_PERFORMANCE;

	/*
	 * Value returned in iob0 will have the number of supported performance
	 * modes per device. The performance mode values will then be given as a
	 * list after this (iob1-iobX). Loop through the supported values and
	 * enable their mapped platform_profile choice, overriding "legacy"
	 * values along the way if a non-legacy value exists.
	 */
	for (i = 1; i <= buf.iob0; i++) {
		err = get_performance_mode_profile(galaxybook, buf.iobs[i], &profile);
		if (err) {
			dev_dbg(&galaxybook->platform->dev,
				"ignoring unmapped performance mode 0x%x\n", buf.iobs[i]);
			continue;
		}
		switch (buf.iobs[i]) {
		case GB_PERFORMANCE_MODE_OPTIMIZED_V2:
			perfmodes[profile] = GB_PERFORMANCE_MODE_OPTIMIZED_V2;
			break;
		case GB_PERFORMANCE_MODE_PERFORMANCE_V2:
			/* only update if not already overwritten by Ultra */
			if (perfmodes[profile] != GB_PERFORMANCE_MODE_ULTRA)
				perfmodes[profile] = GB_PERFORMANCE_MODE_PERFORMANCE_V2;
			break;
		case GB_PERFORMANCE_MODE_ULTRA:
			perfmodes[profile] = GB_PERFORMANCE_MODE_ULTRA;
			break;
		default:
			break;
		}
		set_bit(profile, choices);
		dev_dbg(&galaxybook->platform->dev,
			"setting platform profile %d to use performance mode 0x%x\n",
			profile, perfmodes[profile]);
	}

	/* initialize performance_mode using balanced's mapped value */
	if (test_bit(PLATFORM_PROFILE_BALANCED, choices))
		return performance_mode_acpi_set(galaxybook, perfmodes[PLATFORM_PROFILE_BALANCED]);

	return 0;
}

static const struct platform_profile_ops galaxybook_platform_profile_ops = {
	.probe = galaxybook_platform_profile_probe,
	.profile_get = galaxybook_platform_profile_get,
	.profile_set = galaxybook_platform_profile_set,
};

static int galaxybook_platform_profile_init(struct samsung_galaxybook *galaxybook)
{
	struct device *platform_profile_dev;
	u8 performance_mode;
	int err;

	err = performance_mode_acpi_get(galaxybook, &performance_mode);
	if (err) {
		dev_dbg(&galaxybook->platform->dev,
			"failed to get initial performance mode, error %d\n", err);
		return GB_NOT_SUPPORTED;
	}

	platform_profile_dev = devm_platform_profile_register(&galaxybook->platform->dev,
							      DRIVER_NAME, galaxybook,
							      &galaxybook_platform_profile_ops);

	return PTR_ERR_OR_ZERO(platform_profile_dev);
}

/*
 * Firmware Attributes
 */

/* Power on lid open (device should power on when lid is opened) */

static int power_on_lid_open_acpi_get(struct samsung_galaxybook *galaxybook, bool *value)
{
	struct sawb buf = {};
	int err;

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_POWER_MANAGEMENT;
	buf.gunm = GB_GUNM_POWER_MANAGEMENT;
	buf.guds[0] = GB_GUDS_POWER_ON_LID_OPEN;
	buf.guds[1] = GB_GUDS_POWER_ON_LID_OPEN_GET;

	err = galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				     &buf, GB_SAWB_LEN_SETTINGS);
	if (err)
		return err;

	*value = buf.guds[1];

	return 0;
}

static int power_on_lid_open_acpi_set(struct samsung_galaxybook *galaxybook, const bool value)
{
	struct sawb buf = {};

	lockdep_assert_held(&galaxybook->fw_attr_lock);

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_POWER_MANAGEMENT;
	buf.gunm = GB_GUNM_POWER_MANAGEMENT;
	buf.guds[0] = GB_GUDS_POWER_ON_LID_OPEN;
	buf.guds[1] = GB_GUDS_POWER_ON_LID_OPEN_SET;
	buf.guds[2] = value ? 1 : 0;

	return galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				      &buf, GB_SAWB_LEN_SETTINGS);
}

/* USB Charging (USB ports can provide power when device is powered off) */

static int usb_charging_acpi_get(struct samsung_galaxybook *galaxybook, bool *value)
{
	struct sawb buf = {};
	int err;

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_USB_CHARGING_GET;
	buf.gunm = GB_GUNM_USB_CHARGING_GET;

	err = galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				     &buf, GB_SAWB_LEN_SETTINGS);
	if (err)
		return err;

	*value = buf.gunm == 1;

	return 0;
}

static int usb_charging_acpi_set(struct samsung_galaxybook *galaxybook, const bool value)
{
	struct sawb buf = {};

	lockdep_assert_held(&galaxybook->fw_attr_lock);

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_USB_CHARGING_SET;
	buf.gunm = value ? GB_GUNM_USB_CHARGING_ON : GB_GUNM_USB_CHARGING_OFF;

	return galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				      &buf, GB_SAWB_LEN_SETTINGS);
}

/* Block recording (blocks access to camera and microphone) */

static int block_recording_acpi_get(struct samsung_galaxybook *galaxybook, bool *value)
{
	struct sawb buf = {};
	int err;

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_BLOCK_RECORDING;
	buf.gunm = GB_GUNM_GET;

	err = galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				     &buf, GB_SAWB_LEN_SETTINGS);
	if (err)
		return err;

	*value = buf.gunm == GB_BLOCK_RECORDING_ON;

	return 0;
}

static int block_recording_acpi_set(struct samsung_galaxybook *galaxybook, const bool value)
{
	struct sawb buf = {};
	int err;

	lockdep_assert_held(&galaxybook->fw_attr_lock);

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_BLOCK_RECORDING;
	buf.gunm = GB_GUNM_SET;
	buf.guds[0] = value ? GB_BLOCK_RECORDING_ON : GB_BLOCK_RECORDING_OFF;

	err = galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				     &buf, GB_SAWB_LEN_SETTINGS);
	if (err)
		return err;

	input_report_switch(galaxybook->camera_lens_cover_switch,
			    SW_CAMERA_LENS_COVER, value ? 1 : 0);
	input_sync(galaxybook->camera_lens_cover_switch);

	return 0;
}

static int galaxybook_block_recording_init(struct samsung_galaxybook *galaxybook)
{
	bool value;
	int err;

	err = galaxybook_enable_acpi_feature(galaxybook, GB_SASB_BLOCK_RECORDING);
	if (err) {
		dev_dbg(&galaxybook->platform->dev,
			"failed to initialize block_recording, error %d\n", err);
		return GB_NOT_SUPPORTED;
	}

	guard(mutex)(&galaxybook->fw_attr_lock);

	err = block_recording_acpi_get(galaxybook, &value);
	if (err) {
		dev_dbg(&galaxybook->platform->dev,
			"failed to get initial block_recording state, error %d\n", err);
		return GB_NOT_SUPPORTED;
	}

	galaxybook->camera_lens_cover_switch =
		devm_input_allocate_device(&galaxybook->platform->dev);
	if (!galaxybook->camera_lens_cover_switch)
		return -ENOMEM;

	galaxybook->camera_lens_cover_switch->name = "Samsung Galaxy Book Camera Lens Cover";
	galaxybook->camera_lens_cover_switch->phys = DRIVER_NAME "/input0";
	galaxybook->camera_lens_cover_switch->id.bustype = BUS_HOST;

	input_set_capability(galaxybook->camera_lens_cover_switch, EV_SW, SW_CAMERA_LENS_COVER);

	err = input_register_device(galaxybook->camera_lens_cover_switch);
	if (err)
		return err;

	input_report_switch(galaxybook->camera_lens_cover_switch,
			    SW_CAMERA_LENS_COVER, value ? 1 : 0);
	input_sync(galaxybook->camera_lens_cover_switch);

	return 0;
}

/* Firmware Attributes setup */

static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "enumeration\n");
}

static struct kobj_attribute fw_attr_type = __ATTR_RO(type);

static ssize_t default_value_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0\n");
}

static struct kobj_attribute fw_attr_default_value = __ATTR_RO(default_value);

static ssize_t possible_values_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0;1\n");
}

static struct kobj_attribute fw_attr_possible_values = __ATTR_RO(possible_values);

static ssize_t display_name_language_code_show(struct kobject *kobj, struct kobj_attribute *attr,
					       char *buf)
{
	return sysfs_emit(buf, "%s\n", GB_ATTR_LANGUAGE_CODE);
}

static struct kobj_attribute fw_attr_display_name_language_code =
	__ATTR_RO(display_name_language_code);

static ssize_t display_name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct galaxybook_fw_attr *fw_attr =
		container_of(attr, struct galaxybook_fw_attr, display_name);

	return sysfs_emit(buf, "%s\n", galaxybook_fw_attr_desc[fw_attr->fw_attr_id]);
}

static ssize_t current_value_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct galaxybook_fw_attr *fw_attr =
		container_of(attr, struct galaxybook_fw_attr, current_value);
	bool value;
	int err;

	err = fw_attr->get_value(fw_attr->galaxybook, &value);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n", value);
}

static ssize_t current_value_store(struct kobject *kobj, struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	struct galaxybook_fw_attr *fw_attr =
		container_of(attr, struct galaxybook_fw_attr, current_value);
	struct samsung_galaxybook *galaxybook = fw_attr->galaxybook;
	bool value;
	int err;

	if (!count)
		return -EINVAL;

	err = kstrtobool(buf, &value);
	if (err)
		return err;

	guard(mutex)(&galaxybook->fw_attr_lock);

	err = fw_attr->set_value(galaxybook, value);
	if (err)
		return err;

	return count;
}

#define NUM_FW_ATTR_ENUM_ATTRS  6

static int galaxybook_fw_attr_init(struct samsung_galaxybook *galaxybook,
				   const enum galaxybook_fw_attr_id fw_attr_id,
				   int (*get_value)(struct samsung_galaxybook *galaxybook,
						    bool *value),
				   int (*set_value)(struct samsung_galaxybook *galaxybook,
						    const bool value))
{
	struct galaxybook_fw_attr *fw_attr;
	struct attribute **attrs;

	fw_attr = devm_kzalloc(&galaxybook->platform->dev, sizeof(*fw_attr), GFP_KERNEL);
	if (!fw_attr)
		return -ENOMEM;

	attrs = devm_kcalloc(&galaxybook->platform->dev, NUM_FW_ATTR_ENUM_ATTRS + 1,
			     sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	attrs[0] = &fw_attr_type.attr;
	attrs[1] = &fw_attr_default_value.attr;
	attrs[2] = &fw_attr_possible_values.attr;
	attrs[3] = &fw_attr_display_name_language_code.attr;

	sysfs_attr_init(&fw_attr->display_name.attr);
	fw_attr->display_name.attr.name = "display_name";
	fw_attr->display_name.attr.mode = 0444;
	fw_attr->display_name.show = display_name_show;
	attrs[4] = &fw_attr->display_name.attr;

	sysfs_attr_init(&fw_attr->current_value.attr);
	fw_attr->current_value.attr.name = "current_value";
	fw_attr->current_value.attr.mode = 0644;
	fw_attr->current_value.show = current_value_show;
	fw_attr->current_value.store = current_value_store;
	attrs[5] = &fw_attr->current_value.attr;

	attrs[6] = NULL;

	fw_attr->galaxybook = galaxybook;
	fw_attr->fw_attr_id = fw_attr_id;
	fw_attr->attr_group.name = galaxybook_fw_attr_name[fw_attr_id];
	fw_attr->attr_group.attrs = attrs;
	fw_attr->get_value = get_value;
	fw_attr->set_value = set_value;

	return sysfs_create_group(&galaxybook->fw_attrs_kset->kobj, &fw_attr->attr_group);
}

static void galaxybook_kset_unregister(void *data)
{
	struct kset *kset = data;

	kset_unregister(kset);
}

static void galaxybook_fw_attrs_dev_unregister(void *data)
{
	struct device *fw_attrs_dev = data;

	device_unregister(fw_attrs_dev);
}

static int galaxybook_fw_attrs_init(struct samsung_galaxybook *galaxybook)
{
	bool value;
	int err;

	err = devm_mutex_init(&galaxybook->platform->dev, &galaxybook->fw_attr_lock);
	if (err)
		return err;

	galaxybook->fw_attrs_dev = device_create(&firmware_attributes_class, NULL, MKDEV(0, 0),
						 NULL, "%s", DRIVER_NAME);
	if (IS_ERR(galaxybook->fw_attrs_dev))
		return PTR_ERR(galaxybook->fw_attrs_dev);

	err = devm_add_action_or_reset(&galaxybook->platform->dev,
				       galaxybook_fw_attrs_dev_unregister,
				       galaxybook->fw_attrs_dev);
	if (err)
		return err;

	galaxybook->fw_attrs_kset = kset_create_and_add("attributes", NULL,
							&galaxybook->fw_attrs_dev->kobj);
	if (!galaxybook->fw_attrs_kset)
		return -ENOMEM;
	err = devm_add_action_or_reset(&galaxybook->platform->dev,
				       galaxybook_kset_unregister, galaxybook->fw_attrs_kset);
	if (err)
		return err;

	err = power_on_lid_open_acpi_get(galaxybook, &value);
	if (!err) {
		err = galaxybook_fw_attr_init(galaxybook,
					      GB_ATTR_POWER_ON_LID_OPEN,
					      &power_on_lid_open_acpi_get,
					      &power_on_lid_open_acpi_set);
		if (err)
			return err;
	}

	err = usb_charging_acpi_get(galaxybook, &value);
	if (!err) {
		err = galaxybook_fw_attr_init(galaxybook,
					      GB_ATTR_USB_CHARGING,
					      &usb_charging_acpi_get,
					      &usb_charging_acpi_set);
		if (err)
			return err;
	}

	err = galaxybook_block_recording_init(galaxybook);
	if (err == GB_NOT_SUPPORTED)
		return 0;
	else if (err)
		return err;

	galaxybook->has_block_recording = true;

	return galaxybook_fw_attr_init(galaxybook,
				       GB_ATTR_BLOCK_RECORDING,
				       &block_recording_acpi_get,
				       &block_recording_acpi_set);
}

/*
 * Hotkeys and notifications
 */

static void galaxybook_kbd_backlight_hotkey_work(struct work_struct *work)
{
	struct samsung_galaxybook *galaxybook =
		from_work(galaxybook, work, kbd_backlight_hotkey_work);
	int brightness;
	int err;

	guard(mutex)(&galaxybook->kbd_backlight_lock);

	brightness = galaxybook->kbd_backlight.brightness;
	if (brightness < galaxybook->kbd_backlight.max_brightness)
		brightness++;
	else
		brightness = 0;

	err = led_set_brightness_sync(&galaxybook->kbd_backlight, brightness);
	if (err) {
		dev_err(&galaxybook->platform->dev,
			"failed to set kbd_backlight brightness, error %d\n", err);
		return;
	}

	led_classdev_notify_brightness_hw_changed(&galaxybook->kbd_backlight, brightness);
}

static void galaxybook_block_recording_hotkey_work(struct work_struct *work)
{
	struct samsung_galaxybook *galaxybook =
		from_work(galaxybook, work, block_recording_hotkey_work);
	bool value;
	int err;

	guard(mutex)(&galaxybook->fw_attr_lock);

	err = block_recording_acpi_get(galaxybook, &value);
	if (err) {
		dev_err(&galaxybook->platform->dev,
			"failed to get block_recording, error %d\n", err);
		return;
	}

	err = block_recording_acpi_set(galaxybook, !value);
	if (err)
		dev_err(&galaxybook->platform->dev,
			"failed to set block_recording, error %d\n", err);
}

static bool galaxybook_i8042_filter(unsigned char data, unsigned char str, struct serio *port,
				    void *context)
{
	struct samsung_galaxybook *galaxybook = context;
	static bool extended;

	if (str & I8042_STR_AUXDATA)
		return false;

	if (data == 0xe0) {
		extended = true;
		return true;
	} else if (extended) {
		extended = false;
		switch (data) {
		case GB_KEY_KBD_BACKLIGHT_KEYDOWN:
			return true;
		case GB_KEY_KBD_BACKLIGHT_KEYUP:
			if (galaxybook->has_kbd_backlight)
				schedule_work(&galaxybook->kbd_backlight_hotkey_work);
			return true;

		case GB_KEY_BLOCK_RECORDING_KEYDOWN:
			return true;
		case GB_KEY_BLOCK_RECORDING_KEYUP:
			if (galaxybook->has_block_recording)
				schedule_work(&galaxybook->block_recording_hotkey_work);
			return true;

		/* battery notification already sent to battery + SCAI device */
		case GB_KEY_BATTERY_NOTIFY_KEYUP:
		case GB_KEY_BATTERY_NOTIFY_KEYDOWN:
			return true;

		default:
			/*
			 * Report the previously filtered e0 before continuing
			 * with the next non-filtered byte.
			 */
			serio_interrupt(port, 0xe0, 0);
			return false;
		}
	}

	return false;
}

static void galaxybook_i8042_filter_remove(void *data)
{
	struct samsung_galaxybook *galaxybook = data;

	i8042_remove_filter(galaxybook_i8042_filter);
	cancel_work_sync(&galaxybook->kbd_backlight_hotkey_work);
	cancel_work_sync(&galaxybook->block_recording_hotkey_work);
}

static int galaxybook_i8042_filter_install(struct samsung_galaxybook *galaxybook)
{
	int err;

	if (!galaxybook->has_kbd_backlight && !galaxybook->has_block_recording)
		return 0;

	INIT_WORK(&galaxybook->kbd_backlight_hotkey_work,
		  galaxybook_kbd_backlight_hotkey_work);
	INIT_WORK(&galaxybook->block_recording_hotkey_work,
		  galaxybook_block_recording_hotkey_work);

	err = i8042_install_filter(galaxybook_i8042_filter, galaxybook);
	if (err)
		return err;

	return devm_add_action_or_reset(&galaxybook->platform->dev,
					galaxybook_i8042_filter_remove, galaxybook);
}

/*
 * ACPI device setup
 */

static void galaxybook_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	struct samsung_galaxybook *galaxybook = data;

	switch (event) {
	case GB_ACPI_NOTIFY_BATTERY_STATE_CHANGED:
	case GB_ACPI_NOTIFY_DEVICE_ON_TABLE:
	case GB_ACPI_NOTIFY_DEVICE_OFF_TABLE:
		break;
	case GB_ACPI_NOTIFY_HOTKEY_PERFORMANCE_MODE:
		if (galaxybook->has_performance_mode)
			platform_profile_cycle();
		break;
	default:
		dev_warn(&galaxybook->platform->dev,
			 "unknown ACPI notification event: 0x%x\n", event);
	}

	acpi_bus_generate_netlink_event(DRIVER_NAME, dev_name(&galaxybook->platform->dev),
					event, 1);
}

static int galaxybook_enable_acpi_notify(struct samsung_galaxybook *galaxybook)
{
	struct sawb buf = {};
	int err;

	err = galaxybook_enable_acpi_feature(galaxybook, GB_SASB_NOTIFICATIONS);
	if (err)
		return err;

	buf.safn = GB_SAFN;
	buf.sasb = GB_SASB_NOTIFICATIONS;
	buf.gunm = GB_GUNM_ACPI_NOTIFY_ENABLE;
	buf.guds[0] = GB_GUDS_ACPI_NOTIFY_ENABLE;

	return galaxybook_acpi_method(galaxybook, GB_ACPI_METHOD_SETTINGS,
				      &buf, GB_SAWB_LEN_SETTINGS);
}

static void galaxybook_acpi_remove_notify_handler(void *data)
{
	struct samsung_galaxybook *galaxybook = data;

	acpi_remove_notify_handler(galaxybook->acpi->handle, ACPI_ALL_NOTIFY,
				   galaxybook_acpi_notify);
}

static void galaxybook_acpi_disable(void *data)
{
	struct samsung_galaxybook *galaxybook = data;

	acpi_execute_simple_method(galaxybook->acpi->handle,
				   GB_ACPI_METHOD_ENABLE, GB_ACPI_METHOD_ENABLE_OFF);
}

static int galaxybook_acpi_init(struct samsung_galaxybook *galaxybook)
{
	acpi_status status;
	int err;

	status = acpi_execute_simple_method(galaxybook->acpi->handle, GB_ACPI_METHOD_ENABLE,
					    GB_ACPI_METHOD_ENABLE_ON);
	if (ACPI_FAILURE(status))
		return -EIO;
	err = devm_add_action_or_reset(&galaxybook->platform->dev,
				       galaxybook_acpi_disable, galaxybook);
	if (err)
		return err;

	status = acpi_install_notify_handler(galaxybook->acpi->handle, ACPI_ALL_NOTIFY,
					     galaxybook_acpi_notify, galaxybook);
	if (ACPI_FAILURE(status))
		return -EIO;
	err = devm_add_action_or_reset(&galaxybook->platform->dev,
				       galaxybook_acpi_remove_notify_handler, galaxybook);
	if (err)
		return err;

	err = galaxybook_enable_acpi_notify(galaxybook);
	if (err)
		dev_dbg(&galaxybook->platform->dev, "failed to enable ACPI notifications; "
			"some hotkeys will not be supported\n");

	err = galaxybook_enable_acpi_feature(galaxybook, GB_SASB_POWER_MANAGEMENT);
	if (err)
		dev_dbg(&galaxybook->platform->dev,
			"failed to initialize ACPI power management features; "
			"many features of this driver will not be available\n");

	return 0;
}

/*
 * Platform driver
 */

static int galaxybook_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	struct samsung_galaxybook *galaxybook;
	int err;

	if (!adev)
		return -ENODEV;

	galaxybook = devm_kzalloc(&pdev->dev, sizeof(*galaxybook), GFP_KERNEL);
	if (!galaxybook)
		return -ENOMEM;

	galaxybook->platform = pdev;
	galaxybook->acpi = adev;

	/*
	 * Features must be enabled and initialized in the following order to
	 * avoid failures seen on certain devices:
	 * - GB_SASB_POWER_MANAGEMENT (including performance mode)
	 * - GB_SASB_KBD_BACKLIGHT
	 * - GB_SASB_BLOCK_RECORDING (as part of fw_attrs init)
	 */

	err = galaxybook_acpi_init(galaxybook);
	if (err)
		return dev_err_probe(&galaxybook->platform->dev, err,
				     "failed to initialize ACPI device\n");

	err = galaxybook_platform_profile_init(galaxybook);
	if (!err)
		galaxybook->has_performance_mode = true;
	else if (err != GB_NOT_SUPPORTED)
		return dev_err_probe(&galaxybook->platform->dev, err,
				     "failed to initialize platform profile\n");

	err = galaxybook_battery_threshold_init(galaxybook);
	if (err)
		return dev_err_probe(&galaxybook->platform->dev, err,
				     "failed to initialize battery threshold\n");

	err = galaxybook_kbd_backlight_init(galaxybook);
	if (!err)
		galaxybook->has_kbd_backlight = true;
	else if (err != GB_NOT_SUPPORTED)
		return dev_err_probe(&galaxybook->platform->dev, err,
				     "failed to initialize kbd_backlight\n");

	err = galaxybook_fw_attrs_init(galaxybook);
	if (err)
		return dev_err_probe(&galaxybook->platform->dev, err,
				     "failed to initialize firmware-attributes\n");

	err = galaxybook_i8042_filter_install(galaxybook);
	if (err)
		return dev_err_probe(&galaxybook->platform->dev, err,
				     "failed to initialize i8042_filter\n");

	return 0;
}

static const struct acpi_device_id galaxybook_device_ids[] = {
	{ "SAM0426" },
	{ "SAM0427" },
	{ "SAM0428" },
	{ "SAM0429" },
	{ "SAM0430" },
	{}
};
MODULE_DEVICE_TABLE(acpi, galaxybook_device_ids);

static struct platform_driver galaxybook_platform_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.acpi_match_table = galaxybook_device_ids,
	},
	.probe = galaxybook_probe,
};
module_platform_driver(galaxybook_platform_driver);

MODULE_AUTHOR("Joshua Grisham <josh@joshuagrisham.com>");
MODULE_DESCRIPTION("Samsung Galaxy Book driver");
MODULE_LICENSE("GPL");
