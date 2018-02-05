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

#define ST_ACCEL_NUMBER_DATA_CHANNELS		3

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
#define ST_ACCEL_FS_AVL_100G			100
#define ST_ACCEL_FS_AVL_200G			200
#define ST_ACCEL_FS_AVL_400G			400

static const struct iio_chan_spec st_accel_8bit_channels[] = {
	ST_SENSORS_LSM_CHANNELS(IIO_ACCEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_X, 1, IIO_MOD_X, 's', IIO_LE, 8, 8,
			ST_ACCEL_DEFAULT_OUT_X_L_ADDR+1),
	ST_SENSORS_LSM_CHANNELS(IIO_ACCEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Y, 1, IIO_MOD_Y, 's', IIO_LE, 8, 8,
			ST_ACCEL_DEFAULT_OUT_Y_L_ADDR+1),
	ST_SENSORS_LSM_CHANNELS(IIO_ACCEL,
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
			ST_SENSORS_SCAN_Z, 1, IIO_MOD_Z, 's', IIO_LE, 8, 8,
			ST_ACCEL_DEFAULT_OUT_Z_L_ADDR+1),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

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

static const struct st_sensor_settings st_accel_sensors_settings[] = {
	{
		.wai = 0x33,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LIS3DH_ACCEL_DEV_NAME,
			[1] = LSM303DLHC_ACCEL_DEV_NAME,
			[2] = LSM330D_ACCEL_DEV_NAME,
			[3] = LSM330DL_ACCEL_DEV_NAME,
			[4] = LSM330DLC_ACCEL_DEV_NAME,
			[5] = LSM303AGR_ACCEL_DEV_NAME,
			[6] = LIS2DH12_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_12bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0xf0,
			.odr_avl = {
				{ .hz = 1, .value = 0x01, },
				{ .hz = 10, .value = 0x02, },
				{ .hz = 25, .value = 0x03, },
				{ .hz = 50, .value = 0x04, },
				{ .hz = 100, .value = 0x05, },
				{ .hz = 200, .value = 0x06, },
				{ .hz = 400, .value = 0x07, },
				{ .hz = 1600, .value = 0x08, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0xf0,
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
					.num = ST_ACCEL_FS_AVL_2G,
					.value = 0x00,
					.gain = IIO_G_TO_M_S_2(1000),
				},
				[1] = {
					.num = ST_ACCEL_FS_AVL_4G,
					.value = 0x01,
					.gain = IIO_G_TO_M_S_2(2000),
				},
				[2] = {
					.num = ST_ACCEL_FS_AVL_8G,
					.value = 0x02,
					.gain = IIO_G_TO_M_S_2(4000),
				},
				[3] = {
					.num = ST_ACCEL_FS_AVL_16G,
					.value = 0x03,
					.gain = IIO_G_TO_M_S_2(12000),
				},
			},
		},
		.bdu = {
			.addr = 0x23,
			.mask = 0x80,
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x22,
				.mask = 0x10,
			},
			.addr_ihl = 0x25,
			.mask_ihl = 0x02,
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
		.wai = 0x32,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LIS331DLH_ACCEL_DEV_NAME,
			[1] = LSM303DL_ACCEL_DEV_NAME,
			[2] = LSM303DLH_ACCEL_DEV_NAME,
			[3] = LSM303DLM_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_12bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0x18,
			.odr_avl = {
				{ .hz = 50, .value = 0x00, },
				{ .hz = 100, .value = 0x01, },
				{ .hz = 400, .value = 0x02, },
				{ .hz = 1000, .value = 0x03, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0xe0,
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
					.num = ST_ACCEL_FS_AVL_2G,
					.value = 0x00,
					.gain = IIO_G_TO_M_S_2(1000),
				},
				[1] = {
					.num = ST_ACCEL_FS_AVL_4G,
					.value = 0x01,
					.gain = IIO_G_TO_M_S_2(2000),
				},
				[2] = {
					.num = ST_ACCEL_FS_AVL_8G,
					.value = 0x03,
					.gain = IIO_G_TO_M_S_2(3900),
				},
			},
		},
		.bdu = {
			.addr = 0x23,
			.mask = 0x80,
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x22,
				.mask = 0x02,
				.addr_od = 0x22,
				.mask_od = 0x40,
			},
			.int2 = {
				.addr = 0x22,
				.mask = 0x10,
				.addr_od = 0x22,
				.mask_od = 0x40,
			},
			.addr_ihl = 0x22,
			.mask_ihl = 0x80,
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
		.wai = 0x40,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LSM330_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_16bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0xf0,
			.odr_avl = {
				{ .hz = 3, .value = 0x01, },
				{ .hz = 6, .value = 0x02, },
				{ .hz = 12, .value = 0x03, },
				{ .hz = 25, .value = 0x04, },
				{ .hz = 50, .value = 0x05, },
				{ .hz = 100, .value = 0x06, },
				{ .hz = 200, .value = 0x07, },
				{ .hz = 400, .value = 0x08, },
				{ .hz = 800, .value = 0x09, },
				{ .hz = 1600, .value = 0x0a, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0xf0,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = 0x24,
			.mask = 0x38,
			.fs_avl = {
				[0] = {
					.num = ST_ACCEL_FS_AVL_2G,
					.value = 0x00,
					.gain = IIO_G_TO_M_S_2(61),
				},
				[1] = {
					.num = ST_ACCEL_FS_AVL_4G,
					.value = 0x01,
					.gain = IIO_G_TO_M_S_2(122),
				},
				[2] = {
					.num = ST_ACCEL_FS_AVL_6G,
					.value = 0x02,
					.gain = IIO_G_TO_M_S_2(183),
				},
				[3] = {
					.num = ST_ACCEL_FS_AVL_8G,
					.value = 0x03,
					.gain = IIO_G_TO_M_S_2(244),
				},
				[4] = {
					.num = ST_ACCEL_FS_AVL_16G,
					.value = 0x04,
					.gain = IIO_G_TO_M_S_2(732),
				},
			},
		},
		.bdu = {
			.addr = 0x20,
			.mask = 0x08,
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x23,
				.mask = 0x80,
			},
			.addr_ihl = 0x23,
			.mask_ihl = 0x40,
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x07,
			},
			.ig1 = {
				.en_addr = 0x23,
				.en_mask = 0x08,
			},
		},
		.sim = {
			.addr = 0x24,
			.value = BIT(0),
		},
		.multi_read_bit = false,
		.bootime = 2,
	},
	{
		.wai = 0x3a,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LIS3LV02DL_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_12bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0x30, /* DF1 and DF0 */
			.odr_avl = {
				{ .hz = 40, .value = 0x00, },
				{ .hz = 160, .value = 0x01, },
				{ .hz = 640, .value = 0x02, },
				{ .hz = 2560, .value = 0x03, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0xc0,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = 0x21,
			.mask = 0x80,
			.fs_avl = {
				[0] = {
					.num = ST_ACCEL_FS_AVL_2G,
					.value = 0x00,
					.gain = IIO_G_TO_M_S_2(1000),
				},
				[1] = {
					.num = ST_ACCEL_FS_AVL_6G,
					.value = 0x01,
					.gain = IIO_G_TO_M_S_2(3000),
				},
			},
		},
		.bdu = {
			.addr = 0x21,
			.mask = 0x40,
		},
		/*
		 * Data Alignment Setting - needs to be set to get
		 * left-justified data like all other sensors.
		 */
		.das = {
			.addr = 0x21,
			.mask = 0x01,
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x21,
				.mask = 0x04,
			},
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x07,
			},
		},
		.sim = {
			.addr = 0x21,
			.value = BIT(1),
		},
		.multi_read_bit = true,
		.bootime = 2, /* guess */
	},
	{
		.wai = 0x3b,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LIS331DL_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_8bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0x80,
			.odr_avl = {
				{ .hz = 100, .value = 0x00, },
				{ .hz = 400, .value = 0x01, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0x40,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.addr = 0x20,
			.mask = 0x20,
			/*
			 * TODO: check these resulting gain settings, these are
			 * not in the datsheet
			 */
			.fs_avl = {
				[0] = {
					.num = ST_ACCEL_FS_AVL_2G,
					.value = 0x00,
					.gain = IIO_G_TO_M_S_2(18000),
				},
				[1] = {
					.num = ST_ACCEL_FS_AVL_8G,
					.value = 0x01,
					.gain = IIO_G_TO_M_S_2(72000),
				},
			},
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
				.mask = 0x07,
			},
		},
		.sim = {
			.addr = 0x21,
			.value = BIT(7),
		},
		.multi_read_bit = false,
		.bootime = 2, /* guess */
	},
	{
		.wai = 0x32,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = H3LIS331DL_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_12bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0x18,
			.odr_avl = {
				{ .hz = 50, .value = 0x00, },
				{ .hz = 100, .value = 0x01, },
				{ .hz = 400, .value = 0x02, },
				{ .hz = 1000, .value = 0x03, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0x20,
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
					.num = ST_ACCEL_FS_AVL_100G,
					.value = 0x00,
					.gain = IIO_G_TO_M_S_2(49000),
				},
				[1] = {
					.num = ST_ACCEL_FS_AVL_200G,
					.value = 0x01,
					.gain = IIO_G_TO_M_S_2(98000),
				},
				[2] = {
					.num = ST_ACCEL_FS_AVL_400G,
					.value = 0x03,
					.gain = IIO_G_TO_M_S_2(195000),
				},
			},
		},
		.bdu = {
			.addr = 0x23,
			.mask = 0x80,
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x22,
				.mask = 0x02,
			},
			.int2 = {
				.addr = 0x22,
				.mask = 0x10,
			},
			.addr_ihl = 0x22,
			.mask_ihl = 0x80,
		},
		.sim = {
			.addr = 0x23,
			.value = BIT(0),
		},
		.multi_read_bit = true,
		.bootime = 2,
	},
	{
		/* No WAI register present */
		.sensors_supported = {
			[0] = LIS3L02DQ_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_12bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0x30,
			.odr_avl = {
				{ .hz = 280, .value = 0x00, },
				{ .hz = 560, .value = 0x01, },
				{ .hz = 1120, .value = 0x02, },
				{ .hz = 4480, .value = 0x03, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0xc0,
			.value_on = ST_SENSORS_DEFAULT_POWER_ON_VALUE,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.enable_axis = {
			.addr = ST_SENSORS_DEFAULT_AXIS_ADDR,
			.mask = ST_SENSORS_DEFAULT_AXIS_MASK,
		},
		.fs = {
			.fs_avl = {
				[0] = {
					.num = ST_ACCEL_FS_AVL_2G,
					.gain = IIO_G_TO_M_S_2(488),
				},
			},
		},
		/*
		 * The part has a BDU bit but if set the data is never
		 * updated so don't set it.
		 */
		.bdu = {
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x21,
				.mask = 0x04,
			},
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x07,
			},
		},
		.sim = {
			.addr = 0x21,
			.value = BIT(1),
		},
		.multi_read_bit = false,
		.bootime = 2,
	},
	{
		.wai = 0x33,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LNG2DM_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_8bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0xf0,
			.odr_avl = {
				{ .hz = 1, .value = 0x01, },
				{ .hz = 10, .value = 0x02, },
				{ .hz = 25, .value = 0x03, },
				{ .hz = 50, .value = 0x04, },
				{ .hz = 100, .value = 0x05, },
				{ .hz = 200, .value = 0x06, },
				{ .hz = 400, .value = 0x07, },
				{ .hz = 1600, .value = 0x08, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0xf0,
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
					.num = ST_ACCEL_FS_AVL_2G,
					.value = 0x00,
					.gain = IIO_G_TO_M_S_2(15600),
				},
				[1] = {
					.num = ST_ACCEL_FS_AVL_4G,
					.value = 0x01,
					.gain = IIO_G_TO_M_S_2(31200),
				},
				[2] = {
					.num = ST_ACCEL_FS_AVL_8G,
					.value = 0x02,
					.gain = IIO_G_TO_M_S_2(62500),
				},
				[3] = {
					.num = ST_ACCEL_FS_AVL_16G,
					.value = 0x03,
					.gain = IIO_G_TO_M_S_2(187500),
				},
			},
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x22,
				.mask = 0x10,
			},
			.addr_ihl = 0x25,
			.mask_ihl = 0x02,
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
		.wai = 0x44,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LIS2DW12_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_12bit_channels,
		.odr = {
			.addr = 0x20,
			.mask = 0xf0,
			.odr_avl = {
				{ .hz = 1, .value = 0x01, },
				{ .hz = 12, .value = 0x02, },
				{ .hz = 25, .value = 0x03, },
				{ .hz = 50, .value = 0x04, },
				{ .hz = 100, .value = 0x05, },
				{ .hz = 200, .value = 0x06, },
			},
		},
		.pw = {
			.addr = 0x20,
			.mask = 0xf0,
			.value_off = ST_SENSORS_DEFAULT_POWER_OFF_VALUE,
		},
		.fs = {
			.addr = 0x25,
			.mask = 0x30,
			.fs_avl = {
				[0] = {
					.num = ST_ACCEL_FS_AVL_2G,
					.value = 0x00,
					.gain = IIO_G_TO_M_S_2(976),
				},
				[1] = {
					.num = ST_ACCEL_FS_AVL_4G,
					.value = 0x01,
					.gain = IIO_G_TO_M_S_2(1952),
				},
				[2] = {
					.num = ST_ACCEL_FS_AVL_8G,
					.value = 0x02,
					.gain = IIO_G_TO_M_S_2(3904),
				},
				[3] = {
					.num = ST_ACCEL_FS_AVL_16G,
					.value = 0x03,
					.gain = IIO_G_TO_M_S_2(7808),
				},
			},
		},
		.bdu = {
			.addr = 0x21,
			.mask = 0x08,
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x23,
				.mask = 0x01,
				.addr_od = 0x22,
				.mask_od = 0x20,
			},
			.int2 = {
				.addr = 0x24,
				.mask = 0x01,
				.addr_od = 0x22,
				.mask_od = 0x20,
			},
			.addr_ihl = 0x22,
			.mask_ihl = 0x08,
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x01,
			},
		},
		.sim = {
			.addr = 0x21,
			.value = BIT(0),
		},
		.multi_read_bit = false,
		.bootime = 2,
	},
	{
		.wai = 0x11,
		.wai_addr = ST_SENSORS_DEFAULT_WAI_ADDRESS,
		.sensors_supported = {
			[0] = LIS3DHH_ACCEL_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)st_accel_16bit_channels,
		.odr = {
			/* just ODR = 1100Hz available */
			.odr_avl = {
				{ .hz = 1100, .value = 0x00, },
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
				[0] = {
					.num = ST_ACCEL_FS_AVL_2G,
					.gain = IIO_G_TO_M_S_2(76),
				},
			},
		},
		.bdu = {
			.addr = 0x20,
			.mask = 0x01,
		},
		.drdy_irq = {
			.int1 = {
				.addr = 0x21,
				.mask = 0x80,
				.addr_od = 0x23,
				.mask_od = 0x04,
			},
			.int2 = {
				.addr = 0x22,
				.mask = 0x80,
				.addr_od = 0x23,
				.mask_od = 0x08,
			},
			.stat_drdy = {
				.addr = ST_SENSORS_DEFAULT_STAT_ADDR,
				.mask = 0x07,
			},
		},
		.multi_read_bit = false,
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
		*val = adata->current_fullscale->gain / 1000000;
		*val2 = adata->current_fullscale->gain % 1000000;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = adata->odr;
		return IIO_VAL_INT;
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
	case IIO_CHAN_INFO_SCALE: {
		int gain;

		gain = val * 1000000 + val2;
		err = st_sensors_set_fullscale_by_gain(indio_dev, gain);
		break;
	}
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

	return err;
}

static ST_SENSORS_DEV_ATTR_SAMP_FREQ_AVAIL();
static ST_SENSORS_DEV_ATTR_SCALE_AVAIL(in_accel_scale_available);

static struct attribute *st_accel_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_accel_attribute_group = {
	.attrs = st_accel_attributes,
};

static const struct iio_info accel_info = {
	.attrs = &st_accel_attribute_group,
	.read_raw = &st_accel_read_raw,
	.write_raw = &st_accel_write_raw,
	.debugfs_reg_access = &st_sensors_debugfs_reg_access,
};

#ifdef CONFIG_IIO_TRIGGER
static const struct iio_trigger_ops st_accel_trigger_ops = {
	.set_trigger_state = ST_ACCEL_TRIGGER_SET_STATE,
	.validate_device = st_sensors_validate_device,
};
#define ST_ACCEL_TRIGGER_OPS (&st_accel_trigger_ops)
#else
#define ST_ACCEL_TRIGGER_OPS NULL
#endif

int st_accel_common_probe(struct iio_dev *indio_dev)
{
	struct st_sensor_data *adata = iio_priv(indio_dev);
	int irq = adata->get_irq_data_ready(indio_dev);
	int err;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &accel_info;
	mutex_init(&adata->tb.buf_lock);

	err = st_sensors_power_enable(indio_dev);
	if (err)
		return err;

	err = st_sensors_check_device_support(indio_dev,
					ARRAY_SIZE(st_accel_sensors_settings),
					st_accel_sensors_settings);
	if (err < 0)
		goto st_accel_power_off;

	adata->num_data_channels = ST_ACCEL_NUMBER_DATA_CHANNELS;
	adata->multiread_bit = adata->sensor_settings->multi_read_bit;
	indio_dev->channels = adata->sensor_settings->ch;
	indio_dev->num_channels = ST_SENSORS_NUMBER_ALL_CHANNELS;

	adata->current_fullscale = (struct st_sensor_fullscale_avl *)
					&adata->sensor_settings->fs.fs_avl[0];
	adata->odr = adata->sensor_settings->odr.odr_avl[0].hz;

	err = st_sensors_init_sensor(indio_dev, adata->dev->platform_data);
	if (err < 0)
		goto st_accel_power_off;

	err = st_accel_allocate_ring(indio_dev);
	if (err < 0)
		goto st_accel_power_off;

	if (irq > 0) {
		err = st_sensors_allocate_trigger(indio_dev,
						 ST_ACCEL_TRIGGER_OPS);
		if (err < 0)
			goto st_accel_probe_trigger_error;
	}

	err = iio_device_register(indio_dev);
	if (err)
		goto st_accel_device_register_error;

	dev_info(&indio_dev->dev, "registered accelerometer %s\n",
		 indio_dev->name);

	return 0;

st_accel_device_register_error:
	if (irq > 0)
		st_sensors_deallocate_trigger(indio_dev);
st_accel_probe_trigger_error:
	st_accel_deallocate_ring(indio_dev);
st_accel_power_off:
	st_sensors_power_disable(indio_dev);

	return err;
}
EXPORT_SYMBOL(st_accel_common_probe);

void st_accel_common_remove(struct iio_dev *indio_dev)
{
	struct st_sensor_data *adata = iio_priv(indio_dev);

	st_sensors_power_disable(indio_dev);

	iio_device_unregister(indio_dev);
	if (adata->get_irq_data_ready(indio_dev) > 0)
		st_sensors_deallocate_trigger(indio_dev);

	st_accel_deallocate_ring(indio_dev);
}
EXPORT_SYMBOL(st_accel_common_remove);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics accelerometers driver");
MODULE_LICENSE("GPL v2");
