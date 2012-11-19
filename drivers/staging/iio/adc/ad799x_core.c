/*
 * iio/adc/ad799x.c
 * Copyright (C) 2010-1011 Michael Hennerich, Analog Devices Inc.
 *
 * based on iio/adc/max1363
 * Copyright (C) 2008-2010 Jonathan Cameron
 *
 * based on linux/drivers/i2c/chips/max123x
 * Copyright (C) 2002-2004 Stefan Eletzhofer
 *
 * based on linux/drivers/acron/char/pcf8583.c
 * Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * ad799x.c
 *
 * Support for ad7991, ad7995, ad7999, ad7992, ad7993, ad7994, ad7997,
 * ad7998 and similar chips.
 *
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>

#include "ad799x.h"

/*
 * ad799x register access by I2C
 */
static int ad799x_i2c_read16(struct ad799x_state *st, u8 reg, u16 *data)
{
	struct i2c_client *client = st->client;
	int ret = 0;

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "I2C read error\n");
		return ret;
	}

	*data = swab16((u16)ret);

	return 0;
}

static int ad799x_i2c_read8(struct ad799x_state *st, u8 reg, u8 *data)
{
	struct i2c_client *client = st->client;
	int ret = 0;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "I2C read error\n");
		return ret;
	}

	*data = (u8)ret;

	return 0;
}

static int ad799x_i2c_write16(struct ad799x_state *st, u8 reg, u16 data)
{
	struct i2c_client *client = st->client;
	int ret = 0;

	ret = i2c_smbus_write_word_data(client, reg, swab16(data));
	if (ret < 0)
		dev_err(&client->dev, "I2C write error\n");

	return ret;
}

static int ad799x_i2c_write8(struct ad799x_state *st, u8 reg, u8 data)
{
	struct i2c_client *client = st->client;
	int ret = 0;

	ret = i2c_smbus_write_byte_data(client, reg, data);
	if (ret < 0)
		dev_err(&client->dev, "I2C write error\n");

	return ret;
}

static int ad7997_8_update_scan_mode(struct iio_dev *indio_dev,
	const unsigned long *scan_mask)
{
	struct ad799x_state *st = iio_priv(indio_dev);

	switch (st->id) {
	case ad7997:
	case ad7998:
		return ad799x_i2c_write16(st, AD7998_CONF_REG,
			st->config | (*scan_mask << AD799X_CHANNEL_SHIFT));
	default:
		break;
	}

	return 0;
}

static int ad799x_scan_direct(struct ad799x_state *st, unsigned ch)
{
	u16 rxbuf;
	u8 cmd;
	int ret;

	switch (st->id) {
	case ad7991:
	case ad7995:
	case ad7999:
		cmd = st->config | ((1 << ch) << AD799X_CHANNEL_SHIFT);
		break;
	case ad7992:
	case ad7993:
	case ad7994:
		cmd = (1 << ch) << AD799X_CHANNEL_SHIFT;
		break;
	case ad7997:
	case ad7998:
		cmd = (ch << AD799X_CHANNEL_SHIFT) | AD7997_8_READ_SINGLE;
		break;
	default:
		return -EINVAL;
	}

	ret = ad799x_i2c_read16(st, cmd, &rxbuf);
	if (ret < 0)
		return ret;

	return rxbuf;
}

static int ad799x_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret;
	struct ad799x_state *st = iio_priv(indio_dev);
	unsigned int scale_uv;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		if (iio_buffer_enabled(indio_dev))
			ret = -EBUSY;
		else
			ret = ad799x_scan_direct(st, chan->scan_index);
		mutex_unlock(&indio_dev->mlock);

		if (ret < 0)
			return ret;
		*val = (ret >> chan->scan_type.shift) &
			RES_MASK(chan->scan_type.realbits);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		scale_uv = (st->int_vref_mv * 1000) >> chan->scan_type.realbits;
		*val =  scale_uv / 1000;
		*val2 = (scale_uv % 1000) * 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}
static const unsigned int ad7998_frequencies[] = {
	[AD7998_CYC_DIS]	= 0,
	[AD7998_CYC_TCONF_32]	= 15625,
	[AD7998_CYC_TCONF_64]	= 7812,
	[AD7998_CYC_TCONF_128]	= 3906,
	[AD7998_CYC_TCONF_512]	= 976,
	[AD7998_CYC_TCONF_1024]	= 488,
	[AD7998_CYC_TCONF_2048]	= 244,
};
static ssize_t ad799x_read_frequency(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad799x_state *st = iio_priv(indio_dev);

	int ret;
	u8 val;
	ret = ad799x_i2c_read8(st, AD7998_CYCLE_TMR_REG, &val);
	if (ret)
		return ret;

	val &= AD7998_CYC_MASK;

	return sprintf(buf, "%u\n", ad7998_frequencies[val]);
}

static ssize_t ad799x_write_frequency(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad799x_state *st = iio_priv(indio_dev);

	long val;
	int ret, i;
	u8 t;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);
	ret = ad799x_i2c_read8(st, AD7998_CYCLE_TMR_REG, &t);
	if (ret)
		goto error_ret_mutex;
	/* Wipe the bits clean */
	t &= ~AD7998_CYC_MASK;

	for (i = 0; i < ARRAY_SIZE(ad7998_frequencies); i++)
		if (val == ad7998_frequencies[i])
			break;
	if (i == ARRAY_SIZE(ad7998_frequencies)) {
		ret = -EINVAL;
		goto error_ret_mutex;
	}
	t |= i;
	ret = ad799x_i2c_write8(st, AD7998_CYCLE_TMR_REG, t);

error_ret_mutex:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static int ad799x_read_event_config(struct iio_dev *indio_dev,
				    u64 event_code)
{
	return 1;
}

static const u8 ad799x_threshold_addresses[][2] = {
	{ AD7998_DATALOW_CH1_REG, AD7998_DATAHIGH_CH1_REG },
	{ AD7998_DATALOW_CH2_REG, AD7998_DATAHIGH_CH2_REG },
	{ AD7998_DATALOW_CH3_REG, AD7998_DATAHIGH_CH3_REG },
	{ AD7998_DATALOW_CH4_REG, AD7998_DATAHIGH_CH4_REG },
};

static int ad799x_write_event_value(struct iio_dev *indio_dev,
				    u64 event_code,
				    int val)
{
	int ret;
	struct ad799x_state *st = iio_priv(indio_dev);
	int direction = !!(IIO_EVENT_CODE_EXTRACT_DIR(event_code) ==
			   IIO_EV_DIR_FALLING);
	int number = IIO_EVENT_CODE_EXTRACT_CHAN(event_code);

	mutex_lock(&indio_dev->mlock);
	ret = ad799x_i2c_write16(st,
				 ad799x_threshold_addresses[number][direction],
				 val);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int ad799x_read_event_value(struct iio_dev *indio_dev,
				    u64 event_code,
				    int *val)
{
	int ret;
	struct ad799x_state *st = iio_priv(indio_dev);
	int direction = !!(IIO_EVENT_CODE_EXTRACT_DIR(event_code) ==
			   IIO_EV_DIR_FALLING);
	int number = IIO_EVENT_CODE_EXTRACT_CHAN(event_code);
	u16 valin;

	mutex_lock(&indio_dev->mlock);
	ret = ad799x_i2c_read16(st,
				ad799x_threshold_addresses[number][direction],
				&valin);
	mutex_unlock(&indio_dev->mlock);
	if (ret < 0)
		return ret;
	*val = valin;

	return 0;
}

static ssize_t ad799x_read_channel_config(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad799x_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	int ret;
	u16 val;
	ret = ad799x_i2c_read16(st, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", val);
}

static ssize_t ad799x_write_channel_config(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad799x_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	long val;
	int ret;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);
	ret = ad799x_i2c_write16(st, this_attr->address, val);
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static irqreturn_t ad799x_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ad799x_state *st = iio_priv(private);
	u8 status;
	int i, ret;

	ret = ad799x_i2c_read8(st, AD7998_ALERT_STAT_REG, &status);
	if (ret)
		goto done;

	if (!status)
		goto done;

	ad799x_i2c_write8(st, AD7998_ALERT_STAT_REG, AD7998_ALERT_STAT_CLEAR);

	for (i = 0; i < 8; i++) {
		if (status & (1 << i))
			iio_push_event(indio_dev,
				       i & 0x1 ?
				       IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE,
							    (i >> 1),
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_RISING) :
				       IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE,
							    (i >> 1),
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_FALLING),
				       iio_get_time_ns());
	}

done:
	return IRQ_HANDLED;
}

static IIO_DEVICE_ATTR(in_voltage0_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad799x_read_channel_config,
		       ad799x_write_channel_config,
		       AD7998_HYST_CH1_REG);

static IIO_DEVICE_ATTR(in_voltage1_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad799x_read_channel_config,
		       ad799x_write_channel_config,
		       AD7998_HYST_CH2_REG);

static IIO_DEVICE_ATTR(in_voltage2_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad799x_read_channel_config,
		       ad799x_write_channel_config,
		       AD7998_HYST_CH3_REG);

static IIO_DEVICE_ATTR(in_voltage3_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad799x_read_channel_config,
		       ad799x_write_channel_config,
		       AD7998_HYST_CH4_REG);

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
			      ad799x_read_frequency,
			      ad799x_write_frequency);
static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("15625 7812 3906 1953 976 488 244 0");

static struct attribute *ad7993_4_7_8_event_attributes[] = {
	&iio_dev_attr_in_voltage0_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage1_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage2_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage3_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static struct attribute_group ad7993_4_7_8_event_attrs_group = {
	.attrs = ad7993_4_7_8_event_attributes,
	.name = "events",
};

static struct attribute *ad7992_event_attributes[] = {
	&iio_dev_attr_in_voltage0_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage1_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static struct attribute_group ad7992_event_attrs_group = {
	.attrs = ad7992_event_attributes,
	.name = "events",
};

static const struct iio_info ad7991_info = {
	.read_raw = &ad799x_read_raw,
	.driver_module = THIS_MODULE,
};

static const struct iio_info ad7992_info = {
	.read_raw = &ad799x_read_raw,
	.event_attrs = &ad7992_event_attrs_group,
	.read_event_config = &ad799x_read_event_config,
	.read_event_value = &ad799x_read_event_value,
	.write_event_value = &ad799x_write_event_value,
	.driver_module = THIS_MODULE,
};

static const struct iio_info ad7993_4_7_8_info = {
	.read_raw = &ad799x_read_raw,
	.event_attrs = &ad7993_4_7_8_event_attrs_group,
	.read_event_config = &ad799x_read_event_config,
	.read_event_value = &ad799x_read_event_value,
	.write_event_value = &ad799x_write_event_value,
	.driver_module = THIS_MODULE,
	.update_scan_mode = ad7997_8_update_scan_mode,
};

#define AD799X_EV_MASK (IIO_EV_BIT(IIO_EV_TYPE_THRESH, IIO_EV_DIR_RISING) | \
			IIO_EV_BIT(IIO_EV_TYPE_THRESH, IIO_EV_DIR_FALLING))

static const struct ad799x_chip_info ad799x_chip_info_tbl[] = {
	[ad7991] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 0,
				.scan_type = IIO_ST('u', 12, 16, 0),
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 1,
				.scan_type = IIO_ST('u', 12, 16, 0),
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 2,
				.scan_type = IIO_ST('u', 12, 16, 0),
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 3,
				.scan_type = IIO_ST('u', 12, 16, 0),
			},
			[4] = IIO_CHAN_SOFT_TIMESTAMP(4),
		},
		.num_channels = 5,
		.int_vref_mv = 4096,
		.info = &ad7991_info,
	},
	[ad7995] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 0,
				.scan_type = IIO_ST('u', 10, 16, 2),
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 1,
				.scan_type = IIO_ST('u', 10, 16, 2),
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 2,
				.scan_type = IIO_ST('u', 10, 16, 2),
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 3,
				.scan_type = IIO_ST('u', 10, 16, 2),
			},
			[4] = IIO_CHAN_SOFT_TIMESTAMP(4),
		},
		.num_channels = 5,
		.int_vref_mv = 1024,
		.info = &ad7991_info,
	},
	[ad7999] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 0,
				.scan_type = IIO_ST('u', 8, 16, 4),
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 1,
				.scan_type = IIO_ST('u', 8, 16, 4),
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 2,
				.scan_type = IIO_ST('u', 8, 16, 4),
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 3,
				.scan_type = IIO_ST('u', 8, 16, 4),
			},
			[4] = IIO_CHAN_SOFT_TIMESTAMP(4),
		},
		.num_channels = 5,
		.int_vref_mv = 1024,
		.info = &ad7991_info,
	},
	[ad7992] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 0,
				.scan_type = IIO_ST('u', 12, 16, 0),
				.event_mask = AD799X_EV_MASK,
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 1,
				.scan_type = IIO_ST('u', 12, 16, 0),
				.event_mask = AD799X_EV_MASK,
			},
			[2] = IIO_CHAN_SOFT_TIMESTAMP(2),
		},
		.num_channels = 3,
		.int_vref_mv = 4096,
		.default_config = AD7998_ALERT_EN,
		.info = &ad7992_info,
	},
	[ad7993] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 0,
				.scan_type = IIO_ST('u', 10, 16, 2),
				.event_mask = AD799X_EV_MASK,
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.scan_index = 1,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_type = IIO_ST('u', 10, 16, 2),
				.event_mask = AD799X_EV_MASK,
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 2,
				.scan_type = IIO_ST('u', 10, 16, 2),
				.event_mask = AD799X_EV_MASK,
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 3,
				.scan_type = IIO_ST('u', 10, 16, 2),
				.event_mask = AD799X_EV_MASK,
			},
			[4] = IIO_CHAN_SOFT_TIMESTAMP(4),
		},
		.num_channels = 5,
		.int_vref_mv = 1024,
		.default_config = AD7998_ALERT_EN,
		.info = &ad7993_4_7_8_info,
	},
	[ad7994] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 0,
				.scan_type = IIO_ST('u', 12, 16, 0),
				.event_mask = AD799X_EV_MASK,
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 1,
				.scan_type = IIO_ST('u', 12, 16, 0),
				.event_mask = AD799X_EV_MASK,
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 2,
				.scan_type = IIO_ST('u', 12, 16, 0),
				.event_mask = AD799X_EV_MASK,
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 3,
				.scan_type = IIO_ST('u', 12, 16, 0),
				.event_mask = AD799X_EV_MASK,
			},
			[4] = IIO_CHAN_SOFT_TIMESTAMP(4),
		},
		.num_channels = 5,
		.int_vref_mv = 4096,
		.default_config = AD7998_ALERT_EN,
		.info = &ad7993_4_7_8_info,
	},
	[ad7997] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 0,
				.scan_type = IIO_ST('u', 10, 16, 2),
				.event_mask = AD799X_EV_MASK,
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 1,
				.scan_type = IIO_ST('u', 10, 16, 2),
				.event_mask = AD799X_EV_MASK,
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 2,
				.scan_type = IIO_ST('u', 10, 16, 2),
				.event_mask = AD799X_EV_MASK,
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 3,
				.scan_type = IIO_ST('u', 10, 16, 2),
				.event_mask = AD799X_EV_MASK,
			},
			[4] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 4,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 4,
				.scan_type = IIO_ST('u', 10, 16, 2),
			},
			[5] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 5,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 5,
				.scan_type = IIO_ST('u', 10, 16, 2),
			},
			[6] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 6,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 6,
				.scan_type = IIO_ST('u', 10, 16, 2),
			},
			[7] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 7,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 7,
				.scan_type = IIO_ST('u', 10, 16, 2),
			},
			[8] = IIO_CHAN_SOFT_TIMESTAMP(8),
		},
		.num_channels = 9,
		.int_vref_mv = 1024,
		.default_config = AD7998_ALERT_EN,
		.info = &ad7993_4_7_8_info,
	},
	[ad7998] = {
		.channel = {
			[0] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 0,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 0,
				.scan_type = IIO_ST('u', 12, 16, 0),
				.event_mask = AD799X_EV_MASK,
			},
			[1] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 1,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 1,
				.scan_type = IIO_ST('u', 12, 16, 0),
				.event_mask = AD799X_EV_MASK,
			},
			[2] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 2,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 2,
				.scan_type = IIO_ST('u', 12, 16, 0),
				.event_mask = AD799X_EV_MASK,
			},
			[3] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 3,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 3,
				.scan_type = IIO_ST('u', 12, 16, 0),
				.event_mask = AD799X_EV_MASK,
			},
			[4] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 4,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 4,
				.scan_type = IIO_ST('u', 12, 16, 0),
			},
			[5] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 5,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 5,
				.scan_type = IIO_ST('u', 12, 16, 0),
			},
			[6] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 6,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 6,
				.scan_type = IIO_ST('u', 12, 16, 0),
			},
			[7] = {
				.type = IIO_VOLTAGE,
				.indexed = 1,
				.channel = 7,
				.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
				.scan_index = 7,
				.scan_type = IIO_ST('u', 12, 16, 0),
			},
			[8] = IIO_CHAN_SOFT_TIMESTAMP(8),
		},
		.num_channels = 9,
		.int_vref_mv = 4096,
		.default_config = AD7998_ALERT_EN,
		.info = &ad7993_4_7_8_info,
	},
};

static int ad799x_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	int ret;
	struct ad799x_platform_data *pdata = client->dev.platform_data;
	struct ad799x_state *st;
	struct iio_dev *indio_dev = iio_device_alloc(sizeof(*st));

	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, indio_dev);

	st->id = id->driver_data;
	st->chip_info = &ad799x_chip_info_tbl[st->id];
	st->config = st->chip_info->default_config;

	/* TODO: Add pdata options for filtering and bit delay */

	if (pdata)
		st->int_vref_mv = pdata->vref_mv;
	else
		st->int_vref_mv = st->chip_info->int_vref_mv;

	st->reg = regulator_get(&client->dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			goto error_put_reg;
	}
	st->client = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;
	indio_dev->info = st->chip_info->info;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channel;
	indio_dev->num_channels = st->chip_info->num_channels;

	ret = ad799x_register_ring_funcs_and_init(indio_dev);
	if (ret)
		goto error_disable_reg;

	if (client->irq > 0) {
		ret = request_threaded_irq(client->irq,
					   NULL,
					   ad799x_event_handler,
					   IRQF_TRIGGER_FALLING |
					   IRQF_ONESHOT,
					   client->name,
					   indio_dev);
		if (ret)
			goto error_cleanup_ring;
	}
	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_irq;

	return 0;

error_free_irq:
	free_irq(client->irq, indio_dev);
error_cleanup_ring:
	ad799x_ring_cleanup(indio_dev);
error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(st->reg))
		regulator_put(st->reg);
	iio_device_free(indio_dev);

	return ret;
}

static __devexit int ad799x_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ad799x_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (client->irq > 0)
		free_irq(client->irq, indio_dev);

	ad799x_ring_cleanup(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	iio_device_free(indio_dev);

	return 0;
}

static const struct i2c_device_id ad799x_id[] = {
	{ "ad7991", ad7991 },
	{ "ad7995", ad7995 },
	{ "ad7999", ad7999 },
	{ "ad7992", ad7992 },
	{ "ad7993", ad7993 },
	{ "ad7994", ad7994 },
	{ "ad7997", ad7997 },
	{ "ad7998", ad7998 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ad799x_id);

static struct i2c_driver ad799x_driver = {
	.driver = {
		.name = "ad799x",
	},
	.probe = ad799x_probe,
	.remove = __devexit_p(ad799x_remove),
	.id_table = ad799x_id,
};
module_i2c_driver(ad799x_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD799x ADC");
MODULE_LICENSE("GPL v2");
