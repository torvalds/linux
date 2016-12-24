/*
 * AXP20x GPIO driver
 *
 * Copyright (C) 2016 Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/axp20x.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define AXP20X_GPIO_FUNCTIONS		0x7
#define AXP20X_GPIO_FUNCTION_OUT_LOW	0
#define AXP20X_GPIO_FUNCTION_OUT_HIGH	1
#define AXP20X_GPIO_FUNCTION_INPUT	2

struct axp20x_gpio {
	struct gpio_chip	chip;
	struct regmap		*regmap;
};

static int axp20x_gpio_get_reg(unsigned offset)
{
	switch (offset) {
	case 0:
		return AXP20X_GPIO0_CTRL;
	case 1:
		return AXP20X_GPIO1_CTRL;
	case 2:
		return AXP20X_GPIO2_CTRL;
	}

	return -EINVAL;
}

static int axp20x_gpio_input(struct gpio_chip *chip, unsigned offset)
{
	struct axp20x_gpio *gpio = gpiochip_get_data(chip);
	int reg;

	reg = axp20x_gpio_get_reg(offset);
	if (reg < 0)
		return reg;

	return regmap_update_bits(gpio->regmap, reg,
				  AXP20X_GPIO_FUNCTIONS,
				  AXP20X_GPIO_FUNCTION_INPUT);
}

static int axp20x_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct axp20x_gpio *gpio = gpiochip_get_data(chip);
	unsigned int val;
	int reg, ret;

	reg = axp20x_gpio_get_reg(offset);
	if (reg < 0)
		return reg;

	ret = regmap_read(gpio->regmap, reg, &val);
	if (ret)
		return ret;

	return !!(val & BIT(offset + 4));
}

static int axp20x_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct axp20x_gpio *gpio = gpiochip_get_data(chip);
	unsigned int val;
	int reg, ret;

	reg = axp20x_gpio_get_reg(offset);
	if (reg < 0)
		return reg;

	ret = regmap_read(gpio->regmap, reg, &val);
	if (ret)
		return ret;

	/*
	 * This shouldn't really happen if the pin is in use already,
	 * or if it's not in use yet, it doesn't matter since we're
	 * going to change the value soon anyway. Default to output.
	 */
	if ((val & AXP20X_GPIO_FUNCTIONS) > 2)
		return 0;

	/*
	 * The GPIO directions are the three lowest values.
	 * 2 is input, 0 and 1 are output
	 */
	return val & 2;
}

static int axp20x_gpio_output(struct gpio_chip *chip, unsigned offset,
			      int value)
{
	struct axp20x_gpio *gpio = gpiochip_get_data(chip);
	int reg;

	reg = axp20x_gpio_get_reg(offset);
	if (reg < 0)
		return reg;

	return regmap_update_bits(gpio->regmap, reg,
				  AXP20X_GPIO_FUNCTIONS,
				  value ? AXP20X_GPIO_FUNCTION_OUT_HIGH
				  : AXP20X_GPIO_FUNCTION_OUT_LOW);
}

static void axp20x_gpio_set(struct gpio_chip *chip, unsigned offset,
			    int value)
{
	axp20x_gpio_output(chip, offset, value);
}

static int axp20x_gpio_probe(struct platform_device *pdev)
{
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	struct axp20x_gpio *gpio;
	int ret;

	if (!of_device_is_available(pdev->dev.of_node))
		return -ENODEV;

	if (!axp20x) {
		dev_err(&pdev->dev, "Parent drvdata not set\n");
		return -EINVAL;
	}

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->chip.base			= -1;
	gpio->chip.can_sleep		= true;
	gpio->chip.parent		= &pdev->dev;
	gpio->chip.label		= dev_name(&pdev->dev);
	gpio->chip.owner		= THIS_MODULE;
	gpio->chip.get			= axp20x_gpio_get;
	gpio->chip.get_direction	= axp20x_gpio_get_direction;
	gpio->chip.set			= axp20x_gpio_set;
	gpio->chip.direction_input	= axp20x_gpio_input;
	gpio->chip.direction_output	= axp20x_gpio_output;
	gpio->chip.ngpio		= 3;

	gpio->regmap = axp20x->regmap;

	ret = devm_gpiochip_add_data(&pdev->dev, &gpio->chip, gpio);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GPIO chip\n");
		return ret;
	}

	dev_info(&pdev->dev, "AXP209 GPIO driver loaded\n");

	return 0;
}

static const struct of_device_id axp20x_gpio_match[] = {
	{ .compatible = "x-powers,axp209-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, axp20x_gpio_match);

static struct platform_driver axp20x_gpio_driver = {
	.probe		= axp20x_gpio_probe,
	.driver = {
		.name		= "axp20x-gpio",
		.of_match_table	= axp20x_gpio_match,
	},
};

module_platform_driver(axp20x_gpio_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("AXP20x PMIC GPIO driver");
MODULE_LICENSE("GPL");
