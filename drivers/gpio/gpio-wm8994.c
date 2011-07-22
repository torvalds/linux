/*
 * gpiolib support for Wolfson WM8994
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
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/mfd/core.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#include <linux/mfd/wm8994/core.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/mfd/wm8994/gpio.h>
#include <linux/mfd/wm8994/registers.h>

struct wm8994_gpio {
	struct wm8994 *wm8994;
	struct gpio_chip gpio_chip;
};

static inline struct wm8994_gpio *to_wm8994_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct wm8994_gpio, gpio_chip);
}

static int wm8994_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct wm8994_gpio *wm8994_gpio = to_wm8994_gpio(chip);
	struct wm8994 *wm8994 = wm8994_gpio->wm8994;

	switch (wm8994->type) {
	case WM8958:
		switch (offset) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 6:
			return -EINVAL;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int wm8994_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct wm8994_gpio *wm8994_gpio = to_wm8994_gpio(chip);
	struct wm8994 *wm8994 = wm8994_gpio->wm8994;

	return wm8994_set_bits(wm8994, WM8994_GPIO_1 + offset,
			       WM8994_GPN_DIR, WM8994_GPN_DIR);
}

static int wm8994_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct wm8994_gpio *wm8994_gpio = to_wm8994_gpio(chip);
	struct wm8994 *wm8994 = wm8994_gpio->wm8994;
	int ret;

	ret = wm8994_reg_read(wm8994, WM8994_GPIO_1 + offset);
	if (ret < 0)
		return ret;

	if (ret & WM8994_GPN_LVL)
		return 1;
	else
		return 0;
}

static int wm8994_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct wm8994_gpio *wm8994_gpio = to_wm8994_gpio(chip);
	struct wm8994 *wm8994 = wm8994_gpio->wm8994;

	return wm8994_set_bits(wm8994, WM8994_GPIO_1 + offset,
			       WM8994_GPN_DIR, 0);
}

static void wm8994_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct wm8994_gpio *wm8994_gpio = to_wm8994_gpio(chip);
	struct wm8994 *wm8994 = wm8994_gpio->wm8994;

	if (value)
		value = WM8994_GPN_LVL;

	wm8994_set_bits(wm8994, WM8994_GPIO_1 + offset, WM8994_GPN_LVL, value);
}

static int wm8994_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct wm8994_gpio *wm8994_gpio = to_wm8994_gpio(chip);
	struct wm8994 *wm8994 = wm8994_gpio->wm8994;

	if (!wm8994->irq_base)
		return -EINVAL;

	return wm8994->irq_base + offset;
}


#ifdef CONFIG_DEBUG_FS
static void wm8994_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct wm8994_gpio *wm8994_gpio = to_wm8994_gpio(chip);
	struct wm8994 *wm8994 = wm8994_gpio->wm8994;
	int i;

	for (i = 0; i < chip->ngpio; i++) {
		int gpio = i + chip->base;
		int reg;
		const char *label;

		/* We report the GPIO even if it's not requested since
		 * we're also reporting things like alternate
		 * functions which apply even when the GPIO is not in
		 * use as a GPIO.
		 */
		label = gpiochip_is_requested(chip, i);
		if (!label)
			label = "Unrequested";

		seq_printf(s, " gpio-%-3d (%-20.20s) ", gpio, label);

		reg = wm8994_reg_read(wm8994, WM8994_GPIO_1 + i);
		if (reg < 0) {
			dev_err(wm8994->dev,
				"GPIO control %d read failed: %d\n",
				gpio, reg);
			seq_printf(s, "\n");
			continue;
		}

		/* No decode yet; note that GPIO2 is special */
		seq_printf(s, "(%x)\n", reg);
	}
}
#else
#define wm8994_gpio_dbg_show NULL
#endif

static struct gpio_chip template_chip = {
	.label			= "wm8994",
	.owner			= THIS_MODULE,
	.request		= wm8994_gpio_request,
	.direction_input	= wm8994_gpio_direction_in,
	.get			= wm8994_gpio_get,
	.direction_output	= wm8994_gpio_direction_out,
	.set			= wm8994_gpio_set,
	.to_irq			= wm8994_gpio_to_irq,
	.dbg_show		= wm8994_gpio_dbg_show,
	.can_sleep		= 1,
};

static int __devinit wm8994_gpio_probe(struct platform_device *pdev)
{
	struct wm8994 *wm8994 = dev_get_drvdata(pdev->dev.parent);
	struct wm8994_pdata *pdata = wm8994->dev->platform_data;
	struct wm8994_gpio *wm8994_gpio;
	int ret;

	wm8994_gpio = kzalloc(sizeof(*wm8994_gpio), GFP_KERNEL);
	if (wm8994_gpio == NULL)
		return -ENOMEM;

	wm8994_gpio->wm8994 = wm8994;
	wm8994_gpio->gpio_chip = template_chip;
	wm8994_gpio->gpio_chip.ngpio = WM8994_GPIO_MAX;
	wm8994_gpio->gpio_chip.dev = &pdev->dev;
	if (pdata && pdata->gpio_base)
		wm8994_gpio->gpio_chip.base = pdata->gpio_base;
	else
		wm8994_gpio->gpio_chip.base = -1;

	ret = gpiochip_add(&wm8994_gpio->gpio_chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n",
			ret);
		goto err;
	}

	platform_set_drvdata(pdev, wm8994_gpio);

	return ret;

err:
	kfree(wm8994_gpio);
	return ret;
}

static int __devexit wm8994_gpio_remove(struct platform_device *pdev)
{
	struct wm8994_gpio *wm8994_gpio = platform_get_drvdata(pdev);
	int ret;

	ret = gpiochip_remove(&wm8994_gpio->gpio_chip);
	if (ret == 0)
		kfree(wm8994_gpio);

	return ret;
}

static struct platform_driver wm8994_gpio_driver = {
	.driver.name	= "wm8994-gpio",
	.driver.owner	= THIS_MODULE,
	.probe		= wm8994_gpio_probe,
	.remove		= __devexit_p(wm8994_gpio_remove),
};

static int __init wm8994_gpio_init(void)
{
	return platform_driver_register(&wm8994_gpio_driver);
}
subsys_initcall(wm8994_gpio_init);

static void __exit wm8994_gpio_exit(void)
{
	platform_driver_unregister(&wm8994_gpio_driver);
}
module_exit(wm8994_gpio_exit);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("GPIO interface for WM8994");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm8994-gpio");
