/*
 *  Backlight Driver for Nvidia 8600 in Macbook Pro
 *
 *  Copyright (c) Red Hat <mjg@redhat.com>
 *  Based on code from Pommed:
 *  Copyright (C) 2006 Nicolas Boichat <nicolas @boichat.ch>
 *  Copyright (C) 2006 Felipe Alfaro Solana <felipe_alfaro @linuxmail.org>
 *  Copyright (C) 2007 Julien BLACHE <jb@jblache.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This driver triggers SMIs which cause the firmware to change the
 *  backlight brightness. This is icky in many ways, but it's impractical to
 *  get at the firmware code in order to figure out what it's actually doing.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/dmi.h>
#include <linux/io.h>

static struct backlight_device *mbp_backlight_device;

static struct dmi_system_id __initdata mbp_device_table[] = {
	{
		.ident = "3,1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro3,1"),
		},
	},
	{
		.ident = "3,2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro3,2"),
		},
	},
	{
		.ident = "4,1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro4,1"),
		},
	},
	{ }
};

static int mbp_send_intensity(struct backlight_device *bd)
{
	int intensity = bd->props.brightness;

	outb(0x04 | (intensity << 4), 0xb3);
	outb(0xbf, 0xb2);

	return 0;
}

static int mbp_get_intensity(struct backlight_device *bd)
{
	outb(0x03, 0xb3);
	outb(0xbf, 0xb2);
	return inb(0xb3) >> 4;
}

static struct backlight_ops mbp_ops = {
	.get_brightness = mbp_get_intensity,
	.update_status  = mbp_send_intensity,
};

static int __init mbp_init(void)
{
	if (!dmi_check_system(mbp_device_table))
		return -ENODEV;

	if (!request_region(0xb2, 2, "Macbook Pro backlight"))
		return -ENXIO;

	mbp_backlight_device = backlight_device_register("mbp_backlight",
							 NULL, NULL,
							 &mbp_ops);
	if (IS_ERR(mbp_backlight_device)) {
		release_region(0xb2, 2);
		return PTR_ERR(mbp_backlight_device);
	}

	mbp_backlight_device->props.max_brightness = 15;
	mbp_backlight_device->props.brightness =
		mbp_get_intensity(mbp_backlight_device);
	backlight_update_status(mbp_backlight_device);

	return 0;
}

static void __exit mbp_exit(void)
{
	backlight_device_unregister(mbp_backlight_device);

	release_region(0xb2, 2);
}

module_init(mbp_init);
module_exit(mbp_exit);

MODULE_AUTHOR("Matthew Garrett <mjg@redhat.com>");
MODULE_DESCRIPTION("Nvidia-based Macbook Pro Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("svnAppleInc.:pnMacBookPro3,1");
MODULE_ALIAS("svnAppleInc.:pnMacBookPro3,2");
MODULE_ALIAS("svnAppleInc.:pnMacBookPro4,1");
