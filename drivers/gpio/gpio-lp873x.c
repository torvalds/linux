/*
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/
 *	Keerthy <j-keerthy@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether expressed or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 *
 * Based on the TPS65218 driver
 */

#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <linux/mfd/lp873x.h>

#define BITS_PER_GPO		0x4
#define LP873X_GPO_CTRL_OD	0x2

struct lp873x_gpio {
	struct gpio_chip chip;
	struct lp873x *lp873;
};

static int lp873x_gpio_get_direction(struct gpio_chip *chip,
				     unsigned int offset)
{
	/* This device is output only */
	return 0;
}

static int lp873x_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	/* This device is output only */
	return -EINVAL;
}

static int lp873x_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	struct lp873x_gpio *gpio = gpiochip_get_data(chip);

	/* Set the initial value */
	return regmap_update_bits(gpio->lp873->regmap, LP873X_REG_GPO_CTRL,
				  BIT(offset * BITS_PER_GPO),
				  value ? BIT(offset * BITS_PER_GPO) : 0);
}

static int lp873x_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct lp873x_gpio *gpio = gpiochip_get_data(chip);
	int ret, val;

	ret = regmap_read(gpio->lp873->regmap, LP873X_REG_GPO_CTRL, &val);
	if (ret < 0)
		return ret;

	return val & BIT(offset * BITS_PER_GPO);
}

static void lp873x_gpio_set(struct gpio_chip *chip, unsigned int offset,
			    int value)
{
	struct lp873x_gpio *gpio = gpiochip_get_data(chip);

	regmap_update_bits(gpio->lp873->regmap, LP873X_REG_GPO_CTRL,
			   BIT(offset * BITS_PER_GPO),
			   value ? BIT(offset * BITS_PER_GPO) : 0);
}

static int lp873x_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
	struct lp873x_gpio *gpio = gpiochip_get_data(gc);
	int ret;

	switch (offset) {
	case 0:
		/* No MUX Set up Needed for GPO */
		break;
	case 1:
		/* Setup the CLKIN_PIN_SEL MUX to GPO2 */
		ret = regmap_update_bits(gpio->lp873->regmap, LP873X_REG_CONFIG,
					 LP873X_CONFIG_CLKIN_PIN_SEL, 0);
		if (ret)
			return ret;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int lp873x_gpio_set_single_ended(struct gpio_chip *gc,
					unsigned int offset,
					enum single_ended_mode mode)
{
	struct lp873x_gpio *gpio = gpiochip_get_data(gc);

	switch (mode) {
	case LINE_MODE_OPEN_DRAIN:
		return regmap_update_bits(gpio->lp873->regmap,
					  LP873X_REG_GPO_CTRL,
					  BIT(offset * BITS_PER_GPO +
					  LP873X_GPO_CTRL_OD),
					  BIT(offset * BITS_PER_GPO +
					  LP873X_GPO_CTRL_OD));
	case LINE_MODE_PUSH_PULL:
		return regmap_update_bits(gpio->lp873->regmap,
					  LP873X_REG_GPO_CTRL,
					  BIT(offset * BITS_PER_GPO +
					  LP873X_GPO_CTRL_OD), 0);
	default:
		return -ENOTSUPP;
	}
}

static struct gpio_chip template_chip = {
	.label			= "lp873x-gpio",
	.owner			= THIS_MODULE,
	.request		= lp873x_gpio_request,
	.get_direction		= lp873x_gpio_get_direction,
	.direction_input	= lp873x_gpio_direction_input,
	.direction_output	= lp873x_gpio_direction_output,
	.get			= lp873x_gpio_get,
	.set			= lp873x_gpio_set,
	.set_single_ended	= lp873x_gpio_set_single_ended,
	.base			= -1,
	.ngpio			= 2,
	.can_sleep		= true,
};

static int lp873x_gpio_probe(struct platform_device *pdev)
{
	struct lp873x_gpio *gpio;
	int ret;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	platform_set_drvdata(pdev, gpio);

	gpio->lp873 = dev_get_drvdata(pdev->dev.parent);
	gpio->chip = template_chip;
	gpio->chip.parent = gpio->lp873->dev;

	ret = gpiochip_add_data(&gpio->chip, gpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n", ret);
		return ret;
	}

	return 0;
}

static int lp873x_gpio_remove(struct platform_device *pdev)
{
	struct lp873x_gpio *gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&gpio->chip);

	return 0;
}

static const struct platform_device_id lp873x_gpio_id_table[] = {
	{ "lp873x-gpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, lp873x_gpio_id_table);

static struct platform_driver lp873x_gpio_driver = {
	.driver = {
		.name = "lp873x-gpio",
	},
	.probe = lp873x_gpio_probe,
	.remove = lp873x_gpio_remove,
	.id_table = lp873x_gpio_id_table,
};
module_platform_driver(lp873x_gpio_driver);

MODULE_AUTHOR("Keerthy <j-keerthy@ti.com>");
MODULE_DESCRIPTION("LP873X GPIO driver");
MODULE_LICENSE("GPL v2");
