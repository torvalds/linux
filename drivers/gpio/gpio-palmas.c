// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI Palma series PMIC's GPIO driver.
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */

#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mfd/palmas.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

struct palmas_gpio {
	struct gpio_chip gpio_chip;
	struct palmas *palmas;
};

struct palmas_device_data {
	int ngpio;
};

static int palmas_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct palmas_gpio *pg = gpiochip_get_data(gc);
	struct palmas *palmas = pg->palmas;
	unsigned int val;
	int ret;
	unsigned int reg;
	int gpio16 = (offset/8);

	offset %= 8;
	reg = (gpio16) ? PALMAS_GPIO_DATA_DIR2 : PALMAS_GPIO_DATA_DIR;

	ret = palmas_read(palmas, PALMAS_GPIO_BASE, reg, &val);
	if (ret < 0) {
		dev_err(gc->parent, "Reg 0x%02x read failed, %d\n", reg, ret);
		return ret;
	}

	if (val & BIT(offset))
		reg = (gpio16) ? PALMAS_GPIO_DATA_OUT2 : PALMAS_GPIO_DATA_OUT;
	else
		reg = (gpio16) ? PALMAS_GPIO_DATA_IN2 : PALMAS_GPIO_DATA_IN;

	ret = palmas_read(palmas, PALMAS_GPIO_BASE, reg, &val);
	if (ret < 0) {
		dev_err(gc->parent, "Reg 0x%02x read failed, %d\n", reg, ret);
		return ret;
	}
	return !!(val & BIT(offset));
}

static void palmas_gpio_set(struct gpio_chip *gc, unsigned offset,
			int value)
{
	struct palmas_gpio *pg = gpiochip_get_data(gc);
	struct palmas *palmas = pg->palmas;
	int ret;
	unsigned int reg;
	int gpio16 = (offset/8);

	offset %= 8;
	if (gpio16)
		reg = (value) ?
			PALMAS_GPIO_SET_DATA_OUT2 : PALMAS_GPIO_CLEAR_DATA_OUT2;
	else
		reg = (value) ?
			PALMAS_GPIO_SET_DATA_OUT : PALMAS_GPIO_CLEAR_DATA_OUT;

	ret = palmas_write(palmas, PALMAS_GPIO_BASE, reg, BIT(offset));
	if (ret < 0)
		dev_err(gc->parent, "Reg 0x%02x write failed, %d\n", reg, ret);
}

static int palmas_gpio_output(struct gpio_chip *gc, unsigned offset,
				int value)
{
	struct palmas_gpio *pg = gpiochip_get_data(gc);
	struct palmas *palmas = pg->palmas;
	int ret;
	unsigned int reg;
	int gpio16 = (offset/8);

	offset %= 8;
	reg = (gpio16) ? PALMAS_GPIO_DATA_DIR2 : PALMAS_GPIO_DATA_DIR;

	/* Set the initial value */
	palmas_gpio_set(gc, offset, value);

	ret = palmas_update_bits(palmas, PALMAS_GPIO_BASE, reg,
				BIT(offset), BIT(offset));
	if (ret < 0)
		dev_err(gc->parent, "Reg 0x%02x update failed, %d\n", reg,
			ret);
	return ret;
}

static int palmas_gpio_input(struct gpio_chip *gc, unsigned offset)
{
	struct palmas_gpio *pg = gpiochip_get_data(gc);
	struct palmas *palmas = pg->palmas;
	int ret;
	unsigned int reg;
	int gpio16 = (offset/8);

	offset %= 8;
	reg = (gpio16) ? PALMAS_GPIO_DATA_DIR2 : PALMAS_GPIO_DATA_DIR;

	ret = palmas_update_bits(palmas, PALMAS_GPIO_BASE, reg, BIT(offset), 0);
	if (ret < 0)
		dev_err(gc->parent, "Reg 0x%02x update failed, %d\n", reg,
			ret);
	return ret;
}

static int palmas_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct palmas_gpio *pg = gpiochip_get_data(gc);
	struct palmas *palmas = pg->palmas;

	return palmas_irq_get_virq(palmas, PALMAS_GPIO_0_IRQ + offset);
}

static const struct palmas_device_data palmas_dev_data = {
	.ngpio = 8,
};

static const struct palmas_device_data tps80036_dev_data = {
	.ngpio = 16,
};

static const struct of_device_id of_palmas_gpio_match[] = {
	{ .compatible = "ti,palmas-gpio", .data = &palmas_dev_data,},
	{ .compatible = "ti,tps65913-gpio", .data = &palmas_dev_data,},
	{ .compatible = "ti,tps65914-gpio", .data = &palmas_dev_data,},
	{ .compatible = "ti,tps80036-gpio", .data = &tps80036_dev_data,},
	{ },
};

static int palmas_gpio_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct palmas_platform_data *palmas_pdata;
	struct palmas_gpio *palmas_gpio;
	int ret;
	const struct palmas_device_data *dev_data;

	dev_data = of_device_get_match_data(&pdev->dev);
	if (!dev_data)
		dev_data = &palmas_dev_data;

	palmas_gpio = devm_kzalloc(&pdev->dev,
				sizeof(*palmas_gpio), GFP_KERNEL);
	if (!palmas_gpio)
		return -ENOMEM;

	palmas_gpio->palmas = palmas;
	palmas_gpio->gpio_chip.owner = THIS_MODULE;
	palmas_gpio->gpio_chip.label = dev_name(&pdev->dev);
	palmas_gpio->gpio_chip.ngpio = dev_data->ngpio;
	palmas_gpio->gpio_chip.can_sleep = true;
	palmas_gpio->gpio_chip.direction_input = palmas_gpio_input;
	palmas_gpio->gpio_chip.direction_output = palmas_gpio_output;
	palmas_gpio->gpio_chip.to_irq = palmas_gpio_to_irq;
	palmas_gpio->gpio_chip.set	= palmas_gpio_set;
	palmas_gpio->gpio_chip.get	= palmas_gpio_get;
	palmas_gpio->gpio_chip.parent = &pdev->dev;

	palmas_pdata = dev_get_platdata(palmas->dev);
	if (palmas_pdata && palmas_pdata->gpio_base)
		palmas_gpio->gpio_chip.base = palmas_pdata->gpio_base;
	else
		palmas_gpio->gpio_chip.base = -1;

	ret = devm_gpiochip_add_data(&pdev->dev, &palmas_gpio->gpio_chip,
				     palmas_gpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, palmas_gpio);
	return ret;
}

static struct platform_driver palmas_gpio_driver = {
	.driver.name	= "palmas-gpio",
	.driver.of_match_table = of_palmas_gpio_match,
	.probe		= palmas_gpio_probe,
};

static int __init palmas_gpio_init(void)
{
	return platform_driver_register(&palmas_gpio_driver);
}
subsys_initcall(palmas_gpio_init);
