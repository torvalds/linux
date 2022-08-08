// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO driver for TPS68470 PMIC
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Authors:
 *	Antti Laakso <antti.laakso@intel.com>
 *	Tianshu Qiu <tian.shu.qiu@intel.com>
 *	Jian Xu Zheng <jian.xu.zheng@intel.com>
 *	Yuning Pu <yuning.pu@intel.com>
 */

#include <linux/gpio/driver.h>
#include <linux/mfd/tps68470.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define TPS68470_N_LOGIC_OUTPUT	3
#define TPS68470_N_REGULAR_GPIO	7
#define TPS68470_N_GPIO	(TPS68470_N_LOGIC_OUTPUT + TPS68470_N_REGULAR_GPIO)

struct tps68470_gpio_data {
	struct regmap *tps68470_regmap;
	struct gpio_chip gc;
};

static int tps68470_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct tps68470_gpio_data *tps68470_gpio = gpiochip_get_data(gc);
	struct regmap *regmap = tps68470_gpio->tps68470_regmap;
	unsigned int reg = TPS68470_REG_GPDO;
	int val, ret;

	if (offset >= TPS68470_N_REGULAR_GPIO) {
		offset -= TPS68470_N_REGULAR_GPIO;
		reg = TPS68470_REG_SGPO;
	}

	ret = regmap_read(regmap, reg, &val);
	if (ret) {
		dev_err(tps68470_gpio->gc.parent, "reg 0x%x read failed\n",
			TPS68470_REG_SGPO);
		return ret;
	}
	return !!(val & BIT(offset));
}

static int tps68470_gpio_get_direction(struct gpio_chip *gc,
				       unsigned int offset)
{
	struct tps68470_gpio_data *tps68470_gpio = gpiochip_get_data(gc);
	struct regmap *regmap = tps68470_gpio->tps68470_regmap;
	int val, ret;

	/* rest are always outputs */
	if (offset >= TPS68470_N_REGULAR_GPIO)
		return GPIO_LINE_DIRECTION_OUT;

	ret = regmap_read(regmap, TPS68470_GPIO_CTL_REG_A(offset), &val);
	if (ret) {
		dev_err(tps68470_gpio->gc.parent, "reg 0x%x read failed\n",
			TPS68470_GPIO_CTL_REG_A(offset));
		return ret;
	}

	val &= TPS68470_GPIO_MODE_MASK;
	return val >= TPS68470_GPIO_MODE_OUT_CMOS ? GPIO_LINE_DIRECTION_OUT :
						    GPIO_LINE_DIRECTION_IN;
}

static void tps68470_gpio_set(struct gpio_chip *gc, unsigned int offset,
				int value)
{
	struct tps68470_gpio_data *tps68470_gpio = gpiochip_get_data(gc);
	struct regmap *regmap = tps68470_gpio->tps68470_regmap;
	unsigned int reg = TPS68470_REG_GPDO;

	if (offset >= TPS68470_N_REGULAR_GPIO) {
		reg = TPS68470_REG_SGPO;
		offset -= TPS68470_N_REGULAR_GPIO;
	}

	regmap_update_bits(regmap, reg, BIT(offset), value ? BIT(offset) : 0);
}

static int tps68470_gpio_output(struct gpio_chip *gc, unsigned int offset,
				int value)
{
	struct tps68470_gpio_data *tps68470_gpio = gpiochip_get_data(gc);
	struct regmap *regmap = tps68470_gpio->tps68470_regmap;

	/* rest are always outputs */
	if (offset >= TPS68470_N_REGULAR_GPIO)
		return 0;

	/* Set the initial value */
	tps68470_gpio_set(gc, offset, value);

	return regmap_update_bits(regmap, TPS68470_GPIO_CTL_REG_A(offset),
				 TPS68470_GPIO_MODE_MASK,
				 TPS68470_GPIO_MODE_OUT_CMOS);
}

static int tps68470_gpio_input(struct gpio_chip *gc, unsigned int offset)
{
	struct tps68470_gpio_data *tps68470_gpio = gpiochip_get_data(gc);
	struct regmap *regmap = tps68470_gpio->tps68470_regmap;

	/* rest are always outputs */
	if (offset >= TPS68470_N_REGULAR_GPIO)
		return -EINVAL;

	return regmap_update_bits(regmap, TPS68470_GPIO_CTL_REG_A(offset),
				   TPS68470_GPIO_MODE_MASK, 0x00);
}

static const char *tps68470_names[TPS68470_N_GPIO] = {
	"gpio.0", "gpio.1", "gpio.2", "gpio.3",
	"gpio.4", "gpio.5", "gpio.6",
	"s_enable", "s_idle", "s_resetn",
};

static int tps68470_gpio_probe(struct platform_device *pdev)
{
	struct tps68470_gpio_data *tps68470_gpio;

	tps68470_gpio = devm_kzalloc(&pdev->dev, sizeof(*tps68470_gpio),
				     GFP_KERNEL);
	if (!tps68470_gpio)
		return -ENOMEM;

	tps68470_gpio->tps68470_regmap = dev_get_drvdata(pdev->dev.parent);
	tps68470_gpio->gc.label = "tps68470-gpio";
	tps68470_gpio->gc.owner = THIS_MODULE;
	tps68470_gpio->gc.direction_input = tps68470_gpio_input;
	tps68470_gpio->gc.direction_output = tps68470_gpio_output;
	tps68470_gpio->gc.get = tps68470_gpio_get;
	tps68470_gpio->gc.get_direction = tps68470_gpio_get_direction;
	tps68470_gpio->gc.set = tps68470_gpio_set;
	tps68470_gpio->gc.can_sleep = true;
	tps68470_gpio->gc.names = tps68470_names;
	tps68470_gpio->gc.ngpio = TPS68470_N_GPIO;
	tps68470_gpio->gc.base = -1;
	tps68470_gpio->gc.parent = &pdev->dev;

	return devm_gpiochip_add_data(&pdev->dev, &tps68470_gpio->gc, tps68470_gpio);
}

static struct platform_driver tps68470_gpio_driver = {
	.driver = {
		   .name = "tps68470-gpio",
	},
	.probe = tps68470_gpio_probe,
};

builtin_platform_driver(tps68470_gpio_driver)
