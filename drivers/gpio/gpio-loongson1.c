/*
 * GPIO Driver for Loongson 1 SoC
 *
 * Copyright (C) 2015-2016 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>

/* Loongson 1 GPIO Register Definitions */
#define GPIO_CFG		0x0
#define GPIO_DIR		0x10
#define GPIO_DATA		0x20
#define GPIO_OUTPUT		0x30

static void __iomem *gpio_reg_base;

static int ls1x_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);
	__raw_writel(__raw_readl(gpio_reg_base + GPIO_CFG) | BIT(offset),
		     gpio_reg_base + GPIO_CFG);
	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	return 0;
}

static void ls1x_gpio_free(struct gpio_chip *gc, unsigned int offset)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);
	__raw_writel(__raw_readl(gpio_reg_base + GPIO_CFG) & ~BIT(offset),
		     gpio_reg_base + GPIO_CFG);
	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static int ls1x_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_chip *gc;
	int ret;

	gc = devm_kzalloc(dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	gpio_reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpio_reg_base))
		return PTR_ERR(gpio_reg_base);

	ret = bgpio_init(gc, dev, 4, gpio_reg_base + GPIO_DATA,
			 gpio_reg_base + GPIO_OUTPUT, NULL,
			 NULL, gpio_reg_base + GPIO_DIR, 0);
	if (ret)
		goto err;

	gc->owner = THIS_MODULE;
	gc->request = ls1x_gpio_request;
	gc->free = ls1x_gpio_free;
	gc->base = pdev->id * 32;

	ret = devm_gpiochip_add_data(dev, gc, NULL);
	if (ret)
		goto err;

	platform_set_drvdata(pdev, gc);
	dev_info(dev, "Loongson1 GPIO driver registered\n");

	return 0;
err:
	dev_err(dev, "failed to register GPIO device\n");
	return ret;
}

static struct platform_driver ls1x_gpio_driver = {
	.probe	= ls1x_gpio_probe,
	.driver	= {
		.name	= "ls1x-gpio",
	},
};

module_platform_driver(ls1x_gpio_driver);

MODULE_AUTHOR("Kelvin Cheung <keguang.zhang@gmail.com>");
MODULE_DESCRIPTION("Loongson1 GPIO driver");
MODULE_LICENSE("GPL");
