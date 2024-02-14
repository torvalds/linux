// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright Intel Corporation (C) 2014-2016. All Rights Reserved
 *
 * GPIO driver for  Altera Arria10 MAX5 System Resource Chip
 *
 * Adapted from gpio-tps65910.c
 */

#include <linux/gpio/driver.h>
#include <linux/mfd/altera-a10sr.h>
#include <linux/module.h>
#include <linux/property.h>

/**
 * struct altr_a10sr_gpio - Altera Max5 GPIO device private data structure
 * @gp:   : instance of the gpio_chip
 * @regmap: the regmap from the parent device.
 */
struct altr_a10sr_gpio {
	struct gpio_chip gp;
	struct regmap *regmap;
};

static int altr_a10sr_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct altr_a10sr_gpio *gpio = gpiochip_get_data(chip);
	int ret, val;

	ret = regmap_read(gpio->regmap, ALTR_A10SR_PBDSW_REG, &val);
	if (ret < 0)
		return ret;

	return !!(val & BIT(offset - ALTR_A10SR_LED_VALID_SHIFT));
}

static void altr_a10sr_gpio_set(struct gpio_chip *chip, unsigned int offset,
				int value)
{
	struct altr_a10sr_gpio *gpio = gpiochip_get_data(chip);

	regmap_update_bits(gpio->regmap, ALTR_A10SR_LED_REG,
			   BIT(ALTR_A10SR_LED_VALID_SHIFT + offset),
			   value ? BIT(ALTR_A10SR_LED_VALID_SHIFT + offset)
			   : 0);
}

static int altr_a10sr_gpio_direction_input(struct gpio_chip *gc,
					   unsigned int nr)
{
	if (nr < (ALTR_A10SR_IN_VALID_RANGE_LO - ALTR_A10SR_LED_VALID_SHIFT))
		return -EINVAL;

	return 0;
}

static int altr_a10sr_gpio_direction_output(struct gpio_chip *gc,
					    unsigned int nr, int value)
{
	if (nr > (ALTR_A10SR_OUT_VALID_RANGE_HI - ALTR_A10SR_LED_VALID_SHIFT))
		return -EINVAL;

	altr_a10sr_gpio_set(gc, nr, value);
	return 0;
}

static const struct gpio_chip altr_a10sr_gc = {
	.label = "altr_a10sr_gpio",
	.owner = THIS_MODULE,
	.get = altr_a10sr_gpio_get,
	.set = altr_a10sr_gpio_set,
	.direction_input = altr_a10sr_gpio_direction_input,
	.direction_output = altr_a10sr_gpio_direction_output,
	.can_sleep = true,
	.ngpio = 12,
	.base = -1,
};

static int altr_a10sr_gpio_probe(struct platform_device *pdev)
{
	struct altr_a10sr_gpio *gpio;
	struct altr_a10sr *a10sr = dev_get_drvdata(pdev->dev.parent);

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->regmap = a10sr->regmap;

	gpio->gp = altr_a10sr_gc;
	gpio->gp.parent = pdev->dev.parent;
	gpio->gp.fwnode = dev_fwnode(&pdev->dev);

	return devm_gpiochip_add_data(&pdev->dev, &gpio->gp, gpio);
}

static const struct of_device_id altr_a10sr_gpio_of_match[] = {
	{ .compatible = "altr,a10sr-gpio" },
	{ },
};
MODULE_DEVICE_TABLE(of, altr_a10sr_gpio_of_match);

static struct platform_driver altr_a10sr_gpio_driver = {
	.probe = altr_a10sr_gpio_probe,
	.driver = {
		.name	= "altr_a10sr_gpio",
		.of_match_table = of_match_ptr(altr_a10sr_gpio_of_match),
	},
};
module_platform_driver(altr_a10sr_gpio_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Thor Thayer <tthayer@opensource.altera.com>");
MODULE_DESCRIPTION("Altera Arria10 System Resource Chip GPIO");
