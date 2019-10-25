// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * gpiolib support for Wolfson Arizona class devices
 *
 * Copyright 2012 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/pdata.h>
#include <linux/mfd/arizona/registers.h>

struct arizona_gpio {
	struct arizona *arizona;
	struct gpio_chip gpio_chip;
};

static int arizona_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct arizona_gpio *arizona_gpio = gpiochip_get_data(chip);
	struct arizona *arizona = arizona_gpio->arizona;
	bool persistent = gpiochip_line_is_persistent(chip, offset);
	bool change;
	int ret;

	ret = regmap_update_bits_check(arizona->regmap,
				       ARIZONA_GPIO1_CTRL + offset,
				       ARIZONA_GPN_DIR, ARIZONA_GPN_DIR,
				       &change);
	if (ret < 0)
		return ret;

	if (change && persistent) {
		pm_runtime_mark_last_busy(chip->parent);
		pm_runtime_put_autosuspend(chip->parent);
	}

	return 0;
}

static int arizona_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct arizona_gpio *arizona_gpio = gpiochip_get_data(chip);
	struct arizona *arizona = arizona_gpio->arizona;
	unsigned int reg, val;
	int ret;

	reg = ARIZONA_GPIO1_CTRL + offset;
	ret = regmap_read(arizona->regmap, reg, &val);
	if (ret < 0)
		return ret;

	/* Resume to read actual registers for input pins */
	if (val & ARIZONA_GPN_DIR) {
		ret = pm_runtime_get_sync(chip->parent);
		if (ret < 0) {
			dev_err(chip->parent, "Failed to resume: %d\n", ret);
			return ret;
		}

		/* Register is cached, drop it to ensure a physical read */
		ret = regcache_drop_region(arizona->regmap, reg, reg);
		if (ret < 0) {
			dev_err(chip->parent, "Failed to drop cache: %d\n",
				ret);
			return ret;
		}

		ret = regmap_read(arizona->regmap, reg, &val);
		if (ret < 0)
			return ret;

		pm_runtime_mark_last_busy(chip->parent);
		pm_runtime_put_autosuspend(chip->parent);
	}

	if (val & ARIZONA_GPN_LVL)
		return 1;
	else
		return 0;
}

static int arizona_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct arizona_gpio *arizona_gpio = gpiochip_get_data(chip);
	struct arizona *arizona = arizona_gpio->arizona;
	bool persistent = gpiochip_line_is_persistent(chip, offset);
	unsigned int val;
	int ret;

	ret = regmap_read(arizona->regmap, ARIZONA_GPIO1_CTRL + offset, &val);
	if (ret < 0)
		return ret;

	if ((val & ARIZONA_GPN_DIR) && persistent) {
		ret = pm_runtime_get_sync(chip->parent);
		if (ret < 0) {
			dev_err(chip->parent, "Failed to resume: %d\n", ret);
			return ret;
		}
	}

	if (value)
		value = ARIZONA_GPN_LVL;

	return regmap_update_bits(arizona->regmap, ARIZONA_GPIO1_CTRL + offset,
				  ARIZONA_GPN_DIR | ARIZONA_GPN_LVL, value);
}

static void arizona_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct arizona_gpio *arizona_gpio = gpiochip_get_data(chip);
	struct arizona *arizona = arizona_gpio->arizona;

	if (value)
		value = ARIZONA_GPN_LVL;

	regmap_update_bits(arizona->regmap, ARIZONA_GPIO1_CTRL + offset,
			   ARIZONA_GPN_LVL, value);
}

static const struct gpio_chip template_chip = {
	.label			= "arizona",
	.owner			= THIS_MODULE,
	.direction_input	= arizona_gpio_direction_in,
	.get			= arizona_gpio_get,
	.direction_output	= arizona_gpio_direction_out,
	.set			= arizona_gpio_set,
	.can_sleep		= true,
};

static int arizona_gpio_probe(struct platform_device *pdev)
{
	struct arizona *arizona = dev_get_drvdata(pdev->dev.parent);
	struct arizona_pdata *pdata = &arizona->pdata;
	struct arizona_gpio *arizona_gpio;
	int ret;

	arizona_gpio = devm_kzalloc(&pdev->dev, sizeof(*arizona_gpio),
				    GFP_KERNEL);
	if (!arizona_gpio)
		return -ENOMEM;

	arizona_gpio->arizona = arizona;
	arizona_gpio->gpio_chip = template_chip;
	arizona_gpio->gpio_chip.parent = &pdev->dev;
#ifdef CONFIG_OF_GPIO
	arizona_gpio->gpio_chip.of_node = arizona->dev->of_node;
#endif

	switch (arizona->type) {
	case WM5102:
	case WM5110:
	case WM8280:
	case WM8997:
	case WM8998:
	case WM1814:
		arizona_gpio->gpio_chip.ngpio = 5;
		break;
	case WM1831:
	case CS47L24:
		arizona_gpio->gpio_chip.ngpio = 2;
		break;
	default:
		dev_err(&pdev->dev, "Unknown chip variant %d\n",
			arizona->type);
		return -EINVAL;
	}

	if (pdata->gpio_base)
		arizona_gpio->gpio_chip.base = pdata->gpio_base;
	else
		arizona_gpio->gpio_chip.base = -1;

	pm_runtime_enable(&pdev->dev);

	ret = devm_gpiochip_add_data(&pdev->dev, &arizona_gpio->gpio_chip,
				     arizona_gpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n",
			ret);
		return ret;
	}

	return 0;
}

static struct platform_driver arizona_gpio_driver = {
	.driver.name	= "arizona-gpio",
	.probe		= arizona_gpio_probe,
};

module_platform_driver(arizona_gpio_driver);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("GPIO interface for Arizona devices");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:arizona-gpio");
