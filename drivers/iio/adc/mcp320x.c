/*
 * Copyright (C) 2013 Oskar Andero <oskar.andero@gmail.com>
 *
 * Driver for Microchip Technology's MCP3204 and MCP3208 ADC chips.
 * Datasheet can be found here:
 * http://ww1.microchip.com/downloads/en/devicedoc/21298c.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

#define MCP_SINGLE_ENDED	(1 << 3)
#define MCP_START_BIT		(1 << 4)

enum {
	mcp3204,
	mcp3208,
};

struct mcp320x {
	struct spi_device *spi;
	struct spi_message msg;
	struct spi_transfer transfer[2];

	u8 tx_buf;
	u8 rx_buf[2];

	struct regulator *reg;
	struct mutex lock;
};

static int mcp320x_adc_conversion(struct mcp320x *adc, u8 msg)
{
	int ret;

	adc->tx_buf = msg;
	ret = spi_sync(adc->spi, &adc->msg);
	if (ret < 0)
		return ret;

	return ((adc->rx_buf[0] & 0x3f) << 6)  |
		(adc->rx_buf[1] >> 2);
}

static int mcp320x_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel, int *val,
			    int *val2, long mask)
{
	struct mcp320x *adc = iio_priv(indio_dev);
	int ret = -EINVAL;

	mutex_lock(&adc->lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (channel->differential)
			ret = mcp320x_adc_conversion(adc,
				MCP_START_BIT | channel->address);
		else
			ret = mcp320x_adc_conversion(adc,
				MCP_START_BIT | MCP_SINGLE_ENDED |
				channel->address);
		if (ret < 0)
			goto out;

		*val = ret;
		ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_SCALE:
		/* Digital output code = (4096 * Vin) / Vref */
		ret = regulator_get_voltage(adc->reg);
		if (ret < 0)
			goto out;

		*val = ret / 1000;
		*val2 = 12;
		ret = IIO_VAL_FRACTIONAL_LOG2;
		break;

	default:
		break;
	}

out:
	mutex_unlock(&adc->lock);

	return ret;
}

#define MCP320X_VOLTAGE_CHANNEL(num)				\
	{							\
		.type = IIO_VOLTAGE,				\
		.indexed = 1,					\
		.channel = (num),				\
		.address = (num),				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) \
	}

#define MCP320X_VOLTAGE_CHANNEL_DIFF(num)			\
	{							\
		.type = IIO_VOLTAGE,				\
		.indexed = 1,					\
		.channel = (num * 2),				\
		.channel2 = (num * 2 + 1),			\
		.address = (num * 2),				\
		.differential = 1,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) \
	}

static const struct iio_chan_spec mcp3204_channels[] = {
	MCP320X_VOLTAGE_CHANNEL(0),
	MCP320X_VOLTAGE_CHANNEL(1),
	MCP320X_VOLTAGE_CHANNEL(2),
	MCP320X_VOLTAGE_CHANNEL(3),
	MCP320X_VOLTAGE_CHANNEL_DIFF(0),
	MCP320X_VOLTAGE_CHANNEL_DIFF(1),
};

static const struct iio_chan_spec mcp3208_channels[] = {
	MCP320X_VOLTAGE_CHANNEL(0),
	MCP320X_VOLTAGE_CHANNEL(1),
	MCP320X_VOLTAGE_CHANNEL(2),
	MCP320X_VOLTAGE_CHANNEL(3),
	MCP320X_VOLTAGE_CHANNEL(4),
	MCP320X_VOLTAGE_CHANNEL(5),
	MCP320X_VOLTAGE_CHANNEL(6),
	MCP320X_VOLTAGE_CHANNEL(7),
	MCP320X_VOLTAGE_CHANNEL_DIFF(0),
	MCP320X_VOLTAGE_CHANNEL_DIFF(1),
	MCP320X_VOLTAGE_CHANNEL_DIFF(2),
	MCP320X_VOLTAGE_CHANNEL_DIFF(3),
};

static const struct iio_info mcp320x_info = {
	.read_raw = mcp320x_read_raw,
	.driver_module = THIS_MODULE,
};

struct mcp3208_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
};

static const struct mcp3208_chip_info mcp3208_chip_infos[] = {
	[mcp3204] = {
		.channels = mcp3204_channels,
		.num_channels = ARRAY_SIZE(mcp3204_channels)
	},
	[mcp3208] = {
		.channels = mcp3208_channels,
		.num_channels = ARRAY_SIZE(mcp3208_channels)
	},
};

static int mcp320x_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct mcp320x *adc;
	const struct mcp3208_chip_info *chip_info;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mcp320x_info;

	chip_info = &mcp3208_chip_infos[spi_get_device_id(spi)->driver_data];
	indio_dev->channels = chip_info->channels;
	indio_dev->num_channels = chip_info->num_channels;

	adc->transfer[0].tx_buf = &adc->tx_buf;
	adc->transfer[0].len = sizeof(adc->tx_buf);
	adc->transfer[1].rx_buf = adc->rx_buf;
	adc->transfer[1].len = sizeof(adc->rx_buf);

	spi_message_init_with_transfers(&adc->msg, adc->transfer,
					ARRAY_SIZE(adc->transfer));

	adc->reg = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(adc->reg))
		return PTR_ERR(adc->reg);

	ret = regulator_enable(adc->reg);
	if (ret < 0)
		return ret;

	mutex_init(&adc->lock);

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto reg_disable;

	return 0;

reg_disable:
	regulator_disable(adc->reg);

	return ret;
}

static int mcp320x_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct mcp320x *adc = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	regulator_disable(adc->reg);

	return 0;
}

static const struct spi_device_id mcp320x_id[] = {
	{ "mcp3204", mcp3204 },
	{ "mcp3208", mcp3208 },
	{ }
};
MODULE_DEVICE_TABLE(spi, mcp320x_id);

static struct spi_driver mcp320x_driver = {
	.driver = {
		.name = "mcp320x",
		.owner = THIS_MODULE,
	},
	.probe = mcp320x_probe,
	.remove = mcp320x_remove,
	.id_table = mcp320x_id,
};
module_spi_driver(mcp320x_driver);

MODULE_AUTHOR("Oskar Andero <oskar.andero@gmail.com>");
MODULE_DESCRIPTION("Microchip Technology MCP3204/08");
MODULE_LICENSE("GPL v2");
