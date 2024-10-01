// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  LEDs driver for the Cobalt Raq series.
 *
 *  Copyright (C) 2007  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/export.h>

#define LED_WEB		0x04
#define LED_POWER_OFF	0x08

static void __iomem *led_port;
static u8 led_value;
static DEFINE_SPINLOCK(led_value_lock);

static void raq_web_led_set(struct led_classdev *led_cdev,
			    enum led_brightness brightness)
{
	unsigned long flags;

	spin_lock_irqsave(&led_value_lock, flags);

	if (brightness)
		led_value |= LED_WEB;
	else
		led_value &= ~LED_WEB;
	writeb(led_value, led_port);

	spin_unlock_irqrestore(&led_value_lock, flags);
}

static struct led_classdev raq_web_led = {
	.name		= "raq::web",
	.brightness_set	= raq_web_led_set,
};

static void raq_power_off_led_set(struct led_classdev *led_cdev,
				  enum led_brightness brightness)
{
	unsigned long flags;

	spin_lock_irqsave(&led_value_lock, flags);

	if (brightness)
		led_value |= LED_POWER_OFF;
	else
		led_value &= ~LED_POWER_OFF;
	writeb(led_value, led_port);

	spin_unlock_irqrestore(&led_value_lock, flags);
}

static struct led_classdev raq_power_off_led = {
	.name			= "raq::power-off",
	.brightness_set		= raq_power_off_led_set,
	.default_trigger	= "power-off",
};

static int cobalt_raq_led_probe(struct platform_device *pdev)
{
	struct resource *res;
	int retval;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EBUSY;

	led_port = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!led_port)
		return -ENOMEM;

	retval = led_classdev_register(&pdev->dev, &raq_power_off_led);
	if (retval)
		goto err_null;

	retval = led_classdev_register(&pdev->dev, &raq_web_led);
	if (retval)
		goto err_unregister;

	return 0;

err_unregister:
	led_classdev_unregister(&raq_power_off_led);

err_null:
	led_port = NULL;

	return retval;
}

static struct platform_driver cobalt_raq_led_driver = {
	.probe	= cobalt_raq_led_probe,
	.driver = {
		.name	= "cobalt-raq-leds",
	},
};

builtin_platform_driver(cobalt_raq_led_driver);
