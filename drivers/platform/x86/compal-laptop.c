/*-*-linux-c-*-*/

/*
  Copyright (C) 2008 Cezary Jackiewicz <cezary.jackiewicz (at) gmail.com>

  based on MSI driver

  Copyright (C) 2006 Lennart Poettering <mzxreary (at) 0pointer (dot) de>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
 */

/*
 * comapl-laptop.c - Compal laptop support.
 *
 * This driver exports a few files in /sys/devices/platform/compal-laptop/:
 *
 *   wlan - wlan subsystem state: contains 0 or 1 (rw)
 *
 *   bluetooth - Bluetooth subsystem state: contains 0 or 1 (rw)
 *
 *   raw - raw value taken from embedded controller register (ro)
 *
 * In addition to these platform device attributes the driver
 * registers itself in the Linux backlight control subsystem and is
 * available to userspace under /sys/class/backlight/compal-laptop/.
 *
 * This driver might work on other laptops produced by Compal. If you
 * want to try it you can pass force=1 as argument to the module which
 * will force it to load even when the DMI data doesn't identify the
 * laptop as FL9x.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>

#define COMPAL_DRIVER_VERSION "0.2.6"

#define COMPAL_LCD_LEVEL_MAX 8

#define COMPAL_EC_COMMAND_WIRELESS 0xBB
#define COMPAL_EC_COMMAND_LCD_LEVEL 0xB9

#define KILLSWITCH_MASK 0x10
#define WLAN_MASK	0x01
#define BT_MASK 	0x02

static int force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Force driver load, ignore DMI data");

/* Hardware access */

static int set_lcd_level(int level)
{
	if (level < 0 || level >= COMPAL_LCD_LEVEL_MAX)
		return -EINVAL;

	ec_write(COMPAL_EC_COMMAND_LCD_LEVEL, level);

	return 0;
}

static int get_lcd_level(void)
{
	u8 result;

	ec_read(COMPAL_EC_COMMAND_LCD_LEVEL, &result);

	return (int) result;
}

static int set_wlan_state(int state)
{
	u8 result, value;

	ec_read(COMPAL_EC_COMMAND_WIRELESS, &result);

	if ((result & KILLSWITCH_MASK) == 0)
		return -EINVAL;
	else {
		if (state)
			value = (u8) (result | WLAN_MASK);
		else
			value = (u8) (result & ~WLAN_MASK);
		ec_write(COMPAL_EC_COMMAND_WIRELESS, value);
	}

	return 0;
}

static int set_bluetooth_state(int state)
{
	u8 result, value;

	ec_read(COMPAL_EC_COMMAND_WIRELESS, &result);

	if ((result & KILLSWITCH_MASK) == 0)
		return -EINVAL;
	else {
		if (state)
			value = (u8) (result | BT_MASK);
		else
			value = (u8) (result & ~BT_MASK);
		ec_write(COMPAL_EC_COMMAND_WIRELESS, value);
	}

	return 0;
}

static int get_wireless_state(int *wlan, int *bluetooth)
{
	u8 result;

	ec_read(COMPAL_EC_COMMAND_WIRELESS, &result);

	if (wlan) {
		if ((result & KILLSWITCH_MASK) == 0)
			*wlan = 0;
		else
			*wlan = result & WLAN_MASK;
	}

	if (bluetooth) {
		if ((result & KILLSWITCH_MASK) == 0)
			*bluetooth = 0;
		else
			*bluetooth = (result & BT_MASK) >> 1;
	}

	return 0;
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

static struct backlight_ops compalbl_ops = {
	.get_brightness = bl_get_brightness,
	.update_status	= bl_update_status,
};

static struct backlight_device *compalbl_device;

/* Platform device */

static ssize_t show_wlan(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret, enabled;

	ret = get_wireless_state(&enabled, NULL);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", enabled);
}

static ssize_t show_raw(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	u8 result;

	ec_read(COMPAL_EC_COMMAND_WIRELESS, &result);

	return sprintf(buf, "%i\n", result);
}

static ssize_t show_bluetooth(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret, enabled;

	ret = get_wireless_state(NULL, &enabled);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", enabled);
}

static ssize_t store_wlan_state(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int state, ret;

	if (sscanf(buf, "%i", &state) != 1 || (state < 0 || state > 1))
		return -EINVAL;

	ret = set_wlan_state(state);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t store_bluetooth_state(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int state, ret;

	if (sscanf(buf, "%i", &state) != 1 || (state < 0 || state > 1))
		return -EINVAL;

	ret = set_bluetooth_state(state);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(bluetooth, 0644, show_bluetooth, store_bluetooth_state);
static DEVICE_ATTR(wlan, 0644, show_wlan, store_wlan_state);
static DEVICE_ATTR(raw, 0444, show_raw, NULL);

static struct attribute *compal_attributes[] = {
	&dev_attr_bluetooth.attr,
	&dev_attr_wlan.attr,
	&dev_attr_raw.attr,
	NULL
};

static struct attribute_group compal_attribute_group = {
	.attrs = compal_attributes
};

static struct platform_driver compal_driver = {
	.driver = {
		.name = "compal-laptop",
		.owner = THIS_MODULE,
	}
};

static struct platform_device *compal_device;

/* Initialization */

static int dmi_check_cb(const struct dmi_system_id *id)
{
	printk(KERN_INFO "compal-laptop: Identified laptop model '%s'.\n",
		id->ident);

	return 0;
}

static struct dmi_system_id __initdata compal_dmi_table[] = {
	{
		.ident = "FL90/IFL90",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "IFL90"),
			DMI_MATCH(DMI_BOARD_VERSION, "IFT00"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "FL90/IFL90",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "IFL90"),
			DMI_MATCH(DMI_BOARD_VERSION, "REFERENCE"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "FL91/IFL91",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "IFL91"),
			DMI_MATCH(DMI_BOARD_VERSION, "IFT00"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "FL92/JFL92",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "JFL92"),
			DMI_MATCH(DMI_BOARD_VERSION, "IFT00"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "FT00/IFT00",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "IFT00"),
			DMI_MATCH(DMI_BOARD_VERSION, "IFT00"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "Dell Mini 9",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 910"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "Dell Mini 10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 1010"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "Dell Mini 10v",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 1011"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "Dell Inspiron 11z",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 1110"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "Dell Mini 12",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 1210"),
		},
		.callback = dmi_check_cb
	},

	{ }
};

static int __init compal_init(void)
{
	int ret;

	if (acpi_disabled)
		return -ENODEV;

	if (!force && !dmi_check_system(compal_dmi_table))
		return -ENODEV;

	/* Register backlight stuff */

	if (!acpi_video_backlight_support()) {
		compalbl_device = backlight_device_register("compal-laptop", NULL, NULL,
							    &compalbl_ops);
		if (IS_ERR(compalbl_device))
			return PTR_ERR(compalbl_device);

		compalbl_device->props.max_brightness = COMPAL_LCD_LEVEL_MAX-1;
	}

	ret = platform_driver_register(&compal_driver);
	if (ret)
		goto fail_backlight;

	/* Register platform stuff */

	compal_device = platform_device_alloc("compal-laptop", -1);
	if (!compal_device) {
		ret = -ENOMEM;
		goto fail_platform_driver;
	}

	ret = platform_device_add(compal_device);
	if (ret)
		goto fail_platform_device1;

	ret = sysfs_create_group(&compal_device->dev.kobj,
		&compal_attribute_group);
	if (ret)
		goto fail_platform_device2;

	printk(KERN_INFO "compal-laptop: driver "COMPAL_DRIVER_VERSION
		" successfully loaded.\n");

	return 0;

fail_platform_device2:

	platform_device_del(compal_device);

fail_platform_device1:

	platform_device_put(compal_device);

fail_platform_driver:

	platform_driver_unregister(&compal_driver);

fail_backlight:

	backlight_device_unregister(compalbl_device);

	return ret;
}

static void __exit compal_cleanup(void)
{

	sysfs_remove_group(&compal_device->dev.kobj, &compal_attribute_group);
	platform_device_unregister(compal_device);
	platform_driver_unregister(&compal_driver);
	backlight_device_unregister(compalbl_device);

	printk(KERN_INFO "compal-laptop: driver unloaded.\n");
}

module_init(compal_init);
module_exit(compal_cleanup);

MODULE_AUTHOR("Cezary Jackiewicz");
MODULE_DESCRIPTION("Compal Laptop Support");
MODULE_VERSION(COMPAL_DRIVER_VERSION);
MODULE_LICENSE("GPL");

MODULE_ALIAS("dmi:*:rnIFL90:rvrIFT00:*");
MODULE_ALIAS("dmi:*:rnIFL90:rvrREFERENCE:*");
MODULE_ALIAS("dmi:*:rnIFL91:rvrIFT00:*");
MODULE_ALIAS("dmi:*:rnJFL92:rvrIFT00:*");
MODULE_ALIAS("dmi:*:rnIFT00:rvrIFT00:*");
MODULE_ALIAS("dmi:*:svnDellInc.:pnInspiron910:*");
MODULE_ALIAS("dmi:*:svnDellInc.:pnInspiron1010:*");
MODULE_ALIAS("dmi:*:svnDellInc.:pnInspiron1011:*");
MODULE_ALIAS("dmi:*:svnDellInc.:pnInspiron1110:*");
MODULE_ALIAS("dmi:*:svnDellInc.:pnInspiron1210:*");
