// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim MAX11205 16-Bit Delta-Sigma ADC
 *
 * Datasheet: https://datasheets.maximintegrated.com/en/ds/MAX1240-max11205.pdf
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Ramona Bolboaca <ramona.bolboaca@analog.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/adc/ad_sigma_delta.h>

#define MAX11205_BIT_SCALE	15
#define MAX11205A_OUT_DATA_RATE	116
#define MAX11205B_OUT_DATA_RATE	13

enum max11205_chip_type {
	TYPE_MAX11205A,
	TYPE_MAX11205B,
};

struct max11205_chip_info {
	unsigned int	out_data_rate;
	const char	*name;
};

struct max11205_state {
	const struct max11205_chip_info	*chip_info;
	struct regulator		*vref;
	struct ad_sigma_delta		sd;
};

static const struct ad_sigma_delta_info max11205_sigma_delta_info = {
	.has_registers = false,
};

static int max11205_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max11205_state *st = iio_priv(indio_dev);
	int reg_mv;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return ad_sigma_delta_single_conversion(indio_dev, chan, val);
	case IIO_CHAN_INFO_SCALE:
		reg_mv = regulator_get_voltage(st->vref);
		if (reg_mv < 0)
			return reg_mv;
		reg_mv /= 1000;
		*val = reg_mv;
		*val2 = MAX11205_BIT_SCALE;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->chip_info->out_data_rate;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info max11205_iio_info = {
	.read_raw = max11205_read_raw,
	.validate_trigger = ad_sd_validate_trigger,
};

static const struct iio_chan_spec max11205_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct max11205_chip_info max11205_chip_info[] = {
	[TYPE_MAX11205A] = {
		.out_data_rate = MAX11205A_OUT_DATA_RATE,
		.name = "max11205a",
	},
	[TYPE_MAX11205B] = {
		.out_data_rate = MAX11205B_OUT_DATA_RATE,
		.name = "max11205b",
	},
};

static void max11205_reg_disable(void *reg)
{
	regulator_disable(reg);
}

static int max11205_probe(struct spi_device *spi)
{
	struct max11205_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	ad_sd_init(&st->sd, indio_dev, spi, &max11205_sigma_delta_info);

	st->chip_info = spi_get_device_match_data(spi);

	indio_dev->name = st->chip_info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = max11205_channels;
	indio_dev->num_channels = 1;
	indio_dev->info = &max11205_iio_info;

	st->vref = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(st->vref))
		return dev_err_probe(&spi->dev, PTR_ERR(st->vref),
				     "Failed to get vref regulator\n");

	ret = regulator_enable(st->vref);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, max11205_reg_disable, st->vref);
	if (ret)
		return ret;

	ret = devm_ad_sd_setup_buffer_and_trigger(&spi->dev, indio_dev);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id max11205_spi_ids[] = {
	{ "max11205a", (kernel_ulong_t)&max11205_chip_info[TYPE_MAX11205A] },
	{ "max11205b", (kernel_ulong_t)&max11205_chip_info[TYPE_MAX11205B] },
	{ }
};
MODULE_DEVICE_TABLE(spi, max11205_spi_ids);

static const struct of_device_id max11205_dt_ids[] = {
	{
		.compatible = "maxim,max11205a",
		.data = &max11205_chip_info[TYPE_MAX11205A],
	},
	{
		.compatible = "maxim,max11205b",
		.data = &max11205_chip_info[TYPE_MAX11205B],
	},
	{ }
};
MODULE_DEVICE_TABLE(of, max11205_dt_ids);

static struct spi_driver max11205_spi_driver = {
	.driver = {
		.name = "max11205",
		.of_match_table = max11205_dt_ids,
	},
	.probe = max11205_probe,
	.id_table = max11205_spi_ids,
};
module_spi_driver(max11205_spi_driver);

MODULE_AUTHOR("Ramona Bolboaca <ramona.bolboaca@analog.com>");
MODULE_DESCRIPTION("MAX11205 ADC driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_AD_SIGMA_DELTA");
