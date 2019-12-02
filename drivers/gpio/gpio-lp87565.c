/*
 * Copyright (C) 2017 Texas Instruments Incorporated - http://www.ti.com/
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
 * Based on the LP873X driver
 */

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <linux/mfd/lp87565.h>

struct lp87565_gpio {
	struct gpio_chip chip;
	struct regmap *map;
};

static int lp87565_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct lp87565_gpio *gpio = gpiochip_get_data(chip);
	int ret, val;

	ret = regmap_read(gpio->map, LP87565_REG_GPIO_IN, &val);
	if (ret < 0)
		return ret;

	return !!(val & BIT(offset));
}

static void lp87565_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
	struct lp87565_gpio *gpio = gpiochip_get_data(chip);

	regmap_update_bits(gpio->map, LP87565_REG_GPIO_OUT,
			   BIT(offset), value ? BIT(offset) : 0);
}

static int lp87565_gpio_get_direction(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct lp87565_gpio *gpio = gpiochip_get_data(chip);
	int ret, val;

	ret = regmap_read(gpio->map, LP87565_REG_GPIO_CONFIG, &val);
	if (ret < 0)
		return ret;

	if (val & BIT(offset))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int lp87565_gpio_direction_input(struct gpio_chip *chip,
					unsigned int offset)
{
	struct lp87565_gpio *gpio = gpiochip_get_data(chip);

	return regmap_update_bits(gpio->map,
				  LP87565_REG_GPIO_CONFIG,
				  BIT(offset), 0);
}

static int lp87565_gpio_direction_output(struct gpio_chip *chip,
					 unsigned int offset, int value)
{
	struct lp87565_gpio *gpio = gpiochip_get_data(chip);

	lp87565_gpio_set(chip, offset, value);

	return regmap_update_bits(gpio->map,
				  LP87565_REG_GPIO_CONFIG,
				  BIT(offset), BIT(offset));
}

static int lp87565_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
	struct lp87565_gpio *gpio = gpiochip_get_data(gc);
	int ret;

	switch (offset) {
	case 0:
	case 1:
	case 2:
		/*
		 * MUX can program the pin to be in EN1/2/3 pin mode
		 * Or GPIO1/2/3 mode.
		 * Setup the GPIO*_SEL MUX to GPIO mode
		 */
		ret = regmap_update_bits(gpio->map,
					 LP87565_REG_PIN_FUNCTION,
					 BIT(offset), BIT(offset));
		if (ret)
			return ret;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int lp87565_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
				   unsigned long config)
{
	struct lp87565_gpio *gpio = gpiochip_get_data(gc);

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		return regmap_update_bits(gpio->map,
					  LP87565_REG_GPIO_CONFIG,
					  BIT(offset +
					      __ffs(LP87565_GOIO1_OD)),
					  BIT(offset +
					      __ffs(LP87565_GOIO1_OD)));
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return regmap_update_bits(gpio->map,
					  LP87565_REG_GPIO_CONFIG,
					  BIT(offset +
					      __ffs(LP87565_GOIO1_OD)), 0);
	default:
		return -ENOTSUPP;
	}
}

static const struct gpio_chip template_chip = {
	.label			= "lp87565-gpio",
	.owner			= THIS_MODULE,
	.request		= lp87565_gpio_request,
	.get_direction		= lp87565_gpio_get_direction,
	.direction_input	= lp87565_gpio_direction_input,
	.direction_output	= lp87565_gpio_direction_output,
	.get			= lp87565_gpio_get,
	.set			= lp87565_gpio_set,
	.set_config		= lp87565_gpio_set_config,
	.base			= -1,
	.ngpio			= 3,
	.can_sleep		= true,
};

static int lp87565_gpio_probe(struct platform_device *pdev)
{
	struct lp87565_gpio *gpio;
	struct lp87565 *lp87565;
	int ret;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	lp87565 = dev_get_drvdata(pdev->dev.parent);
	gpio->chip = template_chip;
	gpio->chip.parent = lp87565->dev;
	gpio->map = lp87565->regmap;

	ret = devm_gpiochip_add_data(&pdev->dev, &gpio->chip, gpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct platform_device_id lp87565_gpio_id_table[] = {
	{ "lp87565-q1-gpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, lp87565_gpio_id_table);

static struct platform_driver lp87565_gpio_driver = {
	.driver = {
		.name = "lp87565-gpio",
	},
	.probe = lp87565_gpio_probe,
	.id_table = lp87565_gpio_id_table,
};
module_platform_driver(lp87565_gpio_driver);

MODULE_AUTHOR("Keerthy <j-keerthy@ti.com>");
MODULE_DESCRIPTION("LP87565 GPIO driver");
MODULE_LICENSE("GPL v2");
