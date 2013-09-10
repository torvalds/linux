/*
 * STMicroelectronics pressures driver
 *
 * Copyright 2013 STMicroelectronics Inc.
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
#include <asm/unaligned.h>

#include <linux/iio/common/st_sensors.h>
#include "st_pressure.h"

#define ST_PRESS_LSB_PER_MBAR			4096UL
#define ST_PRESS_KPASCAL_NANO_SCALE		(100000000UL / \
						 ST_PRESS_LSB_PER_MBAR)
#define ST_PRESS_LSB_PER_CELSIUS		480UL
#define ST_PRESS_CELSIUS_NANO_SCALE		(1000000000UL / \
						 ST_PRESS_LSB_PER_CELSIUS)
#define ST_PRESS_NUMBER_DATA_CHANNELS		1

/* FULLSCALE */
#define ST_PRESS_FS_AVL_1260MB			1260

/* CUSTOM VALUES FOR LPS331AP SENSOR */
#define ST_PRESS_LPS331AP_WAI_EXP		0xbb
#define ST_PRESS_LPS331AP_ODR_ADDR		0x20
#define ST_PRESS_LPS331AP_ODR_MASK		0x70
#define ST_PRESS_LPS331AP_ODR_AVL_1HZ_VAL	0x01
#define ST_PRESS_LPS331AP_ODR_AVL_7HZ_VAL	0x05
#define ST_PRESS_LPS331AP_ODR_AVL_13HZ_VAL	0x06
#define ST_PRESS_LPS331AP_ODR_AVL_25HZ_VAL	0x07
#define ST_PRESS_LPS331AP_PW_ADDR		0x20
#define ST_PRESS_LPS331AP_PW_MASK		0x80
#define ST_PRESS_LPS331AP_FS_ADDR		0x23
#define ST_PRESS_LPS331AP_FS_MASK		0x30
#define ST_PRESS_LPS331AP_FS_AVL_1260_VAL	0x00
#define ST_PRESS_LPS331AP_FS_AVL_1260_GAIN	ST_PRESS_KPASCAL_NANO_SCALE
#define ST_PRESS_LPS331AP_FS_AVL_TEMP_GAIN	ST_PRESS_CELSIUS_NANO_SCALE
#define ST_PRESS_LPS331AP_BDU_ADDR		0x20
#define ST_PRESS_LPS331AP_BDU_MASK		0x04
#define ST_PRESS_LPS331AP_DRDY_IRQ_ADDR		0x22
#define ST_PRESS_LPS331AP_DRDY_IRQ_INT1_MASK	0x04
#define ST_PRESS_LPS331AP_DRDY_IRQ_INT2_MASK	0x20
#define ST_PRESS_LPS331AP_MULTIREAD_BIT		true
#define ST_PRESS_LPS331AP_TEMP_OFFSET		42500
#define ST_PRESS_LPS331AP_OUT_XL_ADDR		0x28
#define ST_TEMP_LPS331AP_OUT_L_ADDR		0x2b

static const struct iio_chan_spec st_press_channels[] = {
	ST_SENSORS_LSM_CHANNELS(IIO_PRESSURE,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 0, IIO_NO_MOD, 'u', IIO_LE, 24, 24,
			ST_PRESS_LPS331AP_OUT_XL_ADDR),
	ST_SENSORS_LSM_CHANNELS(IIO_TEMP,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE) |
						BIT(IIO_CHAN_INFO_OFFSET),
			-1, 0, IIO_NO_MOD, 's', IIO_LE, 16, 16,
			ST_TEMP_LPS331AP_OUT_L_ADDR),
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

static const struct st_sensors st_press_sensors[] = {
	{
		.wai = ST_PRESS_LPS331AP_WAI_EXP,
		.sensors_supported = {
			[0] = LPS331AP_PRESS_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_press_channels,
		.odr = {
			.addr = ST_PRESS_LPS331AP_ODR_ADDR,
			.mask = ST_PRESS_LPS331AP_ODR_MASK,
			.odr_avl = {
				{ 1, ST_PRESS_LPS331AP_ODR_AVL_1HZ_VAL, },
				{ 7, ST_PRESS_LPS331AP_ODR_AVL_7HZ_VAL, },
				{ 13, ST_PRESS_LPS331AP_ODR_AVL_13HZ_VAL, },
				{ 25, ST_PRESS_LPS331AP_ODR_AVL_25HZ_VAL, },
			},
		},
		.pw = {
			.addr = ST_PRESS_LPS331AP_PW_ADDR,
			.mask = ST_PRESS_LPS331AP_PW_MASK,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.fs = {
			.addr = ST_PRESS_LPS331AP_FS_ADDR,
			.mask = ST_PRESS_LPS331AP_FS_MASK,
			.fs_avl = {
				[0] = {
					.num = ST_PRESS_FS_AVL_1260MB,
					.value = ST_PRESS_LPS331AP_FS_AVL_1260_VAL,
					.gain = ST_PRESS_LPS331AP_FS_AVL_1260_GAIN,
					.gain2 = ST_PRESS_LPS331AP_FS_AVL_TEMP_GAIN,
				},
			},
		},
		.bdu = {
			.addr = ST_PRESS_LPS331AP_BDU_ADDR,
			.mask = ST_PRESS_LPS331AP_BDU_MASK,
		},
		.drdy_irq = {
			.addr = ST_PRESS_LPS331AP_DRDY_IRQ_ADDR,
			.mask_int1 = ST_PRESS_LPS331AP_DRDY_IRQ_INT1_MASK,
			.mask_int2 = ST_PRESS_LPS331AP_DRDY_IRQ_INT2_MASK,
		},
		.multi_read_bit = ST_PRESS_LPS331AP_MULTIREAD_BIT,
		.bootime = 2,
	},
};

static int st_press_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *ch, int *val,
							int *val2, long mask)
{
	int err;
	struct st_sensor_data *pdata = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = st_sensors_read_info_raw(indio_dev, ch, val);
		if (err < 0)
			goto read_error;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;

		switch (ch->type) {
		case IIO_PRESSURE:
			*val2 = pdata->current_fullscale->gain;
			break;
		case IIO_TEMP:
			*val2 = pdata->current_fullscale->gain2;
			break;
		default:
			err = -EINVAL;
			goto read_error;
		}

		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OFFSET:
		switch (ch->type) {
		case IIO_TEMP:
			*val = 425;
			*val2 = 10;
			break;
		default:
			err = -EINVAL;
			goto read_error;
		}

		return IIO_VAL_FRACTIONAL;
	default:
		return -EINVAL;
	}

read_error:
	return err;
}

static ST_SENSOR_DEV_ATTR_SAMP_FREQ();
static ST_SENSORS_DEV_ATTR_SAMP_FREQ_AVAIL();

static struct attribute *st_press_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_press_attribute_group = {
	.attrs = st_press_attributes,
};

static const struct iio_info press_info = {
	.driver_module = THIS_MODULE,
	.attrs = &st_press_attribute_group,
	.read_raw = &st_press_read_raw,
};

#ifdef CONFIG_IIO_TRIGGER
static const struct iio_trigger_ops st_press_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = ST_PRESS_TRIGGER_SET_STATE,
};
#define ST_PRESS_TRIGGER_OPS (&st_press_trigger_ops)
#else
#define ST_PRESS_TRIGGER_OPS NULL
#endif

int st_press_common_probe(struct iio_dev *indio_dev,
				struct st_sensors_platform_data *plat_data)
{
	int err;
	struct st_sensor_data *pdata = iio_priv(indio_dev);

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &press_info;

	err = st_sensors_check_device_support(indio_dev,
				ARRAY_SIZE(st_press_sensors), st_press_sensors);
	if (err < 0)
		goto st_press_common_probe_error;

	pdata->num_data_channels = ST_PRESS_NUMBER_DATA_CHANNELS;
	pdata->multiread_bit = pdata->sensor->multi_read_bit;
	indio_dev->channels = pdata->sensor->ch;
	indio_dev->num_channels = ARRAY_SIZE(st_press_channels);

	if (pdata->sensor->fs.addr != 0)
		pdata->current_fullscale = (struct st_sensor_fullscale_avl *)
			&pdata->sensor->fs.fs_avl[0];

	pdata->odr = pdata->sensor->odr.odr_avl[0].hz;

	if (!plat_data)
		plat_data =
			(struct st_sensors_platform_data *)&default_press_pdata;

	err = st_sensors_init_sensor(indio_dev, plat_data);
	if (err < 0)
		goto st_press_common_probe_error;

	if (pdata->get_irq_data_ready(indio_dev) > 0) {
		err = st_press_allocate_ring(indio_dev);
		if (err < 0)
			goto st_press_common_probe_error;

		err = st_sensors_allocate_trigger(indio_dev,
							ST_PRESS_TRIGGER_OPS);
		if (err < 0)
			goto st_press_probe_trigger_error;
	}

	err = iio_device_register(indio_dev);
	if (err)
		goto st_press_device_register_error;

	return err;

st_press_device_register_error:
	if (pdata->get_irq_data_ready(indio_dev) > 0)
		st_sensors_deallocate_trigger(indio_dev);
st_press_probe_trigger_error:
	if (pdata->get_irq_data_ready(indio_dev) > 0)
		st_press_deallocate_ring(indio_dev);
st_press_common_probe_error:
	return err;
}
EXPORT_SYMBOL(st_press_common_probe);

void st_press_common_remove(struct iio_dev *indio_dev)
{
	struct st_sensor_data *pdata = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (pdata->get_irq_data_ready(indio_dev) > 0) {
		st_sensors_deallocate_trigger(indio_dev);
		st_press_deallocate_ring(indio_dev);
	}
}
EXPORT_SYMBOL(st_press_common_remove);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics pressures driver");
MODULE_LICENSE("GPL v2");
