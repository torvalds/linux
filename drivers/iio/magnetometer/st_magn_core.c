// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics magnetometers driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>

#include <linux/iio/common/st_sensors.h>
#include "st_magn.h"

#define ST_MAGN_NUMBER_DATA_CHANNELS		3

/* DEFAULT VALUE FOR SENSORS */
#define ST_MAGN_DEFAULT_OUT_X_H_ADDR		0x03
#define ST_MAGN_DEFAULT_OUT_Y_H_ADDR		0x07
#define ST_MAGN_DEFAULT_OUT_Z_H_ADDR		0x05

/* FULLSCALE */
#define ST_MAGN_FS_AVL_1300MG			1300
#define ST_MAGN_FS_AVL_1900MG			1900
#define ST_MAGN_FS_AVL_2000MG			2000
#define ST_MAGN_FS_AVL_2500MG			2500
#define ST_MAGN_FS_AVL_4000MG			4000
#define ST_MAGN_FS_AVL_4700MG			4700
#define ST_MAGN_FS_AVL_5600MG			5600
#define ST_MAGN_FS_AVL_8000MG			8000
#define ST_MAGN_FS_AVL_8100MG			8100
#define ST_MAGN_FS_AVL_12000MG			12000
#define ST_MAGN_FS_AVL_15000MG			15000
#define ST_MAGN_FS_AVL_16000MG			16000

/* Special L addresses for Sensor 2 */
#define ST_MAGN_2_OUT_X_L_ADDR			0x28
#define ST_MAGN_2_OUT_Y_L_ADDR			0x2a
#define ST_MAGN_2_OUT_Z_L_ADDR			0x2c

/* Special L addresses for sensor 3 */
#define ST_MAGN_3_OUT_X_L_ADDR			0x68
#define ST_MAGN_3_OUT_Y_L_ADDR			0x6a
#define ST_MAGN_3_OUT_Z_L_ADDR			0x6c

/* Special L addresses for sensor 4 */
#define ST_MAGN_4_OUT_X_L_ADDR			0x08
#define ST_MAGN_4_OUT_Y_L_ADDR			0x0a
#define ST_MAGN_4_OUT_Z_L_ADDR			0x0c

static const struct iio_mount_matrix *
st_magn_get_mount_matrix(const struct iio_dev *indio_dev,
			 const struct iio_chan_spec *chan)
{
	struct st_sensor_data *mdata = iio_priv(indio_dev);

	return &mdata->mount_matrix;
}

static const struct iio_chan_spec_ext_info st_magn_mount_matrix_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_ALL, st_magn_get_mount_matrix),
	{ }
};

static const struct iio_chan_spec st_magn_16bit_channels[] = {
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 1, IIO_MOD_X, 's', IIO_BE, 16, 16,
			ST_MAGN_DEFAULT_OUT_X_H_ADDR,
			st_magn_mount_matrix_ext_info),
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Y, 1, IIO_MOD_Y, 's', IIO_BE, 16, 16,
			ST_MAGN_DEFAULT_OUT_Y_H_ADDR,
			st_magn_mount_matrix_ext_info),
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Z, 1, IIO_MOD_Z, 's', IIO_BE, 16, 16,
			ST_MAGN_DEFAULT_OUT_Z_H_ADDR,
			st_magn_mount_matrix_ext_info),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct iio_chan_spec st_magn_2_16bit_channels[] = {
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 1, IIO_MOD_X, 's', IIO_LE, 16, 16,
			ST_MAGN_2_OUT_X_L_ADDR,
			st_magn_mount_matrix_ext_info),
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Y, 1, IIO_MOD_Y, 's', IIO_LE, 16, 16,
			ST_MAGN_2_OUT_Y_L_ADDR,
			st_magn_mount_matrix_ext_info),
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Z, 1, IIO_MOD_Z, 's', IIO_LE, 16, 16,
			ST_MAGN_2_OUT_Z_L_ADDR,
			st_magn_mount_matrix_ext_info),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct iio_chan_spec st_magn_3_16bit_channels[] = {
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 1, IIO_MOD_X, 's', IIO_LE, 16, 16,
			ST_MAGN_3_OUT_X_L_ADDR,
			st_magn_mount_matrix_ext_info),
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Y, 1, IIO_MOD_Y, 's', IIO_LE, 16, 16,
			ST_MAGN_3_OUT_Y_L_ADDR,
			st_magn_mount_matrix_ext_info),
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Z, 1, IIO_MOD_Z, 's', IIO_LE, 16, 16,
			ST_MAGN_3_OUT_Z_L_ADDR,
			st_magn_mount_matrix_ext_info),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct iio_chan_spec st_magn_4_16bit_channels[] = {
	ST_SENSORS_LSM_CHANNELS(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 1, IIO_MOD_X, 's', IIO_LE, 16, 16,
			ST_MAGN_4_OUT_X_L_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Y, 1, IIO_MOD_Y, 's', IIO_LE, 16, 16,
			ST_MAGN_4_OUT_Y_L_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_MAGN,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Z, 1, IIO_MOD_Z, 's', IIO_LE, 16, 16,
			ST_MAGN_4_OUT_Z_L_ADDR),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct st_sensor_settings st_magn_sensors_settings[] = {
	{
		.wai = 0, /* This sensor has no valid WhoAmI report 0 */
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LSM303DLH_MAGN_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_magn_16bit_channels,
		.odr = {
			.addr = 0x00,
			.mask = 0x1c,
			.odr_avl = {
				{ .hz = 1, .value = 0x00 },
				{ .hz = 2, .value = 0x01 },
				{ .hz = 3, .value = 0x02 },
				{ .hz = 8, .value = 0x03 },
				{ .hz = 15, .value = 0x04 },
				{ .hz = 30, .value = 0x05 },
				{ .hz = 75, .value = 0x06 },
				/* 220 Hz, 0x07 reportedly exist */
			},
		},
		.pw = {
			.addr = 0x02,
			.mask = 0x03,
			.value_on = 0x00,
			.value_off = 0x03,
		},
		.fs = {
			.addr = 0x01,
			.mask = 0xe0,
			.fs_avl = {
				[0] = {
					.num = ST_MAGN_FS_AVL_1300MG,
					.value = 0x01,
					.gain = 1100,
					.gain2 = 980,
				},
				[1] = {
					.num = ST_MAGN_FS_AVL_1900MG,
					.value = 0x02,
					.gain = 855,
					.gain2 = 760,
				},
				[2] = {
					.num = ST_MAGN_FS_AVL_2500MG,
					.value = 0x03,
					.gain = 670,
					.gain2 = 600,
				},
				[3] = {
					.num = ST_MAGN_FS_AVL_4000MG,
					.value = 0x04,
					.gain = 450,
					.gain2 = 400,
				},
				[4] = {
					.num = ST_MAGN_FS_AVL_4700MG,
					.value = 0x05,
					.gain = 400,
					.gain2 = 355,
				},
				[5] = {
					.num = ST_MAGN_FS_AVL_5600MG,
					.value = 0x06,
					.gain = 330,
					.gain2 = 295,
				},
				[6] = {
					.num = ST_MAGN_FS_AVL_8100MG,
					.value = 0x07,
					.gain = 230,
					.gain2 = 205,
				},
			},
		},
		.multi_read_bit = false,
		.bootime = 2,
	},
	{
		.wai = 0x3c,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LSM303DLHC_MAGN_DEV_NAME,
			[1] = LSM303DLM_MAGN_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_magn_16bit_channels,
		.odr = {
			.addr = 0x00,
			.mask = 0x1c,
			.odr_avl = {
				{ .hz = 1, .value = 0x00 },
				{ .hz = 2, .value = 0x01 },
				{ .hz = 3, .value = 0x02 },
				{ .hz = 8, .value = 0x03 },
				{ .hz = 15, .value = 0x04 },
				{ .hz = 30, .value = 0x05 },
				{ .hz = 75, .value = 0x06 },
				{ .hz = 220, .value = 0x07 },
			},
		},
		.pw = {
			.addr = 0x02,
			.mask = 0x03,
			.value_on = 0x00,
			.value_off = 0x03,
		},
		.fs = {
			.addr = 0x01,
			.mask = 0xe0,
			.fs_avl = {
				[0] = {
					.num = ST_MAGN_FS_AVL_1300MG,
					.value = 0x01,
					.gain = 909,
					.gain2 = 1020,
				},
				[1] = {
					.num = ST_MAGN_FS_AVL_1900MG,
					.value = 0x02,
					.gain = 1169,
					.gain2 = 1315,
				},
				[2] = {
					.num = ST_MAGN_FS_AVL_2500MG,
					.value = 0x03,
					.gain = 1492,
					.gain2 = 1666,
				},
				[3] = {
					.num = ST_MAGN_FS_AVL_4000MG,
					.value = 0x04,
					.gain = 2222,
					.gain2 = 2500,
				},
				[4] = {
					.num = ST_MAGN_FS_AVL_4700MG,
					.value = 0x05,
					.gain = 2500,
					.gain2 = 2816,
				},
				[5] = {
					.num = ST_MAGN_FS_AVL_5600MG,
					.value = 0x06,
					.gain = 3030,
					.gain2 = 3389,
				},
				[6] = {
					.num = ST_MAGN_FS_AVL_8100MG,
					.value = 0x07,
					.gain = 4347,
					.gain2 = 4878,
				},
			},
		},
		.multi_read_bit = false,
		.bootime = 2,
	},
	{
		.wai = 0x3d,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LIS3MDL_MAGN_DEV_NAME,
			[1] = LSM9DS1_MAGN_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_magn_2_16bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0x1c,
			.odr_avl = {
				{ .hz = 1, .value = 0x00 },
				{ .hz = 2, .value = 0x01 },
				{ .hz = 3, .value = 0x02 },
				{ .hz = 5, .value = 0x03 },
				{ .hz = 10, .value = 0x04 },
				{ .hz = 20, .value = 0x05 },
				{ .hz = 40, .value = 0x06 },
				{ .hz = 80, .value = 0x07 },
			},
		},
		.pw = {
			.addr = 0x22,
			.mask = 0x03,
			.value_on = 0x00,
			.value_off = 0x03,
		},
		.fs = {
			.addr = 0x21,
			.mask = 0x60,
			.fs_avl = {
				[0] = {
					.num = ST_MAGN_FS_AVL_4000MG,
					.value = 0x00,
					.gain = 146,
				},
				[1] = {
					.num = ST_MAGN_FS_AVL_8000MG,
					.value = 0x01,
					.gain = 292,
				},
				[2] = {
					.num = ST_MAGN_FS_AVL_12000MG,
					.value = 0x02,
					.gain = 438,
				},
				[3] = {
					.num = ST_MAGN_FS_AVL_16000MG,
					.value = 0x03,
					.gain = 584,
				},
			},
		},
		.bdu = {
			.addr = 0x24,
			.mask = 0x40,
		},
		.drdy_irq = {
			/* drdy line is routed drdy pin */
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x07,
			},
		},
		.sim = {
			.addr = 0x22,
			.value = BIT(2),
		},
		.multi_read_bit = true,
		.bootime = 2,
	},
	{
		.wai = 0x40,
		.wai_addr = 0x4f,
		.sensors_supported = {
			[0] = LSM303AGR_MAGN_DEV_NAME,
			[1] = LIS2MDL_MAGN_DEV_NAME,
			[2] = IIS2MDC_MAGN_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_magn_3_16bit_channels,
		.odr = {
			.addr = 0x60,
			.mask = 0x0c,
			.odr_avl = {
				{ .hz = 10, .value = 0x00 },
				{ .hz = 20, .value = 0x01 },
				{ .hz = 50, .value = 0x02 },
				{ .hz = 100, .value = 0x03 },
			},
		},
		.pw = {
			.addr = 0x60,
			.mask = 0x03,
			.value_on = 0x00,
			.value_off = 0x03,
		},
		.fs = {
			.fs_avl = {
				[0] = {
					.num = ST_MAGN_FS_AVL_15000MG,
					.gain = 1500,
				},
			},
		},
		.bdu = {
			.addr = 0x62,
			.mask = 0x10,
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x62,
				.mask = 0x01,
			},
			.stat_drdy = {
				.addr = 0x67,
				.mask = 0x07,
			},
		},
		.multi_read_bit = false,
		.bootime = 2,
	},
	{
		.wai = 0x49,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LSM9DS0_IMU_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_magn_4_16bit_channels,
		.odr = {
			.addr = 0x24,
			.mask = GENMASK(4, 2),
			.odr_avl = {
				{ 3, 0x00, },
				{ 6, 0x01, },
				{ 12, 0x02, },
				{ 25, 0x03, },
				{ 50, 0x04, },
				{ 100, 0x05, },
			},
		},
		.pw = {
			.addr = 0x26,
			.mask = GENMASK(1, 0),
			.value_on = 0x00,
			.value_off = 0x03,
		},
		.fs = {
			.addr = 0x25,
			.mask = GENMASK(6, 5),
			.fs_avl = {
				[0] = {
					.num = ST_MAGN_FS_AVL_2000MG,
					.value = 0x00,
					.gain = 73,
				},
				[1] = {
					.num = ST_MAGN_FS_AVL_4000MG,
					.value = 0x01,
					.gain = 146,
				},
				[2] = {
					.num = ST_MAGN_FS_AVL_8000MG,
					.value = 0x02,
					.gain = 292,
				},
				[3] = {
					.num = ST_MAGN_FS_AVL_12000MG,
					.value = 0x03,
					.gain = 438,
				},
			},
		},
		.bdu = {
			.addr = 0x20,
			.mask = BIT(3),
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x22,
				.mask = BIT(1),
			},
			.int2 = {
				.addr = 0x23,
				.mask = BIT(2),
			},
			.stat_drdy = {
				.addr = 0x07,
				.mask = GENMASK(2, 0),
			},
		},
		.sim = {
			.addr = 0x21,
			.value = BIT(0),
		},
		.multi_read_bit = true,
		.bootime = 2,
	},
};

/* Default magn DRDY is available on INT2 pin */
static const struct st_sensors_platform_data default_magn_pdata = {
	.drdy_int_pin = 2,
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
	.attrs = &st_magn_attribute_group,
	.read_raw = &st_magn_read_raw,
	.write_raw = &st_magn_write_raw,
	.debugfs_reg_access = &st_sensors_debugfs_reg_access,
};

#ifdef CONFIG_IIO_TRIGGER
static const struct iio_trigger_ops st_magn_trigger_ops = {
	.set_trigger_state = ST_MAGN_TRIGGER_SET_STATE,
	.validate_device = st_sensors_validate_device,
};
#define ST_MAGN_TRIGGER_OPS (&st_magn_trigger_ops)
#else
#define ST_MAGN_TRIGGER_OPS NULL
#endif

/*
 * st_magn_get_settings() - get sensor settings from device name
 * @name: device name buffer reference.
 *
 * Return: valid reference on success, NULL otherwise.
 */
const struct st_sensor_settings *st_magn_get_settings(const char *name)
{
	int index = st_sensors_get_settings_index(name,
					st_magn_sensors_settings,
					ARRAY_SIZE(st_magn_sensors_settings));
	if (index < 0)
		return NULL;

	return &st_magn_sensors_settings[index];
}
EXPORT_SYMBOL(st_magn_get_settings);

int st_magn_common_probe(struct iio_dev *indio_dev)
{
	struct st_sensor_data *mdata = iio_priv(indio_dev);
	struct st_sensors_platform_data *pdata = dev_get_platdata(mdata->dev);
	struct device *parent = indio_dev->dev.parent;
	int err;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &magn_info;

	err = st_sensors_verify_id(indio_dev);
	if (err < 0)
		return err;

	mdata->num_data_channels = ST_MAGN_NUMBER_DATA_CHANNELS;
	indio_dev->channels = mdata->sensor_settings->ch;
	indio_dev->num_channels = ST_SENSORS_NUMBER_ALL_CHANNELS;

	err = iio_read_mount_matrix(mdata->dev, &mdata->mount_matrix);
	if (err)
		return err;

	mdata->current_fullscale = &mdata->sensor_settings->fs.fs_avl[0];
	mdata->odr = mdata->sensor_settings->odr.odr_avl[0].hz;

	if (!pdata)
		pdata = (struct st_sensors_platform_data *)&default_magn_pdata;

	err = st_sensors_init_sensor(indio_dev, pdata);
	if (err < 0)
		return err;

	err = st_magn_allocate_ring(indio_dev);
	if (err < 0)
		return err;

	if (mdata->irq > 0) {
		err = st_sensors_allocate_trigger(indio_dev,
						ST_MAGN_TRIGGER_OPS);
		if (err < 0)
			return err;
	}

	return devm_iio_device_register(parent, indio_dev);
}
EXPORT_SYMBOL(st_magn_common_probe);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics magnetometers driver");
MODULE_LICENSE("GPL v2");
