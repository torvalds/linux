/*
 *  CLPS711X GPIO driver
 *
 *  Copyright (C) 2012,2013 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/basic_mmio_gpio.h>
#include <linux/platform_device.h>

static int clps711x_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	void __iomem *dat, *dir;
	struct bgpio_chip *bgc;
	struct resource *res;
	int err, id = np ? of_alias_get_id(np, "gpio") : pdev->id;

	if ((id < 0) || (id > 4))
		return -ENODEV;

	bgc = devm_kzalloc(&pdev->dev, sizeof(*bgc), GFP_KERNEL);
	if (!bgc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dat = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dat))
		return PTR_ERR(dat);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	dir = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	switch (id) {
	case 3:
		/* PORTD is inverted logic for direction register */
		err = bgpio_init(bgc, &pdev->dev, 1, dat, NULL, NULL,
				 NULL, dir, 0);
		break;
	default:
		err = bgpio_init(bgc, &pdev->dev, 1, dat, NULL, NULL,
				 dir, NULL, 0);
		break;
	}

	if (err)
		return err;

	switch (id) {
	case 4:
		/* PORTE is 3 lines only */
		bgc->gc.ngpio = 3;
		break;
	default:
		break;
	}

	bgc->gc.base = id * 8;
	platform_set_drvdata(pdev, bgc);

	return gpiochip_add(&bgc->gc);
}

static int clps711x_gpio_remove(struct platform_device *pdev)
{
	struct bgpio_chip *bgc = platform_get_drvdata(pdev);

	return bgpio_remove(bgc);
}

static const struct of_device_id __maybe_unused clps711x_gpio_ids[] = {
	{ .compatible = "cirrus,clps711x-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, clps711x_gpio_ids);

static struct platform_driver clps711x_gpio_driver = {
	.driver	= {
		.name		= "clps711x-gpio",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(clps711x_gpio_ids),
	},
	.probe	= clps711x_gpio_probe,
	.remove	= clps711x_gpio_remove,
};
module_platform_driver(clps711x_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("CLPS711X GPIO driver");
