/*-*-linux-c-*-*/

/*
  Copyright (C) 2007,2008 Jonathan Woithe <jwoithe@physics.adelaide.edu.au>
  Copyright (C) 2008 Peter Gruber <nokos@gmx.net>
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
 * This driver has been tested on a Fujitsu Lifebook S6410 and S7020.  It
 * should work on most P-series and S-series Lifebooks, but YMMV.
 *
 * The module parameter use_alt_lcd_levels switches between different ACPI
 * brightness controls which are used by different Fujitsu laptops.  In most
 * cases the correct method is automatically detected. "use_alt_lcd_levels=1"
 * is applicable for a Fujitsu Lifebook S6410 if autodetection fails.
 *
 */

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

#define FUJITSU_DRIVER_VERSION "0.4.2"

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

/* Hotkey details */
#define LOCK_KEY	0x410	/* codes for the keys in the GIRB register */
#define DISPLAY_KEY	0x411	/* keys are mapped to KEY_SCREENLOCK (the key with the key symbol) */
#define ENERGY_KEY	0x412	/* KEY_MEDIA (the key with the laptop symbol, KEY_EMAIL (E key)) */
#define REST_KEY	0x413	/* KEY_SUSPEND (R key) */

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

	unsigned int max_brightness;
	unsigned int brightness_changed;
	unsigned int brightness_level;
};

static struct fujitsu_t *fujitsu;
static int use_alt_lcd_levels = -1;
static int disable_brightness_keys = -1;
static int disable_brightness_adjust = -1;

/* Device used to access other hotkeys on the laptop */
struct fujitsu_hotkey_t {
	acpi_handle acpi_handle;
	struct acpi_device *dev;
	struct input_dev *input;
	char phys[32];
	struct platform_device *pf_device;
	struct kfifo *fifo;
	spinlock_t fifo_lock;

	unsigned int irb;	/* info about the pressed buttons */
};

static struct fujitsu_hotkey_t *fujitsu_hotkey;

static void acpi_fujitsu_hotkey_notify(acpi_handle handle, u32 event,
				       void *data);

#ifdef CONFIG_FUJITSU_LAPTOP_DEBUG
static u32 dbg_level = 0x03;
#endif

static void acpi_fujitsu_notify(acpi_handle handle, u32 event, void *data);

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

	if (!fujitsu)
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

	if (!fujitsu)
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
	unsigned long state = 0;
	acpi_status status = AE_OK;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "get lcd level via GBLL\n");

	status =
	    acpi_evaluate_integer(fujitsu->acpi_handle, "GBLL", NULL, &state);
	if (status < 0)
		return status;

	fujitsu->brightness_level = state & 0x0fffffff;

	if (state & 0x80000000)
		fujitsu->brightness_changed = 1;
	else
		fujitsu->brightness_changed = 0;

	return fujitsu->brightness_level;
}

static int get_max_brightness(void)
{
	unsigned long state = 0;
	acpi_status status = AE_OK;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "get max lcd level via RBLL\n");

	status =
	    acpi_evaluate_integer(fujitsu->acpi_handle, "RBLL", NULL, &state);
	if (status < 0)
		return status;

	fujitsu->max_brightness = state;

	return fujitsu->max_brightness;
}

static int get_lcd_level_alt(void)
{
	unsigned long state = 0;
	acpi_status status = AE_OK;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "get lcd level via GBLS\n");

	status =
	    acpi_evaluate_integer(fujitsu->acpi_handle, "GBLS", NULL, &state);
	if (status < 0)
		return status;

	fujitsu->brightness_level = state & 0x0fffffff;

	if (state & 0x80000000)
		fujitsu->brightness_changed = 1;
	else
		fujitsu->brightness_changed = 0;

	return fujitsu->brightness_level;
}

/* Backlight device stuff */

static int bl_get_brightness(struct backlight_device *b)
{
	if (use_alt_lcd_levels)
		return get_lcd_level_alt();
	else
		return get_lcd_level();
}

static int bl_update_status(struct backlight_device *b)
{
	if (use_alt_lcd_levels)
		return set_lcd_level_alt(b->props.brightness);
	else
		return set_lcd_level(b->props.brightness);
}

static struct backlight_ops fujitsubl_ops = {
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

	if (use_alt_lcd_levels)
		ret = get_lcd_level_alt();
	else
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

	if (use_alt_lcd_levels)
		ret = get_lcd_level_alt();
	else
		ret = get_lcd_level();
	if (ret < 0)
		return ret;

	return count;
}

/* Hardware access for hotkey device */

static int get_irb(void)
{
	unsigned long state = 0;
	acpi_status status = AE_OK;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "Get irb\n");

	status =
	    acpi_evaluate_integer(fujitsu_hotkey->acpi_handle, "GIRB", NULL,
				  &state);
	if (status < 0)
		return status;

	fujitsu_hotkey->irb = state;

	return fujitsu_hotkey->irb;
}

static ssize_t
ignore_store(struct device *dev,
	     struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(max_brightness, 0444, show_max_brightness, ignore_store);
static DEVICE_ATTR(brightness_changed, 0444, show_brightness_changed,
		   ignore_store);
static DEVICE_ATTR(lcd_level, 0644, show_lcd_level, store_lcd_level);

static struct attribute *fujitsupf_attributes[] = {
	&dev_attr_brightness_changed.attr,
	&dev_attr_max_brightness.attr,
	&dev_attr_lcd_level.attr,
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

static int dmi_check_cb_s6410(const struct dmi_system_id *id)
{
	acpi_handle handle;
	int have_blnf;
	printk(KERN_INFO "fujitsu-laptop: Identified laptop model '%s'.\n",
	       id->ident);
	have_blnf = ACPI_SUCCESS
	    (acpi_get_handle(NULL, "\\_SB.PCI0.GFX0.LCD.BLNF", &handle));
	if (use_alt_lcd_levels == -1) {
		vdbg_printk(FUJLAPTOP_DBG_TRACE, "auto-detecting usealt\n");
		use_alt_lcd_levels = 1;
	}
	if (disable_brightness_keys == -1) {
		vdbg_printk(FUJLAPTOP_DBG_TRACE,
			    "auto-detecting disable_keys\n");
		disable_brightness_keys = have_blnf ? 1 : 0;
	}
	if (disable_brightness_adjust == -1) {
		vdbg_printk(FUJLAPTOP_DBG_TRACE,
			    "auto-detecting disable_adjust\n");
		disable_brightness_adjust = have_blnf ? 0 : 1;
	}
	return 0;
}

static struct dmi_system_id __initdata fujitsu_dmi_table[] = {
	{
	 .ident = "Fujitsu Siemens",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK S6410"),
		     },
	 .callback = dmi_check_cb_s6410},
	{
	 .ident = "FUJITSU LifeBook P8010",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook P8010"),
		    },
	 .callback = dmi_check_cb_s6410},
	{}
};

/* ACPI device for LCD brightness control */

static int acpi_fujitsu_add(struct acpi_device *device)
{
	acpi_status status;
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

	status = acpi_install_notify_handler(device->handle,
					     ACPI_DEVICE_NOTIFY,
					     acpi_fujitsu_notify, fujitsu);

	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR "Error installing notify handler\n");
		error = -ENODEV;
		goto err_stop;
	}

	fujitsu->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_uninstall_notify;
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

	result = acpi_bus_get_power(fujitsu->acpi_handle, &state);
	if (result) {
		printk(KERN_ERR "Error reading power state\n");
		goto end;
	}

	printk(KERN_INFO PREFIX "%s [%s] (%s)\n",
	       acpi_device_name(device), acpi_device_bid(device),
	       !device->power.state ? "on" : "off");

	fujitsu->dev = device;

	if (ACPI_SUCCESS
	    (acpi_get_handle(device->handle, METHOD_NAME__INI, &handle))) {
		vdbg_printk(FUJLAPTOP_DBG_INFO, "Invoking _INI\n");
		if (ACPI_FAILURE
		    (acpi_evaluate_object
		     (device->handle, METHOD_NAME__INI, NULL, NULL)))
			printk(KERN_ERR "_INI Method failed\n");
	}

	/* do config (detect defaults) */
	dmi_check_system(fujitsu_dmi_table);
	use_alt_lcd_levels = use_alt_lcd_levels == 1 ? 1 : 0;
	disable_brightness_keys = disable_brightness_keys == 1 ? 1 : 0;
	disable_brightness_adjust = disable_brightness_adjust == 1 ? 1 : 0;
	vdbg_printk(FUJLAPTOP_DBG_INFO,
		    "config: [alt interface: %d], [key disable: %d], [adjust disable: %d]\n",
		    use_alt_lcd_levels, disable_brightness_keys,
		    disable_brightness_adjust);

	if (get_max_brightness() <= 0)
		fujitsu->max_brightness = FUJITSU_LCD_N_LEVELS;
	if (use_alt_lcd_levels)
		get_lcd_level_alt();
	else
		get_lcd_level();

	return result;

end:
err_free_input_dev:
	input_free_device(input);
err_uninstall_notify:
	acpi_remove_notify_handler(device->handle, ACPI_DEVICE_NOTIFY,
				   acpi_fujitsu_notify);
err_stop:

	return result;
}

static int acpi_fujitsu_remove(struct acpi_device *device, int type)
{
	acpi_status status;
	struct fujitsu_t *fujitsu = NULL;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	fujitsu = acpi_driver_data(device);

	status = acpi_remove_notify_handler(fujitsu->acpi_handle,
					    ACPI_DEVICE_NOTIFY,
					    acpi_fujitsu_notify);

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	fujitsu->acpi_handle = NULL;

	return 0;
}

/* Brightness notify */

static void acpi_fujitsu_notify(acpi_handle handle, u32 event, void *data)
{
	struct input_dev *input;
	int keycode;
	int oldb, newb;

	input = fujitsu->input;

	switch (event) {
	case ACPI_FUJITSU_NOTIFY_CODE1:
		keycode = 0;
		oldb = fujitsu->brightness_level;
		get_lcd_level();  /* the alt version always yields changed */
		newb = fujitsu->brightness_level;

		vdbg_printk(FUJLAPTOP_DBG_TRACE,
			    "brightness button event [%i -> %i (%i)]\n",
			    oldb, newb, fujitsu->brightness_changed);

		if (oldb == newb && fujitsu->brightness_changed) {
			keycode = 0;
			if (disable_brightness_keys != 1) {
				if (oldb == 0) {
					acpi_bus_generate_proc_event(fujitsu->
						dev,
						ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS,
						0);
					keycode = KEY_BRIGHTNESSDOWN;
				} else if (oldb ==
					   (fujitsu->max_brightness) - 1) {
					acpi_bus_generate_proc_event(fujitsu->
						dev,
						ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS,
						0);
					keycode = KEY_BRIGHTNESSUP;
				}
			}
		} else if (oldb < newb) {
			if (disable_brightness_adjust != 1) {
				if (use_alt_lcd_levels)
					set_lcd_level_alt(newb);
				else
					set_lcd_level(newb);
			}
			if (disable_brightness_keys != 1) {
				acpi_bus_generate_proc_event(fujitsu->dev,
					ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS,
					0);
				keycode = KEY_BRIGHTNESSUP;
			}
		} else if (oldb > newb) {
			if (disable_brightness_adjust != 1) {
				if (use_alt_lcd_levels)
					set_lcd_level_alt(newb);
				else
					set_lcd_level(newb);
			}
			if (disable_brightness_keys != 1) {
				acpi_bus_generate_proc_event(fujitsu->dev,
					ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS,
					0);
				keycode = KEY_BRIGHTNESSDOWN;
			}
		} else {
			keycode = KEY_UNKNOWN;
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

	return;
}

/* ACPI device for hotkey handling */

static int acpi_fujitsu_hotkey_add(struct acpi_device *device)
{
	acpi_status status;
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

	status = acpi_install_notify_handler(device->handle,
					     ACPI_DEVICE_NOTIFY,
					     acpi_fujitsu_hotkey_notify,
					     fujitsu_hotkey);

	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR "Error installing notify handler\n");
		error = -ENODEV;
		goto err_stop;
	}

	/* kfifo */
	spin_lock_init(&fujitsu_hotkey->fifo_lock);
	fujitsu_hotkey->fifo =
	    kfifo_alloc(RINGBUFFERSIZE * sizeof(int), GFP_KERNEL,
			&fujitsu_hotkey->fifo_lock);
	if (IS_ERR(fujitsu_hotkey->fifo)) {
		printk(KERN_ERR "kfifo_alloc failed\n");
		error = PTR_ERR(fujitsu_hotkey->fifo);
		goto err_stop;
	}

	fujitsu_hotkey->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_uninstall_notify;
	}

	snprintf(fujitsu_hotkey->phys, sizeof(fujitsu_hotkey->phys),
		 "%s/video/input0", acpi_device_hid(device));

	input->name = acpi_device_name(device);
	input->phys = fujitsu_hotkey->phys;
	input->id.bustype = BUS_HOST;
	input->id.product = 0x06;
	input->dev.parent = &device->dev;
	input->evbit[0] = BIT(EV_KEY);
	set_bit(KEY_SCREENLOCK, input->keybit);
	set_bit(KEY_MEDIA, input->keybit);
	set_bit(KEY_EMAIL, input->keybit);
	set_bit(KEY_SUSPEND, input->keybit);
	set_bit(KEY_UNKNOWN, input->keybit);

	error = input_register_device(input);
	if (error)
		goto err_free_input_dev;

	result = acpi_bus_get_power(fujitsu_hotkey->acpi_handle, &state);
	if (result) {
		printk(KERN_ERR "Error reading power state\n");
		goto end;
	}

	printk(KERN_INFO PREFIX "%s [%s] (%s)\n",
	       acpi_device_name(device), acpi_device_bid(device),
	       !device->power.state ? "on" : "off");

	fujitsu_hotkey->dev = device;

	if (ACPI_SUCCESS
	    (acpi_get_handle(device->handle, METHOD_NAME__INI, &handle))) {
		vdbg_printk(FUJLAPTOP_DBG_INFO, "Invoking _INI\n");
		if (ACPI_FAILURE
		    (acpi_evaluate_object
		     (device->handle, METHOD_NAME__INI, NULL, NULL)))
			printk(KERN_ERR "_INI Method failed\n");
	}

	i = 0;			/* Discard hotkey ringbuffer */
	while (get_irb() != 0 && (i++) < MAX_HOTKEY_RINGBUFFER_SIZE) ;
	vdbg_printk(FUJLAPTOP_DBG_INFO, "Discarded %i ringbuffer entries\n", i);

	return result;

end:
err_free_input_dev:
	input_free_device(input);
err_uninstall_notify:
	acpi_remove_notify_handler(device->handle, ACPI_DEVICE_NOTIFY,
				   acpi_fujitsu_hotkey_notify);
	kfifo_free(fujitsu_hotkey->fifo);
err_stop:

	return result;
}

static int acpi_fujitsu_hotkey_remove(struct acpi_device *device, int type)
{
	acpi_status status;
	struct fujitsu_hotkey_t *fujitsu_hotkey = NULL;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	fujitsu_hotkey = acpi_driver_data(device);

	status = acpi_remove_notify_handler(fujitsu_hotkey->acpi_handle,
					    ACPI_DEVICE_NOTIFY,
					    acpi_fujitsu_hotkey_notify);

	fujitsu_hotkey->acpi_handle = NULL;

	kfifo_free(fujitsu_hotkey->fifo);

	return 0;
}

static void acpi_fujitsu_hotkey_notify(acpi_handle handle, u32 event,
				       void *data)
{
	struct input_dev *input;
	int keycode, keycode_r;
	unsigned int irb = 1;
	int i, status;

	input = fujitsu_hotkey->input;

	vdbg_printk(FUJLAPTOP_DBG_TRACE, "Hotkey event\n");

	switch (event) {
	case ACPI_FUJITSU_NOTIFY_CODE1:
		i = 0;
		while ((irb = get_irb()) != 0
		       && (i++) < MAX_HOTKEY_RINGBUFFER_SIZE) {
			vdbg_printk(FUJLAPTOP_DBG_TRACE, "GIRB result [%x]\n",
				    irb);

			switch (irb & 0x4ff) {
			case LOCK_KEY:
				keycode = KEY_SCREENLOCK;
				break;
			case DISPLAY_KEY:
				keycode = KEY_MEDIA;
				break;
			case ENERGY_KEY:
				keycode = KEY_EMAIL;
				break;
			case REST_KEY:
				keycode = KEY_SUSPEND;
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
				status = kfifo_put(fujitsu_hotkey->fifo,
						(unsigned char *)&keycode,
						sizeof(keycode));
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
					kfifo_get
					(fujitsu_hotkey->fifo, (unsigned char *)
					 &keycode_r,
					 sizeof
					 (keycode_r))) == sizeof(keycode_r)) {
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

	return;
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
		},
};

static int __init fujitsu_init(void)
{
	int ret, result, max_brightness;

	if (acpi_disabled)
		return -ENODEV;

	fujitsu = kmalloc(sizeof(struct fujitsu_t), GFP_KERNEL);
	if (!fujitsu)
		return -ENOMEM;
	memset(fujitsu, 0, sizeof(struct fujitsu_t));

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

	fujitsu->bl_device =
	    backlight_device_register("fujitsu-laptop", NULL, NULL,
				      &fujitsubl_ops);
	if (IS_ERR(fujitsu->bl_device))
		return PTR_ERR(fujitsu->bl_device);

	max_brightness = fujitsu->max_brightness;

	fujitsu->bl_device->props.max_brightness = max_brightness - 1;
	fujitsu->bl_device->props.brightness = fujitsu->brightness_level;

	ret = platform_driver_register(&fujitsupf_driver);
	if (ret)
		goto fail_backlight;

	/* Register hotkey driver */

	fujitsu_hotkey = kmalloc(sizeof(struct fujitsu_hotkey_t), GFP_KERNEL);
	if (!fujitsu_hotkey) {
		ret = -ENOMEM;
		goto fail_hotkey;
	}
	memset(fujitsu_hotkey, 0, sizeof(struct fujitsu_hotkey_t));

	result = acpi_bus_register_driver(&acpi_fujitsu_hotkey_driver);
	if (result < 0) {
		ret = -ENODEV;
		goto fail_hotkey1;
	}

	printk(KERN_INFO "fujitsu-laptop: driver " FUJITSU_DRIVER_VERSION
	       " successfully loaded.\n");

	return 0;

fail_hotkey1:

	kfree(fujitsu_hotkey);

fail_hotkey:

	platform_driver_unregister(&fujitsupf_driver);

fail_backlight:

	backlight_device_unregister(fujitsu->bl_device);

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
	sysfs_remove_group(&fujitsu->pf_device->dev.kobj,
			   &fujitsupf_attribute_group);
	platform_device_unregister(fujitsu->pf_device);
	platform_driver_unregister(&fujitsupf_driver);
	backlight_device_unregister(fujitsu->bl_device);

	acpi_bus_unregister_driver(&acpi_fujitsu_driver);

	kfree(fujitsu);

	acpi_bus_unregister_driver(&acpi_fujitsu_hotkey_driver);

	kfree(fujitsu_hotkey);

	printk(KERN_INFO "fujitsu-laptop: driver unloaded.\n");
}

module_init(fujitsu_init);
module_exit(fujitsu_cleanup);

module_param(use_alt_lcd_levels, uint, 0644);
MODULE_PARM_DESC(use_alt_lcd_levels,
		 "Use alternative interface for lcd_levels (needed for Lifebook s6410).");
module_param(disable_brightness_keys, uint, 0644);
MODULE_PARM_DESC(disable_brightness_keys,
		 "Disable brightness keys (eg. if they are already handled by the generic ACPI_VIDEO device).");
module_param(disable_brightness_adjust, uint, 0644);
MODULE_PARM_DESC(disable_brightness_adjust, "Disable brightness adjustment .");
#ifdef CONFIG_FUJITSU_LAPTOP_DEBUG
module_param_named(debug, dbg_level, uint, 0644);
MODULE_PARM_DESC(debug, "Sets debug level bit-mask");
#endif

MODULE_AUTHOR("Jonathan Woithe, Peter Gruber");
MODULE_DESCRIPTION("Fujitsu laptop extras support");
MODULE_VERSION(FUJITSU_DRIVER_VERSION);
MODULE_LICENSE("GPL");

MODULE_ALIAS
    ("dmi:*:svnFUJITSUSIEMENS:*:pvr:rvnFUJITSU:rnFJNB1D3:*:cvrS6410:*");
MODULE_ALIAS
    ("dmi:*:svnFUJITSU:*:pvr:rvnFUJITSU:rnFJNB19C:*:cvrS7020:*");

static struct pnp_device_id pnp_ids[] = {
	{ .id = "FUJ02bf" },
	{ .id = "FUJ02B1" },
	{ .id = "FUJ02E3" },
	{ .id = "" }
};
MODULE_DEVICE_TABLE(pnp, pnp_ids);
