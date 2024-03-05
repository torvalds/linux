/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_acc33 sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#ifndef ST_ACC33_H
#define ST_ACC33_H

#include "../common/stm_iio_types.h"

#define LIS2DH_DEV_NAME			"lis2dh_accel"
#define LIS2DH12_DEV_NAME		"lis2dh12_accel"
#define LIS3DH_DEV_NAME			"lis3dh_accel"
#define LSM303AGR_DEV_NAME		"lsm303agr_accel"
#define IIS2DH_DEV_NAME			"iis2dh_accel"

#define ST_ACC33_DATA_SIZE		6

#include <linux/iio/iio.h>

#define ST_ACC33_RX_MAX_LENGTH		96
#define ST_ACC33_TX_MAX_LENGTH		8

struct st_acc33_transfer_buffer {
	u8 rx_buf[ST_ACC33_RX_MAX_LENGTH];
	u8 tx_buf[ST_ACC33_TX_MAX_LENGTH] ____cacheline_aligned;
};

struct st_acc33_transfer_function {
	int (*read)(struct device *dev, u8 addr, int len, u8 *data);
	int (*write)(struct device *dev, u8 addr, int len, u8 *data);
};

enum st_acc33_fifo_mode {
	ST_ACC33_FIFO_BYPASS = 0x0,
	ST_ACC33_FIFO_STREAM = 0x2,
};

struct st_acc33_hw {
	struct device *dev;
	const char *name;
	int irq;

	u32 module_id;

	struct mutex fifo_lock;
	struct mutex lock;

	u8 watermark;
	u32 gain;
	u16 odr;

	u64 samples;
	u8 std_level;

	s64 delta_ts;
	s64 ts_irq;
	s64 ts;

	struct iio_dev *iio_dev;

	const struct st_acc33_transfer_function *tf;
	struct st_acc33_transfer_buffer tb;
};

int st_acc33_write_with_mask(struct st_acc33_hw *hw, u8 addr, u8 mask,
			     u8 val);
int st_acc33_set_enable(struct st_acc33_hw *hw, bool enable);
int st_acc33_probe(struct device *device, int irq, const char *name,
		   const struct st_acc33_transfer_function *tf_ops);
int st_acc33_fifo_setup(struct st_acc33_hw *hw);
ssize_t st_acc33_flush_hwfifo(struct device *device,
			      struct device_attribute *attr,
			      const char *buf, size_t size);
ssize_t st_acc33_get_max_hwfifo_watermark(struct device *dev,
					  struct device_attribute *attr,
					  char *buf);
ssize_t st_acc33_get_hwfifo_watermark(struct device *device,
				      struct device_attribute *attr,
				      char *buf);
ssize_t st_acc33_set_hwfifo_watermark(struct device *device,
				      struct device_attribute *attr,
				      const char *buf, size_t size);
int st_acc33_update_watermark(struct st_acc33_hw *hw, u8 watermark);

#endif /* ST_ACC33_H */
