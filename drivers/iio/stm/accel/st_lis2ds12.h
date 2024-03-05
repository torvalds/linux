/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics lis2ds12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2015 STMicroelectronics Inc.
 */

#ifndef __LIS2DS12_H
#define __LIS2DS12_H

#include <linux/types.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/platform_data/stm/lis2ds12.h>
#include <linux/version.h>

#include "../common/stm_iio_types.h"

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
#include <linux/iio/iio-opaque.h>
#endif /* LINUX_VERSION_CODE */

#define LIS2DS12_WHO_AM_I_ADDR			0x0f
#define LIS2DS12_WHO_AM_I_DEF			0x43
#define LIS2DS12_CTRL1_ADDR			0x20
#define LIS2DS12_CTRL2_ADDR			0x21
#define LIS2DS12_CTRL3_ADDR			0x22
#define LIS2DS12_CTRL4_INT1_PAD_ADDR		0x23
#define LIS2DS12_CTRL5_INT2_PAD_ADDR		0x24
#define LIS2DS12_FIFO_CTRL_ADDR			0x25
#define LIS2DS12_OUTX_L_ADDR			0x28
#define LIS2DS12_OUTY_L_ADDR			0x2a
#define LIS2DS12_OUTZ_L_ADDR			0x2c
#define LIS2DS12_TAP_THS_6D_ADDR		0x31
#define LIS2DS12_WAKE_UP_THS_ADDR		0x33
#define LIS2DS12_FREE_FALL_ADDR			0x35
#define LIS2DS12_STEP_C_MINTHS_ADDR		0x3a
#define LIS2DS12_STEP_C_MINTHS_RST_NSTEP_MASK	0x80
#define LIS2DS12_STEP_C_OUT_L_ADDR		0x3b
#define LIS2DS12_FUNC_CTRL_ADDR			0x3f
#define LIS2DS12_FUNC_CTRL_TILT_MASK		0x10
#define LIS2DS12_FUNC_CTRL_SIGN_MOT_MASK	0x02
#define LIS2DS12_FUNC_CTRL_STEP_CNT_MASK	0x01
#define LIS2DS12_FUNC_CTRL_EV_MASK		(LIS2DS12_FUNC_CTRL_TILT_MASK | \
						LIS2DS12_FUNC_CTRL_SIGN_MOT_MASK | \
						LIS2DS12_FUNC_CTRL_STEP_CNT_MASK)
#define LIS2DS12_FIFO_THS_ADDR			0x2e
#define LIS2DS12_FIFO_THS_MASK			0xff
#define LIS2DS12_ODR_ADDR			LIS2DS12_CTRL1_ADDR
#define LIS2DS12_ODR_MASK			0xf0
#define LIS2DS12_ODR_POWER_OFF_VAL		0x00
#define LIS2DS12_ODR_1HZ_LP_VAL			0x08
#define LIS2DS12_ODR_12HZ_LP_VAL		0x09
#define LIS2DS12_ODR_25HZ_LP_VAL		0x0a
#define LIS2DS12_ODR_50HZ_LP_VAL		0x0b
#define LIS2DS12_ODR_100HZ_LP_VAL		0x0c
#define LIS2DS12_ODR_200HZ_LP_VAL		0x0d
#define LIS2DS12_ODR_400HZ_LP_VAL		0x0e
#define LIS2DS12_ODR_800HZ_LP_VAL		0x0f
#define LIS2DS12_ODR_LP_LIST_NUM		9

#define LIS2DS12_ODR_12_5HZ_HR_VAL		0x01
#define LIS2DS12_ODR_25HZ_HR_VAL		0x02
#define LIS2DS12_ODR_50HZ_HR_VAL		0x03
#define LIS2DS12_ODR_100HZ_HR_VAL		0x04
#define LIS2DS12_ODR_200HZ_HR_VAL		0x05
#define LIS2DS12_ODR_400HZ_HR_VAL		0x06
#define LIS2DS12_ODR_800HZ_HR_VAL		0x07
#define LIS2DS12_ODR_HR_LIST_NUM		8

#define LIS2DS12_FS_ADDR			LIS2DS12_CTRL1_ADDR
#define LIS2DS12_FS_MASK			0x0c
#define LIS2DS12_FS_2G_VAL			0x00
#define LIS2DS12_FS_4G_VAL			0x02
#define LIS2DS12_FS_8G_VAL			0x03
#define LIS2DS12_FS_16G_VAL			0x01

/* Advanced Configuration Registers */
#define LIS2DS12_FUNC_CFG_ENTER_ADDR		LIS2DS12_CTRL2_ADDR
#define LIS2DS12_FUNC_CFG_EXIT_ADDR			0x3F
#define LIS2DS12_FUNC_CFG_EN_MASK			0x10
#define LIS2DS12_STEP_COUNT_DELTA			0x3A

#define LIS2DS12_SIM_ADDR			LIS2DS12_CTRL2_ADDR
#define LIS2DS12_SIM_MASK			0x01
#define LIS2DS12_ADD_INC_MASK			0x04

/*
 * Sensitivity for the 16-bit data
 */
#define LIS2DS12_FS_2G_GAIN			IIO_G_TO_M_S_2(61)
#define LIS2DS12_FS_4G_GAIN			IIO_G_TO_M_S_2(122)
#define LIS2DS12_FS_8G_GAIN			IIO_G_TO_M_S_2(244)
#define LIS2DS12_FS_16G_GAIN		IIO_G_TO_M_S_2(488)

#define LIS2DS12_MODE_DEFAULT			LIS2DS12_HR_MODE
#define LIS2DS12_INT1_S_TAP_MASK		0x40
#define LIS2DS12_INT1_WAKEUP_MASK		0x20
#define LIS2DS12_INT1_FREE_FALL_MASK		0x10
#define LIS2DS12_INT1_TAP_MASK			0x08
#define LIS2DS12_INT1_6D_MASK			0x04
#define LIS2DS12_INT1_FTH_MASK			0x02
#define LIS2DS12_INT1_DRDY_MASK			0x01
#define LIS2DS12_INT1_EVENTS_MASK		(LIS2DS12_INT1_S_TAP_MASK | \
						LIS2DS12_INT1_WAKEUP_MASK | \
						LIS2DS12_INT1_FREE_FALL_MASK | \
						LIS2DS12_INT1_TAP_MASK | \
						LIS2DS12_INT1_6D_MASK | \
						LIS2DS12_INT1_FTH_MASK | \
						LIS2DS12_INT1_DRDY_MASK)
#define LIS2DS12_INT2_ON_INT1_MASK		0x20
#define LIS2DS12_INT2_TILT_MASK			0x10
#define LIS2DS12_INT2_SIG_MOT_DET_MASK		0x08
#define LIS2DS12_INT2_STEP_DET_MASK		0x04
#define LIS2DS12_INT2_FTH_MASK			0x02
#define LIS2DS12_INT2_DRDY_MASK			0x01
#define LIS2DS12_INT2_EVENTS_MASK		(LIS2DS12_INT2_TILT_MASK | \
						LIS2DS12_INT2_SIG_MOT_DET_MASK | \
						LIS2DS12_INT2_STEP_DET_MASK | \
						LIS2DS12_INT2_FTH_MASK | \
						LIS2DS12_INT2_DRDY_MASK)
#define LIS2DS12_WAKE_UP_THS_WU_MASK		0x3f
#define LIS2DS12_WAKE_UP_THS_WU_DEFAULT		0x02
#define LIS2DS12_FREE_FALL_THS_MASK		0x07
#define LIS2DS12_FREE_FALL_DUR_MASK		0xF8
#define LIS2DS12_FREE_FALL_THS_DEFAULT		0x01
#define LIS2DS12_FREE_FALL_DUR_DEFAULT		0x01
#define LIS2DS12_BDU_ADDR			LIS2DS12_CTRL1_ADDR
#define LIS2DS12_BDU_MASK			0x01
#define LIS2DS12_SOFT_RESET_ADDR		LIS2DS12_CTRL2_ADDR
#define LIS2DS12_SOFT_RESET_MASK		0x40
#define LIS2DS12_LIR_ADDR			LIS2DS12_CTRL3_ADDR
#define LIS2DS12_LIR_MASK			0x04
#define LIS2DS12_TAP_AXIS_ADDR			LIS2DS12_CTRL3_ADDR
#define LIS2DS12_TAP_AXIS_MASK			0x38
#define LIS2DS12_TAP_AXIS_ANABLE_ALL		0x07
#define LIS2DS12_TAP_THS_ADDR			LIS2DS12_TAP_THS_6D_ADDR
#define LIS2DS12_TAP_THS_MASK			0x1f
#define LIS2DS12_TAP_THS_DEFAULT		0x09
#define LIS2DS12_INT2_ON_INT1_ADDR		LIS2DS12_CTRL5_INT2_PAD_ADDR
#define LIS2DS12_INT2_ON_INT1_MASK		0x20
#define LIS2DS12_FIFO_MODE_ADDR			LIS2DS12_FIFO_CTRL_ADDR
#define LIS2DS12_FIFO_MODE_MASK			0xe0
#define LIS2DS12_FIFO_MODE_BYPASS		0x00
#define LIS2DS12_FIFO_MODE_CONTINUOS		0x06
#define LIS2DS12_OUT_XYZ_SIZE			8

#define LIS2DS12_SELFTEST_ADDR			LIS2DS12_CTRL3_ADDR
#define LIS2DS12_SELFTEST_MASK			0xc0
#define LIS2DS12_SELFTEST_NORMAL		0x00
#define LIS2DS12_SELFTEST_POS_SIGN		0x01
#define LIS2DS12_SELFTEST_NEG_SIGN		0x02

#define LIS2DS12_FIFO_SRC			0x2f
#define LIS2DS12_FIFO_SRC_DIFF_MASK		0x20

#define LIS2DS12_FIFO_NUM_AXIS			3
#define LIS2DS12_FIFO_BYTE_X_AXIS		2
#define LIS2DS12_FIFO_BYTE_FOR_SAMPLE		(LIS2DS12_FIFO_NUM_AXIS * \
						LIS2DS12_FIFO_BYTE_X_AXIS)
#define LIS2DS12_TIMESTAMP_SIZE			8

#define LIS2DS12_STATUS_ADDR			0x27
#define LIS2DS12_STATUS_DUP_ADDR		0x36
#define LIS2DS12_FUNC_CK_GATE_ADDR		0x3d
#define LIS2DS12_FUNC_CK_GATE_TILT_INT_MASK	0x80
#define LIS2DS12_FUNC_CK_GATE_SIGN_M_DET_MASK	0x10
#define LIS2DS12_FUNC_CK_GATE_RST_SIGN_M_MASK	0x08
#define LIS2DS12_FUNC_CK_GATE_RST_PEDO_MASK	0x04
#define LIS2DS12_FUNC_CK_GATE_STEP_D_MASK	0x02
#define LIS2DS12_FUNC_CK_GATE_MASK		(LIS2DS12_FUNC_CK_GATE_TILT_INT_MASK | \
						LIS2DS12_FUNC_CK_GATE_SIGN_M_DET_MASK | \
						LIS2DS12_FUNC_CK_GATE_STEP_D_MASK)
#define LIS2DS12_WAKE_UP_IA_MASK		0x40
#define LIS2DS12_DOUBLE_TAP_MASK		0x10
#define LIS2DS12_TAP_MASK			0x08
#define LIS2DS12_6D_IA_MASK			0x04
#define LIS2DS12_FF_IA_MASK			0x02
#define LIS2DS12_DRDY_MASK			0x01
#define LIS2DS12_EVENT_MASK			(LIS2DS12_WAKE_UP_IA_MASK | \
						LIS2DS12_DOUBLE_TAP_MASK | \
						LIS2DS12_TAP_MASK | \
						LIS2DS12_6D_IA_MASK | \
						LIS2DS12_FF_IA_MASK)
#define LIS2DS12_FIFO_SRC_ADDR			0x2f
#define LIS2DS12_FIFO_SRC_FTH_MASK		0x80

#define LIS2DS12_EN_BIT				0x01
#define LIS2DS12_DIS_BIT			0x00
#define LIS2DS12_ACCEL_ODR			1
#define LIS2DS12_DEFAULT_ACCEL_FS		2
#define LIS2DS12_FF_ODR				25
#define LIS2DS12_STEP_D_ODR			25
#define LIS2DS12_TILT_ODR			25
#define LIS2DS12_SIGN_M_ODR			25
#define LIS2DS12_TAP_ODR			400
#define LIS2DS12_WAKEUP_ODR			25
#define LIS2DS12_ACTIVITY_ODR			12
#define LIS2DS12_MAX_FIFO_LENGHT		256
#define LIS2DS12_MAX_FIFO_THS		(LIS2DS12_MAX_FIFO_LENGHT - 1)
#define LIS2DS12_MAX_CHANNEL_SPEC		5
#define LIS2DS12_EVENT_CHANNEL_SPEC_SIZE	2
#define LIS2DS12_MIN_DURATION_MS		1638

#define LIS2DS12_DEV_NAME			"lis2ds12"
#define SET_BIT(a, b)				{a |= (1 << b);}
#define RESET_BIT(a, b)				{a &= ~(1 << b);}
#define CHECK_BIT(a, b)				(a & (1 << b))

enum {
	LIS2DS12_ACCEL = 0,
	LIS2DS12_STEP_C,
	LIS2DS12_TAP,
	LIS2DS12_DOUBLE_TAP,
	LIS2DS12_STEP_D,
	LIS2DS12_TILT,
	LIS2DS12_SIGN_M,
	LIS2DS12_SENSORS_NUMB,
};

#define ST_LIS2DS12_FLUSH_CHANNEL(device_type) \
{ \
	.type = device_type, \
	.modified = 0, \
	.scan_index = -1, \
	.indexed = -1, \
	.event_spec = &lis2ds12_fifo_flush_event,\
	.num_event_specs = 1, \
}

#define ST_LIS2DS12_HWFIFO_ENABLED() \
	IIO_DEVICE_ATTR(hwfifo_enabled, S_IWUSR | S_IRUGO, \
			lis2ds12_sysfs_get_hwfifo_enabled,\
			lis2ds12_sysfs_set_hwfifo_enabled, 0);

#define ST_LIS2DS12_HWFIFO_WATERMARK() \
	IIO_DEVICE_ATTR(hwfifo_watermark, S_IWUSR | S_IRUGO, \
			lis2ds12_sysfs_get_hwfifo_watermark,\
			lis2ds12_sysfs_set_hwfifo_watermark, 0);

#define ST_LIS2DS12_HWFIFO_WATERMARK_MIN() \
	IIO_DEVICE_ATTR(hwfifo_watermark_min, S_IRUGO, \
			lis2ds12_sysfs_get_hwfifo_watermark_min, NULL, 0);

#define ST_LIS2DS12_HWFIFO_WATERMARK_MAX() \
	IIO_DEVICE_ATTR(hwfifo_watermark_max, S_IRUGO, \
			lis2ds12_sysfs_get_hwfifo_watermark_max, NULL, 0);

#define ST_LIS2DS12_HWFIFO_FLUSH() \
	IIO_DEVICE_ATTR(hwfifo_flush, S_IWUSR, NULL, \
			lis2ds12_sysfs_flush_fifo, 0);

enum fifo_mode {
	BYPASS = 0,
	CONTINUOS,
};

#define LIS2DS12_TX_MAX_LENGTH			12
#define LIS2DS12_RX_MAX_LENGTH			8193
#define LIS2DS12_EWMA_DIV			128

struct lis2ds12_transfer_buffer {
	struct mutex buf_lock;
	u8 rx_buf[LIS2DS12_RX_MAX_LENGTH];
	u8 tx_buf[LIS2DS12_TX_MAX_LENGTH] ____cacheline_aligned;
};

struct lis2ds12_data;

struct lis2ds12_transfer_function {
	int (*write)(struct lis2ds12_data *cdata, u8 reg_addr, int len,
								u8 *data, bool b_lock);
	int (*read)(struct lis2ds12_data *cdata, u8 reg_addr, int len,
								u8 *data, bool b_lock);
};

struct lis2ds12_sensor_data {
	struct lis2ds12_data *cdata;
	const char *name;
	s64 timestamp;
	u8 enabled;
	u32 odr;
	u32 gain;
	u8 sindex;
	u8 sample_to_discard;
};

struct lis2ds12_data {
	const char *name;
	u8 drdy_int_pin;
	bool spi_3wire;
	u8 selftest_status;
	u8 hwfifo_enabled;
	u8 hwfifo_watermark;
	u8 power_mode;
	u8 enabled_sensor;
	u32 common_odr;
	int irq;
	s64 timestamp;
	s64 accel_deltatime;
	s64 sample_timestamp;
	u8 *fifo_data;
	u16 fifo_size;
	u64 samples;
	u8 std_level;
	struct mutex fifo_lock;
	struct device *dev;
	struct iio_dev *iio_sensors_dev[LIS2DS12_SENSORS_NUMB];
	struct iio_trigger *iio_trig[LIS2DS12_SENSORS_NUMB];
	struct mutex regs_lock;
	const struct lis2ds12_transfer_function *tf;
	struct lis2ds12_transfer_buffer tb;
};

static inline s64 lis2ds12_get_time_ns(struct iio_dev *iio_dev)
{
	return iio_get_time_ns(iio_dev);
}

static inline int lis2ds12_iio_dev_currentmode(struct iio_dev *indio_dev)
{

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
	struct iio_dev_opaque *iio_opq = to_iio_dev_opaque(indio_dev);

	return iio_opq->currentmode;
#else /* LINUX_VERSION_CODE */
	return indio_dev->currentmode;
#endif /* LINUX_VERSION_CODE */

}

int lis2ds12_common_probe(struct lis2ds12_data *cdata, int irq);
#ifdef CONFIG_PM
int lis2ds12_common_suspend(struct lis2ds12_data *cdata);
int lis2ds12_common_resume(struct lis2ds12_data *cdata);
#endif
int lis2ds12_allocate_rings(struct lis2ds12_data *cdata);
int lis2ds12_allocate_triggers(struct lis2ds12_data *cdata,
			     const struct iio_trigger_ops *trigger_ops);
int lis2ds12_trig_set_state(struct iio_trigger *trig, bool state);
int lis2ds12_read_register(struct lis2ds12_data *cdata, u8 reg_addr,
							int data_len, u8 *data, bool b_lock);
int lis2ds12_update_drdy_irq(struct lis2ds12_sensor_data *sdata, bool state);
int lis2ds12_set_enable(struct lis2ds12_sensor_data *sdata, bool enable);
int lis2ds12_update_fifo(struct lis2ds12_data *cdata, u16 watermark);
void lis2ds12_common_remove(struct lis2ds12_data *cdata, int irq);
void lis2ds12_read_xyz(struct lis2ds12_data *cdata);
void lis2ds12_read_fifo(struct lis2ds12_data *cdata, bool check_fifo_len);
void lis2ds12_read_step_c(struct lis2ds12_data *cdata);
void lis2ds12_deallocate_rings(struct lis2ds12_data *cdata);
void lis2ds12_deallocate_triggers(struct lis2ds12_data *cdata);

#endif /* __LIS2DS12_H */
