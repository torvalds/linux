/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics lps22hb driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2017 STMicroelectronics Inc.
 */

#ifndef __ST_LPS22HB_H
#define __ST_LPS22HB_H

#include <linux/module.h>
#include <linux/types.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>

#include "../common/stm_iio_types.h"

#define ST_LPS22HB_MAX_FIFO_LENGTH		31

#define ST_LPS22HB_CTRL3_ADDR			0x12

enum st_lps22hb_sensor_type {
	ST_LPS22HB_PRESS = 0,
	ST_LPS22HB_TEMP,
	ST_LPS22HB_SENSORS_NUMB,
};

enum st_lps22hb_fifo_mode {
	ST_LPS22HB_BYPASS = 0x0,
	ST_LPS22HB_STREAM = 0x6,
};

#define ST_LPS22HB_TX_MAX_LENGTH		8
#define ST_LPS22HB_RX_MAX_LENGTH		192

struct st_lps22hb_transfer_buffer {
	u8 rx_buf[ST_LPS22HB_RX_MAX_LENGTH];
	u8 tx_buf[ST_LPS22HB_TX_MAX_LENGTH] ____cacheline_aligned;
};

struct st_lps22hb_transfer_function {
	int (*write)(struct device *dev, u8 addr, int len, u8 *data);
	int (*read)(struct device *dev, u8 addr, int len, u8 *data);
};

struct st_lps22hb_hw {
	struct device *dev;
	int irq;

	struct mutex fifo_lock;
	struct mutex lock;
	u8 watermark;

	struct iio_dev *iio_devs[ST_LPS22HB_SENSORS_NUMB];
	u8 enable_mask;
	u8 odr;

	s64 delta_ts;
	s64 ts_irq;
	s64 ts;

	const struct st_lps22hb_transfer_function *tf;
	struct st_lps22hb_transfer_buffer tb;
};

struct st_lps22hb_sensor {
	struct st_lps22hb_hw *hw;
	enum st_lps22hb_sensor_type type;
	char name[32];

	u32 gain;
	u8 odr;
};

int st_lps22hb_common_probe(struct device *dev, int irq, const char *name,
			    const struct st_lps22hb_transfer_function *tf_ops);
int st_lps22hb_write_with_mask(struct st_lps22hb_hw *hw, u8 addr, u8 mask,
			       u8 data);
int st_lps22hb_allocate_buffers(struct st_lps22hb_hw *hw);
int st_lps22hb_set_enable(struct st_lps22hb_sensor *sensor, bool enable);
ssize_t st_lps22hb_sysfs_set_hwfifo_watermark(struct device * dev,
					      struct device_attribute * attr,
					      const char *buf, size_t count);
ssize_t st_lps22hb_sysfs_flush_fifo(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size);

#endif /* __ST_LPS22HB_H */
