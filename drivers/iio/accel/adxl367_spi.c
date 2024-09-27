// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Cosmin Tanislav <cosmin.tanislav@analog.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>

#include "adxl367.h"

#define ADXL367_SPI_WRITE_COMMAND	0x0A
#define ADXL367_SPI_READ_COMMAND	0x0B
#define ADXL367_SPI_FIFO_COMMAND	0x0D

struct adxl367_spi_state {
	struct spi_device	*spi;

	struct spi_message	reg_write_msg;
	struct spi_transfer	reg_write_xfer[2];

	struct spi_message	reg_read_msg;
	struct spi_transfer	reg_read_xfer[2];

	struct spi_message	fifo_msg;
	struct spi_transfer	fifo_xfer[2];

	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers live in their own cache lines.
	 */
	u8			reg_write_tx_buf[1] __aligned(IIO_DMA_MINALIGN);
	u8			reg_read_tx_buf[2];
	u8			fifo_tx_buf[1];
};

static int adxl367_read_fifo(void *context, __be16 *fifo_buf,
			     unsigned int fifo_entries)
{
	struct adxl367_spi_state *st = context;

	st->fifo_xfer[1].rx_buf = fifo_buf;
	st->fifo_xfer[1].len = fifo_entries * sizeof(*fifo_buf);

	return spi_sync(st->spi, &st->fifo_msg);
}

static int adxl367_read(void *context, const void *reg_buf, size_t reg_size,
			void *val_buf, size_t val_size)
{
	struct adxl367_spi_state *st = context;
	u8 reg = ((const u8 *)reg_buf)[0];

	st->reg_read_tx_buf[1] = reg;
	st->reg_read_xfer[1].rx_buf = val_buf;
	st->reg_read_xfer[1].len = val_size;

	return spi_sync(st->spi, &st->reg_read_msg);
}

static int adxl367_write(void *context, const void *val_buf, size_t val_size)
{
	struct adxl367_spi_state *st = context;

	st->reg_write_xfer[1].tx_buf = val_buf;
	st->reg_write_xfer[1].len = val_size;

	return spi_sync(st->spi, &st->reg_write_msg);
}

static const struct regmap_bus adxl367_spi_regmap_bus = {
	.read = adxl367_read,
	.write = adxl367_write,
};

static const struct regmap_config adxl367_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct adxl367_ops adxl367_spi_ops = {
	.read_fifo = adxl367_read_fifo,
};

static int adxl367_spi_probe(struct spi_device *spi)
{
	struct adxl367_spi_state *st;
	struct regmap *regmap;

	st = devm_kzalloc(&spi->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->spi = spi;

	/*
	 * Xfer:   [XFR1] [           XFR2           ]
	 * Master:  0x0A   ADDR DATA0 DATA1 ... DATAN
	 * Slave:   ....   ..........................
	 */
	st->reg_write_tx_buf[0] = ADXL367_SPI_WRITE_COMMAND;
	st->reg_write_xfer[0].tx_buf = st->reg_write_tx_buf;
	st->reg_write_xfer[0].len = sizeof(st->reg_write_tx_buf);
	spi_message_init_with_transfers(&st->reg_write_msg,
					st->reg_write_xfer, 2);

	/*
	 * Xfer:   [   XFR1  ] [         XFR2        ]
	 * Master:  0x0B ADDR   .....................
	 * Slave:   .........   DATA0 DATA1 ... DATAN
	 */
	st->reg_read_tx_buf[0] = ADXL367_SPI_READ_COMMAND;
	st->reg_read_xfer[0].tx_buf = st->reg_read_tx_buf;
	st->reg_read_xfer[0].len = sizeof(st->reg_read_tx_buf);
	spi_message_init_with_transfers(&st->reg_read_msg,
					st->reg_read_xfer, 2);

	/*
	 * Xfer:   [XFR1] [         XFR2        ]
	 * Master:  0x0D   .....................
	 * Slave:   ....   DATA0 DATA1 ... DATAN
	 */
	st->fifo_tx_buf[0] = ADXL367_SPI_FIFO_COMMAND;
	st->fifo_xfer[0].tx_buf = st->fifo_tx_buf;
	st->fifo_xfer[0].len = sizeof(st->fifo_tx_buf);
	spi_message_init_with_transfers(&st->fifo_msg, st->fifo_xfer, 2);

	regmap = devm_regmap_init(&spi->dev, &adxl367_spi_regmap_bus, st,
				  &adxl367_spi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return adxl367_probe(&spi->dev, &adxl367_spi_ops, st, regmap, spi->irq);
}

static const struct spi_device_id adxl367_spi_id[] = {
	{ "adxl367", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, adxl367_spi_id);

static const struct of_device_id adxl367_of_match[] = {
	{ .compatible = "adi,adxl367" },
	{ },
};
MODULE_DEVICE_TABLE(of, adxl367_of_match);

static struct spi_driver adxl367_spi_driver = {
	.driver = {
		.name = "adxl367_spi",
		.of_match_table = adxl367_of_match,
	},
	.probe = adxl367_spi_probe,
	.id_table = adxl367_spi_id,
};

module_spi_driver(adxl367_spi_driver);

MODULE_IMPORT_NS(IIO_ADXL367);
MODULE_AUTHOR("Cosmin Tanislav <cosmin.tanislav@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADXL367 3-axis accelerometer SPI driver");
MODULE_LICENSE("GPL");
