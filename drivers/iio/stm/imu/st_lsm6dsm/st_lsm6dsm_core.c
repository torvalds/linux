// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lsm6dsm core driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/irq.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <asm/unaligned.h>

#include <linux/iio/common/st_sensors.h>
#include "st_lsm6dsm.h"

#define MS_TO_NS(msec)				((msec) * 1000 * 1000)

#ifndef MAX
#define MAX(a, b)				(((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b)				(((a) < (b)) ? (a) : (b))
#endif

#define MIN_BNZ(a, b)				(((a) < (b)) ? ((a == 0) ? \
						(b) : (a)) : ((b == 0) ? \
						(a) : (b)))

/* COMMON VALUES FOR ACCEL-GYRO SENSORS */
#define ST_LSM6DSM_DRDY_PULSE_CFG_G			0x0b
#define ST_LSM6DSM_WAI_ADDRESS				0x0f
#define ST_LSM6DSM_WAI_EXP				0x6a
#define ST_LSM6DSM_INT1_ADDR				0x0d
#define ST_LSM6DSM_INT2_ADDR				0x0e
#define ST_LSM6DSM_ACCEL_DRDY_IRQ_MASK			0x01
#define ST_LSM6DSM_GYRO_DRDY_IRQ_MASK			0x02
#define ST_LSM6DSM_MD1_ADDR				0x5e
#define ST_LSM6DSM_ODR_LIST_NUM				7
#define ST_LSM6DSM_ODR_POWER_OFF_VAL			0x00
#define ST_LSM6DSM_ODR_13HZ_VAL				0x01
#define ST_LSM6DSM_ODR_26HZ_VAL				0x02
#define ST_LSM6DSM_ODR_52HZ_VAL				0x03
#define ST_LSM6DSM_ODR_104HZ_VAL			0x04
#define ST_LSM6DSM_ODR_208HZ_VAL			0x05
#define ST_LSM6DSM_ODR_416HZ_VAL			0x06
#define ST_LSM6DSM_ODR_833HZ_VAL			0x07
#define ST_LSM6DSM_FS_LIST_NUM				4
#define ST_LSM6DSM_BDU_ADDR				0x12
#define ST_LSM6DSM_BDU_MASK				0x40
#define ST_LSM6DSM_EN_BIT				0x01
#define ST_LSM6DSM_DIS_BIT				0x00
#define ST_LSM6DSM_FUNC_EN_ADDR				0x19
#define ST_LSM6DSM_FUNC_EN_MASK				0x04
#define ST_LSM6DSM_FUNC_CFG_ACCESS_ADDR			0x01
#define ST_LSM6DSM_FUNC_CFG_ACCESS_MASK			0x01
#define ST_LSM6DSM_FUNC_CFG_ACCESS_MASK2		0x04
#define ST_LSM6DSM_FUNC_CFG_REG2_MASK			0x80
#define ST_LSM6DSM_FUNC_CFG_START1_ADDR			0x62
#define ST_LSM6DSM_FUNC_CFG_START2_ADDR			0x63
#define ST_LSM6DSM_SENSORHUB_ADDR			0x1a
#define ST_LSM6DSM_SENSORHUB_MASK			0x01
#define ST_LSM6DSM_SENSORHUB_TRIG_MASK			0x10
#define ST_LSM6DSM_TRIG_INTERNAL			0x00
#define ST_LSM6DSM_TRIG_EXTERNAL			0x01
#define ST_LSM6DSM_SELFTEST_ADDR			0x14
#define ST_LSM6DSM_SELFTEST_ACCEL_MASK			0x03
#define ST_LSM6DSM_SELFTEST_GYRO_MASK			0x0c
#define ST_LSM6DSM_SELF_TEST_DISABLED_VAL		0x00
#define ST_LSM6DSM_SELF_TEST_POS_SIGN_VAL		0x01
#define ST_LSM6DSM_SELF_TEST_NEG_ACCEL_SIGN_VAL		0x02
#define ST_LSM6DSM_SELF_TEST_NEG_GYRO_SIGN_VAL		0x03
#define ST_LSM6DSM_LIR_ADDR				0x58
#define ST_LSM6DSM_LIR_MASK				0x01
#define ST_LSM6DSM_INT_ENABLE_MASK			0x80
#define ST_LSM6DSM_TIMER_EN_ADDR			0x19
#define ST_LSM6DSM_TIMER_EN_MASK			0x20
#define ST_LSM6DSM_PEDOMETER_EN_ADDR			0x19
#define ST_LSM6DSM_PEDOMETER_EN_MASK			0x10
#define ST_LSM6DSM_INT2_ON_INT1_ADDR			0x13
#define ST_LSM6DSM_INT2_ON_INT1_MASK			0x20
#define ST_LSM6DSM_MIN_DURATION_MS			1638
#define ST_LSM6DSM_ROUNDING_ADDR			0x16
#define ST_LSM6DSM_ROUNDING_MASK			0x04
#define ST_LSM6DSM_FIFO_MODE_ADDR			0x0a
#define ST_LSM6DSM_FIFO_MODE_MASK			0x07
#define ST_LSM6DSM_FIFO_MODE_BYPASS			0x00
#define ST_LSM6DSM_FIFO_MODE_CONTINUOS			0x06
#define ST_LSM6DSM_FIFO_THRESHOLD_IRQ_MASK		0x08
#define ST_LSM6DSM_FIFO_ODR_MAX				0x40
#define ST_LSM6DSM_FIFO_DECIMATOR_ADDR			0x08
#define ST_LSM6DSM_FIFO_ACCEL_DECIMATOR_MASK		0x07
#define ST_LSM6DSM_FIFO_GYRO_DECIMATOR_MASK		0x38
#define ST_LSM6DSM_FIFO_DECIMATOR2_ADDR			0x09
#define ST_LSM6DSM_FIFO_THR_L_ADDR			0x06
#define ST_LSM6DSM_FIFO_THR_H_ADDR			0x07
#define ST_LSM6DSM_FIFO_THR_MASK			0x07ff
#define ST_LSM6DSM_FIFO_THR_H_MASK			0x07
#define ST_LSM6DSM_FIFO_THR_IRQ_MASK			0x08
#define ST_LSM6DSM_RESET_ADDR				0x12
#define ST_LSM6DSM_RESET_MASK				0x01
#define ST_LSM6DSM_TEST_REG_ADDR			0x00
#define ST_LSM6DSM_START_INJECT_XL_MASK			0x08
#define ST_LSM6DSM_INJECT_XL_X_ADDR			0x06
#define ST_LSM6DSM_SELFTEST_NA_MS			"na"
#define ST_LSM6DSM_SELFTEST_FAIL_MS			"fail"
#define ST_LSM6DSM_SELFTEST_PASS_MS			"pass"

/* CUSTOM VALUES FOR ACCEL SENSOR */
#define ST_LSM6DSM_ACCEL_ODR_ADDR			0x10
#define ST_LSM6DSM_ACCEL_ODR_MASK			0xf0
#define ST_LSM6DSM_ACCEL_FS_ADDR			0x10
#define ST_LSM6DSM_ACCEL_FS_MASK			0x0c
#define ST_LSM6DSM_ACCEL_FS_2G_VAL			0x00
#define ST_LSM6DSM_ACCEL_FS_4G_VAL			0x02
#define ST_LSM6DSM_ACCEL_FS_8G_VAL			0x03
#define ST_LSM6DSM_ACCEL_FS_16G_VAL			0x01
#define ST_LSM6DSM_ACCEL_FS_2G_GAIN			IIO_G_TO_M_S_2(61000)
#define ST_LSM6DSM_ACCEL_FS_4G_GAIN			IIO_G_TO_M_S_2(122000)
#define ST_LSM6DSM_ACCEL_FS_8G_GAIN			IIO_G_TO_M_S_2(244000)
#define ST_LSM6DSM_ACCEL_FS_16G_GAIN			IIO_G_TO_M_S_2(488000)
#define ST_LSM6DSM_ACCEL_OUT_X_L_ADDR			0x28
#define ST_LSM6DSM_ACCEL_OUT_Y_L_ADDR			0x2a
#define ST_LSM6DSM_ACCEL_OUT_Z_L_ADDR			0x2c
#define ST_LSM6DSM_ACCEL_STD_52HZ			1
#define ST_LSM6DSM_ACCEL_STD_104HZ			2
#define ST_LSM6DSM_ACCEL_STD_208HZ			3
#define ST_LSM6DSM_SELFTEST_ACCEL_ADDR			0x10
#define ST_LSM6DSM_SELFTEST_ACCEL_REG_VALUE		0x40
#define ST_LSM6DSM_SELFTEST_ACCEL_MIN			1492
#define ST_LSM6DSM_SELFTEST_ACCEL_MAX			27868

/* CUSTOM VALUES FOR GYRO SENSOR */
#define ST_LSM6DSM_GYRO_ODR_ADDR			0x11
#define ST_LSM6DSM_GYRO_ODR_MASK			0xf0
#define ST_LSM6DSM_GYRO_FS_ADDR				0x11
#define ST_LSM6DSM_GYRO_FS_MASK				0x0c
#define ST_LSM6DSM_GYRO_FS_250_VAL			0x00
#define ST_LSM6DSM_GYRO_FS_500_VAL			0x01
#define ST_LSM6DSM_GYRO_FS_1000_VAL			0x02
#define ST_LSM6DSM_GYRO_FS_2000_VAL			0x03
#define ST_LSM6DSM_GYRO_FS_250_GAIN			IIO_DEGREE_TO_RAD(8750000)
#define ST_LSM6DSM_GYRO_FS_500_GAIN			IIO_DEGREE_TO_RAD(17500000)
#define ST_LSM6DSM_GYRO_FS_1000_GAIN			IIO_DEGREE_TO_RAD(35000000)
#define ST_LSM6DSM_GYRO_FS_2000_GAIN			IIO_DEGREE_TO_RAD(70000000)
#define ST_LSM6DSM_GYRO_OUT_X_L_ADDR			0x22
#define ST_LSM6DSM_GYRO_OUT_Y_L_ADDR			0x24
#define ST_LSM6DSM_GYRO_OUT_Z_L_ADDR			0x26
#define ST_LSM6DSM_GYRO_STD_13HZ			2
#define ST_LSM6DSM_GYRO_STD_52HZ			3
#define ST_LSM6DSM_GYRO_STD_104HZ			5
#define ST_LSM6DSM_GYRO_STD_208HZ			16	
#define ST_LSM6DSM_SELFTEST_GYRO_ADDR			0x11
#define ST_LSM6DSM_SELFTEST_GYRO_REG_VALUE		0x4c
#define ST_LSM6DSM_SELFTEST_GYRO_MIN			2142
#define ST_LSM6DSM_SELFTEST_GYRO_MAX			10000

/* CUSTOM VALUES FOR SIGNIFICANT MOTION SENSOR */
#define ST_LSM6DSM_SIGN_MOTION_EN_ADDR			0x19
#define ST_LSM6DSM_SIGN_MOTION_EN_MASK			0x01
#define ST_LSM6DSM_SIGN_MOTION_DRDY_IRQ_MASK		0x40

/* CUSTOM VALUES FOR STEP DETECTOR SENSOR */
#define ST_LSM6DSM_STEP_DETECTOR_DRDY_IRQ_MASK		0x80

/* CUSTOM VALUES FOR STEP COUNTER SENSOR */
#define ST_LSM6DSM_STEP_COUNTER_DRDY_IRQ_MASK		0x80
#define ST_LSM6DSM_STEP_COUNTER_OUT_L_ADDR		0x4b
#define ST_LSM6DSM_STEP_COUNTER_RES_ADDR		0x19
#define ST_LSM6DSM_STEP_COUNTER_RES_MASK		0x06
#define ST_LSM6DSM_STEP_COUNTER_RES_ALL_EN		0x03
#define ST_LSM6DSM_STEP_COUNTER_RES_FUNC_EN		0x02
#define ST_LSM6DSM_STEP_COUNTER_DURATION_ADDR		0x15
#define ST_LSM6DSM_STEP_COUNTER_THS_ADDR		0x0f
#define ST_LSM6DSM_STEP_COUNTER_THS_2G_VALUE		(0x00 | 0x10)
#define ST_LSM6DSM_STEP_COUNTER_THS_4G_VALUE		(0x80 | 0x08)

/* CUSTOM VALUES FOR TILT SENSOR */
#define ST_LSM6DSM_TILT_EN_ADDR				0x19
#define ST_LSM6DSM_TILT_EN_MASK				0x08
#define ST_LSM6DSM_TILT_DRDY_IRQ_MASK			0x02

/* CUSTOM VALUES FOR TAP AND TAP_TAP SENSOR */
#define ST_LSM6DSM_TAP_EN_ADDR				0x58
#define ST_LSM6DSM_TAP_EN_MASK				GENMASK(3, 1)
#define ST_LSM6DSM_STAP_DRDY_IRQ_MASK			0x40
#define ST_LSM6DSM_DTAP_DRDY_IRQ_MASK			0x08
#define ST_LSM6DSM_TAP_THS_ADDR				0x59
#define ST_LSM6DSM_TAP_THS_MASK				GENMASK(4, 0)
#define ST_LSM6DSM_TAP_DUR_ADDR				0x5a
#define ST_LSM6DSM_TAP_DUR_MASK				0xff
#define ST_LSM6DSM_DTAP_EN_ADDR				0x5b
#define ST_LSM6DSM_DTAP_EN_MASK				0x80

/* CUSTOM VALUES FOR WRIST TILT SENSOR */
#define ST_LSM6DSM_WTILT_EN_ADDR			0x19
#define ST_LSM6DSM_WTILT_EN_MASK			0x80
#define ST_LSM6DSM_WTILT_DRDY_IRQ_MASK			0x01
#define ST_LSM6DSM_WRIST_TILT_IA			0x55

#define ST_LSM6DSM_ACCEL_SUFFIX_NAME			"accel"
#define ST_LSM6DSM_GYRO_SUFFIX_NAME			"gyro"
#define ST_LSM6DSM_STEP_COUNTER_SUFFIX_NAME		"step_c"
#define ST_LSM6DSM_STEP_DETECTOR_SUFFIX_NAME		"step_d"
#define ST_LSM6DSM_SIGN_MOTION_SUFFIX_NAME		"sign_motion"
#define ST_LSM6DSM_TILT_SUFFIX_NAME			"tilt"
#define ST_LSM6DSM_WTILT_SUFFIX_NAME			"wrist"
#define ST_LSM6DSM_STAP_SUFFIX_NAME			"stap"
#define ST_LSM6DSM_DTAP_SUFFIX_NAME			"dtap"

#define ST_LSM6DSM_26HZ_INJECT_NS_UP			(ULLONG_MAX)
#define ST_LSM6DSM_26HZ_INJECT_NS_DOWN			(25641026LL)
#define ST_LSM6DSM_52HZ_INJECT_NS_UP			ST_LSM6DSM_26HZ_INJECT_NS_DOWN
#define ST_LSM6DSM_52HZ_INJECT_NS_DOWN			(12820512LL)
#define ST_LSM6DSM_104HZ_INJECT_NS_UP			ST_LSM6DSM_52HZ_INJECT_NS_DOWN
#define ST_LSM6DSM_104HZ_INJECT_NS_DOWN			(6410256LL)
#define ST_LSM6DSM_208HZ_INJECT_NS_UP			ST_LSM6DSM_104HZ_INJECT_NS_DOWN
#define ST_LSM6DSM_208HZ_INJECT_NS_DOWN			(0)

#define ST_LSM6DSM_DEV_ATTR_SAMP_FREQ() \
		IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO, \
			st_lsm6dsm_sysfs_get_sampling_frequency, \
			st_lsm6dsm_sysfs_set_sampling_frequency)

#define ST_LSM6DSM_DEV_ATTR_SAMP_FREQ_AVAIL() \
		IIO_DEV_ATTR_SAMP_FREQ_AVAIL( \
			st_lsm6dsm_sysfs_sampling_frequency_avail)

#define ST_LSM6DSM_DEV_ATTR_SCALE_AVAIL(name) \
		IIO_DEVICE_ATTR(name, S_IRUGO, \
			st_lsm6dsm_sysfs_scale_avail, NULL , 0);

static struct st_lsm6dsm_selftest_table {
	char *string_mode;
	u8 accel_value;
	u8 gyro_value;
	u8 gyro_mask;
} st_lsm6dsm_selftest_table[] = {
	[0] = {
		.string_mode = "disabled",
		.accel_value = ST_LSM6DSM_SELF_TEST_DISABLED_VAL,
		.gyro_value = ST_LSM6DSM_SELF_TEST_DISABLED_VAL,
	},
	[1] = {
		.string_mode = "positive-sign",
		.accel_value = ST_LSM6DSM_SELF_TEST_POS_SIGN_VAL,
		.gyro_value = ST_LSM6DSM_SELF_TEST_POS_SIGN_VAL
	},
	[2] = {
		.string_mode = "negative-sign",
		.accel_value = ST_LSM6DSM_SELF_TEST_NEG_ACCEL_SIGN_VAL,
		.gyro_value = ST_LSM6DSM_SELF_TEST_NEG_GYRO_SIGN_VAL
	},
};

struct st_lsm6dsm_odr_reg {
	unsigned int hz;
	u8 value;
};

static struct st_lsm6dsm_odr_table {
	u8 addr[2];
	u8 mask[2];
	struct st_lsm6dsm_odr_reg odr_avl[ST_LSM6DSM_ODR_LIST_NUM];
} st_lsm6dsm_odr_table = {
	.addr[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_ODR_ADDR,
	.mask[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_ODR_MASK,
	.addr[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_ODR_ADDR,
	.mask[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_ODR_MASK,
	.odr_avl[0] = { .hz = 13, .value = ST_LSM6DSM_ODR_13HZ_VAL },
	.odr_avl[1] = { .hz = 26, .value = ST_LSM6DSM_ODR_26HZ_VAL },
	.odr_avl[2] = { .hz = 52, .value = ST_LSM6DSM_ODR_52HZ_VAL },
	.odr_avl[3] = { .hz = 104, .value = ST_LSM6DSM_ODR_104HZ_VAL },
	.odr_avl[4] = { .hz = 208, .value = ST_LSM6DSM_ODR_208HZ_VAL },
	.odr_avl[5] = { .hz = 416, .value = ST_LSM6DSM_ODR_416HZ_VAL },
	.odr_avl[6] = { .hz = 833, .value = ST_LSM6DSM_ODR_833HZ_VAL },
};

struct st_lsm6dsm_fs_reg {
	unsigned int gain;
	u8 value;
};

static struct st_lsm6dsm_fs_table {
	u8 addr;
	u8 mask;
	struct st_lsm6dsm_fs_reg fs_avl[ST_LSM6DSM_FS_LIST_NUM];
} st_lsm6dsm_fs_table[ST_INDIO_DEV_NUM] = {
	[ST_MASK_ID_ACCEL] = {
		.addr = ST_LSM6DSM_ACCEL_FS_ADDR,
		.mask = ST_LSM6DSM_ACCEL_FS_MASK,
		.fs_avl[0] = { .gain = ST_LSM6DSM_ACCEL_FS_2G_GAIN,
					.value = ST_LSM6DSM_ACCEL_FS_2G_VAL },
		.fs_avl[1] = { .gain = ST_LSM6DSM_ACCEL_FS_4G_GAIN,
					.value = ST_LSM6DSM_ACCEL_FS_4G_VAL },
		.fs_avl[2] = { .gain = ST_LSM6DSM_ACCEL_FS_8G_GAIN,
					.value = ST_LSM6DSM_ACCEL_FS_8G_VAL },
		.fs_avl[3] = { .gain = ST_LSM6DSM_ACCEL_FS_16G_GAIN,
					.value = ST_LSM6DSM_ACCEL_FS_16G_VAL },
	},
	[ST_MASK_ID_GYRO] = {
		.addr = ST_LSM6DSM_GYRO_FS_ADDR,
		.mask = ST_LSM6DSM_GYRO_FS_MASK,
		.fs_avl[0] = { .gain = ST_LSM6DSM_GYRO_FS_250_GAIN,
					.value = ST_LSM6DSM_GYRO_FS_250_VAL },
		.fs_avl[1] = { .gain = ST_LSM6DSM_GYRO_FS_500_GAIN,
					.value = ST_LSM6DSM_GYRO_FS_500_VAL },
		.fs_avl[2] = { .gain = ST_LSM6DSM_GYRO_FS_1000_GAIN,
					.value = ST_LSM6DSM_GYRO_FS_1000_VAL },
		.fs_avl[3] = { .gain = ST_LSM6DSM_GYRO_FS_2000_GAIN,
					.value = ST_LSM6DSM_GYRO_FS_2000_VAL },
	}
};

static const struct iio_event_spec singol_thr_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_RISING,
};

const struct iio_event_spec lsm6dsm_fifo_flush_event = {
	.type = STM_IIO_EV_TYPE_FIFO_FLUSH,
	.dir = IIO_EV_DIR_EITHER,
};

static const struct iio_chan_spec st_lsm6dsm_accel_ch[] = {
	ST_LSM6DSM_LSM_CHANNELS(IIO_ACCEL, 1, 0, IIO_MOD_X, IIO_LE,
				16, 16, ST_LSM6DSM_ACCEL_OUT_X_L_ADDR, 's'),
	ST_LSM6DSM_LSM_CHANNELS(IIO_ACCEL, 1, 1, IIO_MOD_Y, IIO_LE,
				16, 16, ST_LSM6DSM_ACCEL_OUT_Y_L_ADDR, 's'),
	ST_LSM6DSM_LSM_CHANNELS(IIO_ACCEL, 1, 2, IIO_MOD_Z, IIO_LE,
				16, 16, ST_LSM6DSM_ACCEL_OUT_Z_L_ADDR, 's'),
	ST_LSM6DSM_FLUSH_CHANNEL(IIO_ACCEL),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct iio_chan_spec st_lsm6dsm_gyro_ch[] = {
	ST_LSM6DSM_LSM_CHANNELS(IIO_ANGL_VEL, 1, 0, IIO_MOD_X, IIO_LE,
				16, 16, ST_LSM6DSM_GYRO_OUT_X_L_ADDR, 's'),
	ST_LSM6DSM_LSM_CHANNELS(IIO_ANGL_VEL, 1, 1, IIO_MOD_Y, IIO_LE,
				16, 16, ST_LSM6DSM_GYRO_OUT_Y_L_ADDR, 's'),
	ST_LSM6DSM_LSM_CHANNELS(IIO_ANGL_VEL, 1, 2, IIO_MOD_Z, IIO_LE,
				16, 16, ST_LSM6DSM_GYRO_OUT_Z_L_ADDR, 's'),
	ST_LSM6DSM_FLUSH_CHANNEL(IIO_ANGL_VEL),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

static const struct iio_chan_spec st_lsm6dsm_sign_motion_ch[] = {
	{
		.type = STM_IIO_SIGN_MOTION,
		.channel = 0,
		.modified = 0,
		.event_spec = &singol_thr_event,
		.num_event_specs = 1,
	},
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

static const struct iio_chan_spec st_lsm6dsm_step_c_ch[] = {
	{
		.type = STM_IIO_STEP_COUNTER,
		.modified = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.address = ST_LSM6DSM_STEP_COUNTER_OUT_L_ADDR,
		.scan_type = {
			.sign = 'u',
			.realbits = 64,
			.storagebits = 64,
			.endianness = IIO_LE,
		},
	},
	ST_LSM6DSM_FLUSH_CHANNEL(STM_IIO_STEP_COUNTER),
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

static const struct iio_chan_spec st_lsm6dsm_step_d_ch[] = {
	ST_LSM6DSM_FLUSH_CHANNEL(IIO_STEPS),
	IIO_CHAN_SOFT_TIMESTAMP(0)
};

static const struct iio_chan_spec st_lsm6dsm_tilt_ch[] = {
	ST_LSM6DSM_FLUSH_CHANNEL(STM_IIO_TILT),
	IIO_CHAN_SOFT_TIMESTAMP(0)
};

static const struct iio_chan_spec st_lsm6dsm_wtilt_ch[] = {
	{
		.type = STM_IIO_WRIST_TILT_GESTURE,
		.modified = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.address = ST_LSM6DSM_WRIST_TILT_IA,
		.scan_type = {
			.sign = 'u',
			.realbits = 8,
			.storagebits = 8,
			.endianness = IIO_LE,
		},
	},
	ST_LSM6DSM_FLUSH_CHANNEL(STM_IIO_WRIST_TILT_GESTURE),
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

static const struct iio_chan_spec st_lsm6dsm_tap_ch[] = {
	{
		.type = STM_IIO_TAP,
		.channel = 0,
		.modified = 0,
		.event_spec = &singol_thr_event,
		.num_event_specs = 1,
	},
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

static const struct iio_chan_spec st_lsm6dsm_tap_tap_ch[] = {
	{
		.type = STM_IIO_TAP_TAP,
		.channel = 0,
		.modified = 0,
		.event_spec = &singol_thr_event,
		.num_event_specs = 1,
	},
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

int st_lsm6dsm_write_data_with_mask(struct lsm6dsm_data *cdata,
				u8 reg_addr, u8 mask, u8 data, bool b_lock)
{
	int err;
	u8 new_data = 0x00, old_data = 0x00;

	err = cdata->tf->read(cdata, reg_addr, 1, &old_data, b_lock);
	if (err < 0)
		return err;

	new_data = ((old_data & (~mask)) | ((data << __ffs(mask)) & mask));

	if (new_data == old_data)
		return 1;

	return cdata->tf->write(cdata, reg_addr, 1, &new_data, b_lock);
}
EXPORT_SYMBOL(st_lsm6dsm_write_data_with_mask);

static inline int st_lsm6dsm_enable_embedded_page_regs(struct lsm6dsm_data *cdata, bool enable)
{
	u8 value = 0x00;

	if (enable)
		value = ST_LSM6DSM_FUNC_CFG_REG2_MASK;

	return cdata->tf->write(cdata, ST_LSM6DSM_FUNC_CFG_ACCESS_ADDR, 1, &value, false);
}

int st_lsm6dsm_write_embedded_registers(struct lsm6dsm_data *cdata,
					u8 reg_addr, u8 *data, int len)
{
	int err = 0, err2, count = 0;

	mutex_lock(&cdata->bank_registers_lock);

	if (cdata->enable_digfunc_mask) {
		err = st_lsm6dsm_write_data_with_mask(cdata,
					ST_LSM6DSM_FUNC_EN_ADDR,
					ST_LSM6DSM_FUNC_EN_MASK,
					ST_LSM6DSM_DIS_BIT, false);
		if (err < 0) {
			mutex_unlock(&cdata->bank_registers_lock);
			return err;
		}
	}

	udelay(100);

	err = st_lsm6dsm_enable_embedded_page_regs(cdata, true);
	if (err < 0)
		goto restore_digfunc;

	udelay(100);

	err = cdata->tf->write(cdata, reg_addr, len, data, false);
	if (err < 0)
		goto restore_bank_regs;

	err = st_lsm6dsm_enable_embedded_page_regs(cdata, false);
	if (err < 0)
		goto restore_bank_regs;

	udelay(100);

	if (cdata->enable_digfunc_mask) {
		err = st_lsm6dsm_write_data_with_mask(cdata,
					ST_LSM6DSM_FUNC_EN_ADDR,
					ST_LSM6DSM_FUNC_EN_MASK,
					ST_LSM6DSM_EN_BIT, false);
		if (err < 0)
			goto restore_digfunc;
	}

	mutex_unlock(&cdata->bank_registers_lock);

	return 0;

restore_bank_regs:
	do {
		msleep(200);
		err2 = st_lsm6dsm_enable_embedded_page_regs(cdata, false);
	} while ((err2 < 0) && (count++ < 10));

	if (count >= 10)
		pr_err("not able to close embedded page registers. It make driver unstable!\n");

restore_digfunc:
	if (!cdata->enable_digfunc_mask) {
		err2 = st_lsm6dsm_write_data_with_mask(cdata,
					ST_LSM6DSM_FUNC_EN_ADDR,
					ST_LSM6DSM_FUNC_EN_MASK,
					ST_LSM6DSM_EN_BIT, false);
	}

	mutex_unlock(&cdata->bank_registers_lock);

	return err;
}

static int lsm6dsm_set_watermark(struct lsm6dsm_data *cdata)
{
	int err;
	u8 reg_value = 0;
	u16 fifo_watermark;
	unsigned int fifo_len, sip = 0, min_pattern = UINT_MAX;

	if (cdata->fifo_output[ST_MASK_ID_ACCEL].sip > 0) {
		sip += cdata->fifo_output[ST_MASK_ID_ACCEL].sip;
		min_pattern = MIN(min_pattern,
			cdata->hwfifo_watermark[ST_MASK_ID_ACCEL] /
			cdata->fifo_output[ST_MASK_ID_ACCEL].sip);
	}

	if (cdata->fifo_output[ST_MASK_ID_GYRO].sip > 0) {
		sip += cdata->fifo_output[ST_MASK_ID_GYRO].sip;
		min_pattern = MIN(min_pattern,
			cdata->hwfifo_watermark[ST_MASK_ID_GYRO] /
			cdata->fifo_output[ST_MASK_ID_GYRO].sip);
	}

#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
	if (cdata->fifo_output[ST_MASK_ID_EXT0].sip > 0) {
		sip += cdata->fifo_output[ST_MASK_ID_EXT0].sip;
		min_pattern = MIN(min_pattern,
			cdata->hwfifo_watermark[ST_MASK_ID_EXT0] /
			cdata->fifo_output[ST_MASK_ID_EXT0].sip);
	}
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */

	if (sip == 0)
		return 0;

	if (min_pattern == 0)
		min_pattern = 1;

	min_pattern = MIN(min_pattern, ((unsigned int)ST_LSM6DSM_MAX_FIFO_THRESHOLD / sip));

	fifo_len = min_pattern * sip * ST_LSM6DSM_FIFO_ELEMENT_LEN_BYTE;
	fifo_watermark = (fifo_len / 2);

	if (fifo_watermark < (ST_LSM6DSM_FIFO_ELEMENT_LEN_BYTE / 2))
		fifo_watermark = ST_LSM6DSM_FIFO_ELEMENT_LEN_BYTE / 2;

	if (fifo_watermark != cdata->fifo_watermark) {
		err = cdata->tf->read(cdata, ST_LSM6DSM_FIFO_THR_H_ADDR, 1, &reg_value, true);
		if (err < 0)
			return err;

		fifo_watermark = (fifo_watermark & ST_LSM6DSM_FIFO_THR_MASK) |
				 ((reg_value & ~ST_LSM6DSM_FIFO_THR_H_MASK) << 8);

		err = cdata->tf->write(cdata, ST_LSM6DSM_FIFO_THR_L_ADDR, 2,
						(u8 *)&fifo_watermark, true);
		if (err < 0)
			return err;

		cdata->fifo_watermark = fifo_watermark;
	}

	return 0;
}

int st_lsm6dsm_set_fifo_mode(struct lsm6dsm_data *cdata, enum fifo_mode fm)
{
	int err;
	u8 reg_value;

	switch (fm) {
	case BYPASS:
		reg_value = ST_LSM6DSM_FIFO_MODE_BYPASS;
		break;
	case CONTINUOS:
		reg_value = ST_LSM6DSM_FIFO_MODE_CONTINUOS | ST_LSM6DSM_FIFO_ODR_MAX;
		break;
	default:
		return -EINVAL;
	}

	err = cdata->tf->write(cdata, ST_LSM6DSM_FIFO_MODE_ADDR, 1, &reg_value, true);
	if (err < 0)
		return err;

	if (fm != BYPASS) {
		cdata->slower_counter = 0;
		cdata->fifo_enable_timestamp =
			iio_get_time_ns(cdata->indio_dev[ST_MASK_ID_ACCEL]);
		cdata->fifo_output[ST_MASK_ID_GYRO].timestamp = 0;
		cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp = 0;
		cdata->fifo_output[ST_MASK_ID_EXT0].timestamp = 0;
	}

	cdata->fifo_status = fm;

	return 0;
}
EXPORT_SYMBOL(st_lsm6dsm_set_fifo_mode);

static int lsm6dsm_write_decimators(struct lsm6dsm_data *cdata,
							u8 decimators[3])
{
	int i;
	u8 value[3], decimators_reg[2];

	for (i = 0; i < 3; i++) {
		switch (decimators[i]) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
			value[i] = decimators[i];
			break;
		case 8:
			value[i] = 0x05;
			break;
		case 16:
			value[i] = 0x06;
			break;
		case 32:
			value[i] = 0x07;
			break;
		default:
			return -EINVAL;
		}
	}

	decimators_reg[0] = value[0] | (value[1] << 3);
	decimators_reg[1] = value[2];

	return cdata->tf->write(cdata, ST_LSM6DSM_FIFO_DECIMATOR_ADDR,
			ARRAY_SIZE(decimators_reg), decimators_reg, true);
}

static bool lsm6dsm_calculate_fifo_decimators(struct lsm6dsm_data *cdata,
				u8 decimators[3], u8 samples_in_pattern[3],
				unsigned int new_v_odr[ST_INDIO_DEV_NUM + 1],
				unsigned int new_hw_odr[ST_INDIO_DEV_NUM + 1],
				int64_t new_deltatime[ST_INDIO_DEV_NUM + 1],
				short new_fifo_decimator[ST_INDIO_DEV_NUM + 1])
{
	unsigned int trigger_odr;
	u8 min_decimator, max_decimator = 0;
	u8 accel_decimator = 0, gyro_decimator = 0, ext_decimator = 0;

	trigger_odr = new_hw_odr[ST_MASK_ID_ACCEL];
	if (trigger_odr < new_hw_odr[ST_MASK_ID_GYRO])
		trigger_odr = new_hw_odr[ST_MASK_ID_GYRO];

	if ((cdata->sensors_use_fifo & BIT(ST_MASK_ID_ACCEL)) &&
			(new_v_odr[ST_MASK_ID_ACCEL] != 0) && cdata->accel_on)
		accel_decimator = trigger_odr / new_v_odr[ST_MASK_ID_ACCEL];

	if ((cdata->sensors_use_fifo & BIT(ST_MASK_ID_GYRO)) &&
				(new_v_odr[ST_MASK_ID_GYRO] != 0) &&
					(new_hw_odr[ST_MASK_ID_GYRO] > 0))
		gyro_decimator = trigger_odr / new_v_odr[ST_MASK_ID_GYRO];

#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
	if ((cdata->sensors_use_fifo & BIT(ST_MASK_ID_EXT0)) &&
			(new_v_odr[ST_MASK_ID_EXT0] != 0) && cdata->magn_on)
		ext_decimator = trigger_odr / new_v_odr[ST_MASK_ID_EXT0];

	new_fifo_decimator[ST_MASK_ID_EXT0] = 1;
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */

	new_fifo_decimator[ST_MASK_ID_ACCEL] = 1;
	new_fifo_decimator[ST_MASK_ID_GYRO] = 1;

	if ((accel_decimator != 0) || (gyro_decimator != 0) || (ext_decimator != 0)) {
		min_decimator = MIN_BNZ(MIN_BNZ(accel_decimator, gyro_decimator), ext_decimator);
		max_decimator = MAX(MAX(accel_decimator, gyro_decimator), ext_decimator);
		if (min_decimator != 1) {
			if ((accel_decimator / min_decimator) == 1) {
				accel_decimator = 1;
				new_fifo_decimator[ST_MASK_ID_ACCEL] = min_decimator;
			} else if ((gyro_decimator / min_decimator) == 1) {
				gyro_decimator = 1;
				new_fifo_decimator[ST_MASK_ID_GYRO] = min_decimator;
			} else if ((ext_decimator / min_decimator) == 1) {
				ext_decimator = 1;
				new_fifo_decimator[ST_MASK_ID_EXT0] = min_decimator;
			}
			min_decimator = 1;
		}
		if ((accel_decimator > 4) && (accel_decimator < 8)) {
			new_fifo_decimator[ST_MASK_ID_ACCEL] = accel_decimator - 3;
			accel_decimator = 4;
		} else if ((accel_decimator > 8) && (accel_decimator < 16)) {
			new_fifo_decimator[ST_MASK_ID_ACCEL] = accel_decimator - 7;
			accel_decimator = 8;
		} else if (accel_decimator > 16 && accel_decimator < 32) {
			new_fifo_decimator[ST_MASK_ID_ACCEL] = accel_decimator - 15;
			accel_decimator = 16;
		} else if (accel_decimator > 32) {
			new_fifo_decimator[ST_MASK_ID_ACCEL] = accel_decimator / 32;
			accel_decimator = 32;
		}
		if ((gyro_decimator > 4) && (gyro_decimator < 8)) {
			new_fifo_decimator[ST_MASK_ID_GYRO] = gyro_decimator - 3;
			gyro_decimator = 4;
		} else if ((gyro_decimator > 8) && (gyro_decimator < 16)) {
			new_fifo_decimator[ST_MASK_ID_GYRO] = gyro_decimator - 7;
			gyro_decimator = 8;
		} else if (gyro_decimator > 16 && gyro_decimator < 32) {
			new_fifo_decimator[ST_MASK_ID_GYRO] = gyro_decimator - 15;
			gyro_decimator = 16;
		} else if (gyro_decimator > 32) {
			new_fifo_decimator[ST_MASK_ID_GYRO] = gyro_decimator / 32;
			gyro_decimator = 32;
		}
#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
		if ((ext_decimator > 4) && (ext_decimator < 8)) {
			new_fifo_decimator[ST_MASK_ID_EXT0] = ext_decimator - 3;
			ext_decimator = 4;
		} else if ((ext_decimator > 8) && (ext_decimator < 16)) {
			new_fifo_decimator[ST_MASK_ID_EXT0] = ext_decimator - 7;
			ext_decimator = 8;
		} else if (ext_decimator > 16 && ext_decimator < 32) {
			new_fifo_decimator[ST_MASK_ID_EXT0] = ext_decimator - 15;
			ext_decimator = 16;
		} else if (ext_decimator > 32) {
			new_fifo_decimator[ST_MASK_ID_EXT0] = ext_decimator / 32;
			ext_decimator = 32;
		}
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */
		max_decimator = MAX(MAX(accel_decimator, gyro_decimator), ext_decimator);
	}

	decimators[0] = accel_decimator;
	if (accel_decimator > 0) {
		new_deltatime[ST_MASK_ID_ACCEL] = accel_decimator *
						(1000000000U / trigger_odr);
		samples_in_pattern[0] = max_decimator / accel_decimator;
	} else
		samples_in_pattern[0] = 0;

	decimators[1] = gyro_decimator;
	if (gyro_decimator > 0) {
		new_deltatime[ST_MASK_ID_GYRO] = gyro_decimator *
						(1000000000U / trigger_odr);
		samples_in_pattern[1] = max_decimator / gyro_decimator;
	} else
		samples_in_pattern[1] = 0;

#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
	decimators[2] = ext_decimator;
	if (ext_decimator > 0) {
		new_deltatime[ST_MASK_ID_EXT0] = ext_decimator *
						(1000000000U / trigger_odr);
		samples_in_pattern[2] = max_decimator / ext_decimator;
	} else
		samples_in_pattern[2] = 0;
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */

#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
	if ((accel_decimator == cdata->hwfifo_decimator[ST_MASK_ID_ACCEL]) &&
	    (ext_decimator == cdata->hwfifo_decimator[ST_MASK_ID_EXT0]) &&
	    (gyro_decimator == cdata->hwfifo_decimator[ST_MASK_ID_GYRO])) {
		if (cdata->fifo_output[ST_MASK_ID_EXT0].decimator != new_fifo_decimator[ST_MASK_ID_EXT0]) {
			return true;
		}
#else /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */
	if ((accel_decimator == cdata->hwfifo_decimator[ST_MASK_ID_ACCEL]) &&
	    (gyro_decimator == cdata->hwfifo_decimator[ST_MASK_ID_GYRO])) {
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */
		return false;
	}

	return true;
}

static int st_lsm6dsm_of_get_drdy_pin(struct lsm6dsm_data *cdata,
				      int *drdy_pin)
{
	struct device_node *np = cdata->dev->of_node;

	if (!np)
		return -EINVAL;

	return of_property_read_u32(np, "st,drdy-int-pin", drdy_pin);
}

static int st_lsm6dsm_get_drdy_reg(struct lsm6dsm_data *cdata, u8 *drdy_reg)
{
	int err = 0, drdy_pin;

	if (st_lsm6dsm_of_get_drdy_pin(cdata, &drdy_pin) < 0) {
		struct st_sensors_platform_data *pdata;
		struct device *dev = cdata->dev;

		pdata = (struct st_sensors_platform_data *)dev->platform_data;
		drdy_pin = pdata ? pdata->drdy_int_pin : 1;
	}

	switch (drdy_pin) {
	case 1:
		*drdy_reg = ST_LSM6DSM_INT1_ADDR;
		break;
	case 2:
		*drdy_reg = ST_LSM6DSM_INT2_ADDR;
		break;
	default:
		dev_err(cdata->dev, "unsupported data ready pin\n");
		err = -EINVAL;
		break;
	}

	return err;
}

int st_lsm6dsm_set_drdy_irq(struct lsm6dsm_sensor_data *sdata, bool state)
{
	int err;
	u16 *irq_mask = NULL;
	u8 reg_addr, mask = 0, value;
	u16 tmp_irq_enable_fifo_mask, tmp_irq_enable_accel_ext_mask;

	if (state)
		value = ST_LSM6DSM_EN_BIT;
	else
		value = ST_LSM6DSM_DIS_BIT;

	tmp_irq_enable_fifo_mask =
			sdata->cdata->irq_enable_fifo_mask & ~sdata->sindex;
	tmp_irq_enable_accel_ext_mask =
			sdata->cdata->irq_enable_accel_ext_mask & ~sdata->sindex;

	switch (sdata->sindex) {
	case ST_MASK_ID_ACCEL:
		reg_addr = sdata->cdata->drdy_reg;

		if (sdata->cdata->hwfifo_enabled[ST_MASK_ID_ACCEL]) {
			if (tmp_irq_enable_fifo_mask == 0)
				mask = ST_LSM6DSM_FIFO_THR_IRQ_MASK;

			irq_mask = &sdata->cdata->irq_enable_fifo_mask;
		} else {
			if (tmp_irq_enable_accel_ext_mask == 0)
				mask = ST_LSM6DSM_ACCEL_DRDY_IRQ_MASK;

			irq_mask = &sdata->cdata->irq_enable_accel_ext_mask;
		}

		break;
	case ST_MASK_ID_GYRO:
		reg_addr = sdata->cdata->drdy_reg;

		if (sdata->cdata->hwfifo_enabled[ST_MASK_ID_GYRO]) {
			if (tmp_irq_enable_fifo_mask == 0)
				mask = ST_LSM6DSM_FIFO_THR_IRQ_MASK;

			irq_mask = &sdata->cdata->irq_enable_fifo_mask;
		} else
			mask = ST_LSM6DSM_GYRO_DRDY_IRQ_MASK;

		break;
	case ST_MASK_ID_SIGN_MOTION:
		reg_addr = ST_LSM6DSM_INT1_ADDR;
		mask = ST_LSM6DSM_SIGN_MOTION_DRDY_IRQ_MASK;
		break;
	case ST_MASK_ID_STEP_COUNTER:
		reg_addr = ST_LSM6DSM_INT2_ADDR;
		mask = ST_LSM6DSM_STEP_COUNTER_DRDY_IRQ_MASK;
		break;
	case ST_MASK_ID_STEP_DETECTOR:
		reg_addr = ST_LSM6DSM_INT1_ADDR;
		mask = ST_LSM6DSM_STEP_DETECTOR_DRDY_IRQ_MASK;
		break;
	case ST_MASK_ID_TILT:
		reg_addr = ST_LSM6DSM_MD1_ADDR;
		mask = ST_LSM6DSM_TILT_DRDY_IRQ_MASK;
		break;
	case ST_MASK_ID_WTILT:
		reg_addr = ST_LSM6DSM_DRDY_PULSE_CFG_G;
		mask = ST_LSM6DSM_WTILT_DRDY_IRQ_MASK;
		break;
	case ST_MASK_ID_TAP:
		reg_addr = ST_LSM6DSM_MD1_ADDR;
		mask = ST_LSM6DSM_STAP_DRDY_IRQ_MASK;
		break;
	case ST_MASK_ID_TAP_TAP:
		reg_addr = ST_LSM6DSM_MD1_ADDR;
		mask = ST_LSM6DSM_DTAP_DRDY_IRQ_MASK;
		break;
#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
	case ST_MASK_ID_EXT0:
		reg_addr = sdata->cdata->drdy_reg;

		if (sdata->cdata->hwfifo_enabled[ST_MASK_ID_EXT0]) {
			if (tmp_irq_enable_fifo_mask == 0)
				mask = ST_LSM6DSM_FIFO_THR_IRQ_MASK;

			irq_mask = &sdata->cdata->irq_enable_fifo_mask;
		} else {
			if (tmp_irq_enable_accel_ext_mask == 0)
				mask = ST_LSM6DSM_ACCEL_DRDY_IRQ_MASK;

			irq_mask = &sdata->cdata->irq_enable_accel_ext_mask;
		}

		break;
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */
	default:
		return -EINVAL;
	}

	if (mask > 0) {
		err =  st_lsm6dsm_write_data_with_mask(sdata->cdata,
						reg_addr, mask, value, true);
		if (err < 0)
			return err;
	}

	if (irq_mask != NULL) {
		if (state)
			*irq_mask |= BIT(sdata->sindex);
		else
			*irq_mask &= ~BIT(sdata->sindex);
	}

	return 0;
}
EXPORT_SYMBOL(st_lsm6dsm_set_drdy_irq);

static int st_lsm6dsm_set_odr(struct lsm6dsm_sensor_data *sdata,
						unsigned int odr, bool force)
{
	u8 reg_value;
	int err, i = 0, n;
	int64_t temp_last_timestamp[3] = { 0 };
	bool scan_odr = true, fifo_conf_changed;
	unsigned int temp_v_odr[ST_INDIO_DEV_NUM + 1];
	unsigned int temp_hw_odr[ST_INDIO_DEV_NUM + 1];
	int64_t new_deltatime[ST_INDIO_DEV_NUM + 1] = { 0 };
	short new_fifo_decimator[ST_INDIO_DEV_NUM + 1] = { 0 };
	u8 fifo_decimator[3] = { 0 }, samples_in_pattern[3] = { 0 };
	u8 temp_num_samples[3] = { 0 }, temp_old_decimator[3] = { 1 };

	if (odr == 0) {
		if (force)
			scan_odr = false;
		else
			return -EINVAL;
	}

	if (scan_odr) {
		for (i = 0; i < ST_LSM6DSM_ODR_LIST_NUM; i++) {
			if (st_lsm6dsm_odr_table.odr_avl[i].hz == odr)
				break;
		}
		if (i == ST_LSM6DSM_ODR_LIST_NUM)
			return -EINVAL;

		if (!force) {
			if ((sdata->cdata->sensors_enabled & BIT(sdata->sindex)) == 0) {
				sdata->cdata->v_odr[sdata->sindex] = st_lsm6dsm_odr_table.odr_avl[i].hz;
				return 0;
			}
		}

		if (sdata->cdata->hw_odr[sdata->sindex] == st_lsm6dsm_odr_table.odr_avl[i].hz)
			reg_value = 0xff;
		else
			reg_value = st_lsm6dsm_odr_table.odr_avl[i].value;
	} else
		reg_value = ST_LSM6DSM_ODR_POWER_OFF_VAL;

	if (sdata->cdata->sensors_use_fifo > 0) {
		/* someone is using fifo */
		temp_v_odr[ST_MASK_ID_ACCEL] = sdata->cdata->v_odr[ST_MASK_ID_ACCEL];
		temp_v_odr[ST_MASK_ID_GYRO] = sdata->cdata->v_odr[ST_MASK_ID_GYRO];

		if (sdata->sindex == ST_MASK_ID_ACCEL) {
			if (force)
				temp_v_odr[ST_MASK_ID_ACCEL] = sdata->cdata->accel_odr_dependency[0];

			temp_hw_odr[ST_MASK_ID_ACCEL] = odr;
			temp_hw_odr[ST_MASK_ID_GYRO] = sdata->cdata->hw_odr[ST_MASK_ID_GYRO];
		} else {
			if (!force)
				temp_v_odr[ST_MASK_ID_GYRO] = odr;

			temp_hw_odr[ST_MASK_ID_GYRO] = odr;
			temp_hw_odr[ST_MASK_ID_ACCEL] = sdata->cdata->hw_odr[ST_MASK_ID_ACCEL];
		}
#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
		temp_v_odr[ST_MASK_ID_EXT0] = sdata->cdata->v_odr[ST_MASK_ID_EXT0];
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */

		fifo_conf_changed = lsm6dsm_calculate_fifo_decimators(sdata->cdata,
				fifo_decimator, samples_in_pattern, temp_v_odr,
				temp_hw_odr, new_deltatime, new_fifo_decimator);
		if (fifo_conf_changed) {
			/* FIFO configuration changed, needs to write new decimators */
			disable_irq(sdata->cdata->irq);

			if (sdata->cdata->fifo_status != BYPASS) {
				st_lsm6dsm_read_fifo(sdata->cdata, true);

				temp_num_samples[0] = sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].sip;
				temp_num_samples[1] = sdata->cdata->fifo_output[ST_MASK_ID_GYRO].sip;
				temp_last_timestamp[0] = sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp_p;
				temp_last_timestamp[1] = sdata->cdata->fifo_output[ST_MASK_ID_GYRO].timestamp_p;
				temp_old_decimator[0] = sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].decimator;
				temp_old_decimator[1] = sdata->cdata->fifo_output[ST_MASK_ID_GYRO].decimator;

#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
				temp_num_samples[2] = sdata->cdata->fifo_output[ST_MASK_ID_EXT0].sip;
				temp_last_timestamp[2] = sdata->cdata->fifo_output[ST_MASK_ID_EXT0].timestamp_p;
				temp_old_decimator[2] = sdata->cdata->fifo_output[ST_MASK_ID_EXT0].decimator;
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */

				err = st_lsm6dsm_set_fifo_mode(sdata->cdata, BYPASS);
				if (err < 0)
					goto reenable_fifo_irq;
			} else {
				temp_num_samples[0] = 0;
				temp_num_samples[1] = 0;
#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
				temp_num_samples[2] = 0;
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */
			}

			err = lsm6dsm_write_decimators(sdata->cdata, fifo_decimator);
			if (err < 0)
				goto reenable_fifo_irq;

			if (reg_value != 0xff) {
				err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
						st_lsm6dsm_odr_table.addr[sdata->sindex],
						st_lsm6dsm_odr_table.mask[sdata->sindex],
						reg_value, true);
				if (err < 0)
					goto reenable_fifo_irq;

				if (sdata->sindex == ST_MASK_ID_ACCEL) {
					switch (temp_hw_odr[ST_MASK_ID_ACCEL]) {
					case 13:
					case 26:
					case 52:
						if (temp_num_samples[0] == 0)
							sdata->cdata->samples_to_discard[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_52HZ;
						else
							sdata->cdata->samples_to_discard_2[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_52HZ;
						break;
					case 104:
						if (temp_num_samples[0] == 0)
							sdata->cdata->samples_to_discard[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_104HZ;
						else
							sdata->cdata->samples_to_discard_2[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_104HZ;
						break;
					default:
						if (temp_num_samples[0] == 0)
							sdata->cdata->samples_to_discard[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_208HZ;
						else
							sdata->cdata->samples_to_discard_2[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_208HZ;
						break;
					}
				}

				switch (temp_hw_odr[ST_MASK_ID_GYRO]) {
				case 13:
					if (temp_num_samples[1] == 0)
						sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_13HZ;
					else
						sdata->cdata->samples_to_discard_2[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_13HZ;
					break;
				case 26:
				case 52:
					if (temp_num_samples[1] == 0)
						sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_52HZ;
					else
						sdata->cdata->samples_to_discard_2[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_52HZ;
					break;
				case 104:
					if (temp_num_samples[1] == 0)
						sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_104HZ;
					else
						sdata->cdata->samples_to_discard_2[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_104HZ;
					break;
				default:
					if (temp_num_samples[1] == 0)
						sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_208HZ;
					else
						sdata->cdata->samples_to_discard_2[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_208HZ;
					break;
				}
			}

			sdata->cdata->hwfifo_decimator[ST_MASK_ID_ACCEL] = fifo_decimator[0];
			sdata->cdata->hwfifo_decimator[ST_MASK_ID_GYRO] = fifo_decimator[1];

			sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].decimator = new_fifo_decimator[ST_MASK_ID_ACCEL];
			sdata->cdata->fifo_output[ST_MASK_ID_GYRO].decimator = new_fifo_decimator[ST_MASK_ID_GYRO];

			sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].num_samples = new_fifo_decimator[ST_MASK_ID_ACCEL] - 1;
			sdata->cdata->fifo_output[ST_MASK_ID_GYRO].num_samples = new_fifo_decimator[ST_MASK_ID_GYRO] - 1;

			sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].sip = samples_in_pattern[0];
			sdata->cdata->fifo_output[ST_MASK_ID_GYRO].sip = samples_in_pattern[1];

			sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime_default = new_deltatime[ST_MASK_ID_ACCEL];
			sdata->cdata->fifo_output[ST_MASK_ID_GYRO].deltatime_default = new_deltatime[ST_MASK_ID_GYRO];

#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
			sdata->cdata->hwfifo_decimator[ST_MASK_ID_EXT0] = fifo_decimator[2];
			sdata->cdata->fifo_output[ST_MASK_ID_EXT0].decimator = new_fifo_decimator[ST_MASK_ID_EXT0];
			sdata->cdata->fifo_output[ST_MASK_ID_EXT0].num_samples = new_fifo_decimator[ST_MASK_ID_EXT0] - 1;
			sdata->cdata->fifo_output[ST_MASK_ID_EXT0].sip = samples_in_pattern[2];
			sdata->cdata->fifo_output[ST_MASK_ID_EXT0].deltatime_default = new_deltatime[ST_MASK_ID_EXT0];
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */

			err = lsm6dsm_set_watermark(sdata->cdata);
			if (err < 0)
				goto reenable_fifo_irq;

			if ((samples_in_pattern[0] > 0) || (samples_in_pattern[1] > 0) || (samples_in_pattern[2] > 0)) {
				err = st_lsm6dsm_set_fifo_mode(sdata->cdata, CONTINUOS);
				if (err < 0)
					goto reenable_fifo_irq;

				if (((temp_num_samples[0] > 0) && (samples_in_pattern[0] > 0)) && (sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].initialized)) {
					unsigned int n_gen;
					int64_t temp_deltatime = 0;

					if (sdata->cdata->fifo_enable_timestamp > temp_last_timestamp[0]) {
						n_gen = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[0],
							sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime * temp_old_decimator[0]);

						if (n_gen > 0)
							temp_deltatime = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[0], n_gen);

						for (n = 0; n < n_gen; n++) {
							temp_last_timestamp[0] += temp_deltatime;
							err = st_lsm6dsm_push_data_with_timestamp(sdata->cdata, ST_MASK_ID_ACCEL,
								sdata->cdata->accel_last_push, temp_last_timestamp[0]);
							if (err < 0)
								break;

							sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp_p = temp_last_timestamp[0];
						}
					}
				} else
					sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime = new_deltatime[ST_MASK_ID_ACCEL];

				if (((temp_num_samples[1] > 0) && (samples_in_pattern[1] > 0)) && (sdata->cdata->fifo_output[ST_MASK_ID_GYRO].initialized)) {
					unsigned int n_gen;
					int64_t temp_deltatime = 0;

					if (sdata->cdata->fifo_enable_timestamp > temp_last_timestamp[1]) {
						n_gen = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[1],
							sdata->cdata->fifo_output[ST_MASK_ID_GYRO].deltatime * temp_old_decimator[1]);

						if (n_gen > 0)
							temp_deltatime = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[1], n_gen);

						for (n = 0; n < n_gen; n++) {
							temp_last_timestamp[1] += temp_deltatime;
							err = st_lsm6dsm_push_data_with_timestamp(sdata->cdata, ST_MASK_ID_GYRO,
								sdata->cdata->gyro_last_push, temp_last_timestamp[1]);
							if (err < 0)
								break;

							sdata->cdata->fifo_output[ST_MASK_ID_GYRO].timestamp_p = temp_last_timestamp[1];
						}
					}
				} else
					sdata->cdata->fifo_output[ST_MASK_ID_GYRO].deltatime = new_deltatime[ST_MASK_ID_GYRO];

#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
				if (((temp_num_samples[2] > 0) && (samples_in_pattern[2] > 0)) && (sdata->cdata->fifo_output[ST_MASK_ID_EXT0].initialized)) {
					unsigned int n_gen;
					int64_t temp_deltatime = 0;

					if (sdata->cdata->fifo_enable_timestamp > temp_last_timestamp[2]) {
						n_gen = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[2],
							sdata->cdata->fifo_output[ST_MASK_ID_EXT0].deltatime * temp_old_decimator[2]);

						if (n_gen > 0)
							temp_deltatime = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[2], n_gen);

						for (n = 0; n < n_gen; n++) {
							temp_last_timestamp[2] += temp_deltatime;
							err = st_lsm6dsm_push_data_with_timestamp(sdata->cdata, ST_MASK_ID_EXT0,
								sdata->cdata->ext0_last_push, temp_last_timestamp[2]);
							if (err < 0)
								break;

							sdata->cdata->fifo_output[ST_MASK_ID_EXT0].timestamp_p = temp_last_timestamp[2];
						}
					}
				} else
					sdata->cdata->fifo_output[ST_MASK_ID_EXT0].deltatime = new_deltatime[ST_MASK_ID_EXT0];
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */
			}

			enable_irq(sdata->cdata->irq);
		} else {
			/* FIFO configuration not changed */

			if (reg_value == 0xff) {
				if (temp_v_odr[sdata->sindex] != 0)
					sdata->cdata->v_odr[sdata->sindex] = temp_v_odr[sdata->sindex];

				sdata->cdata->hw_odr[sdata->sindex] = temp_hw_odr[sdata->sindex];
				return 0;
			}

			disable_irq(sdata->cdata->irq);

			if (sdata->cdata->fifo_status != BYPASS) {
				st_lsm6dsm_read_fifo(sdata->cdata, true);

				temp_num_samples[0] = sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].sip;
				temp_num_samples[1] = sdata->cdata->fifo_output[ST_MASK_ID_GYRO].sip;
				temp_last_timestamp[0] = sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp_p;
				temp_last_timestamp[1] = sdata->cdata->fifo_output[ST_MASK_ID_GYRO].timestamp_p;
				temp_old_decimator[0] = sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].decimator;
				temp_old_decimator[1] = sdata->cdata->fifo_output[ST_MASK_ID_GYRO].decimator;

#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
				temp_num_samples[2] = sdata->cdata->fifo_output[ST_MASK_ID_EXT0].sip;
				temp_last_timestamp[2] = sdata->cdata->fifo_output[ST_MASK_ID_EXT0].timestamp_p;
				temp_old_decimator[2] = sdata->cdata->fifo_output[ST_MASK_ID_EXT0].decimator;
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */

				err = st_lsm6dsm_set_fifo_mode(sdata->cdata, BYPASS);
				if (err < 0)
					goto reenable_fifo_irq;
			} else {
				temp_num_samples[0] = 0;
				temp_num_samples[1] = 0;
#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
				temp_num_samples[2] = 0;
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */
			}

			err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
					st_lsm6dsm_odr_table.addr[sdata->sindex],
					st_lsm6dsm_odr_table.mask[sdata->sindex],
					reg_value, true);
			if (err < 0)
				goto reenable_fifo_irq;

			if (sdata->sindex == ST_MASK_ID_ACCEL) {
				switch (temp_hw_odr[ST_MASK_ID_ACCEL]) {
				case 13:
				case 26:
				case 52:
					if (temp_num_samples[0] == 0)
						sdata->cdata->samples_to_discard[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_52HZ;
					else
						sdata->cdata->samples_to_discard_2[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_52HZ;
					break;
				case 104:
					if (temp_num_samples[0] == 0)
						sdata->cdata->samples_to_discard[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_104HZ;
					else
						sdata->cdata->samples_to_discard_2[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_104HZ;
					break;
				default:
					if (temp_num_samples[0] == 0)
						sdata->cdata->samples_to_discard[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_208HZ;
					else
						sdata->cdata->samples_to_discard_2[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_208HZ;
					break;
				}
			}

			switch (temp_hw_odr[ST_MASK_ID_GYRO]) {
			case 13:
				if (temp_num_samples[1] == 0)
					sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_13HZ;
				else
					sdata->cdata->samples_to_discard_2[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_13HZ;
				break;
			case 26:
			case 52:
				if (temp_num_samples[1] == 0)
					sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_52HZ;
				else
					sdata->cdata->samples_to_discard_2[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_52HZ;
				break;
			case 104:
				if (temp_num_samples[1] == 0)
					sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_104HZ;
				else
					sdata->cdata->samples_to_discard_2[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_104HZ;
				break;
			default:
				if (temp_num_samples[1] == 0)
					sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_208HZ;
				else
					sdata->cdata->samples_to_discard_2[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_208HZ;
				break;
			}

			if ((sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].sip > 0) ||
					(sdata->cdata->fifo_output[ST_MASK_ID_GYRO].sip > 0) ||
						(sdata->cdata->fifo_output[ST_MASK_ID_EXT0].sip > 0)) {
				err = st_lsm6dsm_set_fifo_mode(sdata->cdata, CONTINUOS);
				if (err < 0)
					goto reenable_fifo_irq;

				if (((temp_num_samples[0] > 0) && (samples_in_pattern[0] > 0)) && (sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].initialized)) {
					unsigned int n_gen;
					int64_t temp_deltatime = 0;

					if (sdata->cdata->fifo_enable_timestamp > temp_last_timestamp[0]) {
						n_gen = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[0],
							sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime * temp_old_decimator[0]);

						if (n_gen > 0)
							temp_deltatime = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[0], n_gen);

						for (n = 0; n < n_gen; n++) {
							temp_last_timestamp[0] += temp_deltatime;
							err = st_lsm6dsm_push_data_with_timestamp(sdata->cdata, ST_MASK_ID_ACCEL,
								sdata->cdata->accel_last_push, temp_last_timestamp[0]);
							if (err < 0)
								break;

							sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp_p = temp_last_timestamp[0];
						}
					}
				} else
					sdata->cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime = new_deltatime[ST_MASK_ID_ACCEL];

				if (((temp_num_samples[1] > 0) && (samples_in_pattern[1] > 0)) && (sdata->cdata->fifo_output[ST_MASK_ID_GYRO].initialized)) {
					unsigned int n_gen;
					int64_t temp_deltatime = 0;

					if (sdata->cdata->fifo_enable_timestamp > temp_last_timestamp[1]) {
						n_gen = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[1],
							sdata->cdata->fifo_output[ST_MASK_ID_GYRO].deltatime * temp_old_decimator[1]);

						if (n_gen > 0)
							temp_deltatime = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[1], n_gen);

						for (n = 0; n < n_gen; n++) {
							temp_last_timestamp[1] += temp_deltatime;
							err = st_lsm6dsm_push_data_with_timestamp(sdata->cdata, ST_MASK_ID_GYRO,
								sdata->cdata->gyro_last_push, temp_last_timestamp[1]);
							if (err < 0)
								break;

							sdata->cdata->fifo_output[ST_MASK_ID_GYRO].timestamp_p = temp_last_timestamp[1];
						}
					}
				} else
					sdata->cdata->fifo_output[ST_MASK_ID_GYRO].deltatime = new_deltatime[ST_MASK_ID_GYRO];

#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
				if (((temp_num_samples[2] > 0) && (samples_in_pattern[2] > 0)) && (sdata->cdata->fifo_output[ST_MASK_ID_EXT0].initialized)) {
					unsigned int n_gen;
					int64_t temp_deltatime = 0;

					if (sdata->cdata->fifo_enable_timestamp > temp_last_timestamp[2]) {
						n_gen = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[2],
							sdata->cdata->fifo_output[ST_MASK_ID_EXT0].deltatime * temp_old_decimator[2]);

						if (n_gen > 0)
							temp_deltatime = div64_s64(sdata->cdata->fifo_enable_timestamp - temp_last_timestamp[2], n_gen);

						for (n = 0; n < n_gen; n++) {
							temp_last_timestamp[2] += temp_deltatime;
							err = st_lsm6dsm_push_data_with_timestamp(sdata->cdata, ST_MASK_ID_EXT0,
								sdata->cdata->ext0_last_push, temp_last_timestamp[2]);
							if (err < 0)
								break;

							sdata->cdata->fifo_output[ST_MASK_ID_EXT0].timestamp_p = temp_last_timestamp[2];
						}
					}
				} else
					sdata->cdata->fifo_output[ST_MASK_ID_EXT0].deltatime = new_deltatime[ST_MASK_ID_EXT0];
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */
			}

			enable_irq(sdata->cdata->irq);
		}

		if (temp_v_odr[sdata->sindex] != 0)
			sdata->cdata->v_odr[sdata->sindex] = temp_v_odr[sdata->sindex];

		sdata->cdata->hw_odr[sdata->sindex] = temp_hw_odr[sdata->sindex];
	} else {
		/* no one is using FIFO */

		disable_irq(sdata->cdata->irq);

		if ((odr != 0) && (sdata->cdata->hw_odr[sdata->sindex] == st_lsm6dsm_odr_table.odr_avl[i].hz)) {
			if (sdata->sindex == ST_MASK_ID_ACCEL) {
				sdata->cdata->nofifo_decimation[ST_MASK_ID_ACCEL].decimator =
					sdata->cdata->hw_odr[ST_MASK_ID_ACCEL] / sdata->cdata->v_odr[ST_MASK_ID_ACCEL];
				sdata->cdata->nofifo_decimation[ST_MASK_ID_ACCEL].num_samples =
					sdata->cdata->nofifo_decimation[ST_MASK_ID_ACCEL].decimator - 1;
#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
				sdata->cdata->nofifo_decimation[ST_MASK_ID_EXT0].decimator =
					sdata->cdata->hw_odr[ST_MASK_ID_ACCEL] / sdata->cdata->v_odr[ST_MASK_ID_EXT0];
				sdata->cdata->nofifo_decimation[ST_MASK_ID_EXT0].num_samples =
					sdata->cdata->nofifo_decimation[ST_MASK_ID_EXT0].decimator - 1;
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */
			}

			enable_irq(sdata->cdata->irq);

			return 0;
		}

		err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
				st_lsm6dsm_odr_table.addr[sdata->sindex],
				st_lsm6dsm_odr_table.mask[sdata->sindex],
				reg_value, true);
		if (err < 0) {
			enable_irq(sdata->cdata->irq);
			return err;
		}

		if (!force)
			sdata->cdata->v_odr[sdata->sindex] = st_lsm6dsm_odr_table.odr_avl[i].hz;

		if (odr == 0)
			sdata->cdata->hw_odr[sdata->sindex] = 0;
		else
			sdata->cdata->hw_odr[sdata->sindex] = st_lsm6dsm_odr_table.odr_avl[i].hz;

		if (sdata->sindex == ST_MASK_ID_ACCEL) {
			switch (sdata->cdata->hw_odr[sdata->sindex]) {
			case 13:
			case 26:
			case 52:
				sdata->cdata->samples_to_discard[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_52HZ;
				break;
			case 104:
				sdata->cdata->samples_to_discard[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_104HZ;
				break;
			default:
				sdata->cdata->samples_to_discard[ST_MASK_ID_ACCEL] = ST_LSM6DSM_ACCEL_STD_208HZ;
				break;
			}
		}

		switch (sdata->cdata->hw_odr[ST_MASK_ID_GYRO]) {
		case 13:
			sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_13HZ;
			break;
		case 26:
		case 52:
			sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_52HZ;
			break;
		case 104:
			sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_104HZ;
			break;
		default:
			sdata->cdata->samples_to_discard[ST_MASK_ID_GYRO] = ST_LSM6DSM_GYRO_STD_208HZ;
			break;
		}

		if (sdata->sindex == ST_MASK_ID_ACCEL) {
			if (sdata->cdata->hw_odr[sdata->sindex] > 0) {
				sdata->cdata->nofifo_decimation[ST_MASK_ID_ACCEL].decimator =
					sdata->cdata->hw_odr[ST_MASK_ID_ACCEL] / sdata->cdata->v_odr[ST_MASK_ID_ACCEL];
#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
				sdata->cdata->nofifo_decimation[ST_MASK_ID_EXT0].decimator =
					sdata->cdata->hw_odr[ST_MASK_ID_ACCEL] / sdata->cdata->v_odr[ST_MASK_ID_EXT0];
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */
			} else {
				sdata->cdata->nofifo_decimation[ST_MASK_ID_ACCEL].decimator = 1;
				sdata->cdata->nofifo_decimation[ST_MASK_ID_EXT0].decimator = 1;
			}

			sdata->cdata->nofifo_decimation[ST_MASK_ID_ACCEL].num_samples =
				sdata->cdata->nofifo_decimation[ST_MASK_ID_ACCEL].decimator - 1;
#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
			sdata->cdata->nofifo_decimation[ST_MASK_ID_EXT0].num_samples =
				sdata->cdata->nofifo_decimation[ST_MASK_ID_EXT0].decimator - 1;
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */
		}

		enable_irq(sdata->cdata->irq);
	}

	sdata->cdata->trigger_odr = sdata->cdata->hw_odr[0] > sdata->cdata->hw_odr[1] ? sdata->cdata->hw_odr[0] : sdata->cdata->hw_odr[1];

	return 0;

reenable_fifo_irq:
	enable_irq(sdata->cdata->irq);
	return err;
}

/*
 * Enable / disable accelerometer
 */
static int lsm6dsm_enable_accel(struct lsm6dsm_data *cdata, enum st_mask_id id, int min_odr)
{
	int odr;
	struct lsm6dsm_sensor_data *sdata_accel = iio_priv(cdata->indio_dev[ST_MASK_ID_ACCEL]);

	switch (id) {
	case ST_MASK_ID_ACCEL:
		cdata->accel_odr_dependency[0] = min_odr;
		if (min_odr > 0)
			cdata->accel_on = true;
		else
			cdata->accel_on = false;

		break;
	case ST_MASK_ID_SENSOR_HUB:
		cdata->accel_odr_dependency[1] = min_odr;
		if (min_odr > 0)
			cdata->magn_on = true;
		else
			cdata->magn_on = false;

		break;
	case ST_MASK_ID_DIGITAL_FUNC:
		cdata->accel_odr_dependency[2] = min_odr;
		break;
	default:
		return -EINVAL;
	}

	if (cdata->accel_odr_dependency[0] > cdata->accel_odr_dependency[1])
		odr = cdata->accel_odr_dependency[0];
	else
		odr = cdata->accel_odr_dependency[1];

	if (cdata->accel_odr_dependency[2] > odr)
		odr = cdata->accel_odr_dependency[2];

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
	if (cdata->injection_mode)
		return 0;
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

	return st_lsm6dsm_set_odr(sdata_accel, odr, true);
}

/*
 * Enable / disable digital func
 */
static int lsm6dsm_enable_digital_func(struct lsm6dsm_data *cdata,
					bool enable, enum st_mask_id id)
{
	int err;

	if (enable) {
		if (cdata->enable_digfunc_mask == 0) {
			err = lsm6dsm_enable_accel(cdata,
						ST_MASK_ID_DIGITAL_FUNC, 26);
			if (err < 0)
				return err;

			err = st_lsm6dsm_write_data_with_mask(cdata,
						ST_LSM6DSM_FUNC_EN_ADDR,
						ST_LSM6DSM_FUNC_EN_MASK,
						ST_LSM6DSM_EN_BIT, true);
			if (err < 0)
				return err;
		}
		cdata->enable_digfunc_mask |= BIT(id);
	} else {
		if ((cdata->enable_digfunc_mask & ~BIT(id)) == 0) {
			err = st_lsm6dsm_write_data_with_mask(cdata,
						ST_LSM6DSM_FUNC_EN_ADDR,
						ST_LSM6DSM_FUNC_EN_MASK,
						ST_LSM6DSM_DIS_BIT, true);
			if (err < 0)
				return err;

			err = lsm6dsm_enable_accel(cdata,
						ST_MASK_ID_DIGITAL_FUNC, 0);
			if (err < 0)
				return err;
		}
		cdata->enable_digfunc_mask &= ~BIT(id);

	}

	return 0;
}

/*
 * Enable / disable HW pedometer
 */
static int lsm6dsm_enable_pedometer(struct lsm6dsm_data *cdata,
					bool enable, enum st_mask_id id)
{
	int err;

	if (enable) {
		if (cdata->enable_pedometer_mask == 0) {
			err =  st_lsm6dsm_write_data_with_mask(cdata,
						ST_LSM6DSM_PEDOMETER_EN_ADDR,
						ST_LSM6DSM_PEDOMETER_EN_MASK,
						ST_LSM6DSM_EN_BIT, true);
			if (err < 0)
				return err;

			err = lsm6dsm_enable_digital_func(cdata,
						true, ST_MASK_ID_HW_PEDOMETER);
			if (err < 0)
				return err;
		}
		cdata->enable_pedometer_mask |= BIT(id);
	} else {
		if ((cdata->enable_pedometer_mask & ~BIT(id)) == 0) {
			err =  st_lsm6dsm_write_data_with_mask(cdata,
						ST_LSM6DSM_PEDOMETER_EN_ADDR,
						ST_LSM6DSM_PEDOMETER_EN_MASK,
						ST_LSM6DSM_DIS_BIT, true);
			if (err < 0)
				return err;

			err = lsm6dsm_enable_digital_func(cdata,
						false, ST_MASK_ID_HW_PEDOMETER);
			if (err < 0)
				return err;
		}
		cdata->enable_pedometer_mask &= ~BIT(id);
	}

	return 0;
}

static int lsm6dsm_enable_tap(struct lsm6dsm_sensor_data *sdata,
                    bool enable)
{
	int err;
	struct lsm6dsm_data *cdata = sdata->cdata;
	bool stap_en = (cdata->sensors_enabled & BIT(ST_MASK_ID_TAP));
	bool dtap_en = (cdata->sensors_enabled & BIT(ST_MASK_ID_TAP_TAP));

	if (enable) {
		if (!stap_en && !dtap_en) {
			err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
					ST_LSM6DSM_LIR_ADDR,
					ST_LSM6DSM_INT_ENABLE_MASK,
					ST_LSM6DSM_EN_BIT, true);
			if (err < 0)
				return err;

                        err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
                                        ST_LSM6DSM_LIR_ADDR,
                                        ST_LSM6DSM_LIR_MASK,
                                        ST_LSM6DSM_DIS_BIT, true);
                        if (err < 0)
                                return err;

			err = st_lsm6dsm_write_data_with_mask(cdata,
				ST_LSM6DSM_TAP_EN_ADDR, ST_LSM6DSM_TAP_EN_MASK,
				0x7, true);
			if (err < 0)
				return err;

			err = lsm6dsm_enable_accel(cdata,
					ST_MASK_ID_DIGITAL_FUNC, 416);
			if (err < 0)
				return err;
		}
	} else {
		if (((sdata->sindex == ST_MASK_ID_TAP) && dtap_en) ||
		    ((sdata->sindex == ST_MASK_ID_TAP_TAP) && stap_en)) {
			/* do not disable tap */
			return 0;
		}

		err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
				ST_LSM6DSM_LIR_ADDR,
				ST_LSM6DSM_INT_ENABLE_MASK,
				ST_LSM6DSM_DIS_BIT, true);
		if (err < 0)
			return err;

                err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
                                ST_LSM6DSM_LIR_ADDR,
                                ST_LSM6DSM_LIR_MASK,
                                ST_LSM6DSM_EN_BIT, true);
                if (err < 0)
                        return err;

		err = lsm6dsm_enable_accel(cdata,
			ST_MASK_ID_DIGITAL_FUNC, 0);
		if (err < 0)
			return err;

		err = st_lsm6dsm_write_data_with_mask(cdata,
					ST_LSM6DSM_TAP_EN_ADDR, ST_LSM6DSM_TAP_EN_MASK,
					ST_LSM6DSM_DIS_BIT, true);
		if (err < 0)
			return err;

	}
	return 0;
}

#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
int st_lsm6dsm_enable_sensor_hub(struct lsm6dsm_data *cdata,
					bool enable, enum st_mask_id id)
{
	int err;

	if (enable) {
		if (cdata->enable_sensorhub_mask == 0) {
			err = lsm6dsm_enable_digital_func(cdata,
						true, ST_MASK_ID_SENSOR_HUB);
			if (err < 0)
				return err;

			err = lsm6dsm_enable_accel(cdata, ST_MASK_ID_SENSOR_HUB,
						cdata->v_odr[ST_MASK_ID_EXT0]);
			if (err < 0)
				return err;

			err = st_lsm6dsm_write_data_with_mask(cdata,
						ST_LSM6DSM_SENSORHUB_ADDR,
						ST_LSM6DSM_SENSORHUB_MASK,
						ST_LSM6DSM_EN_BIT, true);
			if (err < 0)
				return err;

		} else
			err = lsm6dsm_enable_accel(cdata, ST_MASK_ID_SENSOR_HUB,
						cdata->v_odr[ST_MASK_ID_EXT0]);

		cdata->enable_sensorhub_mask |= BIT(id);
	} else {
		if ((cdata->enable_sensorhub_mask & ~BIT(id)) == 0) {
			err = st_lsm6dsm_write_data_with_mask(cdata,
						ST_LSM6DSM_SENSORHUB_ADDR,
						ST_LSM6DSM_SENSORHUB_MASK,
						ST_LSM6DSM_DIS_BIT, true);
			if (err < 0)
				return err;

			err = lsm6dsm_enable_accel(cdata,
						ST_MASK_ID_SENSOR_HUB, 0);
			if (err < 0)
				return err;

			err = lsm6dsm_enable_digital_func(cdata,
						false, ST_MASK_ID_SENSOR_HUB);
			if (err < 0)
				return err;
		} else
			err = lsm6dsm_enable_accel(cdata, ST_MASK_ID_SENSOR_HUB,
						cdata->v_odr[ST_MASK_ID_EXT0]);

		cdata->enable_sensorhub_mask &= ~BIT(id);
	}

	return err < 0 ? err : 0;
}
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */

int st_lsm6dsm_set_enable(struct lsm6dsm_sensor_data *sdata, bool enable, bool buffer)
{
	int err;
	u8 reg_value;

	switch (sdata->sindex) {
	case ST_MASK_ID_ACCEL:
		err = lsm6dsm_enable_accel(sdata->cdata, ST_MASK_ID_ACCEL,
			enable ? sdata->cdata->v_odr[ST_MASK_ID_ACCEL] : 0);
		if (err < 0)
			return 0;

		break;
	case ST_MASK_ID_GYRO:
		err = st_lsm6dsm_set_odr(sdata, enable ?
			sdata->cdata->v_odr[ST_MASK_ID_GYRO] : 0, true);
		if (err < 0)
			return err;

		break;
	case ST_MASK_ID_SIGN_MOTION:
		if (enable)
			reg_value = ST_LSM6DSM_EN_BIT;
		else
			reg_value = ST_LSM6DSM_DIS_BIT;

		err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
					ST_LSM6DSM_SIGN_MOTION_EN_ADDR,
					ST_LSM6DSM_SIGN_MOTION_EN_MASK,
					reg_value, true);
		if (err < 0)
			return err;

		err = lsm6dsm_enable_pedometer(sdata->cdata,
						enable, ST_MASK_ID_SIGN_MOTION);
		if (err < 0)
			return err;

		break;
	case ST_MASK_ID_STEP_COUNTER:
		if (enable)
			reg_value = ST_LSM6DSM_EN_BIT;
		else
			reg_value = ST_LSM6DSM_DIS_BIT;

		err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
					ST_LSM6DSM_TIMER_EN_ADDR,
					ST_LSM6DSM_TIMER_EN_MASK,
					reg_value, true);
		if (err < 0)
			return err;

		err = lsm6dsm_enable_pedometer(sdata->cdata,
					enable, ST_MASK_ID_STEP_COUNTER);
		if (err < 0)
			return err;

		break;
	case ST_MASK_ID_STEP_DETECTOR:
		err = lsm6dsm_enable_pedometer(sdata->cdata,
					enable, ST_MASK_ID_STEP_DETECTOR);
		if (err < 0)
			return err;

		break;
	case ST_MASK_ID_TILT:
		if (enable)
			reg_value = ST_LSM6DSM_EN_BIT;
		else
			reg_value = ST_LSM6DSM_DIS_BIT;

		err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
					ST_LSM6DSM_TILT_EN_ADDR,
					ST_LSM6DSM_TILT_EN_MASK,
					reg_value, true);
		if (err < 0)
			return err;

		err = lsm6dsm_enable_digital_func(sdata->cdata,
						enable, ST_MASK_ID_TILT);
		if (err < 0)
			return err;

		break;
	case ST_MASK_ID_WTILT:
		if (enable)
			reg_value = ST_LSM6DSM_EN_BIT;
		else
			reg_value = ST_LSM6DSM_DIS_BIT;

		err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
					ST_LSM6DSM_WTILT_EN_ADDR,
					ST_LSM6DSM_WTILT_EN_MASK,
					reg_value, true);
		if (err < 0)
			return err;

		err = lsm6dsm_enable_digital_func(sdata->cdata,
						enable, ST_MASK_ID_WTILT);
		if (err < 0)
			return err;

		break;
	case ST_MASK_ID_TAP:
	case ST_MASK_ID_TAP_TAP:
		err = lsm6dsm_enable_tap(sdata, enable);
		if (err < 0)
			return err;

		break;
	default:
		return -EINVAL;
	}

	if (buffer) {
		err = st_lsm6dsm_set_drdy_irq(sdata, enable);
		if (err < 0)
			return err;

		if (enable)
			sdata->cdata->sensors_enabled |= BIT(sdata->sindex);
		else
			sdata->cdata->sensors_enabled &= ~BIT(sdata->sindex);
	}

	return 0;
}

static int st_lsm6dsm_set_fs(struct lsm6dsm_sensor_data *sdata,
							unsigned int gain)
{
	int err, i;
	u8 pedometer_reg_value;

	for (i = 0; i < ST_LSM6DSM_FS_LIST_NUM; i++) {
		if (st_lsm6dsm_fs_table[sdata->sindex].fs_avl[i].gain == gain)
			break;
	}
	if (i == ST_LSM6DSM_FS_LIST_NUM)
		return -EINVAL;

	err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
			st_lsm6dsm_fs_table[sdata->sindex].addr,
			st_lsm6dsm_fs_table[sdata->sindex].mask,
			st_lsm6dsm_fs_table[sdata->sindex].fs_avl[i].value,
			true);
	if (err < 0)
		return err;

	sdata->c_gain[0] = gain;

	if (sdata->sindex == ST_MASK_ID_ACCEL) {
		if (i == 0)
			pedometer_reg_value = ST_LSM6DSM_STEP_COUNTER_THS_2G_VALUE;
		else
			pedometer_reg_value = ST_LSM6DSM_STEP_COUNTER_THS_4G_VALUE;

		st_lsm6dsm_write_embedded_registers(sdata->cdata,
					ST_LSM6DSM_STEP_COUNTER_THS_ADDR,
					&pedometer_reg_value, 1);
	}

	return 0;
}

static int st_lsm6dsm_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *ch, int *val,
							int *val2, long mask)
{
	int err;
	u8 outdata[ST_LSM6DSM_BYTE_FOR_CHANNEL];
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);

		if (st_lsm6dsm_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
			mutex_unlock(&indio_dev->mlock);
			return -EBUSY;
		}

		mutex_lock(&sdata->cdata->odr_lock);

		err = st_lsm6dsm_set_enable(sdata, true, false);
		if (err < 0) {
			mutex_unlock(&sdata->cdata->odr_lock);
			mutex_unlock(&indio_dev->mlock);
			return err;
		}

		if (sdata->sindex == ST_MASK_ID_ACCEL)
			msleep(40);

		if (sdata->sindex == ST_MASK_ID_GYRO)
			msleep(120);

		err = sdata->cdata->tf->read(sdata->cdata, ch->address,
				ST_LSM6DSM_BYTE_FOR_CHANNEL, outdata, true);
		if (err < 0) {
			st_lsm6dsm_set_enable(sdata, false, false);
			mutex_unlock(&sdata->cdata->odr_lock);
			mutex_unlock(&indio_dev->mlock);
			return err;
		}

		*val = (s16)get_unaligned_le16(outdata);
		*val = *val >> ch->scan_type.shift;

		st_lsm6dsm_set_enable(sdata, false, false);

		mutex_unlock(&sdata->cdata->odr_lock);
		mutex_unlock(&indio_dev->mlock);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = sdata->c_gain[0];
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}

	return 0;
}

static int st_lsm6dsm_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	int err;
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&indio_dev->mlock);

		if (st_lsm6dsm_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
			mutex_unlock(&indio_dev->mlock);
			return -EBUSY;
		}

		err = st_lsm6dsm_set_fs(sdata, val2);
		mutex_unlock(&indio_dev->mlock);
		break;
	default:
		return -EINVAL;
	}

	return err < 0 ? err : 0;
}

static int st_lsm6dsm_reset_steps(struct lsm6dsm_data *cdata)
{
	int err;
	u8 reg_value = 0x00;

	err = cdata->tf->read(cdata,
			ST_LSM6DSM_STEP_COUNTER_RES_ADDR, 1, &reg_value, true);
	if (err < 0)
		return err;

	if (reg_value & ST_LSM6DSM_FUNC_EN_MASK)
		reg_value = ST_LSM6DSM_STEP_COUNTER_RES_FUNC_EN;
	else
		reg_value = ST_LSM6DSM_DIS_BIT;

	err = st_lsm6dsm_write_data_with_mask(cdata,
				ST_LSM6DSM_STEP_COUNTER_RES_ADDR,
				ST_LSM6DSM_STEP_COUNTER_RES_MASK,
				ST_LSM6DSM_STEP_COUNTER_RES_ALL_EN, true);
	if (err < 0)
		return err;

	err = st_lsm6dsm_write_data_with_mask(cdata,
				ST_LSM6DSM_STEP_COUNTER_RES_ADDR,
				ST_LSM6DSM_STEP_COUNTER_RES_MASK,
				reg_value, true);
	if (err < 0)
		return err;

	cdata->reset_steps = true;

	return 0;
}

static int st_lsm6dsm_init_sensor(struct lsm6dsm_data *cdata)
{
	int err;
	u8 default_reg_value = ST_LSM6DSM_RESET_MASK;

	err = cdata->tf->write(cdata, ST_LSM6DSM_RESET_ADDR, 1,
					&default_reg_value, true);
	if (err < 0)
		return err;

	msleep(200);

	/* Latch interrupts */
	err = st_lsm6dsm_write_data_with_mask(cdata, ST_LSM6DSM_LIR_ADDR,
				ST_LSM6DSM_LIR_MASK, ST_LSM6DSM_EN_BIT, true);
	if (err < 0)
		return err;

	/* Enable BDU for sensors data */
	err = st_lsm6dsm_write_data_with_mask(cdata, ST_LSM6DSM_BDU_ADDR,
				ST_LSM6DSM_BDU_MASK, ST_LSM6DSM_EN_BIT, true);
	if (err < 0)
		return err;

	err = st_lsm6dsm_write_data_with_mask(cdata,
					ST_LSM6DSM_ROUNDING_ADDR,
					ST_LSM6DSM_ROUNDING_MASK,
					ST_LSM6DSM_EN_BIT, true);
	if (err < 0)
		return err;

	/* Set tap and tap-tap threshold */
	err = st_lsm6dsm_write_data_with_mask(cdata,
					  ST_LSM6DSM_TAP_THS_ADDR,
					  ST_LSM6DSM_TAP_THS_MASK,
					  0xc, true);
	if (err < 0)
		return err;

	/* configure int duration  */
	err = st_lsm6dsm_write_data_with_mask(cdata,
					  ST_LSM6DSM_TAP_DUR_ADDR,
					  ST_LSM6DSM_TAP_DUR_MASK,
					  0x7f, true);
	if (err < 0)
		return err;

	/* configure double tap  */
	err = st_lsm6dsm_write_data_with_mask(cdata,
					  ST_LSM6DSM_DTAP_EN_ADDR,
					  ST_LSM6DSM_DTAP_EN_MASK,
					  1, true);
	if (err < 0)
		return err;

	/* Redirect INT2 on INT1, all interrupt will be available on INT1 */
	err = st_lsm6dsm_write_data_with_mask(cdata,
					ST_LSM6DSM_INT2_ON_INT1_ADDR,
					ST_LSM6DSM_INT2_ON_INT1_MASK,
					ST_LSM6DSM_EN_BIT, true);
	if (err < 0)
		return err;

	err = st_lsm6dsm_reset_steps(cdata);
	if (err < 0)
		return err;

	default_reg_value = 0x00;

	err = st_lsm6dsm_write_embedded_registers(cdata,
					ST_LSM6DSM_STEP_COUNTER_DURATION_ADDR,
					&default_reg_value, 1);
	if (err < 0)
		return err;

	default_reg_value = ST_LSM6DSM_STEP_COUNTER_THS_2G_VALUE;

	err = st_lsm6dsm_write_embedded_registers(cdata,
					ST_LSM6DSM_STEP_COUNTER_THS_ADDR,
					&default_reg_value, 1);
	if (err < 0)
		return err;

	return st_lsm6dsm_get_drdy_reg(cdata, &cdata->drdy_reg);
}

static int st_lsm6dsm_set_selftest(struct lsm6dsm_sensor_data *sdata, int index)
{
	u8 mode, mask;

	switch (sdata->sindex) {
	case ST_MASK_ID_ACCEL:
		mask = ST_LSM6DSM_SELFTEST_ACCEL_MASK;
		mode = st_lsm6dsm_selftest_table[index].accel_value;
		break;
	case ST_MASK_ID_GYRO:
		mask = ST_LSM6DSM_SELFTEST_GYRO_MASK;
		mode = st_lsm6dsm_selftest_table[index].gyro_value;
		break;
	default:
		return -EINVAL;
	}

	return st_lsm6dsm_write_data_with_mask(sdata->cdata,
				ST_LSM6DSM_SELFTEST_ADDR, mask, mode, true);
}

static ssize_t st_lsm6dsm_sysfs_set_max_delivery_rate(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u8 duration;
	int err;
	unsigned int max_delivery_rate;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	err = kstrtouint(buf, 10, &max_delivery_rate);
	if (err < 0)
		return -EINVAL;

	if (max_delivery_rate == sdata->cdata->v_odr[ST_MASK_ID_STEP_COUNTER])
		return size;

	duration = max_delivery_rate / ST_LSM6DSM_MIN_DURATION_MS;

	err = st_lsm6dsm_write_embedded_registers(sdata->cdata,
					ST_LSM6DSM_STEP_COUNTER_DURATION_ADDR,
					&duration, 1);
	if (err < 0)
		return err;

	sdata->cdata->v_odr[ST_MASK_ID_STEP_COUNTER] = max_delivery_rate;

	return size;
}

static ssize_t st_lsm6dsm_sysfs_get_max_delivery_rate(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lsm6dsm_sensor_data *sdata = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n",
				sdata->cdata->v_odr[ST_MASK_ID_STEP_COUNTER]);
}

static ssize_t st_lsm6dsm_sysfs_reset_counter(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int err;
	struct lsm6dsm_sensor_data *sdata = iio_priv(dev_to_iio_dev(dev));

	err = st_lsm6dsm_reset_steps(sdata->cdata);
	if (err < 0)
		return err;

	return size;
}

static ssize_t st_lsm6dsm_sysfs_get_sampling_frequency(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lsm6dsm_sensor_data *sdata = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sdata->cdata->v_odr[sdata->sindex]);
}

static ssize_t st_lsm6dsm_sysfs_set_sampling_frequency(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int err;
	unsigned int odr;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	err = kstrtoint(buf, 10, &odr);
	if (err < 0)
		return err;

	if (odr == 12)
		odr++;

	mutex_lock(&indio_dev->mlock);

	mutex_lock(&sdata->cdata->odr_lock);
#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
	if (!((sdata->sindex & ST_MASK_ID_ACCEL) &&
					sdata->cdata->injection_mode)) {
		if (sdata->cdata->v_odr[sdata->sindex] != odr)
			err = st_lsm6dsm_set_odr(sdata, odr, false);
	}
#else /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */
	if (sdata->cdata->v_odr[sdata->sindex] != odr) {
		if ((sdata->sindex == ST_MASK_ID_ACCEL) && (sdata->cdata->sensors_enabled & BIT(ST_MASK_ID_ACCEL)))
			err = lsm6dsm_enable_accel(sdata->cdata, ST_MASK_ID_ACCEL, odr);
		else
			err = st_lsm6dsm_set_odr(sdata, odr, false);
	}
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */
	mutex_unlock(&sdata->cdata->odr_lock);

	mutex_unlock(&indio_dev->mlock);

	return err < 0 ? err : size;
}

static ssize_t st_lsm6dsm_sysfs_sampling_frequency_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, len = 0;

	for (i = 0; i < ST_LSM6DSM_ODR_LIST_NUM; i++) {
		if (st_lsm6dsm_odr_table.odr_avl[i].hz == 13)
			len += scnprintf(buf + len, PAGE_SIZE - len, "%d ", 12);
		else
			len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
					 st_lsm6dsm_odr_table.odr_avl[i].hz);
	}
	buf[len - 1] = '\n';

	return len;
}

static ssize_t st_lsm6dsm_sysfs_scale_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, len = 0;
	struct lsm6dsm_sensor_data *sdata = iio_priv(dev_to_iio_dev(dev));

	for (i = 0; i < ST_LSM6DSM_FS_LIST_NUM; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%09u ",
			st_lsm6dsm_fs_table[sdata->sindex].fs_avl[i].gain);
	}
	buf[len - 1] = '\n';

	return len;
}

static ssize_t st_lsm6dsm_sysfs_get_selftest_available(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s, %s\n",
				st_lsm6dsm_selftest_table[1].string_mode,
				st_lsm6dsm_selftest_table[2].string_mode);
}

static ssize_t st_lsm6dsm_sysfs_get_selftest_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int8_t result;
	char *message = NULL;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&sdata->cdata->odr_lock);

	switch (sdata->sindex) {
	case ST_MASK_ID_ACCEL:
		result = sdata->cdata->accel_selftest_status;
		break;
	case ST_MASK_ID_GYRO:
		result = sdata->cdata->gyro_selftest_status;
		break;
	default:
		mutex_unlock(&sdata->cdata->odr_lock);
		return -EINVAL;
	}

	mutex_unlock(&sdata->cdata->odr_lock);

	if (result == 0)
		message = ST_LSM6DSM_SELFTEST_NA_MS;
	else if (result < 0)
		message = ST_LSM6DSM_SELFTEST_FAIL_MS;
	else if (result > 0)
		message = ST_LSM6DSM_SELFTEST_PASS_MS;

	return sprintf(buf, "%s\n", message);
}

static ssize_t st_lsm6dsm_sysfs_start_selftest_status(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int err, i, n;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);
	u8 reg_status, reg_addr, temp_reg_status, outdata[6];
	int x = 0, y = 0, z = 0, x_selftest = 0, y_selftest = 0, z_selftest = 0;

	mutex_lock(&sdata->cdata->odr_lock);

	switch (sdata->sindex) {
	case ST_MASK_ID_ACCEL:
		sdata->cdata->accel_selftest_status = 0;
		break;
	case ST_MASK_ID_GYRO:
		sdata->cdata->gyro_selftest_status = 0;
		break;
	default:
		mutex_unlock(&sdata->cdata->odr_lock);
		mutex_unlock(&indio_dev->mlock);
		return -EINVAL;
	}

	if (sdata->cdata->sensors_enabled > 0) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return -EBUSY;
	}

	for (n = 0; n < ARRAY_SIZE(st_lsm6dsm_selftest_table); n++) {
		if (strncmp(buf, st_lsm6dsm_selftest_table[n].string_mode,
								size - 2) == 0)
			break;
	}
	if (n == ARRAY_SIZE(st_lsm6dsm_selftest_table)) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return -EINVAL;
	}

	switch (sdata->sindex) {
	case ST_MASK_ID_ACCEL:
		reg_addr = ST_LSM6DSM_SELFTEST_ACCEL_ADDR;
		temp_reg_status = ST_LSM6DSM_SELFTEST_ACCEL_REG_VALUE;
		break;
	case ST_MASK_ID_GYRO:
		reg_addr = ST_LSM6DSM_SELFTEST_GYRO_ADDR;
		temp_reg_status = ST_LSM6DSM_SELFTEST_GYRO_REG_VALUE;
		break;
	default:
		mutex_unlock(&sdata->cdata->odr_lock);
		return -EINVAL;
	}

	err = sdata->cdata->tf->read(sdata->cdata,
					reg_addr, 1, &reg_status, true);
	if (err < 0) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return err;
	}

	err = sdata->cdata->tf->write(sdata->cdata,
					reg_addr, 1, &temp_reg_status, false);
	if (err < 0) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return err;
	}

	/* get data with selftest disabled */
	msleep(100);

	for (i = 0; i < 20; i++) {
		err = sdata->cdata->tf->read(sdata->cdata,
					sdata->data_out_reg, 6, outdata, true);
		if (err < 0) {
			i--;
			continue;
		}

		x += ((s16)*(u16 *)&outdata[0]) / 20;
		y += ((s16)*(u16 *)&outdata[2]) / 20;
		z += ((s16)*(u16 *)&outdata[4]) / 20;

		mdelay(10);
	}

	err = st_lsm6dsm_set_selftest(sdata, n);
	if (err < 0) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return err;
	}

	/* get data with selftest enabled */
	msleep(100);

	for (i = 0; i < 20; i++) {
		err = sdata->cdata->tf->read(sdata->cdata,
					sdata->data_out_reg, 6, outdata, true);
		if (err < 0) {
			i--;
			continue;
		}

		x_selftest += ((s16)*(u16 *)&outdata[0]) / 20;
		y_selftest += ((s16)*(u16 *)&outdata[2]) / 20;
		z_selftest += ((s16)*(u16 *)&outdata[4]) / 20;

		mdelay(10);
	}

	err = sdata->cdata->tf->write(sdata->cdata,
					reg_addr, 1, &reg_status, false);
	if (err < 0) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return err;
	}

	err = st_lsm6dsm_set_selftest(sdata, 0);
	if (err < 0) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return err;
	}

	switch (sdata->sindex) {
	case ST_MASK_ID_ACCEL:
		if ((abs(x_selftest - x) < ST_LSM6DSM_SELFTEST_ACCEL_MIN) ||
				(abs(x_selftest - x) > ST_LSM6DSM_SELFTEST_ACCEL_MAX)) {
			sdata->cdata->accel_selftest_status = -1;
			goto selftest_failure;
		}

		if ((abs(y_selftest - y) < ST_LSM6DSM_SELFTEST_ACCEL_MIN) ||
				(abs(y_selftest - y) > ST_LSM6DSM_SELFTEST_ACCEL_MAX)) {
			sdata->cdata->accel_selftest_status = -1;
			goto selftest_failure;
		}

		if ((abs(z_selftest - z) < ST_LSM6DSM_SELFTEST_ACCEL_MIN) ||
				(abs(z_selftest - z) > ST_LSM6DSM_SELFTEST_ACCEL_MAX)) {
			sdata->cdata->accel_selftest_status = -1;
			goto selftest_failure;
		}

		sdata->cdata->accel_selftest_status = 1;
		break;
	case ST_MASK_ID_GYRO:
		if ((abs(x_selftest - x) < ST_LSM6DSM_SELFTEST_GYRO_MIN) ||
				(abs(x_selftest - x) > ST_LSM6DSM_SELFTEST_GYRO_MAX)) {
			sdata->cdata->gyro_selftest_status = -1;
			goto selftest_failure;
		}

		if ((abs(y_selftest - y) < ST_LSM6DSM_SELFTEST_GYRO_MIN) ||
				(abs(y_selftest - y) > ST_LSM6DSM_SELFTEST_GYRO_MAX)) {
			sdata->cdata->gyro_selftest_status = -1;
			goto selftest_failure;
		}

		if ((abs(z_selftest - z) < ST_LSM6DSM_SELFTEST_GYRO_MIN) ||
				(abs(z_selftest - z) > ST_LSM6DSM_SELFTEST_GYRO_MAX)) {
			sdata->cdata->gyro_selftest_status = -1;
			goto selftest_failure;
		}

		sdata->cdata->gyro_selftest_status = 1;
		break;
	default:
		mutex_unlock(&sdata->cdata->odr_lock);
		return -EINVAL;
	}

selftest_failure:
	mutex_unlock(&sdata->cdata->odr_lock);

	return size;
}

ssize_t st_lsm6dsm_sysfs_flush_fifo(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u64 sensor_last_timestamp, event_type = 0;
	int stype = 0;
	u64 timestamp_flush = 0;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);

	if (st_lsm6dsm_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
		mutex_lock(&sdata->cdata->odr_lock);
		disable_irq(sdata->cdata->irq);
	} else {
		mutex_unlock(&indio_dev->mlock);
		return -EINVAL;
	}

	sensor_last_timestamp =
			sdata->cdata->fifo_output[sdata->sindex].timestamp_p;

	st_lsm6dsm_read_fifo(sdata->cdata, true);

	if (sensor_last_timestamp ==
			sdata->cdata->fifo_output[sdata->sindex].timestamp_p)
		event_type = STM_IIO_EV_DIR_FIFO_EMPTY;
	else
		event_type = STM_IIO_EV_DIR_FIFO_DATA;

	timestamp_flush = sdata->cdata->fifo_output[sdata->sindex].timestamp_p;

	enable_irq(sdata->cdata->irq);

	switch (sdata->sindex) {
	case ST_MASK_ID_ACCEL:
		stype = IIO_ACCEL;
		break;

	case ST_MASK_ID_GYRO:
		stype = IIO_ANGL_VEL;
		break;

#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
	case ST_MASK_ID_EXT0:
		stype = IIO_MAGN;
		break;
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */

	}

	iio_push_event(indio_dev, IIO_UNMOD_EVENT_CODE(stype,
				-1, STM_IIO_EV_TYPE_FIFO_FLUSH, event_type),
				timestamp_flush);

	mutex_unlock(&sdata->cdata->odr_lock);
	mutex_unlock(&indio_dev->mlock);

	return size;
}

ssize_t st_lsm6dsm_sysfs_get_hwfifo_enabled(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	return sprintf(buf, "%d\n",
				sdata->cdata->hwfifo_enabled[sdata->sindex]);
}

ssize_t st_lsm6dsm_sysfs_set_hwfifo_enabled(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int err;
	bool enable = false;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	if (st_lsm6dsm_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
		err = -EBUSY;
		goto set_hwfifo_enabled_unlock_mutex;
	}

	err = strtobool(buf, &enable);
	if (err < 0)
		goto set_hwfifo_enabled_unlock_mutex;

	mutex_lock(&sdata->cdata->odr_lock);

	sdata->cdata->hwfifo_enabled[sdata->sindex] = enable;

	if (enable)
		sdata->cdata->sensors_use_fifo |= BIT(sdata->sindex);
	else
		sdata->cdata->sensors_use_fifo &= ~BIT(sdata->sindex);

	mutex_unlock(&sdata->cdata->odr_lock);
	mutex_unlock(&indio_dev->mlock);

	return size;

set_hwfifo_enabled_unlock_mutex:
	mutex_unlock(&indio_dev->mlock);
	return err;
}

ssize_t st_lsm6dsm_sysfs_get_hwfifo_watermark(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	return sprintf(buf, "%d\n",
				sdata->cdata->hwfifo_watermark[sdata->sindex]);
}

ssize_t st_lsm6dsm_sysfs_set_hwfifo_watermark(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int err = 0, watermark = 0, old_watermark;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	err = kstrtoint(buf, 10, &watermark);
	if (err < 0)
		return err;

	if ((watermark < 1) || (watermark > ST_LSM6DSM_MAX_FIFO_LENGHT))
		return -EINVAL;

	mutex_lock(&sdata->cdata->odr_lock);

	if ((sdata->cdata->sensors_enabled & BIT(sdata->sindex)) &&
				(sdata->cdata->sensors_use_fifo & BIT(sdata->sindex))) {
		disable_irq(sdata->cdata->irq);

		if (sdata->cdata->fifo_status != BYPASS)
			st_lsm6dsm_read_fifo(sdata->cdata, true);

		old_watermark = sdata->cdata->hwfifo_watermark[sdata->sindex];
		sdata->cdata->hwfifo_watermark[sdata->sindex] = watermark;

		err = lsm6dsm_set_watermark(sdata->cdata);
		if (err < 0)
			sdata->cdata->hwfifo_watermark[sdata->sindex] = old_watermark;

		enable_irq(sdata->cdata->irq);
	} else
		sdata->cdata->hwfifo_watermark[sdata->sindex] = watermark;

	mutex_unlock(&sdata->cdata->odr_lock);

	return err < 0 ? err : size;
}

ssize_t st_lsm6dsm_sysfs_get_hwfifo_watermark_max(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ST_LSM6DSM_MAX_FIFO_LENGHT);
}

ssize_t st_lsm6dsm_sysfs_get_hwfifo_watermark_min(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", 1);
}

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
static ssize_t st_lsm6dsm_sysfs_set_injection_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int err, start;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);

	if (st_lsm6dsm_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
		mutex_unlock(&indio_dev->mlock);
		return -EBUSY;
	}

	err = kstrtoint(buf, 10, &start);
	if (err < 0) {
		mutex_unlock(&indio_dev->mlock);
		return err;
	}

	mutex_lock(&sdata->cdata->odr_lock);

	if (start == 0) {
		/* End injection */
		err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
				ST_LSM6DSM_TEST_REG_ADDR,
				ST_LSM6DSM_START_INJECT_XL_MASK, 0, true);
		if (err < 0) {
			mutex_unlock(&sdata->cdata->odr_lock);
			mutex_unlock(&indio_dev->mlock);
			return err;
		}

		 /* Force accel ODR to 26Hz if dependencies are enabled */
		 if (sdata->cdata->sensors_enabled > 0) {
			err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
				st_lsm6dsm_odr_table.addr[sdata->sindex],
				st_lsm6dsm_odr_table.mask[sdata->sindex],
				st_lsm6dsm_odr_table.odr_avl[1].value, true);
			if (err < 0) {
				mutex_unlock(&sdata->cdata->odr_lock);
				mutex_unlock(&indio_dev->mlock);
				return err;
			}
		}

		sdata->cdata->injection_mode = false;
	} else {
		sdata->cdata->last_injection_timestamp = 0;
		sdata->cdata->injection_odr = 0;

		/* Set start injection */
		err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
				ST_LSM6DSM_TEST_REG_ADDR,
				ST_LSM6DSM_START_INJECT_XL_MASK, 1, true);
		if (err < 0) {
			mutex_unlock(&sdata->cdata->odr_lock);
			mutex_unlock(&indio_dev->mlock);
			return err;
		}

		sdata->cdata->injection_mode = true;
	}

	mutex_unlock(&sdata->cdata->odr_lock);
	mutex_unlock(&indio_dev->mlock);

	return size;
}

static ssize_t st_lsm6dsm_sysfs_get_injection_mode(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", sdata->cdata->injection_mode);
}

static ssize_t st_lsm6dsm_sysfs_upload_xl_data(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int err, i, n = 1;
	s64 timestamp, deltatime;
	u8 sample[3], current_odr;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);

	if (!sdata->cdata->injection_mode) {
		mutex_unlock(&indio_dev->mlock);
		return -EINVAL;
	}

	for (i = 0; i < 3; i++)
		sample[i] = *(s16 *)(&buf[i * 2]) >> 8;

	timestamp = *(s64 *)(buf + ALIGN(6, sizeof(s64)));

	if (sdata->cdata->last_injection_timestamp > 0) {
		deltatime = timestamp - sdata->cdata->last_injection_timestamp;
		if ((deltatime > ST_LSM6DSM_208HZ_INJECT_NS_DOWN) &&
				(deltatime <= ST_LSM6DSM_208HZ_INJECT_NS_UP)) {
			current_odr = 208;
			n = 4;
		} else if ((deltatime > ST_LSM6DSM_104HZ_INJECT_NS_DOWN) &&
				(deltatime <= ST_LSM6DSM_104HZ_INJECT_NS_UP)) {
			current_odr = 104;
			n = 3;
		} else if ((deltatime > ST_LSM6DSM_52HZ_INJECT_NS_DOWN) &&
				(deltatime <= ST_LSM6DSM_52HZ_INJECT_NS_UP)) {
			current_odr = 52;
			n = 2;
		} else if ((deltatime > ST_LSM6DSM_26HZ_INJECT_NS_DOWN) &&
				(deltatime <= ST_LSM6DSM_26HZ_INJECT_NS_UP)) {
			current_odr = 26;
			n = 1;
		} else {
			mutex_unlock(&indio_dev->mlock);
			return -EINVAL;
		}

		if (sdata->cdata->injection_odr != current_odr) {
			err = st_lsm6dsm_write_data_with_mask(sdata->cdata,
				st_lsm6dsm_odr_table.addr[sdata->sindex],
				st_lsm6dsm_odr_table.mask[sdata->sindex],
				st_lsm6dsm_odr_table.odr_avl[n].value, true);
			if (err < 0) {
				mutex_unlock(&indio_dev->mlock);
				return err;
			}

			sdata->cdata->injection_odr = current_odr;
		}
	}

	sdata->cdata->last_injection_timestamp = timestamp;

	err = sdata->cdata->tf->write(sdata->cdata, ST_LSM6DSM_INJECT_XL_X_ADDR,
					3, (u8 *)sample, false);
	if (err < 0) {
		mutex_unlock(&indio_dev->mlock);
		return err;
	}

	mutex_unlock(&indio_dev->mlock);

	usleep_range(1000, 2000);

	return size;
}

static ssize_t st_lsm6dsm_sysfs_get_injection_sensors(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", "lsm6dsm_accel");
}
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

ssize_t st_lsm6dsm_get_module_id(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6dsm_sensor_data *sdata = iio_priv(indio_dev);
	struct lsm6dsm_data *cdata = sdata->cdata;

	return scnprintf(buf, PAGE_SIZE, "%u\n", cdata->module_id);
}

static ST_LSM6DSM_DEV_ATTR_SAMP_FREQ();
static ST_LSM6DSM_DEV_ATTR_SAMP_FREQ_AVAIL();
static ST_LSM6DSM_DEV_ATTR_SCALE_AVAIL(in_accel_scale_available);
static ST_LSM6DSM_DEV_ATTR_SCALE_AVAIL(in_anglvel_scale_available);

static ST_LSM6DSM_HWFIFO_ENABLED();
static ST_LSM6DSM_HWFIFO_WATERMARK();
static ST_LSM6DSM_HWFIFO_WATERMARK_MIN();
static ST_LSM6DSM_HWFIFO_WATERMARK_MAX();
static ST_LSM6DSM_HWFIFO_FLUSH();

static IIO_DEVICE_ATTR(reset_counter, S_IWUSR,
				NULL, st_lsm6dsm_sysfs_reset_counter, 0);

static IIO_DEVICE_ATTR(max_delivery_rate, S_IWUSR | S_IRUGO,
				st_lsm6dsm_sysfs_get_max_delivery_rate,
				st_lsm6dsm_sysfs_set_max_delivery_rate, 0);

static IIO_DEVICE_ATTR(selftest_available, S_IRUGO,
				st_lsm6dsm_sysfs_get_selftest_available,
				NULL, 0);

static IIO_DEVICE_ATTR(selftest, S_IWUSR | S_IRUGO,
				st_lsm6dsm_sysfs_get_selftest_status,
				st_lsm6dsm_sysfs_start_selftest_status, 0);

static IIO_DEVICE_ATTR(module_id, 0444, st_lsm6dsm_get_module_id, NULL, 0);

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
static IIO_DEVICE_ATTR(injection_mode, S_IWUSR | S_IRUGO,
				st_lsm6dsm_sysfs_get_injection_mode,
				st_lsm6dsm_sysfs_set_injection_mode, 0);

static IIO_DEVICE_ATTR(in_accel_injection_raw, S_IWUSR, NULL,
				st_lsm6dsm_sysfs_upload_xl_data, 0);

static IIO_DEVICE_ATTR(injection_sensors, S_IRUGO,
				st_lsm6dsm_sysfs_get_injection_sensors,
				NULL, 0);
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

static int st_lsm6dsm_write_raw_get_fmt(struct iio_dev *indio_dev,
					struct iio_chan_spec const *chan,
					long mask)
{
	if (mask == IIO_CHAN_INFO_SCALE) {
		if ((chan->type == IIO_ANGL_VEL) ||
		    (chan->type == IIO_ACCEL))
			return IIO_VAL_INT_PLUS_NANO;
	}

	return -EINVAL;
}

static struct attribute *st_lsm6dsm_accel_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
	&iio_dev_attr_hwfifo_enabled.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_min.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
	&iio_dev_attr_injection_mode.dev_attr.attr,
	&iio_dev_attr_in_accel_injection_raw.dev_attr.attr,
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

	NULL,
};

static const struct attribute_group st_lsm6dsm_accel_attribute_group = {
	.attrs = st_lsm6dsm_accel_attributes,
};

static const struct iio_info st_lsm6dsm_accel_info = {
	.attrs = &st_lsm6dsm_accel_attribute_group,
	.read_raw = &st_lsm6dsm_read_raw,
	.write_raw = &st_lsm6dsm_write_raw,
	.write_raw_get_fmt = st_lsm6dsm_write_raw_get_fmt,
};

static struct attribute *st_lsm6dsm_gyro_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_anglvel_scale_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
	&iio_dev_attr_hwfifo_enabled.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_min.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsm_gyro_attribute_group = {
	.attrs = st_lsm6dsm_gyro_attributes,
};

static const struct iio_info st_lsm6dsm_gyro_info = {
	.attrs = &st_lsm6dsm_gyro_attribute_group,
	.read_raw = &st_lsm6dsm_read_raw,
	.write_raw = &st_lsm6dsm_write_raw,
	.write_raw_get_fmt = st_lsm6dsm_write_raw_get_fmt,
};

static struct attribute *st_lsm6dsm_sign_motion_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
	&iio_dev_attr_injection_sensors.dev_attr.attr,
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

	NULL,
};

static const struct attribute_group st_lsm6dsm_sign_motion_attribute_group = {
	.attrs = st_lsm6dsm_sign_motion_attributes,
};

static const struct iio_info st_lsm6dsm_sign_motion_info = {
	.attrs = &st_lsm6dsm_sign_motion_attribute_group,
};

static struct attribute *st_lsm6dsm_step_c_attributes[] = {
	&iio_dev_attr_reset_counter.dev_attr.attr,
	&iio_dev_attr_max_delivery_rate.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
	&iio_dev_attr_injection_sensors.dev_attr.attr,
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

	NULL,
};

static const struct attribute_group st_lsm6dsm_step_c_attribute_group = {
	.attrs = st_lsm6dsm_step_c_attributes,
};

static const struct iio_info st_lsm6dsm_step_c_info = {
	.attrs = &st_lsm6dsm_step_c_attribute_group,
	.read_raw = &st_lsm6dsm_read_raw,
};

static struct attribute *st_lsm6dsm_step_d_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
	&iio_dev_attr_injection_sensors.dev_attr.attr,
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

	NULL,
};

static const struct attribute_group st_lsm6dsm_step_d_attribute_group = {
	.attrs = st_lsm6dsm_step_d_attributes,
};

static const struct iio_info st_lsm6dsm_step_d_info = {
	.attrs = &st_lsm6dsm_step_d_attribute_group,
};

static struct attribute *st_lsm6dsm_tilt_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
	&iio_dev_attr_injection_sensors.dev_attr.attr,
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

	NULL,
};

static const struct attribute_group st_lsm6dsm_tilt_attribute_group = {
	.attrs = st_lsm6dsm_tilt_attributes,
};

static const struct iio_info st_lsm6dsm_tilt_info = {
	.attrs = &st_lsm6dsm_tilt_attribute_group,
};

static struct attribute *st_lsm6dsm_wtilt_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
	&iio_dev_attr_injection_sensors.dev_attr.attr,
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

	NULL,
};

static const struct attribute_group st_lsm6dsm_wtilt_attribute_group = {
	.attrs = st_lsm6dsm_wtilt_attributes,
};

static const struct iio_info st_lsm6dsm_wtilt_info = {
	.attrs = &st_lsm6dsm_wtilt_attribute_group,
};

static struct attribute *st_lsm6dsm_tap_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
	&iio_dev_attr_injection_sensors.dev_attr.attr,
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

	NULL,
};

static const struct attribute_group st_lsm6dsm_tap_attribute_group = {
	.attrs = st_lsm6dsm_tap_attributes,
};

static const struct iio_info st_lsm6dsm_tap_info = {
	.attrs = &st_lsm6dsm_tap_attribute_group,
};

static struct attribute *st_lsm6dsm_tap_tap_attributes[] = {
	&iio_dev_attr_module_id.dev_attr.attr,

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
	&iio_dev_attr_injection_sensors.dev_attr.attr,
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

	NULL,
};

static const struct attribute_group st_lsm6dsm_tap_tap_attribute_group = {
	.attrs = st_lsm6dsm_tap_tap_attributes,
};

static const struct iio_info st_lsm6dsm_tap_tap_info = {
	.attrs = &st_lsm6dsm_tap_tap_attribute_group,
};

#ifdef CONFIG_IIO_TRIGGER
static const struct iio_trigger_ops st_lsm6dsm_trigger_ops = {
	.set_trigger_state = ST_LSM6DSM_TRIGGER_SET_STATE,
};
#define ST_LSM6DSM_TRIGGER_OPS (&st_lsm6dsm_trigger_ops)
#else
#define ST_LSM6DSM_TRIGGER_OPS NULL
#endif

static void st_lsm6dsm_get_properties(struct lsm6dsm_data *cdata)
{
	if (device_property_read_u32(cdata->dev, "st,module_id",
				     &cdata->module_id)) {
		cdata->module_id = 1;
	}
}

int st_lsm6dsm_common_probe(struct lsm6dsm_data *cdata, int irq)
{
	u8 wai = 0x00;
	int i, n, err;
	struct lsm6dsm_sensor_data *sdata;

	mutex_init(&cdata->bank_registers_lock);
	mutex_init(&cdata->fifo_lock);
	mutex_init(&cdata->tb.buf_lock);
	mutex_init(&cdata->odr_lock);

	cdata->fifo_watermark = 0;
	cdata->fifo_status = BYPASS;
	cdata->enable_digfunc_mask = 0;
	cdata->enable_pedometer_mask = 0;
#ifdef CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT
	cdata->enable_sensorhub_mask = 0;
#endif /* CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT */

	cdata->irq_enable_fifo_mask = 0;
	cdata->irq_enable_accel_ext_mask = 0;

	for (i = 0; i < ST_INDIO_DEV_NUM + 1; i++) {
		cdata->hw_odr[i] = 0;
		cdata->v_odr[i] = 0;
		cdata->hwfifo_enabled[i] = false;
		cdata->hwfifo_decimator[i] = 0;
		cdata->hwfifo_watermark[i] = 1;
		cdata->nofifo_decimation[i].decimator = 1;
		cdata->nofifo_decimation[i].num_samples = 0;
		cdata->fifo_output[i].sip = 0;
		cdata->fifo_output[i].decimator = 1;
		cdata->fifo_output[i].timestamp_p = 0;
		cdata->fifo_output[i].sip = 0;
		cdata->fifo_output[i].initialized = false;
	}

	cdata->sensors_use_fifo = 0;
	cdata->sensors_enabled = 0;

	cdata->gyro_selftest_status = 0;
	cdata->accel_selftest_status = 0;

	cdata->accel_on = false;
	cdata->magn_on = false;

	cdata->reset_steps = false;
	cdata->num_steps = 0;

	cdata->accel_odr_dependency[0] = 0;
	cdata->accel_odr_dependency[1] = 0;
	cdata->accel_odr_dependency[2] = 0;

	cdata->trigger_odr = 0;

	cdata->fifo_data = kmalloc(ST_LSM6DSM_MAX_FIFO_SIZE *
						sizeof(u8), GFP_KERNEL);
	if (!cdata->fifo_data)
		return -ENOMEM;

#ifdef CONFIG_ST_LSM6DSM_XL_DATA_INJECTION
	cdata->injection_mode = false;
	cdata->last_injection_timestamp = 0;
	cdata->injection_odr = 0;
#endif /* CONFIG_ST_LSM6DSM_XL_DATA_INJECTION */

	err = cdata->tf->read(cdata, ST_LSM6DSM_WAI_ADDRESS, 1, &wai, true);
	if (err < 0) {
		dev_err(cdata->dev, "failed to read Who-Am-I register.\n");
		goto free_fifo_data;
	}
	if (wai != ST_LSM6DSM_WAI_EXP) {
		dev_err(cdata->dev,
			"Who-Am-I value not valid. Expected %x, Found %x\n",
						ST_LSM6DSM_WAI_EXP, wai);
		err = -ENODEV;
		goto free_fifo_data;
	}

	st_lsm6dsm_get_properties(cdata);

	if (irq > 0) {
		cdata->irq = irq;
	} else {
		err = -EINVAL;
		dev_info(cdata->dev,
			"DRDY not available, curernt implementation needs irq!\n");
		goto free_fifo_data;
	}

	for (i = 0; i < ST_INDIO_DEV_NUM; i++) {
		cdata->indio_dev[i] = devm_iio_device_alloc(cdata->dev,
						       sizeof(struct lsm6dsm_sensor_data));
		if (!cdata->indio_dev[i]) {
			err = -ENOMEM;
			goto free_fifo_data;
		}

		sdata = iio_priv(cdata->indio_dev[i]);
		sdata->cdata = cdata;
		sdata->sindex = i;

		switch (i) {
		case ST_MASK_ID_ACCEL:
			sdata->data_out_reg = st_lsm6dsm_accel_ch[0].address;
			cdata->v_odr[i] = st_lsm6dsm_odr_table.odr_avl[0].hz;
			sdata->c_gain[0] = st_lsm6dsm_fs_table[i].fs_avl[0].gain;
			sdata->cdata->samples_to_discard_2[ST_MASK_ID_ACCEL] = 0;
			sdata->num_data_channels = 3;
			break;
		case ST_MASK_ID_GYRO:
			sdata->data_out_reg = st_lsm6dsm_gyro_ch[0].address;
			cdata->v_odr[i] = st_lsm6dsm_odr_table.odr_avl[0].hz;
			sdata->c_gain[0] = st_lsm6dsm_fs_table[i].fs_avl[0].gain;
			sdata->cdata->samples_to_discard_2[ST_MASK_ID_GYRO] = 0;
			sdata->num_data_channels = 3;
			break;
		case ST_MASK_ID_STEP_COUNTER:
			sdata->data_out_reg = st_lsm6dsm_step_c_ch[0].address;
			sdata->num_data_channels = 1;
			break;
		case ST_MASK_ID_WTILT:
			sdata->data_out_reg = st_lsm6dsm_wtilt_ch[0].address;
			sdata->num_data_channels = 1;
			break;

		default:
			sdata->num_data_channels = 0;
			break;
		}

		cdata->indio_dev[i]->modes = INDIO_DIRECT_MODE;
	}

	cdata->indio_dev[ST_MASK_ID_ACCEL]->name =
			kasprintf(GFP_KERNEL, "%s_%s", cdata->name,
					ST_LSM6DSM_ACCEL_SUFFIX_NAME);
	cdata->indio_dev[ST_MASK_ID_ACCEL]->info = &st_lsm6dsm_accel_info;
	cdata->indio_dev[ST_MASK_ID_ACCEL]->channels = st_lsm6dsm_accel_ch;
	cdata->indio_dev[ST_MASK_ID_ACCEL]->num_channels =
						ARRAY_SIZE(st_lsm6dsm_accel_ch);

	cdata->indio_dev[ST_MASK_ID_GYRO]->name =
			kasprintf(GFP_KERNEL, "%s_%s", cdata->name,
					ST_LSM6DSM_GYRO_SUFFIX_NAME);
	cdata->indio_dev[ST_MASK_ID_GYRO]->info = &st_lsm6dsm_gyro_info;
	cdata->indio_dev[ST_MASK_ID_GYRO]->channels = st_lsm6dsm_gyro_ch;
	cdata->indio_dev[ST_MASK_ID_GYRO]->num_channels =
						ARRAY_SIZE(st_lsm6dsm_gyro_ch);

	cdata->indio_dev[ST_MASK_ID_SIGN_MOTION]->name =
			kasprintf(GFP_KERNEL, "%s_%s", cdata->name,
					ST_LSM6DSM_SIGN_MOTION_SUFFIX_NAME);
	cdata->indio_dev[ST_MASK_ID_SIGN_MOTION]->info =
						&st_lsm6dsm_sign_motion_info;
	cdata->indio_dev[ST_MASK_ID_SIGN_MOTION]->channels =
						st_lsm6dsm_sign_motion_ch;
	cdata->indio_dev[ST_MASK_ID_SIGN_MOTION]->num_channels =
					ARRAY_SIZE(st_lsm6dsm_sign_motion_ch);

	cdata->indio_dev[ST_MASK_ID_STEP_COUNTER]->name =
			kasprintf(GFP_KERNEL, "%s_%s", cdata->name,
					ST_LSM6DSM_STEP_COUNTER_SUFFIX_NAME);
	cdata->indio_dev[ST_MASK_ID_STEP_COUNTER]->info =
						&st_lsm6dsm_step_c_info;
	cdata->indio_dev[ST_MASK_ID_STEP_COUNTER]->channels =
						st_lsm6dsm_step_c_ch;
	cdata->indio_dev[ST_MASK_ID_STEP_COUNTER]->num_channels =
					ARRAY_SIZE(st_lsm6dsm_step_c_ch);

	cdata->indio_dev[ST_MASK_ID_STEP_DETECTOR]->name =
			kasprintf(GFP_KERNEL, "%s_%s", cdata->name,
					ST_LSM6DSM_STEP_DETECTOR_SUFFIX_NAME);
	cdata->indio_dev[ST_MASK_ID_STEP_DETECTOR]->info =
						&st_lsm6dsm_step_d_info;
	cdata->indio_dev[ST_MASK_ID_STEP_DETECTOR]->channels =
						st_lsm6dsm_step_d_ch;
	cdata->indio_dev[ST_MASK_ID_STEP_DETECTOR]->num_channels =
					ARRAY_SIZE(st_lsm6dsm_step_d_ch);

	cdata->indio_dev[ST_MASK_ID_TILT]->name =
			kasprintf(GFP_KERNEL, "%s_%s", cdata->name,
					ST_LSM6DSM_TILT_SUFFIX_NAME);
	cdata->indio_dev[ST_MASK_ID_TILT]->info = &st_lsm6dsm_tilt_info;
	cdata->indio_dev[ST_MASK_ID_TILT]->channels = st_lsm6dsm_tilt_ch;
	cdata->indio_dev[ST_MASK_ID_TILT]->num_channels =
					ARRAY_SIZE(st_lsm6dsm_tilt_ch);

	cdata->indio_dev[ST_MASK_ID_WTILT]->name =
			kasprintf(GFP_KERNEL, "%s_%s", cdata->name,
					ST_LSM6DSM_WTILT_SUFFIX_NAME);
	cdata->indio_dev[ST_MASK_ID_WTILT]->info = &st_lsm6dsm_wtilt_info;
	cdata->indio_dev[ST_MASK_ID_WTILT]->channels = st_lsm6dsm_wtilt_ch;
	cdata->indio_dev[ST_MASK_ID_WTILT]->num_channels =
					ARRAY_SIZE(st_lsm6dsm_wtilt_ch);

	cdata->indio_dev[ST_MASK_ID_TAP]->name =
			kasprintf(GFP_KERNEL, "%s_%s", cdata->name,
					ST_LSM6DSM_STAP_SUFFIX_NAME);
	cdata->indio_dev[ST_MASK_ID_TAP]->info = &st_lsm6dsm_tap_info;
	cdata->indio_dev[ST_MASK_ID_TAP]->channels = st_lsm6dsm_tap_ch;
	cdata->indio_dev[ST_MASK_ID_TAP]->num_channels =
			ARRAY_SIZE(st_lsm6dsm_tap_ch);

	cdata->indio_dev[ST_MASK_ID_TAP_TAP]->name =
			kasprintf(GFP_KERNEL, "%s_%s", cdata->name,
					ST_LSM6DSM_DTAP_SUFFIX_NAME);
	cdata->indio_dev[ST_MASK_ID_TAP_TAP]->info = &st_lsm6dsm_tap_tap_info;
	cdata->indio_dev[ST_MASK_ID_TAP_TAP]->channels = st_lsm6dsm_tap_tap_ch;
	cdata->indio_dev[ST_MASK_ID_TAP_TAP]->num_channels =
			ARRAY_SIZE(st_lsm6dsm_tap_tap_ch);

	err = st_lsm6dsm_init_sensor(cdata);
	if (err < 0)
		goto free_fifo_data;

	err = st_lsm6dsm_allocate_rings(cdata);
	if (err < 0)
		goto free_fifo_data;

	if (irq > 0) {
		err = st_lsm6dsm_allocate_triggers(cdata,
							ST_LSM6DSM_TRIGGER_OPS);
		if (err < 0)
			goto deallocate_ring;
	}

	for (n = 0; n < ST_INDIO_DEV_NUM; n++) {
		err = iio_device_register(cdata->indio_dev[n]);
		if (err)
			goto iio_device_unregister_and_trigger_deallocate;
	}

	st_lsm6dsm_i2c_master_probe(cdata);

	device_init_wakeup(cdata->dev, true);

	return 0;

iio_device_unregister_and_trigger_deallocate:
	for (n--; n >= 0; n--)
		iio_device_unregister(cdata->indio_dev[n]);

	if (irq > 0)
		st_lsm6dsm_deallocate_triggers(cdata);
deallocate_ring:
	st_lsm6dsm_deallocate_rings(cdata);
free_fifo_data:
	kfree(cdata->fifo_data);

	return err;
}
EXPORT_SYMBOL(st_lsm6dsm_common_probe);

void st_lsm6dsm_common_remove(struct lsm6dsm_data *cdata, int irq)
{
	int i;

	for (i = 0; i < ST_INDIO_DEV_NUM; i++)
		iio_device_unregister(cdata->indio_dev[i]);

	if (irq > 0)
		st_lsm6dsm_deallocate_triggers(cdata);

	st_lsm6dsm_deallocate_rings(cdata);

	kfree(cdata->fifo_data);

	st_lsm6dsm_i2c_master_exit(cdata);
}
EXPORT_SYMBOL(st_lsm6dsm_common_remove);

#ifdef CONFIG_PM
int __maybe_unused st_lsm6dsm_common_suspend(struct lsm6dsm_data *cdata)
{
	int err, i;
	u8 tmp_sensors_enabled;
	struct lsm6dsm_sensor_data *sdata;

	tmp_sensors_enabled = cdata->sensors_enabled;

	for (i = 0; i < ST_INDIO_DEV_NUM; i++) {
		if ((i == ST_MASK_ID_SIGN_MOTION) || (i == ST_MASK_ID_TILT) ||
		    (i == ST_MASK_ID_WTILT))
			continue;

		sdata = iio_priv(cdata->indio_dev[i]);

#ifdef CONFIG_ST_LSM6DSM_STEP_COUNTER_ON_DURING_SUSPEND
		if ((BIT(i) & cdata->sensors_enabled) &&
					(i == ST_MASK_ID_STEP_COUNTER)) {
			err =  st_lsm6dsm_write_data_with_mask(sdata->cdata,
					ST_LSM6DSM_INT2_ADDR,
					ST_LSM6DSM_STEP_COUNTER_DRDY_IRQ_MASK,
					ST_LSM6DSM_DIS_BIT, true);
			if (err < 0)
				return err;

			continue;
		}
#endif /* CONFIG_ST_LSM6DSM_STEP_COUNTER_ON_DURING_SUSPEND */

		err = st_lsm6dsm_set_enable(sdata, false, true);
		if (err < 0)
			return err;
	}
	cdata->sensors_enabled = tmp_sensors_enabled;

	if (cdata->sensors_enabled & ST_LSM6DSM_WAKE_UP_SENSORS) {
		if (device_may_wakeup(cdata->dev))
			enable_irq_wake(cdata->irq);
	}

	return 0;
}
EXPORT_SYMBOL(st_lsm6dsm_common_suspend);

int __maybe_unused st_lsm6dsm_common_resume(struct lsm6dsm_data *cdata)
{
	int err, i;
	struct lsm6dsm_sensor_data *sdata;

	for (i = 0; i < ST_INDIO_DEV_NUM; i++) {
		if ((i == ST_MASK_ID_SIGN_MOTION) || (i == ST_MASK_ID_TILT) ||
			(i == ST_MASK_ID_WTILT))
			continue;

		sdata = iio_priv(cdata->indio_dev[i]);

		if (BIT(sdata->sindex) & cdata->sensors_enabled) {
#ifdef CONFIG_ST_LSM6DSM_STEP_COUNTER_ON_DURING_SUSPEND
		if (i == ST_MASK_ID_STEP_COUNTER) {
			err =  st_lsm6dsm_write_data_with_mask(sdata->cdata,
					ST_LSM6DSM_INT2_ADDR,
					ST_LSM6DSM_STEP_COUNTER_DRDY_IRQ_MASK,
					ST_LSM6DSM_EN_BIT, true);
			if (err < 0)
				return err;

			continue;
		}
#endif /* CONFIG_ST_LSM6DSM_STEP_COUNTER_ON_DURING_SUSPEND */

			err = st_lsm6dsm_set_enable(sdata, true, true);
			if (err < 0)
				return err;
		}
	}

	if (cdata->sensors_enabled & ST_LSM6DSM_WAKE_UP_SENSORS) {
		if (device_may_wakeup(cdata->dev))
			disable_irq_wake(cdata->irq);
	}

	return 0;
}
EXPORT_SYMBOL(st_lsm6dsm_common_resume);
#endif /* CONFIG_PM */

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics lsm6dsm core driver");
MODULE_LICENSE("GPL v2");
