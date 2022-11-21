// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lis2dw12 spi driver
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

#include "st_lis2dw12.h"

#define SENSORS_SPI_READ	BIT(7)

static int st_lis2dw12_spi_read(struct device *dev, u8 addr, int len, u8 *data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct st_lis2dw12_hw *hw = spi_get_drvdata(spi);
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

static int st_lis2dw12_spi_write(struct device *dev, u8 addr, int len,
				 u8 *data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct st_lis2dw12_hw *hw = spi_get_drvdata(spi);

	if (len >= ST_LIS2DW12_TX_MAX_LENGTH)
		return -ENOMEM;

	hw->tb.tx_buf[0] = addr;
	memcpy(&hw->tb.tx_buf[1], data, len);

	return spi_write(spi, hw->tb.tx_buf, len + 1);
}

static const struct st_lis2dw12_transfer_function st_lis2dw12_transfer_fn = {
	.read = st_lis2dw12_spi_read,
	.write = st_lis2dw12_spi_write,
};

static int st_lis2dw12_spi_probe(struct spi_device *spi)
{
	return st_lis2dw12_probe(&spi->dev, spi->irq,
				 &st_lis2dw12_transfer_fn);
}

static const struct of_device_id st_lis2dw12_spi_of_match[] = {
	{
		.compatible = "st,lis2dw12",
		.data = ST_LIS2DW12_DEV_NAME,
	},
	{
		.compatible = "st,iis2dlpc",
		.data = ST_IIS2DLPC_DEV_NAME,
	},
	{
		.compatible = "st,ais2ih",
		.data = ST_AIS2IH_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lis2dw12_spi_of_match);

static const struct spi_device_id st_lis2dw12_spi_id_table[] = {
	{ ST_LIS2DW12_DEV_NAME },
	{ ST_AIS2IH_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, st_lis2dw12_spi_id_table);

static struct spi_driver st_lis2dw12_driver = {
	.driver = {
		.name = "st_lis2dw12_spi",
		.of_match_table = of_match_ptr(st_lis2dw12_spi_of_match),
	},
	.probe = st_lis2dw12_spi_probe,
	.id_table = st_lis2dw12_spi_id_table,
};
module_spi_driver(st_lis2dw12_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lis2dw12 spi driver");
MODULE_LICENSE("GPL v2");
