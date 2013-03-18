/*
 * Toggles a GPIO pin to power down a device
 *
 * Jamie Lentin <jm@lentin.co.uk>
 * Andrew Lunn <andrew@lunn.ch>
 *
 * Copyright (C) 2012 Jamie Lentin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/module.h>

/*
 * Hold configuration here, cannot be more than one instance of the driver
 * since pm_power_off itself is global.
 */
static int gpio_num = -1;
static int gpio_active_low;

static void gpio_poweroff_do_poweroff(void)
{
	BUG_ON(!gpio_is_valid(gpio_num));

	/* drive it active, also inactive->active edge */
	gpio_direction_output(gpio_num, !gpio_active_low);
	mdelay(100);
	/* drive inactive, also active->inactive edge */
	gpio_set_value(gpio_num, gpio_active_low);
	mdelay(100);

	/* drive it active, also inactive->active edge */
	gpio_set_value(gpio_num, !gpio_active_low);

	/* give it some time */
	mdelay(3000);

	WARN_ON(1);
}

static int gpio_poweroff_probe(struct platform_device *pdev)
{
	enum of_gpio_flags flags;
	bool input = false;
	int ret;

	/* If a pm_power_off function has already been added, leave it alone */
	if (pm_power_off != NULL) {
		pr_err("%s: pm_power_off function already registered",
		       __func__);
		return -EBUSY;
	}

	gpio_num = of_get_gpio_flags(pdev->dev.of_node, 0, &flags);
	if (!gpio_is_valid(gpio_num))
		return gpio_num;

	gpio_active_low = flags & OF_GPIO_ACTIVE_LOW;

	input = of_property_read_bool(pdev->dev.of_node, "input");

	ret = gpio_request(gpio_num, "poweroff-gpio");
	if (ret) {
		pr_err("%s: Could not get GPIO %d", __func__, gpio_num);
		return ret;
	}
	if (input) {
		if (gpio_direction_input(gpio_num)) {
			pr_err("Could not set direction of GPIO %d to input",
			       gpio_num);
			goto err;
		}
	} else {
		if (gpio_direction_output(gpio_num, gpio_active_low)) {
			pr_err("Could not set direction of GPIO %d", gpio_num);
			goto err;
		}
	}

	pm_power_off = &gpio_poweroff_do_poweroff;
	return 0;

err:
	gpio_free(gpio_num);
	return -ENODEV;
}

static int gpio_poweroff_remove(struct platform_device *pdev)
{
	gpio_free(gpio_num);
	if (pm_power_off == &gpio_poweroff_do_poweroff)
		pm_power_off = NULL;

	return 0;
}

static const struct of_device_id of_gpio_poweroff_match[] = {
	{ .compatible = "gpio-poweroff", },
	{},
};

static struct platform_driver gpio_poweroff_driver = {
	.probe = gpio_poweroff_probe,
	.remove = gpio_poweroff_remove,
	.driver = {
		.name = "poweroff-gpio",
		.owner = THIS_MODULE,
		.of_match_table = of_gpio_poweroff_match,
	},
};

module_platform_driver(gpio_poweroff_driver);

MODULE_AUTHOR("Jamie Lentin <jm@lentin.co.uk>");
MODULE_DESCRIPTION("GPIO poweroff driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:poweroff-gpio");
