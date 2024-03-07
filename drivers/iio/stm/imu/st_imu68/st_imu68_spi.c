// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_imu68 spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_imu68.h"

#define SENSORS_SPI_READ	BIT(7)

static int st_imu68_spi_read(struct device *dev, u8 addr, int len, u8 *data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct st_imu68_hw *hw = spi_get_drvdata(spi);
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

	hw->tb.tx_buf[0] = addr | SENSORS_SPI_READ;

	err = spi_sync_transfer(spi, xfers,  ARRAY_SIZE(xfers));
	if (err < 0)
		return err;

	memcpy(data, hw->tb.rx_buf, len * sizeof(u8));

	return len;
}

static int st_imu68_spi_write(struct device *dev, u8 addr, int len, u8 *data)
{
	struct st_imu68_hw *hw;
	struct spi_device *spi;

	if (len >= ST_IMU68_TX_MAX_LENGTH)
		return -ENOMEM;

	spi = to_spi_device(dev);
	hw = spi_get_drvdata(spi);

	hw->tb.tx_buf[0] = addr;
	memcpy(&hw->tb.tx_buf[1], data, len);

	return spi_write(spi, hw->tb.tx_buf, len + 1);
}

static const struct st_imu68_transfer_function st_imu68_transfer_fn = {
	.read = st_imu68_spi_read,
	.write = st_imu68_spi_write,
};

static int st_imu68_spi_probe(struct spi_device *spi)
{
	return st_imu68_probe(&spi->dev, spi->irq, spi->modalias,
			      &st_imu68_transfer_fn);
}

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
static void st_imu68_spi_remove(struct spi_device *spi)
{
	st_imu68_remove(&spi->dev);
}
#else /* LINUX_VERSION_CODE */
static int st_imu68_spi_remove(struct spi_device *spi)
{
	st_imu68_remove(&spi->dev);

	return 0;
}
#endif /* LINUX_VERSION_CODE */

static const struct of_device_id st_imu68_spi_of_match[] = {
	{
		.compatible = "st,lsm9ds1",
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_imu68_spi_of_match);

static const struct spi_device_id st_imu68_spi_id_table[] = {
	{ ST_LSM9DS1_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, st_imu68_spi_id_table);

static struct spi_driver st_imu68_driver = {
	.driver = {
		.name = "st_imu68_spi",
		.of_match_table = of_match_ptr(st_imu68_spi_of_match),
	},
	.probe = st_imu68_spi_probe,
	.remove = st_imu68_spi_remove,
	.id_table = st_imu68_spi_id_table,
};
module_spi_driver(st_imu68_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_imu68 spi driver");
MODULE_LICENSE("GPL v2");
