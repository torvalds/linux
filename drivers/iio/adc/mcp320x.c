// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Oskar Andero <oskar.andero@gmail.com>
 * Copyright (C) 2014 Rose Technology
 * 	   Allan Bendorff Jensen <abj@rosetechnology.dk>
 *	   Soren Andersen <san@rosetechnology.dk>
 *
 * Driver for following ADC chips from Microchip Technology's:
 * 10 Bit converter
 * MCP3001
 * MCP3002
 * MCP3004
 * MCP3008
 * ------------
 * 12 bit converter
 * MCP3201
 * MCP3202
 * MCP3204
 * MCP3208
 * ------------
 * 13 bit converter
 * MCP3301
 * ------------
 * 22 bit converter
 * MCP3550
 * MCP3551
 * MCP3553
 *
 * Datasheet can be found here:
 * https://ww1.microchip.com/downloads/en/DeviceDoc/21293C.pdf  mcp3001
 * https://ww1.microchip.com/downloads/en/DeviceDoc/21294E.pdf  mcp3002
 * https://ww1.microchip.com/downloads/en/DeviceDoc/21295d.pdf  mcp3004/08
 * http://ww1.microchip.com/downloads/en/DeviceDoc/21290D.pdf  mcp3201
 * http://ww1.microchip.com/downloads/en/DeviceDoc/21034D.pdf  mcp3202
 * http://ww1.microchip.com/downloads/en/DeviceDoc/21298c.pdf  mcp3204/08
 * https://ww1.microchip.com/downloads/en/DeviceDoc/21700E.pdf  mcp3301
 * http://ww1.microchip.com/downloads/en/DeviceDoc/21950D.pdf  mcp3550/1/3
 */

#include <linux/err.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

enum {
	mcp3001,
	mcp3002,
	mcp3004,
	mcp3008,
	mcp3201,
	mcp3202,
	mcp3204,
	mcp3208,
	mcp3301,
	mcp3550_50,
	mcp3550_60,
	mcp3551,
	mcp3553,
};

struct mcp320x_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	unsigned int resolution;
	unsigned int conv_time; /* usec */
};

/**
 * struct mcp320x - Microchip SPI ADC instance
 * @spi: SPI slave (parent of the IIO device)
 * @msg: SPI message to select a channel and receive a value from the ADC
 * @transfer: SPI transfers used by @msg
 * @start_conv_msg: SPI message to start a conversion by briefly asserting CS
 * @start_conv_transfer: SPI transfer used by @start_conv_msg
 * @reg: regulator generating Vref
 * @lock: protects read sequences
 * @chip_info: ADC properties
 * @tx_buf: buffer for @transfer[0] (not used on single-channel converters)
 * @rx_buf: buffer for @transfer[1]
 */
struct mcp320x {
	struct spi_device *spi;
	struct spi_message msg;
	struct spi_transfer transfer[2];
	struct spi_message start_conv_msg;
	struct spi_transfer start_conv_transfer;

	struct regulator *reg;
	struct mutex lock;
	const struct mcp320x_chip_info *chip_info;

	u8 tx_buf ____cacheline_aligned;
	u8 rx_buf[4];
};

static int mcp320x_channel_to_tx_data(int device_index,
			const unsigned int channel, bool differential)
{
	int start_bit = 1;

	switch (device_index) {
	case mcp3002:
	case mcp3202:
		return ((start_bit << 4) | (!differential << 3) |
							(channel << 2));
	case mcp3004:
	case mcp3204:
	case mcp3008:
	case mcp3208:
		return ((start_bit << 6) | (!differential << 5) |
							(channel << 2));
	default:
		return -EINVAL;
	}
}

static int mcp320x_adc_conversion(struct mcp320x *adc, u8 channel,
				  bool differential, int device_index, int *val)
{
	int ret;

	if (adc->chip_info->conv_time) {
		ret = spi_sync(adc->spi, &adc->start_conv_msg);
		if (ret < 0)
			return ret;

		usleep_range(adc->chip_info->conv_time,
			     adc->chip_info->conv_time + 100);
	}

	memset(&adc->rx_buf, 0, sizeof(adc->rx_buf));
	if (adc->chip_info->num_channels > 1)
		adc->tx_buf = mcp320x_channel_to_tx_data(device_index, channel,
							 differential);

	ret = spi_sync(adc->spi, &adc->msg);
	if (ret < 0)
		return ret;

	switch (device_index) {
	case mcp3001:
		*val = (adc->rx_buf[0] << 5 | adc->rx_buf[1] >> 3);
		return 0;
	case mcp3002:
	case mcp3004:
	case mcp3008:
		*val = (adc->rx_buf[0] << 2 | adc->rx_buf[1] >> 6);
		return 0;
	case mcp3201:
		*val = (adc->rx_buf[0] << 7 | adc->rx_buf[1] >> 1);
		return 0;
	case mcp3202:
	case mcp3204:
	case mcp3208:
		*val = (adc->rx_buf[0] << 4 | adc->rx_buf[1] >> 4);
		return 0;
	case mcp3301:
		*val = sign_extend32((adc->rx_buf[0] & 0x1f) << 8
				    | adc->rx_buf[1], 12);
		return 0;
	case mcp3550_50:
	case mcp3550_60:
	case mcp3551:
	case mcp3553: {
		u32 raw = be32_to_cpup((__be32 *)adc->rx_buf);

		if (!(adc->spi->mode & SPI_CPOL))
			raw <<= 1; /* strip Data Ready bit in SPI mode 0,0 */

		/*
		 * If the input is within -vref and vref, bit 21 is the sign.
		 * Up to 12% overrange or underrange are allowed, in which case
		 * bit 23 is the sign and bit 0 to 21 is the value.
		 */
		raw >>= 8;
		if (raw & BIT(22) && raw & BIT(23))
			return -EIO; /* cannot have overrange AND underrange */
		else if (raw & BIT(22))
			raw &= ~BIT(22); /* overrange */
		else if (raw & BIT(23) || raw & BIT(21))
			raw |= GENMASK(31, 22); /* underrange or negative */

		*val = (s32)raw;
		return 0;
		}
	default:
		return -EINVAL;
	}
}

static int mcp320x_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel, int *val,
			    int *val2, long mask)
{
	struct mcp320x *adc = iio_priv(indio_dev);
	int ret = -EINVAL;
	int device_index = 0;

	mutex_lock(&adc->lock);

	device_index = spi_get_device_id(adc->spi)->driver_data;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = mcp320x_adc_conversion(adc, channel->address,
			channel->differential, device_index, val);
		if (ret < 0)
			goto out;

		ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(adc->reg);
		if (ret < 0)
			goto out;

		/* convert regulator output voltage to mV */
		*val = ret / 1000;
		*val2 = adc->chip_info->resolution;
		ret = IIO_VAL_FRACTIONAL_LOG2;
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

#define MCP320X_VOLTAGE_CHANNEL_DIFF(chan1, chan2)		\
	{							\
		.type = IIO_VOLTAGE,				\
		.indexed = 1,					\
		.channel = (chan1),				\
		.channel2 = (chan2),				\
		.address = (chan1),				\
		.differential = 1,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) \
	}

static const struct iio_chan_spec mcp3201_channels[] = {
	MCP320X_VOLTAGE_CHANNEL_DIFF(0, 1),
};

static const struct iio_chan_spec mcp3202_channels[] = {
	MCP320X_VOLTAGE_CHANNEL(0),
	MCP320X_VOLTAGE_CHANNEL(1),
	MCP320X_VOLTAGE_CHANNEL_DIFF(0, 1),
	MCP320X_VOLTAGE_CHANNEL_DIFF(1, 0),
};

static const struct iio_chan_spec mcp3204_channels[] = {
	MCP320X_VOLTAGE_CHANNEL(0),
	MCP320X_VOLTAGE_CHANNEL(1),
	MCP320X_VOLTAGE_CHANNEL(2),
	MCP320X_VOLTAGE_CHANNEL(3),
	MCP320X_VOLTAGE_CHANNEL_DIFF(0, 1),
	MCP320X_VOLTAGE_CHANNEL_DIFF(1, 0),
	MCP320X_VOLTAGE_CHANNEL_DIFF(2, 3),
	MCP320X_VOLTAGE_CHANNEL_DIFF(3, 2),
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
	MCP320X_VOLTAGE_CHANNEL_DIFF(0, 1),
	MCP320X_VOLTAGE_CHANNEL_DIFF(1, 0),
	MCP320X_VOLTAGE_CHANNEL_DIFF(2, 3),
	MCP320X_VOLTAGE_CHANNEL_DIFF(3, 2),
	MCP320X_VOLTAGE_CHANNEL_DIFF(4, 5),
	MCP320X_VOLTAGE_CHANNEL_DIFF(5, 4),
	MCP320X_VOLTAGE_CHANNEL_DIFF(6, 7),
	MCP320X_VOLTAGE_CHANNEL_DIFF(7, 6),
};

static const struct iio_info mcp320x_info = {
	.read_raw = mcp320x_read_raw,
};

static const struct mcp320x_chip_info mcp320x_chip_infos[] = {
	[mcp3001] = {
		.channels = mcp3201_channels,
		.num_channels = ARRAY_SIZE(mcp3201_channels),
		.resolution = 10
	},
	[mcp3002] = {
		.channels = mcp3202_channels,
		.num_channels = ARRAY_SIZE(mcp3202_channels),
		.resolution = 10
	},
	[mcp3004] = {
		.channels = mcp3204_channels,
		.num_channels = ARRAY_SIZE(mcp3204_channels),
		.resolution = 10
	},
	[mcp3008] = {
		.channels = mcp3208_channels,
		.num_channels = ARRAY_SIZE(mcp3208_channels),
		.resolution = 10
	},
	[mcp3201] = {
		.channels = mcp3201_channels,
		.num_channels = ARRAY_SIZE(mcp3201_channels),
		.resolution = 12
	},
	[mcp3202] = {
		.channels = mcp3202_channels,
		.num_channels = ARRAY_SIZE(mcp3202_channels),
		.resolution = 12
	},
	[mcp3204] = {
		.channels = mcp3204_channels,
		.num_channels = ARRAY_SIZE(mcp3204_channels),
		.resolution = 12
	},
	[mcp3208] = {
		.channels = mcp3208_channels,
		.num_channels = ARRAY_SIZE(mcp3208_channels),
		.resolution = 12
	},
	[mcp3301] = {
		.channels = mcp3201_channels,
		.num_channels = ARRAY_SIZE(mcp3201_channels),
		.resolution = 13
	},
	[mcp3550_50] = {
		.channels = mcp3201_channels,
		.num_channels = ARRAY_SIZE(mcp3201_channels),
		.resolution = 21,
		/* 2% max deviation + 144 clock periods to exit shutdown */
		.conv_time = 80000 * 1.02 + 144000 / 102.4,
	},
	[mcp3550_60] = {
		.channels = mcp3201_channels,
		.num_channels = ARRAY_SIZE(mcp3201_channels),
		.resolution = 21,
		.conv_time = 66670 * 1.02 + 144000 / 122.88,
	},
	[mcp3551] = {
		.channels = mcp3201_channels,
		.num_channels = ARRAY_SIZE(mcp3201_channels),
		.resolution = 21,
		.conv_time = 73100 * 1.02 + 144000 / 112.64,
	},
	[mcp3553] = {
		.channels = mcp3201_channels,
		.num_channels = ARRAY_SIZE(mcp3201_channels),
		.resolution = 21,
		.conv_time = 16670 * 1.02 + 144000 / 122.88,
	},
};

static int mcp320x_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct mcp320x *adc;
	const struct mcp320x_chip_info *chip_info;
	int ret, device_index;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mcp320x_info;
	spi_set_drvdata(spi, indio_dev);

	device_index = spi_get_device_id(spi)->driver_data;
	chip_info = &mcp320x_chip_infos[device_index];
	indio_dev->channels = chip_info->channels;
	indio_dev->num_channels = chip_info->num_channels;

	adc->chip_info = chip_info;

	adc->transfer[0].tx_buf = &adc->tx_buf;
	adc->transfer[0].len = sizeof(adc->tx_buf);
	adc->transfer[1].rx_buf = adc->rx_buf;
	adc->transfer[1].len = DIV_ROUND_UP(chip_info->resolution, 8);

	if (chip_info->num_channels == 1)
		/* single-channel converters are rx only (no MOSI pin) */
		spi_message_init_with_transfers(&adc->msg,
						&adc->transfer[1], 1);
	else
		spi_message_init_with_transfers(&adc->msg, adc->transfer,
						ARRAY_SIZE(adc->transfer));

	switch (device_index) {
	case mcp3550_50:
	case mcp3550_60:
	case mcp3551:
	case mcp3553:
		/* rx len increases from 24 to 25 bit in SPI mode 0,0 */
		if (!(spi->mode & SPI_CPOL))
			adc->transfer[1].len++;

		/* conversions are started by asserting CS pin for 8 usec */
		adc->start_conv_transfer.delay.value = 8;
		adc->start_conv_transfer.delay.unit = SPI_DELAY_UNIT_USECS;
		spi_message_init_with_transfers(&adc->start_conv_msg,
						&adc->start_conv_transfer, 1);

		/*
		 * If CS was previously kept low (continuous conversion mode)
		 * and then changed to high, the chip is in shutdown.
		 * Sometimes it fails to wake from shutdown and clocks out
		 * only 0xffffff.  The magic sequence of performing two
		 * conversions without delay between them resets the chip
		 * and ensures all subsequent conversions succeed.
		 */
		mcp320x_adc_conversion(adc, 0, 1, device_index, &ret);
		mcp320x_adc_conversion(adc, 0, 1, device_index, &ret);
	}

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

static void mcp320x_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct mcp320x *adc = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	regulator_disable(adc->reg);
}

static const struct of_device_id mcp320x_dt_ids[] = {
	/* NOTE: The use of compatibles with no vendor prefix is deprecated. */
	{ .compatible = "mcp3001" },
	{ .compatible = "mcp3002" },
	{ .compatible = "mcp3004" },
	{ .compatible = "mcp3008" },
	{ .compatible = "mcp3201" },
	{ .compatible = "mcp3202" },
	{ .compatible = "mcp3204" },
	{ .compatible = "mcp3208" },
	{ .compatible = "mcp3301" },
	{ .compatible = "microchip,mcp3001" },
	{ .compatible = "microchip,mcp3002" },
	{ .compatible = "microchip,mcp3004" },
	{ .compatible = "microchip,mcp3008" },
	{ .compatible = "microchip,mcp3201" },
	{ .compatible = "microchip,mcp3202" },
	{ .compatible = "microchip,mcp3204" },
	{ .compatible = "microchip,mcp3208" },
	{ .compatible = "microchip,mcp3301" },
	{ .compatible = "microchip,mcp3550-50" },
	{ .compatible = "microchip,mcp3550-60" },
	{ .compatible = "microchip,mcp3551" },
	{ .compatible = "microchip,mcp3553" },
	{ }
};
MODULE_DEVICE_TABLE(of, mcp320x_dt_ids);

static const struct spi_device_id mcp320x_id[] = {
	{ "mcp3001", mcp3001 },
	{ "mcp3002", mcp3002 },
	{ "mcp3004", mcp3004 },
	{ "mcp3008", mcp3008 },
	{ "mcp3201", mcp3201 },
	{ "mcp3202", mcp3202 },
	{ "mcp3204", mcp3204 },
	{ "mcp3208", mcp3208 },
	{ "mcp3301", mcp3301 },
	{ "mcp3550-50", mcp3550_50 },
	{ "mcp3550-60", mcp3550_60 },
	{ "mcp3551", mcp3551 },
	{ "mcp3553", mcp3553 },
	{ }
};
MODULE_DEVICE_TABLE(spi, mcp320x_id);

static struct spi_driver mcp320x_driver = {
	.driver = {
		.name = "mcp320x",
		.of_match_table = mcp320x_dt_ids,
	},
	.probe = mcp320x_probe,
	.remove = mcp320x_remove,
	.id_table = mcp320x_id,
};
module_spi_driver(mcp320x_driver);

MODULE_AUTHOR("Oskar Andero <oskar.andero@gmail.com>");
MODULE_DESCRIPTION("Microchip Technology MCP3x01/02/04/08 and MCP3550/1/3");
MODULE_LICENSE("GPL v2");
