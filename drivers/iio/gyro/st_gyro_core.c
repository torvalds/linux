/*
 * STMicroelectronics gyroscopes driver
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
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>

#include <linux/iio/common/st_sensors.h>
#include "st_gyro.h"

#define ST_GYRO_NUMBER_DATA_CHANNELS		3

/* DEFAULT VALUE FOR SENSORS */
#define ST_GYRO_DEFAULT_OUT_X_L_ADDR		0x28
#define ST_GYRO_DEFAULT_OUT_Y_L_ADDR		0x2a
#define ST_GYRO_DEFAULT_OUT_Z_L_ADDR		0x2c

/* FULLSCALE */
#define ST_GYRO_FS_AVL_250DPS			250
#define ST_GYRO_FS_AVL_500DPS			500
#define ST_GYRO_FS_AVL_2000DPS			2000

/* CUSTOM VALUES FOR SENSOR 1 */
#define ST_GYRO_1_WAI_EXP			0xd3
#define ST_GYRO_1_ODR_ADDR			0x20
#define ST_GYRO_1_ODR_MASK			0xc0
#define ST_GYRO_1_ODR_AVL_100HZ_VAL		0x00
#define ST_GYRO_1_ODR_AVL_200HZ_VAL		0x01
#define ST_GYRO_1_ODR_AVL_400HZ_VAL		0x02
#define ST_GYRO_1_ODR_AVL_800HZ_VAL		0x03
#define ST_GYRO_1_PW_ADDR			0x20
#define ST_GYRO_1_PW_MASK			0x08
#define ST_GYRO_1_FS_ADDR			0x23
#define ST_GYRO_1_FS_MASK			0x30
#define ST_GYRO_1_FS_AVL_250_VAL		0x00
#define ST_GYRO_1_FS_AVL_500_VAL		0x01
#define ST_GYRO_1_FS_AVL_2000_VAL		0x02
#define ST_GYRO_1_FS_AVL_250_GAIN		IIO_DEGREE_TO_RAD(8750)
#define ST_GYRO_1_FS_AVL_500_GAIN		IIO_DEGREE_TO_RAD(17500)
#define ST_GYRO_1_FS_AVL_2000_GAIN		IIO_DEGREE_TO_RAD(70000)
#define ST_GYRO_1_BDU_ADDR			0x23
#define ST_GYRO_1_BDU_MASK			0x80
#define ST_GYRO_1_DRDY_IRQ_ADDR			0x22
#define ST_GYRO_1_DRDY_IRQ_INT2_MASK		0x08
#define ST_GYRO_1_MULTIREAD_BIT			true

/* CUSTOM VALUES FOR SENSOR 2 */
#define ST_GYRO_2_WAI_EXP			0xd4
#define ST_GYRO_2_ODR_ADDR			0x20
#define ST_GYRO_2_ODR_MASK			0xc0
#define ST_GYRO_2_ODR_AVL_95HZ_VAL		0x00
#define ST_GYRO_2_ODR_AVL_190HZ_VAL		0x01
#define ST_GYRO_2_ODR_AVL_380HZ_VAL		0x02
#define ST_GYRO_2_ODR_AVL_760HZ_VAL		0x03
#define ST_GYRO_2_PW_ADDR			0x20
#define ST_GYRO_2_PW_MASK			0x08
#define ST_GYRO_2_FS_ADDR			0x23
#define ST_GYRO_2_FS_MASK			0x30
#define ST_GYRO_2_FS_AVL_250_VAL		0x00
#define ST_GYRO_2_FS_AVL_500_VAL		0x01
#define ST_GYRO_2_FS_AVL_2000_VAL		0x02
#define ST_GYRO_2_FS_AVL_250_GAIN		IIO_DEGREE_TO_RAD(8750)
#define ST_GYRO_2_FS_AVL_500_GAIN		IIO_DEGREE_TO_RAD(17500)
#define ST_GYRO_2_FS_AVL_2000_GAIN		IIO_DEGREE_TO_RAD(70000)
#define ST_GYRO_2_BDU_ADDR			0x23
#define ST_GYRO_2_BDU_MASK			0x80
#define ST_GYRO_2_DRDY_IRQ_ADDR			0x22
#define ST_GYRO_2_DRDY_IRQ_INT2_MASK		0x08
#define ST_GYRO_2_MULTIREAD_BIT			true

static const struct iio_chan_spec st_gyro_16bit_channels[] = {
	ST_SENSORS_LSM_CHANNELS(IIO_ANGL_VEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 1, IIO_MOD_X, 's', IIO_LE, 16, 16,
			ST_GYRO_DEFAULT_OUT_X_L_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_ANGL_VEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Y, 1, IIO_MOD_Y, 's', IIO_LE, 16, 16,
			ST_GYRO_DEFAULT_OUT_Y_L_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_ANGL_VEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Z, 1, IIO_MOD_Z, 's', IIO_LE, 16, 16,
			ST_GYRO_DEFAULT_OUT_Z_L_ADDR),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct st_sensors st_gyro_sensors[] = {
	{
		.wai = ST_GYRO_1_WAI_EXP,
		.sensors_supported = {
			[0] = L3G4200D_GYRO_DEV_NAME,
			[1] = LSM330DL_GYRO_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_gyro_16bit_channels,
		.odr = {
			.addr = ST_GYRO_1_ODR_ADDR,
			.mask = ST_GYRO_1_ODR_MASK,
			.odr_avl = {
				{ 100, ST_GYRO_1_ODR_AVL_100HZ_VAL, },
				{ 200, ST_GYRO_1_ODR_AVL_200HZ_VAL, },
				{ 400, ST_GYRO_1_ODR_AVL_400HZ_VAL, },
				{ 800, ST_GYRO_1_ODR_AVL_800HZ_VAL, },
			},
		},
		.pw = {
			.addr = ST_GYRO_1_PW_ADDR,
			.mask = ST_GYRO_1_PW_MASK,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = ST_GYRO_1_FS_ADDR,
			.mask = ST_GYRO_1_FS_MASK,
			.fs_avl = {
				[0] = {
					.num = ST_GYRO_FS_AVL_250DPS,
					.value = ST_GYRO_1_FS_AVL_250_VAL,
					.gain = ST_GYRO_1_FS_AVL_250_GAIN,
				},
				[1] = {
					.num = ST_GYRO_FS_AVL_500DPS,
					.value = ST_GYRO_1_FS_AVL_500_VAL,
					.gain = ST_GYRO_1_FS_AVL_500_GAIN,
				},
				[2] = {
					.num = ST_GYRO_FS_AVL_2000DPS,
					.value = ST_GYRO_1_FS_AVL_2000_VAL,
					.gain = ST_GYRO_1_FS_AVL_2000_GAIN,
				},
			},
		},
		.bdu = {
			.addr = ST_GYRO_1_BDU_ADDR,
			.mask = ST_GYRO_1_BDU_MASK,
		},
		.drdy_irq = {
			.addr = ST_GYRO_1_DRDY_IRQ_ADDR,
			.mask_int2 = ST_GYRO_1_DRDY_IRQ_INT2_MASK,
		},
		.multi_read_bit = ST_GYRO_1_MULTIREAD_BIT,
		.bootime = 2,
	},
	{
		.wai = ST_GYRO_2_WAI_EXP,
		.sensors_supported = {
			[0] = L3GD20_GYRO_DEV_NAME,
			[1] = LSM330D_GYRO_DEV_NAME,
			[2] = LSM330DLC_GYRO_DEV_NAME,
			[3] = L3G4IS_GYRO_DEV_NAME,
			[4] = LSM330_GYRO_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_gyro_16bit_channels,
		.odr = {
			.addr = ST_GYRO_2_ODR_ADDR,
			.mask = ST_GYRO_2_ODR_MASK,
			.odr_avl = {
				{ 95, ST_GYRO_2_ODR_AVL_95HZ_VAL, },
				{ 190, ST_GYRO_2_ODR_AVL_190HZ_VAL, },
				{ 380, ST_GYRO_2_ODR_AVL_380HZ_VAL, },
				{ 760, ST_GYRO_2_ODR_AVL_760HZ_VAL, },
			},
		},
		.pw = {
			.addr = ST_GYRO_2_PW_ADDR,
			.mask = ST_GYRO_2_PW_MASK,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = ST_GYRO_2_FS_ADDR,
			.mask = ST_GYRO_2_FS_MASK,
			.fs_avl = {
				[0] = {
					.num = ST_GYRO_FS_AVL_250DPS,
					.value = ST_GYRO_2_FS_AVL_250_VAL,
					.gain = ST_GYRO_2_FS_AVL_250_GAIN,
				},
				[1] = {
					.num = ST_GYRO_FS_AVL_500DPS,
					.value = ST_GYRO_2_FS_AVL_500_VAL,
					.gain = ST_GYRO_2_FS_AVL_500_GAIN,
				},
				[2] = {
					.num = ST_GYRO_FS_AVL_2000DPS,
					.value = ST_GYRO_2_FS_AVL_2000_VAL,
					.gain = ST_GYRO_2_FS_AVL_2000_GAIN,
				},
			},
		},
		.bdu = {
			.addr = ST_GYRO_2_BDU_ADDR,
			.mask = ST_GYRO_2_BDU_MASK,
		},
		.drdy_irq = {
			.addr = ST_GYRO_2_DRDY_IRQ_ADDR,
			.mask_int2 = ST_GYRO_2_DRDY_IRQ_INT2_MASK,
		},
		.multi_read_bit = ST_GYRO_2_MULTIREAD_BIT,
		.bootime = 2,
	},
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
	default:
		err = -EINVAL;
	}

	return err;
}

static ST_SENSOR_DEV_ATTR_SAMP_FREQ();
static ST_SENSORS_DEV_ATTR_SAMP_FREQ_AVAIL();
static ST_SENSORS_DEV_ATTR_SCALE_AVAIL(in_anglvel_scale_available);

static struct attribute *st_gyro_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_anglvel_scale_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_gyro_attribute_group = {
	.attrs = st_gyro_attributes,
};

static const struct iio_info gyro_info = {
	.driver_module = THIS_MODULE,
	.attrs = &st_gyro_attribute_group,
	.read_raw = &st_gyro_read_raw,
	.write_raw = &st_gyro_write_raw,
};

#ifdef CONFIG_IIO_TRIGGER
static const struct iio_trigger_ops st_gyro_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = ST_GYRO_TRIGGER_SET_STATE,
};
#define ST_GYRO_TRIGGER_OPS (&st_gyro_trigger_ops)
#else
#define ST_GYRO_TRIGGER_OPS NULL
#endif

int st_gyro_common_probe(struct iio_dev *indio_dev,
					struct st_sensors_platform_data *pdata)
{
	struct st_sensor_data *gdata = iio_priv(indio_dev);
	int irq = gdata->get_irq_data_ready(indio_dev);
	int err;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &gyro_info;

	err = st_sensors_check_device_support(indio_dev,
				ARRAY_SIZE(st_gyro_sensors), st_gyro_sensors);
	if (err < 0)
		return err;

	gdata->num_data_channels = ST_GYRO_NUMBER_DATA_CHANNELS;
	gdata->multiread_bit = gdata->sensor->multi_read_bit;
	indio_dev->channels = gdata->sensor->ch;
	indio_dev->num_channels = ST_SENSORS_NUMBER_ALL_CHANNELS;

	gdata->current_fullscale = (struct st_sensor_fullscale_avl *)
						&gdata->sensor->fs.fs_avl[0];
	gdata->odr = gdata->sensor->odr.odr_avl[0].hz;

	err = st_sensors_init_sensor(indio_dev, pdata);
	if (err < 0)
		return err;

	err = st_gyro_allocate_ring(indio_dev);
	if (err < 0)
		return err;

	if (irq > 0) {
		err = st_sensors_allocate_trigger(indio_dev,
						  ST_GYRO_TRIGGER_OPS);
		if (err < 0)
			goto st_gyro_probe_trigger_error;
	}

	err = iio_device_register(indio_dev);
	if (err)
		goto st_gyro_device_register_error;

	return 0;

st_gyro_device_register_error:
	if (irq > 0)
		st_sensors_deallocate_trigger(indio_dev);
st_gyro_probe_trigger_error:
	st_gyro_deallocate_ring(indio_dev);

	return err;
}
EXPORT_SYMBOL(st_gyro_common_probe);

void st_gyro_common_remove(struct iio_dev *indio_dev)
{
	struct st_sensor_data *gdata = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (gdata->get_irq_data_ready(indio_dev) > 0)
		st_sensors_deallocate_trigger(indio_dev);

	st_gyro_deallocate_ring(indio_dev);
}
EXPORT_SYMBOL(st_gyro_common_remove);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics gyroscopes driver");
MODULE_LICENSE("GPL v2");
