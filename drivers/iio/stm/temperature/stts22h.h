/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics stts22h temperature driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2021 STMicroelectronics Inc.
 */

#ifndef ST_STTS22H_H
#define ST_STTS22H_H

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>

#define ST_STTS22H_DEV_NAME				"stts22h"

#define ST_STTS22H_WHOAMI_ADDR				0x01
#define ST_STTS22H_WHOAMI_VAL				0xa0

#define ST_STTS22H_TEMP_H_LIMIT_ADDR			0x02
#define ST_STTS22H_TEMP_L_LIMIT_ADDR			0x03

#define ST_STTS22H_CTRL_ADDR				0x04
#define ST_STTS22H_LOW_ODR_START_MASK			BIT(7)
#define ST_STTS22H_BDU_MASK				BIT(6)
#define ST_STTS22H_AVG_MASK				GENMASK(5,4)
#define ST_STTS22H_IF_ADD_INC_MASK			BIT(3)
#define ST_STTS22H_FREERUN_MASK				BIT(2)
#define ST_STTS22H_TIME_OUT_DIS_MASK			BIT(1)
#define ST_STTS22H_ONE_SHOT_MASK			BIT(0)

#define ST_STTS22H_STATUS_ADDR				0x05
#define ST_STTS22H_UNDER_THL_MASK			BIT(2)
#define ST_STTS22H_OVER_THH_MASK			BIT(1)
#define ST_STTS22H_BUSY_MASK				BIT(0)

#define ST_STTS22H_TEMP_L_OUT_ADDR			0x06

#define ST_STTS22H_SOFTWARE_RESET_ADDR			0x0c
#define ST_STTS22H_LOW_ODR_ENABLE_MASK			BIT(6)
#define ST_STTS22H_SW_RESET_MASK			BIT(1)

#define ST_STTS22H_ODR_LIST_SIZE			4
#define ST_STTS22H_GAIN					100
#define ST_STTS22H_SAMPLE_SIZE				sizeof(s16)

#define HZ_TO_PERIOD_NSEC(hz)				(1000000000 / \
							 ((u32)(hz)))


/**
 * struct st_stts22h_reg - Sensor data register and mask
 *
 * @addr: Register address.
 * @mask: Bit mask.
 */
struct st_stts22h_reg {
	u8 addr;
	u8 mask;
};

/**
 * struct st_stts22h_odr - Sensor data odr entry
 *
 * @hz: Sensor ODR.
 * @val: Register value.
 */
struct st_stts22h_odr {
	u8 hz;
	u8 val;
};

/**
 * struct st_stts22h_data - Sensor data instance
 *
 * @st_stts22h_workqueue: Temperature workqueue.
 * @iio_work: Work to schedule temperature read function.
 * @iio_devs: Linux Device.
 * @hr_timer: Timer to schedule workeueue.
 * @sensorktime: Sensor schedule timeout.
 * @dev: I2C client device.
 * @mutex: Mutex lock to access to device registers.
 * @timestamp: Sensor timestamp.
 * @enable: Enable sensor flag.
 * @irq: Interrupt number (TODO).
 * @odr: Sensor ODR.
 */
struct st_stts22h_data {
	struct workqueue_struct *st_stts22h_workqueue;
	struct work_struct iio_work;
	struct iio_dev *iio_devs;
	struct hrtimer hr_timer;
	ktime_t sensorktime;
	struct device *dev;
	struct mutex lock;
	s64 timestamp;
	bool enable;
	int irq;
	u8 odr;
};

#endif /* ST_STTS22H_H */
