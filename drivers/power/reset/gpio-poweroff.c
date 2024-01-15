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
#include <linux/property.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/reboot.h>

#define DEFAULT_TIMEOUT_MS 3000

struct gpio_poweroff {
	struct gpio_desc *reset_gpio;
	u32 timeout_ms;
	u32 active_delay_ms;
	u32 inactive_delay_ms;
};

static int gpio_poweroff_do_poweroff(struct sys_off_data *data)
{
	struct gpio_poweroff *gpio_poweroff = data->cb_data;

	/* drive it active, also inactive->active edge */
	gpiod_direction_output(gpio_poweroff->reset_gpio, 1);
	mdelay(gpio_poweroff->active_delay_ms);

	/* drive inactive, also active->inactive edge */
	gpiod_set_value_cansleep(gpio_poweroff->reset_gpio, 0);
	mdelay(gpio_poweroff->inactive_delay_ms);

	/* drive it active, also inactive->active edge */
	gpiod_set_value_cansleep(gpio_poweroff->reset_gpio, 1);

	/* give it some time */
	mdelay(gpio_poweroff->timeout_ms);

	WARN_ON(1);

	return NOTIFY_DONE;
}

static int gpio_poweroff_probe(struct platform_device *pdev)
{
	struct gpio_poweroff *gpio_poweroff;
	bool input = false;
	enum gpiod_flags flags;
	int priority = SYS_OFF_PRIO_DEFAULT;
	int ret;

	gpio_poweroff = devm_kzalloc(&pdev->dev, sizeof(*gpio_poweroff), GFP_KERNEL);
	if (!gpio_poweroff)
		return -ENOMEM;

	input = device_property_read_bool(&pdev->dev, "input");
	if (input)
		flags = GPIOD_IN;
	else
		flags = GPIOD_OUT_LOW;


	gpio_poweroff->active_delay_ms = 100;
	gpio_poweroff->inactive_delay_ms = 100;
	gpio_poweroff->timeout_ms = DEFAULT_TIMEOUT_MS;

	device_property_read_u32(&pdev->dev, "active-delay-ms", &gpio_poweroff->active_delay_ms);
	device_property_read_u32(&pdev->dev, "inactive-delay-ms",
				 &gpio_poweroff->inactive_delay_ms);
	device_property_read_u32(&pdev->dev, "timeout-ms", &gpio_poweroff->timeout_ms);
	device_property_read_u32(&pdev->dev, "priority", &priority);
	if (priority > 255) {
		dev_err(&pdev->dev, "Invalid priority property: %u\n", priority);
		return -EINVAL;
	}

	gpio_poweroff->reset_gpio = devm_gpiod_get(&pdev->dev, NULL, flags);
	if (IS_ERR(gpio_poweroff->reset_gpio))
		return PTR_ERR(gpio_poweroff->reset_gpio);

	ret = devm_register_sys_off_handler(&pdev->dev, SYS_OFF_MODE_POWER_OFF,
					    priority, gpio_poweroff_do_poweroff, gpio_poweroff);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Cannot register poweroff handler\n");

	return 0;
}

static const struct of_device_id of_gpio_poweroff_match[] = {
	{ .compatible = "gpio-poweroff", },
	{},
};
MODULE_DEVICE_TABLE(of, of_gpio_poweroff_match);

static struct platform_driver gpio_poweroff_driver = {
	.probe = gpio_poweroff_probe,
	.driver = {
		.name = "poweroff-gpio",
		.of_match_table = of_gpio_poweroff_match,
	},
};

module_platform_driver(gpio_poweroff_driver);

MODULE_AUTHOR("Jamie Lentin <jm@lentin.co.uk>");
MODULE_DESCRIPTION("GPIO poweroff driver");
MODULE_ALIAS("platform:poweroff-gpio");
