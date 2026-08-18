// SPDX-License-Identifier: GPL-2.0
/*
 * AD5686R, AD5685R, AD5684R Digital to analog converters  driver
 *
 * Copyright 2011 Analog Devices Inc.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/wordpart.h>

#include <linux/iio/iio.h>

#include "ad5686.h"

static const char * const ad5686_powerdown_modes[] = {
	"1kohm_to_gnd",
	"100kohm_to_gnd",
	"three_state"
};

static int ad5310_control_sync(struct ad5686_state *st)
{
	unsigned int pd_val = st->pwr_down_mask & st->pwr_down_mode;

	return ad5686_write(st, AD5686_CMD_CONTROL_REG, 0,
			    FIELD_PREP(AD5310_PD_MSK, pd_val & AD5686_PD_MSK) |
			    FIELD_PREP(AD5310_REF_BIT_MSK, st->use_internal_vref ? 0 : 1));
}

static int ad5683_control_sync(struct ad5686_state *st)
{
	unsigned int pd_val = st->pwr_down_mask & st->pwr_down_mode;

	return ad5686_write(st, AD5686_CMD_CONTROL_REG, 0,
			    FIELD_PREP(AD5683_PD_MSK, pd_val & AD5686_PD_MSK) |
			    FIELD_PREP(AD5683_REF_BIT_MSK, st->use_internal_vref ? 0 : 1));
}

static inline unsigned int ad5686_pd_mask_shift(const struct iio_chan_spec *chan)
{
	if (chan->channel == chan->address)
		return chan->channel * 2;

	/* one-hot encoding is used in dual/quad channel devices */
	return __ffs(chan->address) * 2;
}

static inline void ad5686_pd_field_set(const struct iio_chan_spec *chan,
				       unsigned int *pd, unsigned int val)
{
	unsigned int shift = ad5686_pd_mask_shift(chan);

	*pd = (*pd & ~(AD5686_PD_MSK << shift)) | ((val & AD5686_PD_MSK) << shift);
}

static inline unsigned int ad5686_pd_field_get(const struct iio_chan_spec *chan,
					       unsigned int pd)
{
	unsigned int shift = ad5686_pd_mask_shift(chan);

	return (pd >> shift) & AD5686_PD_MSK;
}

static int ad5686_get_powerdown_mode(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan)
{
	struct ad5686_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);

	return ad5686_pd_field_get(chan, st->pwr_down_mode) - 1;
}

static int ad5686_set_powerdown_mode(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     unsigned int mode)
{
	struct ad5686_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);
	ad5686_pd_field_set(chan, &st->pwr_down_mode, mode + 1);

	return 0;
}

static const struct iio_enum ad5686_powerdown_mode_enum = {
	.items = ad5686_powerdown_modes,
	.num_items = ARRAY_SIZE(ad5686_powerdown_modes),
	.get = ad5686_get_powerdown_mode,
	.set = ad5686_set_powerdown_mode,
};

static ssize_t ad5686_read_dac_powerdown(struct iio_dev *indio_dev,
		uintptr_t private, const struct iio_chan_spec *chan, char *buf)
{
	struct ad5686_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);

	return sysfs_emit(buf, "%d\n",
			  !!ad5686_pd_field_get(chan, st->pwr_down_mask));
}

static ssize_t ad5686_write_dac_powerdown(struct iio_dev *indio_dev,
					  uintptr_t private,
					  const struct iio_chan_spec *chan,
					  const char *buf,
					  size_t len)
{
	bool readin;
	int ret;
	struct ad5686_state *st = iio_priv(indio_dev);
	unsigned int val;
	u8 address;

	ret = kstrtobool(buf, &readin);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);

	if (readin)
		ad5686_pd_field_set(chan, &st->pwr_down_mask, AD5686_PD_MSK_PWR_DOWN);
	else
		ad5686_pd_field_set(chan, &st->pwr_down_mask, AD5686_PD_MSK_PWR_UP);

	switch (st->chip_info->regmap_type) {
	case AD5310_REGMAP:
		ret = ad5310_control_sync(st);
		if (ret)
			return ret;
		break;
	case AD5683_REGMAP:
		ret = ad5683_control_sync(st);
		if (ret)
			return ret;
		break;
	case AD5686_REGMAP:
		/* AD5674R/AD5679R have 16 channels and 2 powerdown registers */
		val = st->pwr_down_mask & st->pwr_down_mode;
		if (chan->channel > 0x7) {
			address = 0x8;
			val = upper_16_bits(val);
		} else {
			address = 0x0;
			val = lower_16_bits(val);
		}
		ret = ad5686_write(st, AD5686_CMD_POWERDOWN_DAC, address, val);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return len;
}

static int ad5686_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5686_state *st = iio_priv(indio_dev);
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		ret = ad5686_read(st, chan->address);
		mutex_unlock(&st->lock);
		if (ret < 0)
			return ret;
		*val = (ret >> chan->scan_type.shift) &
			GENMASK(chan->scan_type.realbits - 1, 0);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_mv;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	}
	return -EINVAL;
}

static int ad5686_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad5686_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val >= (1 << chan->scan_type.realbits) || val < 0)
			return -EINVAL;

		mutex_lock(&st->lock);
		ret = ad5686_write(st, AD5686_CMD_WRITE_INPUT_N_UPDATE_N,
				   chan->address, val << chan->scan_type.shift);
		mutex_unlock(&st->lock);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info ad5686_info = {
	.read_raw = ad5686_read_raw,
	.write_raw = ad5686_write_raw,
};

static const struct iio_chan_spec_ext_info ad5686_ext_info[] = {
	{
		.name = "powerdown",
		.read = ad5686_read_dac_powerdown,
		.write = ad5686_write_dac_powerdown,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE, &ad5686_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE, &ad5686_powerdown_mode_enum),
	{ }
};

#define AD5868_CHANNEL(chan, addr, bits, _shift) {		\
		.type = IIO_VOLTAGE,				\
		.indexed = 1,					\
		.output = 1,					\
		.channel = chan,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),\
		.address = addr,				\
		.scan_type = {					\
			.sign = 'u',				\
			.realbits = (bits),			\
			.storagebits = 16,			\
			.shift = (_shift),			\
		},						\
		.ext_info = ad5686_ext_info,			\
}

#define DECLARE_AD5683_CHANNELS(name, bits, _shift)		\
static const struct iio_chan_spec name[] = {			\
		AD5868_CHANNEL(0, 0, bits, _shift),		\
}

#define DECLARE_AD5338_CHANNELS(name, bits, _shift)		\
static const struct iio_chan_spec name[] = {			\
		AD5868_CHANNEL(0, 1, bits, _shift),		\
		AD5868_CHANNEL(1, 8, bits, _shift),		\
}

#define DECLARE_AD5686_CHANNELS(name, bits, _shift)		\
static const struct iio_chan_spec name[] = {			\
		AD5868_CHANNEL(0, 1, bits, _shift),		\
		AD5868_CHANNEL(1, 2, bits, _shift),		\
		AD5868_CHANNEL(2, 4, bits, _shift),		\
		AD5868_CHANNEL(3, 8, bits, _shift),		\
}

#define DECLARE_AD5676_CHANNELS(name, bits, _shift)		\
static const struct iio_chan_spec name[] = {			\
		AD5868_CHANNEL(0, 0, bits, _shift),		\
		AD5868_CHANNEL(1, 1, bits, _shift),		\
		AD5868_CHANNEL(2, 2, bits, _shift),		\
		AD5868_CHANNEL(3, 3, bits, _shift),		\
		AD5868_CHANNEL(4, 4, bits, _shift),		\
		AD5868_CHANNEL(5, 5, bits, _shift),		\
		AD5868_CHANNEL(6, 6, bits, _shift),		\
		AD5868_CHANNEL(7, 7, bits, _shift),		\
}

#define DECLARE_AD5679_CHANNELS(name, bits, _shift)		\
static const struct iio_chan_spec name[] = {			\
		AD5868_CHANNEL(0, 0, bits, _shift),		\
		AD5868_CHANNEL(1, 1, bits, _shift),		\
		AD5868_CHANNEL(2, 2, bits, _shift),		\
		AD5868_CHANNEL(3, 3, bits, _shift),		\
		AD5868_CHANNEL(4, 4, bits, _shift),		\
		AD5868_CHANNEL(5, 5, bits, _shift),		\
		AD5868_CHANNEL(6, 6, bits, _shift),		\
		AD5868_CHANNEL(7, 7, bits, _shift),		\
		AD5868_CHANNEL(8, 8, bits, _shift),		\
		AD5868_CHANNEL(9, 9, bits, _shift),		\
		AD5868_CHANNEL(10, 10, bits, _shift),		\
		AD5868_CHANNEL(11, 11, bits, _shift),		\
		AD5868_CHANNEL(12, 12, bits, _shift),		\
		AD5868_CHANNEL(13, 13, bits, _shift),		\
		AD5868_CHANNEL(14, 14, bits, _shift),		\
		AD5868_CHANNEL(15, 15, bits, _shift),		\
}

/* single-channel */
DECLARE_AD5683_CHANNELS(ad5310r_channels, 10, 2);
DECLARE_AD5683_CHANNELS(ad5311r_channels, 10, 6);
DECLARE_AD5683_CHANNELS(ad5681r_channels, 12, 4);
DECLARE_AD5683_CHANNELS(ad5682r_channels, 14, 2);
DECLARE_AD5683_CHANNELS(ad5683r_channels, 16, 0);

/* dual-channel */
DECLARE_AD5338_CHANNELS(ad5337r_channels, 8, 8);
DECLARE_AD5338_CHANNELS(ad5338r_channels, 10, 6);

/* quad-channel */
DECLARE_AD5686_CHANNELS(ad5684r_channels, 12, 4);
DECLARE_AD5686_CHANNELS(ad5685r_channels, 14, 2);
DECLARE_AD5686_CHANNELS(ad5686r_channels, 16, 0);

/* 8-channel */
DECLARE_AD5676_CHANNELS(ad5672r_channels, 12, 4);
DECLARE_AD5676_CHANNELS(ad5676r_channels, 16, 0);

/* 16-channel */
DECLARE_AD5679_CHANNELS(ad5674r_channels, 12, 4);
DECLARE_AD5679_CHANNELS(ad5679r_channels, 16, 0);

const struct ad5686_chip_info ad5310r_chip_info = {
	.channels = ad5310r_channels,
	.int_vref_mv = 2500,
	.num_channels = 1,
	.regmap_type = AD5310_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5310r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5311r_chip_info = {
	.channels = ad5311r_channels,
	.int_vref_mv = 2500,
	.num_channels = 1,
	.regmap_type = AD5683_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5311r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5681r_chip_info = {
	.channels = ad5681r_channels,
	.int_vref_mv = 2500,
	.num_channels = 1,
	.regmap_type = AD5683_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5681r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5682r_chip_info = {
	.channels = ad5682r_channels,
	.int_vref_mv = 2500,
	.num_channels = 1,
	.regmap_type = AD5683_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5682r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5683_chip_info = {
	.channels = ad5683r_channels,
	.num_channels = 1,
	.regmap_type = AD5683_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5683_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5683r_chip_info = {
	.channels = ad5683r_channels,
	.int_vref_mv = 2500,
	.num_channels = 1,
	.regmap_type = AD5683_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5683r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5337r_chip_info = {
	.channels = ad5337r_channels,
	.int_vref_mv = 2500,
	.num_channels = 2,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5337r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5338r_chip_info = {
	.channels = ad5338r_channels,
	.int_vref_mv = 2500,
	.num_channels = 2,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5338r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5684_chip_info = {
	.channels = ad5684r_channels,
	.num_channels = 4,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5684_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5684r_chip_info = {
	.channels = ad5684r_channels,
	.int_vref_mv = 2500,
	.num_channels = 4,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5684r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5685r_chip_info = {
	.channels = ad5685r_channels,
	.int_vref_mv = 2500,
	.num_channels = 4,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5685r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5686_chip_info = {
	.channels = ad5686r_channels,
	.num_channels = 4,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5686_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5686r_chip_info = {
	.channels = ad5686r_channels,
	.int_vref_mv = 2500,
	.num_channels = 4,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5686r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5672r_chip_info = {
	.channels = ad5672r_channels,
	.int_vref_mv = 2500,
	.num_channels = 8,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5672r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5676_chip_info = {
	.channels = ad5676r_channels,
	.num_channels = 8,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5676_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5676r_chip_info = {
	.channels = ad5676r_channels,
	.int_vref_mv = 2500,
	.num_channels = 8,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5676r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5674r_chip_info = {
	.channels = ad5674r_channels,
	.int_vref_mv = 2500,
	.num_channels = 16,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5674r_chip_info, "IIO_AD5686");

const struct ad5686_chip_info ad5679r_chip_info = {
	.channels = ad5679r_channels,
	.int_vref_mv = 2500,
	.num_channels = 16,
	.regmap_type = AD5686_REGMAP,
};
EXPORT_SYMBOL_NS_GPL(ad5679r_chip_info, "IIO_AD5686");

int ad5686_probe(struct device *dev,
		 const struct ad5686_chip_info *chip_info,
		 const char *name, const struct ad5686_bus_ops *ops)
{
	struct ad5686_state *st;
	struct iio_dev *indio_dev;
	int ret, i;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (indio_dev == NULL)
		return  -ENOMEM;

	st = iio_priv(indio_dev);

	st->dev = dev;
	st->ops = ops;
	st->chip_info = chip_info;

	ret = devm_regulator_get_enable_read_voltage(dev, "vcc");
	if (ret < 0 && ret != -ENODEV)
		return ret;

	st->use_internal_vref = ret == -ENODEV;
	st->vref_mv = st->use_internal_vref ? st->chip_info->int_vref_mv : ret / 1000;

	/* Initialize masks to all ones */
	st->pwr_down_mask = ~0;
	st->pwr_down_mode = ~0;

	/* Set all the power down mode for all channels to 1K pulldown */
	for (i = 0; i < st->chip_info->num_channels; i++) {
		ad5686_pd_field_set(&st->chip_info->channels[i],
				    &st->pwr_down_mask, AD5686_PD_MSK_PWR_UP);
		ad5686_pd_field_set(&st->chip_info->channels[i],
				    &st->pwr_down_mode, AD5686_PD_MODE_1K_TO_GND);
	}

	indio_dev->name = name;
	indio_dev->info = &ad5686_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	switch (st->chip_info->regmap_type) {
	case AD5310_REGMAP:
		ret = ad5310_control_sync(st);
		if (ret)
			return ret;
		break;
	case AD5683_REGMAP:
		ret = ad5683_control_sync(st);
		if (ret)
			return ret;
		break;
	case AD5686_REGMAP:
		ret = ad5686_write(st, AD5686_CMD_INTERNAL_REFER_SETUP, 0,
				   st->use_internal_vref ? 0 : AD5686_REF_BIT_MSK);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS_GPL(ad5686_probe, "IIO_AD5686");

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5686/85/84 DAC");
MODULE_LICENSE("GPL v2");
