/*
 * Copyright 2011 Texas Instruments Inc.
 *
 * Author: Margarita Olaya <magi@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 * This driver is based on wm8350 implementation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/mfd/core.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/mfd/tps65912.h>

struct tps65912_gpio_data {
	struct tps65912 *tps65912;
	struct gpio_chip gpio_chip;
};

#define to_tgd(gc) container_of(gc, struct tps65912_gpio_data, gpio_chip)

static int tps65912_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct tps65912_gpio_data *tps65912_gpio = to_tgd(gc);
	struct tps65912 *tps65912 = tps65912_gpio->tps65912;
	int val;

	val = tps65912_reg_read(tps65912, TPS65912_GPIO1 + offset);

	if (val & GPIO_STS_MASK)
		return 1;

	return 0;
}

static void tps65912_gpio_set(struct gpio_chip *gc, unsigned offset,
			      int value)
{
	struct tps65912_gpio_data *tps65912_gpio = to_tgd(gc);
	struct tps65912 *tps65912 = tps65912_gpio->tps65912;

	if (value)
		tps65912_set_bits(tps65912, TPS65912_GPIO1 + offset,
							GPIO_SET_MASK);
	else
		tps65912_clear_bits(tps65912, TPS65912_GPIO1 + offset,
								GPIO_SET_MASK);
}

static int tps65912_gpio_output(struct gpio_chip *gc, unsigned offset,
				int value)
{
	struct tps65912_gpio_data *tps65912_gpio = to_tgd(gc);
	struct tps65912 *tps65912 = tps65912_gpio->tps65912;

	/* Set the initial value */
	tps65912_gpio_set(gc, offset, value);

	return tps65912_set_bits(tps65912, TPS65912_GPIO1 + offset,
								GPIO_CFG_MASK);
}

static int tps65912_gpio_input(struct gpio_chip *gc, unsigned offset)
{
	struct tps65912_gpio_data *tps65912_gpio = to_tgd(gc);
	struct tps65912 *tps65912 = tps65912_gpio->tps65912;

	return tps65912_clear_bits(tps65912, TPS65912_GPIO1 + offset,
								GPIO_CFG_MASK);
}

static struct gpio_chip template_chip = {
	.label			= "tps65912",
	.owner			= THIS_MODULE,
	.direction_input	= tps65912_gpio_input,
	.direction_output	= tps65912_gpio_output,
	.get			= tps65912_gpio_get,
	.set			= tps65912_gpio_set,
	.can_sleep		= true,
	.ngpio			= 5,
	.base			= -1,
};

static int tps65912_gpio_probe(struct platform_device *pdev)
{
	struct tps65912 *tps65912 = dev_get_drvdata(pdev->dev.parent);
	struct tps65912_board *pdata = dev_get_platdata(tps65912->dev);
	struct tps65912_gpio_data *tps65912_gpio;
	int ret;

	tps65912_gpio = devm_kzalloc(&pdev->dev, sizeof(*tps65912_gpio),
				     GFP_KERNEL);
	if (tps65912_gpio == NULL)
		return -ENOMEM;

	tps65912_gpio->tps65912 = tps65912;
	tps65912_gpio->gpio_chip = template_chip;
	tps65912_gpio->gpio_chip.parent = &pdev->dev;
	if (pdata && pdata->gpio_base)
		tps65912_gpio->gpio_chip.base = pdata->gpio_base;

	ret = gpiochip_add(&tps65912_gpio->gpio_chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register gpiochip, %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, tps65912_gpio);

	return ret;
}

static int tps65912_gpio_remove(struct platform_device *pdev)
{
	struct tps65912_gpio_data  *tps65912_gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&tps65912_gpio->gpio_chip);
	return 0;
}

static struct platform_driver tps65912_gpio_driver = {
	.driver = {
		.name = "tps65912-gpio",
	},
	.probe = tps65912_gpio_probe,
	.remove = tps65912_gpio_remove,
};

static int __init tps65912_gpio_init(void)
{
	return platform_driver_register(&tps65912_gpio_driver);
}
subsys_initcall(tps65912_gpio_init);

static void __exit tps65912_gpio_exit(void)
{
	platform_driver_unregister(&tps65912_gpio_driver);
}
module_exit(tps65912_gpio_exit);

MODULE_AUTHOR("Margarita Olaya Cabrera <magi@slimlogic.co.uk>");
MODULE_DESCRIPTION("GPIO interface for TPS65912 PMICs");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tps65912-gpio");
