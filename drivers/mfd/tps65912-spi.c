// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI access driver for TI TPS65912x PMICs
 *
 * Copyright (C) 2015 Texas Instruments Incorporated - https://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *
 * Based on the TPS65218 driver and the previous TPS65912 driver by
 * Margarita Olaya Cabrera <magi@slimlogic.co.uk>
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include <linux/mfd/tps65912.h>

static const struct of_device_id tps65912_spi_of_match_table[] = {
	{ .compatible = "ti,tps65912", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tps65912_spi_of_match_table);

static int tps65912_spi_probe(struct spi_device *spi)
{
	struct tps65912 *tps;

	tps = devm_kzalloc(&spi->dev, sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	spi_set_drvdata(spi, tps);
	tps->dev = &spi->dev;
	tps->irq = spi->irq;

	tps->regmap = devm_regmap_init_spi(spi, &tps65912_regmap_config);
	if (IS_ERR(tps->regmap)) {
		dev_err(tps->dev, "Failed to initialize register map\n");
		return PTR_ERR(tps->regmap);
	}

	return tps65912_device_init(tps);
}

static void tps65912_spi_remove(struct spi_device *spi)
{
	struct tps65912 *tps = spi_get_drvdata(spi);

	tps65912_device_exit(tps);
}

static const struct spi_device_id tps65912_spi_id_table[] = {
	{ "tps65912", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, tps65912_spi_id_table);

static struct spi_driver tps65912_spi_driver = {
	.driver		= {
		.name	= "tps65912",
		.of_match_table = tps65912_spi_of_match_table,
	},
	.probe		= tps65912_spi_probe,
	.remove		= tps65912_spi_remove,
	.id_table       = tps65912_spi_id_table,
};
module_spi_driver(tps65912_spi_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("TPS65912x SPI Interface Driver");
MODULE_LICENSE("GPL v2");
