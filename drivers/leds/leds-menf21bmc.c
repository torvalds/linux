/*
 *  MEN 14F021P00 Board Management Controller (BMC) LEDs Driver.
 *
 *  This is the core LED driver of the MEN 14F021P00 BMC.
 *  There are four LEDs available which can be switched on and off.
 *  STATUS LED, HOT SWAP LED, USER LED 1, USER LED 2
 *
 *  Copyright (C) 2014 MEN Mikro Elektronik Nuernberg GmbH
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/i2c.h>

#define BMC_CMD_LED_GET_SET	0xA0
#define BMC_BIT_LED_STATUS	BIT(0)
#define BMC_BIT_LED_HOTSWAP	BIT(1)
#define BMC_BIT_LED_USER1	BIT(2)
#define BMC_BIT_LED_USER2	BIT(3)

struct menf21bmc_led {
	struct led_classdev cdev;
	u8 led_bit;
	const char *name;
	struct i2c_client *i2c_client;
};

static struct menf21bmc_led leds[] = {
	{
		.name = "menf21bmc:led_status",
		.led_bit = BMC_BIT_LED_STATUS,
	},
	{
		.name = "menf21bmc:led_hotswap",
		.led_bit = BMC_BIT_LED_HOTSWAP,
	},
	{
		.name = "menf21bmc:led_user1",
		.led_bit = BMC_BIT_LED_USER1,
	},
	{
		.name = "menf21bmc:led_user2",
		.led_bit = BMC_BIT_LED_USER2,
	}
};

static DEFINE_MUTEX(led_lock);

static void
menf21bmc_led_set(struct led_classdev *led_cdev, enum led_brightness value)
{
	int led_val;
	struct menf21bmc_led *led = container_of(led_cdev,
					struct menf21bmc_led, cdev);

	mutex_lock(&led_lock);
	led_val = i2c_smbus_read_byte_data(led->i2c_client,
					   BMC_CMD_LED_GET_SET);
	if (led_val < 0)
		goto err_out;

	if (value == LED_OFF)
		led_val &= ~led->led_bit;
	else
		led_val |= led->led_bit;

	i2c_smbus_write_byte_data(led->i2c_client,
				  BMC_CMD_LED_GET_SET, led_val);
err_out:
	mutex_unlock(&led_lock);
}

static int menf21bmc_led_probe(struct platform_device *pdev)
{
	int i;
	int ret;
	struct i2c_client *i2c_client = to_i2c_client(pdev->dev.parent);

	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		leds[i].cdev.name = leds[i].name;
		leds[i].cdev.brightness_set = menf21bmc_led_set;
		leds[i].i2c_client = i2c_client;
		ret = led_classdev_register(&pdev->dev, &leds[i].cdev);
		if (ret < 0)
			goto err_free_leds;
	}
	dev_info(&pdev->dev, "MEN 140F21P00 BMC LED device enabled\n");

	return 0;

err_free_leds:
	dev_err(&pdev->dev, "failed to register LED device\n");

	for (i = i - 1; i >= 0; i--)
		led_classdev_unregister(&leds[i].cdev);

	return ret;
}

static int menf21bmc_led_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(leds); i++)
		led_classdev_unregister(&leds[i].cdev);

	return 0;
}

static struct platform_driver menf21bmc_led = {
	.probe		= menf21bmc_led_probe,
	.remove		= menf21bmc_led_remove,
	.driver		= {
		.name		= "menf21bmc_led",
		.owner		= THIS_MODULE,
	},
};

module_platform_driver(menf21bmc_led);

MODULE_AUTHOR("Andreas Werner <andreas.werner@men.de>");
MODULE_DESCRIPTION("MEN 14F021P00 BMC led driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:menf21bmc_led");
