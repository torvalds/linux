/*-*-linux-c-*-*/

/*
  Copyright (C) 2007 Jonathan Woithe <jwoithe@physics.adelaide.edu.au>
  Based on earlier work:
    Copyright (C) 2003 Shane Spencer <shane@bogomip.com>
    Adrian Yee <brewt-fujitsu@brewt.org>

  Templated from msi-laptop.c which is copyright by its respective authors.

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
 * This driver has been tested on a Fujitsu Lifebook S7020.  It should
 * work on most P-series and S-series Lifebooks, but YMMV.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>

#define FUJITSU_DRIVER_VERSION "0.3"

#define FUJITSU_LCD_N_LEVELS 8

#define ACPI_FUJITSU_CLASS              "fujitsu"
#define ACPI_FUJITSU_HID                "FUJ02B1"
#define ACPI_FUJITSU_DRIVER_NAME        "Fujitsu laptop FUJ02B1 ACPI extras driver"
#define ACPI_FUJITSU_DEVICE_NAME        "Fujitsu FUJ02B1"

struct fujitsu_t {
	acpi_handle acpi_handle;
	struct backlight_device *bl_device;
	struct platform_device *pf_device;

	unsigned long fuj02b1_state;
	unsigned int brightness_changed;
	unsigned int brightness_level;
};

static struct fujitsu_t *fujitsu;

/* Hardware access */

static int set_lcd_level(int level)
{
	acpi_status status = AE_OK;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg0 };
	acpi_handle handle = NULL;

	if (level < 0 || level >= FUJITSU_LCD_N_LEVELS)
		return -EINVAL;

	if (!fujitsu)
		return -EINVAL;

	status = acpi_get_handle(fujitsu->acpi_handle, "SBLL", &handle);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "SBLL not present\n"));
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

	// Get the Brightness
	status =
	    acpi_evaluate_integer(fujitsu->acpi_handle, "GBLL", NULL, &state);
	if (status < 0)
		return status;

	fujitsu->fuj02b1_state = state;
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
	return get_lcd_level();
}

static int bl_update_status(struct backlight_device *b)
{
	return set_lcd_level(b->props.brightness);
}

static struct backlight_ops fujitsubl_ops = {
	.get_brightness = bl_get_brightness,
	.update_status = bl_update_status,
};

/* Platform device */

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
	    || (level < 0 || level >= FUJITSU_LCD_N_LEVELS))
		return -EINVAL;

	ret = set_lcd_level(level);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(lcd_level, 0644, show_lcd_level, store_lcd_level);

static struct attribute *fujitsupf_attributes[] = {
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

/* ACPI device */

static int acpi_fujitsu_add(struct acpi_device *device)
{
	int result = 0;
	int state = 0;

	ACPI_FUNCTION_TRACE("acpi_fujitsu_add");

	if (!device)
		return -EINVAL;

	fujitsu->acpi_handle = device->handle;
	sprintf(acpi_device_name(device), "%s", ACPI_FUJITSU_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_FUJITSU_CLASS);
	acpi_driver_data(device) = fujitsu;

	result = acpi_bus_get_power(fujitsu->acpi_handle, &state);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Error reading power state\n"));
		goto end;
	}

	printk(KERN_INFO PREFIX "%s [%s] (%s)\n",
	       acpi_device_name(device), acpi_device_bid(device),
	       !device->power.state ? "on" : "off");

      end:

	return result;
}

static int acpi_fujitsu_remove(struct acpi_device *device, int type)
{
	ACPI_FUNCTION_TRACE("acpi_fujitsu_remove");

	if (!device || !acpi_driver_data(device))
		return -EINVAL;
	fujitsu->acpi_handle = NULL;

	return 0;
}

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

/* Initialization */

static int __init fujitsu_init(void)
{
	int ret, result;

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

	/* Register backlight stuff */

	fujitsu->bl_device =
	    backlight_device_register("fujitsu-laptop", NULL, NULL,
				      &fujitsubl_ops);
	if (IS_ERR(fujitsu->bl_device))
		return PTR_ERR(fujitsu->bl_device);

	fujitsu->bl_device->props.max_brightness = FUJITSU_LCD_N_LEVELS - 1;
	ret = platform_driver_register(&fujitsupf_driver);
	if (ret)
		goto fail_backlight;

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

	printk(KERN_INFO "fujitsu-laptop: driver " FUJITSU_DRIVER_VERSION
	       " successfully loaded.\n");

	return 0;

      fail_platform_device2:

	platform_device_del(fujitsu->pf_device);

      fail_platform_device1:

	platform_device_put(fujitsu->pf_device);

      fail_platform_driver:

	platform_driver_unregister(&fujitsupf_driver);

      fail_backlight:

	backlight_device_unregister(fujitsu->bl_device);

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

	printk(KERN_INFO "fujitsu-laptop: driver unloaded.\n");
}

module_init(fujitsu_init);
module_exit(fujitsu_cleanup);

MODULE_AUTHOR("Jonathan Woithe");
MODULE_DESCRIPTION("Fujitsu laptop extras support");
MODULE_VERSION(FUJITSU_DRIVER_VERSION);
MODULE_LICENSE("GPL");
