// SPDX-License-Identifier: GPL-2.0
/*
 * AD8366 and similar Gain Amplifiers
 * This driver supports the following gain amplifiers:
 *   AD8366 Dual-Digital Variable Gain Amplifier (VGA)
 *   ADA4961 BiCMOS RF Digital Gain Amplifier (DGA)
 *   ADL5240 Digitally controlled variable gain amplifier (VGA)
 *   ADRF5702: 0.125 dB LSB, 8-Bit, Silicon Digital Attenuator, 50 MHz to 20 GHz
 *   ADRF5703: 0.25 dB LSB, 7-Bit, Silicon Digital Attenuator, 9 kHz to 20 GHz
 *   ADRF5720: 0.5 dB LSB, 6-Bit, Silicon Digital Attenuator, 9 kHz to 40 GHz
 *   ADRF5730: 0.5 dB LSB, 6-Bit, Silicon Digital Attenuator, 100 MHz to 40 GHz
 *   ADRF5731: 2 dB LSB, 4-Bit, Silicon Digital Attenuator, 100 MHz to 40 GHz
 *   HMC271A: 1dB LSB 5-Bit Digital Attenuator SMT, 0.7 - 3.7 GHz
 *   HMC792A 0.25 dB LSB GaAs MMIC 6-Bit Digital Attenuator
 *   HMC1018A: 1.0 dB LSB GaAs MMIC 5-BIT DIGITAL ATTENUATOR, 0.1 - 30 GHz
 *   HMC1019A: 0.5 dB LSB GaAs MMIC 5-BIT DIGITAL ATTENUATOR, 0.1 - 30 GHz
 *   HMC1119 0.25 dB LSB, 7-Bit, Silicon Digital Attenuator
 *
 * Copyright 2012-2026 Analog Devices Inc.
 */

#include <linux/bitrev.h>
#include <linux/bits.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#include <linux/iio/iio.h>

struct ad8366_info {
	const char *name;
	int gain_min;
	int gain_max;
	int gain_step;
	size_t num_channels;
	size_t (*pack_code)(const unsigned char *code, size_t num_channels,
			    unsigned char *data);
};

struct ad8366_state {
	struct spi_device	*spi;
	struct mutex            lock; /* protect sensor state */
	unsigned char		ch[2];
	const struct ad8366_info *info;
	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 */
	unsigned char		data[2] __aligned(IIO_DMA_MINALIGN);
};

static size_t ad8366_pack_code(const unsigned char *code, size_t num_channels,
			       unsigned char *data)
{
	u8 ch_a = bitrev8(code[0]) >> 2;
	u8 ch_b = bitrev8(code[1]) >> 2;

	put_unaligned_be16((ch_b << 6) | ch_a, &data[0]);
	return sizeof(__be16);
}

static size_t adrf5731_pack_code(const unsigned char *code, size_t num_channels,
				 unsigned char *data)
{
	data[0] = code[0] << 2;
	return 1;
}

static size_t hmc271_pack_code(const unsigned char *code, size_t num_channels,
			       unsigned char *data)
{
	data[0] = bitrev8(code[0]) >> 3;
	return 1;
}

static const struct ad8366_info ad8366_chip_info = {
	.name = "ad8366",
	.gain_min = 4500,
	.gain_max = 20500,
	.gain_step = 253,
	.num_channels = 2,
	.pack_code = ad8366_pack_code,
};

static const struct ad8366_info ada4961_chip_info = {
	.name = "ada4961",
	.gain_min = -6000,
	.gain_max = 15000,
	.gain_step = -1000,
	.num_channels = 1,
};

static const struct ad8366_info adl5240_chip_info = {
	.name = "adl5240",
	.gain_min = -11500,
	.gain_max = 20000,
	.gain_step = 500,
	.num_channels = 1,
};

static const struct ad8366_info adrf5702_chip_info = {
	.name = "adrf5702",
	.gain_min = -31875,
	.gain_max = 0,
	.gain_step = -125,
	.num_channels = 1,
};

static const struct ad8366_info adrf5703_chip_info = {
	.name = "adrf5703",
	.gain_min = -31750,
	.gain_max = 0,
	.gain_step = -250,
	.num_channels = 1,
};

static const struct ad8366_info adrf5720_chip_info = {
	.name = "adrf5720",
	.gain_min = -31500,
	.gain_max = 0,
	.gain_step = -500,
	.num_channels = 1,
};

static const struct ad8366_info adrf5730_chip_info = {
	.name = "adrf5730",
	.gain_min = -31500,
	.gain_max = 0,
	.gain_step = -500,
	.num_channels = 1,
};

static const struct ad8366_info adrf5731_chip_info = {
	.name = "adrf5731",
	.gain_min = -30000,
	.gain_max = 0,
	.gain_step = -2000,
	.num_channels = 1,
	.pack_code = adrf5731_pack_code,
};

static const struct ad8366_info hmc271_chip_info = {
	.name = "hmc271a",
	.gain_min = -31000,
	.gain_max = 0,
	.gain_step = 1000,
	.num_channels = 1,
	.pack_code = hmc271_pack_code,
};

static const struct ad8366_info hmc792_chip_info = {
	.name = "hmc792a",
	.gain_min = -15750,
	.gain_max = 0,
	.gain_step = 250,
	.num_channels = 1,
};

static const struct ad8366_info hmc1018_chip_info = {
	.name = "hmc1018a",
	.gain_min = -31000,
	.gain_max = 0,
	.gain_step = 1000,
	.num_channels = 1,
};

static const struct ad8366_info hmc1019_chip_info = {
	.name = "hmc1019a",
	.gain_min = -15500,
	.gain_max = 0,
	.gain_step = 500,
	.num_channels = 1,
};

static const struct ad8366_info hmc1119_chip_info = {
	.name = "hmc1119",
	.gain_min = -31750,
	.gain_max = 0,
	.gain_step = -250,
	.num_channels = 1,
};

static int ad8366_write_code(struct ad8366_state *st)
{
	const struct ad8366_info *inf = st->info;
	size_t len = 1;

	if (inf->pack_code)
		len = inf->pack_code(st->ch, inf->num_channels, st->data);
	else
		st->data[0] = st->ch[0];

	return spi_write(st->spi, st->data, len);
}

static int ad8366_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad8366_state *st = iio_priv(indio_dev);
	const struct ad8366_info *inf = st->info;
	int ret;
	int code, gain = 0;

	mutex_lock(&st->lock);
	switch (m) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		code = st->ch[chan->channel];
		gain = inf->gain_step > 0 ? inf->gain_min : inf->gain_max;
		gain += inf->gain_step * code;
		/* Values in dB */
		*val = gain / 1000;
		*val2 = (gain % 1000) * 1000;

		ret = IIO_VAL_INT_PLUS_MICRO_DB;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&st->lock);

	return ret;
};

static int ad8366_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad8366_state *st = iio_priv(indio_dev);
	const struct ad8366_info *inf = st->info;
	int code = 0, gain;
	int ret;

	/* Values in dB */
	if (val < 0)
		gain = (val * 1000) - (val2 / 1000);
	else
		gain = (val * 1000) + (val2 / 1000);

	if (gain > inf->gain_max || gain < inf->gain_min)
		return -EINVAL;

	gain -= inf->gain_step > 0 ? inf->gain_min : inf->gain_max;
	code = DIV_ROUND_CLOSEST(gain, inf->gain_step);

	mutex_lock(&st->lock);
	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		st->ch[chan->channel] = code;
		ret = ad8366_write_code(st);
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&st->lock);

	return ret;
}

static int ad8366_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		return IIO_VAL_INT_PLUS_MICRO_DB;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ad8366_info = {
	.read_raw = &ad8366_read_raw,
	.write_raw = &ad8366_write_raw,
	.write_raw_get_fmt = &ad8366_write_raw_get_fmt,
};

#define AD8366_CHAN(_channel) {				\
	.type = IIO_VOLTAGE,				\
	.output = 1,					\
	.indexed = 1,					\
	.channel = _channel,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_HARDWAREGAIN),\
}

static const struct iio_chan_spec ad8366_channels[] = {
	AD8366_CHAN(0),
	AD8366_CHAN(1),
};

static int ad8366_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct gpio_desc *enable_gpio;
	struct reset_control *rstc;
	struct iio_dev *indio_dev;
	struct ad8366_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	ret = devm_regulator_get_enable(dev, "vcc");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulator\n");

	st->spi = spi;
	st->info = spi_get_device_match_data(spi);

	enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(enable_gpio))
		return dev_err_probe(dev, PTR_ERR(enable_gpio),
				     "Failed to get enable GPIO\n");

	rstc = devm_reset_control_get_optional_exclusive_deasserted(dev, NULL);
	if (IS_ERR(rstc))
		return dev_err_probe(dev, PTR_ERR(rstc),
				     "Failed to get reset controller\n");

	indio_dev->name = st->info->name;
	indio_dev->info = &ad8366_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad8366_channels;
	indio_dev->num_channels = st->info->num_channels;

	ret = ad8366_write_code(st);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to write initial gain\n");

	return devm_iio_device_register(dev, indio_dev);
}

static const struct spi_device_id ad8366_id[] = {
	{ "ad8366", (kernel_ulong_t)&ad8366_chip_info },
	{ "ada4961", (kernel_ulong_t)&ada4961_chip_info },
	{ "adl5240", (kernel_ulong_t)&adl5240_chip_info },
	{ "adrf5702", (kernel_ulong_t)&adrf5702_chip_info },
	{ "adrf5703", (kernel_ulong_t)&adrf5703_chip_info },
	{ "adrf5720", (kernel_ulong_t)&adrf5720_chip_info },
	{ "adrf5730", (kernel_ulong_t)&adrf5730_chip_info },
	{ "adrf5731", (kernel_ulong_t)&adrf5731_chip_info },
	{ "hmc271a", (kernel_ulong_t)&hmc271_chip_info },
	{ "hmc792a", (kernel_ulong_t)&hmc792_chip_info },
	{ "hmc1018a", (kernel_ulong_t)&hmc1018_chip_info },
	{ "hmc1019a", (kernel_ulong_t)&hmc1019_chip_info },
	{ "hmc1119", (kernel_ulong_t)&hmc1119_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad8366_id);

static const struct of_device_id ad8366_of_match[] = {
	{ .compatible = "adi,ad8366", .data = &ad8366_chip_info },
	{ .compatible = "adi,ada4961", .data = &ada4961_chip_info },
	{ .compatible = "adi,adl5240", .data = &adl5240_chip_info },
	{ .compatible = "adi,adrf5702", .data = &adrf5702_chip_info },
	{ .compatible = "adi,adrf5703", .data = &adrf5703_chip_info },
	{ .compatible = "adi,adrf5720", .data = &adrf5720_chip_info },
	{ .compatible = "adi,adrf5730", .data = &adrf5730_chip_info },
	{ .compatible = "adi,adrf5731", .data = &adrf5731_chip_info },
	{ .compatible = "adi,hmc271a", .data = &hmc271_chip_info },
	{ .compatible = "adi,hmc792a", .data = &hmc792_chip_info },
	{ .compatible = "adi,hmc1018a", .data = &hmc1018_chip_info },
	{ .compatible = "adi,hmc1019a", .data = &hmc1019_chip_info },
	{ .compatible = "adi,hmc1119", .data = &hmc1119_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad8366_of_match);

static struct spi_driver ad8366_driver = {
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= ad8366_of_match,
	},
	.probe		= ad8366_probe,
	.id_table	= ad8366_id,
};

module_spi_driver(ad8366_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD8366 and similar Gain Amplifiers");
MODULE_LICENSE("GPL v2");
