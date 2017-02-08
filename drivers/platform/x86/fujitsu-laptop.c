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
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kfifo.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#if IS_ENABLED(CONFIG_LEDS_CLASS)
#include <linux/leds.h>
#endif
#include <acpi/video.h>

#define FUJITSU_DRIVER_VERSION "0.6.0"

#define FUJITSU_LCD_N_LEVELS 8

#define ACPI_FUJITSU_CLASS		"fujitsu"
#define ACPI_FUJITSU_BL_HID		"FUJ02B1"
#define ACPI_FUJITSU_BL_DRIVER_NAME	"Fujitsu laptop FUJ02B1 ACPI brightness driver"
#define ACPI_FUJITSU_BL_DEVICE_NAME	"Fujitsu FUJ02B1"
#define ACPI_FUJITSU_LAPTOP_HID		"FUJ02E3"
#define ACPI_FUJITSU_LAPTOP_DRIVER_NAME	"Fujitsu laptop FUJ02E3 ACPI hotkeys driver"
#define ACPI_FUJITSU_LAPTOP_DEVICE_NAME	"Fujitsu FUJ02E3"

#define ACPI_FUJITSU_NOTIFY_CODE1     0x80

/* FUNC interface - command values */
#define FUNC_RFKILL	0x1000
#define FUNC_LEDS	0x1001
#define FUNC_BUTTONS	0x1002
#define FUNC_BACKLIGHT  0x1004

/* FUNC interface - responses */
#define UNSUPPORTED_CMD 0x80000000

#if IS_ENABLED(CONFIG_LEDS_CLASS)
/* FUNC interface - LED control */
#define FUNC_LED_OFF	0x1
#define FUNC_LED_ON	0x30001
#define KEYBOARD_LAMPS	0x100
#define LOGOLAMP_POWERON 0x2000
#define LOGOLAMP_ALWAYS  0x4000
#define RADIO_LED_ON	0x20
#define ECO_LED	0x10000
#define ECO_LED_ON	0x80000
#endif

/* Hotkey details */
#define KEY1_CODE	0x410	/* codes for the keys in the GIRB register */
#define KEY2_CODE	0x411
#define KEY3_CODE	0x412
#define KEY4_CODE	0x413
#define KEY5_CODE	0x420

#define MAX_HOTKEY_RINGBUFFER_SIZE 100
#define RINGBUFFERSIZE 40

/* Debugging */
#define FUJLAPTOP_DBG_ERROR	  0x0001
#define FUJLAPTOP_DBG_WARN	  0x0002
#define FUJLAPTOP_DBG_INFO	  0x0004
#define FUJLAPTOP_DBG_TRACE	  0x0008

#ifdef CONFIG_FUJITSU_LAPTOP_DEBUG
#define vdbg_printk(a_dbg_level, format, arg...) \
	do { if (dbg_level & a_dbg_level) \
		printk(KERN_DEBUG pr_fmt("%s: " format), __func__, ## arg); \
	} while (0)
#else
#define vdbg_printk(a_dbg_level, format, arg...) \
	do { } while (0)
#endif

/* Device controlling the backlight and associated keys */
struct fujitsu_bl {
	acpi_handle acpi_handle;
	struct acpi_device *dev;
	struct input_dev *input;
	char phys[32];
	struct backlight_device *bl_device;
	struct platform_device *pf_device;
	int keycode1, keycode2, keycode3, keycode4, keycode5;

	unsigned int max_brightness;
	unsigned int brightness_changed;
	unsigned int brightness_level;
};

static struct fujitsu_bl *fujitsu_bl;
static int use_alt_lcd_levels = -1;
static int disable_brightness_adjust = -1;

/* Device used to access hotkeys and other features on the laptop */
struct fujitsu_laptop {
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
	int radio_led_registered;
	int eco_led_registered;
};

static struct fujitsu_laptop *fujitsu_laptop;

static void acpi_fujitsu_laptop_notify(struct acpi_device *device, u32 event);

#if IS_ENABLED(CONFIG_LEDS_CLASS)
static enum led_brightness logolamp_get(struct led_classdev *cdev);
static int logolamp_set(struct led_classdev *cdev,
			       enum led_brightness brightness);

static struct led_classdev logolamp_led = {
 .name = "fujitsu::logolamp",
 .brightness_get = logolamp_get,
 .brightness_set_blocking = logolamp_set
};

static enum led_brightness kblamps_get(struct led_classdev *cdev);
static int kblamps_set(struct led_classdev *cdev,
			       enum led_brightness brightness);

static struct led_classdev kblamps_led = {
 .name = "fujitsu::kblamps",
 .brightness_get = kblamps_get,
 .brightness_set_blocking = kblamps_set
};

static enum led_brightness radio_led_get(struct led_classdev *cdev);
static int radio_led_set(struct led_classdev *cdev,
			       enum led_brightness brightness);

static struct led_classdev radio_led = {
 .name = "fujitsu::radio_led",
 .default_trigger = "rfkill-any",
 .brightness_get = radio_led_get,
 .brightness_set_blocking = radio_led_set
};

static enum led_brightness eco_led_get(struct led_classdev *cdev);
static int eco_led_set(struct led_classdev *cdev,
			       enum led_brightness brightness);

static struct led_classdev eco_led = {
 .name = "fujitsu::eco_led",
 .brightness_get = eco_led_get,
 .brightness_set_blocking = eco_led_set
};
#endif

#ifdef CONFIG_FUJITSU_LAPTOP_DEBUG
static u32 dbg_level = 0x03;
#endif

static void acpi_fujitsu_bl_notify(struct acpi_device *device, u32 event);

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
	unsigned long long value;
	acpi_handle handle = NULL;

	status = acpi_get_handle(fujitsu_laptop->acpi_handle, "FUNC", &handle);
	if (ACPI_FAILURE(status)) {
		vdbg_printk(FUJLAPTOP_DBG_ERROR,
				"FUNC interface is not present\n");
		return -ENODEV;
	}

	params[0].integer.value = cmd;
	params[1].integer.value = arg0;
	params[2].integer.value = arg1;
	params[3].integer.value = arg2;

	status = acpi_evaluate_integer(handle, NULL, &arg_list, &value);
	if (ACPI_FAILURE(status)) {
		vdbg_printk(FUJLAPTOP_DBG_WARN,
			"FUNC 0x%x (args 0x%x, 0x%x, 0x%x) call failed\n",
				cmd, arg0, arg1, arg2);
		return -ENODEV;
	}

	vdbg_printk(FUJLAPTOP_DBG_TRACE,
		"FUNC 0x%x (args 0x%x, 0x%x, 0x%x) returned 0x%x\n",
			cmd, arg0, arg1, arg2, (int)value);
	return value;
}

#if IS_ENABLED(CONFIG_LEDS_CLASS)
/* LED class callbacks */

static int logolamp_set(struct led_classdev *cdev,
			       enum led_brightness brightness)
{
	int poweron = FUNC_LED_ON, always = FUNC_LED_ON;
	int ret;

	if (brightness < LED_HALF)
		poweron = FUNC_LED_OFF;

	if (brightness < LED_FULL)
		always = FUNC_LED_OFF;

	ret = call_fext_func(FUNC_LEDS, 0x1, LOGOLAMP_POWERON, poweron);
	if (ret < 0)
		return ret;

	return call_fext_func(FUNC_LEDS, 0x1, LOGOLAMP_ALWAYS, always);
}

static int kblamps_set(struct led_classdev *cdev,
			       enum led_brightness brightness)
{
	if (brightness >= LED_FULL)
		return call_fext_func(FUNC_LEDS, 0x1, KEYBOARD_LAMPS, FUNC_LED_ON);
	else
		return call_fext_func(FUNC_LEDS, 0x1, KEYBOARD_LAMPS, FUNC_LED_OFF);
}

static int radio_led_set(struct led_classdev *cdev,
				enum led_brightness brightness)
{
	if (brightness >= LED_FULL)
		return call_fext_func(FUNC_RFKILL, 0x5, RADIO_LED_ON, RADIO_LED_ON);
	else
		return call_fext_func(FUNC_RFKILL, 0x5, RADIO_LED_ON, 0x0);
}

static int eco_led_set(struct led_classdev *cdev,
				enum led_brightness brightness)
{
	int curr;

	curr = call_fext_func(FUNC_LEDS, 0x2, ECO_LED, 0x0);
	if (brightness >= LED_FULL)
		return call_fext_func(FUNC_LEDS, 0x1, ECO_LED, curr | ECO_LED_ON);
	else
		return call_fext_func(FUNC_LEDS, 0x1, ECO_LED, curr & ~ECO_LED_ON);
}

static enum led_brightness logolamp_get(struct led_classdev *cdev)
{
	int ret;

	ret = call_fext_func(FUNC_LEDS, 0x2, LOGOLAMP_ALWAYS, 0x0);
	if (ret == FUNC_LED_ON)
		return LED_FULL;

	ret = call_fext_func(FUNC_LEDS, 0x2, LOGOLAMP_POWERON, 0x0);
	if (ret == FUNC_LED_ON)
		return LED_HALF;

	return LED_OFF;
}

static enum led_brightness kblamps_get(struct led_classdev *cdev)
{
	enum led_brightness brightness = LED_OFF;

	if (call_fext_func(FUNC_LEDS, 0x2, KEYBOARD_LAMPS, 0x0) == FUNC_LED_ON)
		brightness = LED_FULL;

	return brightness;
}

static enum led_brightness radio_led_get(struct led_classdev *cdev)
{
	enum led_brightness brightness = LED_OFF;

	if (call_fext_func(FUNC_RFKILL, 0x4, 0x0, 0x0) & RADIO_LED_ON)
		brightness = LED_FULL;

	return brightness;
}

static enum led_brightness eco_led_get(struct led_classdev *cdev)
{
	enum led_brightness brightness = LED_OFF;

	if (call_fext_func(FUNC_LEDS, 0x2, ECO_LED, 0x0) & ECO_LED_ON)
		brightness = LED_FULL;

	return brightness;
}
#endif

/* Hardware access for LCD brightness control */

static int set_lcd_level(int level)
{
	acpi_status status = AE_OK;
	acpi_handle handle = NULL;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "set lcd level via SBLL [%d]\n",
		    level);

	if (level < 0 || level >= fujitsu_bl->max_brightness)
		return -EINVAL;

	status = acpi_get_handle(fujitsu_bl->acpi_handle, "SBLL", &handle);
	if (ACPI_FAILURE(status)) {
		vdbg_printk(FUJLAPTOP_DBG_ERROR, "SBLL not present\n");
		return -ENODEV;
	}


	status = acpi_execute_simple_method(handle, NULL, level);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return 0;
}

static int set_lcd_level_alt(int level)
{
	acpi_status status = AE_OK;
	acpi_handle handle = NULL;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "set lcd level via SBL2 [%d]\n",
		    level);

	if (level < 0 || level >= fujitsu_bl->max_brightness)
		return -EINVAL;

	status = acpi_get_handle(fujitsu_bl->acpi_handle, "SBL2", &handle);
	if (ACPI_FAILURE(status)) {
		vdbg_printk(FUJLAPTOP_DBG_ERROR, "SBL2 not present\n");
		return -ENODEV;
	}

	status = acpi_execute_simple_method(handle, NULL, level);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return 0;
}

static int get_lcd_level(void)
{
	unsigned long long state = 0;
	acpi_status status = AE_OK;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "get lcd level via GBLL\n");

	status = acpi_evaluate_integer(fujitsu_bl->acpi_handle, "GBLL", NULL,
				       &state);
	if (ACPI_FAILURE(status))
		return 0;

	fujitsu_bl->brightness_level = state & 0x0fffffff;

	if (state & 0x80000000)
		fujitsu_bl->brightness_changed = 1;
	else
		fujitsu_bl->brightness_changed = 0;

	return fujitsu_bl->brightness_level;
}

static int get_max_brightness(void)
{
	unsigned long long state = 0;
	acpi_status status = AE_OK;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "get max lcd level via RBLL\n");

	status = acpi_evaluate_integer(fujitsu_bl->acpi_handle, "RBLL", NULL,
				       &state);
	if (ACPI_FAILURE(status))
		return -1;

	fujitsu_bl->max_brightness = state;

	return fujitsu_bl->max_brightness;
}

/* Backlight device stuff */

static int bl_get_brightness(struct backlight_device *b)
{
	return get_lcd_level();
}

static int bl_update_status(struct backlight_device *b)
{
	int ret;
	if (b->props.power == FB_BLANK_POWERDOWN)
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

static const struct backlight_ops fujitsu_bl_ops = {
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

	ret = fujitsu_bl->brightness_changed;
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
	    || (level < 0 || level >= fujitsu_bl->max_brightness))
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
	if (!(fujitsu_laptop->rfkill_supported & 0x100))
		return sprintf(buf, "unknown\n");
	if (fujitsu_laptop->rfkill_state & 0x100)
		return sprintf(buf, "open\n");
	else
		return sprintf(buf, "closed\n");
}

static ssize_t
show_dock_state(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	if (!(fujitsu_laptop->rfkill_supported & 0x200))
		return sprintf(buf, "unknown\n");
	if (fujitsu_laptop->rfkill_state & 0x200)
		return sprintf(buf, "docked\n");
	else
		return sprintf(buf, "undocked\n");
}

static ssize_t
show_radios_state(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	if (!(fujitsu_laptop->rfkill_supported & 0x20))
		return sprintf(buf, "unknown\n");
	if (fujitsu_laptop->rfkill_state & 0x20)
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
		   }
};

static void __init dmi_check_cb_common(const struct dmi_system_id *id)
{
	pr_info("Identified laptop model '%s'\n", id->ident);
	if (use_alt_lcd_levels == -1) {
		if (acpi_has_method(NULL,
				"\\_SB.PCI0.LPCB.FJEX.SBL2"))
			use_alt_lcd_levels = 1;
		else
			use_alt_lcd_levels = 0;
		vdbg_printk(FUJLAPTOP_DBG_TRACE, "auto-detected usealt as "
			"%i\n", use_alt_lcd_levels);
	}
}

static int __init dmi_check_cb_s6410(const struct dmi_system_id *id)
{
	dmi_check_cb_common(id);
	fujitsu_bl->keycode1 = KEY_SCREENLOCK;	/* "Lock" */
	fujitsu_bl->keycode2 = KEY_HELP;	/* "Mobility Center" */
	return 1;
}

static int __init dmi_check_cb_s6420(const struct dmi_system_id *id)
{
	dmi_check_cb_common(id);
	fujitsu_bl->keycode1 = KEY_SCREENLOCK;	/* "Lock" */
	fujitsu_bl->keycode2 = KEY_HELP;	/* "Mobility Center" */
	return 1;
}

static int __init dmi_check_cb_p8010(const struct dmi_system_id *id)
{
	dmi_check_cb_common(id);
	fujitsu_bl->keycode1 = KEY_HELP;		/* "Support" */
	fujitsu_bl->keycode3 = KEY_SWITCHVIDEOMODE;	/* "Presentation" */
	fujitsu_bl->keycode4 = KEY_WWW;			/* "Internet" */
	return 1;
}

static const struct dmi_system_id fujitsu_dmi_table[] __initconst = {
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

static int acpi_fujitsu_bl_add(struct acpi_device *device)
{
	int state = 0;
	struct input_dev *input;
	int error;

	if (!device)
		return -EINVAL;

	fujitsu_bl->acpi_handle = device->handle;
	sprintf(acpi_device_name(device), "%s", ACPI_FUJITSU_BL_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_FUJITSU_CLASS);
	device->driver_data = fujitsu_bl;

	fujitsu_bl->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_stop;
	}

	snprintf(fujitsu_bl->phys, sizeof(fujitsu_bl->phys),
		 "%s/video/input0", acpi_device_hid(device));

	input->name = acpi_device_name(device);
	input->phys = fujitsu_bl->phys;
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

	error = acpi_bus_update_power(fujitsu_bl->acpi_handle, &state);
	if (error) {
		pr_err("Error reading power state\n");
		goto err_unregister_input_dev;
	}

	pr_info("ACPI: %s [%s] (%s)\n",
	       acpi_device_name(device), acpi_device_bid(device),
	       !device->power.state ? "on" : "off");

	fujitsu_bl->dev = device;

	if (acpi_has_method(device->handle, METHOD_NAME__INI)) {
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
		fujitsu_bl->max_brightness = FUJITSU_LCD_N_LEVELS;
	get_lcd_level();

	return 0;

err_unregister_input_dev:
	input_unregister_device(input);
	input = NULL;
err_free_input_dev:
	input_free_device(input);
err_stop:
	return error;
}

static int acpi_fujitsu_bl_remove(struct acpi_device *device)
{
	struct fujitsu_bl *fujitsu_bl = acpi_driver_data(device);
	struct input_dev *input = fujitsu_bl->input;

	input_unregister_device(input);

	fujitsu_bl->acpi_handle = NULL;

	return 0;
}

/* Brightness notify */

static void acpi_fujitsu_bl_notify(struct acpi_device *device, u32 event)
{
	struct input_dev *input;
	int keycode;
	int oldb, newb;

	input = fujitsu_bl->input;

	switch (event) {
	case ACPI_FUJITSU_NOTIFY_CODE1:
		keycode = 0;
		oldb = fujitsu_bl->brightness_level;
		get_lcd_level();
		newb = fujitsu_bl->brightness_level;

		vdbg_printk(FUJLAPTOP_DBG_TRACE,
			    "brightness button event [%i -> %i (%i)]\n",
			    oldb, newb, fujitsu_bl->brightness_changed);

		if (oldb < newb) {
			if (disable_brightness_adjust != 1) {
				if (use_alt_lcd_levels)
					set_lcd_level_alt(newb);
				else
					set_lcd_level(newb);
			}
			keycode = KEY_BRIGHTNESSUP;
		} else if (oldb > newb) {
			if (disable_brightness_adjust != 1) {
				if (use_alt_lcd_levels)
					set_lcd_level_alt(newb);
				else
					set_lcd_level(newb);
			}
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

static int acpi_fujitsu_laptop_add(struct acpi_device *device)
{
	int result = 0;
	int state = 0;
	struct input_dev *input;
	int error;
	int i;

	if (!device)
		return -EINVAL;

	fujitsu_laptop->acpi_handle = device->handle;
	sprintf(acpi_device_name(device), "%s",
		ACPI_FUJITSU_LAPTOP_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_FUJITSU_CLASS);
	device->driver_data = fujitsu_laptop;

	/* kfifo */
	spin_lock_init(&fujitsu_laptop->fifo_lock);
	error = kfifo_alloc(&fujitsu_laptop->fifo, RINGBUFFERSIZE * sizeof(int),
			GFP_KERNEL);
	if (error) {
		pr_err("kfifo_alloc failed\n");
		goto err_stop;
	}

	fujitsu_laptop->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_free_fifo;
	}

	snprintf(fujitsu_laptop->phys, sizeof(fujitsu_laptop->phys),
		 "%s/video/input0", acpi_device_hid(device));

	input->name = acpi_device_name(device);
	input->phys = fujitsu_laptop->phys;
	input->id.bustype = BUS_HOST;
	input->id.product = 0x06;
	input->dev.parent = &device->dev;

	set_bit(EV_KEY, input->evbit);
	set_bit(fujitsu_bl->keycode1, input->keybit);
	set_bit(fujitsu_bl->keycode2, input->keybit);
	set_bit(fujitsu_bl->keycode3, input->keybit);
	set_bit(fujitsu_bl->keycode4, input->keybit);
	set_bit(fujitsu_bl->keycode5, input->keybit);
	set_bit(KEY_TOUCHPAD_TOGGLE, input->keybit);
	set_bit(KEY_UNKNOWN, input->keybit);

	error = input_register_device(input);
	if (error)
		goto err_free_input_dev;

	error = acpi_bus_update_power(fujitsu_laptop->acpi_handle, &state);
	if (error) {
		pr_err("Error reading power state\n");
		goto err_unregister_input_dev;
	}

	pr_info("ACPI: %s [%s] (%s)\n",
		acpi_device_name(device), acpi_device_bid(device),
		!device->power.state ? "on" : "off");

	fujitsu_laptop->dev = device;

	if (acpi_has_method(device->handle, METHOD_NAME__INI)) {
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

	fujitsu_laptop->rfkill_supported =
		call_fext_func(FUNC_RFKILL, 0x0, 0x0, 0x0);

	/* Make sure our bitmask of supported functions is cleared if the
	   RFKILL function block is not implemented, like on the S7020. */
	if (fujitsu_laptop->rfkill_supported == UNSUPPORTED_CMD)
		fujitsu_laptop->rfkill_supported = 0;

	if (fujitsu_laptop->rfkill_supported)
		fujitsu_laptop->rfkill_state =
			call_fext_func(FUNC_RFKILL, 0x4, 0x0, 0x0);

	/* Suspect this is a keymap of the application panel, print it */
	pr_info("BTNI: [0x%x]\n", call_fext_func(FUNC_BUTTONS, 0x0, 0x0, 0x0));

#if IS_ENABLED(CONFIG_LEDS_CLASS)
	if (call_fext_func(FUNC_LEDS, 0x0, 0x0, 0x0) & LOGOLAMP_POWERON) {
		result = led_classdev_register(&fujitsu_bl->pf_device->dev,
						&logolamp_led);
		if (result == 0) {
			fujitsu_laptop->logolamp_registered = 1;
		} else {
			pr_err("Could not register LED handler for logo lamp, error %i\n",
			       result);
		}
	}

	if ((call_fext_func(FUNC_LEDS, 0x0, 0x0, 0x0) & KEYBOARD_LAMPS) &&
	   (call_fext_func(FUNC_BUTTONS, 0x0, 0x0, 0x0) == 0x0)) {
		result = led_classdev_register(&fujitsu_bl->pf_device->dev,
						&kblamps_led);
		if (result == 0) {
			fujitsu_laptop->kblamps_registered = 1;
		} else {
			pr_err("Could not register LED handler for keyboard lamps, error %i\n",
			       result);
		}
	}

	/*
	 * BTNI bit 24 seems to indicate the presence of a radio toggle
	 * button in place of a slide switch, and all such machines appear
	 * to also have an RF LED.  Therefore use bit 24 as an indicator
	 * that an RF LED is present.
	 */
	if (call_fext_func(FUNC_BUTTONS, 0x0, 0x0, 0x0) & BIT(24)) {
		result = led_classdev_register(&fujitsu_bl->pf_device->dev,
						&radio_led);
		if (result == 0) {
			fujitsu_laptop->radio_led_registered = 1;
		} else {
			pr_err("Could not register LED handler for radio LED, error %i\n",
			       result);
		}
	}

	/* Support for eco led is not always signaled in bit corresponding
	 * to the bit used to control the led. According to the DSDT table,
	 * bit 14 seems to indicate presence of said led as well.
	 * Confirm by testing the status.
	*/
	if ((call_fext_func(FUNC_LEDS, 0x0, 0x0, 0x0) & BIT(14)) &&
	   (call_fext_func(FUNC_LEDS, 0x2, ECO_LED, 0x0) != UNSUPPORTED_CMD)) {
		result = led_classdev_register(&fujitsu_bl->pf_device->dev,
						&eco_led);
		if (result == 0) {
			fujitsu_laptop->eco_led_registered = 1;
		} else {
			pr_err("Could not register LED handler for eco LED, error %i\n",
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
	kfifo_free(&fujitsu_laptop->fifo);
err_stop:
	return error;
}

static int acpi_fujitsu_laptop_remove(struct acpi_device *device)
{
	struct fujitsu_laptop *fujitsu_laptop = acpi_driver_data(device);
	struct input_dev *input = fujitsu_laptop->input;

#if IS_ENABLED(CONFIG_LEDS_CLASS)
	if (fujitsu_laptop->logolamp_registered)
		led_classdev_unregister(&logolamp_led);

	if (fujitsu_laptop->kblamps_registered)
		led_classdev_unregister(&kblamps_led);

	if (fujitsu_laptop->radio_led_registered)
		led_classdev_unregister(&radio_led);

	if (fujitsu_laptop->eco_led_registered)
		led_classdev_unregister(&eco_led);
#endif

	input_unregister_device(input);

	kfifo_free(&fujitsu_laptop->fifo);

	fujitsu_laptop->acpi_handle = NULL;

	return 0;
}

static void acpi_fujitsu_laptop_press(int keycode)
{
	struct input_dev *input = fujitsu_laptop->input;
	int status;

	status = kfifo_in_locked(&fujitsu_laptop->fifo,
				 (unsigned char *)&keycode, sizeof(keycode),
				 &fujitsu_laptop->fifo_lock);
	if (status != sizeof(keycode)) {
		vdbg_printk(FUJLAPTOP_DBG_WARN,
			    "Could not push keycode [0x%x]\n", keycode);
		return;
	}
	input_report_key(input, keycode, 1);
	input_sync(input);
	vdbg_printk(FUJLAPTOP_DBG_TRACE,
		    "Push keycode into ringbuffer [%d]\n", keycode);
}

static void acpi_fujitsu_laptop_release(void)
{
	struct input_dev *input = fujitsu_laptop->input;
	int keycode, status;

	while (true) {
		status = kfifo_out_locked(&fujitsu_laptop->fifo,
					  (unsigned char *)&keycode,
					  sizeof(keycode),
					  &fujitsu_laptop->fifo_lock);
		if (status != sizeof(keycode))
			return;
		input_report_key(input, keycode, 0);
		input_sync(input);
		vdbg_printk(FUJLAPTOP_DBG_TRACE,
			    "Pop keycode from ringbuffer [%d]\n", keycode);
	}
}

static void acpi_fujitsu_laptop_notify(struct acpi_device *device, u32 event)
{
	struct input_dev *input;
	int keycode;
	unsigned int irb = 1;
	int i;

	input = fujitsu_laptop->input;

	if (event != ACPI_FUJITSU_NOTIFY_CODE1) {
		keycode = KEY_UNKNOWN;
		vdbg_printk(FUJLAPTOP_DBG_WARN,
			    "Unsupported event [0x%x]\n", event);
		input_report_key(input, keycode, 1);
		input_sync(input);
		input_report_key(input, keycode, 0);
		input_sync(input);
		return;
	}

	if (fujitsu_laptop->rfkill_supported)
		fujitsu_laptop->rfkill_state =
			call_fext_func(FUNC_RFKILL, 0x4, 0x0, 0x0);

	i = 0;
	while ((irb =
		call_fext_func(FUNC_BUTTONS, 0x1, 0x0, 0x0)) != 0
			&& (i++) < MAX_HOTKEY_RINGBUFFER_SIZE) {
		switch (irb & 0x4ff) {
		case KEY1_CODE:
			keycode = fujitsu_bl->keycode1;
			break;
		case KEY2_CODE:
			keycode = fujitsu_bl->keycode2;
			break;
		case KEY3_CODE:
			keycode = fujitsu_bl->keycode3;
			break;
		case KEY4_CODE:
			keycode = fujitsu_bl->keycode4;
			break;
		case KEY5_CODE:
			keycode = fujitsu_bl->keycode5;
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

		if (keycode > 0)
			acpi_fujitsu_laptop_press(keycode);
		else if (keycode == 0)
			acpi_fujitsu_laptop_release();
	}

	/* On some models (first seen on the Skylake-based Lifebook
	 * E736/E746/E756), the touchpad toggle hotkey (Fn+F4) is
	 * handled in software; its state is queried using FUNC_RFKILL
	 */
	if ((fujitsu_laptop->rfkill_supported & BIT(26)) &&
	    (call_fext_func(FUNC_RFKILL, 0x1, 0x0, 0x0) & BIT(26))) {
		keycode = KEY_TOUCHPAD_TOGGLE;
		input_report_key(input, keycode, 1);
		input_sync(input);
		input_report_key(input, keycode, 0);
		input_sync(input);
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
		.remove = acpi_fujitsu_bl_remove,
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
	int ret, result, max_brightness;

	if (acpi_disabled)
		return -ENODEV;

	fujitsu_bl = kzalloc(sizeof(struct fujitsu_bl), GFP_KERNEL);
	if (!fujitsu_bl)
		return -ENOMEM;
	fujitsu_bl->keycode1 = KEY_PROG1;
	fujitsu_bl->keycode2 = KEY_PROG2;
	fujitsu_bl->keycode3 = KEY_PROG3;
	fujitsu_bl->keycode4 = KEY_PROG4;
	fujitsu_bl->keycode5 = KEY_RFKILL;
	dmi_check_system(fujitsu_dmi_table);

	result = acpi_bus_register_driver(&acpi_fujitsu_bl_driver);
	if (result < 0) {
		ret = -ENODEV;
		goto fail_acpi;
	}

	/* Register platform stuff */

	fujitsu_bl->pf_device = platform_device_alloc("fujitsu-laptop", -1);
	if (!fujitsu_bl->pf_device) {
		ret = -ENOMEM;
		goto fail_platform_driver;
	}

	ret = platform_device_add(fujitsu_bl->pf_device);
	if (ret)
		goto fail_platform_device1;

	ret =
	    sysfs_create_group(&fujitsu_bl->pf_device->dev.kobj,
			       &fujitsupf_attribute_group);
	if (ret)
		goto fail_platform_device2;

	/* Register backlight stuff */

	if (acpi_video_get_backlight_type() == acpi_backlight_vendor) {
		struct backlight_properties props;

		memset(&props, 0, sizeof(struct backlight_properties));
		max_brightness = fujitsu_bl->max_brightness;
		props.type = BACKLIGHT_PLATFORM;
		props.max_brightness = max_brightness - 1;
		fujitsu_bl->bl_device = backlight_device_register("fujitsu-laptop",
								  NULL, NULL,
								  &fujitsu_bl_ops,
								  &props);
		if (IS_ERR(fujitsu_bl->bl_device)) {
			ret = PTR_ERR(fujitsu_bl->bl_device);
			fujitsu_bl->bl_device = NULL;
			goto fail_sysfs_group;
		}
		fujitsu_bl->bl_device->props.brightness = fujitsu_bl->brightness_level;
	}

	ret = platform_driver_register(&fujitsupf_driver);
	if (ret)
		goto fail_backlight;

	/* Register laptop driver */

	fujitsu_laptop = kzalloc(sizeof(struct fujitsu_laptop), GFP_KERNEL);
	if (!fujitsu_laptop) {
		ret = -ENOMEM;
		goto fail_laptop;
	}

	result = acpi_bus_register_driver(&acpi_fujitsu_laptop_driver);
	if (result < 0) {
		ret = -ENODEV;
		goto fail_laptop1;
	}

	/* Sync backlight power status (needs FUJ02E3 device, hence deferred) */
	if (acpi_video_get_backlight_type() == acpi_backlight_vendor) {
		if (call_fext_func(FUNC_BACKLIGHT, 0x2, 0x4, 0x0) == 3)
			fujitsu_bl->bl_device->props.power = FB_BLANK_POWERDOWN;
		else
			fujitsu_bl->bl_device->props.power = FB_BLANK_UNBLANK;
	}

	pr_info("driver " FUJITSU_DRIVER_VERSION " successfully loaded\n");

	return 0;

fail_laptop1:
	kfree(fujitsu_laptop);
fail_laptop:
	platform_driver_unregister(&fujitsupf_driver);
fail_backlight:
	backlight_device_unregister(fujitsu_bl->bl_device);
fail_sysfs_group:
	sysfs_remove_group(&fujitsu_bl->pf_device->dev.kobj,
			   &fujitsupf_attribute_group);
fail_platform_device2:
	platform_device_del(fujitsu_bl->pf_device);
fail_platform_device1:
	platform_device_put(fujitsu_bl->pf_device);
fail_platform_driver:
	acpi_bus_unregister_driver(&acpi_fujitsu_bl_driver);
fail_acpi:
	kfree(fujitsu_bl);

	return ret;
}

static void __exit fujitsu_cleanup(void)
{
	acpi_bus_unregister_driver(&acpi_fujitsu_laptop_driver);

	kfree(fujitsu_laptop);

	platform_driver_unregister(&fujitsupf_driver);

	backlight_device_unregister(fujitsu_bl->bl_device);

	sysfs_remove_group(&fujitsu_bl->pf_device->dev.kobj,
			   &fujitsupf_attribute_group);

	platform_device_unregister(fujitsu_bl->pf_device);

	acpi_bus_unregister_driver(&acpi_fujitsu_bl_driver);

	kfree(fujitsu_bl);

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
