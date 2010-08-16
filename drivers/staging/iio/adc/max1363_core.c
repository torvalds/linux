 /*
  * iio/adc/max1363.c
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
  * max1363.c
  *
  * Partial support for max1363 and similar chips.
  *
  * Not currently implemented.
  *
  * - Monitor interrrupt generation.
  * - Control of internal reference.
  */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include "../iio.h"
#include "../sysfs.h"

#include "../ring_generic.h"
#include "adc.h"
#include "max1363.h"

/* Here we claim all are 16 bits. This currently does no harm and saves
 * us a lot of scan element listings */

#define MAX1363_SCAN_EL(number)						\
	IIO_SCAN_EL_C(in##number, number, IIO_UNSIGNED(16), 0, NULL);
#define MAX1363_SCAN_EL_D(p, n, number)					\
	IIO_SCAN_NAMED_EL_C(in##p##m##in##n, in##p-in##n,		\
			number, IIO_SIGNED(16), 0 , NULL);

static MAX1363_SCAN_EL(0);
static MAX1363_SCAN_EL(1);
static MAX1363_SCAN_EL(2);
static MAX1363_SCAN_EL(3);
static MAX1363_SCAN_EL(4);
static MAX1363_SCAN_EL(5);
static MAX1363_SCAN_EL(6);
static MAX1363_SCAN_EL(7);
static MAX1363_SCAN_EL(8);
static MAX1363_SCAN_EL(9);
static MAX1363_SCAN_EL(10);
static MAX1363_SCAN_EL(11);
static MAX1363_SCAN_EL_D(0, 1, 12);
static MAX1363_SCAN_EL_D(2, 3, 13);
static MAX1363_SCAN_EL_D(4, 5, 14);
static MAX1363_SCAN_EL_D(6, 7, 15);
static MAX1363_SCAN_EL_D(8, 9, 16);
static MAX1363_SCAN_EL_D(10, 11, 17);
static MAX1363_SCAN_EL_D(1, 0, 18);
static MAX1363_SCAN_EL_D(3, 2, 19);
static MAX1363_SCAN_EL_D(5, 4, 20);
static MAX1363_SCAN_EL_D(7, 6, 21);
static MAX1363_SCAN_EL_D(9, 8, 22);
static MAX1363_SCAN_EL_D(11, 10, 23);

static const struct max1363_mode max1363_mode_table[] = {
	/* All of the single channel options first */
	MAX1363_MODE_SINGLE(0, 1 << 0),
	MAX1363_MODE_SINGLE(1, 1 << 1),
	MAX1363_MODE_SINGLE(2, 1 << 2),
	MAX1363_MODE_SINGLE(3, 1 << 3),
	MAX1363_MODE_SINGLE(4, 1 << 4),
	MAX1363_MODE_SINGLE(5, 1 << 5),
	MAX1363_MODE_SINGLE(6, 1 << 6),
	MAX1363_MODE_SINGLE(7, 1 << 7),
	MAX1363_MODE_SINGLE(8, 1 << 8),
	MAX1363_MODE_SINGLE(9, 1 << 9),
	MAX1363_MODE_SINGLE(10, 1 << 10),
	MAX1363_MODE_SINGLE(11, 1 << 11),

	MAX1363_MODE_DIFF_SINGLE(0, 1, 1 << 12),
	MAX1363_MODE_DIFF_SINGLE(2, 3, 1 << 13),
	MAX1363_MODE_DIFF_SINGLE(4, 5, 1 << 14),
	MAX1363_MODE_DIFF_SINGLE(6, 7, 1 << 15),
	MAX1363_MODE_DIFF_SINGLE(8, 9, 1 << 16),
	MAX1363_MODE_DIFF_SINGLE(10, 11, 1 << 17),
	MAX1363_MODE_DIFF_SINGLE(1, 0, 1 << 18),
	MAX1363_MODE_DIFF_SINGLE(3, 2, 1 << 19),
	MAX1363_MODE_DIFF_SINGLE(5, 4, 1 << 20),
	MAX1363_MODE_DIFF_SINGLE(7, 6, 1 << 21),
	MAX1363_MODE_DIFF_SINGLE(9, 8, 1 << 22),
	MAX1363_MODE_DIFF_SINGLE(11, 10, 1 << 23),

	/* The multichannel scans next */
	MAX1363_MODE_SCAN_TO_CHANNEL(1, 0x003),
	MAX1363_MODE_SCAN_TO_CHANNEL(2, 0x007),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(2, 3, 0x00C),
	MAX1363_MODE_SCAN_TO_CHANNEL(3, 0x00F),
	MAX1363_MODE_SCAN_TO_CHANNEL(4, 0x01F),
	MAX1363_MODE_SCAN_TO_CHANNEL(5, 0x03F),
	MAX1363_MODE_SCAN_TO_CHANNEL(6, 0x07F),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 7, 0x0C0),
	MAX1363_MODE_SCAN_TO_CHANNEL(7, 0x0FF),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 8, 0x1C0),
	MAX1363_MODE_SCAN_TO_CHANNEL(8, 0x1FF),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 9, 0x3C0),
	MAX1363_MODE_SCAN_TO_CHANNEL(9, 0x3FF),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 10, 0x7C0),
	MAX1363_MODE_SCAN_TO_CHANNEL(10, 0x7FF),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 11, 0xFC0),
	MAX1363_MODE_SCAN_TO_CHANNEL(11, 0xFFF),

	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(2, 2, 0x003000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(4, 3, 0x007000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(6, 4, 0x00F000),
	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL(8, 2, 0x018000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(8, 5, 0x01F000),
	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL(10, 3, 0x038000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(10, 6, 0x3F000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(3, 2, 0x0C0000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(5, 3, 0x1C0000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(7, 4, 0x3C0000),
	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL(9, 2, 0x600000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(9, 5, 0x7C0000),
	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL(11, 3, 0xE00000),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL(11, 6, 0xFC0000),
};

const struct max1363_mode
*max1363_match_mode(u32 mask, const struct max1363_chip_info *ci)
{
	int i;
	if (mask)
		for (i = 0; i < ci->num_modes; i++)
			if (!((~max1363_mode_table[ci->mode_list[i]].modemask) &
			      mask))
				return &max1363_mode_table[ci->mode_list[i]];
	return NULL;
};

static ssize_t max1363_show_precision(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct max1363_state *st = iio_dev_get_devdata(dev_info);
	return sprintf(buf, "%d\n", st->chip_info->bits);
}

static IIO_DEVICE_ATTR(in_precision, S_IRUGO, max1363_show_precision,
		       NULL, 0);

static int max1363_write_basic_config(struct i2c_client *client,
				      unsigned char d1,
				      unsigned char d2)
{
	int ret;
	u8 *tx_buf = kmalloc(2 , GFP_KERNEL);

	if (!tx_buf)
		return -ENOMEM;
	tx_buf[0] = d1;
	tx_buf[1] = d2;

	ret = i2c_master_send(client, tx_buf, 2);
	kfree(tx_buf);

	return (ret > 0) ? 0 : ret;
}

int max1363_set_scan_mode(struct max1363_state *st)
{
	st->configbyte &= ~(MAX1363_CHANNEL_SEL_MASK
			    | MAX1363_SCAN_MASK
			    | MAX1363_SE_DE_MASK);
	st->configbyte |= st->current_mode->conf;

	return max1363_write_basic_config(st->client,
					  st->setupbyte,
					  st->configbyte);
}

static ssize_t max1363_read_single_channel(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct max1363_state *st = iio_dev_get_devdata(dev_info);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct i2c_client *client = st->client;
	int ret = 0, len = 0;
	s32 data ;
	char rxbuf[2];
	long mask;

	mutex_lock(&dev_info->mlock);
	/* If ring buffer capture is occuring, query the buffer */
	if (iio_ring_enabled(dev_info)) {
		mask = max1363_mode_table[this_attr->address].modemask;
		data = max1363_single_channel_from_ring(mask, st);
		if (data < 0) {
			ret = data;
			goto error_ret;
		}
	} else {
		/* Check to see if current scan mode is correct */
		if (st->current_mode !=
		    &max1363_mode_table[this_attr->address]) {
			/* Update scan mode if needed */
			st->current_mode
				= &max1363_mode_table[this_attr->address];
			ret = max1363_set_scan_mode(st);
			if (ret)
				goto error_ret;
		}
		if (st->chip_info->bits != 8) {
			/* Get reading */
			data = i2c_master_recv(client, rxbuf, 2);
			if (data < 0) {
				ret = data;
				goto error_ret;
			}

			data = (s32)(rxbuf[1]) | ((s32)(rxbuf[0] & 0x0F)) << 8;
		} else {
			/* Get reading */
			data = i2c_master_recv(client, rxbuf, 1);
			if (data < 0) {
				ret = data;
				goto error_ret;
			}
			data = rxbuf[0];
		}
	}
	/* Pretty print the result */
	len = sprintf(buf, "%u\n", data);

error_ret:
	mutex_unlock(&dev_info->mlock);
	return ret ? ret : len;
}

/* Direct read attribtues */
static IIO_DEV_ATTR_IN_RAW(0, max1363_read_single_channel, _s0);
static IIO_DEV_ATTR_IN_RAW(1, max1363_read_single_channel, _s1);
static IIO_DEV_ATTR_IN_RAW(2, max1363_read_single_channel, _s2);
static IIO_DEV_ATTR_IN_RAW(3, max1363_read_single_channel, _s3);
static IIO_DEV_ATTR_IN_RAW(4, max1363_read_single_channel, _s4);
static IIO_DEV_ATTR_IN_RAW(5, max1363_read_single_channel, _s5);
static IIO_DEV_ATTR_IN_RAW(6, max1363_read_single_channel, _s6);
static IIO_DEV_ATTR_IN_RAW(7, max1363_read_single_channel, _s7);
static IIO_DEV_ATTR_IN_RAW(8, max1363_read_single_channel, _s8);
static IIO_DEV_ATTR_IN_RAW(9, max1363_read_single_channel, _s9);
static IIO_DEV_ATTR_IN_RAW(10, max1363_read_single_channel, _s10);
static IIO_DEV_ATTR_IN_RAW(11, max1363_read_single_channel, _s11);

static IIO_DEV_ATTR_IN_DIFF_RAW(0, 1, max1363_read_single_channel, d0m1);
static IIO_DEV_ATTR_IN_DIFF_RAW(2, 3, max1363_read_single_channel, d2m3);
static IIO_DEV_ATTR_IN_DIFF_RAW(4, 5, max1363_read_single_channel, d4m5);
static IIO_DEV_ATTR_IN_DIFF_RAW(6, 7, max1363_read_single_channel, d6m7);
static IIO_DEV_ATTR_IN_DIFF_RAW(8, 9, max1363_read_single_channel, d8m9);
static IIO_DEV_ATTR_IN_DIFF_RAW(10, 11, max1363_read_single_channel, d10m11);
static IIO_DEV_ATTR_IN_DIFF_RAW(1, 0, max1363_read_single_channel, d1m0);
static IIO_DEV_ATTR_IN_DIFF_RAW(3, 2, max1363_read_single_channel, d3m2);
static IIO_DEV_ATTR_IN_DIFF_RAW(5, 4, max1363_read_single_channel, d5m4);
static IIO_DEV_ATTR_IN_DIFF_RAW(7, 6, max1363_read_single_channel, d7m6);
static IIO_DEV_ATTR_IN_DIFF_RAW(9, 8, max1363_read_single_channel, d9m8);
static IIO_DEV_ATTR_IN_DIFF_RAW(11, 10, max1363_read_single_channel, d11m10);


static ssize_t max1363_show_scale(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	/* Driver currently only support internal vref */
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct max1363_state *st = iio_dev_get_devdata(dev_info);
	/* Corresponds to Vref / 2^(bits) */

	if ((1 << (st->chip_info->bits + 1))
	    > st->chip_info->int_vref_mv)
		return sprintf(buf, "0.5\n");
	else
		return sprintf(buf, "%d\n",
			st->chip_info->int_vref_mv >> st->chip_info->bits);
}

static IIO_DEVICE_ATTR(in_scale, S_IRUGO, max1363_show_scale, NULL, 0);

static ssize_t max1363_show_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct max1363_state *st = iio_dev_get_devdata(dev_info);
	return sprintf(buf, "%s\n", st->chip_info->name);
}

static IIO_DEVICE_ATTR(name, S_IRUGO, max1363_show_name, NULL, 0);

/* Applies to max1363 */
static const enum max1363_modes max1363_mode_list[] = {
	_s0, _s1, _s2, _s3,
	s0to1, s0to2, s0to3,
	d0m1, d2m3, d1m0, d3m2,
	d0m1to2m3, d1m0to3m2,
};

static struct attribute *max1363_device_attrs[] = {
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_dev_attr_in2_raw.dev_attr.attr,
	&iio_dev_attr_in3_raw.dev_attr.attr,
	&iio_dev_attr_in0min1_raw.dev_attr.attr,
	&iio_dev_attr_in2min3_raw.dev_attr.attr,
	&iio_dev_attr_in1min0_raw.dev_attr.attr,
	&iio_dev_attr_in3min2_raw.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	&iio_dev_attr_in_scale.dev_attr.attr,
	NULL
};

static struct attribute_group max1363_dev_attr_group = {
	.attrs = max1363_device_attrs,
};

static struct attribute *max1363_scan_el_attrs[] = {
	&iio_scan_el_in0.dev_attr.attr,
	&iio_scan_el_in1.dev_attr.attr,
	&iio_scan_el_in2.dev_attr.attr,
	&iio_scan_el_in3.dev_attr.attr,
	&iio_scan_el_in0min1.dev_attr.attr,
	&iio_scan_el_in2min3.dev_attr.attr,
	&iio_scan_el_in1min0.dev_attr.attr,
	&iio_scan_el_in3min2.dev_attr.attr,
	&iio_dev_attr_in_precision.dev_attr.attr,
	NULL,
};

static struct attribute_group max1363_scan_el_group = {
	.name = "scan_elements",
	.attrs = max1363_scan_el_attrs,
};

/* Appies to max1236, max1237 */
static const enum max1363_modes max1236_mode_list[] = {
	_s0, _s1, _s2, _s3,
	s0to1, s0to2, s0to3,
	d0m1, d2m3, d1m0, d3m2,
	d0m1to2m3, d1m0to3m2,
	s2to3,
};

/* Applies to max1238, max1239 */
static const enum max1363_modes max1238_mode_list[] = {
	_s0, _s1, _s2, _s3, _s4, _s5, _s6, _s7, _s8, _s9, _s10, _s11,
	s0to1, s0to2, s0to3, s0to4, s0to5, s0to6,
	s0to7, s0to8, s0to9, s0to10, s0to11,
	d0m1, d2m3, d4m5, d6m7, d8m9, d10m11,
	d1m0, d3m2, d5m4, d7m6, d9m8, d11m10,
	d0m1to2m3, d0m1to4m5, d0m1to6m7, d0m1to8m9, d0m1to10m11,
	d1m0to3m2, d1m0to5m4, d1m0to7m6, d1m0to9m8, d1m0to11m10,
	s6to7, s6to8, s6to9, s6to10, s6to11,
	d6m7to8m9, d6m7to10m11, d7m6to9m8, d7m6to11m10,
};

static struct attribute *max1238_device_attrs[] = {
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_dev_attr_in2_raw.dev_attr.attr,
	&iio_dev_attr_in3_raw.dev_attr.attr,
	&iio_dev_attr_in4_raw.dev_attr.attr,
	&iio_dev_attr_in5_raw.dev_attr.attr,
	&iio_dev_attr_in6_raw.dev_attr.attr,
	&iio_dev_attr_in7_raw.dev_attr.attr,
	&iio_dev_attr_in8_raw.dev_attr.attr,
	&iio_dev_attr_in9_raw.dev_attr.attr,
	&iio_dev_attr_in10_raw.dev_attr.attr,
	&iio_dev_attr_in11_raw.dev_attr.attr,
	&iio_dev_attr_in0min1_raw.dev_attr.attr,
	&iio_dev_attr_in2min3_raw.dev_attr.attr,
	&iio_dev_attr_in4min5_raw.dev_attr.attr,
	&iio_dev_attr_in6min7_raw.dev_attr.attr,
	&iio_dev_attr_in8min9_raw.dev_attr.attr,
	&iio_dev_attr_in10min11_raw.dev_attr.attr,
	&iio_dev_attr_in1min0_raw.dev_attr.attr,
	&iio_dev_attr_in3min2_raw.dev_attr.attr,
	&iio_dev_attr_in5min4_raw.dev_attr.attr,
	&iio_dev_attr_in7min6_raw.dev_attr.attr,
	&iio_dev_attr_in9min8_raw.dev_attr.attr,
	&iio_dev_attr_in11min10_raw.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	&iio_dev_attr_in_scale.dev_attr.attr,
	NULL
};

static struct attribute_group max1238_dev_attr_group = {
	.attrs = max1238_device_attrs,
};

static struct attribute *max1238_scan_el_attrs[] = {
	&iio_scan_el_in0.dev_attr.attr,
	&iio_scan_el_in1.dev_attr.attr,
	&iio_scan_el_in2.dev_attr.attr,
	&iio_scan_el_in3.dev_attr.attr,
	&iio_scan_el_in4.dev_attr.attr,
	&iio_scan_el_in5.dev_attr.attr,
	&iio_scan_el_in6.dev_attr.attr,
	&iio_scan_el_in7.dev_attr.attr,
	&iio_scan_el_in8.dev_attr.attr,
	&iio_scan_el_in9.dev_attr.attr,
	&iio_scan_el_in10.dev_attr.attr,
	&iio_scan_el_in11.dev_attr.attr,
	&iio_scan_el_in0min1.dev_attr.attr,
	&iio_scan_el_in2min3.dev_attr.attr,
	&iio_scan_el_in4min5.dev_attr.attr,
	&iio_scan_el_in6min7.dev_attr.attr,
	&iio_scan_el_in8min9.dev_attr.attr,
	&iio_scan_el_in10min11.dev_attr.attr,
	&iio_scan_el_in1min0.dev_attr.attr,
	&iio_scan_el_in3min2.dev_attr.attr,
	&iio_scan_el_in5min4.dev_attr.attr,
	&iio_scan_el_in7min6.dev_attr.attr,
	&iio_scan_el_in9min8.dev_attr.attr,
	&iio_scan_el_in11min10.dev_attr.attr,
	&iio_dev_attr_in_precision.dev_attr.attr,
	NULL,
};

static struct attribute_group max1238_scan_el_group = {
	.name = "scan_elements",
	.attrs = max1238_scan_el_attrs,
};


static const enum max1363_modes max11607_mode_list[] = {
	_s0, _s1, _s2, _s3,
	s0to1, s0to2, s0to3,
	s2to3,
	d0m1, d2m3, d1m0, d3m2,
	d0m1to2m3, d1m0to3m2,
};

static const enum max1363_modes max11608_mode_list[] = {
	_s0, _s1, _s2, _s3, _s4, _s5, _s6, _s7,
	s0to1, s0to2, s0to3, s0to4, s0to5, s0to6, s0to7,
	s6to7,
	d0m1, d2m3, d4m5, d6m7,
	d1m0, d3m2, d5m4, d7m6,
	d0m1to2m3, d0m1to4m5, d0m1to6m7,
	d1m0to3m2, d1m0to5m4, d1m0to7m6,
};

static struct attribute *max11608_device_attrs[] = {
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_dev_attr_in2_raw.dev_attr.attr,
	&iio_dev_attr_in3_raw.dev_attr.attr,
	&iio_dev_attr_in4_raw.dev_attr.attr,
	&iio_dev_attr_in5_raw.dev_attr.attr,
	&iio_dev_attr_in6_raw.dev_attr.attr,
	&iio_dev_attr_in7_raw.dev_attr.attr,
	&iio_dev_attr_in0min1_raw.dev_attr.attr,
	&iio_dev_attr_in2min3_raw.dev_attr.attr,
	&iio_dev_attr_in4min5_raw.dev_attr.attr,
	&iio_dev_attr_in6min7_raw.dev_attr.attr,
	&iio_dev_attr_in1min0_raw.dev_attr.attr,
	&iio_dev_attr_in3min2_raw.dev_attr.attr,
	&iio_dev_attr_in5min4_raw.dev_attr.attr,
	&iio_dev_attr_in7min6_raw.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	&iio_dev_attr_in_scale.dev_attr.attr,
	NULL
};

static struct attribute_group max11608_dev_attr_group = {
	.attrs = max11608_device_attrs,
};

static struct attribute *max11608_scan_el_attrs[] = {
	&iio_scan_el_in0.dev_attr.attr,
	&iio_scan_el_in1.dev_attr.attr,
	&iio_scan_el_in2.dev_attr.attr,
	&iio_scan_el_in3.dev_attr.attr,
	&iio_scan_el_in4.dev_attr.attr,
	&iio_scan_el_in5.dev_attr.attr,
	&iio_scan_el_in6.dev_attr.attr,
	&iio_scan_el_in7.dev_attr.attr,
	&iio_scan_el_in0min1.dev_attr.attr,
	&iio_scan_el_in2min3.dev_attr.attr,
	&iio_scan_el_in4min5.dev_attr.attr,
	&iio_scan_el_in6min7.dev_attr.attr,
	&iio_scan_el_in1min0.dev_attr.attr,
	&iio_scan_el_in3min2.dev_attr.attr,
	&iio_scan_el_in5min4.dev_attr.attr,
	&iio_scan_el_in7min6.dev_attr.attr,
	&iio_dev_attr_in_precision.dev_attr.attr,
};

static struct attribute_group max11608_scan_el_group = {
	.name = "scan_elements",
	.attrs = max11608_scan_el_attrs,
};

enum { max1361,
       max1362,
       max1363,
       max1364,
       max1036,
       max1037,
       max1038,
       max1039,
       max1136,
       max1137,
       max1138,
       max1139,
       max1236,
       max1237,
       max1238,
       max1239,
       max11600,
       max11601,
       max11602,
       max11603,
       max11604,
       max11605,
       max11606,
       max11607,
       max11608,
       max11609,
       max11610,
       max11611,
       max11612,
       max11613,
       max11614,
       max11615,
       max11616,
       max11617,
};

/* max1363 and max1368 tested - rest from data sheet */
static const struct max1363_chip_info max1363_chip_info_tbl[] = {
	{
		.name = "max1361",
		.num_inputs = 4,
		.bits = 10,
		.int_vref_mv = 2048,
		.monitor_mode = 1,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max1362",
		.num_inputs = 4,
		.bits = 10,
		.int_vref_mv = 4096,
		.monitor_mode = 1,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max1363",
		.num_inputs = 4,
		.bits = 12,
		.int_vref_mv = 2048,
		.monitor_mode = 1,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max1364",
		.num_inputs = 4,
		.bits = 12,
		.int_vref_mv = 4096,
		.monitor_mode = 1,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max1036",
		.num_inputs = 4,
		.bits = 8,
		.int_vref_mv = 4096,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max1037",
		.num_inputs = 4,
		.bits = 8,
		.int_vref_mv = 2048,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max1038",
		.num_inputs = 12,
		.bits = 8,
		.int_vref_mv = 4096,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}, {
		.name = "max1039",
		.num_inputs = 12,
		.bits = 8,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}, {
		.name = "max1136",
		.num_inputs = 4,
		.bits = 10,
		.int_vref_mv = 4096,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max1137",
		.num_inputs = 4,
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max1138",
		.num_inputs = 12,
		.bits = 10,
		.int_vref_mv = 4096,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}, {
		.name = "max1139",
		.num_inputs = 12,
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}, {
		.name = "max1236",
		.num_inputs = 4,
		.bits = 12,
		.int_vref_mv = 4096,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max1237",
		.num_inputs = 4,
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max1238",
		.num_inputs = 12,
		.bits = 12,
		.int_vref_mv = 4096,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}, {
		.name = "max1239",
		.num_inputs = 12,
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}, {
		.name = "max11600",
		.num_inputs = 4,
		.bits = 8,
		.int_vref_mv = 4096,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max11601",
		.num_inputs = 4,
		.bits = 8,
		.int_vref_mv = 2048,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max11602",
		.num_inputs = 8,
		.bits = 8,
		.int_vref_mv = 4096,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.dev_attrs = &max11608_dev_attr_group,
		.scan_attrs = &max11608_scan_el_group,
	}, {
		.name = "max11603",
		.num_inputs = 8,
		.bits = 8,
		.int_vref_mv = 2048,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.dev_attrs = &max11608_dev_attr_group,
		.scan_attrs = &max11608_scan_el_group,
	}, {
		.name = "max11604",
		.num_inputs = 12,
		.bits = 8,
		.int_vref_mv = 4098,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}, {
		.name = "max11605",
		.num_inputs = 12,
		.bits = 8,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}, {
		.name = "max11606",
		.num_inputs = 4,
		.bits = 10,
		.int_vref_mv = 4096,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max11607",
		.num_inputs = 4,
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max11608",
		.num_inputs = 8,
		.bits = 10,
		.int_vref_mv = 4096,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.dev_attrs = &max11608_dev_attr_group,
		.scan_attrs = &max11608_scan_el_group,
	}, {
		.name = "max11609",
		.num_inputs = 8,
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.dev_attrs = &max11608_dev_attr_group,
		.scan_attrs = &max11608_scan_el_group,
	}, {
		.name = "max11610",
		.num_inputs = 12,
		.bits = 10,
		.int_vref_mv = 4098,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}, {
		.name = "max11611",
		.num_inputs = 12,
		.bits = 10,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}, {
		.name = "max11612",
		.num_inputs = 4,
		.bits = 12,
		.int_vref_mv = 4096,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max11613",
		.num_inputs = 4,
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max11607_mode_list,
		.num_modes = ARRAY_SIZE(max11607_mode_list),
		.default_mode = s0to3,
		.dev_attrs = &max1363_dev_attr_group,
		.scan_attrs = &max1363_scan_el_group,
	}, {
		.name = "max11614",
		.num_inputs = 8,
		.bits = 12,
		.int_vref_mv = 4096,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.dev_attrs = &max11608_dev_attr_group,
		.scan_attrs = &max11608_scan_el_group,
	}, {
		.name = "max11615",
		.num_inputs = 8,
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max11608_mode_list,
		.num_modes = ARRAY_SIZE(max11608_mode_list),
		.default_mode = s0to7,
		.dev_attrs = &max11608_dev_attr_group,
		.scan_attrs = &max11608_scan_el_group,
	}, {
		.name = "max11616",
		.num_inputs = 12,
		.bits = 12,
		.int_vref_mv = 4098,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}, {
		.name = "max11617",
		.num_inputs = 12,
		.bits = 12,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
		.dev_attrs = &max1238_dev_attr_group,
		.scan_attrs = &max1238_scan_el_group,
	}
};

static int max1363_initial_setup(struct max1363_state *st)
{
	st->setupbyte = MAX1363_SETUP_AIN3_IS_AIN3_REF_IS_VDD
		| MAX1363_SETUP_POWER_UP_INT_REF
		| MAX1363_SETUP_INT_CLOCK
		| MAX1363_SETUP_UNIPOLAR
		| MAX1363_SETUP_NORESET;

	/* Set scan mode writes the config anyway so wait until then*/
	st->setupbyte = MAX1363_SETUP_BYTE(st->setupbyte);
	st->current_mode = &max1363_mode_table[st->chip_info->default_mode];
	st->configbyte = MAX1363_CONFIG_BYTE(st->configbyte);

	return max1363_set_scan_mode(st);
}

static int __devinit max1363_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	int ret, i, regdone = 0;
	struct max1363_state *st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, st);

	atomic_set(&st->protect_ring, 0);

	/* Find the chip model specific data */
	for (i = 0; i < ARRAY_SIZE(max1363_chip_info_tbl); i++)
		if (!strcmp(max1363_chip_info_tbl[i].name, id->name)) {
			st->chip_info = &max1363_chip_info_tbl[i];
			break;
		};
	/* Unsupported chip */
	if (!st->chip_info) {
		dev_err(&client->dev, "%s is not supported\n", id->name);
		ret = -ENODEV;
		goto error_free_st;
	}

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

	st->indio_dev->available_scan_masks
		= kzalloc(sizeof(*st->indio_dev->available_scan_masks)*
			  (st->chip_info->num_modes + 1), GFP_KERNEL);
	if (!st->indio_dev->available_scan_masks) {
		ret = -ENOMEM;
		goto error_free_device;
	}

	for (i = 0; i < st->chip_info->num_modes; i++)
		st->indio_dev->available_scan_masks[i] =
			max1363_mode_table[st->chip_info->mode_list[i]]
			.modemask;
	/* Estabilish that the iio_dev is a child of the i2c device */
	st->indio_dev->dev.parent = &client->dev;
	st->indio_dev->attrs = st->chip_info->dev_attrs;

	/* Todo: this shouldn't be here. */
	st->indio_dev->scan_el_attrs = st->chip_info->scan_attrs;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = max1363_initial_setup(st);
	if (ret)
		goto error_free_available_scan_masks;

	ret = max1363_register_ring_funcs_and_init(st->indio_dev);
	if (ret)
		goto error_free_available_scan_masks;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_cleanup_ring;
	regdone = 1;
	ret = max1363_initialize_ring(st->indio_dev->ring);
	if (ret)
		goto error_cleanup_ring;
	return 0;
error_cleanup_ring:
	max1363_ring_cleanup(st->indio_dev);
error_free_available_scan_masks:
	kfree(st->indio_dev->available_scan_masks);
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
error_free_st:
	kfree(st);

error_ret:
	return ret;
}

static int max1363_remove(struct i2c_client *client)
{
	struct max1363_state *st = i2c_get_clientdata(client);
	struct iio_dev *indio_dev = st->indio_dev;
	max1363_uninitialize_ring(indio_dev->ring);
	max1363_ring_cleanup(indio_dev);
	kfree(st->indio_dev->available_scan_masks);
	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	kfree(st);

	return 0;
}

static const struct i2c_device_id max1363_id[] = {
	{ "max1361", max1361 },
	{ "max1362", max1362 },
	{ "max1363", max1363 },
	{ "max1364", max1364 },
	{ "max1036", max1036 },
	{ "max1037", max1037 },
	{ "max1038", max1038 },
	{ "max1039", max1039 },
	{ "max1136", max1136 },
	{ "max1137", max1137 },
	{ "max1138", max1138 },
	{ "max1139", max1139 },
	{ "max1236", max1236 },
	{ "max1237", max1237 },
	{ "max1238", max1238 },
	{ "max1239", max1239 },
	{ "max11600", max11600 },
	{ "max11601", max11601 },
	{ "max11602", max11602 },
	{ "max11603", max11603 },
	{ "max11604", max11604 },
	{ "max11605", max11605 },
	{ "max11606", max11606 },
	{ "max11607", max11607 },
	{ "max11608", max11608 },
	{ "max11609", max11609 },
	{ "max11610", max11610 },
	{ "max11611", max11611 },
	{ "max11612", max11612 },
	{ "max11613", max11613 },
	{ "max11614", max11614 },
	{ "max11615", max11615 },
	{ "max11616", max11616 },
	{ "max11617", max11617 },
	{}
};

MODULE_DEVICE_TABLE(i2c, max1363_id);

static struct i2c_driver max1363_driver = {
	.driver = {
		.name = "max1363",
	},
	.probe = max1363_probe,
	.remove = max1363_remove,
	.id_table = max1363_id,
};

static __init int max1363_init(void)
{
	return i2c_add_driver(&max1363_driver);
}

static __exit void max1363_exit(void)
{
	i2c_del_driver(&max1363_driver);
}

MODULE_AUTHOR("Jonathan Cameron <jic23@cam.ac.uk>");
MODULE_DESCRIPTION("Maxim 1363 ADC");
MODULE_LICENSE("GPL v2");

module_init(max1363_init);
module_exit(max1363_exit);
