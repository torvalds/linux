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
		.driver_data = (kernel_ulong_t)&mc13xxx_variant_mc13783,
	}, {
		.name = "mc13892",
		.driver_data = (kernel_ulong_t)&mc13xxx_variant_mc13892,
	}, {
		.name = "mc34708",
		.driver_data = (kernel_ulong_t)&mc13xxx_variant_mc34708,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(spi, mc13xxx_device_id);

static const struct of_device_id mc13xxx_dt_ids[] = {
	{ .compatible = "fsl,mc13783", .data = &mc13xxx_variant_mc13783, },
	{ .compatible = "fsl,mc13892", .data = &mc13xxx_variant_mc13892, },
	{ .compatible = "fsl,mc34708", .data = &mc13xxx_variant_mc34708, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mc13xxx_dt_ids);

static struct regmap_config mc13xxx_regmap_spi_config = {
	.reg_bits = 7,
	.pad_bits = 1,
	.val_bits = 24,
	.write_flag_mask = 0x80,

	.max_register = MC13XXX_NUMREGS,

	.cache_type = REGCACHE_NONE,
	.use_single_rw = 1,
};

static int mc13xxx_spi_read(void *context, const void *reg, size_t reg_size,
				void *val, size_t val_size)
{
	unsigned char w[4] = { *((unsigned char *) reg), 0, 0, 0};
	unsigned char r[4];
	unsigned char *p = val;
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	struct spi_transfer t = {
		.tx_buf = w,
		.rx_buf = r,
		.len = 4,
	};

	struct spi_message m;
	int ret;

	if (val_size != 3 || reg_size != 1)
		return -ENOTSUPP;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(spi, &m);

	memcpy(p, &r[1], 3);

	return ret;
}

static int mc13xxx_spi_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	const char *reg = data;

	if (count != 4)
		return -ENOTSUPP;

	/* include errata fix for spi audio problems */
	if (*reg == MC13783_AUDIO_CODEC || *reg == MC13783_AUDIO_DAC)
		spi_write(spi, data, count);

	return spi_write(spi, data, count);
}

/*
 * We cannot use regmap-spi generic bus implementation here.
 * The MC13783 chip will get corrupted if CS signal is deasserted
 * and on i.Mx31 SoC (the target SoC for MC13783 PMIC) the SPI controller
 * has the following errata (DSPhl22960):
 * "The CSPI negates SS when the FIFO becomes empty with
 * SSCTL= 0. Software cannot guarantee that the FIFO will not
 * drain because of higher priority interrupts and the
 * non-realtime characteristics of the operating system. As a
 * result, the SS will negate before all of the data has been
 * transferred to/from the peripheral."
 * We workaround this by accessing the SPI controller with a
 * single transfert.
 */

static struct regmap_bus regmap_mc13xxx_bus = {
	.write = mc13xxx_spi_write,
	.read = mc13xxx_spi_read,
};

static int mc13xxx_spi_probe(struct spi_device *spi)
{
	struct mc13xxx *mc13xxx;
	int ret;

	mc13xxx = devm_kzalloc(&spi->dev, sizeof(*mc13xxx), GFP_KERNEL);
	if (!mc13xxx)
		return -ENOMEM;

	dev_set_drvdata(&spi->dev, mc13xxx);

	spi->mode = SPI_MODE_0 | SPI_CS_HIGH;

	mc13xxx->irq = spi->irq;

	mc13xxx->regmap = devm_regmap_init(&spi->dev, &regmap_mc13xxx_bus,
					   &spi->dev,
					   &mc13xxx_regmap_spi_config);
	if (IS_ERR(mc13xxx->regmap)) {
		ret = PTR_ERR(mc13xxx->regmap);
		dev_err(&spi->dev, "Failed to initialize regmap: %d\n", ret);
		return ret;
	}

	if (spi->dev.of_node) {
		const struct of_device_id *of_id =
			of_match_device(mc13xxx_dt_ids, &spi->dev);

		mc13xxx->variant = of_id->data;
	} else {
		const struct spi_device_id *id_entry = spi_get_device_id(spi);

		mc13xxx->variant = (void *)id_entry->driver_data;
	}

	return mc13xxx_common_init(&spi->dev);
}

static int mc13xxx_spi_remove(struct spi_device *spi)
{
	return mc13xxx_common_exit(&spi->dev);
}

static struct spi_driver mc13xxx_spi_driver = {
	.id_table = mc13xxx_device_id,
	.driver = {
		.name = "mc13xxx",
		.owner = THIS_MODULE,
		.of_match_table = mc13xxx_dt_ids,
	},
	.probe = mc13xxx_spi_probe,
	.remove = mc13xxx_spi_remove,
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
