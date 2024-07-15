// SPDX-License-Identifier: GPL-2.0-only
/*
 * Honeywell TruStability HSC Series pressure/temperature sensor
 *
 * Copyright (c) 2023 Petre Rodan <petre.rodan@subdimension.ro>
 *
 * Datasheet: https://prod-edam.honeywell.com/content/dam/honeywell-edam/sps/siot/en-us/products/sensors/pressure-sensors/common/documents/sps-siot-spi-comms-digital-ouptu-pressure-sensors-tn-008202-3-en-ciid-45843.pdf
 * Datasheet: https://prod-edam.honeywell.com/content/dam/honeywell-edam/sps/siot/en-us/products/sensors/pressure-sensors/common/documents/sps-siot-sleep-mode-technical-note-008286-1-en-ciid-155793.pdf
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/stddef.h>
#include <linux/types.h>

#include <linux/iio/iio.h>

#include "hsc030pa.h"

static int hsc_spi_recv(struct hsc_data *data)
{
	struct spi_device *spi = to_spi_device(data->dev);

	msleep_interruptible(HSC_RESP_TIME_MS);
	return spi_read(spi, data->buffer, HSC_REG_MEASUREMENT_RD_SIZE);
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
