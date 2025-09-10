// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2023 Texas Instruments Incorporated - https://www.ti.com/
 *	Andrew Davis <afd@ti.com>
 *
 * Based on the TPS65912 driver
 */

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/mfd/tps65086.h>

struct tps65086_gpio {
	struct gpio_chip chip;
	struct tps65086 *tps;
};

static int tps65086_gpio_get_direction(struct gpio_chip *chip,
				       unsigned offset)
{
	/* This device is output only */
	return GPIO_LINE_DIRECTION_OUT;
}

static int tps65086_gpio_direction_input(struct gpio_chip *chip,
					 unsigned offset)
{
	/* This device is output only */
	return -EINVAL;
}

static int tps65086_gpio_direction_output(struct gpio_chip *chip,
					  unsigned offset, int value)
{
	struct tps65086_gpio *gpio = gpiochip_get_data(chip);

	/* Set the initial value */
	return regmap_update_bits(gpio->tps->regmap, TPS65086_GPOCTRL,
				  BIT(4 + offset), value ? BIT(4 + offset) : 0);
}

static int tps65086_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct tps65086_gpio *gpio = gpiochip_get_data(chip);
	int ret, val;

	ret = regmap_read(gpio->tps->regmap, TPS65086_GPOCTRL, &val);
	if (ret < 0)
		return ret;

	return val & BIT(4 + offset);
}

static int tps65086_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
	struct tps65086_gpio *gpio = gpiochip_get_data(chip);

	return regmap_update_bits(gpio->tps->regmap, TPS65086_GPOCTRL,
				  BIT(4 + offset), value ? BIT(4 + offset) : 0);
}

static const struct gpio_chip template_chip = {
	.label			= "tps65086-gpio",
	.owner			= THIS_MODULE,
	.get_direction		= tps65086_gpio_get_direction,
	.direction_input	= tps65086_gpio_direction_input,
	.direction_output	= tps65086_gpio_direction_output,
	.get			= tps65086_gpio_get,
	.set			= tps65086_gpio_set,
	.base			= -1,
	.ngpio			= 4,
	.can_sleep		= true,
};

static int tps65086_gpio_probe(struct platform_device *pdev)
{
	struct tps65086_gpio *gpio;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->tps = dev_get_drvdata(pdev->dev.parent);
	gpio->chip = template_chip;
	gpio->chip.parent = gpio->tps->dev;

	return devm_gpiochip_add_data(&pdev->dev, &gpio->chip, gpio);
}

static const struct platform_device_id tps65086_gpio_id_table[] = {
	{ "tps65086-gpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, tps65086_gpio_id_table);

static struct platform_driver tps65086_gpio_driver = {
	.driver = {
		.name = "tps65086-gpio",
	},
	.probe = tps65086_gpio_probe,
	.id_table = tps65086_gpio_id_table,
};
module_platform_driver(tps65086_gpio_driver);

MODULE_AUTHOR("Andrew Davis <afd@ti.com>");
MODULE_DESCRIPTION("TPS65086 GPIO driver");
MODULE_LICENSE("GPL v2");
