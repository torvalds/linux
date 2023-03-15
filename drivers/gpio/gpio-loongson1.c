// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO Driver for Loongson 1 SoC
 *
 * Copyright (C) 2015-2023 Keguang Zhang <keguang.zhang@gmail.com>
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

struct ls1x_gpio_chip {
	struct gpio_chip gc;
	void __iomem *reg_base;
};

static int ls1x_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
	struct ls1x_gpio_chip *ls1x_gc = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);
	__raw_writel(__raw_readl(ls1x_gc->reg_base + GPIO_CFG) | BIT(offset),
		     ls1x_gc->reg_base + GPIO_CFG);
	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	return 0;
}

static void ls1x_gpio_free(struct gpio_chip *gc, unsigned int offset)
{
	struct ls1x_gpio_chip *ls1x_gc = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);
	__raw_writel(__raw_readl(ls1x_gc->reg_base + GPIO_CFG) & ~BIT(offset),
		     ls1x_gc->reg_base + GPIO_CFG);
	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static int ls1x_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ls1x_gpio_chip *ls1x_gc;
	int ret;

	ls1x_gc = devm_kzalloc(dev, sizeof(*ls1x_gc), GFP_KERNEL);
	if (!ls1x_gc)
		return -ENOMEM;

	ls1x_gc->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ls1x_gc->reg_base))
		return PTR_ERR(ls1x_gc->reg_base);

	ret = bgpio_init(&ls1x_gc->gc, dev, 4, ls1x_gc->reg_base + GPIO_DATA,
			 ls1x_gc->reg_base + GPIO_OUTPUT, NULL,
			 NULL, ls1x_gc->reg_base + GPIO_DIR, 0);
	if (ret)
		goto err;

	ls1x_gc->gc.owner = THIS_MODULE;
	ls1x_gc->gc.request = ls1x_gpio_request;
	ls1x_gc->gc.free = ls1x_gpio_free;
	ls1x_gc->gc.base = pdev->id * 32;

	ret = devm_gpiochip_add_data(dev, &ls1x_gc->gc, ls1x_gc);
	if (ret)
		goto err;

	platform_set_drvdata(pdev, ls1x_gc);
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

MODULE_AUTHOR("Keguang Zhang <keguang.zhang@gmail.com>");
MODULE_DESCRIPTION("Loongson1 GPIO driver");
MODULE_LICENSE("GPL");
