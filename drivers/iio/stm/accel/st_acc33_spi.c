// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_acc33 spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>

#include "st_acc33.h"

#define SENSORS_SPI_READ	BIT(7)
#define SPI_AUTO_INCREMENT	BIT(6)

static int st_acc33_spi_read(struct device *device, u8 addr, int len, u8 *data)
{
	struct spi_device *spi = to_spi_device(device);
	struct iio_dev *iio_dev = spi_get_drvdata(spi);
	struct st_acc33_hw *hw = iio_priv(iio_dev);
	int err;

	struct spi_transfer xfers[] = {
		{
			.tx_buf = hw->tb.tx_buf,
			.bits_per_word = 8,
			.len = 1,
		},
		{
			.rx_buf = hw->tb.rx_buf,
			.bits_per_word = 8,
			.len = len,
		}
	};

	if (len > 1)
		addr |= SPI_AUTO_INCREMENT;

	hw->tb.tx_buf[0] = addr | SENSORS_SPI_READ;

	err = spi_sync_transfer(spi, xfers,  ARRAY_SIZE(xfers));
	if (err < 0)
		return err;

	memcpy(data, hw->tb.rx_buf, len * sizeof(u8));

	return len;
}

static int st_acc33_spi_write(struct device *device, u8 addr, int len, u8 *data)
{
	struct spi_device *spi = to_spi_device(device);
	struct iio_dev *iio_dev = spi_get_drvdata(spi);
	struct st_acc33_hw *hw = iio_priv(iio_dev);

	if (len >= ST_ACC33_TX_MAX_LENGTH)
		return -ENOMEM;

	if (len > 1)
		addr |= SPI_AUTO_INCREMENT;
	hw->tb.tx_buf[0] = addr;
	memcpy(&hw->tb.tx_buf[1], data, len);

	return spi_write(spi, hw->tb.tx_buf, len + 1);
}

static const struct st_acc33_transfer_function st_acc33_transfer_fn = {
	.read = st_acc33_spi_read,
	.write = st_acc33_spi_write,
};

static int st_acc33_spi_probe(struct spi_device *spi)
{
	return st_acc33_probe(&spi->dev, spi->irq, spi->modalias,
			      &st_acc33_transfer_fn);
}

static const struct of_device_id st_acc33_spi_of_match[] = {
	{
		.compatible = "st,lis2dh_accel",
		.data = LIS2DH_DEV_NAME,
	},
	{
		.compatible = "st,lis2dh12_accel",
		.data = LIS2DH12_DEV_NAME,
	},
	{
		.compatible = "st,lis3dh_accel",
		.data = LIS3DH_DEV_NAME,
	},
	{
		.compatible = "st,lsm303agr_accel",
		.data = LSM303AGR_DEV_NAME,
	},
	{
		.compatible = "st,iis2dh_accel",
		.data = IIS2DH_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_acc33_spi_of_match);

static const struct spi_device_id st_acc33_spi_id_table[] = {
	{ LIS2DH_DEV_NAME },
	{ LIS2DH12_DEV_NAME },
	{ LIS3DH_DEV_NAME },
	{ LSM303AGR_DEV_NAME },
	{ IIS2DH_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, st_acc33_spi_id_table);

static struct spi_driver st_acc33_driver = {
	.driver = {
		.name = "st_acc33_spi",
		.of_match_table = of_match_ptr(st_acc33_spi_of_match),
	},
	.probe = st_acc33_spi_probe,
	.id_table = st_acc33_spi_id_table,
};
module_spi_driver(st_acc33_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_acc33 spi driver");
MODULE_LICENSE("GPL v2");
