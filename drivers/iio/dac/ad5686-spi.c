// SPDX-License-Identifier: GPL-2.0+
/*
 * AD5672R, AD5676, AD5676R, AD5681R, AD5682R, AD5683, AD5683R,
 * AD5684, AD5684R, AD5685R, AD5686, AD5686R
 * Digital to analog converters driver
 *
 * Copyright 2018 Analog Devices Inc.
 */

#include "ad5686.h"

#include <linux/module.h>
#include <linux/spi/spi.h>

static int ad5686_spi_write(struct ad5686_state *st,
			    u8 cmd, u8 addr, u16 val)
{
	struct spi_device *spi = to_spi_device(st->dev);
	u8 tx_len, *buf;

	switch (st->chip_info->regmap_type) {
	case AD5310_REGMAP:
		st->data[0].d16 = cpu_to_be16(AD5310_CMD(cmd) |
					      val);
		buf = &st->data[0].d8[0];
		tx_len = 2;
		break;
	case AD5683_REGMAP:
		st->data[0].d32 = cpu_to_be32(AD5686_CMD(cmd) |
					      AD5683_DATA(val));
		buf = &st->data[0].d8[1];
		tx_len = 3;
		break;
	case AD5686_REGMAP:
		st->data[0].d32 = cpu_to_be32(AD5686_CMD(cmd) |
					      AD5686_ADDR(addr) |
					      val);
		buf = &st->data[0].d8[1];
		tx_len = 3;
		break;
	default:
		return -EINVAL;
	}

	return spi_write(spi, buf, tx_len);
}

static int ad5686_spi_read(struct ad5686_state *st, u8 addr)
{
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->data[0].d8[1],
			.len = 3,
			.cs_change = 1,
		}, {
			.tx_buf = &st->data[1].d8[1],
			.rx_buf = &st->data[2].d8[1],
			.len = 3,
		},
	};
	struct spi_device *spi = to_spi_device(st->dev);
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

	st->data[0].d32 = cpu_to_be32(AD5686_CMD(cmd) |
				      AD5686_ADDR(addr));
	st->data[1].d32 = cpu_to_be32(AD5686_CMD(AD5686_CMD_NOOP));

	ret = spi_sync_transfer(spi, t, ARRAY_SIZE(t));
	if (ret < 0)
		return ret;

	return be32_to_cpu(st->data[2].d32);
}

static int ad5686_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);

	return ad5686_probe(&spi->dev, id->driver_data, id->name,
			    ad5686_spi_write, ad5686_spi_read);
}

static int ad5686_spi_remove(struct spi_device *spi)
{
	return ad5686_remove(&spi->dev);
}

static const struct spi_device_id ad5686_spi_id[] = {
	{"ad5310r", ID_AD5310R},
	{"ad5672r", ID_AD5672R},
	{"ad5676", ID_AD5676},
	{"ad5676r", ID_AD5676R},
	{"ad5681r", ID_AD5681R},
	{"ad5682r", ID_AD5682R},
	{"ad5683", ID_AD5683},
	{"ad5683r", ID_AD5683R},
	{"ad5684", ID_AD5684},
	{"ad5684r", ID_AD5684R},
	{"ad5685", ID_AD5685R}, /* Does not exist */
	{"ad5685r", ID_AD5685R},
	{"ad5686", ID_AD5686},
	{"ad5686r", ID_AD5686R},
	{}
};
MODULE_DEVICE_TABLE(spi, ad5686_spi_id);

static struct spi_driver ad5686_spi_driver = {
	.driver = {
		.name = "ad5686",
	},
	.probe = ad5686_spi_probe,
	.remove = ad5686_spi_remove,
	.id_table = ad5686_spi_id,
};

module_spi_driver(ad5686_spi_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5686 and similar multi-channel DACs");
MODULE_LICENSE("GPL v2");
