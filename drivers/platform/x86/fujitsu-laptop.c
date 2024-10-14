// SPDX-License-Identifier: GPL-2.0-or-later
/*-*-linux-c-*-*/

/*
  Copyright (C) 2007,2008 Jonathan Woithe <jwoithe@just42.net>
  Copyright (C) 2008 Peter Gruber <nokos@gmx.net>
  Copyright (C) 2008 Tony Vroon <tony@linx.net>
  Based on earlier work:
    Copyright (C) 2003 Shane Spencer <shane@bogomip.com>
    Adrian Yee <brewt-fujitsu@brewt.org>

  Templated from msi-laptop.c and thinkpad_acpi.c which is copyright
  by its respective authors.

 */

/*
 * fujitsu-laptop.c - Fujitsu laptop support, providing access to additional
 * features made available on a range of Fujitsu laptops including the
 * P2xxx/P5xxx/S6xxx/S7xxx series.
 *
 * This driver implements a vendor-specific backlight control interface for
 * Fujitsu laptops and provides support for hotkeys present on certain Fujitsu
 * laptops.
 *
 * This driver has been tested on a Fujitsu Lifebook S6410, S7020 and
 * P8010.  It should work on most P-series and S-series Lifebooks, but
 * YMMV.
 *
 * The module parameter use_alt_lcd_levels switches between different ACPI
 * brightness controls which are used by different Fujitsu laptops.  In most
 * cases the correct method is automatically detected. "use_alt_lcd_levels=1"
 * is applicable for a Fujitsu Lifebook S6410 if autodetection fails.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/dmi.h>
#include <linux/backlight.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/kfifo.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <acpi/battery.h>
#include <acpi/video.h>

#define FUJITSU_DRIVER_VERSION		"0.6.0"

#define FUJITSU_LCD_N_LEVELS		8

#define ACPI_FUJITSU_CLASS		"fujitsu"
#define ACPI_FUJITSU_BL_HID		"FUJ02B1"
#define ACPI_FUJITSU_BL_DRIVER_NAME	"Fujitsu laptop FUJ02B1 ACPI brightness driver"
#define ACPI_FUJITSU_BL_DEVICE_NAME	"Fujitsu FUJ02B1"
#define ACPI_FUJITSU_LAPTOP_HID		"FUJ02E3"
#define ACPI_FUJITSU_LAPTOP_DRIVER_NAME	"Fujitsu laptop FUJ02E3 ACPI hotkeys driver"
#define ACPI_FUJITSU_LAPTOP_DEVICE_NAME	"Fujitsu FUJ02E3"

#define ACPI_FUJITSU_NOTIFY_CODE	0x80

/* FUNC interface - command values */
#define FUNC_FLAGS			BIT(12)
#define FUNC_LEDS			(BIT(12) | BIT(0))
#define FUNC_BUTTONS			(BIT(12) | BIT(1))
#define FUNC_BACKLIGHT			(BIT(12) | BIT(2))

/* FUNC interface - responses */
#define UNSUPPORTED_CMD			0x80000000

/* FUNC interface - status flags */
#define FLAG_RFKILL			BIT(5)
#define FLAG_LID			BIT(8)
#define FLAG_DOCK			BIT(9)
#define FLAG_TOUCHPAD_TOGGLE		BIT(26)
#define FLAG_MICMUTE			BIT(29)
#define FLAG_SOFTKEYS			(FLAG_RFKILL | FLAG_TOUCHPAD_TOGGLE | FLAG_MICMUTE)

/* FUNC interface - LED control */
#define FUNC_LED_OFF			BIT(0)
#define FUNC_LED_ON			(BIT(0) | BIT(16) | BIT(17))
#define LOGOLAMP_POWERON		BIT(13)
#define LOGOLAMP_ALWAYS			BIT(14)
#define KEYBOARD_LAMPS			BIT(8)
#define RADIO_LED_ON			BIT(5)
#define ECO_LED				BIT(16)
#define ECO_LED_ON			BIT(19)

/* FUNC interface - backlight power control */
#define BACKLIGHT_PARAM_POWER		BIT(2)
#define BACKLIGHT_OFF			(BIT(0) | BIT(1))
#define BACKLIGHT_ON			0

/* FUNC interface - battery control interface */
#define FUNC_S006_METHOD		0x1006
#define CHARGE_CONTROL_RW		0x21

/* Scancodes read from the GIRB register */
#define KEY1_CODE			0x410
#define KEY2_CODE			0x411
#define KEY3_CODE			0x412
#define KEY4_CODE			0x413
#define KEY5_CODE			0x420

/* Hotkey ringbuffer limits */
#define MAX_HOTKEY_RINGBUFFER_SIZE	100
#define RINGBUFFERSIZE			40

/* Module parameters */
static int use_alt_lcd_levels = -1;
static bool disable_brightness_adjust;

/* Device controlling the backlight and associated keys */
struct fujitsu_bl {
	struct input_dev *input;
	char phys[32];
	struct backlight_device *bl_device;
	unsigned int max_brightness;
	unsigned int brightness_level;
};

static struct fujitsu_bl *fujitsu_bl;

/* Device used to access hotkeys and other features on the laptop */
struct fujitsu_laptop {
	struct input_dev *input;
	char phys[32];
	struct platform_device *pf_device;
	struct kfifo fifo;
	spinlock_t fifo_lock;
	int flags_supported;
	int flags_state;
	bool charge_control_supported;
};

static struct acpi_device *fext;

/* Fujitsu ACPI interface function */

static int call_fext_func(struct acpi_device *device,
			  int func, int op, int feature, int state)
{
	union acpi_object params[4] = {
		{ .integer.type = ACPI_TYPE_INTEGER, .integer.value = func },
		{ .integer.type = ACPI_TYPE_INTEGER, .integer.value = op },
		{ .integer.type = ACPI_TYPE_INTEGER, .integer.value = feature },
		{ .integer.type = ACPI_TYPE_INTEGER, .integer.value = state }
	};
	struct acpi_object_list arg_list = { 4, params };
	unsigned long long value;
	acpi_status status;

	status = acpi_evaluate_integer(device->handle, "FUNC", &arg_list,
				       &value);
	if (ACPI_FAILURE(status)) {
		acpi_handle_err(device->handle, "Failed to evaluate FUNC\n");
		return -ENODEV;
	}

	acpi_handle_debug(device->handle,
			  "FUNC 0x%x (args 0x%x, 0x%x, 0x%x) returned 0x%x\n",
			  func, op, feature, state, (int)value);
	return value;
}

/* Battery charge control code */
static ssize_t charge_control_end_threshold_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int cc_end_value, s006_cc_return;
	int value, ret;

	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return ret;

	if (value < 50 || value > 100)
		return -EINVAL;

	cc_end_value = value * 0x100 + 0x20;
	s006_cc_return = call_fext_func(fext, FUNC_S006_METHOD,
					CHARGE_CONTROL_RW, cc_end_value, 0x0);
	if (s006_cc_return < 0)
		return s006_cc_return;
	/*
	 * The S006 0x21 method returns 0x00 in case the provided value
	 * is invalid.
	 */
	if (s006_cc_return == 0x00)
		return -EINVAL;

	return count;
}

static ssize_t charge_control_end_threshold_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int status;

	status = call_fext_func(fext, FUNC_S006_METHOD,
				CHARGE_CONTROL_RW, 0x21, 0x0);
	if (status < 0)
		return status;

	return sysfs_emit(buf, "%d\n", status);
}

static DEVICE_ATTR_RW(charge_control_end_threshold);

/* ACPI battery hook */
static int fujitsu_battery_add_hook(struct power_supply *battery,
			       struct acpi_battery_hook *hook)
{
	return device_create_file(&battery->dev,
				  &dev_attr_charge_control_end_threshold);
}

static int fujitsu_battery_remove_hook(struct power_supply *battery,
				  struct acpi_battery_hook *hook)
{
	device_remove_file(&battery->dev,
			   &dev_attr_charge_control_end_threshold);

	return 0;
}

static struct acpi_battery_hook battery_hook = {
	.add_battery = fujitsu_battery_add_hook,
	.remove_battery = fujitsu_battery_remove_hook,
	.name = "Fujitsu Battery Extension",
};

/*
 * These functions are intended to be called from acpi_fujitsu_laptop_add and
 * acpi_fujitsu_laptop_remove.
 */
static int fujitsu_battery_charge_control_add(struct acpi_device *device)
{
	struct fujitsu_laptop *priv = acpi_driver_data(device);
	int s006_cc_return;

	priv->charge_control_supported = false;
	/*
	 * Check if the S006 0x21 method exists by trying to get the current
	 * battery charge limit.
	 */
	s006_cc_return = call_fext_func(fext, FUNC_S006_METHOD,
					CHARGE_CONTROL_RW, 0x21, 0x0);
	if (s006_cc_return < 0)
		return s006_cc_return;
	if (s006_cc_return == UNSUPPORTED_CMD)
		return -ENODEV;

	priv->charge_control_supported = true;
	battery_hook_register(&battery_hook);

	return 0;
}

static void fujitsu_battery_charge_control_remove(struct acpi_device *device)
{
	struct fujitsu_laptop *priv = acpi_driver_data(device);

	if (priv->charge_control_supported)
		battery_hook_unregister(&battery_hook);
}

/* Hardware access for LCD brightness control */

static int set_lcd_level(struct acpi_device *device, int level)
{
	struct fujitsu_bl *priv = acpi_driver_data(device);
	acpi_status status;
	char *method;

	switch (use_alt_lcd_levels) {
	case -1:
		if (acpi_has_method(device->handle, "SBL2"))
			method = "SBL2";
		else
			method = "SBLL";
		break;
	case 1:
		method = "SBL2";
		break;
	default:
		method = "SBLL";
		break;
	}

	acpi_handle_debug(device->handle, "set lcd level via %s [%d]\n", method,
			  level);

	if (level < 0 || level >= priv->max_brightness)
		return -EINVAL;

	status = acpi_execute_simple_method(device->handle, method, level);
	if (ACPI_FAILURE(status)) {
		acpi_handle_err(device->handle, "Failed to evaluate %s\n",
				method);
		return -ENODEV;
	}

	priv->brightness_level = level;

	return 0;
}

static int get_lcd_level(struct acpi_device *device)
{
	struct fujitsu_bl *priv = acpi_driver_data(device);
	unsigned long long state = 0;
	acpi_status status = AE_OK;

	acpi_handle_debug(device->handle, "get lcd level via GBLL\n");

	status = acpi_evaluate_integer(device->handle, "GBLL", NULL, &state);
	if (ACPI_FAILURE(status))
		return 0;

	priv->brightness_level = state & 0x0fffffff;

	return priv->brightness_level;
}

static int get_max_brightness(struct acpi_device *device)
{
	struct fujitsu_bl *priv = acpi_driver_data(device);
	unsigned long long state = 0;
	acpi_status status = AE_OK;

	acpi_handle_debug(device->handle, "get max lcd level via RBLL\n");

	status = acpi_evaluate_integer(device->handle, "RBLL", NULL, &state);
	if (ACPI_FAILURE(status))
		return -1;

	priv->max_brightness = state;

	return priv->max_brightness;
}

/* Backlight device stuff */

static int bl_get_brightness(struct backlight_device *b)
{
	struct acpi_device *device = bl_get_data(b);

	return b->props.power == BACKLIGHT_POWER_OFF ? 0 : get_lcd_level(device);
}

static int bl_update_status(struct backlight_device *b)
{
	struct acpi_device *device = bl_get_data(b);

	if (fext) {
		if (b->props.power == BACKLIGHT_POWER_OFF)
			call_fext_func(fext, FUNC_BACKLIGHT, 0x1,
				       BACKLIGHT_PARAM_POWER, BACKLIGHT_OFF);
		else
			call_fext_func(fext, FUNC_BACKLIGHT, 0x1,
				       BACKLIGHT_PARAM_POWER, BACKLIGHT_ON);
	}

	return set_lcd_level(device, b->props.brightness);
}

static const struct backlight_ops fujitsu_bl_ops = {
	.get_brightness = bl_get_brightness,
	.update_status = bl_update_status,
};

static ssize_t lid_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct fujitsu_laptop *priv = dev_get_drvdata(dev);

	if (!(priv->flags_supported & FLAG_LID))
		return sysfs_emit(buf, "unknown\n");
	if (priv->flags_state & FLAG_LID)
		return sysfs_emit(buf, "open\n");
	else
		return sysfs_emit(buf, "closed\n");
}

static ssize_t dock_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct fujitsu_laptop *priv = dev_get_drvdata(dev);

	if (!(priv->flags_supported & FLAG_DOCK))
		return sysfs_emit(buf, "unknown\n");
	if (priv->flags_state & FLAG_DOCK)
		return sysfs_emit(buf, "docked\n");
	else
		return sysfs_emit(buf, "undocked\n");
}

static ssize_t radios_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct fujitsu_laptop *priv = dev_get_drvdata(dev);

	if (!(priv->flags_supported & FLAG_RFKILL))
		return sysfs_emit(buf, "unknown\n");
	if (priv->flags_state & FLAG_RFKILL)
		return sysfs_emit(buf, "on\n");
	else
		return sysfs_emit(buf, "killed\n");
}

static DEVICE_ATTR_RO(lid);
static DEVICE_ATTR_RO(dock);
static DEVICE_ATTR_RO(radios);

static struct attribute *fujitsu_pf_attributes[] = {
	&dev_attr_lid.attr,
	&dev_attr_dock.attr,
	&dev_attr_radios.attr,
	NULL
};

static const struct attribute_group fujitsu_pf_attribute_group = {
	.attrs = fujitsu_pf_attributes
};

static struct platform_driver fujitsu_pf_driver = {
	.driver = {
		   .name = "fujitsu-laptop",
		   }
};

/* ACPI device for LCD brightness control */

static const struct key_entry keymap_backlight[] = {
	{ KE_KEY, true, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, false, { KEY_BRIGHTNESSDOWN } },
	{ KE_END, 0 }
};

static int acpi_fujitsu_bl_input_setup(struct acpi_device *device)
{
	struct fujitsu_bl *priv = acpi_driver_data(device);
	int ret;

	priv->input = devm_input_allocate_device(&device->dev);
	if (!priv->input)
		return -ENOMEM;

	snprintf(priv->phys, sizeof(priv->phys), "%s/video/input0",
		 acpi_device_hid(device));

	priv->input->name = acpi_device_name(device);
	priv->input->phys = priv->phys;
	priv->input->id.bustype = BUS_HOST;
	priv->input->id.product = 0x06;

	ret = sparse_keymap_setup(priv->input, keymap_backlight, NULL);
	if (ret)
		return ret;

	return input_register_device(priv->input);
}

static int fujitsu_backlight_register(struct acpi_device *device)
{
	struct fujitsu_bl *priv = acpi_driver_data(device);
	const struct backlight_properties props = {
		.brightness = priv->brightness_level,
		.max_brightness = priv->max_brightness - 1,
		.type = BACKLIGHT_PLATFORM
	};
	struct backlight_device *bd;

	bd = devm_backlight_device_register(&device->dev, "fujitsu-laptop",
					    &device->dev, device,
					    &fujitsu_bl_ops, &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	priv->bl_device = bd;

	return 0;
}

static int acpi_fujitsu_bl_add(struct acpi_device *device)
{
	struct fujitsu_bl *priv;
	int ret;

	if (acpi_video_get_backlight_type() != acpi_backlight_vendor)
		return -ENODEV;

	priv = devm_kzalloc(&device->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	fujitsu_bl = priv;
	strcpy(acpi_device_name(device), ACPI_FUJITSU_BL_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_FUJITSU_CLASS);
	device->driver_data = priv;

	pr_info("ACPI: %s [%s]\n",
		acpi_device_name(device), acpi_device_bid(device));

	if (get_max_brightness(device) <= 0)
		priv->max_brightness = FUJITSU_LCD_N_LEVELS;
	get_lcd_level(device);

	ret = acpi_fujitsu_bl_input_setup(device);
	if (ret)
		return ret;

	return fujitsu_backlight_register(device);
}

/* Brightness notify */

static void acpi_fujitsu_bl_notify(struct acpi_device *device, u32 event)
{
	struct fujitsu_bl *priv = acpi_driver_data(device);
	int oldb, newb;

	if (event != ACPI_FUJITSU_NOTIFY_CODE) {
		acpi_handle_info(device->handle, "unsupported event [0x%x]\n",
				 event);
		sparse_keymap_report_event(priv->input, -1, 1, true);
		return;
	}

	oldb = priv->brightness_level;
	get_lcd_level(device);
	newb = priv->brightness_level;

	acpi_handle_debug(device->handle,
			  "brightness button event [%i -> %i]\n", oldb, newb);

	if (oldb == newb)
		return;

	if (!disable_brightness_adjust)
		set_lcd_level(device, newb);

	sparse_keymap_report_event(priv->input, oldb < newb, 1, true);
}

/* ACPI device for hotkey handling */

static const struct key_entry keymap_default[] = {
	{ KE_KEY, KEY1_CODE,            { KEY_PROG1 } },
	{ KE_KEY, KEY2_CODE,            { KEY_PROG2 } },
	{ KE_KEY, KEY3_CODE,            { KEY_PROG3 } },
	{ KE_KEY, KEY4_CODE,            { KEY_PROG4 } },
	{ KE_KEY, KEY5_CODE,            { KEY_RFKILL } },
	/* Soft keys read from status flags */
	{ KE_KEY, FLAG_RFKILL,          { KEY_RFKILL } },
	{ KE_KEY, FLAG_TOUCHPAD_TOGGLE, { KEY_TOUCHPAD_TOGGLE } },
	{ KE_KEY, FLAG_MICMUTE,         { KEY_MICMUTE } },
	{ KE_END, 0 }
};

static const struct key_entry keymap_s64x0[] = {
	{ KE_KEY, KEY1_CODE, { KEY_SCREENLOCK } },	/* "Lock" */
	{ KE_KEY, KEY2_CODE, { KEY_HELP } },		/* "Mobility Center */
	{ KE_KEY, KEY3_CODE, { KEY_PROG3 } },
	{ KE_KEY, KEY4_CODE, { KEY_PROG4 } },
	{ KE_END, 0 }
};

static const struct key_entry keymap_p8010[] = {
	{ KE_KEY, KEY1_CODE, { KEY_HELP } },		/* "Support" */
	{ KE_KEY, KEY2_CODE, { KEY_PROG2 } },
	{ KE_KEY, KEY3_CODE, { KEY_SWITCHVIDEOMODE } },	/* "Presentation" */
	{ KE_KEY, KEY4_CODE, { KEY_WWW } },		/* "WWW" */
	{ KE_END, 0 }
};

static const struct key_entry *keymap = keymap_default;

static int fujitsu_laptop_dmi_keymap_override(const struct dmi_system_id *id)
{
	pr_info("Identified laptop model '%s'\n", id->ident);
	keymap = id->driver_data;
	return 1;
}

static const struct dmi_system_id fujitsu_laptop_dmi_table[] = {
	{
		.callback = fujitsu_laptop_dmi_keymap_override,
		.ident = "Fujitsu Siemens S6410",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK S6410"),
		},
		.driver_data = (void *)keymap_s64x0
	},
	{
		.callback = fujitsu_laptop_dmi_keymap_override,
		.ident = "Fujitsu Siemens S6420",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK S6420"),
		},
		.driver_data = (void *)keymap_s64x0
	},
	{
		.callback = fujitsu_laptop_dmi_keymap_override,
		.ident = "Fujitsu LifeBook P8010",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook P8010"),
		},
		.driver_data = (void *)keymap_p8010
	},
	{}
};

static int acpi_fujitsu_laptop_input_setup(struct acpi_device *device)
{
	struct fujitsu_laptop *priv = acpi_driver_data(device);
	int ret;

	priv->input = devm_input_allocate_device(&device->dev);
	if (!priv->input)
		return -ENOMEM;

	snprintf(priv->phys, sizeof(priv->phys), "%s/input0",
		 acpi_device_hid(device));

	priv->input->name = acpi_device_name(device);
	priv->input->phys = priv->phys;
	priv->input->id.bustype = BUS_HOST;

	dmi_check_system(fujitsu_laptop_dmi_table);
	ret = sparse_keymap_setup(priv->input, keymap, NULL);
	if (ret)
		return ret;

	return input_register_device(priv->input);
}

static int fujitsu_laptop_platform_add(struct acpi_device *device)
{
	struct fujitsu_laptop *priv = acpi_driver_data(device);
	int ret;

	priv->pf_device = platform_device_alloc("fujitsu-laptop", PLATFORM_DEVID_NONE);
	if (!priv->pf_device)
		return -ENOMEM;

	platform_set_drvdata(priv->pf_device, priv);

	ret = platform_device_add(priv->pf_device);
	if (ret)
		goto err_put_platform_device;

	ret = sysfs_create_group(&priv->pf_device->dev.kobj,
				 &fujitsu_pf_attribute_group);
	if (ret)
		goto err_del_platform_device;

	return 0;

err_del_platform_device:
	platform_device_del(priv->pf_device);
err_put_platform_device:
	platform_device_put(priv->pf_device);

	return ret;
}

static void fujitsu_laptop_platform_remove(struct acpi_device *device)
{
	struct fujitsu_laptop *priv = acpi_driver_data(device);

	sysfs_remove_group(&priv->pf_device->dev.kobj,
			   &fujitsu_pf_attribute_group);
	platform_device_unregister(priv->pf_device);
}

static int logolamp_set(struct led_classdev *cdev,
			enum led_brightness brightness)
{
	struct acpi_device *device = to_acpi_device(cdev->dev->parent);
	int poweron = FUNC_LED_ON, always = FUNC_LED_ON;
	int ret;

	if (brightness < LED_HALF)
		poweron = FUNC_LED_OFF;

	if (brightness < LED_FULL)
		always = FUNC_LED_OFF;

	ret = call_fext_func(device, FUNC_LEDS, 0x1, LOGOLAMP_POWERON, poweron);
	if (ret < 0)
		return ret;

	return call_fext_func(device, FUNC_LEDS, 0x1, LOGOLAMP_ALWAYS, always);
}

static enum led_brightness logolamp_get(struct led_classdev *cdev)
{
	struct acpi_device *device = to_acpi_device(cdev->dev->parent);
	int ret;

	ret = call_fext_func(device, FUNC_LEDS, 0x2, LOGOLAMP_ALWAYS, 0x0);
	if (ret == FUNC_LED_ON)
		return LED_FULL;

	ret = call_fext_func(device, FUNC_LEDS, 0x2, LOGOLAMP_POWERON, 0x0);
	if (ret == FUNC_LED_ON)
		return LED_HALF;

	return LED_OFF;
}

static int kblamps_set(struct led_classdev *cdev,
		       enum led_brightness brightness)
{
	struct acpi_device *device = to_acpi_device(cdev->dev->parent);

	if (brightness >= LED_FULL)
		return call_fext_func(device, FUNC_LEDS, 0x1, KEYBOARD_LAMPS,
				      FUNC_LED_ON);
	else
		return call_fext_func(device, FUNC_LEDS, 0x1, KEYBOARD_LAMPS,
				      FUNC_LED_OFF);
}

static enum led_brightness kblamps_get(struct led_classdev *cdev)
{
	struct acpi_device *device = to_acpi_device(cdev->dev->parent);
	enum led_brightness brightness = LED_OFF;

	if (call_fext_func(device,
			   FUNC_LEDS, 0x2, KEYBOARD_LAMPS, 0x0) == FUNC_LED_ON)
		brightness = LED_FULL;

	return brightness;
}

static int radio_led_set(struct led_classdev *cdev,
			 enum led_brightness brightness)
{
	struct acpi_device *device = to_acpi_device(cdev->dev->parent);

	if (brightness >= LED_FULL)
		return call_fext_func(device, FUNC_FLAGS, 0x5, RADIO_LED_ON,
				      RADIO_LED_ON);
	else
		return call_fext_func(device, FUNC_FLAGS, 0x5, RADIO_LED_ON,
				      0x0);
}

static enum led_brightness radio_led_get(struct led_classdev *cdev)
{
	struct acpi_device *device = to_acpi_device(cdev->dev->parent);
	enum led_brightness brightness = LED_OFF;

	if (call_fext_func(device, FUNC_FLAGS, 0x4, 0x0, 0x0) & RADIO_LED_ON)
		brightness = LED_FULL;

	return brightness;
}

static int eco_led_set(struct led_classdev *cdev,
		       enum led_brightness brightness)
{
	struct acpi_device *device = to_acpi_device(cdev->dev->parent);
	int curr;

	curr = call_fext_func(device, FUNC_LEDS, 0x2, ECO_LED, 0x0);
	if (brightness >= LED_FULL)
		return call_fext_func(device, FUNC_LEDS, 0x1, ECO_LED,
				      curr | ECO_LED_ON);
	else
		return call_fext_func(device, FUNC_LEDS, 0x1, ECO_LED,
				      curr & ~ECO_LED_ON);
}

static enum led_brightness eco_led_get(struct led_classdev *cdev)
{
	struct acpi_device *device = to_acpi_device(cdev->dev->parent);
	enum led_brightness brightness = LED_OFF;

	if (call_fext_func(device, FUNC_LEDS, 0x2, ECO_LED, 0x0) & ECO_LED_ON)
		brightness = LED_FULL;

	return brightness;
}

static int acpi_fujitsu_laptop_leds_register(struct acpi_device *device)
{
	struct fujitsu_laptop *priv = acpi_driver_data(device);
	struct led_classdev *led;
	int ret;

	if (call_fext_func(device,
			   FUNC_LEDS, 0x0, 0x0, 0x0) & LOGOLAMP_POWERON) {
		led = devm_kzalloc(&device->dev, sizeof(*led), GFP_KERNEL);
		if (!led)
			return -ENOMEM;

		led->name = "fujitsu::logolamp";
		led->brightness_set_blocking = logolamp_set;
		led->brightness_get = logolamp_get;
		ret = devm_led_classdev_register(&device->dev, led);
		if (ret)
			return ret;
	}

	if ((call_fext_func(device,
			    FUNC_LEDS, 0x0, 0x0, 0x0) & KEYBOARD_LAMPS) &&
	    (call_fext_func(device, FUNC_BUTTONS, 0x0, 0x0, 0x0) == 0x0)) {
		led = devm_kzalloc(&device->dev, sizeof(*led), GFP_KERNEL);
		if (!led)
			return -ENOMEM;

		led->name = "fujitsu::kblamps";
		led->brightness_set_blocking = kblamps_set;
		led->brightness_get = kblamps_get;
		ret = devm_led_classdev_register(&device->dev, led);
		if (ret)
			return ret;
	}

	/*
	 * Some Fujitsu laptops have a radio toggle button in place of a slide
	 * switch and all such machines appear to also have an RF LED.  Based on
	 * comparing DSDT tables of four Fujitsu Lifebook models (E744, E751,
	 * S7110, S8420; the first one has a radio toggle button, the other
	 * three have slide switches), bit 17 of flags_supported (the value
	 * returned by method S000 of ACPI device FUJ02E3) seems to indicate
	 * whether given model has a radio toggle button.
	 */
	if (priv->flags_supported & BIT(17)) {
		led = devm_kzalloc(&device->dev, sizeof(*led), GFP_KERNEL);
		if (!led)
			return -ENOMEM;

		led->name = "fujitsu::radio_led";
		led->brightness_set_blocking = radio_led_set;
		led->brightness_get = radio_led_get;
		led->default_trigger = "rfkill-any";
		ret = devm_led_classdev_register(&device->dev, led);
		if (ret)
			return ret;
	}

	/* Support for eco led is not always signaled in bit corresponding
	 * to the bit used to control the led. According to the DSDT table,
	 * bit 14 seems to indicate presence of said led as well.
	 * Confirm by testing the status.
	 */
	if ((call_fext_func(device, FUNC_LEDS, 0x0, 0x0, 0x0) & BIT(14)) &&
	    (call_fext_func(device,
			    FUNC_LEDS, 0x2, ECO_LED, 0x0) != UNSUPPORTED_CMD)) {
		led = devm_kzalloc(&device->dev, sizeof(*led), GFP_KERNEL);
		if (!led)
			return -ENOMEM;

		led->name = "fujitsu::eco_led";
		led->brightness_set_blocking = eco_led_set;
		led->brightness_get = eco_led_get;
		ret = devm_led_classdev_register(&device->dev, led);
		if (ret)
			return ret;
	}

	return 0;
}

static int acpi_fujitsu_laptop_add(struct acpi_device *device)
{
	struct fujitsu_laptop *priv;
	int ret, i = 0;

	priv = devm_kzalloc(&device->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	WARN_ONCE(fext, "More than one FUJ02E3 ACPI device was found.  Driver may not work as intended.");
	fext = device;

	strcpy(acpi_device_name(device), ACPI_FUJITSU_LAPTOP_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_FUJITSU_CLASS);
	device->driver_data = priv;

	/* kfifo */
	spin_lock_init(&priv->fifo_lock);
	ret = kfifo_alloc(&priv->fifo, RINGBUFFERSIZE * sizeof(int),
			  GFP_KERNEL);
	if (ret)
		return ret;

	pr_info("ACPI: %s [%s]\n",
		acpi_device_name(device), acpi_device_bid(device));

	while (call_fext_func(device, FUNC_BUTTONS, 0x1, 0x0, 0x0) != 0 &&
	       i++ < MAX_HOTKEY_RINGBUFFER_SIZE)
		; /* No action, result is discarded */
	acpi_handle_debug(device->handle, "Discarded %i ringbuffer entries\n",
			  i);

	priv->flags_supported = call_fext_func(device, FUNC_FLAGS, 0x0, 0x0,
					       0x0);

	/* Make sure our bitmask of supported functions is cleared if the
	   RFKILL function block is not implemented, like on the S7020. */
	if (priv->flags_supported == UNSUPPORTED_CMD)
		priv->flags_supported = 0;

	if (priv->flags_supported)
		priv->flags_state = call_fext_func(device, FUNC_FLAGS, 0x4, 0x0,
						   0x0);

	/* Suspect this is a keymap of the application panel, print it */
	acpi_handle_info(device->handle, "BTNI: [0x%x]\n",
			 call_fext_func(device, FUNC_BUTTONS, 0x0, 0x0, 0x0));

	/* Sync backlight power status */
	if (fujitsu_bl && fujitsu_bl->bl_device &&
	    acpi_video_get_backlight_type() == acpi_backlight_vendor) {
		if (call_fext_func(fext, FUNC_BACKLIGHT, 0x2,
				   BACKLIGHT_PARAM_POWER, 0x0) == BACKLIGHT_OFF)
			fujitsu_bl->bl_device->props.power = BACKLIGHT_POWER_OFF;
		else
			fujitsu_bl->bl_device->props.power = BACKLIGHT_POWER_ON;
	}

	ret = acpi_fujitsu_laptop_input_setup(device);
	if (ret)
		goto err_free_fifo;

	ret = acpi_fujitsu_laptop_leds_register(device);
	if (ret)
		goto err_free_fifo;

	ret = fujitsu_laptop_platform_add(device);
	if (ret)
		goto err_free_fifo;

	ret = fujitsu_battery_charge_control_add(device);
	if (ret < 0)
		pr_warn("Unable to register battery charge control: %d\n", ret);

	return 0;

err_free_fifo:
	kfifo_free(&priv->fifo);

	return ret;
}

static void acpi_fujitsu_laptop_remove(struct acpi_device *device)
{
	struct fujitsu_laptop *priv = acpi_driver_data(device);

	fujitsu_battery_charge_control_remove(device);

	fujitsu_laptop_platform_remove(device);

	kfifo_free(&priv->fifo);
}

static void acpi_fujitsu_laptop_press(struct acpi_device *device, int scancode)
{
	struct fujitsu_laptop *priv = acpi_driver_data(device);
	int ret;

	ret = kfifo_in_locked(&priv->fifo, (unsigned char *)&scancode,
			      sizeof(scancode), &priv->fifo_lock);
	if (ret != sizeof(scancode)) {
		dev_info(&priv->input->dev, "Could not push scancode [0x%x]\n",
			 scancode);
		return;
	}
	sparse_keymap_report_event(priv->input, scancode, 1, false);
	dev_dbg(&priv->input->dev, "Push scancode into ringbuffer [0x%x]\n",
		scancode);
}

static void acpi_fujitsu_laptop_release(struct acpi_device *device)
{
	struct fujitsu_laptop *priv = acpi_driver_data(device);
	int scancode, ret;

	while (true) {
		ret = kfifo_out_locked(&priv->fifo, (unsigned char *)&scancode,
				       sizeof(scancode), &priv->fifo_lock);
		if (ret != sizeof(scancode))
			return;
		sparse_keymap_report_event(priv->input, scancode, 0, false);
		dev_dbg(&priv->input->dev,
			"Pop scancode from ringbuffer [0x%x]\n", scancode);
	}
}

static void acpi_fujitsu_laptop_notify(struct acpi_device *device, u32 event)
{
	struct fujitsu_laptop *priv = acpi_driver_data(device);
	unsigned long flags;
	int scancode, i = 0;
	unsigned int irb;

	if (event != ACPI_FUJITSU_NOTIFY_CODE) {
		acpi_handle_info(device->handle, "Unsupported event [0x%x]\n",
				 event);
		sparse_keymap_report_event(priv->input, -1, 1, true);
		return;
	}

	if (priv->flags_supported)
		priv->flags_state = call_fext_func(device, FUNC_FLAGS, 0x4, 0x0,
						   0x0);

	while ((irb = call_fext_func(device,
				     FUNC_BUTTONS, 0x1, 0x0, 0x0)) != 0 &&
	       i++ < MAX_HOTKEY_RINGBUFFER_SIZE) {
		scancode = irb & 0x4ff;
		if (sparse_keymap_entry_from_scancode(priv->input, scancode))
			acpi_fujitsu_laptop_press(device, scancode);
		else if (scancode == 0)
			acpi_fujitsu_laptop_release(device);
		else
			acpi_handle_info(device->handle,
					 "Unknown GIRB result [%x]\n", irb);
	}

	/*
	 * First seen on the Skylake-based Lifebook E736/E746/E756), the
	 * touchpad toggle hotkey (Fn+F4) is handled in software. Other models
	 * have since added additional "soft keys". These are reported in the
	 * status flags queried using FUNC_FLAGS.
	 */
	if (priv->flags_supported & (FLAG_SOFTKEYS)) {
		flags = call_fext_func(device, FUNC_FLAGS, 0x1, 0x0, 0x0);
		flags &= (FLAG_SOFTKEYS);
		for_each_set_bit(i, &flags, BITS_PER_LONG)
			sparse_keymap_report_event(priv->input, BIT(i), 1, true);
	}
}

/* Initialization */

static const struct acpi_device_id fujitsu_bl_device_ids[] = {
	{ACPI_FUJITSU_BL_HID, 0},
	{"", 0},
};

static struct acpi_driver acpi_fujitsu_bl_driver = {
	.name = ACPI_FUJITSU_BL_DRIVER_NAME,
	.class = ACPI_FUJITSU_CLASS,
	.ids = fujitsu_bl_device_ids,
	.ops = {
		.add = acpi_fujitsu_bl_add,
		.notify = acpi_fujitsu_bl_notify,
		},
};

static const struct acpi_device_id fujitsu_laptop_device_ids[] = {
	{ACPI_FUJITSU_LAPTOP_HID, 0},
	{"", 0},
};

static struct acpi_driver acpi_fujitsu_laptop_driver = {
	.name = ACPI_FUJITSU_LAPTOP_DRIVER_NAME,
	.class = ACPI_FUJITSU_CLASS,
	.ids = fujitsu_laptop_device_ids,
	.ops = {
		.add = acpi_fujitsu_laptop_add,
		.remove = acpi_fujitsu_laptop_remove,
		.notify = acpi_fujitsu_laptop_notify,
		},
};

static const struct acpi_device_id fujitsu_ids[] __used = {
	{ACPI_FUJITSU_BL_HID, 0},
	{ACPI_FUJITSU_LAPTOP_HID, 0},
	{"", 0}
};
MODULE_DEVICE_TABLE(acpi, fujitsu_ids);

static int __init fujitsu_init(void)
{
	int ret;

	ret = acpi_bus_register_driver(&acpi_fujitsu_bl_driver);
	if (ret)
		return ret;

	/* Register platform stuff */

	ret = platform_driver_register(&fujitsu_pf_driver);
	if (ret)
		goto err_unregister_acpi;

	/* Register laptop driver */

	ret = acpi_bus_register_driver(&acpi_fujitsu_laptop_driver);
	if (ret)
		goto err_unregister_platform_driver;

	pr_info("driver " FUJITSU_DRIVER_VERSION " successfully loaded\n");

	return 0;

err_unregister_platform_driver:
	platform_driver_unregister(&fujitsu_pf_driver);
err_unregister_acpi:
	acpi_bus_unregister_driver(&acpi_fujitsu_bl_driver);

	return ret;
}

static void __exit fujitsu_cleanup(void)
{
	acpi_bus_unregister_driver(&acpi_fujitsu_laptop_driver);

	platform_driver_unregister(&fujitsu_pf_driver);

	acpi_bus_unregister_driver(&acpi_fujitsu_bl_driver);

	pr_info("driver unloaded\n");
}

module_init(fujitsu_init);
module_exit(fujitsu_cleanup);

module_param(use_alt_lcd_levels, int, 0644);
MODULE_PARM_DESC(use_alt_lcd_levels, "Interface used for setting LCD brightness level (-1 = auto, 0 = force SBLL, 1 = force SBL2)");
module_param(disable_brightness_adjust, bool, 0644);
MODULE_PARM_DESC(disable_brightness_adjust, "Disable LCD brightness adjustment");

MODULE_AUTHOR("Jonathan Woithe, Peter Gruber, Tony Vroon");
MODULE_DESCRIPTION("Fujitsu laptop extras support");
MODULE_VERSION(FUJITSU_DRIVER_VERSION);
MODULE_LICENSE("GPL");
