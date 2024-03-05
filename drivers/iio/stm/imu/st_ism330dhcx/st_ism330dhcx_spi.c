// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_ism330dhcx spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2020 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_ism330dhcx.h"

#define SENSORS_SPI_READ	BIT(7)

static int st_ism330dhcx_spi_read(struct device *dev, u8 addr, int len,
			       u8 *data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct st_ism330dhcx_hw *hw = spi_get_drvdata(spi);
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

static int st_ism330dhcx_spi_write(struct device *dev, u8 addr, int len,
				const u8 *data)
{
	struct st_ism330dhcx_hw *hw;
	struct spi_device *spi;

	if (len >= ST_ISM330DHCX_TX_MAX_LENGTH)
		return -ENOMEM;

	spi = to_spi_device(dev);
	hw = spi_get_drvdata(spi);

	hw->tb.tx_buf[0] = addr;
	memcpy(&hw->tb.tx_buf[1], data, len);

	return spi_write(spi, hw->tb.tx_buf, len + 1);
}

static const struct st_ism330dhcx_transfer_function st_ism330dhcx_transfer_fn = {
	.read = st_ism330dhcx_spi_read,
	.write = st_ism330dhcx_spi_write,
};

static int st_ism330dhcx_spi_probe(struct spi_device *spi)
{
	return st_ism330dhcx_probe(&spi->dev, spi->irq,
				&st_ism330dhcx_transfer_fn);
}

static const struct of_device_id st_ism330dhcx_spi_of_match[] = {
	{
		.compatible = "st,ism330dhcx",
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_ism330dhcx_spi_of_match);

static const struct spi_device_id st_ism330dhcx_spi_id_table[] = {
	{ ST_ISM330DHCX_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, st_ism330dhcx_spi_id_table);

static struct spi_driver st_ism330dhcx_driver = {
	.driver = {
		.name = "st_ism330dhcx_spi",
		.pm = &st_ism330dhcx_pm_ops,
		.of_match_table = of_match_ptr(st_ism330dhcx_spi_of_match),
	},
	.probe = st_ism330dhcx_spi_probe,
	.id_table = st_ism330dhcx_spi_id_table,
};
module_spi_driver(st_ism330dhcx_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_ism330dhcx spi driver");
MODULE_LICENSE("GPL v2");
