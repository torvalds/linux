/*
 * 74xx MMIO GPIO driver
 *
 *  Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/basic_mmio_gpio.h>
#include <linux/platform_device.h>

#define MMIO_74XX_DIR_IN	(0 << 8)
#define MMIO_74XX_DIR_OUT	(1 << 8)
#define MMIO_74XX_BIT_CNT(x)	((x) & 0xff)

struct mmio_74xx_gpio_priv {
	struct bgpio_chip	bgc;
	unsigned		flags;
};

static const struct of_device_id mmio_74xx_gpio_ids[] = {
	{
		.compatible	= "ti,741g125",
		.data		= (const void *)(MMIO_74XX_DIR_IN | 1),
	},
	{
		.compatible	= "ti,742g125",
		.data		= (const void *)(MMIO_74XX_DIR_IN | 2),
	},
	{
		.compatible	= "ti,74125",
		.data		= (const void *)(MMIO_74XX_DIR_IN | 4),
	},
	{
		.compatible	= "ti,74365",
		.data		= (const void *)(MMIO_74XX_DIR_IN | 6),
	},
	{
		.compatible	= "ti,74244",
		.data		= (const void *)(MMIO_74XX_DIR_IN | 8),
	},
	{
		.compatible	= "ti,741624",
		.data		= (const void *)(MMIO_74XX_DIR_IN | 16),
	},
	{
		.compatible	= "ti,741g74",
		.data		= (const void *)(MMIO_74XX_DIR_OUT | 1),
	},
	{
		.compatible	= "ti,7474",
		.data		= (const void *)(MMIO_74XX_DIR_OUT | 2),
	},
	{
		.compatible	= "ti,74175",
		.data		= (const void *)(MMIO_74XX_DIR_OUT | 4),
	},
	{
		.compatible	= "ti,74174",
		.data		= (const void *)(MMIO_74XX_DIR_OUT | 6),
	},
	{
		.compatible	= "ti,74273",
		.data		= (const void *)(MMIO_74XX_DIR_OUT | 8),
	},
	{
		.compatible	= "ti,7416374",
		.data		= (const void *)(MMIO_74XX_DIR_OUT | 16),
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mmio_74xx_gpio_ids);

static inline struct mmio_74xx_gpio_priv *to_74xx_gpio(struct gpio_chip *gc)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);

	return container_of(bgc, struct mmio_74xx_gpio_priv, bgc);
}

static int mmio_74xx_get_direction(struct gpio_chip *gc, unsigned offset)
{
	struct mmio_74xx_gpio_priv *priv = to_74xx_gpio(gc);

	return (priv->flags & MMIO_74XX_DIR_OUT) ? GPIOF_DIR_OUT : GPIOF_DIR_IN;
}

static int mmio_74xx_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct mmio_74xx_gpio_priv *priv = to_74xx_gpio(gc);

	return (priv->flags & MMIO_74XX_DIR_OUT) ? -ENOTSUPP : 0;
}

static int mmio_74xx_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct mmio_74xx_gpio_priv *priv = to_74xx_gpio(gc);

	if (priv->flags & MMIO_74XX_DIR_OUT) {
		gc->set(gc, gpio, val);
		return 0;
	}

	return -ENOTSUPP;
}

static int mmio_74xx_gpio_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct mmio_74xx_gpio_priv *priv;
	struct resource *res;
	void __iomem *dat;
	int err;

	of_id = of_match_device(mmio_74xx_gpio_ids, &pdev->dev);
	if (!of_id)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dat = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dat))
		return PTR_ERR(dat);

	priv->flags = (uintptr_t) of_id->data;

	err = bgpio_init(&priv->bgc, &pdev->dev,
			 DIV_ROUND_UP(MMIO_74XX_BIT_CNT(priv->flags), 8),
			 dat, NULL, NULL, NULL, NULL, 0);
	if (err)
		return err;

	priv->bgc.gc.direction_input = mmio_74xx_dir_in;
	priv->bgc.gc.direction_output = mmio_74xx_dir_out;
	priv->bgc.gc.get_direction = mmio_74xx_get_direction;
	priv->bgc.gc.ngpio = MMIO_74XX_BIT_CNT(priv->flags);
	priv->bgc.gc.owner = THIS_MODULE;

	platform_set_drvdata(pdev, priv);

	return gpiochip_add(&priv->bgc.gc);
}

static int mmio_74xx_gpio_remove(struct platform_device *pdev)
{
	struct mmio_74xx_gpio_priv *priv = platform_get_drvdata(pdev);

	return bgpio_remove(&priv->bgc);
}

static struct platform_driver mmio_74xx_gpio_driver = {
	.driver	= {
		.name		= "74xx-mmio-gpio",
		.of_match_table	= mmio_74xx_gpio_ids,
	},
	.probe	= mmio_74xx_gpio_probe,
	.remove	= mmio_74xx_gpio_remove,
};
module_platform_driver(mmio_74xx_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("74xx MMIO GPIO driver");
