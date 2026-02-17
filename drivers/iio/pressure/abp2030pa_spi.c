// SPDX-License-Identifier: GPL-2.0-only
/*
 * Honeywell ABP2 series pressure sensor driver
 *
 * Copyright (c) 2025 Petre Rodan <petre.rodan@subdimension.ro>
 */

#include <linux/errno.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "abp2030pa.h"

static int abp2_spi_xfer(struct abp2_data *data, u8 cmd, u8 nbytes)
{
	struct spi_device *spi = to_spi_device(data->dev);
	struct spi_transfer xfer = { };

	if (nbytes > ABP2_MEASUREMENT_RD_SIZE)
		return -EOVERFLOW;

	data->tx_buf[0] = cmd;
	xfer.tx_buf = data->tx_buf;
	xfer.rx_buf = data->rx_buf;
	xfer.len = nbytes;

	return spi_sync_transfer(spi, &xfer, 1);
}

static const struct abp2_ops abp2_spi_ops = {
	.read = abp2_spi_xfer,
	.write = abp2_spi_xfer,
};

static int abp2_spi_probe(struct spi_device *spi)
{
	return abp2_common_probe(&spi->dev, &abp2_spi_ops, spi->irq);
}

static const struct of_device_id abp2_spi_match[] = {
	{ .compatible = "honeywell,abp2030pa" },
	{ }
};
MODULE_DEVICE_TABLE(of, abp2_spi_match);

static const struct spi_device_id abp2_spi_id[] = {
	{ "abp2030pa" },
	{ }
};
MODULE_DEVICE_TABLE(spi, abp2_spi_id);

static struct spi_driver abp2_spi_driver = {
	.driver = {
		.name = "abp2030pa",
		.of_match_table = abp2_spi_match,
	},
	.probe = abp2_spi_probe,
	.id_table = abp2_spi_id,
};
module_spi_driver(abp2_spi_driver);

MODULE_AUTHOR("Petre Rodan <petre.rodan@subdimension.ro>");
MODULE_DESCRIPTION("Honeywell ABP2 pressure sensor spi driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_HONEYWELL_ABP2030PA");
