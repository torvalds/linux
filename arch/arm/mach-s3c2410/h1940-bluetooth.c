/*
 * arch/arm/mach-s3c2410/h1940-bluetooth.c
 * Copyright (c) Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 *	    S3C2410 bluetooth "driver"
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <mach/regs-gpio.h>
#include <mach/hardware.h>
#include <mach/h1940-latch.h>

#define DRV_NAME              "h1940-bt"

#ifdef CONFIG_LEDS_H1940
DEFINE_LED_TRIGGER(bt_led_trigger);
#endif

static int state;

/* Bluetooth control */
static void h1940bt_enable(int on)
{
	if (on) {
#ifdef CONFIG_LEDS_H1940
		/* flashing Blue */
		led_trigger_event(bt_led_trigger, LED_HALF);
#endif

		/* Power on the chip */
		h1940_latch_control(0, H1940_LATCH_BLUETOOTH_POWER);
		/* Reset the chip */
		mdelay(10);
		s3c2410_gpio_setpin(S3C2410_GPH1, 1);
		mdelay(10);
		s3c2410_gpio_setpin(S3C2410_GPH1, 0);

		state = 1;
	}
	else {
#ifdef CONFIG_LEDS_H1940
		led_trigger_event(bt_led_trigger, 0);
#endif

		s3c2410_gpio_setpin(S3C2410_GPH1, 1);
		mdelay(10);
		s3c2410_gpio_setpin(S3C2410_GPH1, 0);
		mdelay(10);
		h1940_latch_control(H1940_LATCH_BLUETOOTH_POWER, 0);

		state = 0;
	}
}

static ssize_t h1940bt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", state);
}

static ssize_t h1940bt_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int new_state;
	char *endp;

	new_state = simple_strtoul(buf, &endp, 0);
	if (*endp && !isspace(*endp))
		return -EINVAL;

	h1940bt_enable(new_state);

	return count;
}
static DEVICE_ATTR(enable, 0644,
		h1940bt_show,
		h1940bt_store);

static int __init h1940bt_probe(struct platform_device *pdev)
{
	/* Configures BT serial port GPIOs */
	s3c2410_gpio_cfgpin(S3C2410_GPH0, S3C2410_GPH0_nCTS0);
	s3c2410_gpio_pullup(S3C2410_GPH0, 1);
	s3c2410_gpio_cfgpin(S3C2410_GPH1, S3C2410_GPH1_OUTP);
	s3c2410_gpio_pullup(S3C2410_GPH1, 1);
	s3c2410_gpio_cfgpin(S3C2410_GPH2, S3C2410_GPH2_TXD0);
	s3c2410_gpio_pullup(S3C2410_GPH2, 1);
	s3c2410_gpio_cfgpin(S3C2410_GPH3, S3C2410_GPH3_RXD0);
	s3c2410_gpio_pullup(S3C2410_GPH3, 1);

#ifdef CONFIG_LEDS_H1940
	led_trigger_register_simple("h1940-bluetooth", &bt_led_trigger);
#endif

	/* disable BT by default */
	h1940bt_enable(0);

	return device_create_file(&pdev->dev, &dev_attr_enable);
}

static int h1940bt_remove(struct platform_device *pdev)
{
#ifdef CONFIG_LEDS_H1940
	led_trigger_unregister_simple(bt_led_trigger);
#endif
	return 0;
}


static struct platform_driver h1940bt_driver = {
	.driver		= {
		.name	= DRV_NAME,
	},
	.probe		= h1940bt_probe,
	.remove		= h1940bt_remove,
};


static int __init h1940bt_init(void)
{
	return platform_driver_register(&h1940bt_driver);
}

static void __exit h1940bt_exit(void)
{
	platform_driver_unregister(&h1940bt_driver);
}

module_init(h1940bt_init);
module_exit(h1940bt_exit);

MODULE_AUTHOR("Arnaud Patard <arnaud.patard@rtp-net.org>");
MODULE_DESCRIPTION("Driver for the iPAQ H1940 bluetooth chip");
MODULE_LICENSE("GPL");
