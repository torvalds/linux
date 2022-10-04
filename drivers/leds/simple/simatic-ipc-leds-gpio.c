// SPDX-License-Identifier: GPL-2.0
/*
 * Siemens SIMATIC IPC driver for GPIO based LEDs
 *
 * Copyright (c) Siemens AG, 2022
 *
 * Authors:
 *  Henning Schild <henning.schild@siemens.com>
 */

#include <linux/gpio/machine.h>
#include <linux/gpio/consumer.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>

static struct gpiod_lookup_table simatic_ipc_led_gpio_table = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 51, NULL, 0, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 52, NULL, 1, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 53, NULL, 2, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 57, NULL, 3, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 58, NULL, 4, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 60, NULL, 5, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 56, NULL, 6, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 59, NULL, 7, GPIO_ACTIVE_HIGH),
	},
};

static const struct gpio_led simatic_ipc_gpio_leds[] = {
	{ .name = "green:" LED_FUNCTION_STATUS "-3" },
	{ .name = "red:" LED_FUNCTION_STATUS "-1" },
	{ .name = "green:" LED_FUNCTION_STATUS "-1" },
	{ .name = "red:" LED_FUNCTION_STATUS "-2" },
	{ .name = "green:" LED_FUNCTION_STATUS "-2" },
	{ .name = "red:" LED_FUNCTION_STATUS "-3" },
};

static const struct gpio_led_platform_data simatic_ipc_gpio_leds_pdata = {
	.num_leds	= ARRAY_SIZE(simatic_ipc_gpio_leds),
	.leds		= simatic_ipc_gpio_leds,
};

static struct platform_device *simatic_leds_pdev;

static int simatic_ipc_leds_gpio_remove(struct platform_device *pdev)
{
	gpiod_remove_lookup_table(&simatic_ipc_led_gpio_table);
	platform_device_unregister(simatic_leds_pdev);

	return 0;
}

static int simatic_ipc_leds_gpio_probe(struct platform_device *pdev)
{
	struct gpio_desc *gpiod;
	int err;

	gpiod_add_lookup_table(&simatic_ipc_led_gpio_table);
	simatic_leds_pdev = platform_device_register_resndata(NULL,
		"leds-gpio", PLATFORM_DEVID_NONE, NULL, 0,
		&simatic_ipc_gpio_leds_pdata,
		sizeof(simatic_ipc_gpio_leds_pdata));
	if (IS_ERR(simatic_leds_pdev)) {
		err = PTR_ERR(simatic_leds_pdev);
		goto out;
	}

	/* PM_BIOS_BOOT_N */
	gpiod = gpiod_get_index(&simatic_leds_pdev->dev, NULL, 6, GPIOD_OUT_LOW);
	if (IS_ERR(gpiod)) {
		err = PTR_ERR(gpiod);
		goto out;
	}
	gpiod_put(gpiod);

	/* PM_WDT_OUT */
	gpiod = gpiod_get_index(&simatic_leds_pdev->dev, NULL, 7, GPIOD_OUT_LOW);
	if (IS_ERR(gpiod)) {
		err = PTR_ERR(gpiod);
		goto out;
	}
	gpiod_put(gpiod);

	return 0;
out:
	simatic_ipc_leds_gpio_remove(pdev);

	return err;
}

static struct platform_driver simatic_ipc_led_gpio_driver = {
	.probe = simatic_ipc_leds_gpio_probe,
	.remove = simatic_ipc_leds_gpio_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	}
};
module_platform_driver(simatic_ipc_led_gpio_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_SOFTDEP("pre: platform:leds-gpio");
MODULE_AUTHOR("Henning Schild <henning.schild@siemens.com>");
