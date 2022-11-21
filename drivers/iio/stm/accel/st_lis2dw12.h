/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics lis2dw12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#ifndef ST_LIS2DW12_H
#define ST_LIS2DW12_H

#include <linux/device.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>

#define ST_LIS2DW12_DEV_NAME		"lis2dw12"
#define ST_IIS2DLPC_DEV_NAME		"iis2dlpc"
#define ST_AIS2IH_DEV_NAME		"ais2ih"
#define ST_LIS2DW12_MAX_WATERMARK	31
#define ST_LIS2DW12_DATA_SIZE		6

struct st_lis2dw12_transfer_function {
	int (*read)(struct device *dev, u8 addr, int len, u8 *data);
	int (*write)(struct device *dev, u8 addr, int len, u8 *data);
};

#define ST_LIS2DW12_RX_MAX_LENGTH	96
#define ST_LIS2DW12_TX_MAX_LENGTH	8

struct st_lis2dw12_transfer_buffer {
	u8 rx_buf[ST_LIS2DW12_RX_MAX_LENGTH];
	u8 tx_buf[ST_LIS2DW12_TX_MAX_LENGTH] ____cacheline_aligned;
};

enum st_lis2dw12_fifo_mode {
	ST_LIS2DW12_FIFO_BYPASS = 0x0,
	ST_LIS2DW12_FIFO_CONTINUOUS = 0x6,
};

enum st_lis2dw12_selftest_status {
	ST_LIS2DW12_ST_RESET,
	ST_LIS2DW12_ST_PASS,
	ST_LIS2DW12_ST_FAIL,
};

enum st_lis2dw12_sensor_id {
	ST_LIS2DW12_ID_ACC,
	ST_LIS2DW12_ID_TAP_TAP,
	ST_LIS2DW12_ID_TAP,
	ST_LIS2DW12_ID_WU,
	ST_LIS2DW12_ID_MAX,
};

struct st_lis2dw12_sensor {
	enum st_lis2dw12_sensor_id id;
	struct st_lis2dw12_hw *hw;

	u16 gain;
	u16 odr;
};

struct st_lis2dw12_hw {
	struct device *dev;
	int irq;

	struct mutex fifo_lock;
	struct mutex lock;

	struct iio_dev *iio_devs[ST_LIS2DW12_ID_MAX];

	enum st_lis2dw12_selftest_status st_status;
	u16 enable_mask;

	u8 watermark;
	u8 std_level;
	u64 samples;

	s64 delta_ts;
	s64 ts_irq;
	s64 ts;

	const struct st_lis2dw12_transfer_function *tf;
	struct st_lis2dw12_transfer_buffer tb;
};

int st_lis2dw12_probe(struct device *dev, int irq,
		      const struct st_lis2dw12_transfer_function *tf_ops);
int st_lis2dw12_fifo_setup(struct st_lis2dw12_hw *hw);
int st_lis2dw12_update_fifo_watermark(struct st_lis2dw12_hw *hw, u8 watermark);
int st_lis2dw12_write_with_mask(struct st_lis2dw12_hw *hw, u8 addr, u8 mask,
				u8 val);
ssize_t st_lis2dw12_flush_fifo(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size);
ssize_t st_lis2dw12_set_hwfifo_watermark(struct device *device,
					 struct device_attribute *attr,
					 const char *buf, size_t size);
int st_lis2dw12_sensor_set_enable(struct st_lis2dw12_sensor *sensor,
				  bool enable);

#endif /* ST_LIS2DW12_H */

