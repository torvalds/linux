/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics ism303dac driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2018 STMicroelectronics Inc.
 */

#ifndef __ISM303DAC_H
#define __ISM303DAC_H

#include <linux/types.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/platform_data/stm/ism303dac.h>
#include <linux/version.h>

#include "../common/stm_iio_types.h"

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
#include <linux/iio/iio-opaque.h>
#endif /* LINUX_VERSION_CODE */

#define ISM303DAC_WHO_AM_I_ADDR			0x0f
#define ISM303DAC_WHO_AM_I_DEF			0x43
#define ISM303DAC_CTRL1_ADDR			0x20
#define ISM303DAC_CTRL2_ADDR			0x21
#define ISM303DAC_CTRL3_ADDR			0x22
#define ISM303DAC_CTRL4_INT1_PAD_ADDR		0x23
#define ISM303DAC_CTRL5_INT2_PAD_ADDR		0x24
#define ISM303DAC_FIFO_CTRL_ADDR		0x25
#define ISM303DAC_OUTX_L_ADDR			0x28
#define ISM303DAC_OUTY_L_ADDR			0x2a
#define ISM303DAC_OUTZ_L_ADDR			0x2c
#define ISM303DAC_TAP_THS_6D_ADDR		0x31
#define ISM303DAC_WAKE_UP_THS_ADDR		0x33
#define ISM303DAC_FREE_FALL_ADDR		0x35
#define ISM303DAC_TAP_SRC_ADDR			0x38
#define ISM303DAC_FUNC_CTRL_ADDR		0x3f
#define ISM303DAC_FIFO_THS_ADDR			0x2e
#define ISM303DAC_FIFO_THS_MASK			0xff
#define ISM303DAC_ODR_ADDR			ISM303DAC_CTRL1_ADDR
#define ISM303DAC_ODR_MASK			0xf0
#define ISM303DAC_ODR_POWER_OFF_VAL		0x00
#define ISM303DAC_ODR_1HZ_LP_VAL		0x08
#define ISM303DAC_ODR_12HZ_LP_VAL		0x09
#define ISM303DAC_ODR_25HZ_LP_VAL		0x0a
#define ISM303DAC_ODR_50HZ_LP_VAL		0x0b
#define ISM303DAC_ODR_100HZ_LP_VAL		0x0c
#define ISM303DAC_ODR_200HZ_LP_VAL		0x0d
#define ISM303DAC_ODR_400HZ_LP_VAL		0x0e
#define ISM303DAC_ODR_800HZ_LP_VAL		0x0f
#define ISM303DAC_ODR_LP_LIST_NUM		9

#define ISM303DAC_ODR_12_5HZ_HR_VAL		0x01
#define ISM303DAC_ODR_25HZ_HR_VAL		0x02
#define ISM303DAC_ODR_50HZ_HR_VAL		0x03
#define ISM303DAC_ODR_100HZ_HR_VAL		0x04
#define ISM303DAC_ODR_200HZ_HR_VAL		0x05
#define ISM303DAC_ODR_400HZ_HR_VAL		0x06
#define ISM303DAC_ODR_800HZ_HR_VAL		0x07
#define ISM303DAC_ODR_HR_LIST_NUM		8

#define ISM303DAC_FS_ADDR			ISM303DAC_CTRL1_ADDR
#define ISM303DAC_FS_MASK			0x0c
#define ISM303DAC_FS_2G_VAL			0x00
#define ISM303DAC_FS_4G_VAL			0x02
#define ISM303DAC_FS_8G_VAL			0x03
#define ISM303DAC_FS_16G_VAL			0x01

/* Advanced Configuration Registers */
#define ISM303DAC_FUNC_CFG_ENTER_ADDR		ISM303DAC_CTRL2_ADDR
#define ISM303DAC_FUNC_CFG_EXIT_ADDR		0x3F
#define ISM303DAC_FUNC_CFG_EN_MASK		0x10

#define ISM303DAC_SIM_ADDR			ISM303DAC_CTRL2_ADDR
#define ISM303DAC_SIM_MASK			0x01
#define ISM303DAC_ADD_INC_MASK			0x04

/* Sensitivity for the 16-bit data */
#define ISM303DAC_FS_2G_GAIN			IIO_G_TO_M_S_2(61)
#define ISM303DAC_FS_4G_GAIN			IIO_G_TO_M_S_2(122)
#define ISM303DAC_FS_8G_GAIN			IIO_G_TO_M_S_2(244)
#define ISM303DAC_FS_16G_GAIN			IIO_G_TO_M_S_2(488)

#define ISM303DAC_MODE_DEFAULT			ISM303DAC_HR_MODE
#define ISM303DAC_INT1_S_TAP_MASK		0x40
#define ISM303DAC_INT1_WAKEUP_MASK		0x20
#define ISM303DAC_INT1_FREE_FALL_MASK		0x10
#define ISM303DAC_INT1_TAP_MASK			0x08
#define ISM303DAC_INT1_6D_MASK			0x04
#define ISM303DAC_INT1_FTH_MASK			0x02
#define ISM303DAC_INT1_DRDY_MASK		0x01
#define ISM303DAC_INT1_EVENTS_MASK		(ISM303DAC_INT1_S_TAP_MASK | \
						 ISM303DAC_INT1_WAKEUP_MASK | \
						 ISM303DAC_INT1_FREE_FALL_MASK | \
						 ISM303DAC_INT1_TAP_MASK | \
						 ISM303DAC_INT1_6D_MASK | \
						 ISM303DAC_INT1_FTH_MASK | \
						 ISM303DAC_INT1_DRDY_MASK)
#define ISM303DAC_INT2_ON_INT1_MASK		0x20
#define ISM303DAC_INT2_FTH_MASK			0x02
#define ISM303DAC_INT2_DRDY_MASK		0x01
#define ISM303DAC_INT2_EVENTS_MASK		(ISM303DAC_INT2_FTH_MASK | \
						 ISM303DAC_INT2_DRDY_MASK)
#define ISM303DAC_WAKE_UP_THS_WU_MASK		0x3f
#define ISM303DAC_WAKE_UP_THS_WU_DEFAULT	0x02
#define ISM303DAC_FREE_FALL_THS_MASK		0x07
#define ISM303DAC_FREE_FALL_DUR_MASK		0xF8
#define ISM303DAC_FREE_FALL_THS_DEFAULT		0x01
#define ISM303DAC_FREE_FALL_DUR_DEFAULT		0x01
#define ISM303DAC_BDU_ADDR			ISM303DAC_CTRL1_ADDR
#define ISM303DAC_BDU_MASK			0x01
#define ISM303DAC_SOFT_RESET_ADDR		ISM303DAC_CTRL2_ADDR
#define ISM303DAC_SOFT_RESET_MASK		0x40
#define ISM303DAC_LIR_ADDR			ISM303DAC_CTRL3_ADDR
#define ISM303DAC_LIR_MASK			0x04
#define ISM303DAC_TAP_AXIS_ADDR			ISM303DAC_CTRL3_ADDR
#define ISM303DAC_TAP_AXIS_MASK			0x38
#define ISM303DAC_TAP_AXIS_ANABLE_ALL		0x07
#define ISM303DAC_TAP_THS_ADDR			ISM303DAC_TAP_THS_6D_ADDR
#define ISM303DAC_TAP_THS_MASK			0x1f
#define ISM303DAC_TAP_THS_DEFAULT		0x09
#define ISM303DAC_INT2_ON_INT1_ADDR		ISM303DAC_CTRL5_INT2_PAD_ADDR
#define ISM303DAC_INT2_ON_INT1_MASK		0x20
#define ISM303DAC_FIFO_MODE_ADDR		ISM303DAC_FIFO_CTRL_ADDR
#define ISM303DAC_FIFO_MODE_MASK		0xe0
#define ISM303DAC_FIFO_MODE_BYPASS		0x00
#define ISM303DAC_FIFO_MODE_CONTINUOS		0x06
#define ISM303DAC_OUT_XYZ_SIZE			8

#define ISM303DAC_SELFTEST_ADDR			ISM303DAC_CTRL3_ADDR
#define ISM303DAC_SELFTEST_MASK			0xc0
#define ISM303DAC_SELFTEST_NORMAL		0x00
#define ISM303DAC_SELFTEST_POS_SIGN		0x01
#define ISM303DAC_SELFTEST_NEG_SIGN		0x02

#define ISM303DAC_FIFO_SRC			0x2f
#define ISM303DAC_FIFO_SRC_DIFF_MASK		0x20

#define ISM303DAC_FIFO_NUM_AXIS			3
#define ISM303DAC_FIFO_BYTE_X_AXIS		2
#define ISM303DAC_FIFO_BYTE_FOR_SAMPLE		(ISM303DAC_FIFO_NUM_AXIS * \
						 ISM303DAC_FIFO_BYTE_X_AXIS)
#define ISM303DAC_TIMESTAMP_SIZE		8

#define ISM303DAC_STATUS_ADDR			0x27
#define ISM303DAC_STATUS_DUP_ADDR		0x36
#define ISM303DAC_WAKE_UP_IA_MASK		0x40
#define ISM303DAC_DOUBLE_TAP_MASK		0x10
#define ISM303DAC_TAP_MASK			0x08
#define ISM303DAC_6D_IA_MASK			0x04
#define ISM303DAC_FF_IA_MASK			0x02
#define ISM303DAC_DRDY_MASK			0x01
#define ISM303DAC_EVENT_MASK			(ISM303DAC_WAKE_UP_IA_MASK | \
						 ISM303DAC_DOUBLE_TAP_MASK | \
						 ISM303DAC_TAP_MASK | \
						 ISM303DAC_6D_IA_MASK | \
						 ISM303DAC_FF_IA_MASK)
#define ISM303DAC_FIFO_SRC_ADDR			0x2f
#define ISM303DAC_FIFO_SRC_FTH_MASK		0x80

#define ISM303DAC_EN_BIT			0x01
#define ISM303DAC_DIS_BIT			0x00
#define ISM303DAC_ACCEL_ODR			1
#define ISM303DAC_DEFAULT_ACCEL_FS		2
#define ISM303DAC_FF_ODR			25
#define ISM303DAC_TAP_ODR			400
#define ISM303DAC_WAKEUP_ODR			25
#define ISM303DAC_ACTIVITY_ODR			12
#define ISM303DAC_MAX_FIFO_LENGHT		256
#define ISM303DAC_MAX_FIFO_THS			(ISM303DAC_MAX_FIFO_LENGHT - 1)
#define ISM303DAC_MAX_CHANNEL_SPEC		5
#define ISM303DAC_EVENT_CHANNEL_SPEC_SIZE	2
#define ISM303DAC_MIN_DURATION_MS		1638

#define ISM303DAC_DEV_NAME			"ism303dac_accel"
#define SET_BIT(a, b)				{a |= (1 << b);}
#define RESET_BIT(a, b)				{a &= ~(1 << b);}
#define CHECK_BIT(a, b)				(a & (1 << b))

enum {
	ISM303DAC_ACCEL = 0,
	ISM303DAC_TAP,
	ISM303DAC_DOUBLE_TAP,
	ISM303DAC_SENSORS_NUMB,
};

#define ST_ISM303DAC_FLUSH_CHANNEL(device_type) \
{ \
	.type = device_type, \
	.modified = 0, \
	.scan_index = -1, \
	.indexed = -1, \
	.event_spec = &ism303dac_fifo_flush_event,\
	.num_event_specs = 1, \
}

#define ST_ISM303DAC_HWFIFO_ENABLED() \
	IIO_DEVICE_ATTR(hwfifo_enabled, S_IWUSR | S_IRUGO, \
			ism303dac_sysfs_get_hwfifo_enabled,\
			ism303dac_sysfs_set_hwfifo_enabled, 0);

#define ST_ISM303DAC_HWFIFO_WATERMARK() \
	IIO_DEVICE_ATTR(hwfifo_watermark, S_IWUSR | S_IRUGO, \
			ism303dac_sysfs_get_hwfifo_watermark,\
			ism303dac_sysfs_set_hwfifo_watermark, 0);

#define ST_ISM303DAC_HWFIFO_WATERMARK_MIN() \
	IIO_DEVICE_ATTR(hwfifo_watermark_min, S_IRUGO, \
			ism303dac_sysfs_get_hwfifo_watermark_min, NULL, 0);

#define ST_ISM303DAC_HWFIFO_WATERMARK_MAX() \
	IIO_DEVICE_ATTR(hwfifo_watermark_max, S_IRUGO, \
			ism303dac_sysfs_get_hwfifo_watermark_max, NULL, 0);

#define ST_ISM303DAC_HWFIFO_FLUSH() \
	IIO_DEVICE_ATTR(hwfifo_flush, S_IWUSR, NULL, \
			ism303dac_sysfs_flush_fifo, 0);

enum fifo_mode {
	BYPASS = 0,
	CONTINUOS,
};

#define ISM303DAC_TX_MAX_LENGTH			12
#define ISM303DAC_RX_MAX_LENGTH			8193
#define ISM303DAC_EWMA_DIV			128

struct ism303dac_transfer_buffer {
	struct mutex buf_lock;
	u8 rx_buf[ISM303DAC_RX_MAX_LENGTH];
	u8 tx_buf[ISM303DAC_TX_MAX_LENGTH] ____cacheline_aligned;
};

struct ism303dac_data;

struct ism303dac_transfer_function {
	int (*write)(struct ism303dac_data *cdata, u8 reg_addr, int len,
		     u8 *data, bool b_lock);
	int (*read)(struct ism303dac_data *cdata, u8 reg_addr, int len,
		    u8 *data, bool b_lock);
};

struct ism303dac_sensor_data {
	struct ism303dac_data *cdata;
	const char *name;
	s64 timestamp;
	u8 enabled;
	u32 odr;
	u32 gain;
	u8 sindex;
	u8 sample_to_discard;
};

struct ism303dac_data {
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
	struct iio_dev *iio_sensors_dev[ISM303DAC_SENSORS_NUMB];
	struct iio_trigger *iio_trig[ISM303DAC_SENSORS_NUMB];
	struct mutex regs_lock;
	const struct ism303dac_transfer_function *tf;
	struct ism303dac_transfer_buffer tb;
};

static inline s64 ism303dac_get_time_ns(struct iio_dev *iio_sensors_dev)
{
	return iio_get_time_ns(iio_sensors_dev);
}

static inline int ism303dac_iio_dev_currentmode(struct iio_dev *indio_dev)
{

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
	struct iio_dev_opaque *iio_opq = to_iio_dev_opaque(indio_dev);

	return iio_opq->currentmode;
#else /* LINUX_VERSION_CODE */
	return indio_dev->currentmode;
#endif /* LINUX_VERSION_CODE */

}

int ism303dac_common_probe(struct ism303dac_data *cdata, int irq);
#ifdef CONFIG_PM
int ism303dac_common_suspend(struct ism303dac_data *cdata);
int ism303dac_common_resume(struct ism303dac_data *cdata);
#endif
int ism303dac_allocate_rings(struct ism303dac_data *cdata);
int ism303dac_allocate_triggers(struct ism303dac_data *cdata,
			     const struct iio_trigger_ops *trigger_ops);
int ism303dac_trig_set_state(struct iio_trigger *trig, bool state);
int ism303dac_read_register(struct ism303dac_data *cdata, u8 reg_addr,
			    int data_len, u8 *data, bool b_lock);
int ism303dac_update_drdy_irq(struct ism303dac_sensor_data *sdata, bool state);
int ism303dac_set_enable(struct ism303dac_sensor_data *sdata, bool enable);
void ism303dac_common_remove(struct ism303dac_data *cdata, int irq);
void ism303dac_read_xyz(struct ism303dac_data *cdata);
void ism303dac_read_fifo(struct ism303dac_data *cdata, bool check_fifo_len);
void ism303dac_deallocate_rings(struct ism303dac_data *cdata);
void ism303dac_deallocate_triggers(struct ism303dac_data *cdata);

#endif /* __ISM303DAC_H */
