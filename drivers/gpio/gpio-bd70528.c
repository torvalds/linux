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
	struct rohm_regmap_dev chip;
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
	case 1 ... 15:
		val = BD70528_DEBOUNCE_15MS;
		break;
	case 16 ... 30:
		val = BD70528_DEBOUNCE_30MS;
		break;
	case 31 ... 50:
		val = BD70528_DEBOUNCE_50MS;
		break;
	default:
		dev_err(bdgpio->chip.dev,
			"Invalid debounce value %u\n", debounce);
		return -EINVAL;
	}
	return regmap_update_bits(bdgpio->chip.regmap, GPIO_IN_REG(offset),
				 BD70528_DEBOUNCE_MASK, val);
}

static int bd70528_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct bd70528_gpio *bdgpio = gpiochip_get_data(chip);
	int val, ret;

	/* Do we need to do something to IRQs here? */
	ret = regmap_read(bdgpio->chip.regmap, GPIO_OUT_REG(offset), &val);
	if (ret) {
		dev_err(bdgpio->chip.dev, "Could not read gpio direction\n");
		return ret;
	}

	return !(val & BD70528_GPIO_OUT_EN_MASK);
}

static int bd70528_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				   unsigned long config)
{
	struct bd70528_gpio *bdgpio = gpiochip_get_data(chip);

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		return regmap_update_bits(bdgpio->chip.regmap,
					  GPIO_OUT_REG(offset),
					  BD70528_GPIO_DRIVE_MASK,
					  BD70528_GPIO_OPEN_DRAIN);
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return regmap_update_bits(bdgpio->chip.regmap,
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
	return regmap_update_bits(bdgpio->chip.regmap, GPIO_OUT_REG(offset),
				 BD70528_GPIO_OUT_EN_MASK,
				 BD70528_GPIO_OUT_DISABLE);
}

static void bd70528_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
	int ret;
	struct bd70528_gpio *bdgpio = gpiochip_get_data(chip);
	u8 val = (value) ? BD70528_GPIO_OUT_HI : BD70528_GPIO_OUT_LO;

	ret = regmap_update_bits(bdgpio->chip.regmap, GPIO_OUT_REG(offset),
				 BD70528_GPIO_OUT_MASK, val);
	if (ret)
		dev_err(bdgpio->chip.dev, "Could not set gpio to %d\n", value);
}

static int bd70528_direction_output(struct gpio_chip *chip, unsigned int offset,
				    int value)
{
	struct bd70528_gpio *bdgpio = gpiochip_get_data(chip);

	bd70528_gpio_set(chip, offset, value);
	return regmap_update_bits(bdgpio->chip.regmap, GPIO_OUT_REG(offset),
				 BD70528_GPIO_OUT_EN_MASK,
				 BD70528_GPIO_OUT_ENABLE);
}

#define GPIO_IN_STATE_MASK(offset) (BD70528_GPIO_IN_STATE_BASE << (offset))

static int bd70528_gpio_get_o(struct bd70528_gpio *bdgpio, unsigned int offset)
{
	int ret;
	unsigned int val;

	ret = regmap_read(bdgpio->chip.regmap, GPIO_OUT_REG(offset), &val);
	if (!ret)
		ret = !!(val & BD70528_GPIO_OUT_MASK);
	else
		dev_err(bdgpio->chip.dev, "GPIO (out) state read failed\n");

	return ret;
}

static int bd70528_gpio_get_i(struct bd70528_gpio *bdgpio, unsigned int offset)
{
	unsigned int val;
	int ret;

	ret = regmap_read(bdgpio->chip.regmap, BD70528_REG_GPIO_STATE, &val);

	if (!ret)
		ret = !(val & GPIO_IN_STATE_MASK(offset));
	else
		dev_err(bdgpio->chip.dev, "GPIO (in) state read failed\n");

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
	if (ret == 0)
		ret = bd70528_gpio_get_o(bdgpio, offset);
	else if (ret == 1)
		ret = bd70528_gpio_get_i(bdgpio, offset);
	else
		dev_err(bdgpio->chip.dev, "failed to read GPIO direction\n");

	return ret;
}

static int bd70528_probe(struct platform_device *pdev)
{
	struct bd70528_gpio *bdgpio;
	struct rohm_regmap_dev *bd70528;
	int ret;

	bd70528 = dev_get_drvdata(pdev->dev.parent);
	if (!bd70528) {
		dev_err(&pdev->dev, "No MFD driver data\n");
		return -EINVAL;
	}

	bdgpio = devm_kzalloc(&pdev->dev, sizeof(*bdgpio),
			      GFP_KERNEL);
	if (!bdgpio)
		return -ENOMEM;
	bdgpio->chip.dev = &pdev->dev;
	bdgpio->gpio.parent = pdev->dev.parent;
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
	bdgpio->gpio.of_node = pdev->dev.parent->of_node;
#endif
	bdgpio->chip.regmap = bd70528->regmap;

	ret = devm_gpiochip_add_data(&pdev->dev, &bdgpio->gpio,
				     bdgpio);
	if (ret)
		dev_err(&pdev->dev, "gpio_init: Failed to add bd70528-gpio\n");

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
