 /*
 * linux/drivers/industrialio/adc/max1363.c
 * Copyright (C) 2008 Jonathan Cameron
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
 * - Sysfs scan interface currently assumes unipolar mode.
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

#include "../iio.h"
#include "../sysfs.h"

#include "max1363.h"

/* Available scan modes.
 * Awkwardly the associated enum is in the header so it is available to
 * the ring buffer code.
 */
static const struct  max1363_mode max1363_mode_table[] = {
	MAX1363_MODE_SINGLE(0),
	MAX1363_MODE_SINGLE(1),
	MAX1363_MODE_SINGLE(2),
	MAX1363_MODE_SINGLE(3),
	MAX1363_MODE_SINGLE(4),
	MAX1363_MODE_SINGLE(5),
	MAX1363_MODE_SINGLE(6),
	MAX1363_MODE_SINGLE(7),
	MAX1363_MODE_SINGLE(8),
	MAX1363_MODE_SINGLE(9),
	MAX1363_MODE_SINGLE(10),
	MAX1363_MODE_SINGLE(11),

	MAX1363_MODE_SINGLE_TIMES_8(0),
	MAX1363_MODE_SINGLE_TIMES_8(1),
	MAX1363_MODE_SINGLE_TIMES_8(2),
	MAX1363_MODE_SINGLE_TIMES_8(3),
	MAX1363_MODE_SINGLE_TIMES_8(4),
	MAX1363_MODE_SINGLE_TIMES_8(5),
	MAX1363_MODE_SINGLE_TIMES_8(6),
	MAX1363_MODE_SINGLE_TIMES_8(7),
	MAX1363_MODE_SINGLE_TIMES_8(8),
	MAX1363_MODE_SINGLE_TIMES_8(9),
	MAX1363_MODE_SINGLE_TIMES_8(10),
	MAX1363_MODE_SINGLE_TIMES_8(11),

	MAX1363_MODE_SCAN_TO_CHANNEL(1),
	MAX1363_MODE_SCAN_TO_CHANNEL(2),
	MAX1363_MODE_SCAN_TO_CHANNEL(3),
	MAX1363_MODE_SCAN_TO_CHANNEL(4),
	MAX1363_MODE_SCAN_TO_CHANNEL(5),
	MAX1363_MODE_SCAN_TO_CHANNEL(6),
	MAX1363_MODE_SCAN_TO_CHANNEL(7),
	MAX1363_MODE_SCAN_TO_CHANNEL(8),
	MAX1363_MODE_SCAN_TO_CHANNEL(9),
	MAX1363_MODE_SCAN_TO_CHANNEL(10),
	MAX1363_MODE_SCAN_TO_CHANNEL(11),

	MAX1363_MODE_DIFF_SINGLE(0, 1),
	MAX1363_MODE_DIFF_SINGLE(2, 3),
	MAX1363_MODE_DIFF_SINGLE(4, 5),
	MAX1363_MODE_DIFF_SINGLE(6, 7),
	MAX1363_MODE_DIFF_SINGLE(8, 9),
	MAX1363_MODE_DIFF_SINGLE(10, 11),
	MAX1363_MODE_DIFF_SINGLE(1, 0),
	MAX1363_MODE_DIFF_SINGLE(3, 2),
	MAX1363_MODE_DIFF_SINGLE(5, 4),
	MAX1363_MODE_DIFF_SINGLE(7, 6),
	MAX1363_MODE_DIFF_SINGLE(9, 8),
	MAX1363_MODE_DIFF_SINGLE(11, 10),

	MAX1363_MODE_DIFF_SINGLE_TIMES_8(0, 1),
	MAX1363_MODE_DIFF_SINGLE_TIMES_8(2, 3),
	MAX1363_MODE_DIFF_SINGLE_TIMES_8(4, 5),
	MAX1363_MODE_DIFF_SINGLE_TIMES_8(6, 7),
	MAX1363_MODE_DIFF_SINGLE_TIMES_8(8, 9),
	MAX1363_MODE_DIFF_SINGLE_TIMES_8(10, 11),
	MAX1363_MODE_DIFF_SINGLE_TIMES_8(1, 0),
	MAX1363_MODE_DIFF_SINGLE_TIMES_8(3, 2),
	MAX1363_MODE_DIFF_SINGLE_TIMES_8(5, 4),
	MAX1363_MODE_DIFF_SINGLE_TIMES_8(7, 6),
	MAX1363_MODE_DIFF_SINGLE_TIMES_8(9, 8),
	MAX1363_MODE_DIFF_SINGLE_TIMES_8(11, 10),

	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL_NAMED(0-1...2-3, 2, 2),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL_NAMED(0-1...4-5, 4, 3),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL_NAMED(0-1...6-7, 6, 4),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL_NAMED(0-1...8-9, 8, 5),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL_NAMED(0-1...10-11, 10, 6),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL_NAMED(1-0...3-2, 3, 2),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL_NAMED(1-0...5-4, 5, 3),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL_NAMED(1-0...7-6, 7, 4),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL_NAMED(1-0...9-8, 9, 5),
	MAX1363_MODE_DIFF_SCAN_TO_CHANNEL_NAMED(1-0...11-10, 11, 6),

	MAX1236_MODE_SCAN_MID_TO_CHANNEL(2, 3),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 7),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 8),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 9),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 10),
	MAX1236_MODE_SCAN_MID_TO_CHANNEL(6, 11),

	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL_NAMED(6-7...8-9, 8, 2),
	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL_NAMED(6-7...10-11, 10, 3),
	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL_NAMED(7-6...9-8, 9, 2),
	MAX1236_MODE_DIFF_SCAN_MID_TO_CHANNEL_NAMED(7-6...11-10, 11, 3),
};

/* Applies to max1363 */
static const enum max1363_modes max1363_mode_list[] = {
	_s0, _s1, _s2, _s3,
	se0, se1, se2, se3,
	s0to1, s0to2, s0to3,
	d0m1, d2m3, d1m0, d3m2,
	de0m1, de2m3, de1m0, de3m2,
	d0m1to2m3, d1m0to3m2,
};

/* Appies to max1236, max1237 */
static const enum max1363_modes max1236_mode_list[] = {
	_s0, _s1, _s2, _s3,
	se0, se1, se2, se3,
	s0to1, s0to2, s0to3,
	d0m1, d2m3, d1m0, d3m2,
	de0m1, de2m3, de1m0, de3m2,
	d0m1to2m3, d1m0to3m2,
	s2to3,
};

/* Applies to max1238, max1239 */
static const enum max1363_modes max1238_mode_list[] = {
	_s0, _s1, _s2, _s3, _s4, _s5, _s6, _s7, _s8, _s9, _s10, _s11,
	se0, se1, se2, se3, se4, se5, se6, se7, se8, se9, se10, se11,
	s0to1, s0to2, s0to3, s0to4, s0to5, s0to6,
	s0to7, s0to8, s0to9, s0to10, s0to11,
	d0m1, d2m3, d4m5, d6m7, d8m9, d10m11,
	d1m0, d3m2, d5m4, d7m6, d9m8, d11m10,
	de0m1, de2m3, de4m5, de6m7, de8m9, de10m11,
	de1m0, de3m2, de5m4, de7m6, de9m8, de11m10,
	d0m1to2m3, d0m1to4m5, d0m1to6m7, d0m1to8m9, d0m1to10m11,
	d1m0to3m2, d1m0to5m4, d1m0to7m6, d1m0to9m8, d1m0to11m10,
	s6to7, s6to8, s6to9, s6to10, s6to11,
	s6m7to8m9, s6m7to10m11, s7m6to9m8, s7m6to11m10,
};


enum { max1361,
       max1362,
       max1363,
       max1364,
       max1136,
       max1137,
       max1138,
       max1139,
       max1236,
       max1237,
       max1238,
       max1239,
};

/* max1363 and max1368 tested - rest from data sheet */
static const struct max1363_chip_info max1363_chip_info_tbl[] = {
	{
		.name = "max1361",
		.num_inputs = 4,
		.monitor_mode = 1,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
	}, {
		.name = "max1362",
		.num_inputs = 4,
		.monitor_mode = 1,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
	}, {
		.name = "max1363",
		.num_inputs = 4,
		.monitor_mode = 1,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
	}, {
		.name = "max1364",
		.num_inputs = 4,
		.monitor_mode = 1,
		.mode_list = max1363_mode_list,
		.num_modes = ARRAY_SIZE(max1363_mode_list),
		.default_mode = s0to3,
	}, {
		.name = "max1136",
		.num_inputs = 4,
		.int_vref_mv = 4096,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
	}, {
		.name = "max1137",
		.num_inputs = 4,
		.int_vref_mv = 2048,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
	}, {
		.name = "max1138",
		.num_inputs = 12,
		.int_vref_mv = 4096,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
	}, {
		.name = "max1139",
		.num_inputs = 12,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
	}, {
		.name = "max1236",
		.num_inputs = 4,
		.int_vref_mv = 4096,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
	}, {
		.name = "max1237",
		.num_inputs = 4,
		.int_vref_mv = 2048,
		.mode_list = max1236_mode_list,
		.num_modes = ARRAY_SIZE(max1236_mode_list),
		.default_mode = s0to3,
	}, {
		.name = "max1238",
		.num_inputs = 12,
		.int_vref_mv = 4096,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
	}, {
		.name = "max1239",
		.num_inputs = 12,
		.int_vref_mv = 2048,
		.mode_list = max1238_mode_list,
		.num_modes = ARRAY_SIZE(max1238_mode_list),
		.default_mode = s0to11,
	},
};

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

static int max1363_set_scan_mode(struct max1363_state *st)
{
	st->configbyte &= ~(MAX1363_CHANNEL_SEL_MASK
			    | MAX1363_SCAN_MASK
			    | MAX1363_SE_DE_MASK);
	st->configbyte |= st->current_mode->conf;

	return max1363_write_basic_config(st->client,
					  st->setupbyte,
					  st->configbyte);
}

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

static ssize_t max1363_show_av_scan_modes(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct max1363_state *st = dev_info->dev_data;
	int i, len = 0;

	for (i = 0; i < st->chip_info->num_modes; i++)
		len += sprintf(buf + len, "%s ",
			       max1363_mode_table[st->chip_info
						  ->mode_list[i]].name);
	len += sprintf(buf + len, "\n");

	return len;
}


/* The dev here is the sysfs related one, not the underlying i2c one */
static ssize_t max1363_scan_direct(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct max1363_state *st = dev_info->dev_data;
	int len = 0, ret, i;
	struct i2c_client *client = st->client;
	char *rxbuf;

	if (st->current_mode->numvals == 0)
		return 0;
	rxbuf = kmalloc(st->current_mode->numvals*2, GFP_KERNEL);
	if (rxbuf == NULL)
		return -ENOMEM;

	/* Interpretation depends on whether these are signed or not!*/
	/* Assume not for now */
	ret = i2c_master_recv(client, rxbuf, st->current_mode->numvals*2);

	if (ret < 0)
		return ret;
	for (i = 0; i < st->current_mode->numvals; i++)
		len += sprintf(buf+len, "%d ",
			       ((int)(rxbuf[i*2+0]&0x0F) << 8)
			       + ((int)(rxbuf[i*2+1])));
	kfree(rxbuf);
	len += sprintf(buf + len, "\n");

	return len;
}

static ssize_t max1363_scan(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&dev_info->mlock);
	if (dev_info->currentmode == INDIO_RING_TRIGGERED)
		ret = max1363_scan_from_ring(dev, attr, buf);
	else
		ret = max1363_scan_direct(dev, attr, buf);
	mutex_unlock(&dev_info->mlock);

	return ret;
}

/* Cannot query the device, so use local copy of state */
static ssize_t max1363_show_scan_mode(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct max1363_state *st = dev_info->dev_data;

	return sprintf(buf, "%s\n", st->current_mode->name);
}

static const struct max1363_mode
*__max1363_find_mode_in_ci(const struct max1363_chip_info *info,
				  const char *buf)
{
	int i;
	for (i = 0; i <  info->num_modes; i++)
		if (strcmp(max1363_mode_table[info->mode_list[i]].name, buf)
		    == 0)
			return &max1363_mode_table[info->mode_list[i]];
	return NULL;
}

static ssize_t max1363_store_scan_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct max1363_state *st = dev_info->dev_data;
	const struct max1363_mode *new_mode;
	int ret;

	mutex_lock(&dev_info->mlock);
	new_mode = NULL;
	/* Avoid state changes if a ring buffer is enabled */
	if (!iio_ring_enabled(dev_info)) {
		new_mode
			= __max1363_find_mode_in_ci(st->chip_info, buf);
		if (!new_mode) {
			ret = -EINVAL;
			goto error_ret;
		}
		st->current_mode = new_mode;
		ret =  max1363_set_scan_mode(st);
		if (ret)
			goto error_ret;
	} else {
		ret = -EBUSY;
		goto error_ret;
	}
	mutex_unlock(&dev_info->mlock);

	return len;

error_ret:
	mutex_unlock(&dev_info->mlock);

	return ret;
}

IIO_DEV_ATTR_AVAIL_SCAN_MODES(max1363_show_av_scan_modes);
IIO_DEV_ATTR_SCAN_MODE(S_IRUGO | S_IWUSR,
		       max1363_show_scan_mode,
		       max1363_store_scan_mode);

IIO_DEV_ATTR_SCAN(max1363_scan);

static ssize_t max1363_show_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct max1363_state *st = dev_info->dev_data;
	return sprintf(buf, "%s\n", st->chip_info->name);
}

IIO_DEVICE_ATTR(name, S_IRUGO, max1363_show_name, NULL, 0);

/*name export */

static struct attribute *max1363_attributes[] = {
	&iio_dev_attr_available_scan_modes.dev_attr.attr,
	&iio_dev_attr_scan_mode.dev_attr.attr,
	&iio_dev_attr_scan.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	NULL,
};

static const struct attribute_group max1363_attribute_group = {
	.attrs = max1363_attributes,
};

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

	/* Estabilish that the iio_dev is a child of the i2c device */
	st->indio_dev->dev.parent = &client->dev;
	st->indio_dev->attrs = &max1363_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = max1363_initial_setup(st);
	if (ret)
		goto error_free_device;

	ret = max1363_register_ring_funcs_and_init(st->indio_dev);
	if (ret)
		goto error_free_device;

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
	{ "max1136", max1136 },
	{ "max1137", max1137 },
	{ "max1138", max1138 },
	{ "max1139", max1139 },
	{ "max1236", max1236 },
	{ "max1237", max1237 },
	{ "max1238", max1238 },
	{ "max1239", max1239 },
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
