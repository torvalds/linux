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
 * The driver registers itself with the rfkill subsystem and
 * the Linux backlight control subsystem.
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
#include <linux/rfkill.h>

#define COMPAL_DRIVER_VERSION "0.2.6"

#define COMPAL_LCD_LEVEL_MAX 8

#define COMPAL_EC_COMMAND_WIRELESS 0xBB
#define COMPAL_EC_COMMAND_LCD_LEVEL 0xB9

#define KILLSWITCH_MASK 0x10
#define WLAN_MASK	0x01
#define BT_MASK 	0x02

static struct rfkill *wifi_rfkill;
static struct rfkill *bt_rfkill;
static struct platform_device *compal_device;

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

static int compal_rfkill_set(void *data, bool blocked)
{
	unsigned long radio = (unsigned long) data;
	u8 result, value;

	ec_read(COMPAL_EC_COMMAND_WIRELESS, &result);

	if (!blocked)
		value = (u8) (result | radio);
	else
		value = (u8) (result & ~radio);
	ec_write(COMPAL_EC_COMMAND_WIRELESS, value);

	return 0;
}

static void compal_rfkill_poll(struct rfkill *rfkill, void *data)
{
	u8 result;
	bool hw_blocked;

	ec_read(COMPAL_EC_COMMAND_WIRELESS, &result);

	hw_blocked = !(result & KILLSWITCH_MASK);
	rfkill_set_hw_state(rfkill, hw_blocked);
}

static const struct rfkill_ops compal_rfkill_ops = {
	.poll = compal_rfkill_poll,
	.set_block = compal_rfkill_set,
};

static int setup_rfkill(void)
{
	int ret;

	wifi_rfkill = rfkill_alloc("compal-wifi", &compal_device->dev,
				RFKILL_TYPE_WLAN, &compal_rfkill_ops,
				(void *) WLAN_MASK);
	if (!wifi_rfkill)
		return -ENOMEM;

	ret = rfkill_register(wifi_rfkill);
	if (ret)
		goto err_wifi;

	bt_rfkill = rfkill_alloc("compal-bluetooth", &compal_device->dev,
				RFKILL_TYPE_BLUETOOTH, &compal_rfkill_ops,
				(void *) BT_MASK);
	if (!bt_rfkill) {
		ret = -ENOMEM;
		goto err_allocate_bt;
	}
	ret = rfkill_register(bt_rfkill);
	if (ret)
		goto err_register_bt;

	return 0;

err_register_bt:
	rfkill_destroy(bt_rfkill);

err_allocate_bt:
	rfkill_unregister(wifi_rfkill);

err_wifi:
	rfkill_destroy(wifi_rfkill);

	return ret;
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


static struct platform_driver compal_driver = {
	.driver = {
		.name = "compal-laptop",
		.owner = THIS_MODULE,
	}
};

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
		goto fail_platform_device;

	ret = setup_rfkill();
	if (ret)
		goto fail_rfkill;

	printk(KERN_INFO "compal-laptop: driver "COMPAL_DRIVER_VERSION
		" successfully loaded.\n");

	return 0;

fail_rfkill:
	platform_device_del(compal_device);

fail_platform_device:

	platform_device_put(compal_device);

fail_platform_driver:

	platform_driver_unregister(&compal_driver);

fail_backlight:

	backlight_device_unregister(compalbl_device);

	return ret;
}

static void __exit compal_cleanup(void)
{

	platform_device_unregister(compal_device);
	platform_driver_unregister(&compal_driver);
	backlight_device_unregister(compalbl_device);
	rfkill_unregister(wifi_rfkill);
	rfkill_destroy(wifi_rfkill);
	rfkill_unregister(bt_rfkill);
	rfkill_destroy(bt_rfkill);

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
