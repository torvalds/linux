// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_spi_device.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * SPI access functions
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#include <linux/spi/spi.h>

#include "cxd2880_spi_device.h"

static int cxd2880_spi_device_write(struct cxd2880_spi *spi,
				    const u8 *data, u32 size)
{
	struct cxd2880_spi_device *spi_device = NULL;
	struct spi_message msg;
	struct spi_transfer tx;
	int result = 0;

	if (!spi || !spi->user || !data || size == 0)
		return -EINVAL;

	spi_device = spi->user;

	memset(&tx, 0, sizeof(tx));
	tx.tx_buf = data;
	tx.len = size;

	spi_message_init(&msg);
	spi_message_add_tail(&tx, &msg);
	result = spi_sync(spi_device->spi, &msg);

	if (result < 0)
		return -EIO;

	return 0;
}

static int cxd2880_spi_device_write_read(struct cxd2880_spi *spi,
					 const u8 *tx_data,
					 u32 tx_size,
					 u8 *rx_data,
					 u32 rx_size)
{
	struct cxd2880_spi_device *spi_device = NULL;
	int result = 0;

	if (!spi || !spi->user || !tx_data ||
	    !tx_size || !rx_data || !rx_size)
		return -EINVAL;

	spi_device = spi->user;

	result = spi_write_then_read(spi_device->spi, tx_data,
				     tx_size, rx_data, rx_size);
	if (result < 0)
		return -EIO;

	return 0;
}

int
cxd2880_spi_device_initialize(struct cxd2880_spi_device *spi_device,
			      enum cxd2880_spi_mode mode,
			      u32 speed_hz)
{
	int result = 0;
	struct spi_device *spi = spi_device->spi;

	switch (mode) {
	case CXD2880_SPI_MODE_0:
		spi->mode = SPI_MODE_0;
		break;
	case CXD2880_SPI_MODE_1:
		spi->mode = SPI_MODE_1;
		break;
	case CXD2880_SPI_MODE_2:
		spi->mode = SPI_MODE_2;
		break;
	case CXD2880_SPI_MODE_3:
		spi->mode = SPI_MODE_3;
		break;
	default:
		return -EINVAL;
	}

	spi->max_speed_hz = speed_hz;
	spi->bits_per_word = 8;
	result = spi_setup(spi);
	if (result != 0) {
		pr_err("spi_setup failed %d\n", result);
		return -EINVAL;
	}

	return 0;
}

int cxd2880_spi_device_create_spi(struct cxd2880_spi *spi,
				  struct cxd2880_spi_device *spi_device)
{
	if (!spi || !spi_device)
		return -EINVAL;

	spi->read = NULL;
	spi->write = cxd2880_spi_device_write;
	spi->write_read = cxd2880_spi_device_write_read;
	spi->flags = 0;
	spi->user = spi_device;

	return 0;
}
