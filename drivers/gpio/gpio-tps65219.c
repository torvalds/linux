// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO driver for TI TPS65214/TPS65215/TPS65219 PMICs
 *
 * Copyright (C) 2022, 2025 Texas Instruments Incorporated - http://www.ti.com/
 */

#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/tps65219.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define TPS65219_GPIO0_DIR_MASK		BIT(3)
#define TPS65214_GPIO0_DIR_MASK		BIT(1)
#define TPS6521X_GPIO0_OFFSET		2
#define TPS6521X_GPIO0_IDX		0

/*
 * TPS65214 GPIO mapping
 * Linux gpio offset 0 -> GPIO (pin16) -> bit_offset 2
 * Linux gpio offset 1 -> GPO1 (pin9 ) -> bit_offset 0
 *
 * TPS65215 & TPS65219 GPIO mapping
 * Linux gpio offset 0 -> GPIO (pin16) -> bit_offset 2
 * Linux gpio offset 1 -> GPO1 (pin8 ) -> bit_offset 0
 * Linux gpio offset 2 -> GPO2 (pin17) -> bit_offset 1
 */

struct tps65219_gpio {
	int (*change_dir)(struct gpio_chip *gc, unsigned int offset, unsigned int dir);
	struct gpio_chip gpio_chip;
	struct tps65219 *tps;
};

static int tps65214_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct tps65219_gpio *gpio = gpiochip_get_data(gc);
	int ret, val;

	if (offset != TPS6521X_GPIO0_IDX)
		return GPIO_LINE_DIRECTION_OUT;

	ret = regmap_read(gpio->tps->regmap, TPS65219_REG_GENERAL_CONFIG, &val);
	if (ret)
		return ret;

	return !(val & TPS65214_GPIO0_DIR_MASK);
}

static int tps65219_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct tps65219_gpio *gpio = gpiochip_get_data(gc);
	int ret, val;

	if (offset != TPS6521X_GPIO0_IDX)
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

	if (offset != TPS6521X_GPIO0_IDX) {
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

static int tps65219_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct tps65219_gpio *gpio = gpiochip_get_data(gc);
	int v, mask, bit;

	bit = (offset == TPS6521X_GPIO0_IDX) ? TPS6521X_GPIO0_OFFSET : offset - 1;

	mask = BIT(bit);
	v = value ? mask : 0;

	return regmap_update_bits(gpio->tps->regmap,
				  TPS65219_REG_GENERAL_CONFIG, mask, v);
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

static int tps65214_gpio_change_direction(struct gpio_chip *gc, unsigned int offset,
					  unsigned int direction)
{
	struct tps65219_gpio *gpio = gpiochip_get_data(gc);
	struct device *dev = gpio->tps->dev;
	int val, ret;

	/**
	 * Verified if GPIO or GPO in parent function
	 * Masked value: 0 = GPIO, 1 = VSEL
	 */
	ret = regmap_read(gpio->tps->regmap, TPS65219_REG_MFP_1_CONFIG, &val);
	if (ret)
		return ret;

	ret = !!(val & BIT(TPS65219_GPIO0_DIR_MASK));
	if (ret)
		dev_err(dev, "GPIO%d configured as VSEL, not GPIO\n", offset);

	ret = regmap_update_bits(gpio->tps->regmap, TPS65219_REG_GENERAL_CONFIG,
				 TPS65214_GPIO0_DIR_MASK, direction);
	if (ret)
		dev_err(dev, "Fail to change direction to %u for GPIO%d.\n", direction, offset);

	return ret;
}

static int tps65219_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct tps65219_gpio *gpio = gpiochip_get_data(gc);
	struct device *dev = gpio->tps->dev;

	if (offset != TPS6521X_GPIO0_IDX) {
		dev_err(dev, "GPIO%d is output only, cannot change to input\n", offset);
		return -ENOTSUPP;
	}

	if (tps65219_gpio_get_direction(gc, offset) == GPIO_LINE_DIRECTION_IN)
		return 0;

	return gpio->change_dir(gc, offset, GPIO_LINE_DIRECTION_IN);
}

static int tps65219_gpio_direction_output(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct tps65219_gpio *gpio = gpiochip_get_data(gc);

	tps65219_gpio_set(gc, offset, value);
	if (offset != TPS6521X_GPIO0_IDX)
		return 0;

	if (tps65219_gpio_get_direction(gc, offset) == GPIO_LINE_DIRECTION_OUT)
		return 0;

	return gpio->change_dir(gc, offset, GPIO_LINE_DIRECTION_OUT);
}

static const struct gpio_chip tps65214_template_chip = {
	.label			= "tps65214-gpio",
	.owner			= THIS_MODULE,
	.get_direction		= tps65214_gpio_get_direction,
	.direction_input	= tps65219_gpio_direction_input,
	.direction_output	= tps65219_gpio_direction_output,
	.get			= tps65219_gpio_get,
	.set			= tps65219_gpio_set,
	.base			= -1,
	.ngpio			= 2,
	.can_sleep		= true,
};

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
	enum pmic_id chip = platform_get_device_id(pdev)->driver_data;
	struct tps65219 *tps = dev_get_drvdata(pdev->dev.parent);
	struct tps65219_gpio *gpio;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	if (chip == TPS65214) {
		gpio->gpio_chip = tps65214_template_chip;
		gpio->change_dir = tps65214_gpio_change_direction;
	} else if (chip == TPS65219) {
		gpio->gpio_chip = tps65219_template_chip;
		gpio->change_dir = tps65219_gpio_change_direction;
	} else {
		return -ENODATA;
	}

	gpio->tps = tps;
	gpio->gpio_chip.parent = tps->dev;

	return devm_gpiochip_add_data(&pdev->dev, &gpio->gpio_chip, gpio);
}

static const struct platform_device_id tps6521x_gpio_id_table[] = {
	{ "tps65214-gpio", TPS65214 },
	{ "tps65219-gpio", TPS65219 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, tps6521x_gpio_id_table);

static struct platform_driver tps65219_gpio_driver = {
	.driver = {
		.name = "tps65219-gpio",
	},
	.probe = tps65219_gpio_probe,
	.id_table = tps6521x_gpio_id_table,
};
module_platform_driver(tps65219_gpio_driver);

MODULE_AUTHOR("Jonathan Cormier <jcormier@criticallink.com>");
MODULE_DESCRIPTION("TPS65214/TPS65215/TPS65219 GPIO driver");
MODULE_LICENSE("GPL");
