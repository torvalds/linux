// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  CLPS711X GPIO driver
 *
 *  Copyright (C) 2012,2013 Alexander Shiyan <shc_work@mail.ru>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/platform_device.h>

static int clps711x_gpio_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config = { };
	struct device_node *np = pdev->dev.of_node;
	struct gpio_generic_chip *gen_gc;
	void __iomem *dat, *dir;
	int err, id;

	if (!np)
		return -ENODEV;

	id = of_alias_get_id(np, "gpio");
	if ((id < 0) || (id > 4))
		return -ENODEV;

	gen_gc = devm_kzalloc(&pdev->dev, sizeof(*gen_gc), GFP_KERNEL);
	if (!gen_gc)
		return -ENOMEM;

	dat = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dat))
		return PTR_ERR(dat);

	dir = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	config.dev = &pdev->dev;
	config.sz = 1;
	config.dat = dat;

	switch (id) {
	case 3:
		/* PORTD is inverted logic for direction register */
		config.dirin = dir;
		break;
	default:
		config.dirout = dir;
		break;
	}

	err = gpio_generic_chip_init(gen_gc, &config);
	if (err)
		return err;

	switch (id) {
	case 4:
		/* PORTE is 3 lines only */
		gen_gc->gc.ngpio = 3;
		break;
	default:
		break;
	}

	gen_gc->gc.base = -1;
	gen_gc->gc.owner = THIS_MODULE;

	return devm_gpiochip_add_data(&pdev->dev, &gen_gc->gc, NULL);
}

static const struct of_device_id clps711x_gpio_ids[] = {
	{ .compatible = "cirrus,ep7209-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, clps711x_gpio_ids);

static struct platform_driver clps711x_gpio_driver = {
	.driver	= {
		.name		= "clps711x-gpio",
		.of_match_table	= clps711x_gpio_ids,
	},
	.probe	= clps711x_gpio_probe,
};
module_platform_driver(clps711x_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("CLPS711X GPIO driver");
MODULE_ALIAS("platform:clps711x-gpio");
