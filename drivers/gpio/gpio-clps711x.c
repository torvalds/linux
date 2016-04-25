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
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>

static int clps711x_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	void __iomem *dat, *dir;
	struct gpio_chip *gc;
	struct resource *res;
	int err, id = np ? of_alias_get_id(np, "gpio") : pdev->id;

	if ((id < 0) || (id > 4))
		return -ENODEV;

	gc = devm_kzalloc(&pdev->dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
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
		err = bgpio_init(gc, &pdev->dev, 1, dat, NULL, NULL,
				 NULL, dir, 0);
		break;
	default:
		err = bgpio_init(gc, &pdev->dev, 1, dat, NULL, NULL,
				 dir, NULL, 0);
		break;
	}

	if (err)
		return err;

	switch (id) {
	case 4:
		/* PORTE is 3 lines only */
		gc->ngpio = 3;
		break;
	default:
		break;
	}

	gc->base = id * 8;
	gc->owner = THIS_MODULE;
	platform_set_drvdata(pdev, gc);

	return devm_gpiochip_add_data(&pdev->dev, gc, NULL);
}

static const struct of_device_id __maybe_unused clps711x_gpio_ids[] = {
	{ .compatible = "cirrus,clps711x-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, clps711x_gpio_ids);

static struct platform_driver clps711x_gpio_driver = {
	.driver	= {
		.name		= "clps711x-gpio",
		.of_match_table	= of_match_ptr(clps711x_gpio_ids),
	},
	.probe	= clps711x_gpio_probe,
};
module_platform_driver(clps711x_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("CLPS711X GPIO driver");
MODULE_ALIAS("platform:clps711x-gpio");
