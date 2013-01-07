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
#define AD7152_STATUS_RDY1		(1 << 0)
#define AD7152_STATUS_RDY2		(1 << 1)
#define AD7152_STATUS_C1C2		(1 << 2)
#define AD7152_STATUS_PWDN		(1 << 7)

/* Setup Register Bit Designations (AD7152_REG_CHx_SETUP) */
#define AD7152_SETUP_CAPDIFF		(1 << 5)
#define AD7152_SETUP_RANGE_2pF		(0 << 6)
#define AD7152_SETUP_RANGE_0_5pF	(1 << 6)
#define AD7152_SETUP_RANGE_1pF		(2 << 6)
#define AD7152_SETUP_RANGE_4pF		(3 << 6)
#define AD7152_SETUP_RANGE(x)		((x) << 6)

/* Config Register Bit Designations (AD7152_REG_CFG) */
#define AD7152_CONF_CH2EN		(1 << 3)
#define AD7152_CONF_CH1EN		(1 << 4)
#define AD7152_CONF_MODE_IDLE		(0 << 0)
#define AD7152_CONF_MODE_CONT_CONV	(1 << 0)
#define AD7152_CONF_MODE_SINGLE_CONV	(2 << 0)
#define AD7152_CONF_MODE_OFFS_CAL	(5 << 0)
#define AD7152_CONF_MODE_GAIN_CAL	(6 << 0)

/* Capdac Register Bit Designations (AD7152_REG_CAPDAC_XXX) */
#define AD7152_CAPDAC_DACEN		(1 << 7)
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
 * struct ad7152_chip_info - chip specifc information
 */

struct ad7152_chip_info {
	struct i2c_client *client;
	/*
	 * Capacitive channel digital filter setup;
	 * conversion time/update rate setup per channel
	 */
	u8	filter_rate_setup;
	u8	setup[2];
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

	mutex_lock(&indio_dev->mlock);
	ret = i2c_smbus_write_byte_data(chip->client, AD7152_REG_CFG, regval);
	if (ret < 0) {
		mutex_unlock(&indio_dev->mlock);
		return ret;
	}

	do {
		mdelay(20);
		ret = i2c_smbus_read_byte_data(chip->client, AD7152_REG_CFG);
		if (ret < 0) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}
	} while ((ret == regval) && timeout--);

	mutex_unlock(&indio_dev->mlock);
	return len;
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
		       S_IWUSR, NULL, ad7152_start_offset_calib, 0);
static IIO_DEVICE_ATTR(in_capacitance1_calibbias_calibration,
		       S_IWUSR, NULL, ad7152_start_offset_calib, 1);
static IIO_DEVICE_ATTR(in_capacitance0_calibscale_calibration,
		       S_IWUSR, NULL, ad7152_start_gain_calib, 0);
static IIO_DEVICE_ATTR(in_capacitance1_calibscale_calibration,
		       S_IWUSR, NULL, ad7152_start_gain_calib, 1);

/* Values are Update Rate (Hz), Conversion Time (ms) + 1*/
static const unsigned char ad7152_filter_rate_table[][2] = {
	{200, 5 + 1}, {50, 20 + 1}, {20, 50 + 1}, {17, 60 + 1},
};

static ssize_t ad7152_show_filter_rate_setup(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7152_chip_info *chip = iio_priv(indio_dev);

	return sprintf(buf, "%d\n",
		       ad7152_filter_rate_table[chip->filter_rate_setup][0]);
}

static ssize_t ad7152_store_filter_rate_setup(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7152_chip_info *chip = iio_priv(indio_dev);
	u8 data;
	int ret, i;

	ret = kstrtou8(buf, 10, &data);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(ad7152_filter_rate_table); i++)
		if (data >= ad7152_filter_rate_table[i][0])
			break;

	if (i >= ARRAY_SIZE(ad7152_filter_rate_table))
		i = ARRAY_SIZE(ad7152_filter_rate_table) - 1;

	mutex_lock(&indio_dev->mlock);
	ret = i2c_smbus_write_byte_data(chip->client,
			AD7152_REG_CFG2, AD7152_CFG2_OSR(i));
	if (ret < 0) {
		mutex_unlock(&indio_dev->mlock);
		return ret;
	}

	chip->filter_rate_setup = i;
	mutex_unlock(&indio_dev->mlock);

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ(S_IRUGO | S_IWUSR,
		ad7152_show_filter_rate_setup,
		ad7152_store_filter_rate_setup);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("200 50 20 17");

static IIO_CONST_ATTR(in_capacitance_scale_available,
		      "0.000061050 0.000030525 0.000015263 0.000007631");

static struct attribute *ad7152_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
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

static int ad7152_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad7152_chip_info *chip = iio_priv(indio_dev);
	int ret, i;

	mutex_lock(&indio_dev->mlock);

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
		if (val != 0) {
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
	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&indio_dev->mlock);
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

	mutex_lock(&indio_dev->mlock);

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
	default:
		ret = -EINVAL;
	}
out:
	mutex_unlock(&indio_dev->mlock);
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
	.read_raw = &ad7152_read_raw,
	.write_raw = &ad7152_write_raw,
	.write_raw_get_fmt = &ad7152_write_raw_get_fmt,
	.driver_module = THIS_MODULE,
};

static const struct iio_chan_spec ad7152_channels[] = {
	{
		.type = IIO_CAPACITANCE,
		.indexed = 1,
		.channel = 0,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBSCALE_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
	}, {
		.type = IIO_CAPACITANCE,
		.differential = 1,
		.indexed = 1,
		.channel = 0,
		.channel2 = 2,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBSCALE_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
	}, {
		.type = IIO_CAPACITANCE,
		.indexed = 1,
		.channel = 1,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBSCALE_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
	}, {
		.type = IIO_CAPACITANCE,
		.differential = 1,
		.indexed = 1,
		.channel = 1,
		.channel2 = 3,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBSCALE_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
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

	indio_dev = iio_device_alloc(sizeof(*chip));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	chip = iio_priv(indio_dev);
	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, indio_dev);

	chip->client = client;

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

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_dev;

	dev_err(&client->dev, "%s capacitive sensor registered\n", id->name);

	return 0;

error_free_dev:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

static int ad7152_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_device_free(indio_dev);

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
	.remove = ad7152_remove,
	.id_table = ad7152_id,
};
module_i2c_driver(ad7152_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices AD7152/3 capacitive sensor driver");
MODULE_LICENSE("GPL v2");
