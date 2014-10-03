/*
 * STMicroelectronics magnetometers driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>

#include <linux/iio/common/st_sensors.h>
#include "st_magn.h"

#define ST_MAGN_NUMBER_DATA_CHANNELS		3

/* DEFAULT VALUE FOR SENSORS */
#define ST_MAGN_DEFAULT_OUT_X_H_ADDR		0X03
#define ST_MAGN_DEFAULT_OUT_Y_H_ADDR		0X07
#define ST_MAGN_DEFAULT_OUT_Z_H_ADDR		0X05

/* FULLSCALE */
#define ST_MAGN_FS_AVL_1300MG			1300
#define ST_MAGN_FS_AVL_1900MG			1900
#define ST_MAGN_FS_AVL_2500MG			2500
#define ST_MAGN_FS_AVL_4000MG			4000
#define ST_MAGN_FS_AVL_4700MG			4700
#define ST_MAGN_FS_AVL_5600MG			5600
#define ST_MAGN_FS_AVL_8000MG			8000
#define ST_MAGN_FS_AVL_8100MG			8100
#define ST_MAGN_FS_AVL_10000MG			10000

/* CUSTOM VALUES FOR SENSOR 1 */
#define ST_MAGN_1_WAI_EXP			0x3c
#define ST_MAGN_1_ODR_ADDR			0x00
#define ST_MAGN_1_ODR_MASK			0x1c
#define ST_MAGN_1_ODR_AVL_1HZ_VAL		0x00
#define ST_MAGN_1_ODR_AVL_2HZ_VAL		0x01
#define ST_MAGN_1_ODR_AVL_3HZ_VAL		0x02
#define ST_MAGN_1_ODR_AVL_8HZ_VAL		0x03
#define ST_MAGN_1_ODR_AVL_15HZ_VAL		0x04
#define ST_MAGN_1_ODR_AVL_30HZ_VAL		0x05
#define ST_MAGN_1_ODR_AVL_75HZ_VAL		0x06
#define ST_MAGN_1_ODR_AVL_220HZ_VAL		0x07
#define ST_MAGN_1_PW_ADDR			0x02
#define ST_MAGN_1_PW_MASK			0x03
#define ST_MAGN_1_PW_ON				0x00
#define ST_MAGN_1_PW_OFF			0x03
#define ST_MAGN_1_FS_ADDR			0x01
#define ST_MAGN_1_FS_MASK			0xe0
#define ST_MAGN_1_FS_AVL_1300_VAL		0x01
#define ST_MAGN_1_FS_AVL_1900_VAL		0x02
#define ST_MAGN_1_FS_AVL_2500_VAL		0x03
#define ST_MAGN_1_FS_AVL_4000_VAL		0x04
#define ST_MAGN_1_FS_AVL_4700_VAL		0x05
#define ST_MAGN_1_FS_AVL_5600_VAL		0x06
#define ST_MAGN_1_FS_AVL_8100_VAL		0x07
#define ST_MAGN_1_FS_AVL_1300_GAIN_XY		1100
#define ST_MAGN_1_FS_AVL_1900_GAIN_XY		855
#define ST_MAGN_1_FS_AVL_2500_GAIN_XY		670
#define ST_MAGN_1_FS_AVL_4000_GAIN_XY		450
#define ST_MAGN_1_FS_AVL_4700_GAIN_XY		400
#define ST_MAGN_1_FS_AVL_5600_GAIN_XY		330
#define ST_MAGN_1_FS_AVL_8100_GAIN_XY		230
#define ST_MAGN_1_FS_AVL_1300_GAIN_Z		980
#define ST_MAGN_1_FS_AVL_1900_GAIN_Z		760
#define ST_MAGN_1_FS_AVL_2500_GAIN_Z		600
#define ST_MAGN_1_FS_AVL_4000_GAIN_Z		400
#define ST_MAGN_1_FS_AVL_4700_GAIN_Z		355
#define ST_MAGN_1_FS_AVL_5600_GAIN_Z		295
#define ST_MAGN_1_FS_AVL_8100_GAIN_Z		205
#define ST_MAGN_1_MULTIREAD_BIT			false

/* CUSTOM VALUES FOR SENSOR 2 */
#define ST_MAGN_2_WAI_EXP			0x3d
#define ST_MAGN_2_ODR_ADDR			0x20
#define ST_MAGN_2_ODR_MASK			0x1c
#define ST_MAGN_2_ODR_AVL_1HZ_VAL		0x00
#define ST_MAGN_2_ODR_AVL_2HZ_VAL		0x01
#define ST_MAGN_2_ODR_AVL_3HZ_VAL		0x02
#define ST_MAGN_2_ODR_AVL_5HZ_VAL		0x03
#define ST_MAGN_2_ODR_AVL_10HZ_VAL		0x04
#define ST_MAGN_2_ODR_AVL_20HZ_VAL		0x05
#define ST_MAGN_2_ODR_AVL_40HZ_VAL		0x06
#define ST_MAGN_2_ODR_AVL_80HZ_VAL		0x07
#define ST_MAGN_2_PW_ADDR			0x22
#define ST_MAGN_2_PW_MASK			0x03
#define ST_MAGN_2_PW_ON				0x00
#define ST_MAGN_2_PW_OFF			0x03
#define ST_MAGN_2_FS_ADDR			0x21
#define ST_MAGN_2_FS_MASK			0x60
#define ST_MAGN_2_FS_AVL_4000_VAL		0x00
#define ST_MAGN_2_FS_AVL_8000_VAL		0x01
#define ST_MAGN_2_FS_AVL_10000_VAL		0x02
#define ST_MAGN_2_FS_AVL_4000_GAIN		430
#define ST_MAGN_2_FS_AVL_8000_GAIN		230
#define ST_MAGN_2_FS_AVL_10000_GAIN		230
#define ST_MAGN_2_MULTIREAD_BIT			false
#define ST_MAGN_2_OUT_X_L_ADDR			0x28
#define ST_MAGN_2_OUT_Y_L_ADDR			0x2a
#define ST_MAGN_2_OUT_Z_L_ADDR			0x2c

static const struct iio_chan_spec st_magn_16bit_channels[] = {
	ST_SENSORS_LSM_CHANNELS(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 1, IIO_MOD_X, 's', IIO_BE, 16, 16,
			ST_MAGN_DEFAULT_OUT_X_H_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Y, 1, IIO_MOD_Y, 's', IIO_BE, 16, 16,
			ST_MAGN_DEFAULT_OUT_Y_H_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Z, 1, IIO_MOD_Z, 's', IIO_BE, 16, 16,
			ST_MAGN_DEFAULT_OUT_Z_H_ADDR),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct iio_chan_spec st_magn_2_16bit_channels[] = {
	ST_SENSORS_LSM_CHANNELS(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 1, IIO_MOD_X, 's', IIO_LE, 16, 16,
			ST_MAGN_2_OUT_X_L_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Y, 1, IIO_MOD_Y, 's', IIO_LE, 16, 16,
			ST_MAGN_2_OUT_Y_L_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Z, 1, IIO_MOD_Z, 's', IIO_LE, 16, 16,
			ST_MAGN_2_OUT_Z_L_ADDR),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct st_sensor_settings st_magn_sensors_settings[] = {
	{
		.wai = ST_MAGN_1_WAI_EXP,
		.sensors_supported = {
			[0] = LSM303DLHC_MAGN_DEV_NAME,
			[1] = LSM303DLM_MAGN_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_magn_16bit_channels,
		.odr = {
			.addr = ST_MAGN_1_ODR_ADDR,
			.mask = ST_MAGN_1_ODR_MASK,
			.odr_avl = {
				{ 1, ST_MAGN_1_ODR_AVL_1HZ_VAL, },
				{ 2, ST_MAGN_1_ODR_AVL_2HZ_VAL, },
				{ 3, ST_MAGN_1_ODR_AVL_3HZ_VAL, },
				{ 8, ST_MAGN_1_ODR_AVL_8HZ_VAL, },
				{ 15, ST_MAGN_1_ODR_AVL_15HZ_VAL, },
				{ 30, ST_MAGN_1_ODR_AVL_30HZ_VAL, },
				{ 75, ST_MAGN_1_ODR_AVL_75HZ_VAL, },
				{ 220, ST_MAGN_1_ODR_AVL_220HZ_VAL, },
			},
		},
		.pw = {
			.addr = ST_MAGN_1_PW_ADDR,
			.mask = ST_MAGN_1_PW_MASK,
			.value_on = ST_MAGN_1_PW_ON,
			.value_off = ST_MAGN_1_PW_OFF,
		},
		.fs = {
			.addr = ST_MAGN_1_FS_ADDR,
			.mask = ST_MAGN_1_FS_MASK,
			.fs_avl = {
				[0] = {
					.num = ST_MAGN_FS_AVL_1300MG,
					.value = ST_MAGN_1_FS_AVL_1300_VAL,
					.gain = ST_MAGN_1_FS_AVL_1300_GAIN_XY,
					.gain2 = ST_MAGN_1_FS_AVL_1300_GAIN_Z,
				},
				[1] = {
					.num = ST_MAGN_FS_AVL_1900MG,
					.value = ST_MAGN_1_FS_AVL_1900_VAL,
					.gain = ST_MAGN_1_FS_AVL_1900_GAIN_XY,
					.gain2 = ST_MAGN_1_FS_AVL_1900_GAIN_Z,
				},
				[2] = {
					.num = ST_MAGN_FS_AVL_2500MG,
					.value = ST_MAGN_1_FS_AVL_2500_VAL,
					.gain = ST_MAGN_1_FS_AVL_2500_GAIN_XY,
					.gain2 = ST_MAGN_1_FS_AVL_2500_GAIN_Z,
				},
				[3] = {
					.num = ST_MAGN_FS_AVL_4000MG,
					.value = ST_MAGN_1_FS_AVL_4000_VAL,
					.gain = ST_MAGN_1_FS_AVL_4000_GAIN_XY,
					.gain2 = ST_MAGN_1_FS_AVL_4000_GAIN_Z,
				},
				[4] = {
					.num = ST_MAGN_FS_AVL_4700MG,
					.value = ST_MAGN_1_FS_AVL_4700_VAL,
					.gain = ST_MAGN_1_FS_AVL_4700_GAIN_XY,
					.gain2 = ST_MAGN_1_FS_AVL_4700_GAIN_Z,
				},
				[5] = {
					.num = ST_MAGN_FS_AVL_5600MG,
					.value = ST_MAGN_1_FS_AVL_5600_VAL,
					.gain = ST_MAGN_1_FS_AVL_5600_GAIN_XY,
					.gain2 = ST_MAGN_1_FS_AVL_5600_GAIN_Z,
				},
				[6] = {
					.num = ST_MAGN_FS_AVL_8100MG,
					.value = ST_MAGN_1_FS_AVL_8100_VAL,
					.gain = ST_MAGN_1_FS_AVL_8100_GAIN_XY,
					.gain2 = ST_MAGN_1_FS_AVL_8100_GAIN_Z,
				},
			},
		},
		.multi_read_bit = ST_MAGN_1_MULTIREAD_BIT,
		.bootime = 2,
	},
	{
		.wai = ST_MAGN_2_WAI_EXP,
		.sensors_supported = {
			[0] = LIS3MDL_MAGN_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_magn_2_16bit_channels,
		.odr = {
			.addr = ST_MAGN_2_ODR_ADDR,
			.mask = ST_MAGN_2_ODR_MASK,
			.odr_avl = {
				{ 1, ST_MAGN_2_ODR_AVL_1HZ_VAL, },
				{ 2, ST_MAGN_2_ODR_AVL_2HZ_VAL, },
				{ 3, ST_MAGN_2_ODR_AVL_3HZ_VAL, },
				{ 5, ST_MAGN_2_ODR_AVL_5HZ_VAL, },
				{ 10, ST_MAGN_2_ODR_AVL_10HZ_VAL, },
				{ 20, ST_MAGN_2_ODR_AVL_20HZ_VAL, },
				{ 40, ST_MAGN_2_ODR_AVL_40HZ_VAL, },
				{ 80, ST_MAGN_2_ODR_AVL_80HZ_VAL, },
			},
		},
		.pw = {
			.addr = ST_MAGN_2_PW_ADDR,
			.mask = ST_MAGN_2_PW_MASK,
			.value_on = ST_MAGN_2_PW_ON,
			.value_off = ST_MAGN_2_PW_OFF,
		},
		.fs = {
			.addr = ST_MAGN_2_FS_ADDR,
			.mask = ST_MAGN_2_FS_MASK,
			.fs_avl = {
				[0] = {
					.num = ST_MAGN_FS_AVL_4000MG,
					.value = ST_MAGN_2_FS_AVL_4000_VAL,
					.gain = ST_MAGN_2_FS_AVL_4000_GAIN,
				},
				[1] = {
					.num = ST_MAGN_FS_AVL_8000MG,
					.value = ST_MAGN_2_FS_AVL_8000_VAL,
					.gain = ST_MAGN_2_FS_AVL_8000_GAIN,
				},
				[2] = {
					.num = ST_MAGN_FS_AVL_10000MG,
					.value = ST_MAGN_2_FS_AVL_10000_VAL,
					.gain = ST_MAGN_2_FS_AVL_10000_GAIN,
				},
			},
		},
		.multi_read_bit = ST_MAGN_2_MULTIREAD_BIT,
		.bootime = 2,
	},
};

static int st_magn_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *ch, int *val,
							int *val2, long mask)
{
	int err;
	struct st_sensor_data *mdata = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = st_sensors_read_info_raw(indio_dev, ch, val);
		if (err < 0)
			goto read_error;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		if ((ch->scan_index == ST_SENSORS_SCAN_Z) &&
					(mdata->current_fullscale->gain2 != 0))
			*val2 = mdata->current_fullscale->gain2;
		else
			*val2 = mdata->current_fullscale->gain;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = mdata->odr;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}

read_error:
	return err;
}

static int st_magn_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = st_sensors_set_fullscale_by_gain(indio_dev, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val2)
			return -EINVAL;
		mutex_lock(&indio_dev->mlock);
		err = st_sensors_set_odr(indio_dev, val);
		mutex_unlock(&indio_dev->mlock);
		return err;
	default:
		err = -EINVAL;
	}

	return err;
}

static ST_SENSORS_DEV_ATTR_SAMP_FREQ_AVAIL();
static ST_SENSORS_DEV_ATTR_SCALE_AVAIL(in_magn_scale_available);

static struct attribute *st_magn_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_magn_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_magn_attribute_group = {
	.attrs = st_magn_attributes,
};

static const struct iio_info magn_info = {
	.driver_module = THIS_MODULE,
	.attrs = &st_magn_attribute_group,
	.read_raw = &st_magn_read_raw,
	.write_raw = &st_magn_write_raw,
};

int st_magn_common_probe(struct iio_dev *indio_dev,
					struct st_sensors_platform_data *pdata)
{
	struct st_sensor_data *mdata = iio_priv(indio_dev);
	int irq = mdata->get_irq_data_ready(indio_dev);
	int err;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &magn_info;

	st_sensors_power_enable(indio_dev);

	err = st_sensors_check_device_support(indio_dev,
					ARRAY_SIZE(st_magn_sensors_settings),
					st_magn_sensors_settings);
	if (err < 0)
		return err;

	mdata->num_data_channels = ST_MAGN_NUMBER_DATA_CHANNELS;
	mdata->multiread_bit = mdata->sensor_settings->multi_read_bit;
	indio_dev->channels = mdata->sensor_settings->ch;
	indio_dev->num_channels = ST_SENSORS_NUMBER_ALL_CHANNELS;

	mdata->current_fullscale = (struct st_sensor_fullscale_avl *)
					&mdata->sensor_settings->fs.fs_avl[0];
	mdata->odr = mdata->sensor_settings->odr.odr_avl[0].hz;

	err = st_sensors_init_sensor(indio_dev, pdata);
	if (err < 0)
		return err;

	err = st_magn_allocate_ring(indio_dev);
	if (err < 0)
		return err;

	if (irq > 0) {
		err = st_sensors_allocate_trigger(indio_dev, NULL);
		if (err < 0)
			goto st_magn_probe_trigger_error;
	}

	err = iio_device_register(indio_dev);
	if (err)
		goto st_magn_device_register_error;

	dev_info(&indio_dev->dev, "registered magnetometer %s\n",
		 indio_dev->name);

	return 0;

st_magn_device_register_error:
	if (irq > 0)
		st_sensors_deallocate_trigger(indio_dev);
st_magn_probe_trigger_error:
	st_magn_deallocate_ring(indio_dev);

	return err;
}
EXPORT_SYMBOL(st_magn_common_probe);

void st_magn_common_remove(struct iio_dev *indio_dev)
{
	struct st_sensor_data *mdata = iio_priv(indio_dev);

	st_sensors_power_disable(indio_dev);

	iio_device_unregister(indio_dev);
	if (mdata->get_irq_data_ready(indio_dev) > 0)
		st_sensors_deallocate_trigger(indio_dev);

	st_magn_deallocate_ring(indio_dev);
}
EXPORT_SYMBOL(st_magn_common_remove);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics magnetometers driver");
MODULE_LICENSE("GPL v2");
