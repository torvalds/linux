// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD7191 ADC driver
 *
 * Copyright 2025 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/units.h>

#include <linux/iio/adc/ad_sigma_delta.h>
#include <linux/iio/iio.h>

#define ad_sigma_delta_to_ad7191(sigmad)	\
	container_of((sigmad), struct ad7191_state, sd)

#define AD7191_TEMP_CODES_PER_DEGREE	2815

#define AD7191_CHAN_MASK		BIT(0)
#define AD7191_TEMP_MASK		BIT(1)

enum ad7191_channel {
	AD7191_CH_AIN1_AIN2,
	AD7191_CH_AIN3_AIN4,
	AD7191_CH_TEMP,
};

/*
 * NOTE:
 * The AD7191 features a dual-use data out ready DOUT/RDY output.
 * In order to avoid contentions on the SPI bus, it's therefore necessary
 * to use SPI bus locking.
 *
 * The DOUT/RDY output must also be wired to an interrupt-capable GPIO.
 *
 * The SPI controller's chip select must be connected to the PDOWN pin
 * of the ADC. When CS (PDOWN) is high, it powers down the device and
 * resets the internal circuitry.
 */

struct ad7191_state {
	struct ad_sigma_delta		sd;
	struct mutex			lock; /* Protect device state */

	struct gpio_descs		*odr_gpios;
	struct gpio_descs		*pga_gpios;
	struct gpio_desc		*temp_gpio;
	struct gpio_desc		*chan_gpio;

	u16				int_vref_mv;
	const u32			(*scale_avail)[2];
	size_t				scale_avail_size;
	u32				scale_index;
	const u32			*samp_freq_avail;
	size_t				samp_freq_avail_size;
	u32				samp_freq_index;

	struct clk			*mclk;
};

static int ad7191_set_channel(struct ad_sigma_delta *sd, unsigned int address)
{
	struct ad7191_state *st = ad_sigma_delta_to_ad7191(sd);
	u8 temp_gpio_val, chan_gpio_val;

	if (!FIELD_FIT(AD7191_CHAN_MASK | AD7191_TEMP_MASK, address))
		return -EINVAL;

	chan_gpio_val = FIELD_GET(AD7191_CHAN_MASK, address);
	temp_gpio_val = FIELD_GET(AD7191_TEMP_MASK, address);

	gpiod_set_value(st->chan_gpio, chan_gpio_val);
	gpiod_set_value(st->temp_gpio, temp_gpio_val);

	return 0;
}

static int ad7191_set_cs(struct ad_sigma_delta *sigma_delta, int assert)
{
	struct spi_transfer t = {
		.len = 0,
		.cs_change = assert,
	};
	struct spi_message m;

	spi_message_init_with_transfers(&m, &t, 1);

	return spi_sync_locked(sigma_delta->spi, &m);
}

static int ad7191_set_mode(struct ad_sigma_delta *sd,
			   enum ad_sigma_delta_mode mode)
{
	struct ad7191_state *st = ad_sigma_delta_to_ad7191(sd);

	switch (mode) {
	case AD_SD_MODE_CONTINUOUS:
	case AD_SD_MODE_SINGLE:
		return ad7191_set_cs(&st->sd, 1);
	case AD_SD_MODE_IDLE:
		return ad7191_set_cs(&st->sd, 0);
	default:
		return -EINVAL;
	}
}

static const struct ad_sigma_delta_info ad7191_sigma_delta_info = {
	.set_channel = ad7191_set_channel,
	.set_mode = ad7191_set_mode,
	.has_registers = false,
};

static int ad7191_init_regulators(struct iio_dev *indio_dev)
{
	struct ad7191_state *st = iio_priv(indio_dev);
	struct device *dev = &st->sd.spi->dev;
	int ret;

	ret = devm_regulator_get_enable(dev, "avdd");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable specified AVdd supply\n");

	ret = devm_regulator_get_enable(dev, "dvdd");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable specified DVdd supply\n");

	ret = devm_regulator_get_enable_read_voltage(dev, "vref");
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get Vref voltage\n");

	st->int_vref_mv = ret / 1000;

	return 0;
}

static int ad7191_config_setup(struct iio_dev *indio_dev)
{
	struct ad7191_state *st = iio_priv(indio_dev);
	struct device *dev = &st->sd.spi->dev;
	/* Sampling frequencies in Hz, see Table 5 */
	static const u32 samp_freq[4] = { 120, 60, 50, 10 };
	/* Gain options, see Table 7 */
	const u32 gain[4] = { 1, 8, 64, 128 };
	static u32 scale_buffer[4][2];
	int odr_value, odr_index = 0, pga_value, pga_index = 0, i, ret;
	u64 scale_uv;

	st->samp_freq_index = 0;
	st->scale_index = 0;

	ret = device_property_read_u32(dev, "adi,odr-value", &odr_value);
	if (ret && ret != -EINVAL)
		return dev_err_probe(dev, ret, "Failed to get odr value.\n");

	if (ret == -EINVAL) {
		st->odr_gpios = devm_gpiod_get_array(dev, "odr", GPIOD_OUT_LOW);
		if (IS_ERR(st->odr_gpios))
			return dev_err_probe(dev, PTR_ERR(st->odr_gpios),
					     "Failed to get odr gpios.\n");

		if (st->odr_gpios->ndescs != 2)
			return dev_err_probe(dev, -EINVAL, "Expected 2 odr gpio pins.\n");

		st->samp_freq_avail = samp_freq;
		st->samp_freq_avail_size = ARRAY_SIZE(samp_freq);
	} else {
		for (i = 0; i < ARRAY_SIZE(samp_freq); i++) {
			if (odr_value != samp_freq[i])
				continue;
			odr_index = i;
			break;
		}

		st->samp_freq_avail = &samp_freq[odr_index];
		st->samp_freq_avail_size = 1;

		st->odr_gpios = NULL;
	}

	mutex_lock(&st->lock);

	for (i = 0; i < ARRAY_SIZE(scale_buffer); i++) {
		scale_uv = ((u64)st->int_vref_mv * NANO) >>
			(indio_dev->channels[0].scan_type.realbits - 1);
		do_div(scale_uv, gain[i]);
		scale_buffer[i][1] = do_div(scale_uv, NANO);
		scale_buffer[i][0] = scale_uv;
	}

	mutex_unlock(&st->lock);

	ret = device_property_read_u32(dev, "adi,pga-value", &pga_value);
	if (ret && ret != -EINVAL)
		return dev_err_probe(dev, ret, "Failed to get pga value.\n");

	if (ret == -EINVAL) {
		st->pga_gpios = devm_gpiod_get_array(dev, "pga", GPIOD_OUT_LOW);
		if (IS_ERR(st->pga_gpios))
			return dev_err_probe(dev, PTR_ERR(st->pga_gpios),
					     "Failed to get pga gpios.\n");

		if (st->pga_gpios->ndescs != 2)
			return dev_err_probe(dev, -EINVAL, "Expected 2 pga gpio pins.\n");

		st->scale_avail = scale_buffer;
		st->scale_avail_size = ARRAY_SIZE(scale_buffer);
	} else {
		for (i = 0; i < ARRAY_SIZE(gain); i++) {
			if (pga_value != gain[i])
				continue;
			pga_index = i;
			break;
		}

		st->scale_avail = &scale_buffer[pga_index];
		st->scale_avail_size = 1;

		st->pga_gpios = NULL;
	}

	st->temp_gpio = devm_gpiod_get(dev, "temp", GPIOD_OUT_LOW);
	if (IS_ERR(st->temp_gpio))
		return dev_err_probe(dev, PTR_ERR(st->temp_gpio),
				     "Failed to get temp gpio.\n");

	st->chan_gpio = devm_gpiod_get(dev, "chan", GPIOD_OUT_LOW);
	if (IS_ERR(st->chan_gpio))
		return dev_err_probe(dev, PTR_ERR(st->chan_gpio),
				     "Failed to get chan gpio.\n");

	return 0;
}

static int ad7191_clock_setup(struct ad7191_state *st)
{
	struct device *dev = &st->sd.spi->dev;

	st->mclk = devm_clk_get_optional_enabled(dev, "mclk");
	if (IS_ERR(st->mclk))
		return dev_err_probe(dev, PTR_ERR(st->mclk),
				     "Failed to get mclk.\n");

	return 0;
}

static int ad7191_setup(struct iio_dev *indio_dev)
{
	struct ad7191_state *st = iio_priv(indio_dev);
	int ret;

	ret = ad7191_init_regulators(indio_dev);
	if (ret)
		return ret;

	ret = ad7191_config_setup(indio_dev);
	if (ret)
		return ret;

	return ad7191_clock_setup(st);
}

static int ad7191_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long m)
{
	struct ad7191_state *st = iio_priv(indio_dev);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		return ad_sigma_delta_single_conversion(indio_dev, chan, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE: {
			guard(mutex)(&st->lock);
			*val = st->scale_avail[st->scale_index][0];
			*val2 = st->scale_avail[st->scale_index][1];
			return IIO_VAL_INT_PLUS_NANO;
		}
		case IIO_TEMP:
			*val = 0;
			*val2 = NANO / AD7191_TEMP_CODES_PER_DEGREE;
			return IIO_VAL_INT_PLUS_NANO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		*val = -(1 << (chan->scan_type.realbits - 1));
		switch (chan->type) {
		case IIO_VOLTAGE:
			return IIO_VAL_INT;
		case IIO_TEMP:
			*val -= 273 * AD7191_TEMP_CODES_PER_DEGREE;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->samp_freq_avail[st->samp_freq_index];
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad7191_set_gain(struct ad7191_state *st, int gain_index)
{
	DECLARE_BITMAP(bitmap, 2) = { };

	st->scale_index = gain_index;

	bitmap_write(bitmap, gain_index, 0, 2);

	return gpiod_multi_set_value_cansleep(st->pga_gpios, bitmap);
}

static int ad7191_set_samp_freq(struct ad7191_state *st, int samp_freq_index)
{
	DECLARE_BITMAP(bitmap, 2) = {};

	st->samp_freq_index = samp_freq_index;

	bitmap_write(bitmap, samp_freq_index, 0, 2);

	return gpiod_multi_set_value_cansleep(st->odr_gpios, bitmap);
}

static int __ad7191_write_raw(struct ad7191_state *st,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	int i;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE: {
		if (!st->pga_gpios)
			return -EPERM;
		guard(mutex)(&st->lock);
		for (i = 0; i < st->scale_avail_size; i++) {
			if (val2 == st->scale_avail[i][1])
				return ad7191_set_gain(st, i);
		}
		return -EINVAL;
	}
	case IIO_CHAN_INFO_SAMP_FREQ: {
		if (!st->odr_gpios)
			return -EPERM;
		guard(mutex)(&st->lock);
		for (i = 0; i < st->samp_freq_avail_size; i++) {
			if (val == st->samp_freq_avail[i])
				return ad7191_set_samp_freq(st, i);
		}
		return -EINVAL;
	}
	default:
		return -EINVAL;
	}
}

static int ad7191_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val, int val2,
			    long mask)
{
	struct ad7191_state *st = iio_priv(indio_dev);
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = __ad7191_write_raw(st, chan, val, val2, mask);

	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad7191_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad7191_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, const int **vals,
			     int *type, int *length, long mask)
{
	struct ad7191_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*vals = (int *)st->scale_avail;
		*type = IIO_VAL_INT_PLUS_NANO;
		*length = st->scale_avail_size * 2;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (int *)st->samp_freq_avail;
		*type = IIO_VAL_INT;
		*length = st->samp_freq_avail_size;
		return IIO_AVAIL_LIST;
	}

	return -EINVAL;
}

static const struct iio_info ad7191_info = {
	.read_raw = ad7191_read_raw,
	.write_raw = ad7191_write_raw,
	.write_raw_get_fmt = ad7191_write_raw_get_fmt,
	.read_avail = ad7191_read_avail,
	.validate_trigger = ad_sd_validate_trigger,
};

static const struct iio_chan_spec ad7191_channels[] = {
	{
		.type = IIO_TEMP,
		.address = AD7191_CH_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.scan_type = {
			.sign = 'u',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_BE,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.differential = 1,
		.indexed = 1,
		.channel = 1,
		.channel2 = 2,
		.address = AD7191_CH_AIN1_AIN2,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 1,
		.scan_type = {
			.sign = 'u',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_BE,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.differential = 1,
		.indexed = 1,
		.channel = 3,
		.channel2 = 4,
		.address = AD7191_CH_AIN3_AIN4,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 2,
		.scan_type = {
			.sign = 'u',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_BE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static int ad7191_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ad7191_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	indio_dev->name = "ad7191";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad7191_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad7191_channels);
	indio_dev->info = &ad7191_info;

	ret = ad_sd_init(&st->sd, indio_dev, spi, &ad7191_sigma_delta_info);
	if (ret)
		return ret;

	ret = devm_ad_sd_setup_buffer_and_trigger(dev, indio_dev);
	if (ret)
		return ret;

	ret = ad7191_setup(indio_dev);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ad7191_of_match[] = {
	{ .compatible = "adi,ad7191", },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7191_of_match);

static const struct spi_device_id ad7191_id_table[] = {
	{ "ad7191" },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad7191_id_table);

static struct spi_driver ad7191_driver = {
	.driver = {
		.name = "ad7191",
		.of_match_table = ad7191_of_match,
	},
	.probe = ad7191_probe,
	.id_table = ad7191_id_table,
};
module_spi_driver(ad7191_driver);

MODULE_AUTHOR("Alisa-Dariana Roman <alisa.roman@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7191 ADC");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_AD_SIGMA_DELTA");
