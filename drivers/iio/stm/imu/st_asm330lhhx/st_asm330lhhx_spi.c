/*
 * STMicroelectronics st_asm330lhhx spi driver
 *
 * Copyright 2019 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 * Mario Tesi <mario.tesi@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_asm330lhhx.h"

#define SENSORS_SPI_READ	BIT(7)

static int st_asm330lhhx_spi_read(struct device *dev, u8 addr, int len,
				  u8 *data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct st_asm330lhhx_hw *hw = spi_get_drvdata(spi);
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

static int st_asm330lhhx_spi_write(struct device *dev, u8 addr, int len,
				   const u8 *data)
{
	struct st_asm330lhhx_hw *hw;
	struct spi_device *spi;

	if (len >= ST_ASM330LHHX_TX_MAX_LENGTH)
		return -ENOMEM;

	spi = to_spi_device(dev);
	hw = spi_get_drvdata(spi);

	hw->tb.tx_buf[0] = addr;
	memcpy(&hw->tb.tx_buf[1], data, len);

	return spi_write(spi, hw->tb.tx_buf, len + 1);
}

static const struct st_asm330lhhx_transfer_function st_asm330lhhx_transfer_fn = {
	.read = st_asm330lhhx_spi_read,
	.write = st_asm330lhhx_spi_write,
};

static int st_asm330lhhx_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	int hw_id = id->driver_data;

	return st_asm330lhhx_probe(&spi->dev, spi->irq, hw_id,
				   &st_asm330lhhx_transfer_fn);
}

static const struct of_device_id st_asm330lhhx_spi_of_match[] = {
	{
		.compatible = "st,asm330lhhx",
		.data = (void *)ST_ASM330LHHX_ID,
	},
	{
		.compatible = "st,asm330lhh",
		.data = (void *)ST_ASM330LHH_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_asm330lhhx_spi_of_match);

static const struct spi_device_id st_asm330lhhx_spi_id_table[] = {
	{ ST_ASM330LHHX_DEV_NAME, ST_ASM330LHHX_ID },
	{ ST_ASM330LHH_DEV_NAME , ST_ASM330LHH_ID },
	{},
};
MODULE_DEVICE_TABLE(spi, st_asm330lhhx_spi_id_table);

static struct spi_driver st_asm330lhhx_driver = {
	.driver = {
		.name = "st_asm330lhhx_spi",
		.pm = &st_asm330lhhx_pm_ops,
		.of_match_table = st_asm330lhhx_spi_of_match,
	},
	.probe = st_asm330lhhx_spi_probe,
	.id_table = st_asm330lhhx_spi_id_table,
};
module_spi_driver(st_asm330lhhx_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_AUTHOR("Mario Tesi <mario.tesi@st.com>");
MODULE_DESCRIPTION("STMicroelectronics st_asm330lhhx spi driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(ST_ASM330LHHX_DRV_VERSION);
