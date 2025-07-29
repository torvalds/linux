// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * 74xx MMIO GPIO driver
 *
 *  Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define MMIO_74XX_DIR_IN	BIT(8)
#define MMIO_74XX_DIR_OUT	BIT(9)
#define MMIO_74XX_BIT_CNT(x)	((x) & GENMASK(7, 0))

struct mmio_74xx_gpio_priv {
	struct gpio_generic_chip gen_gc;
	unsigned int flags;
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

static int mmio_74xx_get_direction(struct gpio_chip *gc, unsigned offset)
{
	struct mmio_74xx_gpio_priv *priv = gpiochip_get_data(gc);

	if (priv->flags & MMIO_74XX_DIR_OUT)
		return GPIO_LINE_DIRECTION_OUT;

	return  GPIO_LINE_DIRECTION_IN;
}

static int mmio_74xx_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct mmio_74xx_gpio_priv *priv = gpiochip_get_data(gc);

	if (priv->flags & MMIO_74XX_DIR_IN)
		return 0;

	return -ENOTSUPP;
}

static int mmio_74xx_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct mmio_74xx_gpio_priv *priv = gpiochip_get_data(gc);

	if (priv->flags & MMIO_74XX_DIR_OUT)
		return gpio_generic_chip_set(&priv->gen_gc, gpio, val);

	return -ENOTSUPP;
}

static int mmio_74xx_gpio_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config = { };
	struct mmio_74xx_gpio_priv *priv;
	void __iomem *dat;
	int err;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->flags = (uintptr_t)device_get_match_data(&pdev->dev);

	dat = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dat))
		return PTR_ERR(dat);

	config.dev = &pdev->dev;
	config.sz = DIV_ROUND_UP(MMIO_74XX_BIT_CNT(priv->flags), 8);
	config.dat = dat;

	err = gpio_generic_chip_init(&priv->gen_gc, &config);
	if (err)
		return err;

	priv->gen_gc.gc.direction_input = mmio_74xx_dir_in;
	priv->gen_gc.gc.direction_output = mmio_74xx_dir_out;
	priv->gen_gc.gc.get_direction = mmio_74xx_get_direction;
	priv->gen_gc.gc.ngpio = MMIO_74XX_BIT_CNT(priv->flags);
	priv->gen_gc.gc.owner = THIS_MODULE;

	return devm_gpiochip_add_data(&pdev->dev, &priv->gen_gc.gc, priv);
}

static struct platform_driver mmio_74xx_gpio_driver = {
	.driver	= {
		.name		= "74xx-mmio-gpio",
		.of_match_table	= mmio_74xx_gpio_ids,
	},
	.probe	= mmio_74xx_gpio_probe,
};
module_platform_driver(mmio_74xx_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("74xx MMIO GPIO driver");
