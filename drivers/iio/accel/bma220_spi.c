// SPDX-License-Identifier: GPL-2.0-only
/*
 * BMA220 Digital triaxial acceleration sensor driver
 *
 * Copyright (c) 2016,2020 Intel Corporation.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/spi/spi.h>

#include "bma220.h"

static int bma220_spi_probe(struct spi_device *spi)
{
	return bma220_common_probe(spi);
}

static const struct spi_device_id bma220_spi_id[] = {
	{"bma220", 0},
	{ }
};

static const struct acpi_device_id bma220_acpi_id[] = {
	{"BMA0220", 0},
	{ }
};
MODULE_DEVICE_TABLE(spi, bma220_spi_id);

static struct spi_driver bma220_spi_driver = {
	.driver = {
		.name = "bma220_spi",
		.pm = pm_sleep_ptr(&bma220_pm_ops),
		.acpi_match_table = bma220_acpi_id,
	},
	.probe =            bma220_spi_probe,
	.id_table =         bma220_spi_id,
};
module_spi_driver(bma220_spi_driver);

MODULE_AUTHOR("Tiberiu Breana <tiberiu.a.breana@intel.com>");
MODULE_DESCRIPTION("BMA220 triaxial acceleration sensor spi driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_BOSCH_BMA220");
