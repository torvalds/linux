// SPDX-License-Identifier: GPL-2.0
/*
 * AD5672R, AD5674R, AD5676, AD5676R, AD5679R,
 * AD5681R, AD5682R, AD5683, AD5683R, AD5684,
 * AD5684R, AD5685R, AD5686, AD5686R
 * Digital to analog converters driver
 *
 * Copyright 2018 Analog Devices Inc.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/spi/spi.h>

#include <asm/byteorder.h>

#include "ad5686.h"

/**
 * struct ad5686_spi_data - SPI bus specific data
 * @msg: SPI message used for transfers
 * @size: number of transfers currently in the message
 * @capacity: maximum number of transfers that can be added to the message
 * @xfers: array of SPI transfers, allocated with the provided capacity
 */
struct ad5686_spi_data {
	struct spi_message msg;
	unsigned int size;
	unsigned int capacity;
	struct spi_transfer xfers[] __counted_by(capacity);
};

static int ad5686_spi_write(struct ad5686_state *st,
			    u8 cmd, u8 addr, u16 val)
{
	struct ad5686_spi_data *bus_data = st->bus_data;
	struct spi_transfer *xfer;

	if (bus_data->size >= bus_data->capacity)
		return -E2BIG;

	/*
	 * This function stores spi transfers to a spi message to be sent over
	 * the bus when sync() op is called. If there are already transfers in
	 * the spi message, set the cs_change flag on the last transfer to
	 * ensure that the chip select is deasserted between transfers. If this
	 * is the first transfer, initialize the spi message. Later on, the
	 * current transfer is added to the message with spi_message_add_tail().
	 */
	if (bus_data->size)
		bus_data->xfers[bus_data->size - 1].cs_change = 1;
	else
		spi_message_init(&bus_data->msg);

	xfer = &bus_data->xfers[bus_data->size];
	auto buf = &st->data[bus_data->size];
	switch (st->chip_info->regmap_type) {
	case AD5310_REGMAP:
		buf->d16 = cpu_to_be16(FIELD_PREP(AD5310_CMD_MSK, cmd) |
				       FIELD_PREP(AD5310_DATA_MSK, val));
		*xfer = (struct spi_transfer) {
			.tx_buf = &buf->d16,
			.len = sizeof(buf->d16),
		};
		break;
	case AD5683_REGMAP:
		buf->d32 = cpu_to_be32(FIELD_PREP(AD5686_CMD_MSK, cmd) |
				       FIELD_PREP(AD5683_DATA_MSK, val));
		*xfer = (struct spi_transfer) {
			.tx_buf = &buf->d8[1],
			.len = sizeof(buf->d8) - 1,
		};
		break;
	case AD5686_REGMAP:
		buf->d32 = cpu_to_be32(FIELD_PREP(AD5686_CMD_MSK, cmd) |
				       FIELD_PREP(AD5686_ADDR_MSK, addr) |
				       FIELD_PREP(AD5686_DATA_MSK, val));
		*xfer = (struct spi_transfer) {
			.tx_buf = &buf->d8[1],
			.len = sizeof(buf->d8) - 1,
		};
		break;
	default:
		return -EINVAL;
	}

	spi_message_add_tail(xfer, &bus_data->msg);
	bus_data->size++;

	return 0;
}

static int ad5686_spi_sync(struct ad5686_state *st)
{
	struct spi_device *spi = to_spi_device(st->dev);
	struct ad5686_spi_data *bus_data = st->bus_data;

	bus_data->size = 0; /* always reset, even on sync failure */
	return spi_sync(spi, &bus_data->msg);
}

static int ad5686_spi_read(struct ad5686_state *st, u8 addr)
{
	struct spi_device *spi = to_spi_device(st->dev);
	struct ad5686_spi_data *bus_data = st->bus_data;
	struct spi_transfer *xfer = &bus_data->xfers[0];
	u8 cmd = 0;
	int ret;

	switch (st->chip_info->regmap_type) {
	case AD5310_REGMAP:
		return -ENOTSUPP;
	case AD5683_REGMAP:
		cmd = AD5686_CMD_READBACK_ENABLE_V2;
		break;
	case AD5686_REGMAP:
		cmd = AD5686_CMD_READBACK_ENABLE;
		break;
	default:
		return -EINVAL;
	}

	st->data[0].d32 = cpu_to_be32(FIELD_PREP(AD5686_CMD_MSK, cmd) |
				      FIELD_PREP(AD5686_ADDR_MSK, addr));
	st->data[1].d32 = cpu_to_be32(FIELD_PREP(AD5686_CMD_MSK, AD5686_CMD_NOOP));

	xfer[0] = (struct spi_transfer) {
		.tx_buf = &st->data[0].d8[1],
		.len = sizeof(st->data[0].d8) - 1,
		.cs_change = 1,
	};
	xfer[1] = (struct spi_transfer) {
		.tx_buf = &st->data[1].d8[1],
		.rx_buf = &st->data[2].d8[1],
		.len = sizeof(st->data[1].d8) - 1,
	};

	spi_message_init_with_transfers(&bus_data->msg, xfer, 2);

	ret = spi_sync(spi, &bus_data->msg);
	if (ret)
		return ret;

	return be32_to_cpu(st->data[2].d32);
}

static const struct ad5686_bus_ops ad5686_spi_ops = {
	.write = ad5686_spi_write,
	.read = ad5686_spi_read,
	.sync = ad5686_spi_sync,
};

static int ad5686_spi_probe(struct spi_device *spi)
{
	const struct ad5686_chip_info *info;
	struct ad5686_spi_data *bus_data;
	struct device *dev = &spi->dev;
	unsigned int capacity;

	info = spi_get_device_match_data(spi);
	if (!info)
		return -ENODATA;

	/* read operation requires at least 2 transfers */
	capacity = max(info->num_channels, 2);
	bus_data = devm_kzalloc(dev, struct_size(bus_data, xfers, capacity),
				GFP_KERNEL);
	if (!bus_data)
		return -ENOMEM;

	bus_data->capacity = capacity;

	return ad5686_probe(dev, info, spi->modalias, &ad5686_spi_ops, bus_data);
}

static const struct spi_device_id ad5686_spi_id[] = {
	{ .name = "ad5310r", .driver_data = (kernel_ulong_t)&ad5310r_chip_info },
	{ .name = "ad5672r", .driver_data = (kernel_ulong_t)&ad5672r_chip_info },
	{ .name = "ad5674r", .driver_data = (kernel_ulong_t)&ad5674r_chip_info },
	{ .name = "ad5676",  .driver_data = (kernel_ulong_t)&ad5676_chip_info },
	{ .name = "ad5676r", .driver_data = (kernel_ulong_t)&ad5676r_chip_info },
	{ .name = "ad5679r", .driver_data = (kernel_ulong_t)&ad5679r_chip_info },
	{ .name = "ad5681r", .driver_data = (kernel_ulong_t)&ad5681r_chip_info },
	{ .name = "ad5682r", .driver_data = (kernel_ulong_t)&ad5682r_chip_info },
	{ .name = "ad5683",  .driver_data = (kernel_ulong_t)&ad5683_chip_info },
	{ .name = "ad5683r", .driver_data = (kernel_ulong_t)&ad5683r_chip_info },
	{ .name = "ad5684",  .driver_data = (kernel_ulong_t)&ad5684_chip_info },
	{ .name = "ad5684r", .driver_data = (kernel_ulong_t)&ad5684r_chip_info },
	{ .name = "ad5685",  .driver_data = (kernel_ulong_t)&ad5685r_chip_info }, /* nonexistent */
	{ .name = "ad5685r", .driver_data = (kernel_ulong_t)&ad5685r_chip_info },
	{ .name = "ad5686",  .driver_data = (kernel_ulong_t)&ad5686_chip_info },
	{ .name = "ad5686r", .driver_data = (kernel_ulong_t)&ad5686r_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad5686_spi_id);

static const struct of_device_id ad5686_of_match[] = {
	{ .compatible = "adi,ad5310r", .data = &ad5310r_chip_info },
	{ .compatible = "adi,ad5672r", .data = &ad5672r_chip_info },
	{ .compatible = "adi,ad5674r", .data = &ad5674r_chip_info },
	{ .compatible = "adi,ad5676",  .data = &ad5676_chip_info },
	{ .compatible = "adi,ad5676r", .data = &ad5676r_chip_info },
	{ .compatible = "adi,ad5679r", .data = &ad5679r_chip_info },
	{ .compatible = "adi,ad5681r", .data = &ad5681r_chip_info },
	{ .compatible = "adi,ad5682r", .data = &ad5682r_chip_info },
	{ .compatible = "adi,ad5683",  .data = &ad5683_chip_info },
	{ .compatible = "adi,ad5683r", .data = &ad5683r_chip_info },
	{ .compatible = "adi,ad5684",  .data = &ad5684_chip_info },
	{ .compatible = "adi,ad5684r", .data = &ad5684r_chip_info },
	{ .compatible = "adi,ad5685r", .data = &ad5685r_chip_info },
	{ .compatible = "adi,ad5686",  .data = &ad5686_chip_info },
	{ .compatible = "adi,ad5686r", .data = &ad5686r_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad5686_of_match);

static struct spi_driver ad5686_spi_driver = {
	.driver = {
		.name = "ad5686",
		.of_match_table = ad5686_of_match,
	},
	.probe = ad5686_spi_probe,
	.id_table = ad5686_spi_id,
};

module_spi_driver(ad5686_spi_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5686 and similar multi-channel DACs");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_AD5686");
