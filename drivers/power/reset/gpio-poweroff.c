// SPDX-License-Identifier: GPL-2.0-only
/*
 * Toggles a GPIO pin to power down a device
 *
 * Jamie Lentin <jm@lentin.co.uk>
 * Andrew Lunn <andrew@lunn.ch>
 *
 * Copyright (C) 2012 Jamie Lentin
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>
#include <linux/module.h>

#define DEFAULT_TIMEOUT_MS 3000
/*
 * Hold configuration here, cannot be more than one instance of the driver
 * since pm_power_off itself is global.
 */
static struct gpio_desc *reset_gpio;
static u32 timeout = DEFAULT_TIMEOUT_MS;
static u32 active_delay = 100;
static u32 inactive_delay = 100;

static void gpio_poweroff_do_poweroff(void)
{
	BUG_ON(!reset_gpio);

	/* drive it active, also inactive->active edge */
	gpiod_direction_output(reset_gpio, 1);
	mdelay(active_delay);

	/* drive inactive, also active->inactive edge */
	gpiod_set_value_cansleep(reset_gpio, 0);
	mdelay(inactive_delay);

	/* drive it active, also inactive->active edge */
	gpiod_set_value_cansleep(reset_gpio, 1);

	/* give it some time */
	mdelay(timeout);

	WARN_ON(1);
}

static int gpio_poweroff_probe(struct platform_device *pdev)
{
	bool input = false;
	enum gpiod_flags flags;

	/* If a pm_power_off function has already been added, leave it alone */
	if (pm_power_off != NULL) {
		dev_err(&pdev->dev,
			"%s: pm_power_off function already registered\n",
		       __func__);
		return -EBUSY;
	}

	input = device_property_read_bool(&pdev->dev, "input");
	if (input)
		flags = GPIOD_IN;
	else
		flags = GPIOD_OUT_LOW;

	device_property_read_u32(&pdev->dev, "active-delay-ms", &active_delay);
	device_property_read_u32(&pdev->dev, "inactive-delay-ms",
				 &inactive_delay);
	device_property_read_u32(&pdev->dev, "timeout-ms", &timeout);

	reset_gpio = devm_gpiod_get(&pdev->dev, NULL, flags);
	if (IS_ERR(reset_gpio))
		return PTR_ERR(reset_gpio);

	pm_power_off = &gpio_poweroff_do_poweroff;
	return 0;
}

static int gpio_poweroff_remove(struct platform_device *pdev)
{
	if (pm_power_off == &gpio_poweroff_do_poweroff)
		pm_power_off = NULL;

	return 0;
}

static const struct of_device_id of_gpio_poweroff_match[] = {
	{ .compatible = "gpio-poweroff", },
	{},
};
MODULE_DEVICE_TABLE(of, of_gpio_poweroff_match);

static struct platform_driver gpio_poweroff_driver = {
	.probe = gpio_poweroff_probe,
	.remove = gpio_poweroff_remove,
	.driver = {
		.name = "poweroff-gpio",
		.of_match_table = of_gpio_poweroff_match,
	},
};

module_platform_driver(gpio_poweroff_driver);

MODULE_AUTHOR("Jamie Lentin <jm@lentin.co.uk>");
MODULE_DESCRIPTION("GPIO poweroff driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:poweroff-gpio");
