// SPDX-License-Identifier: GPL-2.0
/*
 * Common CPM GPIO wrapper for the CPM GPIO ports
 *
 * Author: Christophe Leroy <christophe.leroy@c-s.fr>
 *
 * Copyright 2017 CS Systemes d'Information.
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <asm/cpm.h>
#ifdef CONFIG_8xx_GPIO
#include <asm/cpm1.h>
#endif

static int cpm_gpio_probe(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	int (*gp_add)(struct device *dev) = of_device_get_match_data(dev);

	if (!gp_add)
		return -ENODEV;

	return gp_add(dev);
}

static const struct of_device_id cpm_gpio_match[] = {
#ifdef CONFIG_8xx_GPIO
	{
		.compatible = "fsl,cpm1-pario-bank-a",
		.data = cpm1_gpiochip_add16,
	},
	{
		.compatible = "fsl,cpm1-pario-bank-b",
		.data = cpm1_gpiochip_add32,
	},
	{
		.compatible = "fsl,cpm1-pario-bank-c",
		.data = cpm1_gpiochip_add16,
	},
	{
		.compatible = "fsl,cpm1-pario-bank-d",
		.data = cpm1_gpiochip_add16,
	},
	/* Port E uses CPM2 layout */
	{
		.compatible = "fsl,cpm1-pario-bank-e",
		.data = cpm2_gpiochip_add32,
	},
#endif
	{
		.compatible = "fsl,cpm2-pario-bank",
		.data = cpm2_gpiochip_add32,
	},
	{},
};
MODULE_DEVICE_TABLE(of, cpm_gpio_match);

static struct platform_driver cpm_gpio_driver = {
	.probe		= cpm_gpio_probe,
	.driver		= {
		.name	= "cpm-gpio",
		.of_match_table	= cpm_gpio_match,
	},
};

static int __init cpm_gpio_init(void)
{
	return platform_driver_register(&cpm_gpio_driver);
}
arch_initcall(cpm_gpio_init);

MODULE_AUTHOR("Christophe Leroy <christophe.leroy@c-s.fr>");
MODULE_DESCRIPTION("Driver for CPM GPIO");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cpm-gpio");
