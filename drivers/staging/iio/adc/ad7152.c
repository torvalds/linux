/*
 * AD7152 capacitive sensor driver supporting AD7152/3
 *
 * Copyright 2010 Analog Devices Inc.
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

#include "../iio.h"
#include "../sysfs.h"

/*
 * TODO: Check compliance of calibscale and calibbias with abi (units)
 */
/*
 * AD7152 registers definition
 */

#define AD7152_STATUS              0
#define AD7152_STATUS_RDY1         (1 << 0)
#define AD7152_STATUS_RDY2         (1 << 1)
#define AD7152_CH1_DATA_HIGH       1
#define AD7152_CH2_DATA_HIGH       3
#define AD7152_CH1_OFFS_HIGH       5
#define AD7152_CH2_OFFS_HIGH       7
#define AD7152_CH1_GAIN_HIGH       9
#define AD7152_CH1_SETUP           11
#define AD7152_CH2_GAIN_HIGH       12
#define AD7152_CH2_SETUP           14
#define AD7152_CFG                 15
#define AD7152_RESEVERD            16
#define AD7152_CAPDAC_POS          17
#define AD7152_CAPDAC_NEG          18
#define AD7152_CFG2                26

#define AD7152_MAX_CONV_MODE       6

/*
 * struct ad7152_chip_info - chip specifc information
 */

struct ad7152_chip_info {
	struct i2c_client *client;
	u8  ch1_setup;
	u8  ch2_setup;
	/*
	 * Capacitive channel digital filter setup;
	 * conversion time/update rate setup per channel
	 */
	u8  filter_rate_setup;
	char *conversion_mode;
};

struct ad7152_conversion_mode {
	char *name;
	u8 reg_cfg;
};

static struct ad7152_conversion_mode
ad7152_conv_mode_table[AD7152_MAX_CONV_MODE] = {
	{ "idle", 0 },
	{ "continuous-conversion", 1 },
	{ "single-conversion", 2 },
	{ "power-down", 3 },
	{ "offset-calibration", 5 },
	{ "gain-calibration", 6 },
};

/*
 * sysfs nodes
 */

#define IIO_DEV_ATTR_AVAIL_CONVERSION_MODES(_show)			\
	IIO_DEVICE_ATTR(available_conversion_modes, S_IRUGO, _show, NULL, 0)
#define IIO_DEV_ATTR_CONVERSION_MODE(_mode, _show, _store)              \
	IIO_DEVICE_ATTR(conversion_mode, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CH1_SETUP(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(ch1_setup, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CH2_SETUP(_mode, _show, _store)              \
	IIO_DEVICE_ATTR(ch2_setup, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_FILTER_RATE_SETUP(_mode, _show, _store)              \
	IIO_DEVICE_ATTR(filter_rate_setup, _mode, _show, _store, 0)

static ssize_t ad7152_show_conversion_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int i;
	int len = 0;

	for (i = 0; i < AD7152_MAX_CONV_MODE; i++)
		len += sprintf(buf + len, "%s ",
			       ad7152_conv_mode_table[i].name);

	len += sprintf(buf + len, "\n");

	return len;
}

static IIO_DEV_ATTR_AVAIL_CONVERSION_MODES(ad7152_show_conversion_modes);

static ssize_t ad7152_show_conversion_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "%s\n", chip->conversion_mode);
}

static ssize_t ad7152_store_conversion_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = iio_priv(dev_info);
	u8 cfg;
	int i, ret;

	ret = i2c_smbus_read_byte_data(chip->client, AD7152_CFG);
	if (ret < 0)
		return ret;
	cfg = ret;

	for (i = 0; i < AD7152_MAX_CONV_MODE; i++)
		if (strncmp(buf, ad7152_conv_mode_table[i].name,
				strlen(ad7152_conv_mode_table[i].name) - 1) ==
		    0) {
			chip->conversion_mode = ad7152_conv_mode_table[i].name;
			cfg |= 0x18 | ad7152_conv_mode_table[i].reg_cfg;
			ret = i2c_smbus_write_byte_data(chip->client,
							AD7152_CFG, cfg);
			if (ret < 0)
				return ret;
			return len;
		}

	dev_err(dev, "not supported conversion mode\n");

	return -EINVAL;
}

static IIO_DEV_ATTR_CONVERSION_MODE(S_IRUGO | S_IWUSR,
		ad7152_show_conversion_mode,
		ad7152_store_conversion_mode);

static ssize_t ad7152_show_ch1_setup(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "0x%02x\n", chip->ch1_setup);
}

static ssize_t ad7152_store_ch1_setup(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = iio_priv(dev_info);
	u8 data;
	int ret;

	ret = kstrtou8(buf, 10, &data);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(chip->client, AD7152_CH1_SETUP, data);
	if (ret < 0)
		return ret;

	chip->ch1_setup = data;

	return len;
}

static IIO_DEV_ATTR_CH1_SETUP(S_IRUGO | S_IWUSR,
		ad7152_show_ch1_setup,
		ad7152_store_ch1_setup);

static ssize_t ad7152_show_ch2_setup(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "0x%02x\n", chip->ch2_setup);
}

static ssize_t ad7152_store_ch2_setup(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = iio_priv(dev_info);
	u8 data;
	int ret;

	ret = kstrtou8(buf, 10, &data);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(chip->client, AD7152_CH2_SETUP, data);
	if (ret < 0)
		return ret;

	chip->ch2_setup = data;

	return len;
}

static IIO_DEV_ATTR_CH2_SETUP(S_IRUGO | S_IWUSR,
		ad7152_show_ch2_setup,
		ad7152_store_ch2_setup);

static ssize_t ad7152_show_filter_rate_setup(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "0x%02x\n", chip->filter_rate_setup);
}

static ssize_t ad7152_store_filter_rate_setup(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = iio_priv(dev_info);
	u8 data;
	int ret;

	ret = kstrtou8(buf, 10, &data);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(chip->client, AD7152_CFG2, data);
	if (ret < 0)
		return ret;

	chip->filter_rate_setup = data;

	return len;
}

static IIO_DEV_ATTR_FILTER_RATE_SETUP(S_IRUGO | S_IWUSR,
		ad7152_show_filter_rate_setup,
		ad7152_store_filter_rate_setup);

static struct attribute *ad7152_attributes[] = {
	&iio_dev_attr_available_conversion_modes.dev_attr.attr,
	&iio_dev_attr_conversion_mode.dev_attr.attr,
	&iio_dev_attr_ch1_setup.dev_attr.attr,
	&iio_dev_attr_ch2_setup.dev_attr.attr,
	&iio_dev_attr_filter_rate_setup.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7152_attribute_group = {
	.attrs = ad7152_attributes,
};

static const u8 ad7152_addresses[][3] = {
	{ AD7152_CH1_DATA_HIGH, AD7152_CH1_OFFS_HIGH, AD7152_CH1_GAIN_HIGH },
	{ AD7152_CH2_DATA_HIGH, AD7152_CH2_OFFS_HIGH, AD7152_CH2_GAIN_HIGH },
};
static int ad7152_write_raw(struct iio_dev *dev_info,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad7152_chip_info *chip = iio_priv(dev_info);
	int ret;

	switch (mask) {
	case (1 << IIO_CHAN_INFO_CALIBSCALE_SEPARATE):
		if ((val < 0) | (val > 0xFFFF))
			return -EINVAL;
		ret = i2c_smbus_write_word_data(chip->client,
					ad7152_addresses[chan->channel][2],
					val);
		if (ret < 0)
			return ret;

		return 0;

	case (1 << IIO_CHAN_INFO_CALIBBIAS_SEPARATE):
		if ((val < 0) | (val > 0xFFFF))
			return -EINVAL;
		ret = i2c_smbus_write_word_data(chip->client,
					ad7152_addresses[chan->channel][1],
					val);
		if (ret < 0)
			return ret;

		return 0;

	default:
		return -EINVAL;
	}
}
static int ad7152_read_raw(struct iio_dev *dev_info,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2,
			   long mask)
{
	struct ad7152_chip_info *chip = iio_priv(dev_info);
	int ret;

	switch (mask) {
	case 0:
		ret = i2c_smbus_read_word_data(chip->client,
					ad7152_addresses[chan->channel][0]);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case (1 << IIO_CHAN_INFO_CALIBSCALE_SEPARATE):
		/* FIXME: Hmm. very small. it's 1+ 1/(2^16 *val) */
		ret = i2c_smbus_read_word_data(chip->client,
					ad7152_addresses[chan->channel][2]);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case (1 << IIO_CHAN_INFO_CALIBBIAS_SEPARATE):
		ret = i2c_smbus_read_word_data(chip->client,
					ad7152_addresses[chan->channel][1]);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	};
}
static const struct iio_info ad7152_info = {
	.attrs = &ad7152_attribute_group,
	.read_raw = &ad7152_read_raw,
	.write_raw = &ad7152_write_raw,
	.driver_module = THIS_MODULE,
};

static const struct iio_chan_spec ad7152_channels[] = {
	{
		.type = IIO_CAPACITANCE,
		.indexed = 1,
		.channel = 0,
		.info_mask = (1 << IIO_CHAN_INFO_CALIBSCALE_SEPARATE) |
		(1 << IIO_CHAN_INFO_CALIBBIAS_SEPARATE),
	}, {
		.type = IIO_CAPACITANCE,
		.indexed = 1,
		.channel = 1,
		.info_mask = (1 << IIO_CHAN_INFO_CALIBSCALE_SEPARATE) |
		(1 << IIO_CHAN_INFO_CALIBBIAS_SEPARATE),
	}
};
/*
 * device probe and remove
 */

static int __devinit ad7152_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret = 0;
	struct ad7152_chip_info *chip;
	struct iio_dev *indio_dev;

	indio_dev = iio_allocate_device(sizeof(*chip));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	chip = iio_priv(indio_dev);
	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, indio_dev);

	chip->client = client;

	/* Echipabilish that the iio_dev is a child of the i2c device */
	indio_dev->name = id->name;
	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &ad7152_info;
	indio_dev->channels = ad7152_channels;
	if (id->driver_data == 0)
		indio_dev->num_channels = ARRAY_SIZE(ad7152_channels);
	else
		indio_dev->num_channels = 1;
	indio_dev->num_channels = ARRAY_SIZE(ad7152_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_dev;

	dev_err(&client->dev, "%s capacitive sensor registered\n", id->name);

	return 0;

error_free_dev:
	iio_free_device(indio_dev);
error_ret:
	return ret;
}

static int __devexit ad7152_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);

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
		.name = "ad7152",
	},
	.probe = ad7152_probe,
	.remove = __devexit_p(ad7152_remove),
	.id_table = ad7152_id,
};

static __init int ad7152_init(void)
{
	return i2c_add_driver(&ad7152_driver);
}

static __exit void ad7152_exit(void)
{
	i2c_del_driver(&ad7152_driver);
}

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ad7152/3 capacitive sensor driver");
MODULE_LICENSE("GPL v2");

module_init(ad7152_init);
module_exit(ad7152_exit);
