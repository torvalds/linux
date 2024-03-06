// SPDX-License-Identifier: GPL-2.0-only
/*
 * Honeywell TruStability HSC Series pressure/temperature sensor
 *
 * Copyright (c) 2023 Petre Rodan <petre.rodan@subdimension.ro>
 *
 * Datasheet: https://prod-edam.honeywell.com/content/dam/honeywell-edam/sps/siot/en-us/products/sensors/pressure-sensors/board-mount-pressure-sensors/trustability-hsc-series/documents/sps-siot-trustability-hsc-series-high-accuracy-board-mount-pressure-sensors-50099148-a-en-ciid-151133.pdf
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/stddef.h>

#include <linux/iio/iio.h>

#include "hsc030pa.h"

static int hsc_spi_recv(struct hsc_data *data)
{
	struct spi_device *spi = to_spi_device(data->dev);
	struct spi_transfer xfer = {
		.tx_buf = NULL,
		.rx_buf = data->buffer,
		.len = HSC_REG_MEASUREMENT_RD_SIZE,
	};

	return spi_sync_transfer(spi, &xfer, 1);
}

static int hsc_spi_probe(struct spi_device *spi)
{
	return hsc_common_probe(&spi->dev, hsc_spi_recv);
}

static const struct of_device_id hsc_spi_match[] = {
	{ .compatible = "honeywell,hsc030pa" },
	{}
};
MODULE_DEVICE_TABLE(of, hsc_spi_match);

static const struct spi_device_id hsc_spi_id[] = {
	{ "hsc030pa" },
	{}
};
MODULE_DEVICE_TABLE(spi, hsc_spi_id);

static struct spi_driver hsc_spi_driver = {
	.driver = {
		.name = "hsc030pa",
		.of_match_table = hsc_spi_match,
	},
	.probe = hsc_spi_probe,
	.id_table = hsc_spi_id,
};
module_spi_driver(hsc_spi_driver);

MODULE_AUTHOR("Petre Rodan <petre.rodan@subdimension.ro>");
MODULE_DESCRIPTION("Honeywell HSC and SSC pressure sensor spi driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_HONEYWELL_HSC030PA);
