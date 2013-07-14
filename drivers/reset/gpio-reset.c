/*
 * GPIO Reset Controller driver
 *
 * Copyright 2013 Philipp Zabel, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>

struct gpio_reset_data {
	struct reset_controller_dev rcdev;
	unsigned int gpio;
	bool active_low;
	s32 delay_us;
};

static void gpio_reset_set(struct reset_controller_dev *rcdev, int asserted)
{
	struct gpio_reset_data *drvdata = container_of(rcdev,
			struct gpio_reset_data, rcdev);
	int value = asserted;

	if (drvdata->active_low)
		value = !value;

	gpio_set_value(drvdata->gpio, value);
}

static int gpio_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct gpio_reset_data *drvdata = container_of(rcdev,
			struct gpio_reset_data, rcdev);

	if (drvdata->delay_us < 0)
		return -ENOSYS;

	gpio_reset_set(rcdev, 1);
	udelay(drvdata->delay_us);
	gpio_reset_set(rcdev, 0);

	return 0;
}

static int gpio_reset_assert(struct reset_controller_dev *rcdev,
		unsigned long id)
{
	gpio_reset_set(rcdev, 1);

	return 0;
}

static int gpio_reset_deassert(struct reset_controller_dev *rcdev,
		unsigned long id)
{
	gpio_reset_set(rcdev, 0);

	return 0;
}

static struct reset_control_ops gpio_reset_ops = {
	.reset = gpio_reset,
	.assert = gpio_reset_assert,
	.deassert = gpio_reset_deassert,
};

static int of_gpio_reset_xlate(struct reset_controller_dev *rcdev,
			       const struct of_phandle_args *reset_spec)
{
	if (WARN_ON(reset_spec->args_count != 0))
		return -EINVAL;

	return 0;
}

static int gpio_reset_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct gpio_reset_data *drvdata;
	enum of_gpio_flags flags;
	unsigned long gpio_flags;
	bool initially_in_reset;
	int ret;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (drvdata == NULL)
		return -ENOMEM;

	if (of_gpio_named_count(np, "reset-gpios") != 1) {
		dev_err(&pdev->dev,
			"reset-gpios property missing, or not a single gpio\n");
		return -EINVAL;
	}

	drvdata->gpio = of_get_named_gpio_flags(np, "reset-gpios", 0, &flags);
	if (drvdata->gpio == -EPROBE_DEFER) {
		return drvdata->gpio;
	} else if (!gpio_is_valid(drvdata->gpio)) {
		dev_err(&pdev->dev, "invalid reset gpio: %d\n", drvdata->gpio);
		return drvdata->gpio;
	}

	drvdata->active_low = flags & OF_GPIO_ACTIVE_LOW;

	ret = of_property_read_u32(np, "reset-delay-us", &drvdata->delay_us);
	if (ret < 0)
		drvdata->delay_us = -1;
	else if (drvdata->delay_us < 0)
		dev_warn(&pdev->dev, "reset delay too high\n");

	initially_in_reset = of_property_read_bool(np, "initially-in-reset");
	if (drvdata->active_low ^ initially_in_reset)
		gpio_flags = GPIOF_OUT_INIT_HIGH;
	else
		gpio_flags = GPIOF_OUT_INIT_LOW;

	ret = devm_gpio_request_one(&pdev->dev, drvdata->gpio, gpio_flags, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request gpio %d: %d\n",
			drvdata->gpio, ret);
		return ret;
	}

	platform_set_drvdata(pdev, drvdata);

	drvdata->rcdev.of_node = np;
	drvdata->rcdev.owner = THIS_MODULE;
	drvdata->rcdev.nr_resets = 1;
	drvdata->rcdev.ops = &gpio_reset_ops;
	drvdata->rcdev.of_xlate = of_gpio_reset_xlate;
	reset_controller_register(&drvdata->rcdev);

	return 0;
}

static int gpio_reset_remove(struct platform_device *pdev)
{
	struct gpio_reset_data *drvdata = platform_get_drvdata(pdev);

	reset_controller_unregister(&drvdata->rcdev);

	return 0;
}

static struct of_device_id gpio_reset_dt_ids[] = {
	{ .compatible = "gpio-reset" },
	{ }
};

static struct platform_driver gpio_reset_driver = {
	.probe = gpio_reset_probe,
	.remove = gpio_reset_remove,
	.driver = {
		.name = "gpio-reset",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(gpio_reset_dt_ids),
	},
};

static int __init gpio_reset_init(void)
{
	return platform_driver_register(&gpio_reset_driver);
}
arch_initcall(gpio_reset_init);

static void __exit gpio_reset_exit(void)
{
	platform_driver_unregister(&gpio_reset_driver);
}
module_exit(gpio_reset_exit);

MODULE_AUTHOR("Philipp Zabel <p.zabel@pengutronix.de>");
MODULE_DESCRIPTION("gpio reset controller");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-reset");
MODULE_DEVICE_TABLE(of, gpio_reset_dt_ids);
