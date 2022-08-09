// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics pressures driver
 *
 * Copyright 2013 STMicroelectronics Inc.
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
#include <asm/unaligned.h>

#include <linux/iio/common/st_sensors.h>
#include "st_pressure.h"

/*
 * About determining pressure scaling factors
 * ------------------------------------------
 *
 * Datasheets specify typical pressure sensitivity so that pressure is computed
 * according to the following equation :
 *     pressure[mBar] = raw / sensitivity
 * where :
 *     raw          the 24 bits long raw sampled pressure
 *     sensitivity  a scaling factor specified by the datasheet in LSB/mBar
 *
 * IIO ABI expects pressure to be expressed as kPascal, hence pressure should be
 * computed according to :
 *     pressure[kPascal] = pressure[mBar] / 10
 *                       = raw / (sensitivity * 10)                          (1)
 *
 * Finally, st_press_read_raw() returns pressure scaling factor as an
 * IIO_VAL_INT_PLUS_NANO with a zero integral part and "gain" as decimal part.
 * Therefore, from (1), "gain" becomes :
 *     gain = 10^9 / (sensitivity * 10)
 *          = 10^8 / sensitivity
 *
 * About determining temperature scaling factors and offsets
 * ---------------------------------------------------------
 *
 * Datasheets specify typical temperature sensitivity and offset so that
 * temperature is computed according to the following equation :
 *     temp[Celsius] = offset[Celsius] + (raw / sensitivity)
 * where :
 *     raw          the 16 bits long raw sampled temperature
 *     offset       a constant specified by the datasheet in degree Celsius
 *                  (sometimes zero)
 *     sensitivity  a scaling factor specified by the datasheet in LSB/Celsius
 *
 * IIO ABI expects temperature to be expressed as milli degree Celsius such as
 * user space should compute temperature according to :
 *     temp[mCelsius] = temp[Celsius] * 10^3
 *                    = (offset[Celsius] + (raw / sensitivity)) * 10^3
 *                    = ((offset[Celsius] * sensitivity) + raw) *
 *                      (10^3 / sensitivity)                                 (2)
 *
 * IIO ABI expects user space to apply offset and scaling factors to raw samples
 * according to :
 *     temp[mCelsius] = (OFFSET + raw) * SCALE
 * where :
 *     OFFSET an arbitrary constant exposed by device
 *     SCALE  an arbitrary scaling factor exposed by device
 *
 * Matching OFFSET and SCALE with members of (2) gives :
 *     OFFSET = offset[Celsius] * sensitivity                                (3)
 *     SCALE  = 10^3 / sensitivity                                           (4)
 *
 * st_press_read_raw() returns temperature scaling factor as an
 * IIO_VAL_FRACTIONAL with a 10^3 numerator and "gain2" as denominator.
 * Therefore, from (3), "gain2" becomes :
 *     gain2 = sensitivity
 *
 * When declared within channel, i.e. for a non zero specified offset,
 * st_press_read_raw() will return the latter as an IIO_VAL_FRACTIONAL such as :
 *     numerator = OFFSET * 10^3
 *     denominator = 10^3
 * giving from (4):
 *     numerator = offset[Celsius] * 10^3 * sensitivity
 *               = offset[mCelsius] * gain2
 */

#define MCELSIUS_PER_CELSIUS			1000

/* Default pressure sensitivity */
#define ST_PRESS_LSB_PER_MBAR			4096UL
#define ST_PRESS_KPASCAL_NANO_SCALE		(100000000UL / \
						 ST_PRESS_LSB_PER_MBAR)

/* Default temperature sensitivity */
#define ST_PRESS_LSB_PER_CELSIUS		480UL
#define ST_PRESS_MILLI_CELSIUS_OFFSET		42500UL

/* FULLSCALE */
#define ST_PRESS_FS_AVL_1100MB			1100
#define ST_PRESS_FS_AVL_1260MB			1260

#define ST_PRESS_1_OUT_XL_ADDR			0x28
#define ST_TEMP_1_OUT_L_ADDR			0x2b

/* LPS001WP pressure resolution */
#define ST_PRESS_LPS001WP_LSB_PER_MBAR		16UL
/* LPS001WP temperature resolution */
#define ST_PRESS_LPS001WP_LSB_PER_CELSIUS	64UL
/* LPS001WP pressure gain */
#define ST_PRESS_LPS001WP_FS_AVL_PRESS_GAIN \
	(100000000UL / ST_PRESS_LPS001WP_LSB_PER_MBAR)
/* LPS001WP pressure and temp L addresses */
#define ST_PRESS_LPS001WP_OUT_L_ADDR		0x28
#define ST_TEMP_LPS001WP_OUT_L_ADDR		0x2a

/* LPS25H pressure and temp L addresses */
#define ST_PRESS_LPS25H_OUT_XL_ADDR		0x28
#define ST_TEMP_LPS25H_OUT_L_ADDR		0x2b

/* LPS22HB temperature sensitivity */
#define ST_PRESS_LPS22HB_LSB_PER_CELSIUS	100UL

static const struct iio_chan_spec st_press_1_channels[] = {
	{
		.type = IIO_PRESSURE,
		.address = ST_PRESS_1_OUT_XL_ADDR,
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_LE,
		},
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
	{
		.type = IIO_TEMP,
		.address = ST_TEMP_1_OUT_L_ADDR,
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
	IIO_CHAN_SOFT_TIMESTAMP(2)
};

static const struct iio_chan_spec st_press_lps001wp_channels[] = {
	{
		.type = IIO_PRESSURE,
		.address = ST_PRESS_LPS001WP_OUT_L_ADDR,
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_TEMP,
		.address = ST_TEMP_LPS001WP_OUT_L_ADDR,
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
	},
	IIO_CHAN_SOFT_TIMESTAMP(2)
};

static const struct iio_chan_spec st_press_lps22hb_channels[] = {
	{
		.type = IIO_PRESSURE,
		.address = ST_PRESS_1_OUT_XL_ADDR,
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_LE,
		},
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
	{
		.type = IIO_TEMP,
		.address = ST_TEMP_1_OUT_L_ADDR,
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
	IIO_CHAN_SOFT_TIMESTAMP(2)
};

static const struct st_sensor_settings st_press_sensors_settings[] = {
	{
		/*
		 * CUSTOM VALUES FOR LPS331AP SENSOR
		 * See LPS331AP datasheet:
		 * http://www2.st.com/resource/en/datasheet/lps331ap.pdf
		 */
		.wai = 0xbb,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LPS331AP_PRESS_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_press_1_channels,
		.num_ch = ARRAY_SIZE(st_press_1_channels),
		.odr = {
			.addr = 0x20,
			.mask = 0x70,
			.odr_avl = {
				{ .hz = 1, .value = 0x01 },
				{ .hz = 7, .value = 0x05 },
				{ .hz = 13, .value = 0x06 },
				{ .hz = 25, .value = 0x07 },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0x80,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.fs = {
			.addr = 0x23,
			.mask = 0x30,
			.fs_avl = {
				/*
				 * Pressure and temperature sensitivity values
				 * as defined in table 3 of LPS331AP datasheet.
				 */
				[0] = {
					.num = ST_PRESS_FS_AVL_1260MB,
					.gain = ST_PRESS_KPASCAL_NANO_SCALE,
					.gain2 = ST_PRESS_LSB_PER_CELSIUS,
				},
			},
		},
		.bdu = {
			.addr = 0x20,
			.mask = 0x04,
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x22,
				.mask = 0x04,
				.addr_od = 0x22,
				.mask_od = 0x40,
			},
			.int2 = {
				.addr = 0x22,
				.mask = 0x20,
				.addr_od = 0x22,
				.mask_od = 0x40,
			},
			.addr_ihl = 0x22,
			.mask_ihl = 0x80,
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x03,
			},
		},
		.sim = {
			.addr = 0x20,
			.value = BIT(0),
		},
		.multi_read_bit = true,
		.bootime = 2,
	},
	{
		/*
		 * CUSTOM VALUES FOR LPS001WP SENSOR
		 */
		.wai = 0xba,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LPS001WP_PRESS_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_press_lps001wp_channels,
		.num_ch = ARRAY_SIZE(st_press_lps001wp_channels),
		.odr = {
			.addr = 0x20,
			.mask = 0x30,
			.odr_avl = {
				{ .hz = 1, .value = 0x01 },
				{ .hz = 7, .value = 0x02 },
				{ .hz = 13, .value = 0x03 },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0x40,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.fs = {
			.fs_avl = {
				/*
				 * Pressure and temperature resolution values
				 * as defined in table 3 of LPS001WP datasheet.
				 */
				[0] = {
					.num = ST_PRESS_FS_AVL_1100MB,
					.gain = ST_PRESS_LPS001WP_FS_AVL_PRESS_GAIN,
					.gain2 = ST_PRESS_LPS001WP_LSB_PER_CELSIUS,
				},
			},
		},
		.bdu = {
			.addr = 0x20,
			.mask = 0x04,
		},
		.sim = {
			.addr = 0x20,
			.value = BIT(0),
		},
		.multi_read_bit = true,
		.bootime = 2,
	},
	{
		/*
		 * CUSTOM VALUES FOR LPS25H SENSOR
		 * See LPS25H datasheet:
		 * http://www2.st.com/resource/en/datasheet/lps25h.pdf
		 */
		.wai = 0xbd,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LPS25H_PRESS_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_press_1_channels,
		.num_ch = ARRAY_SIZE(st_press_1_channels),
		.odr = {
			.addr = 0x20,
			.mask = 0x70,
			.odr_avl = {
				{ .hz = 1, .value = 0x01 },
				{ .hz = 7, .value = 0x02 },
				{ .hz = 13, .value = 0x03 },
				{ .hz = 25, .value = 0x04 },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0x80,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.fs = {
			.fs_avl = {
				/*
				 * Pressure and temperature sensitivity values
				 * as defined in table 3 of LPS25H datasheet.
				 */
				[0] = {
					.num = ST_PRESS_FS_AVL_1260MB,
					.gain = ST_PRESS_KPASCAL_NANO_SCALE,
					.gain2 = ST_PRESS_LSB_PER_CELSIUS,
				},
			},
		},
		.bdu = {
			.addr = 0x20,
			.mask = 0x04,
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x23,
				.mask = 0x01,
				.addr_od = 0x22,
				.mask_od = 0x40,
			},
			.addr_ihl = 0x22,
			.mask_ihl = 0x80,
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x03,
			},
		},
		.sim = {
			.addr = 0x20,
			.value = BIT(0),
		},
		.multi_read_bit = true,
		.bootime = 2,
	},
	{
		/*
		 * CUSTOM VALUES FOR LPS22HB SENSOR
		 * See LPS22HB datasheet:
		 * http://www2.st.com/resource/en/datasheet/lps22hb.pdf
		 */
		.wai = 0xb1,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LPS22HB_PRESS_DEV_NAME,
			[1] = LPS33HW_PRESS_DEV_NAME,
			[2] = LPS35HW_PRESS_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_press_lps22hb_channels,
		.num_ch = ARRAY_SIZE(st_press_lps22hb_channels),
		.odr = {
			.addr = 0x10,
			.mask = 0x70,
			.odr_avl = {
				{ .hz = 1, .value = 0x01 },
				{ .hz = 10, .value = 0x02 },
				{ .hz = 25, .value = 0x03 },
				{ .hz = 50, .value = 0x04 },
				{ .hz = 75, .value = 0x05 },
			},
		},
		.pw = {
			.addr = 0x10,
			.mask = 0x70,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.fs = {
			.fs_avl = {
				/*
				 * Pressure and temperature sensitivity values
				 * as defined in table 3 of LPS22HB datasheet.
				 */
				[0] = {
					.num = ST_PRESS_FS_AVL_1260MB,
					.gain = ST_PRESS_KPASCAL_NANO_SCALE,
					.gain2 = ST_PRESS_LPS22HB_LSB_PER_CELSIUS,
				},
			},
		},
		.bdu = {
			.addr = 0x10,
			.mask = 0x02,
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x12,
				.mask = 0x04,
				.addr_od = 0x12,
				.mask_od = 0x40,
			},
			.addr_ihl = 0x12,
			.mask_ihl = 0x80,
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x03,
			},
		},
		.sim = {
			.addr = 0x10,
			.value = BIT(0),
		},
		.multi_read_bit = false,
		.bootime = 2,
	},
	{
		/*
		 * CUSTOM VALUES FOR LPS22HH SENSOR
		 * See LPS22HH datasheet:
		 * http://www2.st.com/resource/en/datasheet/lps22hh.pdf
		 */
		.wai = 0xb3,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LPS22HH_PRESS_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_press_lps22hb_channels,
		.num_ch = ARRAY_SIZE(st_press_lps22hb_channels),
		.odr = {
			.addr = 0x10,
			.mask = 0x70,
			.odr_avl = {
				{ .hz = 1, .value = 0x01 },
				{ .hz = 10, .value = 0x02 },
				{ .hz = 25, .value = 0x03 },
				{ .hz = 50, .value = 0x04 },
				{ .hz = 75, .value = 0x05 },
				{ .hz = 100, .value = 0x06 },
				{ .hz = 200, .value = 0x07 },
			},
		},
		.pw = {
			.addr = 0x10,
			.mask = 0x70,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.fs = {
			.fs_avl = {
				/*
				 * Pressure and temperature sensitivity values
				 * as defined in table 3 of LPS22HH datasheet.
				 */
				[0] = {
					.num = ST_PRESS_FS_AVL_1260MB,
					.gain = ST_PRESS_KPASCAL_NANO_SCALE,
					.gain2 = ST_PRESS_LPS22HB_LSB_PER_CELSIUS,
				},
			},
		},
		.bdu = {
			.addr = 0x10,
			.mask = BIT(1),
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x12,
				.mask = BIT(2),
				.addr_od = 0x11,
				.mask_od = BIT(5),
			},
			.addr_ihl = 0x11,
			.mask_ihl = BIT(6),
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x03,
			},
		},
		.sim = {
			.addr = 0x10,
			.value = BIT(0),
		},
		.multi_read_bit = false,
		.bootime = 2,
	},
};

static int st_press_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *ch,
			      int val,
			      int val2,
			      long mask)
{
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val2)
			return -EINVAL;
		mutex_lock(&indio_dev->mlock);
		err = st_sensors_set_odr(indio_dev, val);
		mutex_unlock(&indio_dev->mlock);
		return err;
	default:
		return -EINVAL;
	}
}

static int st_press_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *ch, int *val,
							int *val2, long mask)
{
	int err;
	struct st_sensor_data *press_data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = st_sensors_read_info_raw(indio_dev, ch, val);
		if (err < 0)
			goto read_error;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (ch->type) {
		case IIO_PRESSURE:
			*val = 0;
			*val2 = press_data->current_fullscale->gain;
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_TEMP:
			*val = MCELSIUS_PER_CELSIUS;
			*val2 = press_data->current_fullscale->gain2;
			return IIO_VAL_FRACTIONAL;
		default:
			err = -EINVAL;
			goto read_error;
		}

	case IIO_CHAN_INFO_OFFSET:
		switch (ch->type) {
		case IIO_TEMP:
			*val = ST_PRESS_MILLI_CELSIUS_OFFSET *
			       press_data->current_fullscale->gain2;
			*val2 = MCELSIUS_PER_CELSIUS;
			break;
		default:
			err = -EINVAL;
			goto read_error;
		}

		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = press_data->odr;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}

read_error:
	return err;
}

static ST_SENSORS_DEV_ATTR_SAMP_FREQ_AVAIL();

static struct attribute *st_press_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_press_attribute_group = {
	.attrs = st_press_attributes,
};

static const struct iio_info press_info = {
	.attrs = &st_press_attribute_group,
	.read_raw = &st_press_read_raw,
	.write_raw = &st_press_write_raw,
	.debugfs_reg_access = &st_sensors_debugfs_reg_access,
};

#ifdef CONFIG_IIO_TRIGGER
static const struct iio_trigger_ops st_press_trigger_ops = {
	.set_trigger_state = ST_PRESS_TRIGGER_SET_STATE,
	.validate_device = st_sensors_validate_device,
};
#define ST_PRESS_TRIGGER_OPS (&st_press_trigger_ops)
#else
#define ST_PRESS_TRIGGER_OPS NULL
#endif

/*
 * st_press_get_settings() - get sensor settings from device name
 * @name: device name buffer reference.
 *
 * Return: valid reference on success, NULL otherwise.
 */
const struct st_sensor_settings *st_press_get_settings(const char *name)
{
	int index = st_sensors_get_settings_index(name,
					st_press_sensors_settings,
					ARRAY_SIZE(st_press_sensors_settings));
	if (index < 0)
		return NULL;

	return &st_press_sensors_settings[index];
}
EXPORT_SYMBOL_NS(st_press_get_settings, IIO_ST_SENSORS);

int st_press_common_probe(struct iio_dev *indio_dev)
{
	struct st_sensor_data *press_data = iio_priv(indio_dev);
	struct device *parent = indio_dev->dev.parent;
	struct st_sensors_platform_data *pdata = dev_get_platdata(parent);
	int err;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &press_info;

	err = st_sensors_verify_id(indio_dev);
	if (err < 0)
		return err;

	/*
	 * Skip timestamping channel while declaring available channels to
	 * common st_sensor layer. Look at st_sensors_get_buffer_element() to
	 * see how timestamps are explicitly pushed as last samples block
	 * element.
	 */
	press_data->num_data_channels = press_data->sensor_settings->num_ch - 1;
	indio_dev->channels = press_data->sensor_settings->ch;
	indio_dev->num_channels = press_data->sensor_settings->num_ch;

	press_data->current_fullscale = &press_data->sensor_settings->fs.fs_avl[0];

	press_data->odr = press_data->sensor_settings->odr.odr_avl[0].hz;

	/* Some devices don't support a data ready pin. */
	if (!pdata && (press_data->sensor_settings->drdy_irq.int1.addr ||
		       press_data->sensor_settings->drdy_irq.int2.addr))
		pdata =	(struct st_sensors_platform_data *)&default_press_pdata;

	err = st_sensors_init_sensor(indio_dev, pdata);
	if (err < 0)
		return err;

	err = st_press_allocate_ring(indio_dev);
	if (err < 0)
		return err;

	if (press_data->irq > 0) {
		err = st_sensors_allocate_trigger(indio_dev,
						  ST_PRESS_TRIGGER_OPS);
		if (err < 0)
			return err;
	}

	return devm_iio_device_register(parent, indio_dev);
}
EXPORT_SYMBOL_NS(st_press_common_probe, IIO_ST_SENSORS);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics pressures driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_ST_SENSORS);
