// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lps22df spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2021 STMicroelectronics Inc.
 */

#include <linux/spi/spi.h>

#include "st_lps22df.h"

#define ST_SENSORS_SPI_READ			0x80

static int st_lps22df_spi_read(struct device *dev, u8 addr, int len, u8 *data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct st_lps22df_hw *hw = spi_get_drvdata(spi);
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
	int err;

	if (len >= ST_LPS22DF_RX_MAX_LENGTH)
		return -ENOMEM;

	hw->tb.tx_buf[0] = addr | ST_SENSORS_SPI_READ;
	err = spi_sync_transfer(spi, xfers, ARRAY_SIZE(xfers));
	if (err)
		return err;

	memcpy(data, hw->tb.rx_buf, len);

	return err;
}

static int st_lps22df_spi_write(struct device *dev, u8 addr, int len, u8 *data)
{
	struct st_lps22df_hw *hw;
	struct spi_device *spi;

	if (len >= ST_LPS22DF_TX_MAX_LENGTH)
		return -ENOMEM;

	spi = to_spi_device(dev);
	hw = spi_get_drvdata(spi);

	hw->tb.tx_buf[0] = addr;
	memcpy(&hw->tb.tx_buf[1], data, len);

	return spi_write(spi, hw->tb.tx_buf, len + 1);
}

static const struct st_lps22df_transfer_function st_lps22df_tf_spi = {
	.write = st_lps22df_spi_write,
	.read = st_lps22df_spi_read,
};

static int st_lps22df_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	int hw_id = id->driver_data;

	return st_lps22df_common_probe(&spi->dev, spi->irq,
				       hw_id, &st_lps22df_tf_spi);
}

static const struct spi_device_id st_lps22df_ids[] = {
	{ "lps22df", ST_LPS22DF_ID },
	{ "lps28dfw", ST_LPS28DFW_ID },
	{}
};
MODULE_DEVICE_TABLE(spi, st_lps22df_ids);

static const struct of_device_id st_lps22df_id_table[] = {
	{
		.compatible = "st,lps22df",
		.data = (void *)ST_LPS22DF_ID,
	},
	{
		.compatible = "st,lps28dfw",
		.data = (void *)ST_LPS28DFW_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lps22df_id_table);

static struct spi_driver st_lps22df_spi_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "st_lps22df_spi",
		   .of_match_table = of_match_ptr(st_lps22df_id_table),
	},
	.probe = st_lps22df_spi_probe,
	.id_table = st_lps22df_ids,
};
module_spi_driver(st_lps22df_spi_driver);

MODULE_DESCRIPTION("STMicroelectronics lps22df spi driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
