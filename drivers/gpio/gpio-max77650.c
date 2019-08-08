// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 BayLibre SAS
// Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
//
// GPIO driver for MAXIM 77650/77651 charger/power-supply.

#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/mfd/max77650.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define MAX77650_GPIO_DIR_MASK		BIT(0)
#define MAX77650_GPIO_INVAL_MASK	BIT(1)
#define MAX77650_GPIO_DRV_MASK		BIT(2)
#define MAX77650_GPIO_OUTVAL_MASK	BIT(3)
#define MAX77650_GPIO_DEBOUNCE_MASK	BIT(4)

#define MAX77650_GPIO_DIR_OUT		0x00
#define MAX77650_GPIO_DIR_IN		BIT(0)
#define MAX77650_GPIO_OUT_LOW		0x00
#define MAX77650_GPIO_OUT_HIGH		BIT(3)
#define MAX77650_GPIO_DRV_OPEN_DRAIN	0x00
#define MAX77650_GPIO_DRV_PUSH_PULL	BIT(2)
#define MAX77650_GPIO_DEBOUNCE		BIT(4)

#define MAX77650_GPIO_DIR_BITS(_reg) \
		((_reg) & MAX77650_GPIO_DIR_MASK)
#define MAX77650_GPIO_INVAL_BITS(_reg) \
		(((_reg) & MAX77650_GPIO_INVAL_MASK) >> 1)

struct max77650_gpio_chip {
	struct regmap *map;
	struct gpio_chip gc;
	int irq;
};

static int max77650_gpio_direction_input(struct gpio_chip *gc,
					 unsigned int offset)
{
	struct max77650_gpio_chip *chip = gpiochip_get_data(gc);

	return regmap_update_bits(chip->map,
				  MAX77650_REG_CNFG_GPIO,
				  MAX77650_GPIO_DIR_MASK,
				  MAX77650_GPIO_DIR_IN);
}

static int max77650_gpio_direction_output(struct gpio_chip *gc,
					  unsigned int offset, int value)
{
	struct max77650_gpio_chip *chip = gpiochip_get_data(gc);
	int mask, regval;

	mask = MAX77650_GPIO_DIR_MASK | MAX77650_GPIO_OUTVAL_MASK;
	regval = value ? MAX77650_GPIO_OUT_HIGH : MAX77650_GPIO_OUT_LOW;
	regval |= MAX77650_GPIO_DIR_OUT;

	return regmap_update_bits(chip->map,
				  MAX77650_REG_CNFG_GPIO, mask, regval);
}

static void max77650_gpio_set_value(struct gpio_chip *gc,
				    unsigned int offset, int value)
{
	struct max77650_gpio_chip *chip = gpiochip_get_data(gc);
	int rv, regval;

	regval = value ? MAX77650_GPIO_OUT_HIGH : MAX77650_GPIO_OUT_LOW;

	rv = regmap_update_bits(chip->map, MAX77650_REG_CNFG_GPIO,
				MAX77650_GPIO_OUTVAL_MASK, regval);
	if (rv)
		dev_err(gc->parent, "cannot set GPIO value: %d\n", rv);
}

static int max77650_gpio_get_value(struct gpio_chip *gc,
				   unsigned int offset)
{
	struct max77650_gpio_chip *chip = gpiochip_get_data(gc);
	unsigned int val;
	int rv;

	rv = regmap_read(chip->map, MAX77650_REG_CNFG_GPIO, &val);
	if (rv)
		return rv;

	return MAX77650_GPIO_INVAL_BITS(val);
}

static int max77650_gpio_get_direction(struct gpio_chip *gc,
				       unsigned int offset)
{
	struct max77650_gpio_chip *chip = gpiochip_get_data(gc);
	unsigned int val;
	int rv;

	rv = regmap_read(chip->map, MAX77650_REG_CNFG_GPIO, &val);
	if (rv)
		return rv;

	return MAX77650_GPIO_DIR_BITS(val);
}

static int max77650_gpio_set_config(struct gpio_chip *gc,
				    unsigned int offset, unsigned long cfg)
{
	struct max77650_gpio_chip *chip = gpiochip_get_data(gc);

	switch (pinconf_to_config_param(cfg)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		return regmap_update_bits(chip->map,
					  MAX77650_REG_CNFG_GPIO,
					  MAX77650_GPIO_DRV_MASK,
					  MAX77650_GPIO_DRV_OPEN_DRAIN);
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return regmap_update_bits(chip->map,
					  MAX77650_REG_CNFG_GPIO,
					  MAX77650_GPIO_DRV_MASK,
					  MAX77650_GPIO_DRV_PUSH_PULL);
	case PIN_CONFIG_INPUT_DEBOUNCE:
		return regmap_update_bits(chip->map,
					  MAX77650_REG_CNFG_GPIO,
					  MAX77650_GPIO_DEBOUNCE_MASK,
					  MAX77650_GPIO_DEBOUNCE);
	default:
		return -ENOTSUPP;
	}
}

static int max77650_gpio_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct max77650_gpio_chip *chip = gpiochip_get_data(gc);

	return chip->irq;
}

static int max77650_gpio_probe(struct platform_device *pdev)
{
	struct max77650_gpio_chip *chip;
	struct device *dev, *parent;
	struct i2c_client *i2c;

	dev = &pdev->dev;
	parent = dev->parent;
	i2c = to_i2c_client(parent);

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->map = dev_get_regmap(parent, NULL);
	if (!chip->map)
		return -ENODEV;

	chip->irq = platform_get_irq_byname(pdev, "GPI");
	if (chip->irq < 0)
		return chip->irq;

	chip->gc.base = -1;
	chip->gc.ngpio = 1;
	chip->gc.label = i2c->name;
	chip->gc.parent = dev;
	chip->gc.owner = THIS_MODULE;
	chip->gc.can_sleep = true;

	chip->gc.direction_input = max77650_gpio_direction_input;
	chip->gc.direction_output = max77650_gpio_direction_output;
	chip->gc.set = max77650_gpio_set_value;
	chip->gc.get = max77650_gpio_get_value;
	chip->gc.get_direction = max77650_gpio_get_direction;
	chip->gc.set_config = max77650_gpio_set_config;
	chip->gc.to_irq = max77650_gpio_to_irq;

	return devm_gpiochip_add_data(dev, &chip->gc, chip);
}

static struct platform_driver max77650_gpio_driver = {
	.driver = {
		.name = "max77650-gpio",
	},
	.probe = max77650_gpio_probe,
};
module_platform_driver(max77650_gpio_driver);

MODULE_DESCRIPTION("MAXIM 77650/77651 GPIO driver");
MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_LICENSE("GPL v2");
