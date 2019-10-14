// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsx sensor driver
 *
 * The ST LSM6DSx IMU MEMS series consists of 3D digital accelerometer
 * and 3D digital gyroscope system-in-package with a digital I2C/SPI serial
 * interface standard output.
 * LSM6DSx IMU MEMS series has a dynamic user-selectable full-scale
 * acceleration range of +-2/+-4/+-8/+-16 g and an angular rate range of
 * +-125/+-245/+-500/+-1000/+-2000 dps
 * LSM6DSx series has an integrated First-In-First-Out (FIFO) buffer
 * allowing dynamic batching of sensor data.
 * LSM9DSx series is similar but includes an additional magnetometer, handled
 * by a different driver.
 *
 * Supported sensors:
 * - LSM6DS3:
 *   - Accelerometer/Gyroscope supported ODR [Hz]: 13, 26, 52, 104, 208, 416
 *   - Accelerometer supported full-scale [g]: +-2/+-4/+-8/+-16
 *   - Gyroscope supported full-scale [dps]: +-125/+-245/+-500/+-1000/+-2000
 *   - FIFO size: 8KB
 *
 * - LSM6DS3H/LSM6DSL/LSM6DSM/ISM330DLC/LSM6DS3TR-C:
 *   - Accelerometer/Gyroscope supported ODR [Hz]: 13, 26, 52, 104, 208, 416
 *   - Accelerometer supported full-scale [g]: +-2/+-4/+-8/+-16
 *   - Gyroscope supported full-scale [dps]: +-125/+-245/+-500/+-1000/+-2000
 *   - FIFO size: 4KB
 *
 * - LSM6DSO/LSM6DSOX/ASM330LHH/LSM6DSR/ISM330DHCX:
 *   - Accelerometer/Gyroscope supported ODR [Hz]: 13, 26, 52, 104, 208, 416
 *   - Accelerometer supported full-scale [g]: +-2/+-4/+-8/+-16
 *   - Gyroscope supported full-scale [dps]: +-125/+-245/+-500/+-1000/+-2000
 *   - FIFO size: 3KB
 *
 * - LSM9DS1:
 *   - Accelerometer supported ODR [Hz]: 10, 50, 119, 238, 476, 952
 *   - Accelerometer supported full-scale [g]: +-2/+-4/+-8/+-16
 *   - Gyroscope supported ODR [Hz]: 15, 60, 119, 238, 476, 952
 *   - Gyroscope supported full-scale [dps]: +-245/+-500/+-2000
 *   - FIFO size: 32
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 * Denis Ciocca <denis.ciocca@st.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_lsm6dsx.h"

#define ST_LSM6DSX_REG_FIFO_FTH_IRQ_MASK	BIT(3)
#define ST_LSM6DSX_REG_WHOAMI_ADDR		0x0f
#define ST_LSM6DSX_REG_RESET_MASK		BIT(0)
#define ST_LSM6DSX_REG_BOOT_MASK		BIT(7)
#define ST_LSM6DSX_REG_BDU_ADDR			0x12
#define ST_LSM6DSX_REG_BDU_MASK			BIT(6)

#define ST_LSM6DSX_REG_HLACTIVE_ADDR		0x12
#define ST_LSM6DSX_REG_HLACTIVE_MASK		BIT(5)
#define ST_LSM6DSX_REG_PP_OD_ADDR		0x12
#define ST_LSM6DSX_REG_PP_OD_MASK		BIT(4)

static const struct iio_chan_spec st_lsm6dsx_acc_channels[] = {
	ST_LSM6DSX_CHANNEL_ACC(IIO_ACCEL, 0x28, IIO_MOD_X, 0),
	ST_LSM6DSX_CHANNEL_ACC(IIO_ACCEL, 0x2a, IIO_MOD_Y, 1),
	ST_LSM6DSX_CHANNEL_ACC(IIO_ACCEL, 0x2c, IIO_MOD_Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_chan_spec st_lsm6dsx_gyro_channels[] = {
	ST_LSM6DSX_CHANNEL(IIO_ANGL_VEL, 0x22, IIO_MOD_X, 0),
	ST_LSM6DSX_CHANNEL(IIO_ANGL_VEL, 0x24, IIO_MOD_Y, 1),
	ST_LSM6DSX_CHANNEL(IIO_ANGL_VEL, 0x26, IIO_MOD_Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_chan_spec st_lsm6ds0_gyro_channels[] = {
	ST_LSM6DSX_CHANNEL(IIO_ANGL_VEL, 0x18, IIO_MOD_X, 0),
	ST_LSM6DSX_CHANNEL(IIO_ANGL_VEL, 0x1a, IIO_MOD_Y, 1),
	ST_LSM6DSX_CHANNEL(IIO_ANGL_VEL, 0x1c, IIO_MOD_Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct st_lsm6dsx_settings st_lsm6dsx_sensor_settings[] = {
	{
		.wai = 0x68,
		.int1_addr = 0x0c,
		.int2_addr = 0x0d,
		.reset_addr = 0x22,
		.max_fifo_size = 32,
		.id = {
			{
				.hw_id = ST_LSM9DS1_ID,
				.name = ST_LSM9DS1_DEV_NAME,
			},
		},
		.channels = {
			[ST_LSM6DSX_ID_ACC] = {
				.chan = st_lsm6dsx_acc_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_acc_channels),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.chan = st_lsm6ds0_gyro_channels,
				.len = ARRAY_SIZE(st_lsm6ds0_gyro_channels),
			},
		},
		.odr_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x20,
					.mask = GENMASK(7, 5),
				},
				.odr_avl[0] = {  10, 0x01 },
				.odr_avl[1] = {  50, 0x02 },
				.odr_avl[2] = { 119, 0x03 },
				.odr_avl[3] = { 238, 0x04 },
				.odr_avl[4] = { 476, 0x05 },
				.odr_avl[5] = { 952, 0x06 },
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(7, 5),
				},
				.odr_avl[0] = {  15, 0x01 },
				.odr_avl[1] = {  60, 0x02 },
				.odr_avl[2] = { 119, 0x03 },
				.odr_avl[3] = { 238, 0x04 },
				.odr_avl[4] = { 476, 0x05 },
				.odr_avl[5] = { 952, 0x06 },
			},
		},
		.fs_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x20,
					.mask = GENMASK(4, 3),
				},
				.fs_avl[0] = {  IIO_G_TO_M_S_2(61), 0x0 },
				.fs_avl[1] = { IIO_G_TO_M_S_2(122), 0x2 },
				.fs_avl[2] = { IIO_G_TO_M_S_2(244), 0x3 },
				.fs_avl[3] = { IIO_G_TO_M_S_2(732), 0x1 },
				.fs_len = 4,
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(4, 3),
				},
				.fs_avl[0] = {  IIO_DEGREE_TO_RAD(245), 0x0 },
				.fs_avl[1] = {  IIO_DEGREE_TO_RAD(500), 0x1 },
				.fs_avl[2] = { IIO_DEGREE_TO_RAD(2000), 0x3 },
				.fs_len = 3,
			},
		},
	},
	{
		.wai = 0x69,
		.int1_addr = 0x0d,
		.int2_addr = 0x0e,
		.int1_func_addr = 0x5e,
		.int2_func_addr = 0x5f,
		.int_func_mask = BIT(5),
		.reset_addr = 0x12,
		.max_fifo_size = 1365,
		.id = {
			{
				.hw_id = ST_LSM6DS3_ID,
				.name = ST_LSM6DS3_DEV_NAME,
			},
		},
		.channels = {
			[ST_LSM6DSX_ID_ACC] = {
				.chan = st_lsm6dsx_acc_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_acc_channels),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.chan = st_lsm6dsx_gyro_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_gyro_channels),
			},
		},
		.odr_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
		},
		.fs_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_G_TO_M_S_2(61), 0x0 },
				.fs_avl[1] = { IIO_G_TO_M_S_2(122), 0x2 },
				.fs_avl[2] = { IIO_G_TO_M_S_2(244), 0x3 },
				.fs_avl[3] = { IIO_G_TO_M_S_2(488), 0x1 },
				.fs_len = 4,
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_DEGREE_TO_RAD(8750), 0x0 },
				.fs_avl[1] = { IIO_DEGREE_TO_RAD(17500), 0x1 },
				.fs_avl[2] = { IIO_DEGREE_TO_RAD(35000), 0x2 },
				.fs_avl[3] = { IIO_DEGREE_TO_RAD(70000), 0x3 },
				.fs_len = 4,
			},
		},
		.decimator = {
			[ST_LSM6DSX_ID_ACC] = {
				.addr = 0x08,
				.mask = GENMASK(2, 0),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.addr = 0x08,
				.mask = GENMASK(5, 3),
			},
		},
		.lir = {
			.addr = 0x58,
			.mask = BIT(0),
		},
		.fifo_ops = {
			.update_fifo = st_lsm6dsx_update_fifo,
			.read_fifo = st_lsm6dsx_read_fifo,
			.fifo_th = {
				.addr = 0x06,
				.mask = GENMASK(11, 0),
			},
			.fifo_diff = {
				.addr = 0x3a,
				.mask = GENMASK(11, 0),
			},
			.th_wl = 3, /* 1LSB = 2B */
		},
		.ts_settings = {
			.timer_en = {
				.addr = 0x58,
				.mask = BIT(7),
			},
			.hr_timer = {
				.addr = 0x5c,
				.mask = BIT(4),
			},
			.fifo_en = {
				.addr = 0x07,
				.mask = BIT(7),
			},
			.decimator = {
				.addr = 0x09,
				.mask = GENMASK(5, 3),
			},
		},
		.event_settings = {
			.wakeup_reg = {
				.addr = 0x5B,
				.mask = GENMASK(5, 0),
			},
			.wakeup_src_reg = 0x1b,
			.wakeup_src_status_mask = BIT(3),
			.wakeup_src_z_mask = BIT(0),
			.wakeup_src_y_mask = BIT(1),
			.wakeup_src_x_mask = BIT(2),
		},
	},
	{
		.wai = 0x69,
		.int1_addr = 0x0d,
		.int2_addr = 0x0e,
		.int1_func_addr = 0x5e,
		.int2_func_addr = 0x5f,
		.int_func_mask = BIT(5),
		.reset_addr = 0x12,
		.max_fifo_size = 682,
		.id = {
			{
				.hw_id = ST_LSM6DS3H_ID,
				.name = ST_LSM6DS3H_DEV_NAME,
			},
		},
		.channels = {
			[ST_LSM6DSX_ID_ACC] = {
				.chan = st_lsm6dsx_acc_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_acc_channels),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.chan = st_lsm6dsx_gyro_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_gyro_channels),
			},
		},
		.odr_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
		},
		.fs_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_G_TO_M_S_2(61), 0x0 },
				.fs_avl[1] = { IIO_G_TO_M_S_2(122), 0x2 },
				.fs_avl[2] = { IIO_G_TO_M_S_2(244), 0x3 },
				.fs_avl[3] = { IIO_G_TO_M_S_2(488), 0x1 },
				.fs_len = 4,
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_DEGREE_TO_RAD(8750), 0x0 },
				.fs_avl[1] = { IIO_DEGREE_TO_RAD(17500), 0x1 },
				.fs_avl[2] = { IIO_DEGREE_TO_RAD(35000), 0x2 },
				.fs_avl[3] = { IIO_DEGREE_TO_RAD(70000), 0x3 },
				.fs_len = 4,
			},
		},
		.decimator = {
			[ST_LSM6DSX_ID_ACC] = {
				.addr = 0x08,
				.mask = GENMASK(2, 0),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.addr = 0x08,
				.mask = GENMASK(5, 3),
			},
		},
		.lir = {
			.addr = 0x58,
			.mask = BIT(0),
		},
		.fifo_ops = {
			.update_fifo = st_lsm6dsx_update_fifo,
			.read_fifo = st_lsm6dsx_read_fifo,
			.fifo_th = {
				.addr = 0x06,
				.mask = GENMASK(11, 0),
			},
			.fifo_diff = {
				.addr = 0x3a,
				.mask = GENMASK(11, 0),
			},
			.th_wl = 3, /* 1LSB = 2B */
		},
		.ts_settings = {
			.timer_en = {
				.addr = 0x58,
				.mask = BIT(7),
			},
			.hr_timer = {
				.addr = 0x5c,
				.mask = BIT(4),
			},
			.fifo_en = {
				.addr = 0x07,
				.mask = BIT(7),
			},
			.decimator = {
				.addr = 0x09,
				.mask = GENMASK(5, 3),
			},
		},
		.event_settings = {
			.wakeup_reg = {
				.addr = 0x5B,
				.mask = GENMASK(5, 0),
			},
			.wakeup_src_reg = 0x1b,
			.wakeup_src_status_mask = BIT(3),
			.wakeup_src_z_mask = BIT(0),
			.wakeup_src_y_mask = BIT(1),
			.wakeup_src_x_mask = BIT(2),
		},
	},
	{
		.wai = 0x6a,
		.int1_addr = 0x0d,
		.int2_addr = 0x0e,
		.int1_func_addr = 0x5e,
		.int2_func_addr = 0x5f,
		.int_func_mask = BIT(5),
		.reset_addr = 0x12,
		.max_fifo_size = 682,
		.id = {
			{
				.hw_id = ST_LSM6DSL_ID,
				.name = ST_LSM6DSL_DEV_NAME,
			}, {
				.hw_id = ST_LSM6DSM_ID,
				.name = ST_LSM6DSM_DEV_NAME,
			}, {
				.hw_id = ST_ISM330DLC_ID,
				.name = ST_ISM330DLC_DEV_NAME,
			}, {
				.hw_id = ST_LSM6DS3TRC_ID,
				.name = ST_LSM6DS3TRC_DEV_NAME,
			},
		},
		.channels = {
			[ST_LSM6DSX_ID_ACC] = {
				.chan = st_lsm6dsx_acc_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_acc_channels),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.chan = st_lsm6dsx_gyro_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_gyro_channels),
			},
		},
		.odr_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
		},
		.fs_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_G_TO_M_S_2(61), 0x0 },
				.fs_avl[1] = { IIO_G_TO_M_S_2(122), 0x2 },
				.fs_avl[2] = { IIO_G_TO_M_S_2(244), 0x3 },
				.fs_avl[3] = { IIO_G_TO_M_S_2(488), 0x1 },
				.fs_len = 4,
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_DEGREE_TO_RAD(8750), 0x0 },
				.fs_avl[1] = { IIO_DEGREE_TO_RAD(17500), 0x1 },
				.fs_avl[2] = { IIO_DEGREE_TO_RAD(35000), 0x2 },
				.fs_avl[3] = { IIO_DEGREE_TO_RAD(70000), 0x3 },
				.fs_len = 4,
			},
		},
		.decimator = {
			[ST_LSM6DSX_ID_ACC] = {
				.addr = 0x08,
				.mask = GENMASK(2, 0),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.addr = 0x08,
				.mask = GENMASK(5, 3),
			},
		},
		.lir = {
			.addr = 0x58,
			.mask = BIT(0),
		},
		.fifo_ops = {
			.update_fifo = st_lsm6dsx_update_fifo,
			.read_fifo = st_lsm6dsx_read_fifo,
			.fifo_th = {
				.addr = 0x06,
				.mask = GENMASK(10, 0),
			},
			.fifo_diff = {
				.addr = 0x3a,
				.mask = GENMASK(10, 0),
			},
			.th_wl = 3, /* 1LSB = 2B */
		},
		.ts_settings = {
			.timer_en = {
				.addr = 0x19,
				.mask = BIT(5),
			},
			.hr_timer = {
				.addr = 0x5c,
				.mask = BIT(4),
			},
			.fifo_en = {
				.addr = 0x07,
				.mask = BIT(7),
			},
			.decimator = {
				.addr = 0x09,
				.mask = GENMASK(5, 3),
			},
		},
		.event_settings = {
			.enable_reg = {
				.addr = 0x58,
				.mask = BIT(7),
			},
			.wakeup_reg = {
				.addr = 0x5B,
				.mask = GENMASK(5, 0),
			},
			.wakeup_src_reg = 0x1b,
			.wakeup_src_status_mask = BIT(3),
			.wakeup_src_z_mask = BIT(0),
			.wakeup_src_y_mask = BIT(1),
			.wakeup_src_x_mask = BIT(2),
		},
	},
	{
		.wai = 0x6c,
		.int1_addr = 0x0d,
		.int2_addr = 0x0e,
		.reset_addr = 0x12,
		.max_fifo_size = 512,
		.id = {
			{
				.hw_id = ST_LSM6DSO_ID,
				.name = ST_LSM6DSO_DEV_NAME,
			}, {
				.hw_id = ST_LSM6DSOX_ID,
				.name = ST_LSM6DSOX_DEV_NAME,
			},
		},
		.channels = {
			[ST_LSM6DSX_ID_ACC] = {
				.chan = st_lsm6dsx_acc_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_acc_channels),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.chan = st_lsm6dsx_gyro_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_gyro_channels),
			},
		},
		.odr_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
		},
		.fs_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_G_TO_M_S_2(61), 0x0 },
				.fs_avl[1] = { IIO_G_TO_M_S_2(122), 0x2 },
				.fs_avl[2] = { IIO_G_TO_M_S_2(244), 0x3 },
				.fs_avl[3] = { IIO_G_TO_M_S_2(488), 0x1 },
				.fs_len = 4,
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_DEGREE_TO_RAD(8750), 0x0 },
				.fs_avl[1] = { IIO_DEGREE_TO_RAD(17500), 0x1 },
				.fs_avl[2] = { IIO_DEGREE_TO_RAD(35000), 0x2 },
				.fs_avl[3] = { IIO_DEGREE_TO_RAD(70000), 0x3 },
				.fs_len = 4,
			},
		},
		.batch = {
			[ST_LSM6DSX_ID_ACC] = {
				.addr = 0x09,
				.mask = GENMASK(3, 0),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.addr = 0x09,
				.mask = GENMASK(7, 4),
			},
		},
		.lir = {
			.addr = 0x56,
			.mask = BIT(0),
		},
		.clear_on_read = {
			.addr = 0x56,
			.mask = BIT(6),
		},
		.fifo_ops = {
			.update_fifo = st_lsm6dsx_update_fifo,
			.read_fifo = st_lsm6dsx_read_tagged_fifo,
			.fifo_th = {
				.addr = 0x07,
				.mask = GENMASK(8, 0),
			},
			.fifo_diff = {
				.addr = 0x3a,
				.mask = GENMASK(9, 0),
			},
			.th_wl = 1,
		},
		.ts_settings = {
			.timer_en = {
				.addr = 0x19,
				.mask = BIT(5),
			},
			.decimator = {
				.addr = 0x0a,
				.mask = GENMASK(7, 6),
			},
		},
		.shub_settings = {
			.page_mux = {
				.addr = 0x01,
				.mask = BIT(6),
			},
			.master_en = {
				.addr = 0x14,
				.mask = BIT(2),
			},
			.pullup_en = {
				.addr = 0x14,
				.mask = BIT(3),
			},
			.aux_sens = {
				.addr = 0x14,
				.mask = GENMASK(1, 0),
			},
			.wr_once = {
				.addr = 0x14,
				.mask = BIT(6),
			},
			.shub_out = 0x02,
			.slv0_addr = 0x15,
			.dw_slv0_addr = 0x21,
			.batch_en = BIT(3),
		}
	},
	{
		.wai = 0x6b,
		.int1_addr = 0x0d,
		.int2_addr = 0x0e,
		.int1_func_addr = 0x5e,
		.int2_func_addr = 0x5f,
		.int_func_mask = BIT(5),
		.reset_addr = 0x12,
		.max_fifo_size = 512,
		.id = {
			{
				.hw_id = ST_ASM330LHH_ID,
				.name = ST_ASM330LHH_DEV_NAME,
			},
		},
		.channels = {
			[ST_LSM6DSX_ID_ACC] = {
				.chan = st_lsm6dsx_acc_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_acc_channels),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.chan = st_lsm6dsx_gyro_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_gyro_channels),
			},
		},
		.odr_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
		},
		.fs_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_G_TO_M_S_2(61), 0x0 },
				.fs_avl[1] = { IIO_G_TO_M_S_2(122), 0x2 },
				.fs_avl[2] = { IIO_G_TO_M_S_2(244), 0x3 },
				.fs_avl[3] = { IIO_G_TO_M_S_2(488), 0x1 },
				.fs_len = 4,
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_DEGREE_TO_RAD(8750), 0x0 },
				.fs_avl[1] = { IIO_DEGREE_TO_RAD(17500), 0x1 },
				.fs_avl[2] = { IIO_DEGREE_TO_RAD(35000), 0x2 },
				.fs_avl[3] = { IIO_DEGREE_TO_RAD(70000), 0x3 },
				.fs_len = 4,
			},
		},
		.batch = {
			[ST_LSM6DSX_ID_ACC] = {
				.addr = 0x09,
				.mask = GENMASK(3, 0),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.addr = 0x09,
				.mask = GENMASK(7, 4),
			},
		},
		.lir = {
			.addr = 0x56,
			.mask = BIT(0),
		},
		.clear_on_read = {
			.addr = 0x56,
			.mask = BIT(6),
		},
		.fifo_ops = {
			.update_fifo = st_lsm6dsx_update_fifo,
			.read_fifo = st_lsm6dsx_read_tagged_fifo,
			.fifo_th = {
				.addr = 0x07,
				.mask = GENMASK(8, 0),
			},
			.fifo_diff = {
				.addr = 0x3a,
				.mask = GENMASK(9, 0),
			},
			.th_wl = 1,
		},
		.ts_settings = {
			.timer_en = {
				.addr = 0x19,
				.mask = BIT(5),
			},
			.decimator = {
				.addr = 0x0a,
				.mask = GENMASK(7, 6),
			},
		},
		.event_settings = {
			.enable_reg = {
				.addr = 0x58,
				.mask = BIT(7),
			},
			.wakeup_reg = {
				.addr = 0x5B,
				.mask = GENMASK(5, 0),
			},
			.wakeup_src_reg = 0x1b,
			.wakeup_src_status_mask = BIT(3),
			.wakeup_src_z_mask = BIT(0),
			.wakeup_src_y_mask = BIT(1),
			.wakeup_src_x_mask = BIT(2),
		},
	},
	{
		.wai = 0x6b,
		.int1_addr = 0x0d,
		.int2_addr = 0x0e,
		.int1_func_addr = 0x5e,
		.int2_func_addr = 0x5f,
		.int_func_mask = BIT(5),
		.reset_addr = 0x12,
		.max_fifo_size = 512,
		.id = {
			{
				.hw_id = ST_LSM6DSR_ID,
				.name = ST_LSM6DSR_DEV_NAME,
			}, {
				.hw_id = ST_ISM330DHCX_ID,
				.name = ST_ISM330DHCX_DEV_NAME,
			},
		},
		.channels = {
			[ST_LSM6DSX_ID_ACC] = {
				.chan = st_lsm6dsx_acc_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_acc_channels),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.chan = st_lsm6dsx_gyro_channels,
				.len = ARRAY_SIZE(st_lsm6dsx_gyro_channels),
			},
		},
		.odr_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(7, 4),
				},
				.odr_avl[0] = {  13, 0x01 },
				.odr_avl[1] = {  26, 0x02 },
				.odr_avl[2] = {  52, 0x03 },
				.odr_avl[3] = { 104, 0x04 },
				.odr_avl[4] = { 208, 0x05 },
				.odr_avl[5] = { 416, 0x06 },
			},
		},
		.fs_table = {
			[ST_LSM6DSX_ID_ACC] = {
				.reg = {
					.addr = 0x10,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_G_TO_M_S_2(61), 0x0 },
				.fs_avl[1] = { IIO_G_TO_M_S_2(122), 0x2 },
				.fs_avl[2] = { IIO_G_TO_M_S_2(244), 0x3 },
				.fs_avl[3] = { IIO_G_TO_M_S_2(488), 0x1 },
				.fs_len = 4,
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.reg = {
					.addr = 0x11,
					.mask = GENMASK(3, 2),
				},
				.fs_avl[0] = {  IIO_DEGREE_TO_RAD(8750), 0x0 },
				.fs_avl[1] = { IIO_DEGREE_TO_RAD(17500), 0x1 },
				.fs_avl[2] = { IIO_DEGREE_TO_RAD(35000), 0x2 },
				.fs_avl[3] = { IIO_DEGREE_TO_RAD(70000), 0x3 },
				.fs_len = 4,
			},
		},
		.batch = {
			[ST_LSM6DSX_ID_ACC] = {
				.addr = 0x09,
				.mask = GENMASK(3, 0),
			},
			[ST_LSM6DSX_ID_GYRO] = {
				.addr = 0x09,
				.mask = GENMASK(7, 4),
			},
		},
		.lir = {
			.addr = 0x56,
			.mask = BIT(0),
		},
		.clear_on_read = {
			.addr = 0x56,
			.mask = BIT(6),
		},
		.fifo_ops = {
			.update_fifo = st_lsm6dsx_update_fifo,
			.read_fifo = st_lsm6dsx_read_tagged_fifo,
			.fifo_th = {
				.addr = 0x07,
				.mask = GENMASK(8, 0),
			},
			.fifo_diff = {
				.addr = 0x3a,
				.mask = GENMASK(9, 0),
			},
			.th_wl = 1,
		},
		.ts_settings = {
			.timer_en = {
				.addr = 0x19,
				.mask = BIT(5),
			},
			.decimator = {
				.addr = 0x0a,
				.mask = GENMASK(7, 6),
			},
		},
		.shub_settings = {
			.page_mux = {
				.addr = 0x01,
				.mask = BIT(6),
			},
			.master_en = {
				.addr = 0x14,
				.mask = BIT(2),
			},
			.pullup_en = {
				.addr = 0x14,
				.mask = BIT(3),
			},
			.aux_sens = {
				.addr = 0x14,
				.mask = GENMASK(1, 0),
			},
			.wr_once = {
				.addr = 0x14,
				.mask = BIT(6),
			},
			.shub_out = 0x02,
			.slv0_addr = 0x15,
			.dw_slv0_addr = 0x21,
			.batch_en = BIT(3),
		},
		.event_settings = {
			.enable_reg = {
				.addr = 0x58,
				.mask = BIT(7),
			},
			.wakeup_reg = {
				.addr = 0x5B,
				.mask = GENMASK(5, 0),
			},
			.wakeup_src_reg = 0x1b,
			.wakeup_src_status_mask = BIT(3),
			.wakeup_src_z_mask = BIT(0),
			.wakeup_src_y_mask = BIT(1),
			.wakeup_src_x_mask = BIT(2),
		}
	},
};

int st_lsm6dsx_set_page(struct st_lsm6dsx_hw *hw, bool enable)
{
	const struct st_lsm6dsx_shub_settings *hub_settings;
	unsigned int data;
	int err;

	hub_settings = &hw->settings->shub_settings;
	data = ST_LSM6DSX_SHIFT_VAL(enable, hub_settings->page_mux.mask);
	err = regmap_update_bits(hw->regmap, hub_settings->page_mux.addr,
				 hub_settings->page_mux.mask, data);
	usleep_range(100, 150);

	return err;
}

static int st_lsm6dsx_check_whoami(struct st_lsm6dsx_hw *hw, int id,
				   const char **name)
{
	int err, i, j, data;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsx_sensor_settings); i++) {
		for (j = 0; j < ST_LSM6DSX_MAX_ID; j++) {
			if (id == st_lsm6dsx_sensor_settings[i].id[j].hw_id)
				break;
		}
		if (j < ST_LSM6DSX_MAX_ID)
			break;
	}

	if (i == ARRAY_SIZE(st_lsm6dsx_sensor_settings)) {
		dev_err(hw->dev, "unsupported hw id [%02x]\n", id);
		return -ENODEV;
	}

	err = regmap_read(hw->regmap, ST_LSM6DSX_REG_WHOAMI_ADDR, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");
		return err;
	}

	if (data != st_lsm6dsx_sensor_settings[i].wai) {
		dev_err(hw->dev, "unsupported whoami [%02x]\n", data);
		return -ENODEV;
	}

	*name = st_lsm6dsx_sensor_settings[i].id[j].name;
	hw->settings = &st_lsm6dsx_sensor_settings[i];

	return 0;
}

static int st_lsm6dsx_set_full_scale(struct st_lsm6dsx_sensor *sensor,
				     u32 gain)
{
	const struct st_lsm6dsx_fs_table_entry *fs_table;
	unsigned int data;
	int i, err;

	fs_table = &sensor->hw->settings->fs_table[sensor->id];
	for (i = 0; i < fs_table->fs_len; i++) {
		if (fs_table->fs_avl[i].gain == gain)
			break;
	}

	if (i == fs_table->fs_len)
		return -EINVAL;

	data = ST_LSM6DSX_SHIFT_VAL(fs_table->fs_avl[i].val,
				    fs_table->reg.mask);
	err = st_lsm6dsx_update_bits_locked(sensor->hw, fs_table->reg.addr,
					    fs_table->reg.mask, data);
	if (err < 0)
		return err;

	sensor->gain = gain;

	return 0;
}

int st_lsm6dsx_check_odr(struct st_lsm6dsx_sensor *sensor, u16 odr, u8 *val)
{
	const struct st_lsm6dsx_odr_table_entry *odr_table;
	int i;

	odr_table = &sensor->hw->settings->odr_table[sensor->id];
	for (i = 0; i < ST_LSM6DSX_ODR_LIST_SIZE; i++)
		/*
		 * ext devices can run at different odr respect to
		 * accel sensor
		 */
		if (odr_table->odr_avl[i].hz >= odr)
			break;

	if (i == ST_LSM6DSX_ODR_LIST_SIZE)
		return -EINVAL;

	*val = odr_table->odr_avl[i].val;

	return 0;
}

static u16 st_lsm6dsx_check_odr_dependency(struct st_lsm6dsx_hw *hw, u16 odr,
					   enum st_lsm6dsx_sensor_id id)
{
	struct st_lsm6dsx_sensor *ref = iio_priv(hw->iio_devs[id]);

	if (odr > 0) {
		if (hw->enable_mask & BIT(id))
			return max_t(u16, ref->odr, odr);
		else
			return odr;
	} else {
		return (hw->enable_mask & BIT(id)) ? ref->odr : 0;
	}
}

static int st_lsm6dsx_set_odr(struct st_lsm6dsx_sensor *sensor, u16 req_odr)
{
	struct st_lsm6dsx_sensor *ref_sensor = sensor;
	struct st_lsm6dsx_hw *hw = sensor->hw;
	const struct st_lsm6dsx_reg *reg;
	unsigned int data;
	u8 val = 0;
	int err;

	switch (sensor->id) {
	case ST_LSM6DSX_ID_EXT0:
	case ST_LSM6DSX_ID_EXT1:
	case ST_LSM6DSX_ID_EXT2:
	case ST_LSM6DSX_ID_ACC: {
		u16 odr;
		int i;

		/*
		 * i2c embedded controller relies on the accelerometer sensor as
		 * bus read/write trigger so we need to enable accel device
		 * at odr = max(accel_odr, ext_odr) in order to properly
		 * communicate with i2c slave devices
		 */
		ref_sensor = iio_priv(hw->iio_devs[ST_LSM6DSX_ID_ACC]);
		for (i = ST_LSM6DSX_ID_ACC; i < ST_LSM6DSX_ID_MAX; i++) {
			if (!hw->iio_devs[i] || i == sensor->id)
				continue;

			odr = st_lsm6dsx_check_odr_dependency(hw, req_odr, i);
			if (odr != req_odr)
				/* device already configured */
				return 0;
		}
		break;
	}
	default:
		break;
	}

	if (req_odr > 0) {
		err = st_lsm6dsx_check_odr(ref_sensor, req_odr, &val);
		if (err < 0)
			return err;
	}

	reg = &hw->settings->odr_table[ref_sensor->id].reg;
	data = ST_LSM6DSX_SHIFT_VAL(val, reg->mask);
	return st_lsm6dsx_update_bits_locked(hw, reg->addr, reg->mask, data);
}

int st_lsm6dsx_sensor_set_enable(struct st_lsm6dsx_sensor *sensor,
				 bool enable)
{
	struct st_lsm6dsx_hw *hw = sensor->hw;
	u16 odr = enable ? sensor->odr : 0;
	int err;

	err = st_lsm6dsx_set_odr(sensor, odr);
	if (err < 0)
		return err;

	if (enable)
		hw->enable_mask |= BIT(sensor->id);
	else
		hw->enable_mask &= ~BIT(sensor->id);

	return 0;
}

static int st_lsm6dsx_read_oneshot(struct st_lsm6dsx_sensor *sensor,
				   u8 addr, int *val)
{
	struct st_lsm6dsx_hw *hw = sensor->hw;
	int err, delay;
	__le16 data;

	err = st_lsm6dsx_sensor_set_enable(sensor, true);
	if (err < 0)
		return err;

	delay = 1000000 / sensor->odr;
	usleep_range(delay, 2 * delay);

	err = st_lsm6dsx_read_locked(hw, addr, &data, sizeof(data));
	if (err < 0)
		return err;

	if (!hw->enable_event)
		st_lsm6dsx_sensor_set_enable(sensor, false);

	*val = (s16)le16_to_cpu(data);

	return IIO_VAL_INT;
}

static int st_lsm6dsx_read_raw(struct iio_dev *iio_dev,
			       struct iio_chan_spec const *ch,
			       int *val, int *val2, long mask)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			break;

		ret = st_lsm6dsx_read_oneshot(sensor, ch->address, val);
		iio_device_release_direct_mode(iio_dev);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = sensor->odr;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = sensor->gain;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int st_lsm6dsx_write_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	int err;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = st_lsm6dsx_set_full_scale(sensor, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		u8 data;

		err = st_lsm6dsx_check_odr(sensor, val, &data);
		if (!err)
			sensor->odr = val;
		break;
	}
	default:
		err = -EINVAL;
		break;
	}

	iio_device_release_direct_mode(iio_dev);

	return err;
}

static int st_lsm6dsx_event_setup(struct st_lsm6dsx_hw *hw, int state)
{
	int err;
	u8 enable = 0;

	if (!hw->settings->int1_func_addr)
		return -ENOTSUPP;

	enable = state ? hw->settings->event_settings.enable_reg.mask : 0;

	err = regmap_update_bits(hw->regmap,
				 hw->settings->event_settings.enable_reg.addr,
				 hw->settings->event_settings.enable_reg.mask,
				 enable);
	if (err < 0)
		return err;

	enable = state ? hw->irq_routing.mask : 0;

	/* Enable wakeup interrupt */
	return regmap_update_bits(hw->regmap, hw->irq_routing.addr,
				  hw->irq_routing.mask,
				  enable);
}

static int st_lsm6dsx_read_event(struct iio_dev *iio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info,
				   int *val, int *val2)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsx_hw *hw = sensor->hw;

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	*val2 = 0;
	*val = hw->event_threshold;

	return IIO_VAL_INT;
}

static int st_lsm6dsx_write_event(struct iio_dev *iio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int val, int val2)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsx_hw *hw = sensor->hw;
	int err;

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	if (val < 0 || val > 31)
		return -EINVAL;

	err = regmap_update_bits(hw->regmap,
				 hw->settings->event_settings.wakeup_reg.addr,
				 hw->settings->event_settings.wakeup_reg.mask,
				 val);
	if (err)
		return -EINVAL;

	hw->event_threshold = val;

	return 0;
}

static int st_lsm6dsx_read_event_config(struct iio_dev *iio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsx_hw *hw = sensor->hw;

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	return !!(hw->enable_event & BIT(chan->channel2));
}

static int st_lsm6dsx_write_event_config(struct iio_dev *iio_dev,
					   const struct iio_chan_spec *chan,
					   enum iio_event_type type,
					   enum iio_event_direction dir,
					   int state)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsx_hw *hw = sensor->hw;
	u8 enable_event;
	int err = 0;

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	if (state) {
		enable_event = hw->enable_event | BIT(chan->channel2);

		/* do not enable events if they are already enabled */
		if (hw->enable_event)
			goto out;
	} else {
		enable_event = hw->enable_event & ~BIT(chan->channel2);

		/* only turn off sensor if no events is enabled */
		if (enable_event)
			goto out;
	}

	/* stop here if no changes have been made */
	if (hw->enable_event == enable_event)
		return 0;

	err = st_lsm6dsx_event_setup(hw, state);
	if (err < 0)
		return err;

	err = st_lsm6dsx_sensor_set_enable(sensor, state);
	if (err < 0)
		return err;

out:
	hw->enable_event = enable_event;

	return 0;
}

int st_lsm6dsx_set_watermark(struct iio_dev *iio_dev, unsigned int val)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsx_hw *hw = sensor->hw;
	int err;

	if (val < 1 || val > hw->settings->max_fifo_size)
		return -EINVAL;

	mutex_lock(&hw->conf_lock);

	err = st_lsm6dsx_update_watermark(sensor, val);

	mutex_unlock(&hw->conf_lock);

	if (err < 0)
		return err;

	sensor->watermark = val;

	return 0;
}

static ssize_t
st_lsm6dsx_sysfs_sampling_frequency_avail(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	enum st_lsm6dsx_sensor_id id = sensor->id;
	struct st_lsm6dsx_hw *hw = sensor->hw;
	int i, len = 0;

	for (i = 0; i < ST_LSM6DSX_ODR_LIST_SIZE; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 hw->settings->odr_table[id].odr_avl[i].hz);
	buf[len - 1] = '\n';

	return len;
}

static ssize_t st_lsm6dsx_sysfs_scale_avail(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	const struct st_lsm6dsx_fs_table_entry *fs_table;
	struct st_lsm6dsx_hw *hw = sensor->hw;
	int i, len = 0;

	fs_table = &hw->settings->fs_table[sensor->id];
	for (i = 0; i < fs_table->fs_len; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06u ",
				 fs_table->fs_avl[i].gain);
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lsm6dsx_sysfs_sampling_frequency_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       st_lsm6dsx_sysfs_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_anglvel_scale_available, 0444,
		       st_lsm6dsx_sysfs_scale_avail, NULL, 0);

static struct attribute *st_lsm6dsx_acc_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsx_acc_attribute_group = {
	.attrs = st_lsm6dsx_acc_attributes,
};

static const struct iio_info st_lsm6dsx_acc_info = {
	.attrs = &st_lsm6dsx_acc_attribute_group,
	.read_raw = st_lsm6dsx_read_raw,
	.write_raw = st_lsm6dsx_write_raw,
	.read_event_value = st_lsm6dsx_read_event,
	.write_event_value = st_lsm6dsx_write_event,
	.read_event_config = st_lsm6dsx_read_event_config,
	.write_event_config = st_lsm6dsx_write_event_config,
	.hwfifo_set_watermark = st_lsm6dsx_set_watermark,
};

static struct attribute *st_lsm6dsx_gyro_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_anglvel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsx_gyro_attribute_group = {
	.attrs = st_lsm6dsx_gyro_attributes,
};

static const struct iio_info st_lsm6dsx_gyro_info = {
	.attrs = &st_lsm6dsx_gyro_attribute_group,
	.read_raw = st_lsm6dsx_read_raw,
	.write_raw = st_lsm6dsx_write_raw,
	.hwfifo_set_watermark = st_lsm6dsx_set_watermark,
};

static int st_lsm6dsx_of_get_drdy_pin(struct st_lsm6dsx_hw *hw, int *drdy_pin)
{
	struct device_node *np = hw->dev->of_node;

	if (!np)
		return -EINVAL;

	return of_property_read_u32(np, "st,drdy-int-pin", drdy_pin);
}

static int st_lsm6dsx_get_drdy_reg(struct st_lsm6dsx_hw *hw, u8 *drdy_reg)
{
	int err = 0, drdy_pin;

	if (st_lsm6dsx_of_get_drdy_pin(hw, &drdy_pin) < 0) {
		struct st_sensors_platform_data *pdata;
		struct device *dev = hw->dev;

		pdata = (struct st_sensors_platform_data *)dev->platform_data;
		drdy_pin = pdata ? pdata->drdy_int_pin : 1;
	}

	switch (drdy_pin) {
	case 1:
		*drdy_reg = hw->settings->int1_addr;
		hw->irq_routing.addr = hw->settings->int1_func_addr;
		hw->irq_routing.mask = hw->settings->int_func_mask;
		break;
	case 2:
		*drdy_reg = hw->settings->int2_addr;
		hw->irq_routing.addr = hw->settings->int2_func_addr;
		hw->irq_routing.mask = hw->settings->int_func_mask;
		break;
	default:
		dev_err(hw->dev, "unsupported data ready pin\n");
		err = -EINVAL;
		break;
	}

	return err;
}

static int st_lsm6dsx_init_shub(struct st_lsm6dsx_hw *hw)
{
	const struct st_lsm6dsx_shub_settings *hub_settings;
	struct device_node *np = hw->dev->of_node;
	struct st_sensors_platform_data *pdata;
	unsigned int data;
	int err = 0;

	hub_settings = &hw->settings->shub_settings;

	pdata = (struct st_sensors_platform_data *)hw->dev->platform_data;
	if ((np && of_property_read_bool(np, "st,pullups")) ||
	    (pdata && pdata->pullups)) {
		err = st_lsm6dsx_set_page(hw, true);
		if (err < 0)
			return err;

		data = ST_LSM6DSX_SHIFT_VAL(1, hub_settings->pullup_en.mask);
		err = regmap_update_bits(hw->regmap,
					 hub_settings->pullup_en.addr,
					 hub_settings->pullup_en.mask, data);

		st_lsm6dsx_set_page(hw, false);

		if (err < 0)
			return err;
	}

	if (hub_settings->aux_sens.addr) {
		/* configure aux sensors */
		err = st_lsm6dsx_set_page(hw, true);
		if (err < 0)
			return err;

		data = ST_LSM6DSX_SHIFT_VAL(3, hub_settings->aux_sens.mask);
		err = regmap_update_bits(hw->regmap,
					 hub_settings->aux_sens.addr,
					 hub_settings->aux_sens.mask, data);

		st_lsm6dsx_set_page(hw, false);
	}

	return err;
}

static int st_lsm6dsx_init_hw_timer(struct st_lsm6dsx_hw *hw)
{
	const struct st_lsm6dsx_hw_ts_settings *ts_settings;
	int err, val;

	ts_settings = &hw->settings->ts_settings;
	/* enable hw timestamp generation if necessary */
	if (ts_settings->timer_en.addr) {
		val = ST_LSM6DSX_SHIFT_VAL(1, ts_settings->timer_en.mask);
		err = regmap_update_bits(hw->regmap,
					 ts_settings->timer_en.addr,
					 ts_settings->timer_en.mask, val);
		if (err < 0)
			return err;
	}

	/* enable high resolution for hw ts timer if necessary */
	if (ts_settings->hr_timer.addr) {
		val = ST_LSM6DSX_SHIFT_VAL(1, ts_settings->hr_timer.mask);
		err = regmap_update_bits(hw->regmap,
					 ts_settings->hr_timer.addr,
					 ts_settings->hr_timer.mask, val);
		if (err < 0)
			return err;
	}

	/* enable ts queueing in FIFO if necessary */
	if (ts_settings->fifo_en.addr) {
		val = ST_LSM6DSX_SHIFT_VAL(1, ts_settings->fifo_en.mask);
		err = regmap_update_bits(hw->regmap,
					 ts_settings->fifo_en.addr,
					 ts_settings->fifo_en.mask, val);
		if (err < 0)
			return err;
	}
	return 0;
}

static int st_lsm6dsx_init_device(struct st_lsm6dsx_hw *hw)
{
	u8 drdy_int_reg;
	int err;

	/* device sw reset */
	err = regmap_update_bits(hw->regmap, hw->settings->reset_addr,
				 ST_LSM6DSX_REG_RESET_MASK,
				 FIELD_PREP(ST_LSM6DSX_REG_RESET_MASK, 1));
	if (err < 0)
		return err;

	msleep(50);

	/* reload trimming parameter */
	err = regmap_update_bits(hw->regmap, hw->settings->reset_addr,
				 ST_LSM6DSX_REG_BOOT_MASK,
				 FIELD_PREP(ST_LSM6DSX_REG_BOOT_MASK, 1));
	if (err < 0)
		return err;

	msleep(50);

	/* enable Block Data Update */
	err = regmap_update_bits(hw->regmap, ST_LSM6DSX_REG_BDU_ADDR,
				 ST_LSM6DSX_REG_BDU_MASK,
				 FIELD_PREP(ST_LSM6DSX_REG_BDU_MASK, 1));
	if (err < 0)
		return err;

	/* enable FIFO watermak interrupt */
	err = st_lsm6dsx_get_drdy_reg(hw, &drdy_int_reg);
	if (err < 0)
		return err;

	err = regmap_update_bits(hw->regmap, drdy_int_reg,
				 ST_LSM6DSX_REG_FIFO_FTH_IRQ_MASK,
				 FIELD_PREP(ST_LSM6DSX_REG_FIFO_FTH_IRQ_MASK,
					    1));
	if (err < 0)
		return err;

	/* enable Latched interrupts for device events */
	if (hw->settings->lir.addr) {
		unsigned int data;

		data = ST_LSM6DSX_SHIFT_VAL(1, hw->settings->lir.mask);
		err = regmap_update_bits(hw->regmap, hw->settings->lir.addr,
					 hw->settings->lir.mask, data);
		if (err < 0)
			return err;

		/* enable clear on read for latched interrupts */
		if (hw->settings->clear_on_read.addr) {
			data = ST_LSM6DSX_SHIFT_VAL(1,
					hw->settings->clear_on_read.mask);
			err = regmap_update_bits(hw->regmap,
					hw->settings->clear_on_read.addr,
					hw->settings->clear_on_read.mask,
					data);
			if (err < 0)
				return err;
		}
	}

	err = st_lsm6dsx_init_shub(hw);
	if (err < 0)
		return err;

	return st_lsm6dsx_init_hw_timer(hw);
}

static struct iio_dev *st_lsm6dsx_alloc_iiodev(struct st_lsm6dsx_hw *hw,
					       enum st_lsm6dsx_sensor_id id,
					       const char *name)
{
	struct st_lsm6dsx_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;
	iio_dev->available_scan_masks = st_lsm6dsx_available_scan_masks;
	iio_dev->channels = hw->settings->channels[id].chan;
	iio_dev->num_channels = hw->settings->channels[id].len;

	sensor = iio_priv(iio_dev);
	sensor->id = id;
	sensor->hw = hw;
	sensor->odr = hw->settings->odr_table[id].odr_avl[0].hz;
	sensor->gain = hw->settings->fs_table[id].fs_avl[0].gain;
	sensor->watermark = 1;

	switch (id) {
	case ST_LSM6DSX_ID_ACC:
		iio_dev->info = &st_lsm6dsx_acc_info;
		scnprintf(sensor->name, sizeof(sensor->name), "%s_accel",
			  name);
		break;
	case ST_LSM6DSX_ID_GYRO:
		iio_dev->info = &st_lsm6dsx_gyro_info;
		scnprintf(sensor->name, sizeof(sensor->name), "%s_gyro",
			  name);
		break;
	default:
		return NULL;
	}
	iio_dev->name = sensor->name;

	return iio_dev;
}

static void st_lsm6dsx_report_motion_event(struct st_lsm6dsx_hw *hw, int data)
{
	s64 timestamp = iio_get_time_ns(hw->iio_devs[ST_LSM6DSX_ID_ACC]);

	if ((data & hw->settings->event_settings.wakeup_src_z_mask) &&
	    (hw->enable_event & BIT(IIO_MOD_Z)))
		iio_push_event(hw->iio_devs[ST_LSM6DSX_ID_ACC],
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_Z,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_EITHER),
						  timestamp);

	if ((data & hw->settings->event_settings.wakeup_src_y_mask) &&
	    (hw->enable_event & BIT(IIO_MOD_Y)))
		iio_push_event(hw->iio_devs[ST_LSM6DSX_ID_ACC],
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_Y,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_EITHER),
						  timestamp);

	if ((data & hw->settings->event_settings.wakeup_src_x_mask) &&
	    (hw->enable_event & BIT(IIO_MOD_X)))
		iio_push_event(hw->iio_devs[ST_LSM6DSX_ID_ACC],
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_X,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_EITHER),
						  timestamp);
}

static irqreturn_t st_lsm6dsx_handler_thread(int irq, void *private)
{
	struct st_lsm6dsx_hw *hw = private;
	int count;
	int data, err;

	if (hw->enable_event) {
		err = regmap_read(hw->regmap,
				  hw->settings->event_settings.wakeup_src_reg,
				  &data);
		if (err < 0)
			return IRQ_NONE;

		if (data & hw->settings->event_settings.wakeup_src_status_mask)
			st_lsm6dsx_report_motion_event(hw, data);
	}

	mutex_lock(&hw->fifo_lock);
	count = hw->settings->fifo_ops.read_fifo(hw);
	mutex_unlock(&hw->fifo_lock);

	return count ? IRQ_HANDLED : IRQ_NONE;
}

static int st_lsm6dsx_irq_setup(struct st_lsm6dsx_hw *hw)
{
	struct st_sensors_platform_data *pdata;
	struct device_node *np = hw->dev->of_node;
	unsigned long irq_type;
	bool irq_active_low;
	int err;

	irq_type = irqd_get_trigger_type(irq_get_irq_data(hw->irq));

	switch (irq_type) {
	case IRQF_TRIGGER_HIGH:
	case IRQF_TRIGGER_RISING:
		irq_active_low = false;
		break;
	case IRQF_TRIGGER_LOW:
	case IRQF_TRIGGER_FALLING:
		irq_active_low = true;
		break;
	default:
		dev_info(hw->dev, "mode %lx unsupported\n", irq_type);
		return -EINVAL;
	}

	err = regmap_update_bits(hw->regmap, ST_LSM6DSX_REG_HLACTIVE_ADDR,
				 ST_LSM6DSX_REG_HLACTIVE_MASK,
				 FIELD_PREP(ST_LSM6DSX_REG_HLACTIVE_MASK,
					    irq_active_low));
	if (err < 0)
		return err;

	pdata = (struct st_sensors_platform_data *)hw->dev->platform_data;
	if ((np && of_property_read_bool(np, "drive-open-drain")) ||
	    (pdata && pdata->open_drain)) {
		err = regmap_update_bits(hw->regmap, ST_LSM6DSX_REG_PP_OD_ADDR,
					 ST_LSM6DSX_REG_PP_OD_MASK,
					 FIELD_PREP(ST_LSM6DSX_REG_PP_OD_MASK,
						    1));
		if (err < 0)
			return err;

		irq_type |= IRQF_SHARED;
	}

	err = devm_request_threaded_irq(hw->dev, hw->irq,
					NULL,
					st_lsm6dsx_handler_thread,
					irq_type | IRQF_ONESHOT,
					"lsm6dsx", hw);
	if (err) {
		dev_err(hw->dev, "failed to request trigger irq %d\n",
			hw->irq);
		return err;
	}

	return 0;
}

int st_lsm6dsx_probe(struct device *dev, int irq, int hw_id,
		     struct regmap *regmap)
{
	const struct st_lsm6dsx_shub_settings *hub_settings;
	struct st_lsm6dsx_hw *hw;
	const char *name = NULL;
	int i, err;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)hw);

	mutex_init(&hw->fifo_lock);
	mutex_init(&hw->conf_lock);
	mutex_init(&hw->page_lock);

	hw->buff = devm_kzalloc(dev, ST_LSM6DSX_BUFF_SIZE, GFP_KERNEL);
	if (!hw->buff)
		return -ENOMEM;

	hw->dev = dev;
	hw->irq = irq;
	hw->regmap = regmap;

	err = st_lsm6dsx_check_whoami(hw, hw_id, &name);
	if (err < 0)
		return err;

	for (i = 0; i < ST_LSM6DSX_ID_EXT0; i++) {
		hw->iio_devs[i] = st_lsm6dsx_alloc_iiodev(hw, i, name);
		if (!hw->iio_devs[i])
			return -ENOMEM;
	}

	err = st_lsm6dsx_init_device(hw);
	if (err < 0)
		return err;

	hub_settings = &hw->settings->shub_settings;
	if (hub_settings->master_en.addr) {
		err = st_lsm6dsx_shub_probe(hw, name);
		if (err < 0)
			return err;
	}

	if (hw->irq > 0) {
		err = st_lsm6dsx_irq_setup(hw);
		if (err < 0)
			return err;

		err = st_lsm6dsx_fifo_setup(hw);
		if (err < 0)
			return err;
	}

	for (i = 0; i < ST_LSM6DSX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		err = devm_iio_device_register(hw->dev, hw->iio_devs[i]);
		if (err)
			return err;
	}

	if (dev->of_node && of_property_read_bool(dev->of_node, "wakeup-source"))
		device_init_wakeup(dev, true);

	return 0;
}
EXPORT_SYMBOL(st_lsm6dsx_probe);

static int __maybe_unused st_lsm6dsx_suspend(struct device *dev)
{
	struct st_lsm6dsx_hw *hw = dev_get_drvdata(dev);
	struct st_lsm6dsx_sensor *sensor;
	int i, err = 0;

	for (i = 0; i < ST_LSM6DSX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		if (device_may_wakeup(dev) &&
		    sensor->id == ST_LSM6DSX_ID_ACC && hw->enable_event) {
			/* Enable wake from IRQ */
			enable_irq_wake(hw->irq);
			continue;
		}

		if (sensor->id == ST_LSM6DSX_ID_EXT0 ||
		    sensor->id == ST_LSM6DSX_ID_EXT1 ||
		    sensor->id == ST_LSM6DSX_ID_EXT2)
			err = st_lsm6dsx_shub_set_enable(sensor, false);
		else
			err = st_lsm6dsx_sensor_set_enable(sensor, false);
		if (err < 0)
			return err;

		hw->suspend_mask |= BIT(sensor->id);
	}

	if (hw->fifo_mode != ST_LSM6DSX_FIFO_BYPASS)
		err = st_lsm6dsx_flush_fifo(hw);

	return err;
}

static int __maybe_unused st_lsm6dsx_resume(struct device *dev)
{
	struct st_lsm6dsx_hw *hw = dev_get_drvdata(dev);
	struct st_lsm6dsx_sensor *sensor;
	int i, err = 0;

	for (i = 0; i < ST_LSM6DSX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (device_may_wakeup(dev) &&
		    sensor->id == ST_LSM6DSX_ID_ACC && hw->enable_event)
			disable_irq_wake(hw->irq);

		if (!(hw->suspend_mask & BIT(sensor->id)))
			continue;

		if (sensor->id == ST_LSM6DSX_ID_EXT0 ||
		    sensor->id == ST_LSM6DSX_ID_EXT1 ||
		    sensor->id == ST_LSM6DSX_ID_EXT2)
			err = st_lsm6dsx_shub_set_enable(sensor, true);
		else
			err = st_lsm6dsx_sensor_set_enable(sensor, true);
		if (err < 0)
			return err;

		hw->suspend_mask &= ~BIT(sensor->id);
	}

	if (hw->enable_mask)
		err = st_lsm6dsx_set_fifo_mode(hw, ST_LSM6DSX_FIFO_CONT);

	return err;
}

const struct dev_pm_ops st_lsm6dsx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_lsm6dsx_suspend, st_lsm6dsx_resume)
};
EXPORT_SYMBOL(st_lsm6dsx_pm_ops);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsx driver");
MODULE_LICENSE("GPL v2");
