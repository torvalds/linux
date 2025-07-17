// SPDX-License-Identifier: GPL-2.0-only
/*
 * Murata ZPA2326 SPI pressure and temperature sensor driver
 *
 * Copyright (c) 2016 Parrot S.A.
 *
 * Author: Gregor Boirie <gregor.boirie@parrot.com>
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/mod_devicetable.h>
#include "zpa2326.h"

/*
 * read_flag_mask:
 *   - address bit 7 must be set to request a register read operation
 *   - address bit 6 must be set to request register address auto increment
 */
static const struct regmap_config zpa2326_regmap_spi_config = {
	.reg_bits       = 8,
	.val_bits       = 8,
	.writeable_reg  = zpa2326_isreg_writeable,
	.readable_reg   = zpa2326_isreg_readable,
	.precious_reg   = zpa2326_isreg_precious,
	.max_register   = ZPA2326_TEMP_OUT_H_REG,
	.read_flag_mask = BIT(7) | BIT(6),
};

static int zpa2326_probe_spi(struct spi_device *spi)
{
	struct regmap *regmap;
	int            err;

	regmap = devm_regmap_init_spi(spi, &zpa2326_regmap_spi_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "failed to init registers map");
		return PTR_ERR(regmap);
	}

	/*
	 * Enforce SPI slave settings to prevent from DT misconfiguration.
	 *
	 * Clock is idle high. Sampling happens on trailing edge, i.e., rising
	 * edge. Maximum bus frequency is 1 MHz. Registers are 8 bits wide.
	 */
	spi->mode = SPI_MODE_3;
	spi->max_speed_hz = min(spi->max_speed_hz, 1000000U);
	err = spi_setup(spi);
	if (err < 0)
		return err;

	return zpa2326_probe(&spi->dev, spi_get_device_id(spi)->name,
			     spi->irq, ZPA2326_DEVICE_ID, regmap);
}

static void zpa2326_remove_spi(struct spi_device *spi)
{
	zpa2326_remove(&spi->dev);
}

static const struct spi_device_id zpa2326_spi_ids[] = {
	{ "zpa2326", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, zpa2326_spi_ids);

static const struct of_device_id zpa2326_spi_matches[] = {
	{ .compatible = "murata,zpa2326" },
	{ }
};
MODULE_DEVICE_TABLE(of, zpa2326_spi_matches);

static struct spi_driver zpa2326_spi_driver = {
	.driver = {
		.name           = "zpa2326-spi",
		.of_match_table = zpa2326_spi_matches,
		.pm             = ZPA2326_PM_OPS,
	},
	.probe    = zpa2326_probe_spi,
	.remove   = zpa2326_remove_spi,
	.id_table = zpa2326_spi_ids,
};
module_spi_driver(zpa2326_spi_driver);

MODULE_AUTHOR("Gregor Boirie <gregor.boirie@parrot.com>");
MODULE_DESCRIPTION("SPI driver for Murata ZPA2326 pressure sensor");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_ZPA2326");
