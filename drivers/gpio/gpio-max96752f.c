// SPDX-License-Identifier: GPL-2.0-only
/*
 * Maxim MAX96752F GPIO driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/max96752f.h>

struct max96752f_gpio {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_chip gpio_chip;
};

static int max96752f_gpio_direction_output(struct gpio_chip *gc,
					   unsigned int offset, int value)
{
	struct max96752f_gpio *gpio = gpiochip_get_data(gc);

	regmap_update_bits(gpio->regmap, GPIO_A_REG(offset),
			   GPIO_OUT_DIS | GPIO_OUT,
			   FIELD_PREP(GPIO_OUT_DIS, 0) |
			   FIELD_PREP(GPIO_OUT, value));

	return 0;
}

static void max96752f_gpio_set(struct gpio_chip *gc, unsigned int offset,
			       int value)
{
	struct max96752f_gpio *gpio = gpiochip_get_data(gc);

	regmap_update_bits(gpio->regmap, GPIO_A_REG(offset), GPIO_OUT,
			   FIELD_PREP(GPIO_OUT, value));
}

static int max96752f_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct max96752f_gpio *gpio = gpiochip_get_data(gc);
	unsigned int value;

	regmap_read(gpio->regmap, GPIO_A_REG(offset), &value);

	return !!FIELD_GET(GPIO_OUT, value);
}

static int max96752f_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max96752f_gpio *gpio;
	int ret;

	gpio = devm_kzalloc(dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->dev = dev;
	platform_set_drvdata(pdev, gpio);

	gpio->regmap = dev_get_regmap(dev->parent, NULL);
	if (!gpio->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get regmap\n");

	gpio->gpio_chip.of_node = dev->of_node;
	gpio->gpio_chip.label = dev_name(dev);
	gpio->gpio_chip.parent = dev->parent;
	gpio->gpio_chip.direction_output = max96752f_gpio_direction_output;
	gpio->gpio_chip.set = max96752f_gpio_set;
	gpio->gpio_chip.get = max96752f_gpio_get;
	gpio->gpio_chip.ngpio = 16;
	gpio->gpio_chip.can_sleep = true;
	gpio->gpio_chip.base = -1;

	ret = devm_gpiochip_add_data(dev, &gpio->gpio_chip, gpio);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add gpio chip\n");

	return 0;
}

static const struct of_device_id max96752f_gpio_of_match[] = {
	{ .name = "maxim,max96752f-gpio", },
	{}
};
MODULE_DEVICE_TABLE(of, max96752f_gpio_of_match);

static struct platform_driver max96752f_gpio_driver = {
	.driver = {
		.name	= "max96752f-gpio",
		.of_match_table = max96752f_gpio_of_match,
	},
	.probe = max96752f_gpio_probe,
};

module_platform_driver(max96752f_gpio_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96752F GPIO driver");
MODULE_LICENSE("GPL");
