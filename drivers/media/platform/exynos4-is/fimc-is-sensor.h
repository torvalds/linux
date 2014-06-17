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

#include <linux/of.h>
#include <linux/types.h>

#define S5K6A3_OPEN_TIMEOUT		2000 /* ms */
#define S5K6A3_SENSOR_WIDTH		1392
#define S5K6A3_SENSOR_HEIGHT		1392

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
	/* sensor open timeout in ms */
	unsigned short open_timeout;
};

/**
 * struct fimc_is_sensor - fimc-is sensor data structure
 * @drvdata: a pointer to the sensor's parameters data structure
 * @i2c_bus: ISP I2C bus index (0...1)
 * @test_pattern: true to enable video test pattern
 */
struct fimc_is_sensor {
	const struct sensor_drv_data *drvdata;
	unsigned int i2c_bus;
	u8 test_pattern;
};

const struct sensor_drv_data *fimc_is_sensor_get_drvdata(
				struct device_node *node);

#endif /* FIMC_IS_SENSOR_H_ */
