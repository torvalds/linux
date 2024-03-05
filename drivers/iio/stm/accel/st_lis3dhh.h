/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_lis3dhh sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#ifndef ST_LIS3DHH_H
#define ST_LIS3DHH_H


#include <linux/iio/iio.h>

#include "../common/stm_iio_types.h"

#define ST_LIS3DHH_DATA_SIZE		6
#define ST_LIS3DHH_RX_MAX_LENGTH	96
#define ST_LIS3DHH_TX_MAX_LENGTH	8

#define ST_LIS3DHH_ODR			1100

struct st_lis3dhh_transfer_buffer {
	u8 rx_buf[ST_LIS3DHH_RX_MAX_LENGTH];
	u8 tx_buf[ST_LIS3DHH_TX_MAX_LENGTH] ____cacheline_aligned;
};

struct st_lis3dhh_hw {
	struct device *dev;
	const char *name;
	int irq;

	struct mutex fifo_lock;
	struct mutex lock;

	u8 watermark;
	s64 delta_ts;
	s64 ts_irq;
	s64 ts;
	struct iio_dev *iio_dev;
	struct st_lis3dhh_transfer_buffer tb;
};

int st_lis3dhh_write_with_mask(struct st_lis3dhh_hw *hw, u8 addr, u8 mask,
			       u8 val);
 int st_lis3dhh_spi_read(struct st_lis3dhh_hw *hw, u8 addr, int len, u8 *data);
int st_lis3dhh_set_enable(struct st_lis3dhh_hw *hw, bool enable);
int st_lis3dhh_fifo_setup(struct st_lis3dhh_hw *hw);
ssize_t st_lis3dhh_flush_hwfifo(struct device *device,
				struct device_attribute *attr,
				const char *buf, size_t size);
ssize_t st_lis3dhh_get_max_hwfifo_watermark(struct device *dev,
					    struct device_attribute *attr,
					    char *buf);
ssize_t st_lis3dhh_get_hwfifo_watermark(struct device *device,
					struct device_attribute *attr,
					char *buf);
ssize_t st_lis3dhh_set_hwfifo_watermark(struct device *device,
					struct device_attribute *attr,
					const char *buf, size_t size);
int st_lis3dhh_update_watermark(struct st_lis3dhh_hw *hw, u8 watermark);

#endif /* ST_LIS3DHH_H */
