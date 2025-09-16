// SPDX-License-Identifier: GPL-2.0-only
/*
 * Maxim MAX5522
 * Dual, Ultra-Low-Power 10-Bit, Voltage-Output DACs
 *
 * Copyright 2022 Timesys Corp.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>

#define MAX5522_MAX_ADDR	15
#define MAX5522_CTRL_NONE	0
#define MAX5522_CTRL_LOAD_IN_A	9
#define MAX5522_CTRL_LOAD_IN_B	10

#define MAX5522_REG_DATA(x)	((x) + MAX5522_CTRL_LOAD_IN_A)

struct max5522_chip_info {
	const char *name;
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
};

struct max5522_state {
	struct regmap *regmap;
	const struct max5522_chip_info *chip_info;
	unsigned short dac_cache[2];
	struct regulator *vrefin_reg;
};

#define MAX5522_CHANNEL(chan) {	\
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.output = 1, \
	.channel = chan, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_SCALE), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 10, \
		.storagebits = 16, \
		.shift = 2, \
	} \
}

static const struct iio_chan_spec max5522_channels[] = {
	MAX5522_CHANNEL(0),
	MAX5522_CHANNEL(1),
};

enum max5522_type {
	ID_MAX5522,
};

static const struct max5522_chip_info max5522_chip_info_tbl[] = {
	[ID_MAX5522] = {
		.name = "max5522",
		.channels = max5522_channels,
		.num_channels = 2,
	},
};

static inline int max5522_info_to_reg(struct iio_chan_spec const *chan)
{
	return MAX5522_REG_DATA(chan->channel);
}

static int max5522_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long info)
{
	struct max5522_state *state = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		*val = state->dac_cache[chan->channel];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(state->vrefin_reg);
		if (ret < 0)
			return -EINVAL;
		*val = ret / 1000;
		*val2 = 10;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int max5522_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long info)
{
	struct max5522_state *state = iio_priv(indio_dev);
	int rval;

	if (val > 1023 || val < 0)
		return -EINVAL;

	rval = regmap_write(state->regmap, max5522_info_to_reg(chan),
			    val << chan->scan_type.shift);
	if (rval < 0)
		return rval;

	state->dac_cache[chan->channel] = val;

	return 0;
}

static const struct iio_info max5522_info = {
	.read_raw = max5522_read_raw,
	.write_raw = max5522_write_raw,
};

static const struct regmap_config max5522_regmap_config = {
	.reg_bits = 4,
	.val_bits = 12,
	.max_register = MAX5522_MAX_ADDR,
};

static int max5522_spi_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct max5522_state *state;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*state));
	if (indio_dev == NULL) {
		dev_err(&spi->dev, "failed to allocate iio device\n");
		return  -ENOMEM;
	}

	state = iio_priv(indio_dev);
	state->chip_info = spi_get_device_match_data(spi);
	if (!state->chip_info)
		return -EINVAL;

	state->vrefin_reg = devm_regulator_get(&spi->dev, "vrefin");
	if (IS_ERR(state->vrefin_reg))
		return dev_err_probe(&spi->dev, PTR_ERR(state->vrefin_reg),
				     "Vrefin regulator not specified\n");

	ret = regulator_enable(state->vrefin_reg);
	if (ret) {
		return dev_err_probe(&spi->dev, ret,
				     "Failed to enable vref regulators\n");
	}

	state->regmap = devm_regmap_init_spi(spi, &max5522_regmap_config);

	if (IS_ERR(state->regmap))
		return PTR_ERR(state->regmap);

	indio_dev->info = &max5522_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = max5522_channels;
	indio_dev->num_channels = ARRAY_SIZE(max5522_channels);
	indio_dev->name = max5522_chip_info_tbl[ID_MAX5522].name;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id max5522_ids[] = {
	{ "max5522", (kernel_ulong_t)&max5522_chip_info_tbl[ID_MAX5522] },
	{ }
};
MODULE_DEVICE_TABLE(spi, max5522_ids);

static const struct of_device_id max5522_of_match[] = {
	{
		.compatible = "maxim,max5522",
		.data = &max5522_chip_info_tbl[ID_MAX5522],
	},
	{ }
};
MODULE_DEVICE_TABLE(of, max5522_of_match);

static struct spi_driver max5522_spi_driver = {
	.driver = {
		.name = "max5522",
		.of_match_table = max5522_of_match,
	},
	.probe = max5522_spi_probe,
	.id_table = max5522_ids,
};
module_spi_driver(max5522_spi_driver);

MODULE_AUTHOR("Angelo Dureghello <angelo.dureghello@timesys.com");
MODULE_DESCRIPTION("MAX5522 DAC driver");
MODULE_LICENSE("GPL");
