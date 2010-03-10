/*
 * wm835x-gpiolib.c  --  gpiolib support for Wolfson WM835x PMICs
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/mfd/core.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#include <linux/mfd/wm8350/core.h>
#include <linux/mfd/wm8350/gpio.h>

struct wm8350_gpio_data {
	struct wm8350 *wm8350;
	struct gpio_chip gpio_chip;
};

static inline struct wm8350_gpio_data *to_wm8350_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct wm8350_gpio_data, gpio_chip);
}

static int wm8350_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct wm8350_gpio_data *wm8350_gpio = to_wm8350_gpio(chip);
	struct wm8350 *wm8350 = wm8350_gpio->wm8350;

	return wm8350_set_bits(wm8350, WM8350_GPIO_CONFIGURATION_I_O,
			       1 << offset);
}

static int wm8350_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct wm8350_gpio_data *wm8350_gpio = to_wm8350_gpio(chip);
	struct wm8350 *wm8350 = wm8350_gpio->wm8350;
	int ret;

	ret = wm8350_reg_read(wm8350, WM8350_GPIO_LEVEL);
	if (ret < 0)
		return ret;

	if (ret & (1 << offset))
		return 1;
	else
		return 0;
}

static void wm8350_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct wm8350_gpio_data *wm8350_gpio = to_wm8350_gpio(chip);
	struct wm8350 *wm8350 = wm8350_gpio->wm8350;

	if (value)
		wm8350_set_bits(wm8350, WM8350_GPIO_LEVEL, 1 << offset);
	else
		wm8350_clear_bits(wm8350, WM8350_GPIO_LEVEL, 1 << offset);
}

static int wm8350_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct wm8350_gpio_data *wm8350_gpio = to_wm8350_gpio(chip);
	struct wm8350 *wm8350 = wm8350_gpio->wm8350;
	int ret;

	ret = wm8350_clear_bits(wm8350, WM8350_GPIO_CONFIGURATION_I_O,
				1 << offset);
	if (ret < 0)
		return ret;

	/* Don't have an atomic direction/value setup */
	wm8350_gpio_set(chip, offset, value);

	return 0;
}

static int wm8350_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct wm8350_gpio_data *wm8350_gpio = to_wm8350_gpio(chip);
	struct wm8350 *wm8350 = wm8350_gpio->wm8350;

	if (!wm8350->irq_base)
		return -EINVAL;

	return wm8350->irq_base + WM8350_IRQ_GPIO(offset);
}

static struct gpio_chip template_chip = {
	.label			= "wm8350",
	.owner			= THIS_MODULE,
	.direction_input	= wm8350_gpio_direction_in,
	.get			= wm8350_gpio_get,
	.direction_output	= wm8350_gpio_direction_out,
	.set			= wm8350_gpio_set,
	.to_irq			= wm8350_gpio_to_irq,
	.can_sleep		= 1,
};

static int __devinit wm8350_gpio_probe(struct platform_device *pdev)
{
	struct wm8350 *wm8350 = dev_get_drvdata(pdev->dev.parent);
	struct wm8350_platform_data *pdata = wm8350->dev->platform_data;
	struct wm8350_gpio_data *wm8350_gpio;
	int ret;

	wm8350_gpio = kzalloc(sizeof(*wm8350_gpio), GFP_KERNEL);
	if (wm8350_gpio == NULL)
		return -ENOMEM;

	wm8350_gpio->wm8350 = wm8350;
	wm8350_gpio->gpio_chip = template_chip;
	wm8350_gpio->gpio_chip.ngpio = 13;
	wm8350_gpio->gpio_chip.dev = &pdev->dev;
	if (pdata && pdata->gpio_base)
		wm8350_gpio->gpio_chip.base = pdata->gpio_base;
	else
		wm8350_gpio->gpio_chip.base = -1;

	ret = gpiochip_add(&wm8350_gpio->gpio_chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n",
			ret);
		goto err;
	}

	platform_set_drvdata(pdev, wm8350_gpio);

	return ret;

err:
	kfree(wm8350_gpio);
	return ret;
}

static int __devexit wm8350_gpio_remove(struct platform_device *pdev)
{
	struct wm8350_gpio_data *wm8350_gpio = platform_get_drvdata(pdev);
	int ret;

	ret = gpiochip_remove(&wm8350_gpio->gpio_chip);
	if (ret == 0)
		kfree(wm8350_gpio);

	return ret;
}

static struct platform_driver wm8350_gpio_driver = {
	.driver.name	= "wm8350-gpio",
	.driver.owner	= THIS_MODULE,
	.probe		= wm8350_gpio_probe,
	.remove		= __devexit_p(wm8350_gpio_remove),
};

static int __init wm8350_gpio_init(void)
{
	return platform_driver_register(&wm8350_gpio_driver);
}
subsys_initcall(wm8350_gpio_init);

static void __exit wm8350_gpio_exit(void)
{
	platform_driver_unregister(&wm8350_gpio_driver);
}
module_exit(wm8350_gpio_exit);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("GPIO interface for WM8350 PMICs");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm8350-gpio");
