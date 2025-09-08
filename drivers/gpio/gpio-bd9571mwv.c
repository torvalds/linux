// SPDX-License-Identifier: GPL-2.0-only
/*
 * ROHM BD9571MWV-M and BD9574MWF-M GPIO driver
 *
 * Copyright (C) 2017 Marek Vasut <marek.vasut+renesas@gmail.com>
 *
 * Based on the TPS65086 driver
 *
 * NOTE: Interrupts are not supported yet.
 */

#include <linux/gpio/driver.h>
#include <linux/mfd/rohm-generic.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/mfd/bd9571mwv.h>

struct bd9571mwv_gpio {
	struct regmap *regmap;
	struct gpio_chip chip;
};

static int bd9571mwv_gpio_get_direction(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct bd9571mwv_gpio *gpio = gpiochip_get_data(chip);
	int ret, val;

	ret = regmap_read(gpio->regmap, BD9571MWV_GPIO_DIR, &val);
	if (ret < 0)
		return ret;
	if (val & BIT(offset))
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int bd9571mwv_gpio_direction_input(struct gpio_chip *chip,
					 unsigned int offset)
{
	struct bd9571mwv_gpio *gpio = gpiochip_get_data(chip);

	regmap_update_bits(gpio->regmap, BD9571MWV_GPIO_DIR, BIT(offset), 0);

	return 0;
}

static int bd9571mwv_gpio_direction_output(struct gpio_chip *chip,
					  unsigned int offset, int value)
{
	struct bd9571mwv_gpio *gpio = gpiochip_get_data(chip);

	/* Set the initial value */
	regmap_update_bits(gpio->regmap, BD9571MWV_GPIO_OUT,
			   BIT(offset), value ? BIT(offset) : 0);
	regmap_update_bits(gpio->regmap, BD9571MWV_GPIO_DIR,
			   BIT(offset), BIT(offset));

	return 0;
}

static int bd9571mwv_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct bd9571mwv_gpio *gpio = gpiochip_get_data(chip);
	int ret, val;

	ret = regmap_read(gpio->regmap, BD9571MWV_GPIO_IN, &val);
	if (ret < 0)
		return ret;

	return val & BIT(offset);
}

static int bd9571mwv_gpio_set(struct gpio_chip *chip, unsigned int offset,
			      int value)
{
	struct bd9571mwv_gpio *gpio = gpiochip_get_data(chip);

	return regmap_update_bits(gpio->regmap, BD9571MWV_GPIO_OUT,
				  BIT(offset), value ? BIT(offset) : 0);
}

static const struct gpio_chip template_chip = {
	.label			= "bd9571mwv-gpio",
	.owner			= THIS_MODULE,
	.get_direction		= bd9571mwv_gpio_get_direction,
	.direction_input	= bd9571mwv_gpio_direction_input,
	.direction_output	= bd9571mwv_gpio_direction_output,
	.get			= bd9571mwv_gpio_get,
	.set			= bd9571mwv_gpio_set,
	.base			= -1,
	.ngpio			= 2,
	.can_sleep		= true,
};

static int bd9571mwv_gpio_probe(struct platform_device *pdev)
{
	struct bd9571mwv_gpio *gpio;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	gpio->chip = template_chip;
	gpio->chip.parent = pdev->dev.parent;

	return devm_gpiochip_add_data(&pdev->dev, &gpio->chip, gpio);
}

static const struct platform_device_id bd9571mwv_gpio_id_table[] = {
	{ "bd9571mwv-gpio", ROHM_CHIP_TYPE_BD9571 },
	{ "bd9574mwf-gpio", ROHM_CHIP_TYPE_BD9574 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, bd9571mwv_gpio_id_table);

static struct platform_driver bd9571mwv_gpio_driver = {
	.driver = {
		.name = "bd9571mwv-gpio",
	},
	.probe = bd9571mwv_gpio_probe,
	.id_table = bd9571mwv_gpio_id_table,
};
module_platform_driver(bd9571mwv_gpio_driver);

MODULE_AUTHOR("Marek Vasut <marek.vasut+renesas@gmail.com>");
MODULE_DESCRIPTION("BD9571MWV GPIO driver");
MODULE_LICENSE("GPL v2");
