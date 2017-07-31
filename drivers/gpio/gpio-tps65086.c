/*
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
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
 * Based on the TPS65912 driver
 */

#include <linux/gpio.h>
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
	return 0;
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
	regmap_update_bits(gpio->tps->regmap, TPS65086_GPOCTRL,
			   BIT(4 + offset), value ? BIT(4 + offset) : 0);

	return 0;
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

static void tps65086_gpio_set(struct gpio_chip *chip, unsigned offset,
			      int value)
{
	struct tps65086_gpio *gpio = gpiochip_get_data(chip);

	regmap_update_bits(gpio->tps->regmap, TPS65086_GPOCTRL,
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
	int ret;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	platform_set_drvdata(pdev, gpio);

	gpio->tps = dev_get_drvdata(pdev->dev.parent);
	gpio->chip = template_chip;
	gpio->chip.parent = gpio->tps->dev;

	ret = gpiochip_add_data(&gpio->chip, gpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n", ret);
		return ret;
	}

	return 0;
}

static int tps65086_gpio_remove(struct platform_device *pdev)
{
	struct tps65086_gpio *gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&gpio->chip);

	return 0;
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
	.remove = tps65086_gpio_remove,
	.id_table = tps65086_gpio_id_table,
};
module_platform_driver(tps65086_gpio_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("TPS65086 GPIO driver");
MODULE_LICENSE("GPL v2");
