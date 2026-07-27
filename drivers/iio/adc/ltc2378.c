// SPDX-License-Identifier: GPL-2.0+
/*
 * Analog Devices LTC2378 ADC series driver
 *
 * Copyright (C) 2026 Analog Devices Inc.
 * Author: Marcelo Schmitt <marcelo.schmitt@analog.com>
 */

#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/byteorder/generic.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/device-id/spi.h>
#include <linux/device-id/of.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/units.h>

#include <linux/iio/iio.h>
#include <linux/iio/types.h>

#define LTC2378_MAX_DATA_WAIT_US	4 /* max(TBUSYLH + TCONV + TDSDOBUSYL) */

#define LTC2378_CHANNEL(_sign, _real_bits, _storage_bits)			\
{										\
	.type = IIO_VOLTAGE,							\
	.indexed = 1,								\
	.differential = _sign,							\
	.channel = 0,								\
	.channel2 = _sign ? 1 : 0,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |				\
			      BIT(IIO_CHAN_INFO_SCALE),				\
	.scan_index = 0,							\
	.scan_type = {								\
		.format = _sign ? IIO_SCAN_FORMAT_SIGNED_INT :			\
				  IIO_SCAN_FORMAT_UNSIGNED_INT,			\
		.realbits = _real_bits,						\
		.storagebits = _storage_bits,					\
		.shift = _storage_bits - _real_bits,				\
		.endianness = IIO_BE,						\
	},									\
}

#define LTC2378_DIFF_CHANNEL(_real_bits)					\
	LTC2378_CHANNEL(1, _real_bits, (((_real_bits) > 16) ? 32 : 16))

#define LTC2378_PSEUDO_DIFF_CHANNEL(_real_bits)					\
	LTC2378_CHANNEL(0, _real_bits, (((_real_bits) > 16) ? 32 : 16))

struct ltc2378_chip_info {
	const char *name;
	struct iio_chan_spec chan;
};

struct ltc2378_state {
	const struct ltc2378_chip_info *info;
	struct gpio_desc *cnv_gpio;
	struct spi_device *spi;
	struct mutex lock; /* Protect data acquisition cycle */
	int ref_uV;
	struct spi_transfer xfer;

	/*
	 * DMA (thus cache coherency maintenance) requires the transfer buffers
	 * to live in their own cache lines.
	 */
	struct {
		union {
			__be16 sample_buf16_be;
			__be32 sample_buf32_be;
			u16 sample_buf16;
			u32 sample_buf32;
		} data;
		aligned_s64 timestamp;
	} scan __aligned(IIO_DMA_MINALIGN);
};

static const struct ltc2378_chip_info ltc2364_16_chip_info = {
	.name = "ltc2364-16",
	.chan = LTC2378_PSEUDO_DIFF_CHANNEL(16),
};

static const struct ltc2378_chip_info ltc2364_18_chip_info = {
	.name = "ltc2364-18",
	.chan = LTC2378_PSEUDO_DIFF_CHANNEL(18),
};

static const struct ltc2378_chip_info ltc2367_16_chip_info = {
	.name = "ltc2367-16",
	.chan = LTC2378_PSEUDO_DIFF_CHANNEL(16),
};

static const struct ltc2378_chip_info ltc2367_18_chip_info = {
	.name = "ltc2367-18",
	.chan = LTC2378_PSEUDO_DIFF_CHANNEL(18),
};

static const struct ltc2378_chip_info ltc2368_16_chip_info = {
	.name = "ltc2368-16",
	.chan = LTC2378_PSEUDO_DIFF_CHANNEL(16),
};

static const struct ltc2378_chip_info ltc2368_18_chip_info = {
	.name = "ltc2368-18",
	.chan = LTC2378_PSEUDO_DIFF_CHANNEL(18),
};

static const struct ltc2378_chip_info ltc2369_18_chip_info = {
	.name = "ltc2369-18",
	.chan = LTC2378_PSEUDO_DIFF_CHANNEL(18),
};

static const struct ltc2378_chip_info ltc2370_16_chip_info = {
	.name = "ltc2370-16",
	.chan = LTC2378_PSEUDO_DIFF_CHANNEL(16),
};

static const struct ltc2378_chip_info ltc2376_16_chip_info = {
	.name = "ltc2376-16",
	.chan = LTC2378_DIFF_CHANNEL(16),
};

static const struct ltc2378_chip_info ltc2376_18_chip_info = {
	.name = "ltc2376-18",
	.chan = LTC2378_DIFF_CHANNEL(18),
};

static const struct ltc2378_chip_info ltc2376_20_chip_info = {
	.name = "ltc2376-20",
	.chan = LTC2378_DIFF_CHANNEL(20),
};

static const struct ltc2378_chip_info ltc2377_16_chip_info = {
	.name = "ltc2377-16",
	.chan = LTC2378_DIFF_CHANNEL(16),
};

static const struct ltc2378_chip_info ltc2377_18_chip_info = {
	.name = "ltc2377-18",
	.chan = LTC2378_DIFF_CHANNEL(18),
};

static const struct ltc2378_chip_info ltc2377_20_chip_info = {
	.name = "ltc2377-20",
	.chan = LTC2378_DIFF_CHANNEL(20),
};

static const struct ltc2378_chip_info ltc2378_16_chip_info = {
	.name = "ltc2378-16",
	.chan = LTC2378_DIFF_CHANNEL(16),
};

static const struct ltc2378_chip_info ltc2378_18_chip_info = {
	.name = "ltc2378-18",
	.chan = LTC2378_DIFF_CHANNEL(18),
};

static const struct ltc2378_chip_info ltc2378_20_chip_info = {
	.name = "ltc2378-20",
	.chan = LTC2378_DIFF_CHANNEL(20),
};

static const struct ltc2378_chip_info ltc2379_18_chip_info = {
	.name = "ltc2379-18",
	.chan = LTC2378_DIFF_CHANNEL(18),
};

static const struct ltc2378_chip_info ltc2380_16_chip_info = {
	.name = "ltc2380-16",
	.chan = LTC2378_DIFF_CHANNEL(16),
};

static int ltc2378_convert_and_acquire(struct ltc2378_state *st)
{
	int ret;

	/* Cause a rising edge of CNV to initiate a new ADC conversion */
	gpiod_set_value_cansleep(st->cnv_gpio, 1);
	fsleep(LTC2378_MAX_DATA_WAIT_US);
	ret = spi_sync_transfer(st->spi, &st->xfer, 1);
	gpiod_set_value_cansleep(st->cnv_gpio, 0);

	return ret;
}

static int ltc2378_channel_single_read(const struct iio_chan_spec *chan,
				       struct ltc2378_state *st, int *val)
{
	const struct iio_scan_type *scan_type = &chan->scan_type;
	u32 sample;
	int ret;

	guard(mutex)(&st->lock);
	ret = ltc2378_convert_and_acquire(st);
	if (ret)
		return ret;

	if (chan->scan_type.endianness == IIO_BE) {
		if (chan->scan_type.realbits > 16)
			sample = be32_to_cpu(st->scan.data.sample_buf32_be);
		else
			sample = be16_to_cpu(st->scan.data.sample_buf16_be);
	} else { /* IIO_CPU */
		if (chan->scan_type.realbits > 16)
			sample = st->scan.data.sample_buf32;
		else
			sample = st->scan.data.sample_buf16;
	}

	sample >>= chan->scan_type.shift;

	if (scan_type->format == IIO_SCAN_FORMAT_SIGNED_INT)
		*val = sign_extend32(sample, scan_type->realbits - 1);
	else
		*val = sample;

	return 0;
}

static int ltc2378_read_raw(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan,
			    int *val, int *val2, long mask)
{
	struct ltc2378_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
		if (IIO_DEV_ACQUIRE_FAILED(claim))
			return -EBUSY;

		ret = ltc2378_channel_single_read(chan, st, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		*val = st->ref_uV / MILLI;
		/*
		 * For all LTC2378-like devices, the amount of bits that express
		 * voltage magnitude depend on the polarity / output code format:
		 * - straight binary: All precision/resolution bits are used.
		 * - 2's complement: One of the precision bits is used for sign.
		 */
		if (chan->scan_type.format == IIO_SCAN_FORMAT_SIGNED_INT)
			*val2 = chan->scan_type.realbits - 1;
		else
			*val2 = chan->scan_type.realbits;

		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

static const struct iio_info ltc2378_iio_info = {
	.read_raw = &ltc2378_read_raw,
};

static int ltc2378_ref_setup(struct device *dev, struct ltc2378_state *st)
{
	int ret;

	ret = devm_regulator_get_enable_read_voltage(dev, "ref");
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to read ref regulator\n");

	st->ref_uV = ret;

	return 0;
}

static int ltc2378_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ltc2378_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	st->info = spi_get_device_match_data(spi);
	if (!st->info)
		return -EINVAL;

	ret = ltc2378_ref_setup(dev, st);
	if (ret)
		return ret;

	indio_dev->name = st->info->name;
	indio_dev->info = &ltc2378_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	st->cnv_gpio = devm_gpiod_get(dev, "cnv", GPIOD_OUT_LOW);
	if (IS_ERR(st->cnv_gpio))
		return dev_err_probe(dev, PTR_ERR(st->cnv_gpio),
				     "failed to get CNV GPIO");

	indio_dev->channels = &st->info->chan;
	indio_dev->num_channels = 1;

	st->xfer.rx_buf = &st->scan.data;
	st->xfer.len = spi_bpw_to_bytes(indio_dev->channels[0].scan_type.realbits);

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id ltc2378_of_match[] = {
	{ .compatible = "adi,ltc2364-16", .data = &ltc2364_16_chip_info },
	{ .compatible = "adi,ltc2364-18", .data = &ltc2364_18_chip_info },
	{ .compatible = "adi,ltc2367-16", .data = &ltc2367_16_chip_info },
	{ .compatible = "adi,ltc2367-18", .data = &ltc2367_18_chip_info },
	{ .compatible = "adi,ltc2368-16", .data = &ltc2368_16_chip_info },
	{ .compatible = "adi,ltc2368-18", .data = &ltc2368_18_chip_info },
	{ .compatible = "adi,ltc2369-18", .data = &ltc2369_18_chip_info },
	{ .compatible = "adi,ltc2370-16", .data = &ltc2370_16_chip_info },
	{ .compatible = "adi,ltc2376-16", .data = &ltc2376_16_chip_info },
	{ .compatible = "adi,ltc2376-18", .data = &ltc2376_18_chip_info },
	{ .compatible = "adi,ltc2376-20", .data = &ltc2376_20_chip_info },
	{ .compatible = "adi,ltc2377-16", .data = &ltc2377_16_chip_info },
	{ .compatible = "adi,ltc2377-18", .data = &ltc2377_18_chip_info },
	{ .compatible = "adi,ltc2377-20", .data = &ltc2377_20_chip_info },
	{ .compatible = "adi,ltc2378-16", .data = &ltc2378_16_chip_info },
	{ .compatible = "adi,ltc2378-18", .data = &ltc2378_18_chip_info },
	{ .compatible = "adi,ltc2378-20", .data = &ltc2378_20_chip_info },
	{ .compatible = "adi,ltc2379-18", .data = &ltc2379_18_chip_info },
	{ .compatible = "adi,ltc2380-16", .data = &ltc2380_16_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ltc2378_of_match);

static const struct spi_device_id ltc2378_spi_id[] = {
	{ .name = "ltc2364-16", .driver_data = (kernel_ulong_t)&ltc2364_16_chip_info },
	{ .name = "ltc2364-18", .driver_data = (kernel_ulong_t)&ltc2364_18_chip_info },
	{ .name = "ltc2367-16", .driver_data = (kernel_ulong_t)&ltc2367_16_chip_info },
	{ .name = "ltc2367-18", .driver_data = (kernel_ulong_t)&ltc2367_18_chip_info },
	{ .name = "ltc2368-16", .driver_data = (kernel_ulong_t)&ltc2368_16_chip_info },
	{ .name = "ltc2368-18", .driver_data = (kernel_ulong_t)&ltc2368_18_chip_info },
	{ .name = "ltc2369-18", .driver_data = (kernel_ulong_t)&ltc2369_18_chip_info },
	{ .name = "ltc2370-16", .driver_data = (kernel_ulong_t)&ltc2370_16_chip_info },
	{ .name = "ltc2376-16", .driver_data = (kernel_ulong_t)&ltc2376_16_chip_info },
	{ .name = "ltc2376-18", .driver_data = (kernel_ulong_t)&ltc2376_18_chip_info },
	{ .name = "ltc2376-20", .driver_data = (kernel_ulong_t)&ltc2376_20_chip_info },
	{ .name = "ltc2377-16", .driver_data = (kernel_ulong_t)&ltc2377_16_chip_info },
	{ .name = "ltc2377-18", .driver_data = (kernel_ulong_t)&ltc2377_18_chip_info },
	{ .name = "ltc2377-20", .driver_data = (kernel_ulong_t)&ltc2377_20_chip_info },
	{ .name = "ltc2378-16", .driver_data = (kernel_ulong_t)&ltc2378_16_chip_info },
	{ .name = "ltc2378-18", .driver_data = (kernel_ulong_t)&ltc2378_18_chip_info },
	{ .name = "ltc2378-20", .driver_data = (kernel_ulong_t)&ltc2378_20_chip_info },
	{ .name = "ltc2379-18", .driver_data = (kernel_ulong_t)&ltc2379_18_chip_info },
	{ .name = "ltc2380-16", .driver_data = (kernel_ulong_t)&ltc2380_16_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ltc2378_spi_id);

static struct spi_driver ltc2378_driver = {
	.driver = {
		.name = "ltc2378",
		.of_match_table = ltc2378_of_match
	},
	.probe = ltc2378_probe,
	.id_table = ltc2378_spi_id,
};
module_spi_driver(ltc2378_driver);

MODULE_AUTHOR("Marcelo Schmitt <marcelo.schmitt@analog.com>");
MODULE_DESCRIPTION("Analog Devices LTC2378 ADC series driver");
MODULE_LICENSE("GPL");
