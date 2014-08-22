/*
 *  drivers/gpio/rt5025-gpio.c
 *  Driver foo Richtek RT5025 PMIC GPIO
 *
 *  Copyright (C) 2014 Richtek Technologh Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <linux/mfd/rt5025.h>
#include <linux/mfd/rt5025-gpio.h>

struct rt5025_gpio_info {
	struct i2c_client *i2c;
	unsigned gpio_base;
	unsigned irq_base;
	int ngpio;
	struct gpio_chip gpio_chip;
};

static inline int find_rt5025_gpioreg(unsigned off, int *gpio_reg)
{
	int ret = 0;

	switch (off) {
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
	if (ret < 0) {
		dev_err(chip->dev, "not a valid gpio index\n");
		return ret;
	}

	ret = rt5025_clr_bits(gi->i2c, gpio_reg, RT5025_GPIO_DIRMASK);
	if (ret < 0) {
		dev_err(chip->dev, "set gpio input fail\n");
		return ret;
	}

	return 0;
}

static int rt5025_gpio_direction_output(struct gpio_chip *chip,
	unsigned offset, int value)
{
	struct rt5025_gpio_info *gi = dev_get_drvdata(chip->dev);
	int gpio_reg = 0;
	int ret = 0;

	ret = find_rt5025_gpioreg(offset, &gpio_reg);
	if (ret < 0) {
		dev_err(chip->dev, "not a valid gpio index\n");
		return ret;
	}

	ret = rt5025_clr_bits(gi->i2c, gpio_reg, RT5025_GPIO_DIRSHIFT);
	if (ret < 0) {
		dev_err(chip->dev, "clr gpio direction fail\n");
		return ret;
	}

	ret = rt5025_set_bits(gi->i2c, gpio_reg,
		RT5025_GPIO_OUTPUT<<RT5025_GPIO_DIRSHIFT);
	if (ret < 0) {
		dev_err(chip->dev, "set gpio output dir fail\n");
		return ret;
	}

	if (value)
		ret = rt5025_set_bits(gi->i2c, gpio_reg,
		RT5025_GPIO_OVALUEMASK);
	else
		ret = rt5025_clr_bits(gi->i2c, gpio_reg,
		RT5025_GPIO_OVALUEMASK);

	if (ret < 0) {
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
	if (ret < 0) {
		dev_err(chip->dev, "not a valid gpio index\n");
		return ret;
	}

	ret = rt5025_reg_read(gi->i2c, gpio_reg);
	if (ret < 0) {
		dev_err(chip->dev, "read gpio register fail\n");
		return ret;
	}

	return (ret&RT5025_GPIO_IVALUEMASK)?1 : 0;
}

static void rt5025_gpio_set_value(struct gpio_chip *chip,
	unsigned offset, int value)
{
	struct rt5025_gpio_info *gi = dev_get_drvdata(chip->dev);
	int gpio_reg = 0;
	int ret = 0;

	ret = find_rt5025_gpioreg(offset, &gpio_reg);
	if (ret < 0) {
		dev_err(chip->dev, "not a valid gpio index\n");
		return;
	}

	if (value)
		ret = rt5025_set_bits(gi->i2c, gpio_reg,
		RT5025_GPIO_OVALUEMASK);
	else
		ret = rt5025_clr_bits(gi->i2c, gpio_reg,
		RT5025_GPIO_OVALUEMASK);

	if (ret < 0)
		dev_err(chip->dev, "read gpio register fail\n");
}

static int rt_parse_dt(struct rt5025_gpio_info *gi, struct device *dev)
{
	#ifdef CONFIG_OF
	struct device_node *np = dev->of_node;

	of_property_read_u32(np, "rt,ngpio", &gi->ngpio);
	#endif /* #ifdef CONFIG_OF */
	return 0;
}

static int rt_parse_pdata(struct rt5025_gpio_info *gi, struct device *dev)
{
	struct rt5025_gpio_data *gpio_pdata = dev->platform_data;

	gi->ngpio = gpio_pdata->ngpio;
	return 0;
}

static int rt5025_gpio_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_platform_data *pdata = (pdev->dev.parent)->platform_data;
	struct rt5025_gpio_info *gi;
	bool use_dt = pdev->dev.of_node;
	int rc = 0;

	gi = devm_kzalloc(&pdev->dev, sizeof(*gi), GFP_KERNEL);
	if (!gi)
		return -ENOMEM;


	gi->i2c = chip->i2c;
	if (use_dt) {
		rt_parse_dt(gi, &pdev->dev);
	} else {
		if (!pdata) {
			rc = -EINVAL;
			goto out_dev;
		}
		pdev->dev.platform_data = pdata->gpio_pdata;
		rt_parse_pdata(gi, &pdev->dev);
	}

	gi->gpio_chip.direction_input  = rt5025_gpio_direction_input;
	gi->gpio_chip.direction_output = rt5025_gpio_direction_output;
	gi->gpio_chip.get = rt5025_gpio_get_value;
	gi->gpio_chip.set = rt5025_gpio_set_value;
	gi->gpio_chip.can_sleep = 0;

	gi->gpio_chip.base = -1;
	gi->gpio_chip.ngpio = gi->ngpio;
	gi->gpio_chip.label = pdev->name;
	gi->gpio_chip.dev = &pdev->dev;
	gi->gpio_chip.owner = THIS_MODULE;

	rc = gpiochip_add(&gi->gpio_chip);
	if (rc)
		goto out_dev;

	platform_set_drvdata(pdev, gi);
	dev_info(&pdev->dev, "driver successfully loaded\n");
	return rc;
out_dev:
	return rc;
}

static int rt5025_gpio_remove(struct platform_device *pdev)
{
	int rc = 0;
	struct rt5025_gpio_info *gi = platform_get_drvdata(pdev);

	rc = gpiochip_remove(&gi->gpio_chip);
	dev_info(&pdev->dev, "\n");
	return 0;
}

static struct of_device_id rt_match_table[] = {
	{ .compatible = "rt,rt5025-gpio",},
	{},
};

static struct platform_driver rt5025_gpio_driver = {
	.driver = {
		.name = RT5025_DEV_NAME "-gpio",
		.owner = THIS_MODULE,
		.of_match_table = rt_match_table,
	},
	.probe = rt5025_gpio_probe,
	.remove = rt5025_gpio_remove,
};

static int rt5025_gpio_init(void)
{
	return platform_driver_register(&rt5025_gpio_driver);
}
fs_initcall_sync(rt5025_gpio_init);

static void rt5025_gpio_exit(void)
{
	platform_driver_unregister(&rt5025_gpio_driver);
}
module_exit(rt5025_gpio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("GPIO driver for RT5025");
MODULE_ALIAS("platform:" RT5025_DEV_NAME "-gpio");
MODULE_VERSION(RT5025_DRV_VER);
