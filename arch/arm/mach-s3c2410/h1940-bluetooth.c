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
#include <linux/gpio.h>
#include <linux/rfkill.h>

#include <mach/regs-gpio.h>
#include <mach/hardware.h>
#include <mach/h1940-latch.h>

#define DRV_NAME              "h1940-bt"

/* Bluetooth control */
static void h1940bt_enable(int on)
{
	if (on) {
		/* Power on the chip */
		h1940_latch_control(0, H1940_LATCH_BLUETOOTH_POWER);
		/* Reset the chip */
		mdelay(10);
		s3c2410_gpio_setpin(S3C2410_GPH(1), 1);
		mdelay(10);
		s3c2410_gpio_setpin(S3C2410_GPH(1), 0);
	}
	else {
		s3c2410_gpio_setpin(S3C2410_GPH(1), 1);
		mdelay(10);
		s3c2410_gpio_setpin(S3C2410_GPH(1), 0);
		mdelay(10);
		h1940_latch_control(H1940_LATCH_BLUETOOTH_POWER, 0);
	}
}

static int h1940bt_set_block(void *data, bool blocked)
{
	h1940bt_enable(!blocked);
	return 0;
}

static const struct rfkill_ops h1940bt_rfkill_ops = {
	.set_block = h1940bt_set_block,
};

static int __devinit h1940bt_probe(struct platform_device *pdev)
{
	struct rfkill *rfk;
	int ret = 0;

	/* Configures BT serial port GPIOs */
	s3c2410_gpio_cfgpin(S3C2410_GPH(0), S3C2410_GPH0_nCTS0);
	s3c2410_gpio_pullup(S3C2410_GPH(0), 1);
	s3c2410_gpio_cfgpin(S3C2410_GPH(1), S3C2410_GPIO_OUTPUT);
	s3c2410_gpio_pullup(S3C2410_GPH(1), 1);
	s3c2410_gpio_cfgpin(S3C2410_GPH(2), S3C2410_GPH2_TXD0);
	s3c2410_gpio_pullup(S3C2410_GPH(2), 1);
	s3c2410_gpio_cfgpin(S3C2410_GPH(3), S3C2410_GPH3_RXD0);
	s3c2410_gpio_pullup(S3C2410_GPH(3), 1);


	rfk = rfkill_alloc(DRV_NAME, &pdev->dev, RFKILL_TYPE_BLUETOOTH,
			&h1940bt_rfkill_ops, NULL);
	if (!rfk) {
		ret = -ENOMEM;
		goto err_rfk_alloc;
	}

	rfkill_set_led_trigger_name(rfk, "h1940-bluetooth");

	ret = rfkill_register(rfk);
	if (ret)
		goto err_rfkill;

	platform_set_drvdata(pdev, rfk);

	return 0;

err_rfkill:
	rfkill_destroy(rfk);
err_rfk_alloc:
	return ret;
}

static int h1940bt_remove(struct platform_device *pdev)
{
	struct rfkill *rfk = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (rfk) {
		rfkill_unregister(rfk);
		rfkill_destroy(rfk);
	}
	rfk = NULL;

	h1940bt_enable(0);

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
