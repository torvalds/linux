/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics lps22hh driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2017 STMicroelectronics Inc.
 */

#ifndef __ST_LPS22HH_H
#define __ST_LPS22HH_H

#include <linux/module.h>
#include <linux/types.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>

#include "../common/stm_iio_types.h"

#define ST_LPS22HH_MAX_FIFO_LENGTH		127

#define ST_LPS22HH_LIR_ADDR			0x0b
#define ST_LPS22HH_LIR_MASK			0x04

#define ST_LPS22HH_WHO_AM_I_ADDR		0x0f
#define ST_LPS22HH_WHO_AM_I_DEF			0xb3

#define ST_LPS22HH_CTRL1_ADDR			0x10
#define ST_LPS22HH_BDU_MASK			0x02

#define ST_LPS22HH_CTRL2_ADDR			0x11
#define ST_LPS22HH_LOW_NOISE_EN_MASK		0x02
#define ST_LPS22HH_SOFT_RESET_MASK		0x04
#define ST_LPS22HH_INT_ACTIVE_MASK		0x40
#define ST_LPS22HH_BOOT_MASK			0x80

#define ST_LPS22HH_CTRL3_ADDR			0x12
#define ST_LPS22HH_INT_FTH_MASK			0x10

enum st_lps22hh_sensor_type {
	ST_LPS22HH_PRESS = 0,
	ST_LPS22HH_TEMP,
	ST_LPS22HH_SENSORS_NUMB,
};

enum st_lps22hh_fifo_mode {
	ST_LPS22HH_BYPASS = 0x0,
	ST_LPS22HH_STREAM = 0x2,
};

#define ST_LPS22HH_PRESS_SAMPLE_LEN		3
#define ST_LPS22HH_TEMP_SAMPLE_LEN		2
#define ST_LPS22HH_FIFO_SAMPLE_LEN		(ST_LPS22HH_PRESS_SAMPLE_LEN + \
						 ST_LPS22HH_TEMP_SAMPLE_LEN)

#define ST_LPS22HH_TX_MAX_LENGTH		8
#define ST_LPS22HH_RX_MAX_LENGTH		(ST_LPS22HH_MAX_FIFO_LENGTH + 1) * \
						ST_LPS22HH_FIFO_SAMPLE_LEN

struct st_lps22hh_transfer_buffer {
	u8 rx_buf[ST_LPS22HH_RX_MAX_LENGTH];
	u8 tx_buf[ST_LPS22HH_TX_MAX_LENGTH] ____cacheline_aligned;
};

struct st_lps22hh_transfer_function {
	int (*write)(struct device *dev, u8 addr, int len, u8 *data);
	int (*read)(struct device *dev, u8 addr, int len, u8 *data);
};

struct st_lps22hh_hw {
	struct device *dev;
	int irq;

	struct mutex fifo_lock;
	struct mutex lock;
	u8 watermark;

	struct iio_dev *iio_devs[ST_LPS22HH_SENSORS_NUMB];
	u8 enable_mask;
	u8 odr;

	s64 delta_ts;
	s64 ts_irq;
	s64 ts;

	const struct st_lps22hh_transfer_function *tf;
	struct st_lps22hh_transfer_buffer tb;
};

struct st_lps22hh_sensor {
	struct st_lps22hh_hw *hw;
	enum st_lps22hh_sensor_type type;
	char name[32];

	u32 gain;
	u8 odr;
};

int st_lps22hh_common_probe(struct device *dev, int irq, const char *name,
			    const struct st_lps22hh_transfer_function *tf_ops);
int st_lps22hh_write_with_mask(struct st_lps22hh_hw *hw, u8 addr, u8 mask,
			       u8 data);
int st_lps22hh_allocate_buffers(struct st_lps22hh_hw *hw);
int st_lps22hh_set_enable(struct st_lps22hh_sensor *sensor, bool enable);
ssize_t st_lps22hh_sysfs_set_hwfifo_watermark(struct device * dev,
					      struct device_attribute * attr,
					      const char *buf, size_t count);
ssize_t st_lps22hh_sysfs_flush_fifo(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size);

#endif /* __ST_LPS22HH_H */
