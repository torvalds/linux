// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Elkhart Lake PSE GPIO driver
 *
 * Copyright (c) 2023 Intel Corporation.
 *
 * Authors: Pandith N <pandith.n@intel.com>
 *          Raag Jadav <raag.jadav@intel.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include "gpio-tangier.h"

/* Each Intel EHL PSE GPIO Controller has 30 GPIO pins */
#define EHL_PSE_NGPIO		30

static int ehl_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tng_gpio *priv;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->reg_base))
		return PTR_ERR(priv->reg_base);

	priv->dev = dev;
	priv->irq = irq;

	priv->info.base = -1;
	priv->info.ngpio = EHL_PSE_NGPIO;

	priv->wake_regs.gwmr = GWMR_EHL;
	priv->wake_regs.gwsr = GWSR_EHL;
	priv->wake_regs.gsir = GSIR_EHL;

	ret = devm_tng_gpio_probe(dev, priv);
	if (ret)
		return dev_err_probe(dev, ret, "tng_gpio_probe error\n");

	platform_set_drvdata(pdev, priv);
	return 0;
}

static const struct platform_device_id ehl_gpio_ids[] = {
	{ "gpio-elkhartlake" },
	{ }
};
MODULE_DEVICE_TABLE(platform, ehl_gpio_ids);

static struct platform_driver ehl_gpio_driver = {
	.driver	= {
		.name	= "gpio-elkhartlake",
		.pm	= pm_sleep_ptr(&tng_gpio_pm_ops),
	},
	.probe		= ehl_gpio_probe,
	.id_table	= ehl_gpio_ids,
};
module_platform_driver(ehl_gpio_driver);

MODULE_AUTHOR("Pandith N <pandith.n@intel.com>");
MODULE_AUTHOR("Raag Jadav <raag.jadav@intel.com>");
MODULE_DESCRIPTION("Intel Elkhart Lake PSE GPIO driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(GPIO_TANGIER);
