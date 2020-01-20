// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2018 ROHM Semiconductors

#include <linux/gpio/driver.h>
#include <linux/mfd/rohm-bd71828.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define GPIO_OUT_REG(off) (BD71828_REG_GPIO_CTRL1 + (off))
#define HALL_GPIO_OFFSET 3

/*
 * These defines can be removed when
 * "gpio: Add definition for GPIO direction"
 * (9208b1e77d6e8e9776f34f46ef4079ecac9c3c25 in GPIO tree) gets merged,
 */
#ifndef GPIO_LINE_DIRECTION_IN
	#define GPIO_LINE_DIRECTION_IN 1
	#define GPIO_LINE_DIRECTION_OUT 0
#endif

struct bd71828_gpio {
	struct rohm_regmap_dev chip;
	struct gpio_chip gpio;
};

static void bd71828_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
	int ret;
	struct bd71828_gpio *bdgpio = gpiochip_get_data(chip);
	u8 val = (value) ? BD71828_GPIO_OUT_HI : BD71828_GPIO_OUT_LO;

	/*
	 * The HALL input pin can only be used as input. If this is the pin
	 * we are dealing with - then we are done
	 */
	if (offset == HALL_GPIO_OFFSET)
		return;

	ret = regmap_update_bits(bdgpio->chip.regmap, GPIO_OUT_REG(offset),
				 BD71828_GPIO_OUT_MASK, val);
	if (ret)
		dev_err(bdgpio->chip.dev, "Could not set gpio to %d\n", value);
}

static int bd71828_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	int ret;
	unsigned int val;
	struct bd71828_gpio *bdgpio = gpiochip_get_data(chip);

	if (offset == HALL_GPIO_OFFSET)
		ret = regmap_read(bdgpio->chip.regmap, BD71828_REG_IO_STAT,
				  &val);
	else
		ret = regmap_read(bdgpio->chip.regmap, GPIO_OUT_REG(offset),
				  &val);
	if (!ret)
		ret = (val & BD71828_GPIO_OUT_MASK);

	return ret;
}

static int bd71828_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				   unsigned long config)
{
	struct bd71828_gpio *bdgpio = gpiochip_get_data(chip);

	if (offset == HALL_GPIO_OFFSET)
		return -ENOTSUPP;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		return regmap_update_bits(bdgpio->chip.regmap,
					  GPIO_OUT_REG(offset),
					  BD71828_GPIO_DRIVE_MASK,
					  BD71828_GPIO_OPEN_DRAIN);
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return regmap_update_bits(bdgpio->chip.regmap,
					  GPIO_OUT_REG(offset),
					  BD71828_GPIO_DRIVE_MASK,
					  BD71828_GPIO_PUSH_PULL);
	default:
		break;
	}
	return -ENOTSUPP;
}

static int bd71828_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	/*
	 * Pin usage is selected by OTP data. We can't read it runtime. Hence
	 * we trust that if the pin is not excluded by "gpio-reserved-ranges"
	 * the OTP configuration is set to OUT. (Other pins but HALL input pin
	 * on BD71828 can't really be used for general purpose input - input
	 * states are used for specific cases like regulator control or
	 * PMIC_ON_REQ.
	 */
	if (offset == HALL_GPIO_OFFSET)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int bd71828_probe(struct platform_device *pdev)
{
	struct bd71828_gpio *bdgpio;
	struct rohm_regmap_dev *bd71828;

	bd71828 = dev_get_drvdata(pdev->dev.parent);
	if (!bd71828) {
		dev_err(&pdev->dev, "No MFD driver data\n");
		return -EINVAL;
	}

	bdgpio = devm_kzalloc(&pdev->dev, sizeof(*bdgpio),
			      GFP_KERNEL);
	if (!bdgpio)
		return -ENOMEM;

	bdgpio->chip.dev = &pdev->dev;
	bdgpio->gpio.parent = pdev->dev.parent;
	bdgpio->gpio.label = "bd71828-gpio";
	bdgpio->gpio.owner = THIS_MODULE;
	bdgpio->gpio.get_direction = bd71828_get_direction;
	bdgpio->gpio.set_config = bd71828_gpio_set_config;
	bdgpio->gpio.can_sleep = true;
	bdgpio->gpio.get = bd71828_gpio_get;
	bdgpio->gpio.set = bd71828_gpio_set;
	bdgpio->gpio.base = -1;

	/*
	 * See if we need some implementation to mark some PINs as
	 * not controllable based on DT info or if core can handle
	 * "gpio-reserved-ranges" and exclude them from control
	 */
	bdgpio->gpio.ngpio = 4;
	bdgpio->gpio.of_node = pdev->dev.parent->of_node;
	bdgpio->chip.regmap = bd71828->regmap;

	return devm_gpiochip_add_data(&pdev->dev, &bdgpio->gpio,
				     bdgpio);
}

static struct platform_driver bd71828_gpio = {
	.driver = {
		.name = "bd71828-gpio"
	},
	.probe = bd71828_probe,
};

module_platform_driver(bd71828_gpio);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("BD71828 voltage regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bd71828-gpio");
