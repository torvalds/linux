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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <mach/hardware.h>
#include <asm/io.h>

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
};

static struct led_classdev fsg_wan_led = {
	.name			= "fsg:blue:wan",
	.brightness_set		= fsg_led_wan_set,
};

static struct led_classdev fsg_sata_led = {
	.name			= "fsg:blue:sata",
	.brightness_set		= fsg_led_sata_set,
};

static struct led_classdev fsg_usb_led = {
	.name			= "fsg:blue:usb",
	.brightness_set		= fsg_led_usb_set,
};

static struct led_classdev fsg_sync_led = {
	.name			= "fsg:blue:sync",
	.brightness_set		= fsg_led_sync_set,
};

static struct led_classdev fsg_ring_led = {
	.name			= "fsg:blue:ring",
	.brightness_set		= fsg_led_ring_set,
};



#ifdef CONFIG_PM
static int fsg_led_suspend(struct platform_device *dev, pm_message_t state)
{
	led_classdev_suspend(&fsg_wlan_led);
	led_classdev_suspend(&fsg_wan_led);
	led_classdev_suspend(&fsg_sata_led);
	led_classdev_suspend(&fsg_usb_led);
	led_classdev_suspend(&fsg_sync_led);
	led_classdev_suspend(&fsg_ring_led);
	return 0;
}

static int fsg_led_resume(struct platform_device *dev)
{
	led_classdev_resume(&fsg_wlan_led);
	led_classdev_resume(&fsg_wan_led);
	led_classdev_resume(&fsg_sata_led);
	led_classdev_resume(&fsg_usb_led);
	led_classdev_resume(&fsg_sync_led);
	led_classdev_resume(&fsg_ring_led);
	return 0;
}
#endif


static int fsg_led_probe(struct platform_device *pdev)
{
	int ret;

	/* Map the LED chip select address space */
	latch_address = (unsigned short *) ioremap(IXP4XX_EXP_BUS_BASE(2), 512);
	if (!latch_address) {
		ret = -ENOMEM;
		goto failremap;
	}

	latch_value = 0xffff;
	*latch_address = latch_value;

	ret = led_classdev_register(&pdev->dev, &fsg_wlan_led);
	if (ret < 0)
		goto failwlan;

	ret = led_classdev_register(&pdev->dev, &fsg_wan_led);
	if (ret < 0)
		goto failwan;

	ret = led_classdev_register(&pdev->dev, &fsg_sata_led);
	if (ret < 0)
		goto failsata;

	ret = led_classdev_register(&pdev->dev, &fsg_usb_led);
	if (ret < 0)
		goto failusb;

	ret = led_classdev_register(&pdev->dev, &fsg_sync_led);
	if (ret < 0)
		goto failsync;

	ret = led_classdev_register(&pdev->dev, &fsg_ring_led);
	if (ret < 0)
		goto failring;

	return ret;

 failring:
	led_classdev_unregister(&fsg_sync_led);
 failsync:
	led_classdev_unregister(&fsg_usb_led);
 failusb:
	led_classdev_unregister(&fsg_sata_led);
 failsata:
	led_classdev_unregister(&fsg_wan_led);
 failwan:
	led_classdev_unregister(&fsg_wlan_led);
 failwlan:
	iounmap(latch_address);
 failremap:

	return ret;
}

static int fsg_led_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&fsg_wlan_led);
	led_classdev_unregister(&fsg_wan_led);
	led_classdev_unregister(&fsg_sata_led);
	led_classdev_unregister(&fsg_usb_led);
	led_classdev_unregister(&fsg_sync_led);
	led_classdev_unregister(&fsg_ring_led);

	iounmap(latch_address);

	return 0;
}


static struct platform_driver fsg_led_driver = {
	.probe		= fsg_led_probe,
	.remove		= fsg_led_remove,
#ifdef CONFIG_PM
	.suspend	= fsg_led_suspend,
	.resume		= fsg_led_resume,
#endif
	.driver		= {
		.name		= "fsg-led",
	},
};


static int __init fsg_led_init(void)
{
	return platform_driver_register(&fsg_led_driver);
}

static void __exit fsg_led_exit(void)
{
	platform_driver_unregister(&fsg_led_driver);
}


module_init(fsg_led_init);
module_exit(fsg_led_exit);

MODULE_AUTHOR("Rod Whitby <rod@whitby.id.au>");
MODULE_DESCRIPTION("Freecom FSG-3 LED driver");
MODULE_LICENSE("GPL");
