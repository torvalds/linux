/*
 * Copyright 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * loosely based on an earlier driver that has
 * Copyright 2009 Pengutronix, Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mc13xxx.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/spi/spi.h>

#include "mc13xxx.h"

static const struct spi_device_id mc13xxx_device_id[] = {
	{
		.name = "mc13783",
		.driver_data = MC13XXX_ID_MC13783,
	}, {
		.name = "mc13892",
		.driver_data = MC13XXX_ID_MC13892,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(spi, mc13xxx_device_id);

static const struct of_device_id mc13xxx_dt_ids[] = {
	{ .compatible = "fsl,mc13783", .data = (void *) MC13XXX_ID_MC13783, },
	{ .compatible = "fsl,mc13892", .data = (void *) MC13XXX_ID_MC13892, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mc13xxx_dt_ids);

static struct regmap_config mc13xxx_regmap_spi_config = {
	.reg_bits = 7,
	.pad_bits = 1,
	.val_bits = 24,

	.max_register = MC13XXX_NUMREGS,

	.cache_type = REGCACHE_NONE,
};

static int mc13xxx_spi_probe(struct spi_device *spi)
{
	const struct of_device_id *of_id;
	struct spi_driver *sdrv = to_spi_driver(spi->dev.driver);
	struct mc13xxx *mc13xxx;
	struct mc13xxx_platform_data *pdata = dev_get_platdata(&spi->dev);
	int ret;

	of_id = of_match_device(mc13xxx_dt_ids, &spi->dev);
	if (of_id)
		sdrv->id_table = &mc13xxx_device_id[(enum mc13xxx_id) of_id->data];

	mc13xxx = kzalloc(sizeof(*mc13xxx), GFP_KERNEL);
	if (!mc13xxx)
		return -ENOMEM;

	dev_set_drvdata(&spi->dev, mc13xxx);
	spi->mode = SPI_MODE_0 | SPI_CS_HIGH;
	spi->bits_per_word = 32;

	mc13xxx->dev = &spi->dev;
	mutex_init(&mc13xxx->lock);

	mc13xxx->regmap = regmap_init_spi(spi, &mc13xxx_regmap_spi_config);
	if (IS_ERR(mc13xxx->regmap)) {
		ret = PTR_ERR(mc13xxx->regmap);
		dev_err(mc13xxx->dev, "Failed to initialize register map: %d\n",
				ret);
		dev_set_drvdata(&spi->dev, NULL);
		kfree(mc13xxx);
		return ret;
	}

	ret = mc13xxx_common_init(mc13xxx, pdata, spi->irq);

	if (ret) {
		dev_set_drvdata(&spi->dev, NULL);
	} else {
		const struct spi_device_id *devid =
			spi_get_device_id(spi);
		if (!devid || devid->driver_data != mc13xxx->ictype)
			dev_warn(mc13xxx->dev,
				"device id doesn't match auto detection!\n");
	}

	return ret;
}

static int __devexit mc13xxx_spi_remove(struct spi_device *spi)
{
	struct mc13xxx *mc13xxx = dev_get_drvdata(&spi->dev);

	mc13xxx_common_cleanup(mc13xxx);

	return 0;
}

static struct spi_driver mc13xxx_spi_driver = {
	.id_table = mc13xxx_device_id,
	.driver = {
		.name = "mc13xxx",
		.owner = THIS_MODULE,
		.of_match_table = mc13xxx_dt_ids,
	},
	.probe = mc13xxx_spi_probe,
	.remove = __devexit_p(mc13xxx_spi_remove),
};

static int __init mc13xxx_init(void)
{
	return spi_register_driver(&mc13xxx_spi_driver);
}
subsys_initcall(mc13xxx_init);

static void __exit mc13xxx_exit(void)
{
	spi_unregister_driver(&mc13xxx_spi_driver);
}
module_exit(mc13xxx_exit);

MODULE_DESCRIPTION("Core driver for Freescale MC13XXX PMIC");
MODULE_AUTHOR("Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>");
MODULE_LICENSE("GPL v2");
