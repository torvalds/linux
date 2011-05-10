/*
 * iio/adc/ad799x.c
 * Copyright (C) 2010 Michael Hennerich, Analog Devices Inc.
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
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/err.h>

#include "../iio.h"
#include "../sysfs.h"

#include "../ring_generic.h"
#include "adc.h"
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

static int ad799x_scan_el_set_state(struct iio_scan_el *scan_el,
				       struct iio_dev *indio_dev,
				       bool state)
{
	struct ad799x_state *st = indio_dev->dev_data;
	return ad799x_set_scan_mode(st, st->indio_dev->ring->scan_mask);
}

/* Here we claim all are 16 bits. This currently does no harm and saves
 * us a lot of scan element listings */

#define AD799X_SCAN_EL(number)						\
	IIO_SCAN_EL_C(in##number, number, 0, ad799x_scan_el_set_state);

static AD799X_SCAN_EL(0);
static AD799X_SCAN_EL(1);
static AD799X_SCAN_EL(2);
static AD799X_SCAN_EL(3);
static AD799X_SCAN_EL(4);
static AD799X_SCAN_EL(5);
static AD799X_SCAN_EL(6);
static AD799X_SCAN_EL(7);

static IIO_SCAN_EL_TIMESTAMP(8);
static IIO_CONST_ATTR_SCAN_EL_TYPE(timestamp, s, 64, 64)

static ssize_t ad799x_show_type(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_ring_buffer *ring = dev_get_drvdata(dev);
	struct iio_dev *indio_dev = ring->indio_dev;
	struct ad799x_state *st = indio_dev->dev_data;

	return sprintf(buf, "%c%d/%d\n", st->chip_info->sign,
		       st->chip_info->bits, AD799X_STORAGEBITS);
}
static IIO_DEVICE_ATTR(in_type, S_IRUGO, ad799x_show_type, NULL, 0);

static int ad7991_5_9_set_scan_mode(struct ad799x_state *st, unsigned mask)
{
	return i2c_smbus_write_byte(st->client,
		st->config | (mask << AD799X_CHANNEL_SHIFT));
}

static int ad7992_3_4_set_scan_mode(struct ad799x_state *st, unsigned mask)
{
	return ad799x_i2c_write8(st, AD7998_CONF_REG,
		st->config | (mask << AD799X_CHANNEL_SHIFT));
}

static int ad7997_8_set_scan_mode(struct ad799x_state *st, unsigned mask)
{
	return ad799x_i2c_write16(st, AD7998_CONF_REG,
		st->config | (mask << AD799X_CHANNEL_SHIFT));
}

int ad799x_set_scan_mode(struct ad799x_state *st, unsigned mask)
{
	int ret;

	if (st->chip_info->ad799x_set_scan_mode != NULL) {
		ret = st->chip_info->ad799x_set_scan_mode(st, mask);
		return (ret > 0) ? 0 : ret;
	}

	return 0;
}

static ssize_t ad799x_read_single_channel(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad799x_state *st = iio_dev_get_devdata(dev_info);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret = 0, len = 0;
	u32 data ;
	u16 rxbuf[1];
	u8 cmd;
	long mask;

	mutex_lock(&dev_info->mlock);
	mask = 1 << this_attr->address;
	/* If ring buffer capture is occurring, query the buffer */
	if (iio_ring_enabled(dev_info)) {
		data = ret = ad799x_single_channel_from_ring(st, mask);
		if (ret < 0)
			goto error_ret;
		ret = 0;
	} else {
		switch (st->id) {
		case ad7991:
		case ad7995:
		case ad7999:
			cmd = st->config | (mask << AD799X_CHANNEL_SHIFT);
			break;
		case ad7992:
		case ad7993:
		case ad7994:
			cmd = mask << AD799X_CHANNEL_SHIFT;
			break;
		case ad7997:
		case ad7998:
			cmd = (this_attr->address <<
				AD799X_CHANNEL_SHIFT) | AD7997_8_READ_SINGLE;
			break;
		default:
			cmd = 0;

		}
		ret = ad799x_i2c_read16(st, cmd, rxbuf);
		if (ret < 0)
			goto error_ret;

		data = rxbuf[0];
	}

	/* Pretty print the result */
	len = sprintf(buf, "%u\n", data & ((1 << (st->chip_info->bits)) - 1));

error_ret:
	mutex_unlock(&dev_info->mlock);
	return ret ? ret : len;
}

static ssize_t ad799x_read_frequency(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad799x_state *st = iio_dev_get_devdata(dev_info);

	int ret, len = 0;
	u8 val;
	ret = ad799x_i2c_read8(st, AD7998_CYCLE_TMR_REG, &val);
	if (ret)
		return ret;

	val &= AD7998_CYC_MASK;

	switch (val) {
	case AD7998_CYC_DIS:
		len = sprintf(buf, "0\n");
		break;
	case AD7998_CYC_TCONF_32:
		len = sprintf(buf, "15625\n");
		break;
	case AD7998_CYC_TCONF_64:
		len = sprintf(buf, "7812\n");
		break;
	case AD7998_CYC_TCONF_128:
		len = sprintf(buf, "3906\n");
		break;
	case AD7998_CYC_TCONF_256:
		len = sprintf(buf, "1953\n");
		break;
	case AD7998_CYC_TCONF_512:
		len = sprintf(buf, "976\n");
		break;
	case AD7998_CYC_TCONF_1024:
		len = sprintf(buf, "488\n");
		break;
	case AD7998_CYC_TCONF_2048:
		len = sprintf(buf, "244\n");
		break;
	}
	return len;
}

static ssize_t ad799x_write_frequency(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad799x_state *st = iio_dev_get_devdata(dev_info);

	long val;
	int ret;
	u8 t;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&dev_info->mlock);
	ret = ad799x_i2c_read8(st, AD7998_CYCLE_TMR_REG, &t);
	if (ret)
		goto error_ret_mutex;
	/* Wipe the bits clean */
	t &= ~AD7998_CYC_MASK;

	switch (val) {
	case 15625:
		t |= AD7998_CYC_TCONF_32;
		break;
	case 7812:
		t |= AD7998_CYC_TCONF_64;
		break;
	case 3906:
		t |= AD7998_CYC_TCONF_128;
		break;
	case 1953:
		t |= AD7998_CYC_TCONF_256;
		break;
	case 976:
		t |= AD7998_CYC_TCONF_512;
		break;
	case 488:
		t |= AD7998_CYC_TCONF_1024;
		break;
	case 244:
		t |= AD7998_CYC_TCONF_2048;
		break;
	case  0:
		t |= AD7998_CYC_DIS;
		break;
	default:
		ret = -EINVAL;
		goto error_ret_mutex;
	}

	ret = ad799x_i2c_write8(st, AD7998_CYCLE_TMR_REG, t);

error_ret_mutex:
	mutex_unlock(&dev_info->mlock);

	return ret ? ret : len;
}


static ssize_t ad799x_read_channel_config(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad799x_state *st = iio_dev_get_devdata(dev_info);
	struct iio_event_attr *this_attr = to_iio_event_attr(attr);

	int ret;
	u16 val;
	ret = ad799x_i2c_read16(st, this_attr->mask, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", val);
}

static ssize_t ad799x_write_channel_config(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad799x_state *st = iio_dev_get_devdata(dev_info);
	struct iio_event_attr *this_attr = to_iio_event_attr(attr);

	long val;
	int ret;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&dev_info->mlock);
	ret = ad799x_i2c_write16(st, this_attr->mask, val);
	mutex_unlock(&dev_info->mlock);

	return ret ? ret : len;
}

static void ad799x_interrupt_bh(struct work_struct *work_s)
{
	struct ad799x_state *st = container_of(work_s,
		struct ad799x_state, work_thresh);
	u8 status;
	int i;

	if (ad799x_i2c_read8(st, AD7998_ALERT_STAT_REG, &status))
		goto err_out;

	if (!status)
		goto err_out;

	ad799x_i2c_write8(st, AD7998_ALERT_STAT_REG, AD7998_ALERT_STAT_CLEAR);

	for (i = 0; i < 8; i++) {
		if (status & (1 << i))
			iio_push_event(st->indio_dev, 0,
				i & 0x1 ?
				IIO_EVENT_CODE_IN_HIGH_THRESH(i >> 1) :
				IIO_EVENT_CODE_IN_LOW_THRESH(i >> 1),
				st->last_timestamp);
	}

err_out:
	enable_irq(st->client->irq);
}

static int ad799x_interrupt(struct iio_dev *dev_info,
		int index,
		s64 timestamp,
		int no_test)
{
	struct ad799x_state *st = dev_info->dev_data;

	st->last_timestamp = timestamp;
	schedule_work(&st->work_thresh);
	return 0;
}

IIO_EVENT_SH(ad799x, &ad799x_interrupt);

/* Direct read attribtues */
static IIO_DEV_ATTR_IN_RAW(0, ad799x_read_single_channel, 0);
static IIO_DEV_ATTR_IN_RAW(1, ad799x_read_single_channel, 1);
static IIO_DEV_ATTR_IN_RAW(2, ad799x_read_single_channel, 2);
static IIO_DEV_ATTR_IN_RAW(3, ad799x_read_single_channel, 3);
static IIO_DEV_ATTR_IN_RAW(4, ad799x_read_single_channel, 4);
static IIO_DEV_ATTR_IN_RAW(5, ad799x_read_single_channel, 5);
static IIO_DEV_ATTR_IN_RAW(6, ad799x_read_single_channel, 6);
static IIO_DEV_ATTR_IN_RAW(7, ad799x_read_single_channel, 7);

static ssize_t ad799x_show_scale(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	/* Driver currently only support internal vref */
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad799x_state *st = iio_dev_get_devdata(dev_info);

	/* Corresponds to Vref / 2^(bits) */
	unsigned int scale_uv = (st->int_vref_mv * 1000) >> st->chip_info->bits;

	return sprintf(buf, "%d.%03d\n", scale_uv / 1000, scale_uv % 1000);
}

static IIO_DEVICE_ATTR(in_scale, S_IRUGO, ad799x_show_scale, NULL, 0);

static ssize_t ad799x_show_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad799x_state *st = iio_dev_get_devdata(dev_info);
	return sprintf(buf, "%s\n", st->client->name);
}

static IIO_DEVICE_ATTR(name, S_IRUGO, ad799x_show_name, NULL, 0);

static struct attribute *ad7991_5_9_3_4_device_attrs[] = {
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_dev_attr_in2_raw.dev_attr.attr,
	&iio_dev_attr_in3_raw.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	&iio_dev_attr_in_scale.dev_attr.attr,
	NULL
};

static struct attribute_group ad7991_5_9_3_4_dev_attr_group = {
	.attrs = ad7991_5_9_3_4_device_attrs,
};

static struct attribute *ad7991_5_9_3_4_scan_el_attrs[] = {
	&iio_scan_el_in0.dev_attr.attr,
	&iio_const_attr_in0_index.dev_attr.attr,
	&iio_scan_el_in1.dev_attr.attr,
	&iio_const_attr_in1_index.dev_attr.attr,
	&iio_scan_el_in2.dev_attr.attr,
	&iio_const_attr_in2_index.dev_attr.attr,
	&iio_scan_el_in3.dev_attr.attr,
	&iio_const_attr_in3_index.dev_attr.attr,
	&iio_const_attr_timestamp_index.dev_attr.attr,
	&iio_scan_el_timestamp.dev_attr.attr,
	&iio_const_attr_timestamp_type.dev_attr.attr,
	&iio_dev_attr_in_type.dev_attr.attr,
	NULL,
};

static struct attribute_group ad7991_5_9_3_4_scan_el_group = {
	.name = "scan_elements",
	.attrs = ad7991_5_9_3_4_scan_el_attrs,
};

static struct attribute *ad7992_device_attrs[] = {
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	&iio_dev_attr_in_scale.dev_attr.attr,
	NULL
};

static struct attribute_group ad7992_dev_attr_group = {
	.attrs = ad7992_device_attrs,
};

static struct attribute *ad7992_scan_el_attrs[] = {
	&iio_scan_el_in0.dev_attr.attr,
	&iio_const_attr_in0_index.dev_attr.attr,
	&iio_scan_el_in1.dev_attr.attr,
	&iio_const_attr_in1_index.dev_attr.attr,
	&iio_const_attr_timestamp_index.dev_attr.attr,
	&iio_scan_el_timestamp.dev_attr.attr,
	&iio_const_attr_timestamp_type.dev_attr.attr,
	&iio_dev_attr_in_type.dev_attr.attr,
	NULL,
};

static struct attribute_group ad7992_scan_el_group = {
	.name = "scan_elements",
	.attrs = ad7992_scan_el_attrs,
};

static struct attribute *ad7997_8_device_attrs[] = {
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_dev_attr_in2_raw.dev_attr.attr,
	&iio_dev_attr_in3_raw.dev_attr.attr,
	&iio_dev_attr_in4_raw.dev_attr.attr,
	&iio_dev_attr_in5_raw.dev_attr.attr,
	&iio_dev_attr_in6_raw.dev_attr.attr,
	&iio_dev_attr_in7_raw.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	&iio_dev_attr_in_scale.dev_attr.attr,
	NULL
};

static struct attribute_group ad7997_8_dev_attr_group = {
	.attrs = ad7997_8_device_attrs,
};

static struct attribute *ad7997_8_scan_el_attrs[] = {
	&iio_scan_el_in0.dev_attr.attr,
	&iio_const_attr_in0_index.dev_attr.attr,
	&iio_scan_el_in1.dev_attr.attr,
	&iio_const_attr_in1_index.dev_attr.attr,
	&iio_scan_el_in2.dev_attr.attr,
	&iio_const_attr_in2_index.dev_attr.attr,
	&iio_scan_el_in3.dev_attr.attr,
	&iio_const_attr_in3_index.dev_attr.attr,
	&iio_scan_el_in4.dev_attr.attr,
	&iio_const_attr_in4_index.dev_attr.attr,
	&iio_scan_el_in5.dev_attr.attr,
	&iio_const_attr_in5_index.dev_attr.attr,
	&iio_scan_el_in6.dev_attr.attr,
	&iio_const_attr_in6_index.dev_attr.attr,
	&iio_scan_el_in7.dev_attr.attr,
	&iio_const_attr_in7_index.dev_attr.attr,
	&iio_const_attr_timestamp_index.dev_attr.attr,
	&iio_scan_el_timestamp.dev_attr.attr,
	&iio_const_attr_timestamp_type.dev_attr.attr,
	&iio_dev_attr_in_type.dev_attr.attr,
	NULL,
};

static struct attribute_group ad7997_8_scan_el_group = {
	.name = "scan_elements",
	.attrs = ad7997_8_scan_el_attrs,
};

IIO_EVENT_ATTR_SH(in0_thresh_low_value,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_DATALOW_CH1_REG);

IIO_EVENT_ATTR_SH(in0_thresh_high_value,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_DATAHIGH_CH1_REG);

IIO_EVENT_ATTR_SH(in0_thresh_both_hyst_raw,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_HYST_CH1_REG);

IIO_EVENT_ATTR_SH(in1_thresh_low_value,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_DATALOW_CH2_REG);

IIO_EVENT_ATTR_SH(in1_thresh_high_value,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_DATAHIGH_CH2_REG);

IIO_EVENT_ATTR_SH(in1_thresh_both_hyst_raw,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_HYST_CH2_REG);

IIO_EVENT_ATTR_SH(in2_thresh_low_value,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_DATALOW_CH3_REG);

IIO_EVENT_ATTR_SH(in2_thresh_high_value,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_DATAHIGH_CH3_REG);

IIO_EVENT_ATTR_SH(in2_thresh_both_hyst_raw,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_HYST_CH3_REG);

IIO_EVENT_ATTR_SH(in3_thresh_low_value,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_DATALOW_CH4_REG);

IIO_EVENT_ATTR_SH(in3_thresh_high_value,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_DATAHIGH_CH4_REG);

IIO_EVENT_ATTR_SH(in3_thresh_both_hyst_raw,
		  iio_event_ad799x,
		  ad799x_read_channel_config,
		  ad799x_write_channel_config,
		  AD7998_HYST_CH4_REG);

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
			      ad799x_read_frequency,
			      ad799x_write_frequency);
static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("15625 7812 3906 1953 976 488 244 0");

static struct attribute *ad7993_4_7_8_event_attributes[] = {
	&iio_event_attr_in0_thresh_low_value.dev_attr.attr,
	&iio_event_attr_in0_thresh_high_value.dev_attr.attr,
	&iio_event_attr_in0_thresh_both_hyst_raw.dev_attr.attr,
	&iio_event_attr_in1_thresh_low_value.dev_attr.attr,
	&iio_event_attr_in1_thresh_high_value.dev_attr.attr,
	&iio_event_attr_in1_thresh_both_hyst_raw.dev_attr.attr,
	&iio_event_attr_in2_thresh_low_value.dev_attr.attr,
	&iio_event_attr_in2_thresh_high_value.dev_attr.attr,
	&iio_event_attr_in2_thresh_both_hyst_raw.dev_attr.attr,
	&iio_event_attr_in3_thresh_low_value.dev_attr.attr,
	&iio_event_attr_in3_thresh_high_value.dev_attr.attr,
	&iio_event_attr_in3_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static struct attribute_group ad7993_4_7_8_event_attrs_group = {
	.attrs = ad7993_4_7_8_event_attributes,
};

static struct attribute *ad7992_event_attributes[] = {
	&iio_event_attr_in0_thresh_low_value.dev_attr.attr,
	&iio_event_attr_in0_thresh_high_value.dev_attr.attr,
	&iio_event_attr_in0_thresh_both_hyst_raw.dev_attr.attr,
	&iio_event_attr_in1_thresh_low_value.dev_attr.attr,
	&iio_event_attr_in1_thresh_high_value.dev_attr.attr,
	&iio_event_attr_in1_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static struct attribute_group ad7992_event_attrs_group = {
	.attrs = ad7992_event_attributes,
};

static const struct ad799x_chip_info ad799x_chip_info_tbl[] = {
	[ad7991] = {
		.num_inputs = 4,
		.bits = 12,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
		.int_vref_mv = 4096,
		.dev_attrs = &ad7991_5_9_3_4_dev_attr_group,
		.scan_attrs = &ad7991_5_9_3_4_scan_el_group,
		.ad799x_set_scan_mode = ad7991_5_9_set_scan_mode,
	},
	[ad7995] = {
		.num_inputs = 4,
		.bits = 10,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
		.int_vref_mv = 1024,
		.dev_attrs = &ad7991_5_9_3_4_dev_attr_group,
		.scan_attrs = &ad7991_5_9_3_4_scan_el_group,
		.ad799x_set_scan_mode = ad7991_5_9_set_scan_mode,
	},
	[ad7999] = {
		.num_inputs = 4,
		.bits = 10,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
		.int_vref_mv = 1024,
		.dev_attrs = &ad7991_5_9_3_4_dev_attr_group,
		.scan_attrs = &ad7991_5_9_3_4_scan_el_group,
		.ad799x_set_scan_mode = ad7991_5_9_set_scan_mode,
	},
	[ad7992] = {
		.num_inputs = 2,
		.bits = 12,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
		.int_vref_mv = 4096,
		.monitor_mode = true,
		.default_config = AD7998_ALERT_EN,
		.dev_attrs = &ad7992_dev_attr_group,
		.scan_attrs = &ad7992_scan_el_group,
		.event_attrs = &ad7992_event_attrs_group,
		.ad799x_set_scan_mode = ad7992_3_4_set_scan_mode,
	},
	[ad7993] = {
		.num_inputs = 4,
		.bits = 10,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
		.int_vref_mv = 1024,
		.monitor_mode = true,
		.default_config = AD7998_ALERT_EN,
		.dev_attrs = &ad7991_5_9_3_4_dev_attr_group,
		.scan_attrs = &ad7991_5_9_3_4_scan_el_group,
		.event_attrs = &ad7993_4_7_8_event_attrs_group,
		.ad799x_set_scan_mode = ad7992_3_4_set_scan_mode,
	},
	[ad7994] = {
		.num_inputs = 4,
		.bits = 12,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
		.int_vref_mv = 4096,
		.monitor_mode = true,
		.default_config = AD7998_ALERT_EN,
		.dev_attrs = &ad7991_5_9_3_4_dev_attr_group,
		.scan_attrs = &ad7991_5_9_3_4_scan_el_group,
		.event_attrs = &ad7993_4_7_8_event_attrs_group,
		.ad799x_set_scan_mode = ad7992_3_4_set_scan_mode,
	},
	[ad7997] = {
		.num_inputs = 8,
		.bits = 10,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
		.int_vref_mv = 1024,
		.monitor_mode = true,
		.default_config = AD7998_ALERT_EN,
		.dev_attrs = &ad7997_8_dev_attr_group,
		.scan_attrs = &ad7997_8_scan_el_group,
		.event_attrs = &ad7993_4_7_8_event_attrs_group,
		.ad799x_set_scan_mode = ad7997_8_set_scan_mode,
	},
	[ad7998] = {
		.num_inputs = 8,
		.bits = 12,
		.sign = IIO_SCAN_EL_TYPE_UNSIGNED,
		.int_vref_mv = 4096,
		.monitor_mode = true,
		.default_config = AD7998_ALERT_EN,
		.dev_attrs = &ad7997_8_dev_attr_group,
		.scan_attrs = &ad7997_8_scan_el_group,
		.event_attrs = &ad7993_4_7_8_event_attrs_group,
		.ad799x_set_scan_mode = ad7997_8_set_scan_mode,
	},
};

static int __devinit ad799x_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	int ret, regdone = 0;
	struct ad799x_platform_data *pdata = client->dev.platform_data;
	struct ad799x_state *st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, st);

	atomic_set(&st->protect_ring, 0);
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

	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}

	/* Estabilish that the iio_dev is a child of the i2c device */
	st->indio_dev->dev.parent = &client->dev;
	st->indio_dev->attrs = st->chip_info->dev_attrs;
	st->indio_dev->event_attrs = st->chip_info->event_attrs;

	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;
	st->indio_dev->num_interrupt_lines = 1;

	ret = ad799x_set_scan_mode(st, 0);
	if (ret)
		goto error_free_device;

	ret = ad799x_register_ring_funcs_and_init(st->indio_dev);
	if (ret)
		goto error_free_device;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_cleanup_ring;
	regdone = 1;

	ret = iio_ring_buffer_register(st->indio_dev->ring, 0);
	if (ret)
		goto error_cleanup_ring;

	if (client->irq > 0 && st->chip_info->monitor_mode) {
		INIT_WORK(&st->work_thresh, ad799x_interrupt_bh);

		ret = iio_register_interrupt_line(client->irq,
				st->indio_dev,
				0,
				IRQF_TRIGGER_FALLING,
				client->name);
		if (ret)
			goto error_cleanup_ring;

		/*
		 * The event handler list element refer to iio_event_ad799x.
		 * All event attributes bind to the same event handler.
		 * So, only register event handler once.
		 */
		iio_add_event_to_list(&iio_event_ad799x,
				&st->indio_dev->interrupts[0]->ev_list);
	}

	return 0;
error_cleanup_ring:
	ad799x_ring_cleanup(st->indio_dev);
error_free_device:
	if (!regdone)
		iio_free_device(st->indio_dev);
	else
		iio_device_unregister(st->indio_dev);
error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(st->reg))
		regulator_put(st->reg);
	kfree(st);
error_ret:
	return ret;
}

static __devexit int ad799x_remove(struct i2c_client *client)
{
	struct ad799x_state *st = i2c_get_clientdata(client);
	struct iio_dev *indio_dev = st->indio_dev;

	if (client->irq > 0 && st->chip_info->monitor_mode)
		iio_unregister_interrupt_line(indio_dev, 0);

	iio_ring_buffer_unregister(indio_dev->ring);
	ad799x_ring_cleanup(indio_dev);
	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	kfree(st);

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

static __init int ad799x_init(void)
{
	return i2c_add_driver(&ad799x_driver);
}

static __exit void ad799x_exit(void)
{
	i2c_del_driver(&ad799x_driver);
}

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD799x ADC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:ad799x");

module_init(ad799x_init);
module_exit(ad799x_exit);
