// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022 Analog Devices, Inc.
 * ADI MAX77541 ADC Driver with IIO interface
 */

#include <linux/bitfield.h>
#include <linux/iio/iio.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/units.h>

#include <linux/mfd/max77541.h>

enum max77541_adc_range {
	LOW_RANGE,
	MID_RANGE,
	HIGH_RANGE,
};

enum max77541_adc_channel {
	MAX77541_ADC_VSYS_V,
	MAX77541_ADC_VOUT1_V,
	MAX77541_ADC_VOUT2_V,
	MAX77541_ADC_TEMP,
};

static int max77541_adc_offset(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2)
{
	switch (chan->channel) {
	case MAX77541_ADC_TEMP:
		*val = DIV_ROUND_CLOSEST(ABSOLUTE_ZERO_MILLICELSIUS, 1725);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int max77541_adc_scale(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2)
{
	struct regmap **regmap = iio_priv(indio_dev);
	unsigned int reg_val;
	int ret;

	switch (chan->channel) {
	case MAX77541_ADC_VSYS_V:
		*val = 25;
		return IIO_VAL_INT;
	case MAX77541_ADC_VOUT1_V:
	case MAX77541_ADC_VOUT2_V:
		ret = regmap_read(*regmap, MAX77541_REG_M2_CFG1, &reg_val);
		if (ret)
			return ret;

		reg_val = FIELD_GET(MAX77541_BITS_MX_CFG1_RNG, reg_val);
		switch (reg_val) {
		case LOW_RANGE:
			*val = 6;
			*val2 = 250000;
			break;
		case MID_RANGE:
			*val = 12;
			*val2 = 500000;
			break;
		case HIGH_RANGE:
			*val = 25;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}

		return IIO_VAL_INT_PLUS_MICRO;
	case MAX77541_ADC_TEMP:
		*val = 1725;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int max77541_adc_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val)
{
	struct regmap **regmap = iio_priv(indio_dev);
	int ret;

	ret = regmap_read(*regmap, chan->address, val);
	if (ret)
		return ret;

	return IIO_VAL_INT;
}

#define MAX77541_ADC_CHANNEL_V(_channel, _name, _type, _reg) \
	{							\
		.type = _type,					\
		.indexed = 1,					\
		.channel = _channel,				\
		.address = _reg,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
				      BIT(IIO_CHAN_INFO_SCALE), \
		.datasheet_name = _name,			\
	}

#define MAX77541_ADC_CHANNEL_TEMP(_channel, _name, _type, _reg) \
	{							\
		.type = _type,					\
		.indexed = 1,					\
		.channel = _channel,				\
		.address = _reg,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
				      BIT(IIO_CHAN_INFO_SCALE) |\
				      BIT(IIO_CHAN_INFO_OFFSET),\
		.datasheet_name = _name,			\
	}

static const struct iio_chan_spec max77541_adc_channels[] = {
	MAX77541_ADC_CHANNEL_V(MAX77541_ADC_VSYS_V, "vsys_v", IIO_VOLTAGE,
			       MAX77541_REG_ADC_DATA_CH1),
	MAX77541_ADC_CHANNEL_V(MAX77541_ADC_VOUT1_V, "vout1_v", IIO_VOLTAGE,
			       MAX77541_REG_ADC_DATA_CH2),
	MAX77541_ADC_CHANNEL_V(MAX77541_ADC_VOUT2_V, "vout2_v", IIO_VOLTAGE,
			       MAX77541_REG_ADC_DATA_CH3),
	MAX77541_ADC_CHANNEL_TEMP(MAX77541_ADC_TEMP, "temp", IIO_TEMP,
				  MAX77541_REG_ADC_DATA_CH6),
};

static int max77541_adc_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int *val, int *val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_OFFSET:
		return max77541_adc_offset(indio_dev, chan, val, val2);
	case IIO_CHAN_INFO_SCALE:
		return max77541_adc_scale(indio_dev, chan, val, val2);
	case IIO_CHAN_INFO_RAW:
		return max77541_adc_raw(indio_dev, chan, val);
	default:
		return -EINVAL;
	}
}

static const struct iio_info max77541_adc_info = {
	.read_raw = max77541_adc_read_raw,
};

static int max77541_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct regmap **regmap;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*regmap));
	if (!indio_dev)
		return -ENOMEM;

	regmap = iio_priv(indio_dev);

	*regmap = dev_get_regmap(dev->parent, NULL);
	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->name = "max77541";
	indio_dev->info = &max77541_adc_info;
	indio_dev->channels = max77541_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(max77541_adc_channels);

	return devm_iio_device_register(dev, indio_dev);
}

static const struct platform_device_id max77541_adc_platform_id[] = {
	{ "max77541-adc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, max77541_adc_platform_id);

static struct platform_driver max77541_adc_driver = {
	.driver = {
		.name = "max77541-adc",
	},
	.probe = max77541_adc_probe,
	.id_table = max77541_adc_platform_id,
};
module_platform_driver(max77541_adc_driver);

MODULE_AUTHOR("Okan Sahin <Okan.Sahin@analog.com>");
MODULE_DESCRIPTION("MAX77541 ADC driver");
MODULE_LICENSE("GPL");
