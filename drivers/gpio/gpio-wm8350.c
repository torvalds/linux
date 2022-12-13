// SPDX-License-Identifier: GPL-2.0+
/*
 * gpiolib support for Wolfson WM835x PMICs
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 */

#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/mfd/wm8350/core.h>
#include <linux/mfd/wm8350/gpio.h>

struct wm8350_gpio_data {
	struct wm8350 *wm8350;
	struct gpio_chip gpio_chip;
};

static int wm8350_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct wm8350_gpio_data *wm8350_gpio = gpiochip_get_data(chip);
	struct wm8350 *wm8350 = wm8350_gpio->wm8350;

	return wm8350_set_bits(wm8350, WM8350_GPIO_CONFIGURATION_I_O,
			       1 << offset);
}

static int wm8350_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct wm8350_gpio_data *wm8350_gpio = gpiochip_get_data(chip);
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
	struct wm8350_gpio_data *wm8350_gpio = gpiochip_get_data(chip);
	struct wm8350 *wm8350 = wm8350_gpio->wm8350;

	if (value)
		wm8350_set_bits(wm8350, WM8350_GPIO_LEVEL, 1 << offset);
	else
		wm8350_clear_bits(wm8350, WM8350_GPIO_LEVEL, 1 << offset);
}

static int wm8350_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct wm8350_gpio_data *wm8350_gpio = gpiochip_get_data(chip);
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
	struct wm8350_gpio_data *wm8350_gpio = gpiochip_get_data(chip);
	struct wm8350 *wm8350 = wm8350_gpio->wm8350;

	if (!wm8350->irq_base)
		return -EINVAL;

	return wm8350->irq_base + WM8350_IRQ_GPIO(offset);
}

static const struct gpio_chip template_chip = {
	.label			= "wm8350",
	.owner			= THIS_MODULE,
	.direction_input	= wm8350_gpio_direction_in,
	.get			= wm8350_gpio_get,
	.direction_output	= wm8350_gpio_direction_out,
	.set			= wm8350_gpio_set,
	.to_irq			= wm8350_gpio_to_irq,
	.can_sleep		= true,
};

static int wm8350_gpio_probe(struct platform_device *pdev)
{
	struct wm8350 *wm8350 = dev_get_drvdata(pdev->dev.parent);
	struct wm8350_platform_data *pdata = dev_get_platdata(wm8350->dev);
	struct wm8350_gpio_data *wm8350_gpio;

	wm8350_gpio = devm_kzalloc(&pdev->dev, sizeof(*wm8350_gpio),
				   GFP_KERNEL);
	if (wm8350_gpio == NULL)
		return -ENOMEM;

	wm8350_gpio->wm8350 = wm8350;
	wm8350_gpio->gpio_chip = template_chip;
	wm8350_gpio->gpio_chip.ngpio = 13;
	wm8350_gpio->gpio_chip.parent = &pdev->dev;
	if (pdata && pdata->gpio_base)
		wm8350_gpio->gpio_chip.base = pdata->gpio_base;
	else
		wm8350_gpio->gpio_chip.base = -1;

	return devm_gpiochip_add_data(&pdev->dev, &wm8350_gpio->gpio_chip, wm8350_gpio);
}

static struct platform_driver wm8350_gpio_driver = {
	.driver.name	= "wm8350-gpio",
	.probe		= wm8350_gpio_probe,
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
