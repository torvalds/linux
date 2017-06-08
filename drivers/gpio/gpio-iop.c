/*
 * arch/arm/plat-iop/gpio.c
 * GPIO handling for Intel IOP3xx processors.
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>

#define IOP3XX_GPOE	0x0000
#define IOP3XX_GPID	0x0004
#define IOP3XX_GPOD	0x0008

static int iop3xx_gpio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct gpio_chip *gc;
	void __iomem *base;
	int err;

	gc = devm_kzalloc(&pdev->dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	err = bgpio_init(gc, &pdev->dev, 1, base + IOP3XX_GPID,
			 base + IOP3XX_GPOD, NULL, NULL, base + IOP3XX_GPOE, 0);
	if (err)
		return err;

	gc->base = 0;
	gc->owner = THIS_MODULE;

	return devm_gpiochip_add_data(&pdev->dev, gc, NULL);
}

static struct platform_driver iop3xx_gpio_driver = {
	.driver = {
		.name = "gpio-iop",
	},
	.probe = iop3xx_gpio_probe,
};

static int __init iop3xx_gpio_init(void)
{
	return platform_driver_register(&iop3xx_gpio_driver);
}
arch_initcall(iop3xx_gpio_init);
