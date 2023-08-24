/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_imu68 sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#ifndef ST_IMU68_H
#define ST_IMU68_H

#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/version.h>

#include "../../common/stm_iio_types.h"

#define ST_LSM9DS1_DEV_NAME		"lsm9ds1"

#define ST_IMU68_OUT_LEN		6

#if defined(CONFIG_SPI_MASTER)
#define ST_IMU68_RX_MAX_LENGTH		8
#define ST_IMU68_TX_MAX_LENGTH		8

struct st_imu68_transfer_buffer {
	u8 rx_buf[ST_IMU68_RX_MAX_LENGTH];
	u8 tx_buf[ST_IMU68_TX_MAX_LENGTH] ____cacheline_aligned;
};
#endif /* CONFIG_SPI_MASTER */

struct st_imu68_transfer_function {
	int (*read)(struct device *dev, u8 addr, int len, u8 *data);
	int (*write)(struct device *dev, u8 addr, int len, u8 *data);
};

enum st_imu68_sensor_id {
	ST_IMU68_ID_ACC,
	ST_IMU68_ID_GYRO,
	ST_IMU68_ID_MAX,
};

struct st_imu68_reg {
	u8 addr;
	u8 mask;
};

struct st_imu68_sensor {
	enum st_imu68_sensor_id id;
	struct st_imu68_hw *hw;

	struct iio_trigger *trigger;

	u32 gain;
	u16 odr;

	u8 drdy_mask;
	u8 status_mask;
};

struct st_imu68_hw {
	const char *name;
	struct device *dev;
	int irq;

	struct mutex lock;

	s64 timestamp;
	u8 enabled_mask;
	u32 module_id;

	struct iio_dev *iio_devs[ST_IMU68_ID_MAX];

	const struct st_imu68_transfer_function *tf;
#if defined(CONFIG_SPI_MASTER)
	struct st_imu68_transfer_buffer tb;
#endif /* CONFIG_SPI_MASTER */
};

int st_imu68_write_with_mask(struct st_imu68_hw *hw, u8 addr, u8 mask,
			     u8 val);
int st_imu68_probe(struct device *dev, int irq, const char *name,
		   const struct st_imu68_transfer_function *tf_ops);
int st_imu68_remove(struct device *dev);
int st_imu68_sensor_enable(struct st_imu68_sensor *sensor, bool enable);
int st_imu68_allocate_buffers(struct st_imu68_hw *hw);
void st_imu68_deallocate_buffers(struct st_imu68_hw *hw);
int st_imu68_allocate_triggers(struct st_imu68_hw *hw);
void st_imu68_deallocate_triggers(struct st_imu68_hw *hw);
#endif /* ST_IMU68_H */
