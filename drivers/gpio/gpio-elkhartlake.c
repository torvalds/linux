// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Elkhart Lake PSE GPIO driver
 *
 * Copyright (c) 2023, 2025 Intel Corporation.
 *
 * Authors: Pandith N <pandith.n@intel.com>
 *          Raag Jadav <raag.jadav@intel.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/pm.h>

#include <linux/ehl_pse_io_aux.h>

#include "gpio-tangier.h"

/* Each Intel EHL PSE GPIO Controller has 30 GPIO pins */
#define EHL_PSE_NGPIO		30

static int ehl_gpio_probe(struct auxiliary_device *adev, const struct auxiliary_device_id *id)
{
	struct device *dev = &adev->dev;
	struct ehl_pse_io_data *data;
	struct tng_gpio *priv;
	int ret;

	data = dev_get_platdata(dev);
	if (!data)
		return -ENODATA;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->reg_base = devm_ioremap_resource(dev, &data->mem);
	if (IS_ERR(priv->reg_base))
		return PTR_ERR(priv->reg_base);

	priv->dev = dev;
	priv->irq = data->irq;

	priv->info.base = -1;
	priv->info.ngpio = EHL_PSE_NGPIO;

	priv->wake_regs.gwmr = GWMR_EHL;
	priv->wake_regs.gwsr = GWSR_EHL;
	priv->wake_regs.gsir = GSIR_EHL;

	ret = devm_tng_gpio_probe(dev, priv);
	if (ret)
		return dev_err_probe(dev, ret, "tng_gpio_probe error\n");

	auxiliary_set_drvdata(adev, priv);
	return 0;
}

static const struct auxiliary_device_id ehl_gpio_ids[] = {
	{ EHL_PSE_IO_NAME "." EHL_PSE_GPIO_NAME },
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, ehl_gpio_ids);

static struct auxiliary_driver ehl_gpio_driver = {
	.driver	= {
		.pm	= pm_sleep_ptr(&tng_gpio_pm_ops),
	},
	.probe		= ehl_gpio_probe,
	.id_table	= ehl_gpio_ids,
};
module_auxiliary_driver(ehl_gpio_driver);

MODULE_AUTHOR("Pandith N <pandith.n@intel.com>");
MODULE_AUTHOR("Raag Jadav <raag.jadav@intel.com>");
MODULE_DESCRIPTION("Intel Elkhart Lake PSE GPIO driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("GPIO_TANGIER");
