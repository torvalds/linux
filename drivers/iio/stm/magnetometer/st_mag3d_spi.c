// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics mag3d spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>

#include "st_mag3d.h"

#define ST_SENSORS_SPI_READ	BIT(7)

static int st_mag3d_spi_read(struct device *dev, u8 addr, int len, u8 *data)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_mag3d_hw *hw = iio_priv(iio_dev);
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

	hw->tb.tx_buf[0] = addr | ST_SENSORS_SPI_READ;

	err = spi_sync_transfer(to_spi_device(hw->dev), xfers,
				ARRAY_SIZE(xfers));
	if (err)
		return err;

	memcpy(data, hw->tb.rx_buf, len*sizeof(u8));

	return len;
}

static int st_mag3d_spi_write(struct device *dev, u8 addr, int len, u8 *data)
{

	struct st_mag3d_hw *hw;
	struct iio_dev *iio_dev;

	if (len >= ST_MAG3D_TX_MAX_LENGTH)
		return -ENOMEM;

	iio_dev = dev_get_drvdata(dev);
	hw = iio_priv(iio_dev);

	hw->tb.tx_buf[0] = addr;
	memcpy(&hw->tb.tx_buf[1], data, len);

	return spi_write(to_spi_device(hw->dev), hw->tb.tx_buf, len + 1);
}

static const struct st_mag3d_transfer_function st_mag3d_tf_spi = {
	.write = st_mag3d_spi_write,
	.read = st_mag3d_spi_read,
};

static int st_mag3d_spi_probe(struct spi_device *spi)
{
	return st_mag3d_probe(&spi->dev, spi->irq, spi->modalias,
			      &st_mag3d_tf_spi);
}

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
static void st_mag3d_spi_remove(struct spi_device *spi)
{
	struct iio_dev *iio_dev = spi_get_drvdata(spi);

	st_mag3d_remove(iio_dev);
}
#else /* LINUX_VERSION_CODE */
static int st_mag3d_spi_remove(struct spi_device *spi)
{
	struct iio_dev *iio_dev = spi_get_drvdata(spi);

	st_mag3d_remove(iio_dev);

	return 0;
}
#endif /* LINUX_VERSION_CODE */

static const struct spi_device_id st_mag3d_ids[] = {
	{ LIS3MDL_DEV_NAME },
	{ LSM9DS1_DEV_NAME },
	{}
};
MODULE_DEVICE_TABLE(spi, st_mag3d_ids);

#ifdef CONFIG_OF
static const struct of_device_id st_mag3d_id_table[] = {
	{
		.compatible = "st,lis3mdl_magn",
	},
	{
		.compatible = "st,lsm9ds1_magn",
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_mag3d_id_table);
#endif /* CONFIG_OF */

static struct spi_driver st_mag3d_spi_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "st_mag3d_spi",
#ifdef CONFIG_OF
		   .of_match_table = st_mag3d_id_table,
#endif /* CONFIG_OF */
		   },
	.probe = st_mag3d_spi_probe,
	.remove = st_mag3d_spi_remove,
	.id_table = st_mag3d_ids,
};
module_spi_driver(st_mag3d_spi_driver);

MODULE_DESCRIPTION("STMicroelectronics mag3d spi driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
