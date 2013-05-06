/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * Authors:  Sylwester Nawrocki <s.nawrocki@samsung.com>
 *	     Younghwan Joo <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef FIMC_IS_SENSOR_H_
#define FIMC_IS_SENSOR_H_

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <media/v4l2-subdev.h>

#define FIMC_IS_SENSOR_OPEN_TIMEOUT	2000 /* ms */

#define FIMC_IS_SENSOR_DEF_PIX_WIDTH	1296
#define FIMC_IS_SENSOR_DEF_PIX_HEIGHT	732

#define S5K6A3_SENSOR_WIDTH		1392
#define S5K6A3_SENSOR_HEIGHT		1392

#define SENSOR_NUM_SUPPLIES		2

enum fimc_is_sensor_id {
	FIMC_IS_SENSOR_ID_S5K3H2 = 1,
	FIMC_IS_SENSOR_ID_S5K6A3,
	FIMC_IS_SENSOR_ID_S5K4E5,
	FIMC_IS_SENSOR_ID_S5K3H7,
	FIMC_IS_SENSOR_ID_CUSTOM,
	FIMC_IS_SENSOR_ID_END
};

#define IS_SENSOR_CTRL_BUS_I2C0		0
#define IS_SENSOR_CTRL_BUS_I2C1		1

struct sensor_drv_data {
	enum fimc_is_sensor_id id;
	const char * const subdev_name;
	unsigned int width;
	unsigned int height;
};

/**
 * struct fimc_is_sensor - fimc-is sensor data structure
 * @dev: pointer to this I2C client device structure
 * @subdev: the image sensor's v4l2 subdev
 * @pad: subdev media source pad
 * @supplies: image sensor's voltage regulator supplies
 * @gpio_reset: GPIO connected to the sensor's reset pin
 * @drvdata: a pointer to the sensor's parameters data structure
 * @i2c_bus: ISP I2C bus index (0...1)
 * @test_pattern: true to enable video test pattern
 * @lock: mutex protecting the structure's members below
 * @format: media bus format at the sensor's source pad
 */
struct fimc_is_sensor {
	struct device *dev;
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct regulator_bulk_data supplies[SENSOR_NUM_SUPPLIES];
	int gpio_reset;
	const struct sensor_drv_data *drvdata;
	unsigned int i2c_bus;
	bool test_pattern;

	struct mutex lock;
	struct v4l2_mbus_framefmt format;
};

static inline
struct fimc_is_sensor *sd_to_fimc_is_sensor(struct v4l2_subdev *sd)
{
	return container_of(sd, struct fimc_is_sensor, subdev);
}

int fimc_is_register_sensor_driver(void);
void fimc_is_unregister_sensor_driver(void);

#endif /* FIMC_IS_SENSOR_H_ */
