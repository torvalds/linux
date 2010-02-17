/*
 * wm831x-gpio.c  --  gpiolib support for Wolfson WM831x PMICs
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

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/pdata.h>
#include <linux/mfd/wm831x/gpio.h>
#include <linux/mfd/wm831x/irq.h>

struct wm831x_gpio {
	struct wm831x *wm831x;
	struct gpio_chip gpio_chip;
};

static inline struct wm831x_gpio *to_wm831x_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct wm831x_gpio, gpio_chip);
}

static int wm831x_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct wm831x_gpio *wm831x_gpio = to_wm831x_gpio(chip);
	struct wm831x *wm831x = wm831x_gpio->wm831x;
	int val = WM831X_GPN_DIR;

	if (wm831x->has_gpio_ena)
		val |= WM831X_GPN_TRI;

	return wm831x_set_bits(wm831x, WM831X_GPIO1_CONTROL + offset,
			       WM831X_GPN_DIR | WM831X_GPN_TRI |
			       WM831X_GPN_FN_MASK, val);
}

static int wm831x_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct wm831x_gpio *wm831x_gpio = to_wm831x_gpio(chip);
	struct wm831x *wm831x = wm831x_gpio->wm831x;
	int ret;

	ret = wm831x_reg_read(wm831x, WM831X_GPIO_LEVEL);
	if (ret < 0)
		return ret;

	if (ret & 1 << offset)
		return 1;
	else
		return 0;
}

static void wm831x_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct wm831x_gpio *wm831x_gpio = to_wm831x_gpio(chip);
	struct wm831x *wm831x = wm831x_gpio->wm831x;

	wm831x_set_bits(wm831x, WM831X_GPIO_LEVEL, 1 << offset,
			value << offset);
}

static int wm831x_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct wm831x_gpio *wm831x_gpio = to_wm831x_gpio(chip);
	struct wm831x *wm831x = wm831x_gpio->wm831x;
	int val = 0;
	int ret;

	if (wm831x->has_gpio_ena)
		val |= WM831X_GPN_TRI;

	ret = wm831x_set_bits(wm831x, WM831X_GPIO1_CONTROL + offset,
			      WM831X_GPN_DIR | WM831X_GPN_TRI |
			      WM831X_GPN_FN_MASK, val);
	if (ret < 0)
		return ret;

	/* Can only set GPIO state once it's in output mode */
	wm831x_gpio_set(chip, offset, value);

	return 0;
}

static int wm831x_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct wm831x_gpio *wm831x_gpio = to_wm831x_gpio(chip);
	struct wm831x *wm831x = wm831x_gpio->wm831x;

	if (!wm831x->irq_base)
		return -EINVAL;

	return wm831x->irq_base + WM831X_IRQ_GPIO_1 + offset;
}

#ifdef CONFIG_DEBUG_FS
static void wm831x_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct wm831x_gpio *wm831x_gpio = to_wm831x_gpio(chip);
	struct wm831x *wm831x = wm831x_gpio->wm831x;
	int i, tristated;

	for (i = 0; i < chip->ngpio; i++) {
		int gpio = i + chip->base;
		int reg;
		const char *label, *pull, *powerdomain;

		/* We report the GPIO even if it's not requested since
		 * we're also reporting things like alternate
		 * functions which apply even when the GPIO is not in
		 * use as a GPIO.
		 */
		label = gpiochip_is_requested(chip, i);
		if (!label)
			label = "Unrequested";

		seq_printf(s, " gpio-%-3d (%-20.20s) ", gpio, label);

		reg = wm831x_reg_read(wm831x, WM831X_GPIO1_CONTROL + i);
		if (reg < 0) {
			dev_err(wm831x->dev,
				"GPIO control %d read failed: %d\n",
				gpio, reg);
			seq_printf(s, "\n");
			continue;
		}

		switch (reg & WM831X_GPN_PULL_MASK) {
		case WM831X_GPIO_PULL_NONE:
			pull = "nopull";
			break;
		case WM831X_GPIO_PULL_DOWN:
			pull = "pulldown";
			break;
		case WM831X_GPIO_PULL_UP:
			pull = "pullup";
		default:
			pull = "INVALID PULL";
			break;
		}

		switch (i + 1) {
		case 1 ... 3:
		case 7 ... 9:
			if (reg & WM831X_GPN_PWR_DOM)
				powerdomain = "VPMIC";
			else
				powerdomain = "DBVDD";
			break;

		case 4 ... 6:
		case 10 ... 12:
			if (reg & WM831X_GPN_PWR_DOM)
				powerdomain = "SYSVDD";
			else
				powerdomain = "DBVDD";
			break;

		case 13 ... 16:
			powerdomain = "TPVDD";
			break;

		default:
			BUG();
			break;
		}

		tristated = reg & WM831X_GPN_TRI;
		if (wm831x->has_gpio_ena)
			tristated = !tristated;

		seq_printf(s, " %s %s %s %s%s\n"
			   "                                  %s%s (0x%4x)\n",
			   reg & WM831X_GPN_DIR ? "in" : "out",
			   wm831x_gpio_get(chip, i) ? "high" : "low",
			   pull,
			   powerdomain,
			   reg & WM831X_GPN_POL ? "" : " inverted",
			   reg & WM831X_GPN_OD ? "open-drain" : "CMOS",
			   tristated ? " tristated" : "",
			   reg);
	}
}
#else
#define wm831x_gpio_dbg_show NULL
#endif

static struct gpio_chip template_chip = {
	.label			= "wm831x",
	.owner			= THIS_MODULE,
	.direction_input	= wm831x_gpio_direction_in,
	.get			= wm831x_gpio_get,
	.direction_output	= wm831x_gpio_direction_out,
	.set			= wm831x_gpio_set,
	.to_irq			= wm831x_gpio_to_irq,
	.dbg_show		= wm831x_gpio_dbg_show,
	.can_sleep		= 1,
};

static int __devinit wm831x_gpio_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	struct wm831x_gpio *wm831x_gpio;
	int ret;

	wm831x_gpio = kzalloc(sizeof(*wm831x_gpio), GFP_KERNEL);
	if (wm831x_gpio == NULL)
		return -ENOMEM;

	wm831x_gpio->wm831x = wm831x;
	wm831x_gpio->gpio_chip = template_chip;
	wm831x_gpio->gpio_chip.ngpio = wm831x->num_gpio;
	wm831x_gpio->gpio_chip.dev = &pdev->dev;
	if (pdata && pdata->gpio_base)
		wm831x_gpio->gpio_chip.base = pdata->gpio_base;
	else
		wm831x_gpio->gpio_chip.base = -1;

	ret = gpiochip_add(&wm831x_gpio->gpio_chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n",
			ret);
		goto err;
	}

	platform_set_drvdata(pdev, wm831x_gpio);

	return ret;

err:
	kfree(wm831x_gpio);
	return ret;
}

static int __devexit wm831x_gpio_remove(struct platform_device *pdev)
{
	struct wm831x_gpio *wm831x_gpio = platform_get_drvdata(pdev);
	int ret;

	ret = gpiochip_remove(&wm831x_gpio->gpio_chip);
	if (ret == 0)
		kfree(wm831x_gpio);

	return ret;
}

static struct platform_driver wm831x_gpio_driver = {
	.driver.name	= "wm831x-gpio",
	.driver.owner	= THIS_MODULE,
	.probe		= wm831x_gpio_probe,
	.remove		= __devexit_p(wm831x_gpio_remove),
};

static int __init wm831x_gpio_init(void)
{
	return platform_driver_register(&wm831x_gpio_driver);
}
subsys_initcall(wm831x_gpio_init);

static void __exit wm831x_gpio_exit(void)
{
	platform_driver_unregister(&wm831x_gpio_driver);
}
module_exit(wm831x_gpio_exit);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("GPIO interface for WM831x PMICs");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm831x-gpio");
