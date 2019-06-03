// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/plat-iop/gpio.c
 * GPIO handling for Intel IOP3xx processors.
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
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
	struct gpio_chip *gc;
	void __iomem *base;
	int err;

	gc = devm_kzalloc(&pdev->dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
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

MODULE_DESCRIPTION("GPIO handling for Intel IOP3xx processors");
MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>");
MODULE_LICENSE("GPL");
