/*-*-linux-c-*-*/

/*
  Copyright (C) 2007,2008 Jonathan Woithe <jwoithe@physics.adelaide.edu.au>
  Copyright (C) 2008 Peter Gruber <nokos@gmx.net>
  Copyright (C) 2008 Tony Vroon <tony@linx.net>
  Based on earlier work:
    Copyright (C) 2003 Shane Spencer <shane@bogomip.com>
    Adrian Yee <brewt-fujitsu@brewt.org>

  Templated from msi-laptop.c and thinkpad_acpi.c which is copyright
  by its respective authors.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
 */

/*
 * fujitsu-laptop.c - Fujitsu laptop support, providing access to additional
 * features made available on a range of Fujitsu laptops including the
 * P2xxx/P5xxx/S6xxx/S7xxx series.
 *
 * This driver exports a few files in /sys/devices/platform/fujitsu-laptop/;
 * others may be added at a later date.
 *
 *   lcd_level - Screen brightness: contains a single integer in the
 *   range 0..7. (rw)
 *
 * In addition to these platform device attributes the driver
 * registers itself in the Linux backlight control subsystem and is
 * available to userspace under /sys/class/backlight/fujitsu-laptop/.
 *
 * Hotkeys present on certain Fujitsu laptops (eg: the S6xxx series) are
 * also supported by this driver.
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
#include <linux/dmi.h>
#include <linux/backlight.h>
#include <linux/input.h>
#include <linux/kfifo.h>
#include <linux/video_output.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#if defined(CONFIG_LEDS_CLASS) || defined(CONFIG_LEDS_CLASS_MODULE)
#include <linux/leds.h>
#endif

#define FUJITSU_DRIVER_VERSION "0.6.0"

#define FUJITSU_LCD_N_LEVELS 8

#define ACPI_FUJITSU_CLASS              "fujitsu"
#define ACPI_FUJITSU_HID                "FUJ02B1"
#define ACPI_FUJITSU_DRIVER_NAME	"Fujitsu laptop FUJ02B1 ACPI brightness driver"
#define ACPI_FUJITSU_DEVICE_NAME        "Fujitsu FUJ02B1"
#define ACPI_FUJITSU_HOTKEY_HID 	"FUJ02E3"
#define ACPI_FUJITSU_HOTKEY_DRIVER_NAME "Fujitsu laptop FUJ02E3 ACPI hotkeys driver"
#define ACPI_FUJITSU_HOTKEY_DEVICE_NAME "Fujitsu FUJ02E3"

#define ACPI_FUJITSU_NOTIFY_CODE1     0x80

#define ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS     0x86
#define ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS     0x87

/* FUNC interface - command values */
#define FUNC_RFKILL	0x1000
#define FUNC_LEDS	0x1001
#define FUNC_BUTTONS	0x1002
#define FUNC_BACKLIGHT  0x1004

/* FUNC interface - responses */
#define UNSUPPORTED_CMD 0x80000000

#if defined(CONFIG_LEDS_CLASS) || defined(CONFIG_LEDS_CLASS_MODULE)
/* FUNC interface - LED control */
#define FUNC_LED_OFF	0x1
#define FUNC_LED_ON	0x30001
#define KEYBOARD_LAMPS	0x100
#define LOGOLAMP_POWERON 0x2000
#define LOGOLAMP_ALWAYS  0x4000
#endif

/* Hotkey details */
#define KEY1_CODE	0x410	/* codes for the keys in the GIRB register */
#define KEY2_CODE	0x411
#define KEY3_CODE	0x412
#define KEY4_CODE	0x413

#define MAX_HOTKEY_RINGBUFFER_SIZE 100
#define RINGBUFFERSIZE 40

/* Debugging */
#define FUJLAPTOP_LOG	   ACPI_FUJITSU_HID ": "
#define FUJLAPTOP_ERR	   KERN_ERR FUJLAPTOP_LOG
#define FUJLAPTOP_NOTICE   KERN_NOTICE FUJLAPTOP_LOG
#define FUJLAPTOP_INFO	   KERN_INFO FUJLAPTOP_LOG
#define FUJLAPTOP_DEBUG    KERN_DEBUG FUJLAPTOP_LOG

#define FUJLAPTOP_DBG_ALL	  0xffff
#define FUJLAPTOP_DBG_ERROR	  0x0001
#define FUJLAPTOP_DBG_WARN	  0x0002
#define FUJLAPTOP_DBG_INFO	  0x0004
#define FUJLAPTOP_DBG_TRACE	  0x0008

#define dbg_printk(a_dbg_level, format, arg...) \
	do { if (dbg_level & a_dbg_level) \
		printk(FUJLAPTOP_DEBUG "%s: " format, __func__ , ## arg); \
	} while (0)
#ifdef CONFIG_FUJITSU_LAPTOP_DEBUG
#define vdbg_printk(a_dbg_level, format, arg...) \
	dbg_printk(a_dbg_level, format, ## arg)
#else
#define vdbg_printk(a_dbg_level, format, arg...)
#endif

/* Device controlling the backlight and associated keys */
struct fujitsu_t {
	acpi_handle acpi_handle;
	struct acpi_device *dev;
	struct input_dev *input;
	char phys[32];
	struct backlight_device *bl_device;
	struct platform_device *pf_device;
	int keycode1, keycode2, keycode3, keycode4;

	unsigned int max_brightness;
	unsigned int brightness_changed;
	unsigned int brightness_level;
};

static struct fujitsu_t *fujitsu;
static int use_alt_lcd_levels = -1;
static int disable_brightness_adjust = -1;

/* Device used to access other hotkeys on the laptop */
struct fujitsu_hotkey_t {
	acpi_handle acpi_handle;
	struct acpi_device *dev;
	struct input_dev *input;
	char phys[32];
	struct platform_device *pf_device;
	struct kfifo fifo;
	spinlock_t fifo_lock;
	int rfkill_supported;
	int rfkill_state;
	int logolamp_registered;
	int kblamps_registered;
};

static struct fujitsu_hotkey_t *fujitsu_hotkey;

static void acpi_fujitsu_hotkey_notify(struct acpi_device *device, u32 event);

#if defined(CONFIG_LEDS_CLASS) || defined(CONFIG_LEDS_CLASS_MODULE)
static enum led_brightness logolamp_get(struct led_classdev *cdev);
static void logolamp_set(struct led_classdev *cdev,
			       enum led_brightness brightness);

static struct led_classdev logolamp_led = {
 .name = "fujitsu::logolamp",
 .brightness_get = logolamp_get,
 .brightness_set = logolamp_set
};

static enum led_brightness kblamps_get(struct led_classdev *cdev);
static void kblamps_set(struct led_classdev *cdev,
			       enum led_brightness brightness);

static struct led_classdev kblamps_led = {
 .name = "fujitsu::kblamps",
 .brightness_get = kblamps_get,
 .brightness_set = kblamps_set
};
#endif

#ifdef CONFIG_FUJITSU_LAPTOP_DEBUG
static u32 dbg_level = 0x03;
#endif

static void acpi_fujitsu_notify(struct acpi_device *device, u32 event);

/* Fujitsu ACPI interface function */

static int call_fext_func(int cmd, int arg0, int arg1, int arg2)
{
	acpi_status status = AE_OK;
	union acpi_object params[4] = {
	{ .type = ACPI_TYPE_INTEGER },
	{ .type = ACPI_TYPE_INTEGER },
	{ .type = ACPI_TYPE_INTEGER },
	{ .type = ACPI_TYPE_INTEGER }
	};
	struct acpi_object_list arg_list = { 4, &params[0] };
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_handle handle = NULL;

	status = acpi_get_handle(fujitsu_hotkey->acpi_handle, "FUNC", &handle);
	if (ACPI_FAILURE(status)) {
		vdbg_printk(FUJLAPTOP_DBG_ERROR,
				"FUNC interface is not present\n");
		return -ENODEV;
	}

	params[0].integer.value = cmd;
	params[1].integer.value = arg0;
	params[2].integer.value = arg1;
	params[3].integer.value = arg2;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, NULL, &arg_list, &output);
	if (ACPI_FAILURE(status)) {
		vdbg_printk(FUJLAPTOP_DBG_WARN,
			"FUNC 0x%x (args 0x%x, 0x%x, 0x%x) call failed\n",
				cmd, arg0, arg1, arg2);
		return -ENODEV;
	}

	if (out_obj.type != ACPI_TYPE_INTEGER) {
		vdbg_printk(FUJLAPTOP_DBG_WARN,
			"FUNC 0x%x (args 0x%x, 0x%x, 0x%x) did not "
			"return an integer\n",
			cmd, arg0, arg1, arg2);
		return -ENODEV;
	}

	vdbg_printk(FUJLAPTOP_DBG_TRACE,
		"FUNC 0x%x (args 0x%x, 0x%x, 0x%x) returned 0x%x\n",
			cmd, arg0, arg1, arg2, (int)out_obj.integer.value);
	return out_obj.integer.value;
}

#if defined(CONFIG_LEDS_CLASS) || defined(CONFIG_LEDS_CLASS_MODULE)
/* LED class callbacks */

static void logolamp_set(struct led_classdev *cdev,
			       enum led_brightness brightness)
{
	if (brightness >= LED_FULL) {
		call_fext_func(FUNC_LEDS, 0x1, LOGOLAMP_POWERON, FUNC_LED_ON);
		call_fext_func(FUNC_LEDS, 0x1, LOGOLAMP_ALWAYS, FUNC_LED_ON);
	} else if (brightness >= LED_HALF) {
		call_fext_func(FUNC_LEDS, 0x1, LOGOLAMP_POWERON, FUNC_LED_ON);
		call_fext_func(FUNC_LEDS, 0x1, LOGOLAMP_ALWAYS, FUNC_LED_OFF);
	} else {
		call_fext_func(FUNC_LEDS, 0x1, LOGOLAMP_POWERON, FUNC_LED_OFF);
	}
}

static void kblamps_set(struct led_classdev *cdev,
			       enum led_brightness brightness)
{
	if (brightness >= LED_FULL)
		call_fext_func(FUNC_LEDS, 0x1, KEYBOARD_LAMPS, FUNC_LED_ON);
	else
		call_fext_func(FUNC_LEDS, 0x1, KEYBOARD_LAMPS, FUNC_LED_OFF);
}

static enum led_brightness logolamp_get(struct led_classdev *cdev)
{
	enum led_brightness brightness = LED_OFF;
	int poweron, always;

	poweron = call_fext_func(FUNC_LEDS, 0x2, LOGOLAMP_POWERON, 0x0);
	if (poweron == FUNC_LED_ON) {
		brightness = LED_HALF;
		always = call_fext_func(FUNC_LEDS, 0x2, LOGOLAMP_ALWAYS, 0x0);
		if (always == FUNC_LED_ON)
			brightness = LED_FULL;
	}
	return brightness;
}

static enum led_brightness kblamps_get(struct led_classdev *cdev)
{
	enum led_brightness brightness = LED_OFF;

	if (call_fext_func(FUNC_LEDS, 0x2, KEYBOARD_LAMPS, 0x0) == FUNC_LED_ON)
		brightness = LED_FULL;

	return brightness;
}
#endif

/* Hardware access for LCD brightness control */

static int set_lcd_level(int level)
{
	acpi_status status = AE_OK;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg0 };
	acpi_handle handle = NULL;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "set lcd level via SBLL [%d]\n",
		    level);

	if (level < 0 || level >= fujitsu->max_brightness)
		return -EINVAL;

	status = acpi_get_handle(fujitsu->acpi_handle, "SBLL", &handle);
	if (ACPI_FAILURE(status)) {
		vdbg_printk(FUJLAPTOP_DBG_ERROR, "SBLL not present\n");
		return -ENODEV;
	}

	arg0.integer.value = level;

	status = acpi_evaluate_object(handle, NULL, &arg_list, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return 0;
}

static int set_lcd_level_alt(int level)
{
	acpi_status status = AE_OK;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg0 };
	acpi_handle handle = NULL;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "set lcd level via SBL2 [%d]\n",
		    level);

	if (level < 0 || level >= fujitsu->max_brightness)
		return -EINVAL;

	status = acpi_get_handle(fujitsu->acpi_handle, "SBL2", &handle);
	if (ACPI_FAILURE(status)) {
		vdbg_printk(FUJLAPTOP_DBG_ERROR, "SBL2 not present\n");
		return -ENODEV;
	}

	arg0.integer.value = level;

	status = acpi_evaluate_object(handle, NULL, &arg_list, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return 0;
}

static int get_lcd_level(void)
{
	unsigned long long state = 0;
	acpi_status status = AE_OK;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "get lcd level via GBLL\n");

	status =
	    acpi_evaluate_integer(fujitsu->acpi_handle, "GBLL", NULL, &state);
	if (ACPI_FAILURE(status))
		return 0;

	fujitsu->brightness_level = state & 0x0fffffff;

	if (state & 0x80000000)
		fujitsu->brightness_changed = 1;
	else
		fujitsu->brightness_changed = 0;

	return fujitsu->brightness_level;
}

static int get_max_brightness(void)
{
	unsigned long long state = 0;
	acpi_status status = AE_OK;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "get max lcd level via RBLL\n");

	status =
	    acpi_evaluate_integer(fujitsu->acpi_handle, "RBLL", NULL, &state);
	if (ACPI_FAILURE(status))
		return -1;

	fujitsu->max_brightness = state;

	return fujitsu->max_brightness;
}

/* Backlight device stuff */

static int bl_get_brightness(struct backlight_device *b)
{
	return get_lcd_level();
}

static int bl_update_status(struct backlight_device *b)
{
	int ret;
	if (b->props.power == 4)
		ret = call_fext_func(FUNC_BACKLIGHT, 0x1, 0x4, 0x3);
	else
		ret = call_fext_func(FUNC_BACKLIGHT, 0x1, 0x4, 0x0);
	if (ret != 0)
		vdbg_printk(FUJLAPTOP_DBG_ERROR,
			"Unable to adjust backlight power, error code %i\n",
			ret);

	if (use_alt_lcd_levels)
		ret = set_lcd_level_alt(b->props.brightness);
	else
		ret = set_lcd_level(b->props.brightness);
	if (ret != 0)
		vdbg_printk(FUJLAPTOP_DBG_ERROR,
			"Unable to adjust LCD brightness, error code %i\n",
			ret);
	return ret;
}

static const struct backlight_ops fujitsubl_ops = {
	.get_brightness = bl_get_brightness,
	.update_status = bl_update_status,
};

/* Platform LCD brightness device */

static ssize_t
show_max_brightness(struct device *dev,
		    struct device_attribute *attr, char *buf)
{

	int ret;

	ret = get_max_brightness();
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
show_brightness_changed(struct device *dev,
			struct device_attribute *attr, char *buf)
{

	int ret;

	ret = fujitsu->brightness_changed;
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t show_lcd_level(struct device *dev,
			      struct device_attribute *attr, char *buf)
{

	int ret;

	ret = get_lcd_level();
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t store_lcd_level(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{

	int level, ret;

	if (sscanf(buf, "%i", &level) != 1
	    || (level < 0 || level >= fujitsu->max_brightness))
		return -EINVAL;

	if (use_alt_lcd_levels)
		ret = set_lcd_level_alt(level);
	else
		ret = set_lcd_level(level);
	if (ret < 0)
		return ret;

	ret = get_lcd_level();
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t
ignore_store(struct device *dev,
	     struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t
show_lid_state(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	if (!(fujitsu_hotkey->rfkill_supported & 0x100))
		return sprintf(buf, "unknown\n");
	if (fujitsu_hotkey->rfkill_state & 0x100)
		return sprintf(buf, "open\n");
	else
		return sprintf(buf, "closed\n");
}

static ssize_t
show_dock_state(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	if (!(fujitsu_hotkey->rfkill_supported & 0x200))
		return sprintf(buf, "unknown\n");
	if (fujitsu_hotkey->rfkill_state & 0x200)
		return sprintf(buf, "docked\n");
	else
		return sprintf(buf, "undocked\n");
}

static ssize_t
show_radios_state(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	if (!(fujitsu_hotkey->rfkill_supported & 0x20))
		return sprintf(buf, "unknown\n");
	if (fujitsu_hotkey->rfkill_state & 0x20)
		return sprintf(buf, "on\n");
	else
		return sprintf(buf, "killed\n");
}

static DEVICE_ATTR(max_brightness, 0444, show_max_brightness, ignore_store);
static DEVICE_ATTR(brightness_changed, 0444, show_brightness_changed,
		   ignore_store);
static DEVICE_ATTR(lcd_level, 0644, show_lcd_level, store_lcd_level);
static DEVICE_ATTR(lid, 0444, show_lid_state, ignore_store);
static DEVICE_ATTR(dock, 0444, show_dock_state, ignore_store);
static DEVICE_ATTR(radios, 0444, show_radios_state, ignore_store);

static struct attribute *fujitsupf_attributes[] = {
	&dev_attr_brightness_changed.attr,
	&dev_attr_max_brightness.attr,
	&dev_attr_lcd_level.attr,
	&dev_attr_lid.attr,
	&dev_attr_dock.attr,
	&dev_attr_radios.attr,
	NULL
};

static struct attribute_group fujitsupf_attribute_group = {
	.attrs = fujitsupf_attributes
};

static struct platform_driver fujitsupf_driver = {
	.driver = {
		   .name = "fujitsu-laptop",
		   .owner = THIS_MODULE,
		   }
};

static void dmi_check_cb_common(const struct dmi_system_id *id)
{
	acpi_handle handle;
	pr_info("Identified laptop model '%s'\n", id->ident);
	if (use_alt_lcd_levels == -1) {
		if (ACPI_SUCCESS(acpi_get_handle(NULL,
				"\\_SB.PCI0.LPCB.FJEX.SBL2", &handle)))
			use_alt_lcd_levels = 1;
		else
			use_alt_lcd_levels = 0;
		vdbg_printk(FUJLAPTOP_DBG_TRACE, "auto-detected usealt as "
			"%i\n", use_alt_lcd_levels);
	}
}

static int dmi_check_cb_s6410(const struct dmi_system_id *id)
{
	dmi_check_cb_common(id);
	fujitsu->keycode1 = KEY_SCREENLOCK;	/* "Lock" */
	fujitsu->keycode2 = KEY_HELP;	/* "Mobility Center" */
	return 1;
}

static int dmi_check_cb_s6420(const struct dmi_system_id *id)
{
	dmi_check_cb_common(id);
	fujitsu->keycode1 = KEY_SCREENLOCK;	/* "Lock" */
	fujitsu->keycode2 = KEY_HELP;	/* "Mobility Center" */
	return 1;
}

static int dmi_check_cb_p8010(const struct dmi_system_id *id)
{
	dmi_check_cb_common(id);
	fujitsu->keycode1 = KEY_HELP;	/* "Support" */
	fujitsu->keycode3 = KEY_SWITCHVIDEOMODE;	/* "Presentation" */
	fujitsu->keycode4 = KEY_WWW;	/* "Internet" */
	return 1;
}

static struct dmi_system_id fujitsu_dmi_table[] = {
	{
	 .ident = "Fujitsu Siemens S6410",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK S6410"),
		     },
	 .callback = dmi_check_cb_s6410},
	{
	 .ident = "Fujitsu Siemens S6420",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK S6420"),
		     },
	 .callback = dmi_check_cb_s6420},
	{
	 .ident = "Fujitsu LifeBook P8010",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook P8010"),
		     },
	 .callback = dmi_check_cb_p8010},
	{}
};

/* ACPI device for LCD brightness control */

static int acpi_fujitsu_add(struct acpi_device *device)
{
	acpi_handle handle;
	int result = 0;
	int state = 0;
	struct input_dev *input;
	int error;

	if (!device)
		return -EINVAL;

	fujitsu->acpi_handle = device->handle;
	sprintf(acpi_device_name(device), "%s", ACPI_FUJITSU_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_FUJITSU_CLASS);
	device->driver_data = fujitsu;

	fujitsu->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_stop;
	}

	snprintf(fujitsu->phys, sizeof(fujitsu->phys),
		 "%s/video/input0", acpi_device_hid(device));

	input->name = acpi_device_name(device);
	input->phys = fujitsu->phys;
	input->id.bustype = BUS_HOST;
	input->id.product = 0x06;
	input->dev.parent = &device->dev;
	input->evbit[0] = BIT(EV_KEY);
	set_bit(KEY_BRIGHTNESSUP, input->keybit);
	set_bit(KEY_BRIGHTNESSDOWN, input->keybit);
	set_bit(KEY_UNKNOWN, input->keybit);

	error = input_register_device(input);
	if (error)
		goto err_free_input_dev;

	result = acpi_bus_update_power(fujitsu->acpi_handle, &state);
	if (result) {
		pr_err("Error reading power state\n");
		goto err_unregister_input_dev;
	}

	pr_info("ACPI: %s [%s] (%s)\n",
	       acpi_device_name(device), acpi_device_bid(device),
	       !device->power.state ? "on" : "off");

	fujitsu->dev = device;

	if (ACPI_SUCCESS
	    (acpi_get_handle(device->handle, METHOD_NAME__INI, &handle))) {
		vdbg_printk(FUJLAPTOP_DBG_INFO, "Invoking _INI\n");
		if (ACPI_FAILURE
		    (acpi_evaluate_object
		     (device->handle, METHOD_NAME__INI, NULL, NULL)))
			pr_err("_INI Method failed\n");
	}

	/* do config (detect defaults) */
	use_alt_lcd_levels = use_alt_lcd_levels == 1 ? 1 : 0;
	disable_brightness_adjust = disable_brightness_adjust == 1 ? 1 : 0;
	vdbg_printk(FUJLAPTOP_DBG_INFO,
		    "config: [alt interface: %d], [adjust disable: %d]\n",
		    use_alt_lcd_levels, disable_brightness_adjust);

	if (get_max_brightness() <= 0)
		fujitsu->max_brightness = FUJITSU_LCD_N_LEVELS;
	get_lcd_level();

	return result;

err_unregister_input_dev:
	input_unregister_device(input);
	input = NULL;
err_free_input_dev:
	input_free_device(input);
err_stop:
	return result;
}

static int acpi_fujitsu_remove(struct acpi_device *device, int type)
{
	struct fujitsu_t *fujitsu = acpi_driver_data(device);
	struct input_dev *input = fujitsu->input;

	input_unregister_device(input);

	fujitsu->acpi_handle = NULL;

	return 0;
}

/* Brightness notify */

static void acpi_fujitsu_notify(struct acpi_device *device, u32 event)
{
	struct input_dev *input;
	int keycode;
	int oldb, newb;

	input = fujitsu->input;

	switch (event) {
	case ACPI_FUJITSU_NOTIFY_CODE1:
		keycode = 0;
		oldb = fujitsu->brightness_level;
		get_lcd_level();
		newb = fujitsu->brightness_level;

		vdbg_printk(FUJLAPTOP_DBG_TRACE,
			    "brightness button event [%i -> %i (%i)]\n",
			    oldb, newb, fujitsu->brightness_changed);

		if (oldb < newb) {
			if (disable_brightness_adjust != 1) {
				if (use_alt_lcd_levels)
					set_lcd_level_alt(newb);
				else
					set_lcd_level(newb);
			}
			acpi_bus_generate_proc_event(fujitsu->dev,
				ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS, 0);
			keycode = KEY_BRIGHTNESSUP;
		} else if (oldb > newb) {
			if (disable_brightness_adjust != 1) {
				if (use_alt_lcd_levels)
					set_lcd_level_alt(newb);
				else
					set_lcd_level(newb);
			}
			acpi_bus_generate_proc_event(fujitsu->dev,
				ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS, 0);
			keycode = KEY_BRIGHTNESSDOWN;
		}
		break;
	default:
		keycode = KEY_UNKNOWN;
		vdbg_printk(FUJLAPTOP_DBG_WARN,
			    "unsupported event [0x%x]\n", event);
		break;
	}

	if (keycode != 0) {
		input_report_key(input, keycode, 1);
		input_sync(input);
		input_report_key(input, keycode, 0);
		input_sync(input);
	}
}

/* ACPI device for hotkey handling */

static int acpi_fujitsu_hotkey_add(struct acpi_device *device)
{
	acpi_handle handle;
	int result = 0;
	int state = 0;
	struct input_dev *input;
	int error;
	int i;

	if (!device)
		return -EINVAL;

	fujitsu_hotkey->acpi_handle = device->handle;
	sprintf(acpi_device_name(device), "%s",
		ACPI_FUJITSU_HOTKEY_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_FUJITSU_CLASS);
	device->driver_data = fujitsu_hotkey;

	/* kfifo */
	spin_lock_init(&fujitsu_hotkey->fifo_lock);
	error = kfifo_alloc(&fujitsu_hotkey->fifo, RINGBUFFERSIZE * sizeof(int),
			GFP_KERNEL);
	if (error) {
		pr_err("kfifo_alloc failed\n");
		goto err_stop;
	}

	fujitsu_hotkey->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_free_fifo;
	}

	snprintf(fujitsu_hotkey->phys, sizeof(fujitsu_hotkey->phys),
		 "%s/video/input0", acpi_device_hid(device));

	input->name = acpi_device_name(device);
	input->phys = fujitsu_hotkey->phys;
	input->id.bustype = BUS_HOST;
	input->id.product = 0x06;
	input->dev.parent = &device->dev;

	set_bit(EV_KEY, input->evbit);
	set_bit(fujitsu->keycode1, input->keybit);
	set_bit(fujitsu->keycode2, input->keybit);
	set_bit(fujitsu->keycode3, input->keybit);
	set_bit(fujitsu->keycode4, input->keybit);
	set_bit(KEY_UNKNOWN, input->keybit);

	error = input_register_device(input);
	if (error)
		goto err_free_input_dev;

	result = acpi_bus_update_power(fujitsu_hotkey->acpi_handle, &state);
	if (result) {
		pr_err("Error reading power state\n");
		goto err_unregister_input_dev;
	}

	pr_info("ACPI: %s [%s] (%s)\n",
		acpi_device_name(device), acpi_device_bid(device),
		!device->power.state ? "on" : "off");

	fujitsu_hotkey->dev = device;

	if (ACPI_SUCCESS
	    (acpi_get_handle(device->handle, METHOD_NAME__INI, &handle))) {
		vdbg_printk(FUJLAPTOP_DBG_INFO, "Invoking _INI\n");
		if (ACPI_FAILURE
		    (acpi_evaluate_object
		     (device->handle, METHOD_NAME__INI, NULL, NULL)))
			pr_err("_INI Method failed\n");
	}

	i = 0;
	while (call_fext_func(FUNC_BUTTONS, 0x1, 0x0, 0x0) != 0
		&& (i++) < MAX_HOTKEY_RINGBUFFER_SIZE)
		; /* No action, result is discarded */
	vdbg_printk(FUJLAPTOP_DBG_INFO, "Discarded %i ringbuffer entries\n", i);

	fujitsu_hotkey->rfkill_supported =
		call_fext_func(FUNC_RFKILL, 0x0, 0x0, 0x0);

	/* Make sure our bitmask of supported functions is cleared if the
	   RFKILL function block is not implemented, like on the S7020. */
	if (fujitsu_hotkey->rfkill_supported == UNSUPPORTED_CMD)
		fujitsu_hotkey->rfkill_supported = 0;

	if (fujitsu_hotkey->rfkill_supported)
		fujitsu_hotkey->rfkill_state =
			call_fext_func(FUNC_RFKILL, 0x4, 0x0, 0x0);

	/* Suspect this is a keymap of the application panel, print it */
	pr_info("BTNI: [0x%x]\n", call_fext_func(FUNC_BUTTONS, 0x0, 0x0, 0x0));

#if defined(CONFIG_LEDS_CLASS) || defined(CONFIG_LEDS_CLASS_MODULE)
	if (call_fext_func(FUNC_LEDS, 0x0, 0x0, 0x0) & LOGOLAMP_POWERON) {
		result = led_classdev_register(&fujitsu->pf_device->dev,
						&logolamp_led);
		if (result == 0) {
			fujitsu_hotkey->logolamp_registered = 1;
		} else {
			pr_err("Could not register LED handler for logo lamp, error %i\n",
			       result);
		}
	}

	if ((call_fext_func(FUNC_LEDS, 0x0, 0x0, 0x0) & KEYBOARD_LAMPS) &&
	   (call_fext_func(FUNC_BUTTONS, 0x0, 0x0, 0x0) == 0x0)) {
		result = led_classdev_register(&fujitsu->pf_device->dev,
						&kblamps_led);
		if (result == 0) {
			fujitsu_hotkey->kblamps_registered = 1;
		} else {
			pr_err("Could not register LED handler for keyboard lamps, error %i\n",
			       result);
		}
	}
#endif

	return result;

err_unregister_input_dev:
	input_unregister_device(input);
	input = NULL;
err_free_input_dev:
	input_free_device(input);
err_free_fifo:
	kfifo_free(&fujitsu_hotkey->fifo);
err_stop:
	return result;
}

static int acpi_fujitsu_hotkey_remove(struct acpi_device *device, int type)
{
	struct fujitsu_hotkey_t *fujitsu_hotkey = acpi_driver_data(device);
	struct input_dev *input = fujitsu_hotkey->input;

#if defined(CONFIG_LEDS_CLASS) || defined(CONFIG_LEDS_CLASS_MODULE)
	if (fujitsu_hotkey->logolamp_registered)
		led_classdev_unregister(&logolamp_led);

	if (fujitsu_hotkey->kblamps_registered)
		led_classdev_unregister(&kblamps_led);
#endif

	input_unregister_device(input);

	kfifo_free(&fujitsu_hotkey->fifo);

	fujitsu_hotkey->acpi_handle = NULL;

	return 0;
}

static void acpi_fujitsu_hotkey_notify(struct acpi_device *device, u32 event)
{
	struct input_dev *input;
	int keycode, keycode_r;
	unsigned int irb = 1;
	int i, status;

	input = fujitsu_hotkey->input;

	if (fujitsu_hotkey->rfkill_supported)
		fujitsu_hotkey->rfkill_state =
			call_fext_func(FUNC_RFKILL, 0x4, 0x0, 0x0);

	switch (event) {
	case ACPI_FUJITSU_NOTIFY_CODE1:
		i = 0;
		while ((irb =
			call_fext_func(FUNC_BUTTONS, 0x1, 0x0, 0x0)) != 0
				&& (i++) < MAX_HOTKEY_RINGBUFFER_SIZE) {
			switch (irb & 0x4ff) {
			case KEY1_CODE:
				keycode = fujitsu->keycode1;
				break;
			case KEY2_CODE:
				keycode = fujitsu->keycode2;
				break;
			case KEY3_CODE:
				keycode = fujitsu->keycode3;
				break;
			case KEY4_CODE:
				keycode = fujitsu->keycode4;
				break;
			case 0:
				keycode = 0;
				break;
			default:
				vdbg_printk(FUJLAPTOP_DBG_WARN,
					    "Unknown GIRB result [%x]\n", irb);
				keycode = -1;
				break;
			}
			if (keycode > 0) {
				vdbg_printk(FUJLAPTOP_DBG_TRACE,
					"Push keycode into ringbuffer [%d]\n",
					keycode);
				status = kfifo_in_locked(&fujitsu_hotkey->fifo,
						   (unsigned char *)&keycode,
						   sizeof(keycode),
						   &fujitsu_hotkey->fifo_lock);
				if (status != sizeof(keycode)) {
					vdbg_printk(FUJLAPTOP_DBG_WARN,
					    "Could not push keycode [0x%x]\n",
					    keycode);
				} else {
					input_report_key(input, keycode, 1);
					input_sync(input);
				}
			} else if (keycode == 0) {
				while ((status =
					kfifo_out_locked(
					 &fujitsu_hotkey->fifo,
					 (unsigned char *) &keycode_r,
					 sizeof(keycode_r),
					 &fujitsu_hotkey->fifo_lock))
					 == sizeof(keycode_r)) {
					input_report_key(input, keycode_r, 0);
					input_sync(input);
					vdbg_printk(FUJLAPTOP_DBG_TRACE,
					  "Pop keycode from ringbuffer [%d]\n",
					  keycode_r);
				}
			}
		}

		break;
	default:
		keycode = KEY_UNKNOWN;
		vdbg_printk(FUJLAPTOP_DBG_WARN,
			    "Unsupported event [0x%x]\n", event);
		input_report_key(input, keycode, 1);
		input_sync(input);
		input_report_key(input, keycode, 0);
		input_sync(input);
		break;
	}
}

/* Initialization */

static const struct acpi_device_id fujitsu_device_ids[] = {
	{ACPI_FUJITSU_HID, 0},
	{"", 0},
};

static struct acpi_driver acpi_fujitsu_driver = {
	.name = ACPI_FUJITSU_DRIVER_NAME,
	.class = ACPI_FUJITSU_CLASS,
	.ids = fujitsu_device_ids,
	.ops = {
		.add = acpi_fujitsu_add,
		.remove = acpi_fujitsu_remove,
		.notify = acpi_fujitsu_notify,
		},
};

static const struct acpi_device_id fujitsu_hotkey_device_ids[] = {
	{ACPI_FUJITSU_HOTKEY_HID, 0},
	{"", 0},
};

static struct acpi_driver acpi_fujitsu_hotkey_driver = {
	.name = ACPI_FUJITSU_HOTKEY_DRIVER_NAME,
	.class = ACPI_FUJITSU_CLASS,
	.ids = fujitsu_hotkey_device_ids,
	.ops = {
		.add = acpi_fujitsu_hotkey_add,
		.remove = acpi_fujitsu_hotkey_remove,
		.notify = acpi_fujitsu_hotkey_notify,
		},
};

static int __init fujitsu_init(void)
{
	int ret, result, max_brightness;

	if (acpi_disabled)
		return -ENODEV;

	fujitsu = kzalloc(sizeof(struct fujitsu_t), GFP_KERNEL);
	if (!fujitsu)
		return -ENOMEM;
	fujitsu->keycode1 = KEY_PROG1;
	fujitsu->keycode2 = KEY_PROG2;
	fujitsu->keycode3 = KEY_PROG3;
	fujitsu->keycode4 = KEY_PROG4;
	dmi_check_system(fujitsu_dmi_table);

	result = acpi_bus_register_driver(&acpi_fujitsu_driver);
	if (result < 0) {
		ret = -ENODEV;
		goto fail_acpi;
	}

	/* Register platform stuff */

	fujitsu->pf_device = platform_device_alloc("fujitsu-laptop", -1);
	if (!fujitsu->pf_device) {
		ret = -ENOMEM;
		goto fail_platform_driver;
	}

	ret = platform_device_add(fujitsu->pf_device);
	if (ret)
		goto fail_platform_device1;

	ret =
	    sysfs_create_group(&fujitsu->pf_device->dev.kobj,
			       &fujitsupf_attribute_group);
	if (ret)
		goto fail_platform_device2;

	/* Register backlight stuff */

	if (!acpi_video_backlight_support()) {
		struct backlight_properties props;

		memset(&props, 0, sizeof(struct backlight_properties));
		max_brightness = fujitsu->max_brightness;
		props.type = BACKLIGHT_PLATFORM;
		props.max_brightness = max_brightness - 1;
		fujitsu->bl_device = backlight_device_register("fujitsu-laptop",
							       NULL, NULL,
							       &fujitsubl_ops,
							       &props);
		if (IS_ERR(fujitsu->bl_device)) {
			ret = PTR_ERR(fujitsu->bl_device);
			fujitsu->bl_device = NULL;
			goto fail_sysfs_group;
		}
		fujitsu->bl_device->props.brightness = fujitsu->brightness_level;
	}

	ret = platform_driver_register(&fujitsupf_driver);
	if (ret)
		goto fail_backlight;

	/* Register hotkey driver */

	fujitsu_hotkey = kzalloc(sizeof(struct fujitsu_hotkey_t), GFP_KERNEL);
	if (!fujitsu_hotkey) {
		ret = -ENOMEM;
		goto fail_hotkey;
	}

	result = acpi_bus_register_driver(&acpi_fujitsu_hotkey_driver);
	if (result < 0) {
		ret = -ENODEV;
		goto fail_hotkey1;
	}

	/* Sync backlight power status (needs FUJ02E3 device, hence deferred) */

	if (!acpi_video_backlight_support()) {
		if (call_fext_func(FUNC_BACKLIGHT, 0x2, 0x4, 0x0) == 3)
			fujitsu->bl_device->props.power = 4;
		else
			fujitsu->bl_device->props.power = 0;
	}

	pr_info("driver " FUJITSU_DRIVER_VERSION " successfully loaded\n");

	return 0;

fail_hotkey1:
	kfree(fujitsu_hotkey);
fail_hotkey:
	platform_driver_unregister(&fujitsupf_driver);
fail_backlight:
	if (fujitsu->bl_device)
		backlight_device_unregister(fujitsu->bl_device);
fail_sysfs_group:
	sysfs_remove_group(&fujitsu->pf_device->dev.kobj,
			   &fujitsupf_attribute_group);
fail_platform_device2:
	platform_device_del(fujitsu->pf_device);
fail_platform_device1:
	platform_device_put(fujitsu->pf_device);
fail_platform_driver:
	acpi_bus_unregister_driver(&acpi_fujitsu_driver);
fail_acpi:
	kfree(fujitsu);

	return ret;
}

static void __exit fujitsu_cleanup(void)
{
	acpi_bus_unregister_driver(&acpi_fujitsu_hotkey_driver);

	kfree(fujitsu_hotkey);

	platform_driver_unregister(&fujitsupf_driver);

	if (fujitsu->bl_device)
		backlight_device_unregister(fujitsu->bl_device);

	sysfs_remove_group(&fujitsu->pf_device->dev.kobj,
			   &fujitsupf_attribute_group);

	platform_device_unregister(fujitsu->pf_device);

	acpi_bus_unregister_driver(&acpi_fujitsu_driver);

	kfree(fujitsu);

	pr_info("driver unloaded\n");
}

module_init(fujitsu_init);
module_exit(fujitsu_cleanup);

module_param(use_alt_lcd_levels, uint, 0644);
MODULE_PARM_DESC(use_alt_lcd_levels,
		 "Use alternative interface for lcd_levels (needed for Lifebook s6410).");
module_param(disable_brightness_adjust, uint, 0644);
MODULE_PARM_DESC(disable_brightness_adjust, "Disable brightness adjustment .");
#ifdef CONFIG_FUJITSU_LAPTOP_DEBUG
module_param_named(debug, dbg_level, uint, 0644);
MODULE_PARM_DESC(debug, "Sets debug level bit-mask");
#endif

MODULE_AUTHOR("Jonathan Woithe, Peter Gruber, Tony Vroon");
MODULE_DESCRIPTION("Fujitsu laptop extras support");
MODULE_VERSION(FUJITSU_DRIVER_VERSION);
MODULE_LICENSE("GPL");

MODULE_ALIAS("dmi:*:svnFUJITSUSIEMENS:*:pvr:rvnFUJITSU:rnFJNB1D3:*:cvrS6410:*");
MODULE_ALIAS("dmi:*:svnFUJITSUSIEMENS:*:pvr:rvnFUJITSU:rnFJNB1E6:*:cvrS6420:*");
MODULE_ALIAS("dmi:*:svnFUJITSU:*:pvr:rvnFUJITSU:rnFJNB19C:*:cvrS7020:*");

static struct pnp_device_id pnp_ids[] __used = {
	{.id = "FUJ02bf"},
	{.id = "FUJ02B1"},
	{.id = "FUJ02E3"},
	{.id = ""}
};

MODULE_DEVICE_TABLE(pnp, pnp_ids);
