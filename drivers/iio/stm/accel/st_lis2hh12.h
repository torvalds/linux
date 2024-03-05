/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics lis2hh12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#ifndef __LIS2HH12_H
#define __LIS2HH12_H

#include <linux/types.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/version.h>

#include "../common/stm_iio_types.h"

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
#include <linux/iio/iio-opaque.h>
#endif /* LINUX_VERSION_CODE */

#define LIS2HH12_WHO_AM_I_ADDR			0x0f
#define LIS2HH12_WHO_AM_I_DEF			0x41

#define LIS2HH12_CTRL1_ADDR			0x20
#define LIS2HH12_CTRL2_ADDR			0x21
#define LIS2HH12_CTRL3_ADDR			0x22
#define LIS2HH12_CTRL4_ADDR			0x23
#define LIS2HH12_CTRL5_ADDR			0x24
#define LIS2HH12_CTRL6_ADDR			0x25
#define LIS2HH12_CTRL7_ADDR			0x26

#define LIS2HH12_STATUS_ADDR		0x27
#define LIS2HH12_DATA_XYZ_RDY		0x08

#define LIS2HH12_FIFO_CTRL_ADDR			0x2E

#define LIS2HH12_OUTX_L_ADDR		0x28
#define LIS2HH12_OUTY_L_ADDR		0x2A
#define LIS2HH12_OUTZ_L_ADDR		0x2C

#define LIS2HH12_FIFO_THS_ADDR			LIS2HH12_FIFO_CTRL_ADDR
#define LIS2HH12_FIFO_THS_MASK			0x1f

#define LIS2HH12_FIFO_MODE_ADDR			LIS2HH12_FIFO_CTRL_ADDR
#define LIS2HH12_FIFO_MODE_MASK			0xe0
#define LIS2HH12_FIFO_MODE_BYPASS		0x00
#define LIS2HH12_FIFO_MODE_STREAM		0x02

#define LIS2HH12_FIFO_SRC_ADDR			0x2F
#define LIS2HH12_FIFO_STATUS_ADDR		LIS2HH12_FIFO_SRC_ADDR
#define LIS2HH12_FIFO_FSS_MASK			0x1F
#define LIS2HH12_FIFO_SRC_FTH_MASK		0x80

#define LIS2HH12_ODR_ADDR			LIS2HH12_CTRL1_ADDR
#define LIS2HH12_ODR_MASK			0x70
#define LIS2HH12_ODR_POWER_DOWN_VAL		0x00
#define LIS2HH12_ODR_10HZ_VAL			0x01
#define LIS2HH12_ODR_50HZ_VAL			0x02
#define LIS2HH12_ODR_100HZ_VAL			0x03
#define LIS2HH12_ODR_200HZ_VAL			0x04
#define LIS2HH12_ODR_400HZ_VAL			0x05
#define LIS2HH12_ODR_800HZ_VAL			0x06
#define LIS2HH12_ODR_LIST_NUM			7

#define LIS2HH12_FS_ADDR			LIS2HH12_CTRL4_ADDR
#define LIS2HH12_FS_MASK			0x30
#define LIS2HH12_FS_2G_VAL			0x00
#define LIS2HH12_FS_4G_VAL			0x02
#define LIS2HH12_FS_8G_VAL			0x03
#define LIS2HH12_FS_LIST_NUM		3

#define LIS2HH12_FS_2G_GAIN			IIO_G_TO_M_S_2(61)
#define LIS2HH12_FS_4G_GAIN			IIO_G_TO_M_S_2(122)
#define LIS2HH12_FS_8G_GAIN			IIO_G_TO_M_S_2(244)

#define LIS2HH12_INT_CFG_ADDR			LIS2HH12_CTRL3_ADDR
#define LIS2HH12_INT_DRDY_MASK			0x01
#define LIS2HH12_INT_FTH_MASK			0x02
#define LIS2HH12_INT_FOVR_MASK			0x04

#define LIS2HH12_FIFO_EN_ADDR			LIS2HH12_CTRL3_ADDR
#define LIS2HH12_FIFO_EN_MASK			0x80

#define LIS2HH12_BDU_ADDR			LIS2HH12_CTRL1_ADDR
#define LIS2HH12_BDU_MASK			0x08
#define LIS2HH12_SOFT_RESET_ADDR	LIS2HH12_CTRL5_ADDR
#define LIS2HH12_SOFT_RESET_MASK	0x40
#define LIS2HH12_LIR_ADDR			LIS2HH12_CTRL7_ADDR
#define LIS2HH12_LIR1_MASK			0x04
#define LIS2HH12_LIR2_MASK			0x08

#define LIS2HH12_SELF_TEST_ADDR			LIS2HH12_CTRL5_ADDR
#define LIS2HH12_ST_MASK			0x0c

#define LIS2HH12_MAX_FIFO_LENGHT		32
#define LIS2HH12_MAX_FIFO_THS			(LIS2HH12_MAX_FIFO_LENGHT - 1)
#define LIS2HH12_FIFO_NUM_AXIS			3
#define LIS2HH12_FIFO_BYTE_X_AXIS		2
#define LIS2HH12_FIFO_BYTE_FOR_SAMPLE	(LIS2HH12_FIFO_NUM_AXIS * \
											LIS2HH12_FIFO_BYTE_X_AXIS)
#define LIS2HH12_TIMESTAMP_SIZE			8

#define LIS2HH12_EN_BIT				0x01
#define LIS2HH12_DIS_BIT				0x00

#define LIS2HH12_MAX_CHANNEL_SPEC		5

#define LIS2HH12_ACCEL				0
#define LIS2HH12_SENSORS_NUMB		1

#define LIS2HH12_DEV_NAME			"lis2hh12"
#define SET_BIT(a, b)				{a |= (1 << b);}
#define RESET_BIT(a, b)				{a &= ~(1 << b);}
#define CHECK_BIT(a, b)				(a & (1 << b))

#define LIS2HH12_DATA_SIZE			6

#define LIS2HH12_SELFTEST_MIN		1147
#define LIS2HH12_SELFTEST_MAX		24590

#define ST_LIS2HH12_FLUSH_CHANNEL(device_type) \
{ \
	.type = device_type, \
	.modified = 0, \
	.scan_index = -1, \
	.indexed = -1, \
	.event_spec = &lis2hh12_fifo_flush_event,\
	.num_event_specs = 1, \
}

#define ST_LIS2HH12_HWFIFO_ENABLED() \
	IIO_DEVICE_ATTR(hwfifo_enabled, S_IWUSR | S_IRUGO, \
			lis2hh12_sysfs_get_hwfifo_enabled,\
			lis2hh12_sysfs_set_hwfifo_enabled, 0);

#define ST_LIS2HH12_HWFIFO_WATERMARK() \
	IIO_DEVICE_ATTR(hwfifo_watermark, S_IWUSR | S_IRUGO, \
			lis2hh12_sysfs_get_hwfifo_watermark,\
			lis2hh12_sysfs_set_hwfifo_watermark, 0);

#define ST_LIS2HH12_HWFIFO_WATERMARK_MIN() \
	IIO_DEVICE_ATTR(hwfifo_watermark_min, S_IRUGO, \
			lis2hh12_sysfs_get_hwfifo_watermark_min, NULL, 0);

#define ST_LIS2HH12_HWFIFO_WATERMARK_MAX() \
	IIO_DEVICE_ATTR(hwfifo_watermark_max, S_IRUGO, \
			lis2hh12_sysfs_get_hwfifo_watermark_max, NULL, 0);

#define ST_LIS2HH12_HWFIFO_FLUSH() \
	IIO_DEVICE_ATTR(hwfifo_flush, S_IWUSR, NULL, \
			lis2hh12_sysfs_flush_fifo, 0);

enum fifo_mode {
	BYPASS = 0,
	STREAM,
};

enum lis2hh12_selftest_status {
	LIS2HH12_ST_RESET,
	LIS2HH12_ST_PASS,
	LIS2HH12_ST_FAIL,
};

#define LIS2HH12_TX_MAX_LENGTH		12
#define LIS2HH12_RX_MAX_LENGTH		8193

struct lis2hh12_transfer_buffer {
	struct mutex buf_lock;
	u8 rx_buf[LIS2HH12_RX_MAX_LENGTH];
	u8 tx_buf[LIS2HH12_TX_MAX_LENGTH] ____cacheline_aligned;
};

struct lis2hh12_data;

struct lis2hh12_transfer_function {
	int (*write)(struct lis2hh12_data *cdata, u8 reg_addr, int len, u8 *data);
	int (*read)(struct lis2hh12_data *cdata, u8 reg_addr, int len, u8 *data);
};

struct lis2hh12_sensor_data {
	struct lis2hh12_data *cdata;
	const char *name;
	s64 timestamp;
	u8 enabled;
	u32 odr;
	u32 gain;
	u8 sindex;
	u8 sample_to_discard;
};

struct lis2hh12_data {
	const char *name;
	u8 drdy_int_pin;
	u8 enabled_sensor;
	u8 hwfifo_enabled;
	u8 hwfifo_watermark;
	u32 common_odr;
	int irq;
	s64 timestamp;
	s64 sensor_deltatime;
	s64 sensor_timestamp;
	u8 *fifo_data;
	u16 fifo_size;
	enum lis2hh12_selftest_status st_status;
	struct device *dev;
	struct iio_dev *iio_sensors_dev[LIS2HH12_SENSORS_NUMB];
	struct iio_trigger *iio_trig[LIS2HH12_SENSORS_NUMB];
	const struct lis2hh12_transfer_function *tf;
	struct lis2hh12_transfer_buffer tb;
};

static inline int lis2hh12_iio_dev_currentmode(struct iio_dev *indio_dev)
{

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
	struct iio_dev_opaque *iio_opq = to_iio_dev_opaque(indio_dev);

	return iio_opq->currentmode;
#else /* LINUX_VERSION_CODE */
	return indio_dev->currentmode;
#endif /* LINUX_VERSION_CODE */

}

int lis2hh12_common_probe(struct lis2hh12_data *cdata, int irq);
#ifdef CONFIG_PM
int lis2hh12_common_suspend(struct lis2hh12_data *cdata);
int lis2hh12_common_resume(struct lis2hh12_data *cdata);
#endif
int lis2hh12_allocate_rings(struct lis2hh12_data *cdata);
int lis2hh12_allocate_triggers(struct lis2hh12_data *cdata,
			     const struct iio_trigger_ops *trigger_ops);
int lis2hh12_trig_set_state(struct iio_trigger *trig, bool state);
int lis2hh12_read_register(struct lis2hh12_data *cdata, u8 reg_addr, int data_len,
							u8 *data);
int lis2hh12_update_drdy_irq(struct lis2hh12_sensor_data *sdata, bool state);
int lis2hh12_set_enable(struct lis2hh12_sensor_data *sdata, bool enable);
int lis2hh12_update_fifo_ths(struct lis2hh12_data *cdata, u8 fifo_len);
void lis2hh12_common_remove(struct lis2hh12_data *cdata, int irq);
void lis2hh12_read_fifo(struct lis2hh12_data *cdata, bool check_fifo_len);
void lis2hh12_deallocate_rings(struct lis2hh12_data *cdata);
void lis2hh12_deallocate_triggers(struct lis2hh12_data *cdata);
int lis2hh12_set_fifo_mode(struct lis2hh12_data *cdata, enum fifo_mode fm);
void lis2hh12_read_xyz(struct lis2hh12_data *cdata);

#endif /* __LIS2HH12_H */
