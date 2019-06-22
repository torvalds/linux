// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED Driver for the Freecom FSG-3
 *
 * Copyright (c) 2008 Rod Whitby <rod@whitby.id.au>
 *
 * Author: Rod Whitby <rod@whitby.id.au>
 *
 * Based on leds-spitz.c
 * Copyright 2005-2006 Openedhand Ltd.
 * Author: Richard Purdie <rpurdie@openedhand.com>
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/io.h>
#include <mach/hardware.h>

#define FSG_LED_WLAN_BIT	0
#define FSG_LED_WAN_BIT		1
#define FSG_LED_SATA_BIT	2
#define FSG_LED_USB_BIT		4
#define FSG_LED_RING_BIT	5
#define FSG_LED_SYNC_BIT	7

static short __iomem *latch_address;
static unsigned short latch_value;


static void fsg_led_wlan_set(struct led_classdev *led_cdev,
			     enum led_brightness value)
{
	if (value) {
		latch_value &= ~(1 << FSG_LED_WLAN_BIT);
		*latch_address = latch_value;
	} else {
		latch_value |=  (1 << FSG_LED_WLAN_BIT);
		*latch_address = latch_value;
	}
}

static void fsg_led_wan_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	if (value) {
		latch_value &= ~(1 << FSG_LED_WAN_BIT);
		*latch_address = latch_value;
	} else {
		latch_value |=  (1 << FSG_LED_WAN_BIT);
		*latch_address = latch_value;
	}
}

static void fsg_led_sata_set(struct led_classdev *led_cdev,
			     enum led_brightness value)
{
	if (value) {
		latch_value &= ~(1 << FSG_LED_SATA_BIT);
		*latch_address = latch_value;
	} else {
		latch_value |=  (1 << FSG_LED_SATA_BIT);
		*latch_address = latch_value;
	}
}

static void fsg_led_usb_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	if (value) {
		latch_value &= ~(1 << FSG_LED_USB_BIT);
		*latch_address = latch_value;
	} else {
		latch_value |=  (1 << FSG_LED_USB_BIT);
		*latch_address = latch_value;
	}
}

static void fsg_led_sync_set(struct led_classdev *led_cdev,
			     enum led_brightness value)
{
	if (value) {
		latch_value &= ~(1 << FSG_LED_SYNC_BIT);
		*latch_address = latch_value;
	} else {
		latch_value |=  (1 << FSG_LED_SYNC_BIT);
		*latch_address = latch_value;
	}
}

static void fsg_led_ring_set(struct led_classdev *led_cdev,
			     enum led_brightness value)
{
	if (value) {
		latch_value &= ~(1 << FSG_LED_RING_BIT);
		*latch_address = latch_value;
	} else {
		latch_value |=  (1 << FSG_LED_RING_BIT);
		*latch_address = latch_value;
	}
}


static struct led_classdev fsg_wlan_led = {
	.name			= "fsg:blue:wlan",
	.brightness_set		= fsg_led_wlan_set,
	.flags			= LED_CORE_SUSPENDRESUME,
};

static struct led_classdev fsg_wan_led = {
	.name			= "fsg:blue:wan",
	.brightness_set		= fsg_led_wan_set,
	.flags			= LED_CORE_SUSPENDRESUME,
};

static struct led_classdev fsg_sata_led = {
	.name			= "fsg:blue:sata",
	.brightness_set		= fsg_led_sata_set,
	.flags			= LED_CORE_SUSPENDRESUME,
};

static struct led_classdev fsg_usb_led = {
	.name			= "fsg:blue:usb",
	.brightness_set		= fsg_led_usb_set,
	.flags			= LED_CORE_SUSPENDRESUME,
};

static struct led_classdev fsg_sync_led = {
	.name			= "fsg:blue:sync",
	.brightness_set		= fsg_led_sync_set,
	.flags			= LED_CORE_SUSPENDRESUME,
};

static struct led_classdev fsg_ring_led = {
	.name			= "fsg:blue:ring",
	.brightness_set		= fsg_led_ring_set,
	.flags			= LED_CORE_SUSPENDRESUME,
};


static int fsg_led_probe(struct platform_device *pdev)
{
	int ret;

	/* Map the LED chip select address space */
	latch_address = (unsigned short *) devm_ioremap(&pdev->dev,
						IXP4XX_EXP_BUS_BASE(2), 512);
	if (!latch_address)
		return -ENOMEM;

	latch_value = 0xffff;
	*latch_address = latch_value;

	ret = devm_led_classdev_register(&pdev->dev, &fsg_wlan_led);
	if (ret < 0)
		return ret;

	ret = devm_led_classdev_register(&pdev->dev, &fsg_wan_led);
	if (ret < 0)
		return ret;

	ret = devm_led_classdev_register(&pdev->dev, &fsg_sata_led);
	if (ret < 0)
		return ret;

	ret = devm_led_classdev_register(&pdev->dev, &fsg_usb_led);
	if (ret < 0)
		return ret;

	ret = devm_led_classdev_register(&pdev->dev, &fsg_sync_led);
	if (ret < 0)
		return ret;

	ret = devm_led_classdev_register(&pdev->dev, &fsg_ring_led);
	if (ret < 0)
		return ret;

	return ret;
}

static struct platform_driver fsg_led_driver = {
	.probe		= fsg_led_probe,
	.driver		= {
		.name		= "fsg-led",
	},
};

module_platform_driver(fsg_led_driver);

MODULE_AUTHOR("Rod Whitby <rod@whitby.id.au>");
MODULE_DESCRIPTION("Freecom FSG-3 LED driver");
MODULE_LICENSE("GPL");
