// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO driver for TI TPS65219 PMICs
 *
 * Copyright (C) 2022 Texas Instruments Incorporated - http://www.ti.com/
 */

#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/tps65219.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define TPS65219_GPIO0_DIR_MASK		BIT(3)
#define TPS65219_GPIO0_OFFSET		2
#define TPS65219_GPIO0_IDX		0

struct tps65219_gpio {
	struct gpio_chip gpio_chip;
	struct tps65219 *tps;
};

static int tps65219_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct tps65219_gpio *gpio = gpiochip_get_data(gc);
	int ret, val;

	if (offset != TPS65219_GPIO0_IDX)
		return GPIO_LINE_DIRECTION_OUT;

	ret = regmap_read(gpio->tps->regmap, TPS65219_REG_MFP_1_CONFIG, &val);
	if (ret)
		return ret;

	return !!(val & TPS65219_GPIO0_DIR_MASK);
}

static int tps65219_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct tps65219_gpio *gpio = gpiochip_get_data(gc);
	struct device *dev = gpio->tps->dev;
	int ret, val;

	if (offset != TPS65219_GPIO0_IDX) {
		dev_err(dev, "GPIO%d is output only, cannot get\n", offset);
		return -ENOTSUPP;
	}

	ret = regmap_read(gpio->tps->regmap, TPS65219_REG_MFP_CTRL, &val);
	if (ret)
		return ret;

	ret = !!(val & BIT(TPS65219_MFP_GPIO_STATUS_MASK));
	dev_warn(dev, "GPIO%d = %d, MULTI_DEVICE_ENABLE, not a standard GPIO\n", offset, ret);

	/*
	 * Depending on NVM config, return an error if direction is output, otherwise the GPIO0
	 * status bit.
	 */

	if (tps65219_gpio_get_direction(gc, offset) == GPIO_LINE_DIRECTION_OUT)
		return -ENOTSUPP;

	return ret;
}

static void tps65219_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct tps65219_gpio *gpio = gpiochip_get_data(gc);
	struct device *dev = gpio->tps->dev;
	int v, mask, bit;

	bit = (offset == TPS65219_GPIO0_IDX) ? TPS65219_GPIO0_OFFSET : offset - 1;

	mask = BIT(bit);
	v = value ? mask : 0;

	if (regmap_update_bits(gpio->tps->regmap, TPS65219_REG_GENERAL_CONFIG, mask, v))
		dev_err(dev, "GPIO%d, set to value %d failed.\n", offset, value);
}

static int tps65219_gpio_change_direction(struct gpio_chip *gc, unsigned int offset,
					  unsigned int direction)
{
	struct tps65219_gpio *gpio = gpiochip_get_data(gc);
	struct device *dev = gpio->tps->dev;

	/*
	 * Documentation is stating that GPIO0 direction must not be changed in Linux:
	 * Table 8-34. MFP_1_CONFIG(3): MULTI_DEVICE_ENABLE, should only be changed in INITIALIZE
	 * state (prior to ON Request).
	 * Set statically by NVM, changing direction in application can cause a hang.
	 * Below can be used for test purpose only.
	 */

#if 0
	int ret = regmap_update_bits(gpio->tps->regmap, TPS65219_REG_MFP_1_CONFIG,
				     TPS65219_GPIO0_DIR_MASK, direction);
	if (ret) {
		dev_err(dev,
			"GPIO DEBUG enabled: Fail to change direction to %u for GPIO%d.\n",
			direction, offset);
		return ret;
	}
#endif

	dev_err(dev,
		"GPIO%d direction set by NVM, change to %u failed, not allowed by specification\n",
		 offset, direction);

	return -ENOTSUPP;
}

static int tps65219_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct tps65219_gpio *gpio = gpiochip_get_data(gc);
	struct device *dev = gpio->tps->dev;

	if (offset != TPS65219_GPIO0_IDX) {
		dev_err(dev, "GPIO%d is output only, cannot change to input\n", offset);
		return -ENOTSUPP;
	}

	if (tps65219_gpio_get_direction(gc, offset) == GPIO_LINE_DIRECTION_IN)
		return 0;

	return tps65219_gpio_change_direction(gc, offset, GPIO_LINE_DIRECTION_IN);
}

static int tps65219_gpio_direction_output(struct gpio_chip *gc, unsigned int offset, int value)
{
	tps65219_gpio_set(gc, offset, value);
	if (offset != TPS65219_GPIO0_IDX)
		return 0;

	if (tps65219_gpio_get_direction(gc, offset) == GPIO_LINE_DIRECTION_OUT)
		return 0;

	return tps65219_gpio_change_direction(gc, offset, GPIO_LINE_DIRECTION_OUT);
}

static const struct gpio_chip tps65219_template_chip = {
	.label			= "tps65219-gpio",
	.owner			= THIS_MODULE,
	.get_direction		= tps65219_gpio_get_direction,
	.direction_input	= tps65219_gpio_direction_input,
	.direction_output	= tps65219_gpio_direction_output,
	.get			= tps65219_gpio_get,
	.set			= tps65219_gpio_set,
	.base			= -1,
	.ngpio			= 3,
	.can_sleep		= true,
};

static int tps65219_gpio_probe(struct platform_device *pdev)
{
	struct tps65219 *tps = dev_get_drvdata(pdev->dev.parent);
	struct tps65219_gpio *gpio;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->tps = tps;
	gpio->gpio_chip = tps65219_template_chip;
	gpio->gpio_chip.parent = tps->dev;

	return devm_gpiochip_add_data(&pdev->dev, &gpio->gpio_chip, gpio);
}

static struct platform_driver tps65219_gpio_driver = {
	.driver = {
		.name = "tps65219-gpio",
	},
	.probe = tps65219_gpio_probe,
};
module_platform_driver(tps65219_gpio_driver);

MODULE_ALIAS("platform:tps65219-gpio");
MODULE_AUTHOR("Jonathan Cormier <jcormier@criticallink.com>");
MODULE_DESCRIPTION("TPS65219 GPIO driver");
MODULE_LICENSE("GPL");
