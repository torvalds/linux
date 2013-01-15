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
#include <mach/h1940.h>

#define DRV_NAME "h1940-bt"

/* Bluetooth control */
static void h1940bt_enable(int on)
{
	if (on) {
		/* Power on the chip */
		gpio_set_value(H1940_LATCH_BLUETOOTH_POWER, 1);
		/* Reset the chip */
		mdelay(10);

		gpio_set_value(S3C2410_GPH(1), 1);
		mdelay(10);
		gpio_set_value(S3C2410_GPH(1), 0);

		h1940_led_blink_set(-EINVAL, GPIO_LED_BLINK, NULL, NULL);
	}
	else {
		gpio_set_value(S3C2410_GPH(1), 1);
		mdelay(10);
		gpio_set_value(S3C2410_GPH(1), 0);
		mdelay(10);
		gpio_set_value(H1940_LATCH_BLUETOOTH_POWER, 0);

		h1940_led_blink_set(-EINVAL, GPIO_LED_NO_BLINK_LOW, NULL, NULL);
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

static int h1940bt_probe(struct platform_device *pdev)
{
	struct rfkill *rfk;
	int ret = 0;

	ret = gpio_request(S3C2410_GPH(1), dev_name(&pdev->dev));
	if (ret) {
		dev_err(&pdev->dev, "could not get GPH1\n");
		return ret;
	}

	ret = gpio_request(H1940_LATCH_BLUETOOTH_POWER, dev_name(&pdev->dev));
	if (ret) {
		gpio_free(S3C2410_GPH(1));
		dev_err(&pdev->dev, "could not get BT_POWER\n");
		return ret;
	}

	/* Configures BT serial port GPIOs */
	s3c_gpio_cfgpin(S3C2410_GPH(0), S3C2410_GPH0_nCTS0);
	s3c_gpio_setpull(S3C2410_GPH(0), S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(S3C2410_GPH(1), S3C2410_GPIO_OUTPUT);
	s3c_gpio_setpull(S3C2410_GPH(1), S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(S3C2410_GPH(2), S3C2410_GPH2_TXD0);
	s3c_gpio_setpull(S3C2410_GPH(2), S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(S3C2410_GPH(3), S3C2410_GPH3_RXD0);
	s3c_gpio_setpull(S3C2410_GPH(3), S3C_GPIO_PULL_NONE);

	rfk = rfkill_alloc(DRV_NAME, &pdev->dev, RFKILL_TYPE_BLUETOOTH,
			&h1940bt_rfkill_ops, NULL);
	if (!rfk) {
		ret = -ENOMEM;
		goto err_rfk_alloc;
	}

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
	gpio_free(S3C2410_GPH(1));

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

module_platform_driver(h1940bt_driver);

MODULE_AUTHOR("Arnaud Patard <arnaud.patard@rtp-net.org>");
MODULE_DESCRIPTION("Driver for the iPAQ H1940 bluetooth chip");
MODULE_LICENSE("GPL");
