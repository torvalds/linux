// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022 TQ-Systems GmbH
 * Author: Alexander Stein <linux@ew.tq-group.com>
 */

#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include "gpiolib.h"

struct gpio_delay_timing {
	unsigned long ramp_up_delay_us;
	unsigned long ramp_down_delay_us;
};

struct gpio_delay_priv {
	struct gpio_chip gc;
	struct gpio_descs *input_gpio;
	struct gpio_delay_timing *delay_timings;
};

static int gpio_delay_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static void gpio_delay_set(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct gpio_delay_priv *priv = gpiochip_get_data(gc);
	struct gpio_desc *gpio_desc = priv->input_gpio->desc[offset];
	const struct gpio_delay_timing *delay_timings;
	bool ramp_up;

	gpiod_set_value(gpio_desc, val);

	delay_timings = &priv->delay_timings[offset];
	ramp_up = (!gpiod_is_active_low(gpio_desc) && val) ||
		  (gpiod_is_active_low(gpio_desc) && !val);

	if (ramp_up && delay_timings->ramp_up_delay_us)
		udelay(delay_timings->ramp_up_delay_us);
	if (!ramp_up && delay_timings->ramp_down_delay_us)
		udelay(delay_timings->ramp_down_delay_us);
}

static void gpio_delay_set_can_sleep(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct gpio_delay_priv *priv = gpiochip_get_data(gc);
	struct gpio_desc *gpio_desc = priv->input_gpio->desc[offset];
	const struct gpio_delay_timing *delay_timings;
	bool ramp_up;

	gpiod_set_value_cansleep(gpio_desc, val);

	delay_timings = &priv->delay_timings[offset];
	ramp_up = (!gpiod_is_active_low(gpio_desc) && val) ||
		  (gpiod_is_active_low(gpio_desc) && !val);

	if (ramp_up && delay_timings->ramp_up_delay_us)
		fsleep(delay_timings->ramp_up_delay_us);
	if (!ramp_up && delay_timings->ramp_down_delay_us)
		fsleep(delay_timings->ramp_down_delay_us);
}

static int gpio_delay_of_xlate(struct gpio_chip *gc,
			       const struct of_phandle_args *gpiospec,
			       u32 *flags)
{
	struct gpio_delay_priv *priv = gpiochip_get_data(gc);
	struct gpio_delay_timing *timings;
	u32 line;

	if (gpiospec->args_count != gc->of_gpio_n_cells)
		return -EINVAL;

	line = gpiospec->args[0];
	if (line >= gc->ngpio)
		return -EINVAL;

	timings = &priv->delay_timings[line];
	timings->ramp_up_delay_us = gpiospec->args[1];
	timings->ramp_down_delay_us = gpiospec->args[2];

	return line;
}

static bool gpio_delay_can_sleep(const struct gpio_delay_priv *priv)
{
	int i;

	for (i = 0; i < priv->input_gpio->ndescs; i++)
		if (gpiod_cansleep(priv->input_gpio->desc[i]))
			return true;

	return false;
}

static int gpio_delay_probe(struct platform_device *pdev)
{
	struct gpio_delay_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->input_gpio = devm_gpiod_get_array(&pdev->dev, NULL, GPIOD_OUT_LOW);
	if (IS_ERR(priv->input_gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->input_gpio),
				     "Failed to get input-gpios");

	priv->delay_timings = devm_kcalloc(&pdev->dev,
					   priv->input_gpio->ndescs,
					   sizeof(*priv->delay_timings),
					   GFP_KERNEL);
	if (!priv->delay_timings)
		return -ENOMEM;

	if (gpio_delay_can_sleep(priv)) {
		priv->gc.can_sleep = true;
		priv->gc.set = gpio_delay_set_can_sleep;
	} else {
		priv->gc.can_sleep = false;
		priv->gc.set = gpio_delay_set;
	}

	priv->gc.get_direction = gpio_delay_get_direction;
	priv->gc.of_xlate = gpio_delay_of_xlate;
	priv->gc.of_gpio_n_cells = 3;
	priv->gc.ngpio = priv->input_gpio->ndescs;
	priv->gc.owner = THIS_MODULE;
	priv->gc.base = -1;
	priv->gc.parent = &pdev->dev;

	platform_set_drvdata(pdev, priv);

	return devm_gpiochip_add_data(&pdev->dev, &priv->gc, priv);
}

static const struct of_device_id gpio_delay_ids[] = {
	{
		.compatible = "gpio-delay",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gpio_delay_ids);

static struct platform_driver gpio_delay_driver = {
	.driver	= {
		.name		= "gpio-delay",
		.of_match_table	= gpio_delay_ids,
	},
	.probe	= gpio_delay_probe,
};
module_platform_driver(gpio_delay_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Stein <alexander.stein@ew.tq-group.com>");
MODULE_DESCRIPTION("GPIO delay driver");
