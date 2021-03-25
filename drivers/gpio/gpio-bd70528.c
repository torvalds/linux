// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 ROHM Semiconductors
// gpio-bd70528.c ROHM BD70528MWV gpio driver

#include <linux/gpio/driver.h>
#include <linux/mfd/rohm-bd70528.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define GPIO_IN_REG(offset) (BD70528_REG_GPIO1_IN + (offset) * 2)
#define GPIO_OUT_REG(offset) (BD70528_REG_GPIO1_OUT + (offset) * 2)

struct bd70528_gpio {
	struct regmap *regmap;
	struct device *dev;
	struct gpio_chip gpio;
};

static int bd70528_set_debounce(struct bd70528_gpio *bdgpio,
				unsigned int offset, unsigned int debounce)
{
	u8 val;

	switch (debounce) {
	case 0:
		val = BD70528_DEBOUNCE_DISABLE;
		break;
	case 1 ... 15000:
		val = BD70528_DEBOUNCE_15MS;
		break;
	case 15001 ... 30000:
		val = BD70528_DEBOUNCE_30MS;
		break;
	case 30001 ... 50000:
		val = BD70528_DEBOUNCE_50MS;
		break;
	default:
		dev_err(bdgpio->dev,
			"Invalid debounce value %u\n", debounce);
		return -EINVAL;
	}
	return regmap_update_bits(bdgpio->regmap, GPIO_IN_REG(offset),
				 BD70528_DEBOUNCE_MASK, val);
}

static int bd70528_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct bd70528_gpio *bdgpio = gpiochip_get_data(chip);
	int val, ret;

	/* Do we need to do something to IRQs here? */
	ret = regmap_read(bdgpio->regmap, GPIO_OUT_REG(offset), &val);
	if (ret) {
		dev_err(bdgpio->dev, "Could not read gpio direction\n");
		return ret;
	}
	if (val & BD70528_GPIO_OUT_EN_MASK)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int bd70528_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				   unsigned long config)
{
	struct bd70528_gpio *bdgpio = gpiochip_get_data(chip);

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		return regmap_update_bits(bdgpio->regmap,
					  GPIO_OUT_REG(offset),
					  BD70528_GPIO_DRIVE_MASK,
					  BD70528_GPIO_OPEN_DRAIN);
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return regmap_update_bits(bdgpio->regmap,
					  GPIO_OUT_REG(offset),
					  BD70528_GPIO_DRIVE_MASK,
					  BD70528_GPIO_PUSH_PULL);
		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		return bd70528_set_debounce(bdgpio, offset,
					    pinconf_to_config_argument(config));
		break;
	default:
		break;
	}
	return -ENOTSUPP;
}

static int bd70528_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct bd70528_gpio *bdgpio = gpiochip_get_data(chip);

	/* Do we need to do something to IRQs here? */
	return regmap_update_bits(bdgpio->regmap, GPIO_OUT_REG(offset),
				 BD70528_GPIO_OUT_EN_MASK,
				 BD70528_GPIO_OUT_DISABLE);
}

static void bd70528_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
	int ret;
	struct bd70528_gpio *bdgpio = gpiochip_get_data(chip);
	u8 val = (value) ? BD70528_GPIO_OUT_HI : BD70528_GPIO_OUT_LO;

	ret = regmap_update_bits(bdgpio->regmap, GPIO_OUT_REG(offset),
				 BD70528_GPIO_OUT_MASK, val);
	if (ret)
		dev_err(bdgpio->dev, "Could not set gpio to %d\n", value);
}

static int bd70528_direction_output(struct gpio_chip *chip, unsigned int offset,
				    int value)
{
	struct bd70528_gpio *bdgpio = gpiochip_get_data(chip);

	bd70528_gpio_set(chip, offset, value);
	return regmap_update_bits(bdgpio->regmap, GPIO_OUT_REG(offset),
				 BD70528_GPIO_OUT_EN_MASK,
				 BD70528_GPIO_OUT_ENABLE);
}

#define GPIO_IN_STATE_MASK(offset) (BD70528_GPIO_IN_STATE_BASE << (offset))

static int bd70528_gpio_get_o(struct bd70528_gpio *bdgpio, unsigned int offset)
{
	int ret;
	unsigned int val;

	ret = regmap_read(bdgpio->regmap, GPIO_OUT_REG(offset), &val);
	if (!ret)
		ret = !!(val & BD70528_GPIO_OUT_MASK);
	else
		dev_err(bdgpio->dev, "GPIO (out) state read failed\n");

	return ret;
}

static int bd70528_gpio_get_i(struct bd70528_gpio *bdgpio, unsigned int offset)
{
	unsigned int val;
	int ret;

	ret = regmap_read(bdgpio->regmap, BD70528_REG_GPIO_STATE, &val);

	if (!ret)
		ret = !(val & GPIO_IN_STATE_MASK(offset));
	else
		dev_err(bdgpio->dev, "GPIO (in) state read failed\n");

	return ret;
}

static int bd70528_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	int ret;
	struct bd70528_gpio *bdgpio = gpiochip_get_data(chip);

	/*
	 * There is a race condition where someone might be changing the
	 * GPIO direction after we get it but before we read the value. But
	 * application design where GPIO direction may be changed just when
	 * we read GPIO value would be pointless as reader could not know
	 * whether the returned high/low state is caused by input or output.
	 * Or then there must be other ways to mitigate the issue. Thus
	 * locking would make no sense.
	 */
	ret = bd70528_get_direction(chip, offset);
	if (ret == GPIO_LINE_DIRECTION_OUT)
		ret = bd70528_gpio_get_o(bdgpio, offset);
	else if (ret == GPIO_LINE_DIRECTION_IN)
		ret = bd70528_gpio_get_i(bdgpio, offset);
	else
		dev_err(bdgpio->dev, "failed to read GPIO direction\n");

	return ret;
}

static int bd70528_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bd70528_gpio *bdgpio;
	int ret;

	bdgpio = devm_kzalloc(dev, sizeof(*bdgpio), GFP_KERNEL);
	if (!bdgpio)
		return -ENOMEM;
	bdgpio->dev = dev;
	bdgpio->gpio.parent = dev->parent;
	bdgpio->gpio.label = "bd70528-gpio";
	bdgpio->gpio.owner = THIS_MODULE;
	bdgpio->gpio.get_direction = bd70528_get_direction;
	bdgpio->gpio.direction_input = bd70528_direction_input;
	bdgpio->gpio.direction_output = bd70528_direction_output;
	bdgpio->gpio.set_config = bd70528_gpio_set_config;
	bdgpio->gpio.can_sleep = true;
	bdgpio->gpio.get = bd70528_gpio_get;
	bdgpio->gpio.set = bd70528_gpio_set;
	bdgpio->gpio.ngpio = 4;
	bdgpio->gpio.base = -1;
#ifdef CONFIG_OF_GPIO
	bdgpio->gpio.of_node = dev->parent->of_node;
#endif
	bdgpio->regmap = dev_get_regmap(dev->parent, NULL);
	if (!bdgpio->regmap)
		return -ENODEV;

	ret = devm_gpiochip_add_data(dev, &bdgpio->gpio, bdgpio);
	if (ret)
		dev_err(dev, "gpio_init: Failed to add bd70528-gpio\n");

	return ret;
}

static struct platform_driver bd70528_gpio = {
	.driver = {
		.name = "bd70528-gpio"
	},
	.probe = bd70528_probe,
};

module_platform_driver(bd70528_gpio);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("BD70528 voltage regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bd70528-gpio");
