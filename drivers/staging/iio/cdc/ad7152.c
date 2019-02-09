/*
 * AD7152 capacitive sensor driver supporting AD7152/3
 *
 * Copyright 2010-2011a Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/*
 * TODO: Check compliance of calibbias with abi (units)
 */
/*
 * AD7152 registers definition
 */

#define AD7152_REG_STATUS		0
#define AD7152_REG_CH1_DATA_HIGH	1
#define AD7152_REG_CH2_DATA_HIGH	3
#define AD7152_REG_CH1_OFFS_HIGH	5
#define AD7152_REG_CH2_OFFS_HIGH	7
#define AD7152_REG_CH1_GAIN_HIGH	9
#define AD7152_REG_CH1_SETUP		11
#define AD7152_REG_CH2_GAIN_HIGH	12
#define AD7152_REG_CH2_SETUP		14
#define AD7152_REG_CFG			15
#define AD7152_REG_RESEVERD		16
#define AD7152_REG_CAPDAC_POS		17
#define AD7152_REG_CAPDAC_NEG		18
#define AD7152_REG_CFG2			26

/* Status Register Bit Designations (AD7152_REG_STATUS) */
#define AD7152_STATUS_RDY1		BIT(0)
#define AD7152_STATUS_RDY2		BIT(1)
#define AD7152_STATUS_C1C2		BIT(2)
#define AD7152_STATUS_PWDN		BIT(7)

/* Setup Register Bit Designations (AD7152_REG_CHx_SETUP) */
#define AD7152_SETUP_CAPDIFF		BIT(5)
#define AD7152_SETUP_RANGE_2pF		(0 << 6)
#define AD7152_SETUP_RANGE_0_5pF	(1 << 6)
#define AD7152_SETUP_RANGE_1pF		(2 << 6)
#define AD7152_SETUP_RANGE_4pF		(3 << 6)
#define AD7152_SETUP_RANGE(x)		((x) << 6)

/* Config Register Bit Designations (AD7152_REG_CFG) */
#define AD7152_CONF_CH2EN		BIT(3)
#define AD7152_CONF_CH1EN		BIT(4)
#define AD7152_CONF_MODE_IDLE		(0 << 0)
#define AD7152_CONF_MODE_CONT_CONV	(1 << 0)
#define AD7152_CONF_MODE_SINGLE_CONV	(2 << 0)
#define AD7152_CONF_MODE_OFFS_CAL	(5 << 0)
#define AD7152_CONF_MODE_GAIN_CAL	(6 << 0)

/* Capdac Register Bit Designations (AD7152_REG_CAPDAC_XXX) */
#define AD7152_CAPDAC_DACEN		BIT(7)
#define AD7152_CAPDAC_DACP(x)		((x) & 0x1F)

/* CFG2 Register Bit Designations (AD7152_REG_CFG2) */
#define AD7152_CFG2_OSR(x)		(((x) & 0x3) << 4)

enum {
	AD7152_DATA,
	AD7152_OFFS,
	AD7152_GAIN,
	AD7152_SETUP
};

/*
 * struct ad7152_chip_info - chip specific information
 */

struct ad7152_chip_info {
	struct i2c_client *client;
	/*
	 * Capacitive channel digital filter setup;
	 * conversion time/update rate setup per channel
	 */
	u8	filter_rate_setup;
	u8	setup[2];
	struct mutex state_lock;	/* protect hardware state */
};

static inline ssize_t ad7152_start_calib(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t len,
					 u8 regval)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7152_chip_info *chip = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	bool doit;
	int ret, timeout = 10;

	ret = strtobool(buf, &doit);
	if (ret < 0)
		return ret;

	if (!doit)
		return 0;

	if (this_attr->address == 0)
		regval |= AD7152_CONF_CH1EN;
	else
		regval |= AD7152_CONF_CH2EN;

	mutex_lock(&chip->state_lock);
	ret = i2c_smbus_write_byte_data(chip->client, AD7152_REG_CFG, regval);
	if (ret < 0)
		goto unlock;

	do {
		mdelay(20);
		ret = i2c_smbus_read_byte_data(chip->client, AD7152_REG_CFG);
		if (ret < 0)
			goto unlock;

	} while ((ret == regval) && timeout--);

	mutex_unlock(&chip->state_lock);
	return len;

unlock:
	mutex_unlock(&chip->state_lock);
	return ret;
}

static ssize_t ad7152_start_offset_calib(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t len)
{
	return ad7152_start_calib(dev, attr, buf, len,
				  AD7152_CONF_MODE_OFFS_CAL);
}

static ssize_t ad7152_start_gain_calib(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t len)
{
	return ad7152_start_calib(dev, attr, buf, len,
				  AD7152_CONF_MODE_GAIN_CAL);
}

static IIO_DEVICE_ATTR(in_capacitance0_calibbias_calibration,
		       0200, NULL, ad7152_start_offset_calib, 0);
static IIO_DEVICE_ATTR(in_capacitance1_calibbias_calibration,
		       0200, NULL, ad7152_start_offset_calib, 1);
static IIO_DEVICE_ATTR(in_capacitance0_calibscale_calibration,
		       0200, NULL, ad7152_start_gain_calib, 0);
static IIO_DEVICE_ATTR(in_capacitance1_calibscale_calibration,
		       0200, NULL, ad7152_start_gain_calib, 1);

/* Values are Update Rate (Hz), Conversion Time (ms) + 1*/
static const unsigned char ad7152_filter_rate_table[][2] = {
	{200, 5 + 1}, {50, 20 + 1}, {20, 50 + 1}, {17, 60 + 1},
};

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("200 50 20 17");

static IIO_CONST_ATTR(in_capacitance_scale_available,
		      "0.000061050 0.000030525 0.000015263 0.000007631");

static struct attribute *ad7152_attributes[] = {
	&iio_dev_attr_in_capacitance0_calibbias_calibration.dev_attr.attr,
	&iio_dev_attr_in_capacitance1_calibbias_calibration.dev_attr.attr,
	&iio_dev_attr_in_capacitance0_calibscale_calibration.dev_attr.attr,
	&iio_dev_attr_in_capacitance1_calibscale_calibration.dev_attr.attr,
	&iio_const_attr_in_capacitance_scale_available.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7152_attribute_group = {
	.attrs = ad7152_attributes,
};

static const u8 ad7152_addresses[][4] = {
	{ AD7152_REG_CH1_DATA_HIGH, AD7152_REG_CH1_OFFS_HIGH,
	  AD7152_REG_CH1_GAIN_HIGH, AD7152_REG_CH1_SETUP },
	{ AD7152_REG_CH2_DATA_HIGH, AD7152_REG_CH2_OFFS_HIGH,
	  AD7152_REG_CH2_GAIN_HIGH, AD7152_REG_CH2_SETUP },
};

/* Values are nano relative to pf base. */
static const int ad7152_scale_table[] = {
	30525, 7631, 15263, 61050
};

/**
 * read_raw handler for IIO_CHAN_INFO_SAMP_FREQ
 *
 * lock must be held
 **/
static int ad7152_read_raw_samp_freq(struct device *dev, int *val)
{
	struct ad7152_chip_info *chip = iio_priv(dev_to_iio_dev(dev));

	*val = ad7152_filter_rate_table[chip->filter_rate_setup][0];

	return 0;
}

/**
 * write_raw handler for IIO_CHAN_INFO_SAMP_FREQ
 *
 * lock must be held
 **/
static int ad7152_write_raw_samp_freq(struct device *dev, int val)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7152_chip_info *chip = iio_priv(indio_dev);
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(ad7152_filter_rate_table); i++)
		if (val >= ad7152_filter_rate_table[i][0])
			break;

	if (i >= ARRAY_SIZE(ad7152_filter_rate_table))
		i = ARRAY_SIZE(ad7152_filter_rate_table) - 1;

	ret = i2c_smbus_write_byte_data(chip->client,
					AD7152_REG_CFG2, AD7152_CFG2_OSR(i));
	if (ret < 0)
		return ret;

	chip->filter_rate_setup = i;

	return ret;
}

static int ad7152_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad7152_chip_info *chip = iio_priv(indio_dev);
	int ret, i;

	mutex_lock(&chip->state_lock);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		if (val != 1) {
			ret = -EINVAL;
			goto out;
		}

		val = (val2 * 1024) / 15625;

		ret = i2c_smbus_write_word_data(chip->client,
				ad7152_addresses[chan->channel][AD7152_GAIN],
				swab16(val));
		if (ret < 0)
			goto out;

		ret = 0;
		break;

	case IIO_CHAN_INFO_CALIBBIAS:
		if ((val < 0) | (val > 0xFFFF)) {
			ret = -EINVAL;
			goto out;
		}
		ret = i2c_smbus_write_word_data(chip->client,
				ad7152_addresses[chan->channel][AD7152_OFFS],
				swab16(val));
		if (ret < 0)
			goto out;

		ret = 0;
		break;
	case IIO_CHAN_INFO_SCALE:
		if (val) {
			ret = -EINVAL;
			goto out;
		}
		for (i = 0; i < ARRAY_SIZE(ad7152_scale_table); i++)
			if (val2 == ad7152_scale_table[i])
				break;

		chip->setup[chan->channel] &= ~AD7152_SETUP_RANGE_4pF;
		chip->setup[chan->channel] |= AD7152_SETUP_RANGE(i);

		ret = i2c_smbus_write_byte_data(chip->client,
				ad7152_addresses[chan->channel][AD7152_SETUP],
				chip->setup[chan->channel]);
		if (ret < 0)
			goto out;

		ret = 0;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val2) {
			ret = -EINVAL;
			goto out;
		}
		ret = ad7152_write_raw_samp_freq(&indio_dev->dev, val);
		if (ret < 0)
			goto out;

		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&chip->state_lock);
	return ret;
}

static int ad7152_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2,
			   long mask)
{
	struct ad7152_chip_info *chip = iio_priv(indio_dev);
	int ret;
	u8 regval = 0;

	mutex_lock(&chip->state_lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/* First set whether in differential mode */

		regval = chip->setup[chan->channel];

		if (chan->differential)
			chip->setup[chan->channel] |= AD7152_SETUP_CAPDIFF;
		else
			chip->setup[chan->channel] &= ~AD7152_SETUP_CAPDIFF;

		if (regval != chip->setup[chan->channel]) {
			ret = i2c_smbus_write_byte_data(chip->client,
				ad7152_addresses[chan->channel][AD7152_SETUP],
				chip->setup[chan->channel]);
			if (ret < 0)
				goto out;
		}
		/* Make sure the channel is enabled */
		if (chan->channel == 0)
			regval = AD7152_CONF_CH1EN;
		else
			regval = AD7152_CONF_CH2EN;

		/* Trigger a single read */
		regval |= AD7152_CONF_MODE_SINGLE_CONV;
		ret = i2c_smbus_write_byte_data(chip->client, AD7152_REG_CFG,
				regval);
		if (ret < 0)
			goto out;

		msleep(ad7152_filter_rate_table[chip->filter_rate_setup][1]);
		/* Now read the actual register */
		ret = i2c_smbus_read_word_data(chip->client,
				ad7152_addresses[chan->channel][AD7152_DATA]);
		if (ret < 0)
			goto out;
		*val = swab16(ret);

		if (chan->differential)
			*val -= 0x8000;

		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_CALIBSCALE:

		ret = i2c_smbus_read_word_data(chip->client,
				ad7152_addresses[chan->channel][AD7152_GAIN]);
		if (ret < 0)
			goto out;
		/* 1 + gain_val / 2^16 */
		*val = 1;
		*val2 = (15625 * swab16(ret)) / 1024;

		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = i2c_smbus_read_word_data(chip->client,
				ad7152_addresses[chan->channel][AD7152_OFFS]);
		if (ret < 0)
			goto out;
		*val = swab16(ret);

		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		ret = i2c_smbus_read_byte_data(chip->client,
				ad7152_addresses[chan->channel][AD7152_SETUP]);
		if (ret < 0)
			goto out;
		*val = 0;
		*val2 = ad7152_scale_table[ret >> 6];

		ret = IIO_VAL_INT_PLUS_NANO;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = ad7152_read_raw_samp_freq(&indio_dev->dev, val);
		if (ret < 0)
			goto out;

		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
	}
out:
	mutex_unlock(&chip->state_lock);
	return ret;
}

static int ad7152_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}
}

static const struct iio_info ad7152_info = {
	.attrs = &ad7152_attribute_group,
	.read_raw = ad7152_read_raw,
	.write_raw = ad7152_write_raw,
	.write_raw_get_fmt = ad7152_write_raw_get_fmt,
};

static const struct iio_chan_spec ad7152_channels[] = {
	{
		.type = IIO_CAPACITANCE,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) |
		BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	}, {
		.type = IIO_CAPACITANCE,
		.differential = 1,
		.indexed = 1,
		.channel = 0,
		.channel2 = 2,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) |
		BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	}, {
		.type = IIO_CAPACITANCE,
		.indexed = 1,
		.channel = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) |
		BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	}, {
		.type = IIO_CAPACITANCE,
		.differential = 1,
		.indexed = 1,
		.channel = 1,
		.channel2 = 3,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) |
		BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	}
};

/*
 * device probe and remove
 */

static int ad7152_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret = 0;
	struct ad7152_chip_info *chip;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;
	chip = iio_priv(indio_dev);
	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, indio_dev);

	chip->client = client;
	mutex_init(&chip->state_lock);

	/* Establish that the iio_dev is a child of the i2c device */
	indio_dev->name = id->name;
	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &ad7152_info;
	indio_dev->channels = ad7152_channels;
	if (id->driver_data == 0)
		indio_dev->num_channels = ARRAY_SIZE(ad7152_channels);
	else
		indio_dev->num_channels = 2;
	indio_dev->num_channels = ARRAY_SIZE(ad7152_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_iio_device_register(indio_dev->dev.parent, indio_dev);
	if (ret)
		return ret;

	dev_err(&client->dev, "%s capacitive sensor registered\n", id->name);

	return 0;
}

static const struct i2c_device_id ad7152_id[] = {
	{ "ad7152", 0 },
	{ "ad7153", 1 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ad7152_id);

static struct i2c_driver ad7152_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
	},
	.probe = ad7152_probe,
	.id_table = ad7152_id,
};
module_i2c_driver(ad7152_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices AD7152/3 capacitive sensor driver");
MODULE_LICENSE("GPL v2");
