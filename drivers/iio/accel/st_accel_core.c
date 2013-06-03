/*
 * STMicroelectronics accelerometers driver
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
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>

#include <linux/iio/common/st_sensors.h>
#include "st_accel.h"

/* DEFAULT VALUE FOR SENSORS */
#define ST_ACCEL_DEFAULT_OUT_X_L_ADDR		0x28
#define ST_ACCEL_DEFAULT_OUT_Y_L_ADDR		0x2a
#define ST_ACCEL_DEFAULT_OUT_Z_L_ADDR		0x2c

/* FULLSCALE */
#define ST_ACCEL_FS_AVL_2G			2
#define ST_ACCEL_FS_AVL_4G			4
#define ST_ACCEL_FS_AVL_6G			6
#define ST_ACCEL_FS_AVL_8G			8
#define ST_ACCEL_FS_AVL_16G			16

/* CUSTOM VALUES FOR SENSOR 1 */
#define ST_ACCEL_1_WAI_EXP			0x33
#define ST_ACCEL_1_ODR_ADDR			0x20
#define ST_ACCEL_1_ODR_MASK			0xf0
#define ST_ACCEL_1_ODR_AVL_1HZ_VAL		0x01
#define ST_ACCEL_1_ODR_AVL_10HZ_VAL		0x02
#define ST_ACCEL_1_ODR_AVL_25HZ_VAL		0x03
#define ST_ACCEL_1_ODR_AVL_50HZ_VAL		0x04
#define ST_ACCEL_1_ODR_AVL_100HZ_VAL		0x05
#define ST_ACCEL_1_ODR_AVL_200HZ_VAL		0x06
#define ST_ACCEL_1_ODR_AVL_400HZ_VAL		0x07
#define ST_ACCEL_1_ODR_AVL_1600HZ_VAL		0x08
#define ST_ACCEL_1_FS_ADDR			0x23
#define ST_ACCEL_1_FS_MASK			0x30
#define ST_ACCEL_1_FS_AVL_2_VAL			0x00
#define ST_ACCEL_1_FS_AVL_4_VAL			0x01
#define ST_ACCEL_1_FS_AVL_8_VAL			0x02
#define ST_ACCEL_1_FS_AVL_16_VAL		0x03
#define ST_ACCEL_1_FS_AVL_2_GAIN		IIO_G_TO_M_S_2(1000)
#define ST_ACCEL_1_FS_AVL_4_GAIN		IIO_G_TO_M_S_2(2000)
#define ST_ACCEL_1_FS_AVL_8_GAIN		IIO_G_TO_M_S_2(4000)
#define ST_ACCEL_1_FS_AVL_16_GAIN		IIO_G_TO_M_S_2(12000)
#define ST_ACCEL_1_BDU_ADDR			0x23
#define ST_ACCEL_1_BDU_MASK			0x80
#define ST_ACCEL_1_DRDY_IRQ_ADDR		0x22
#define ST_ACCEL_1_DRDY_IRQ_MASK		0x10
#define ST_ACCEL_1_MULTIREAD_BIT		true

/* CUSTOM VALUES FOR SENSOR 2 */
#define ST_ACCEL_2_WAI_EXP			0x32
#define ST_ACCEL_2_ODR_ADDR			0x20
#define ST_ACCEL_2_ODR_MASK			0x18
#define ST_ACCEL_2_ODR_AVL_50HZ_VAL		0x00
#define ST_ACCEL_2_ODR_AVL_100HZ_VAL		0x01
#define ST_ACCEL_2_ODR_AVL_400HZ_VAL		0x02
#define ST_ACCEL_2_ODR_AVL_1000HZ_VAL		0x03
#define ST_ACCEL_2_PW_ADDR			0x20
#define ST_ACCEL_2_PW_MASK			0xe0
#define ST_ACCEL_2_FS_ADDR			0x23
#define ST_ACCEL_2_FS_MASK			0x30
#define ST_ACCEL_2_FS_AVL_2_VAL			0X00
#define ST_ACCEL_2_FS_AVL_4_VAL			0X01
#define ST_ACCEL_2_FS_AVL_8_VAL			0x03
#define ST_ACCEL_2_FS_AVL_2_GAIN		IIO_G_TO_M_S_2(1000)
#define ST_ACCEL_2_FS_AVL_4_GAIN		IIO_G_TO_M_S_2(2000)
#define ST_ACCEL_2_FS_AVL_8_GAIN		IIO_G_TO_M_S_2(3900)
#define ST_ACCEL_2_BDU_ADDR			0x23
#define ST_ACCEL_2_BDU_MASK			0x80
#define ST_ACCEL_2_DRDY_IRQ_ADDR		0x22
#define ST_ACCEL_2_DRDY_IRQ_MASK		0x02
#define ST_ACCEL_2_MULTIREAD_BIT		true

/* CUSTOM VALUES FOR SENSOR 3 */
#define ST_ACCEL_3_WAI_EXP			0x40
#define ST_ACCEL_3_ODR_ADDR			0x20
#define ST_ACCEL_3_ODR_MASK			0xf0
#define ST_ACCEL_3_ODR_AVL_3HZ_VAL		0x01
#define ST_ACCEL_3_ODR_AVL_6HZ_VAL		0x02
#define ST_ACCEL_3_ODR_AVL_12HZ_VAL		0x03
#define ST_ACCEL_3_ODR_AVL_25HZ_VAL		0x04
#define ST_ACCEL_3_ODR_AVL_50HZ_VAL		0x05
#define ST_ACCEL_3_ODR_AVL_100HZ_VAL		0x06
#define ST_ACCEL_3_ODR_AVL_200HZ_VAL		0x07
#define ST_ACCEL_3_ODR_AVL_400HZ_VAL		0x08
#define ST_ACCEL_3_ODR_AVL_800HZ_VAL		0x09
#define ST_ACCEL_3_ODR_AVL_1600HZ_VAL		0x0a
#define ST_ACCEL_3_FS_ADDR			0x24
#define ST_ACCEL_3_FS_MASK			0x38
#define ST_ACCEL_3_FS_AVL_2_VAL			0X00
#define ST_ACCEL_3_FS_AVL_4_VAL			0X01
#define ST_ACCEL_3_FS_AVL_6_VAL			0x02
#define ST_ACCEL_3_FS_AVL_8_VAL			0x03
#define ST_ACCEL_3_FS_AVL_16_VAL		0x04
#define ST_ACCEL_3_FS_AVL_2_GAIN		IIO_G_TO_M_S_2(61)
#define ST_ACCEL_3_FS_AVL_4_GAIN		IIO_G_TO_M_S_2(122)
#define ST_ACCEL_3_FS_AVL_6_GAIN		IIO_G_TO_M_S_2(183)
#define ST_ACCEL_3_FS_AVL_8_GAIN		IIO_G_TO_M_S_2(244)
#define ST_ACCEL_3_FS_AVL_16_GAIN		IIO_G_TO_M_S_2(732)
#define ST_ACCEL_3_BDU_ADDR			0x20
#define ST_ACCEL_3_BDU_MASK			0x08
#define ST_ACCEL_3_DRDY_IRQ_ADDR		0x23
#define ST_ACCEL_3_DRDY_IRQ_MASK		0x80
#define ST_ACCEL_3_IG1_EN_ADDR			0x23
#define ST_ACCEL_3_IG1_EN_MASK			0x08
#define ST_ACCEL_3_MULTIREAD_BIT		false

static const struct iio_chan_spec st_accel_12bit_channels[] = {
	ST_SENSORS_LSM_CHANNELS(IIO_ACCEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 1, IIO_MOD_X, 's', IIO_LE, 12, 16,
			ST_ACCEL_DEFAULT_OUT_X_L_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_ACCEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Y, 1, IIO_MOD_Y, 's', IIO_LE, 12, 16,
			ST_ACCEL_DEFAULT_OUT_Y_L_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_ACCEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Z, 1, IIO_MOD_Z, 's', IIO_LE, 12, 16,
			ST_ACCEL_DEFAULT_OUT_Z_L_ADDR),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct iio_chan_spec st_accel_16bit_channels[] = {
	ST_SENSORS_LSM_CHANNELS(IIO_ACCEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 1, IIO_MOD_X, 's', IIO_LE, 16, 16,
			ST_ACCEL_DEFAULT_OUT_X_L_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_ACCEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Y, 1, IIO_MOD_Y, 's', IIO_LE, 16, 16,
			ST_ACCEL_DEFAULT_OUT_Y_L_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_ACCEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Z, 1, IIO_MOD_Z, 's', IIO_LE, 16, 16,
			ST_ACCEL_DEFAULT_OUT_Z_L_ADDR),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct st_sensors st_accel_sensors[] = {
	{
		.wai = ST_ACCEL_1_WAI_EXP,
		.sensors_supported = {
			[0] = LIS3DH_ACCEL_DEV_NAME,
			[1] = LSM303DLHC_ACCEL_DEV_NAME,
			[2] = LSM330D_ACCEL_DEV_NAME,
			[3] = LSM330DL_ACCEL_DEV_NAME,
			[4] = LSM330DLC_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_12bit_channels,
		.odr = {
			.addr = ST_ACCEL_1_ODR_ADDR,
			.mask = ST_ACCEL_1_ODR_MASK,
			.odr_avl = {
				{ 1, ST_ACCEL_1_ODR_AVL_1HZ_VAL, },
				{ 10, ST_ACCEL_1_ODR_AVL_10HZ_VAL, },
				{ 25, ST_ACCEL_1_ODR_AVL_25HZ_VAL, },
				{ 50, ST_ACCEL_1_ODR_AVL_50HZ_VAL, },
				{ 100, ST_ACCEL_1_ODR_AVL_100HZ_VAL, },
				{ 200, ST_ACCEL_1_ODR_AVL_200HZ_VAL, },
				{ 400, ST_ACCEL_1_ODR_AVL_400HZ_VAL, },
				{ 1600, ST_ACCEL_1_ODR_AVL_1600HZ_VAL, },
			},
		},
		.pw = {
			.addr = ST_ACCEL_1_ODR_ADDR,
			.mask = ST_ACCEL_1_ODR_MASK,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = ST_ACCEL_1_FS_ADDR,
			.mask = ST_ACCEL_1_FS_MASK,
			.fs_avl = {
				[0] = {
					.num = ST_ACCEL_FS_AVL_2G,
					.value = ST_ACCEL_1_FS_AVL_2_VAL,
					.gain = ST_ACCEL_1_FS_AVL_2_GAIN,
				},
				[1] = {
					.num = ST_ACCEL_FS_AVL_4G,
					.value = ST_ACCEL_1_FS_AVL_4_VAL,
					.gain = ST_ACCEL_1_FS_AVL_4_GAIN,
				},
				[2] = {
					.num = ST_ACCEL_FS_AVL_8G,
					.value = ST_ACCEL_1_FS_AVL_8_VAL,
					.gain = ST_ACCEL_1_FS_AVL_8_GAIN,
				},
				[3] = {
					.num = ST_ACCEL_FS_AVL_16G,
					.value = ST_ACCEL_1_FS_AVL_16_VAL,
					.gain = ST_ACCEL_1_FS_AVL_16_GAIN,
				},
			},
		},
		.bdu = {
			.addr = ST_ACCEL_1_BDU_ADDR,
			.mask = ST_ACCEL_1_BDU_MASK,
		},
		.drdy_irq = {
			.addr = ST_ACCEL_1_DRDY_IRQ_ADDR,
			.mask = ST_ACCEL_1_DRDY_IRQ_MASK,
		},
		.multi_read_bit = ST_ACCEL_1_MULTIREAD_BIT,
		.bootime = 2,
	},
	{
		.wai = ST_ACCEL_2_WAI_EXP,
		.sensors_supported = {
			[0] = LIS331DLH_ACCEL_DEV_NAME,
			[1] = LSM303DL_ACCEL_DEV_NAME,
			[2] = LSM303DLH_ACCEL_DEV_NAME,
			[3] = LSM303DLM_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_12bit_channels,
		.odr = {
			.addr = ST_ACCEL_2_ODR_ADDR,
			.mask = ST_ACCEL_2_ODR_MASK,
			.odr_avl = {
				{ 50, ST_ACCEL_2_ODR_AVL_50HZ_VAL, },
				{ 100, ST_ACCEL_2_ODR_AVL_100HZ_VAL, },
				{ 400, ST_ACCEL_2_ODR_AVL_400HZ_VAL, },
				{ 1000, ST_ACCEL_2_ODR_AVL_1000HZ_VAL, },
			},
		},
		.pw = {
			.addr = ST_ACCEL_2_PW_ADDR,
			.mask = ST_ACCEL_2_PW_MASK,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = ST_ACCEL_2_FS_ADDR,
			.mask = ST_ACCEL_2_FS_MASK,
			.fs_avl = {
				[0] = {
					.num = ST_ACCEL_FS_AVL_2G,
					.value = ST_ACCEL_2_FS_AVL_2_VAL,
					.gain = ST_ACCEL_2_FS_AVL_2_GAIN,
				},
				[1] = {
					.num = ST_ACCEL_FS_AVL_4G,
					.value = ST_ACCEL_2_FS_AVL_4_VAL,
					.gain = ST_ACCEL_2_FS_AVL_4_GAIN,
				},
				[2] = {
					.num = ST_ACCEL_FS_AVL_8G,
					.value = ST_ACCEL_2_FS_AVL_8_VAL,
					.gain = ST_ACCEL_2_FS_AVL_8_GAIN,
				},
			},
		},
		.bdu = {
			.addr = ST_ACCEL_2_BDU_ADDR,
			.mask = ST_ACCEL_2_BDU_MASK,
		},
		.drdy_irq = {
			.addr = ST_ACCEL_2_DRDY_IRQ_ADDR,
			.mask = ST_ACCEL_2_DRDY_IRQ_MASK,
		},
		.multi_read_bit = ST_ACCEL_2_MULTIREAD_BIT,
		.bootime = 2,
	},
	{
		.wai = ST_ACCEL_3_WAI_EXP,
		.sensors_supported = {
			[0] = LSM330_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_16bit_channels,
		.odr = {
			.addr = ST_ACCEL_3_ODR_ADDR,
			.mask = ST_ACCEL_3_ODR_MASK,
			.odr_avl = {
				{ 3, ST_ACCEL_3_ODR_AVL_3HZ_VAL },
				{ 6, ST_ACCEL_3_ODR_AVL_6HZ_VAL, },
				{ 12, ST_ACCEL_3_ODR_AVL_12HZ_VAL, },
				{ 25, ST_ACCEL_3_ODR_AVL_25HZ_VAL, },
				{ 50, ST_ACCEL_3_ODR_AVL_50HZ_VAL, },
				{ 100, ST_ACCEL_3_ODR_AVL_100HZ_VAL, },
				{ 200, ST_ACCEL_3_ODR_AVL_200HZ_VAL, },
				{ 400, ST_ACCEL_3_ODR_AVL_400HZ_VAL, },
				{ 800, ST_ACCEL_3_ODR_AVL_800HZ_VAL, },
				{ 1600, ST_ACCEL_3_ODR_AVL_1600HZ_VAL, },
			},
		},
		.pw = {
			.addr = ST_ACCEL_3_ODR_ADDR,
			.mask = ST_ACCEL_3_ODR_MASK,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = ST_ACCEL_3_FS_ADDR,
			.mask = ST_ACCEL_3_FS_MASK,
			.fs_avl = {
				[0] = {
					.num = ST_ACCEL_FS_AVL_2G,
					.value = ST_ACCEL_3_FS_AVL_2_VAL,
					.gain = ST_ACCEL_3_FS_AVL_2_GAIN,
				},
				[1] = {
					.num = ST_ACCEL_FS_AVL_4G,
					.value = ST_ACCEL_3_FS_AVL_4_VAL,
					.gain = ST_ACCEL_3_FS_AVL_4_GAIN,
				},
				[2] = {
					.num = ST_ACCEL_FS_AVL_6G,
					.value = ST_ACCEL_3_FS_AVL_6_VAL,
					.gain = ST_ACCEL_3_FS_AVL_6_GAIN,
				},
				[3] = {
					.num = ST_ACCEL_FS_AVL_8G,
					.value = ST_ACCEL_3_FS_AVL_8_VAL,
					.gain = ST_ACCEL_3_FS_AVL_8_GAIN,
				},
				[4] = {
					.num = ST_ACCEL_FS_AVL_16G,
					.value = ST_ACCEL_3_FS_AVL_16_VAL,
					.gain = ST_ACCEL_3_FS_AVL_16_GAIN,
				},
			},
		},
		.bdu = {
			.addr = ST_ACCEL_3_BDU_ADDR,
			.mask = ST_ACCEL_3_BDU_MASK,
		},
		.drdy_irq = {
			.addr = ST_ACCEL_3_DRDY_IRQ_ADDR,
			.mask = ST_ACCEL_3_DRDY_IRQ_MASK,
			.ig1 = {
				.en_addr = ST_ACCEL_3_IG1_EN_ADDR,
				.en_mask = ST_ACCEL_3_IG1_EN_MASK,
			},
		},
		.multi_read_bit = ST_ACCEL_3_MULTIREAD_BIT,
		.bootime = 2,
	},
};

static int st_accel_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *ch, int *val,
							int *val2, long mask)
{
	int err;
	struct st_sensor_data *adata = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = st_sensors_read_info_raw(indio_dev, ch, val);
		if (err < 0)
			goto read_error;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = adata->current_fullscale->gain;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}

read_error:
	return err;
}

static int st_accel_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = st_sensors_set_fullscale_by_gain(indio_dev, val2);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

static ST_SENSOR_DEV_ATTR_SAMP_FREQ();
static ST_SENSORS_DEV_ATTR_SAMP_FREQ_AVAIL();
static ST_SENSORS_DEV_ATTR_SCALE_AVAIL(in_accel_scale_available);

static struct attribute *st_accel_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_accel_attribute_group = {
	.attrs = st_accel_attributes,
};

static const struct iio_info accel_info = {
	.driver_module = THIS_MODULE,
	.attrs = &st_accel_attribute_group,
	.read_raw = &st_accel_read_raw,
	.write_raw = &st_accel_write_raw,
};

#ifdef CONFIG_IIO_TRIGGER
static const struct iio_trigger_ops st_accel_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = ST_ACCEL_TRIGGER_SET_STATE,
};
#define ST_ACCEL_TRIGGER_OPS (&st_accel_trigger_ops)
#else
#define ST_ACCEL_TRIGGER_OPS NULL
#endif

int st_accel_common_probe(struct iio_dev *indio_dev)
{
	int err;
	struct st_sensor_data *adata = iio_priv(indio_dev);

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &accel_info;

	err = st_sensors_check_device_support(indio_dev,
				ARRAY_SIZE(st_accel_sensors), st_accel_sensors);
	if (err < 0)
		goto st_accel_common_probe_error;

	adata->multiread_bit = adata->sensor->multi_read_bit;
	indio_dev->channels = adata->sensor->ch;
	indio_dev->num_channels = ST_SENSORS_NUMBER_ALL_CHANNELS;

	adata->current_fullscale = (struct st_sensor_fullscale_avl *)
						&adata->sensor->fs.fs_avl[0];
	adata->odr = adata->sensor->odr.odr_avl[0].hz;

	err = st_sensors_init_sensor(indio_dev);
	if (err < 0)
		goto st_accel_common_probe_error;

	if (adata->get_irq_data_ready(indio_dev) > 0) {
		err = st_accel_allocate_ring(indio_dev);
		if (err < 0)
			goto st_accel_common_probe_error;

		err = st_sensors_allocate_trigger(indio_dev,
						 ST_ACCEL_TRIGGER_OPS);
		if (err < 0)
			goto st_accel_probe_trigger_error;
	}

	err = iio_device_register(indio_dev);
	if (err)
		goto st_accel_device_register_error;

	return err;

st_accel_device_register_error:
	if (adata->get_irq_data_ready(indio_dev) > 0)
		st_sensors_deallocate_trigger(indio_dev);
st_accel_probe_trigger_error:
	if (adata->get_irq_data_ready(indio_dev) > 0)
		st_accel_deallocate_ring(indio_dev);
st_accel_common_probe_error:
	return err;
}
EXPORT_SYMBOL(st_accel_common_probe);

void st_accel_common_remove(struct iio_dev *indio_dev)
{
	struct st_sensor_data *adata = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (adata->get_irq_data_ready(indio_dev) > 0) {
		st_sensors_deallocate_trigger(indio_dev);
		st_accel_deallocate_ring(indio_dev);
	}
	iio_device_free(indio_dev);
}
EXPORT_SYMBOL(st_accel_common_remove);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics accelerometers driver");
MODULE_LICENSE("GPL v2");
