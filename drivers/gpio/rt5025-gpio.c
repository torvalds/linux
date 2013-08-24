/*
 *  drivers/gpio/rt5025-gpio.c
 *  Driver foo Richtek RT5025 PMIC GPIO
 *
 *  Copyright (C) 2013 Richtek Electronics
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <linux/mfd/rt5025.h>
#include <linux/mfd/rt5025-gpio.h>

struct rt5025_gpio_info {
	struct i2c_client *i2c;
	unsigned gpio_base;
	unsigned irq_base;
	struct gpio_chip gpio_chip;
};

static inline int find_rt5025_gpioreg(unsigned off, int *gpio_reg)
{
	int ret = 0;
	switch (off)
	{
		case 0:
		case 1:
		case 2:
			*gpio_reg = RT5025_REG_GPIO0 + off;
			break;
		default:
			ret = -EINVAL;
	}
	return ret;
}

static int rt5025_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct rt5025_gpio_info *gi = dev_get_drvdata(chip->dev);
	int gpio_reg = 0;
	int ret = 0;
	
	ret = find_rt5025_gpioreg(offset , &gpio_reg);
	if (ret < 0)
	{
		dev_err(chip->dev, "not a valid gpio index\n");
		return ret;
	}

	ret = rt5025_clr_bits(gi->i2c, gpio_reg, RT5025_GPIO_DIRMASK);
	if (ret<0)
	{
		dev_err(chip->dev, "set gpio input fail\n");
		return ret;
	}

	return 0;
}

static int rt5025_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct rt5025_gpio_info *gi = dev_get_drvdata(chip->dev);
	int gpio_reg = 0;
	int ret = 0;
	
	ret = find_rt5025_gpioreg(offset, &gpio_reg);
	if (ret < 0)
	{
		dev_err(chip->dev, "not a valid gpio index\n");
		return ret;
	}
	
	ret = rt5025_clr_bits(gi->i2c, gpio_reg, RT5025_GPIO_DIRSHIFT);
	if (ret<0)
	{
		dev_err(chip->dev, "clr gpio direction fail\n");
		return ret;
	}

	ret = rt5025_set_bits(gi->i2c, gpio_reg, RT5025_GPIO_OUTPUT<<RT5025_GPIO_DIRSHIFT);
	if (ret<0)
	{
		dev_err(chip->dev, "set gpio output dir fail\n");
		return ret;
	}

	if (value)
		ret = rt5025_set_bits(gi->i2c, gpio_reg, RT5025_GPIO_OVALUEMASK);
	else
		ret = rt5025_clr_bits(gi->i2c, gpio_reg, RT5025_GPIO_OVALUEMASK);

	if (ret<0)
	{
		dev_err(chip->dev, "set gpio output value fail\n");
		return ret;
	}

	return 0;
}

static int rt5025_gpio_get_value(struct gpio_chip *chip, unsigned offset)
{
	struct rt5025_gpio_info *gi = dev_get_drvdata(chip->dev);
	int gpio_reg = 0;
	int ret = 0;
	
	ret = find_rt5025_gpioreg(offset, &gpio_reg);
	if (ret < 0)
	{
		dev_err(chip->dev, "not a valid gpio index\n");
		return ret;
	}
	
	ret = rt5025_reg_read(gi->i2c, gpio_reg);
	if (ret<0)
	{
		dev_err(chip->dev, "read gpio register fail\n");
		return ret;
	}

	return (ret&RT5025_GPIO_IVALUEMASK)?1:0;
}

static void rt5025_gpio_set_value(struct gpio_chip *chip, unsigned offset, int value)
{
	struct rt5025_gpio_info *gi = dev_get_drvdata(chip->dev);
	int gpio_reg = 0;
	int ret = 0;
	
	ret = find_rt5025_gpioreg(offset, &gpio_reg);
	if (ret < 0)
	{
		dev_err(chip->dev, "not a valid gpio index\n");
		return;
	}

	if (value)
		ret = rt5025_set_bits(gi->i2c, gpio_reg, RT5025_GPIO_OVALUEMASK);
	else
		ret = rt5025_clr_bits(gi->i2c, gpio_reg, RT5025_GPIO_OVALUEMASK);

	if (ret<0)
	{
		dev_err(chip->dev, "read gpio register fail\n");
	}
}

static int __devinit rt5025_gpio_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_platform_data *pdata = chip->dev->platform_data;
	struct rt5025_gpio_info *gi;
	int ret = 0;

	gi = kzalloc(sizeof(*gi), GFP_KERNEL);
	if (!gi)
		return -ENOMEM;

	gi->i2c = chip->i2c;
	gi->gpio_base = pdata->gpio_data->gpio_base;
	gi->irq_base = pdata->gpio_data->irq_base;

	gi->gpio_chip.direction_input  = rt5025_gpio_direction_input;
	gi->gpio_chip.direction_output = rt5025_gpio_direction_output;
	gi->gpio_chip.get = rt5025_gpio_get_value;
	gi->gpio_chip.set = rt5025_gpio_set_value;
	gi->gpio_chip.can_sleep = 0;

	gi->gpio_chip.base = gi->gpio_base;
	gi->gpio_chip.ngpio = RT5025_GPIO_NR;
	gi->gpio_chip.label = pdev->name;
	gi->gpio_chip.dev = &pdev->dev;
	gi->gpio_chip.owner = THIS_MODULE;

	ret = gpiochip_add(&gi->gpio_chip);
	if (ret)
		goto out_dev;
		
	platform_set_drvdata(pdev, gi);
	return ret;
out_dev:
	kfree(gi);
	return ret;
}

static int __devexit rt5025_gpio_remove(struct platform_device *pdev)
{
	int ret;
	struct rt5025_gpio_info *gi = platform_get_drvdata(pdev);

	ret = gpiochip_remove(&gi->gpio_chip);
	kfree(gi);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver rt5025_gpio_driver = 
{
	.driver = {
		.name = RT5025_DEVICE_NAME "-gpio",
		.owner = THIS_MODULE,
	},
	.probe = rt5025_gpio_probe,
	.remove = __devexit_p(rt5025_gpio_remove),
};

static int __init rt5025_gpio_init(void)
{
	return platform_driver_register(&rt5025_gpio_driver);
}
subsys_initcall_sync(rt5025_gpio_init);

static void __exit rt5025_gpio_exit(void)
{
	platform_driver_unregister(&rt5025_gpio_driver);
}
module_exit(rt5025_gpio_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("GPIO driver for RT5025");
MODULE_ALIAS("platform:" RT5025_DEVICE_NAME "-gpio");
MODULE_VERSION(RT5025_DRV_VER);
