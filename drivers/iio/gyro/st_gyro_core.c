// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics gyroscopes driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>

#include <linux/iio/common/st_sensors.h>
#include "st_gyro.h"

#define ST_GYRO_NUMBER_DATA_CHANNELS		3

/* DEFAULT VALUE FOR SENSORS */
#define ST_GYRO_DEFAULT_OUT_X_L_ADDR		0x28
#define ST_GYRO_DEFAULT_OUT_Y_L_ADDR		0x2a
#define ST_GYRO_DEFAULT_OUT_Z_L_ADDR		0x2c

/* FULLSCALE */
#define ST_GYRO_FS_AVL_245DPS			245
#define ST_GYRO_FS_AVL_250DPS			250
#define ST_GYRO_FS_AVL_500DPS			500
#define ST_GYRO_FS_AVL_2000DPS			2000

static const struct iio_mount_matrix *
st_gyro_get_mount_matrix(const struct iio_dev *indio_dev,
			 const struct iio_chan_spec *chan)
{
	struct st_sensor_data *gdata = iio_priv(indio_dev);

	return &gdata->mount_matrix;
}

static const struct iio_chan_spec_ext_info st_gyro_mount_matrix_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_ALL, st_gyro_get_mount_matrix),
	{ }
};

static const struct iio_chan_spec st_gyro_16bit_channels[] = {
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_ANGL_VEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 1, IIO_MOD_X, 's', IIO_LE, 16, 16,
			ST_GYRO_DEFAULT_OUT_X_L_ADDR,
			st_gyro_mount_matrix_ext_info),
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_ANGL_VEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Y, 1, IIO_MOD_Y, 's', IIO_LE, 16, 16,
			ST_GYRO_DEFAULT_OUT_Y_L_ADDR,
			st_gyro_mount_matrix_ext_info),
	ST_SENSORS_LSM_CHANNELS_EXT(IIO_ANGL_VEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Z, 1, IIO_MOD_Z, 's', IIO_LE, 16, 16,
			ST_GYRO_DEFAULT_OUT_Z_L_ADDR,
			st_gyro_mount_matrix_ext_info),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct st_sensor_settings st_gyro_sensors_settings[] = {
	{
		.wai = 0xd3,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = L3G4200D_GYRO_DEV_NAME,
			[1] = LSM330DL_GYRO_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_gyro_16bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0xc0,
			.odr_avl = {
				{ .hz = 100, .value = 0x00, },
				{ .hz = 200, .value = 0x01, },
				{ .hz = 400, .value = 0x02, },
				{ .hz = 800, .value = 0x03, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0x08,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = 0x23,
			.mask = 0x30,
			.fs_avl = {
				[0] = {
					.num = ST_GYRO_FS_AVL_250DPS,
					.value = 0x00,
					.gain = IIO_DEGREE_TO_RAD(8750),
				},
				[1] = {
					.num = ST_GYRO_FS_AVL_500DPS,
					.value = 0x01,
					.gain = IIO_DEGREE_TO_RAD(17500),
				},
				[2] = {
					.num = ST_GYRO_FS_AVL_2000DPS,
					.value = 0x02,
					.gain = IIO_DEGREE_TO_RAD(70000),
				},
			},
		},
		.bdu = {
			.addr = 0x23,
			.mask = 0x80,
		},
		.drdy_irq = {
			.int2 = {
				.addr = 0x22,
				.mask = 0x08,
			},
			/*
			 * The sensor has IHL (active low) and open
			 * drain settings, but only for INT1 and not
			 * for the DRDY line on INT2.
			 */
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x07,
			},
		},
		.sim = {
			.addr = 0x23,
			.value = BIT(0),
		},
		.multi_read_bit = true,
		.bootime = 2,
	},
	{
		.wai = 0xd4,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = L3GD20_GYRO_DEV_NAME,
			[1] = LSM330D_GYRO_DEV_NAME,
			[2] = LSM330DLC_GYRO_DEV_NAME,
			[3] = L3G4IS_GYRO_DEV_NAME,
			[4] = LSM330_GYRO_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_gyro_16bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0xc0,
			.odr_avl = {
				{ .hz = 95, .value = 0x00, },
				{ .hz = 190, .value = 0x01, },
				{ .hz = 380, .value = 0x02, },
				{ .hz = 760, .value = 0x03, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0x08,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = 0x23,
			.mask = 0x30,
			.fs_avl = {
				[0] = {
					.num = ST_GYRO_FS_AVL_250DPS,
					.value = 0x00,
					.gain = IIO_DEGREE_TO_RAD(8750),
				},
				[1] = {
					.num = ST_GYRO_FS_AVL_500DPS,
					.value = 0x01,
					.gain = IIO_DEGREE_TO_RAD(17500),
				},
				[2] = {
					.num = ST_GYRO_FS_AVL_2000DPS,
					.value = 0x02,
					.gain = IIO_DEGREE_TO_RAD(70000),
				},
			},
		},
		.bdu = {
			.addr = 0x23,
			.mask = 0x80,
		},
		.drdy_irq = {
			.int2 = {
				.addr = 0x22,
				.mask = 0x08,
			},
			/*
			 * The sensor has IHL (active low) and open
			 * drain settings, but only for INT1 and not
			 * for the DRDY line on INT2.
			 */
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x07,
			},
		},
		.sim = {
			.addr = 0x23,
			.value = BIT(0),
		},
		.multi_read_bit = true,
		.bootime = 2,
	},
	{
		.wai = 0xd4,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LSM9DS0_GYRO_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_gyro_16bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = GENMASK(7, 6),
			.odr_avl = {
				{ .hz = 95, .value = 0x00, },
				{ .hz = 190, .value = 0x01, },
				{ .hz = 380, .value = 0x02, },
				{ .hz = 760, .value = 0x03, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = BIT(3),
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = 0x23,
			.mask = GENMASK(5, 4),
			.fs_avl = {
				[0] = {
					.num = ST_GYRO_FS_AVL_245DPS,
					.value = 0x00,
					.gain = IIO_DEGREE_TO_RAD(8750),
				},
				[1] = {
					.num = ST_GYRO_FS_AVL_500DPS,
					.value = 0x01,
					.gain = IIO_DEGREE_TO_RAD(17500),
				},
				[2] = {
					.num = ST_GYRO_FS_AVL_2000DPS,
					.value = 0x02,
					.gain = IIO_DEGREE_TO_RAD(70000),
				},
			},
		},
		.bdu = {
			.addr = 0x23,
			.mask = BIT(7),
		},
		.drdy_irq = {
			.int2 = {
				.addr = 0x22,
				.mask = BIT(3),
			},
			/*
			 * The sensor has IHL (active low) and open
			 * drain settings, but only for INT1 and not
			 * for the DRDY line on INT2.
			 */
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = GENMASK(2, 0),
			},
		},
		.sim = {
			.addr = 0x23,
			.value = BIT(0),
		},
		.multi_read_bit = true,
		.bootime = 2,
	},
	{
		.wai = 0xd7,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = L3GD20H_GYRO_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_gyro_16bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0xc0,
			.odr_avl = {
				{ .hz = 100, .value = 0x00, },
				{ .hz = 200, .value = 0x01, },
				{ .hz = 400, .value = 0x02, },
				{ .hz = 800, .value = 0x03, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0x08,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = 0x23,
			.mask = 0x30,
			.fs_avl = {
				[0] = {
					.num = ST_GYRO_FS_AVL_245DPS,
					.value = 0x00,
					.gain = IIO_DEGREE_TO_RAD(8750),
				},
				[1] = {
					.num = ST_GYRO_FS_AVL_500DPS,
					.value = 0x01,
					.gain = IIO_DEGREE_TO_RAD(17500),
				},
				[2] = {
					.num = ST_GYRO_FS_AVL_2000DPS,
					.value = 0x02,
					.gain = IIO_DEGREE_TO_RAD(70000),
				},
			},
		},
		.bdu = {
			.addr = 0x23,
			.mask = 0x80,
		},
		.drdy_irq = {
			.int2 = {
				.addr = 0x22,
				.mask = 0x08,
			},
			/*
			 * The sensor has IHL (active low) and open
			 * drain settings, but only for INT1 and not
			 * for the DRDY line on INT2.
			 */
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x07,
			},
		},
		.sim = {
			.addr = 0x23,
			.value = BIT(0),
		},
		.multi_read_bit = true,
		.bootime = 2,
	},
};

/* DRDY on gyros is available only on INT2 pin */
static const struct st_sensors_platform_data gyro_pdata = {
	.drdy_int_pin = 2,
};

static int st_gyro_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *ch, int *val,
							int *val2, long mask)
{
	int err;
	struct st_sensor_data *gdata = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = st_sensors_read_info_raw(indio_dev, ch, val);
		if (err < 0)
			goto read_error;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = gdata->current_fullscale->gain;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = gdata->odr;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}

read_error:
	return err;
}

static int st_gyro_write_raw(struct iio_dev *indio_dev,
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
static ST_SENSORS_DEV_ATTR_SCALE_AVAIL(in_anglvel_scale_available);

static struct attribute *st_gyro_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_anglvel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_gyro_attribute_group = {
	.attrs = st_gyro_attributes,
};

static const struct iio_info gyro_info = {
	.attrs = &st_gyro_attribute_group,
	.read_raw = &st_gyro_read_raw,
	.write_raw = &st_gyro_write_raw,
	.debugfs_reg_access = &st_sensors_debugfs_reg_access,
};

#ifdef CONFIG_IIO_TRIGGER
static const struct iio_trigger_ops st_gyro_trigger_ops = {
	.set_trigger_state = ST_GYRO_TRIGGER_SET_STATE,
	.validate_device = st_sensors_validate_device,
};
#define ST_GYRO_TRIGGER_OPS (&st_gyro_trigger_ops)
#else
#define ST_GYRO_TRIGGER_OPS NULL
#endif

/*
 * st_gyro_get_settings() - get sensor settings from device name
 * @name: device name buffer reference.
 *
 * Return: valid reference on success, NULL otherwise.
 */
const struct st_sensor_settings *st_gyro_get_settings(const char *name)
{
	int index = st_sensors_get_settings_index(name,
					st_gyro_sensors_settings,
					ARRAY_SIZE(st_gyro_sensors_settings));
	if (index < 0)
		return NULL;

	return &st_gyro_sensors_settings[index];
}
EXPORT_SYMBOL(st_gyro_get_settings);

int st_gyro_common_probe(struct iio_dev *indio_dev)
{
	struct st_sensor_data *gdata = iio_priv(indio_dev);
	struct st_sensors_platform_data *pdata;
	int err;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &gyro_info;

	err = st_sensors_verify_id(indio_dev);
	if (err < 0)
		return err;

	gdata->num_data_channels = ST_GYRO_NUMBER_DATA_CHANNELS;
	indio_dev->channels = gdata->sensor_settings->ch;
	indio_dev->num_channels = ST_SENSORS_NUMBER_ALL_CHANNELS;

	err = iio_read_mount_matrix(gdata->dev, &gdata->mount_matrix);
	if (err)
		return err;

	gdata->current_fullscale = &gdata->sensor_settings->fs.fs_avl[0];
	gdata->odr = gdata->sensor_settings->odr.odr_avl[0].hz;

	pdata = (struct st_sensors_platform_data *)&gyro_pdata;

	err = st_sensors_init_sensor(indio_dev, pdata);
	if (err < 0)
		return err;

	err = st_gyro_allocate_ring(indio_dev);
	if (err < 0)
		return err;

	if (gdata->irq > 0) {
		err = st_sensors_allocate_trigger(indio_dev,
						  ST_GYRO_TRIGGER_OPS);
		if (err < 0)
			return err;
	}

	err = iio_device_register(indio_dev);
	if (err)
		goto st_gyro_device_register_error;

	dev_info(&indio_dev->dev, "registered gyroscope %s\n",
		 indio_dev->name);

	return 0;

st_gyro_device_register_error:
	if (gdata->irq > 0)
		st_sensors_deallocate_trigger(indio_dev);
	return err;
}
EXPORT_SYMBOL(st_gyro_common_probe);

void st_gyro_common_remove(struct iio_dev *indio_dev)
{
	struct st_sensor_data *gdata = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (gdata->irq > 0)
		st_sensors_deallocate_trigger(indio_dev);
}
EXPORT_SYMBOL(st_gyro_common_remove);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics gyroscopes driver");
MODULE_LICENSE("GPL v2");
