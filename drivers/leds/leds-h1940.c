/*
 * drivers/leds/h1940-leds.c
 * Copyright (c) Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * H1940 leds driver
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>
#include <asm/arch/h1940-latch.h>

/*
 * Green led.
 */
void h1940_greenled_set(struct led_classdev *led_dev, enum led_brightness value)
{
	switch (value) {
		case LED_HALF:
			h1940_latch_control(0,H1940_LATCH_LED_FLASH);
			s3c2410_gpio_setpin(S3C2410_GPA7,1);
			break;
		case LED_FULL:
			h1940_latch_control(0,H1940_LATCH_LED_GREEN);
			s3c2410_gpio_setpin(S3C2410_GPA7,1);
			break;
		default:
		case LED_OFF:
			h1940_latch_control(H1940_LATCH_LED_FLASH,0);
			h1940_latch_control(H1940_LATCH_LED_GREEN,0);
			s3c2410_gpio_setpin(S3C2410_GPA7,0);
			break;
	}
}

static struct led_classdev h1940_greenled = {
	.name			= "h1940:green",
	.brightness_set		= h1940_greenled_set,
	.default_trigger	= "h1940-charger",
};

/*
 * Red led.
 */
void h1940_redled_set(struct led_classdev *led_dev, enum led_brightness value)
{
	switch (value) {
		case LED_HALF:
			h1940_latch_control(0,H1940_LATCH_LED_FLASH);
			s3c2410_gpio_setpin(S3C2410_GPA1,1);
			break;
		case LED_FULL:
			h1940_latch_control(0,H1940_LATCH_LED_RED);
			s3c2410_gpio_setpin(S3C2410_GPA1,1);
			break;
		default:
		case LED_OFF:
			h1940_latch_control(H1940_LATCH_LED_FLASH,0);
			h1940_latch_control(H1940_LATCH_LED_RED,0);
			s3c2410_gpio_setpin(S3C2410_GPA1,0);
			break;
	}
}

static struct led_classdev h1940_redled = {
	.name			= "h1940:red",
	.brightness_set		= h1940_redled_set,
	.default_trigger	= "h1940-charger",
};

/*
 * Blue led.
 * (it can only be blue flashing led)
 */
void h1940_blueled_set(struct led_classdev *led_dev, enum led_brightness value)
{
	if (value) {
		/* flashing Blue */
		h1940_latch_control(0,H1940_LATCH_LED_FLASH);
		s3c2410_gpio_setpin(S3C2410_GPA3,1);
	} else {
		h1940_latch_control(H1940_LATCH_LED_FLASH,0);
		s3c2410_gpio_setpin(S3C2410_GPA3,0);
	}

}

static struct led_classdev h1940_blueled = {
	.name			= "h1940:blue",
	.brightness_set		= h1940_blueled_set,
	.default_trigger	= "h1940-bluetooth",
};

static int __init h1940leds_probe(struct platform_device *pdev)
{
	int ret;

	ret = led_classdev_register(&pdev->dev, &h1940_greenled);
	if (ret)
		goto err_green;

	ret = led_classdev_register(&pdev->dev, &h1940_redled);
	if (ret)
		goto err_red;

	ret = led_classdev_register(&pdev->dev, &h1940_blueled);
	if (ret)
		goto err_blue;

	return 0;

err_blue:
	led_classdev_unregister(&h1940_redled);
err_red:
	led_classdev_unregister(&h1940_greenled);
err_green:
	return ret;
}

static int h1940leds_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&h1940_greenled);
	led_classdev_unregister(&h1940_redled);
	led_classdev_unregister(&h1940_blueled);
	return 0;
}


static struct platform_driver h1940leds_driver = {
	.driver		= {
		.name	= "h1940-leds",
	},
	.probe		= h1940leds_probe,
	.remove		= h1940leds_remove,
};


static int __init h1940leds_init(void)
{
	return platform_driver_register(&h1940leds_driver);
}

static void __exit h1940leds_exit(void)
{
	platform_driver_unregister(&h1940leds_driver);
}

module_init(h1940leds_init);
module_exit(h1940leds_exit);

MODULE_AUTHOR("Arnaud Patard <arnaud.patard@rtp-net.org>");
MODULE_DESCRIPTION("LED driver for the iPAQ H1940");
MODULE_LICENSE("GPL");
