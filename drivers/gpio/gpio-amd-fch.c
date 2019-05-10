// SPDX-License-Identifier: GPL-2.0+

/*
 * GPIO driver for the AMD G series FCH (eg. GX-412TC)
 *
 * Copyright (C) 2018 metux IT consult
 * Author: Enrico Weigelt, metux IT consult <info@metux.net>
 *
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/driver.h>
#include <linux/platform_data/gpio/gpio-amd-fch.h>
#include <linux/spinlock.h>

#define AMD_FCH_MMIO_BASE		0xFED80000
#define AMD_FCH_GPIO_BANK0_BASE		0x1500
#define AMD_FCH_GPIO_SIZE		0x0300

#define AMD_FCH_GPIO_FLAG_DIRECTION	BIT(23)
#define AMD_FCH_GPIO_FLAG_WRITE		BIT(22)
#define AMD_FCH_GPIO_FLAG_READ		BIT(16)

static struct resource amd_fch_gpio_iores =
	DEFINE_RES_MEM_NAMED(
		AMD_FCH_MMIO_BASE + AMD_FCH_GPIO_BANK0_BASE,
		AMD_FCH_GPIO_SIZE,
		"amd-fch-gpio-iomem");

struct amd_fch_gpio_priv {
	struct platform_device		*pdev;
	struct gpio_chip		gc;
	void __iomem			*base;
	struct amd_fch_gpio_pdata	*pdata;
	spinlock_t			lock;
};

static void __iomem *amd_fch_gpio_addr(struct amd_fch_gpio_priv *priv,
				       unsigned int gpio)
{
	return priv->base + priv->pdata->gpio_reg[gpio]*sizeof(u32);
}

static int amd_fch_gpio_direction_input(struct gpio_chip *gc,
					unsigned int offset)
{
	unsigned long flags;
	struct amd_fch_gpio_priv *priv = gpiochip_get_data(gc);
	void __iomem *ptr = amd_fch_gpio_addr(priv, offset);

	spin_lock_irqsave(&priv->lock, flags);
	writel_relaxed(readl_relaxed(ptr) & ~AMD_FCH_GPIO_FLAG_DIRECTION, ptr);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int amd_fch_gpio_direction_output(struct gpio_chip *gc,
					 unsigned int gpio, int value)
{
	unsigned long flags;
	struct amd_fch_gpio_priv *priv = gpiochip_get_data(gc);
	void __iomem *ptr = amd_fch_gpio_addr(priv, gpio);
	u32 val;

	spin_lock_irqsave(&priv->lock, flags);

	val = readl_relaxed(ptr);
	if (value)
		val |= AMD_FCH_GPIO_FLAG_WRITE;
	else
		val &= ~AMD_FCH_GPIO_FLAG_WRITE;

	writel_relaxed(val | AMD_FCH_GPIO_FLAG_DIRECTION, ptr);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int amd_fch_gpio_get_direction(struct gpio_chip *gc, unsigned int gpio)
{
	int ret;
	unsigned long flags;
	struct amd_fch_gpio_priv *priv = gpiochip_get_data(gc);
	void __iomem *ptr = amd_fch_gpio_addr(priv, gpio);

	spin_lock_irqsave(&priv->lock, flags);
	ret = (readl_relaxed(ptr) & AMD_FCH_GPIO_FLAG_DIRECTION);
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static void amd_fch_gpio_set(struct gpio_chip *gc,
			     unsigned int gpio, int value)
{
	unsigned long flags;
	struct amd_fch_gpio_priv *priv = gpiochip_get_data(gc);
	void __iomem *ptr = amd_fch_gpio_addr(priv, gpio);
	u32 mask;

	spin_lock_irqsave(&priv->lock, flags);

	mask = readl_relaxed(ptr);
	if (value)
		mask |= AMD_FCH_GPIO_FLAG_WRITE;
	else
		mask &= ~AMD_FCH_GPIO_FLAG_WRITE;
	writel_relaxed(mask, ptr);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static int amd_fch_gpio_get(struct gpio_chip *gc,
			    unsigned int offset)
{
	unsigned long flags;
	int ret;
	struct amd_fch_gpio_priv *priv = gpiochip_get_data(gc);
	void __iomem *ptr = amd_fch_gpio_addr(priv, offset);

	spin_lock_irqsave(&priv->lock, flags);
	ret = (readl_relaxed(ptr) & AMD_FCH_GPIO_FLAG_READ);
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static int amd_fch_gpio_request(struct gpio_chip *chip,
				unsigned int gpio_pin)
{
	return 0;
}

static int amd_fch_gpio_probe(struct platform_device *pdev)
{
	struct amd_fch_gpio_priv *priv;
	struct amd_fch_gpio_pdata *pdata;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		dev_err(&pdev->dev, "no platform_data\n");
		return -ENOENT;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdata	= pdata;
	priv->pdev	= pdev;

	priv->gc.owner			= THIS_MODULE;
	priv->gc.parent			= &pdev->dev;
	priv->gc.label			= dev_name(&pdev->dev);
	priv->gc.ngpio			= priv->pdata->gpio_num;
	priv->gc.names			= priv->pdata->gpio_names;
	priv->gc.base			= -1;
	priv->gc.request		= amd_fch_gpio_request;
	priv->gc.direction_input	= amd_fch_gpio_direction_input;
	priv->gc.direction_output	= amd_fch_gpio_direction_output;
	priv->gc.get_direction		= amd_fch_gpio_get_direction;
	priv->gc.get			= amd_fch_gpio_get;
	priv->gc.set			= amd_fch_gpio_set;

	spin_lock_init(&priv->lock);

	priv->base = devm_ioremap_resource(&pdev->dev, &amd_fch_gpio_iores);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	platform_set_drvdata(pdev, priv);

	return devm_gpiochip_add_data(&pdev->dev, &priv->gc, priv);
}

static struct platform_driver amd_fch_gpio_driver = {
	.driver = {
		.name = AMD_FCH_GPIO_DRIVER_NAME,
	},
	.probe = amd_fch_gpio_probe,
};

module_platform_driver(amd_fch_gpio_driver);

MODULE_AUTHOR("Enrico Weigelt, metux IT consult <info@metux.net>");
MODULE_DESCRIPTION("AMD G-series FCH GPIO driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" AMD_FCH_GPIO_DRIVER_NAME);
