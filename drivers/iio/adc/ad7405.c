// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices AD7405 driver
 *
 * Copyright 2025 Analog Devices Inc.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/util_macros.h>

#include <linux/iio/backend.h>
#include <linux/iio/iio.h>

static const unsigned int ad7405_dec_rates_range[] = {
	32, 1, 4096,
};

struct ad7405_chip_info {
	const char *name;
	const unsigned int full_scale_mv;
};

struct ad7405_state {
	struct iio_backend *back;
	const struct ad7405_chip_info *info;
	unsigned int ref_frequency;
	unsigned int dec_rate;
};

static int ad7405_set_dec_rate(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       unsigned int dec_rate)
{
	struct ad7405_state *st = iio_priv(indio_dev);
	int ret;

	if (dec_rate > 4096 || dec_rate < 32)
		return -EINVAL;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = iio_backend_oversampling_ratio_set(st->back, chan->scan_index, dec_rate);
	iio_device_release_direct(indio_dev);

	if (ret < 0)
		return ret;

	st->dec_rate = dec_rate;

	return 0;
}

static int ad7405_read_raw(struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan, int *val,
			   int *val2, long info)
{
	struct ad7405_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		*val = st->info->full_scale_mv;
		*val2 = indio_dev->channels[0].scan_type.realbits - 1;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = st->dec_rate;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = DIV_ROUND_CLOSEST_ULL(st->ref_frequency, st->dec_rate);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		*val = -(1 << (indio_dev->channels[0].scan_type.realbits - 1));
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad7405_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val,
			    int val2, long info)
{
	switch (info) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		if (val < 0)
			return -EINVAL;
		return ad7405_set_dec_rate(indio_dev, chan, val);
	default:
		return -EINVAL;
	}
}

static int ad7405_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long info)
{
	switch (info) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = ad7405_dec_rates_range;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ad7405_iio_info = {
	.read_raw = &ad7405_read_raw,
	.write_raw = &ad7405_write_raw,
	.read_avail = &ad7405_read_avail,
};

static const struct iio_chan_spec ad7405_channel = {
	.type = IIO_VOLTAGE,
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
	.info_mask_shared_by_all = IIO_CHAN_INFO_SAMP_FREQ |
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	.info_mask_shared_by_all_available =
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	.indexed = 1,
	.channel = 0,
	.channel2 = 1,
	.differential = 1,
	.scan_index = 0,
	.scan_type = {
		.sign = 'u',
		.realbits = 16,
		.storagebits = 16,
	},
};

static const struct ad7405_chip_info ad7405_chip_info = {
	.name = "ad7405",
	.full_scale_mv = 320,
};

static const struct ad7405_chip_info adum7701_chip_info = {
	.name = "adum7701",
	.full_scale_mv = 320,
};

static const struct ad7405_chip_info adum7702_chip_info = {
	.name = "adum7702",
	.full_scale_mv = 64,
};

static const struct ad7405_chip_info adum7703_chip_info = {
	.name = "adum7703",
	.full_scale_mv = 320,
};

static const char * const ad7405_power_supplies[] = {
	"vdd1",	"vdd2",
};

static int ad7405_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct ad7405_state *st;
	struct clk *clk;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->info = device_get_match_data(dev);
	if (!st->info)
		return dev_err_probe(dev, -EINVAL, "no chip info\n");

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(ad7405_power_supplies),
					     ad7405_power_supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get and enable supplies");

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	st->ref_frequency = clk_get_rate(clk);
	if (!st->ref_frequency)
		return -EINVAL;

	indio_dev->name = st->info->name;
	indio_dev->channels = &ad7405_channel;
	indio_dev->num_channels = 1;
	indio_dev->info = &ad7405_iio_info;

	st->back = devm_iio_backend_get(dev, NULL);
	if (IS_ERR(st->back))
		return dev_err_probe(dev, PTR_ERR(st->back),
				     "failed to get IIO backend");

	ret = iio_backend_chan_enable(st->back, 0);
	if (ret)
		return ret;

	ret = devm_iio_backend_request_buffer(dev, st->back, indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_backend_enable(dev, st->back);
	if (ret)
		return ret;

	/*
	 * Set 256 decimation rate. The default value in the AXI_ADC register
	 * is 0, so we set the register with a decimation rate value that is
	 * functional for all parts.
	 */
	ret = ad7405_set_dec_rate(indio_dev, &indio_dev->channels[0], 256);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ad7405_of_match[] = {
	{ .compatible = "adi,ad7405", .data = &ad7405_chip_info, },
	{ .compatible = "adi,adum7701", .data = &adum7701_chip_info, },
	{ .compatible = "adi,adum7702", .data = &adum7702_chip_info, },
	{ .compatible = "adi,adum7703", .data = &adum7703_chip_info, },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7405_of_match);

static struct platform_driver ad7405_driver = {
	.driver = {
		.name = "ad7405",
		.of_match_table = ad7405_of_match,
	},
	.probe = ad7405_probe,
};
module_platform_driver(ad7405_driver);

MODULE_AUTHOR("Dragos Bogdan <dragos.bogdan@analog.com>");
MODULE_AUTHOR("Pop Ioan Daniel <pop.ioan-daniel@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7405 driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_BACKEND");
