// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 */

#include "fimc-is-sensor.h"

static const struct sensor_drv_data s5k6a3_drvdata = {
	.id		= FIMC_IS_SENSOR_ID_S5K6A3,
	.open_timeout	= S5K6A3_OPEN_TIMEOUT,
};

static const struct of_device_id fimc_is_sensor_of_ids[] = {
	{
		.compatible	= "samsung,s5k6a3",
		.data		= &s5k6a3_drvdata,
	},
	{  }
};

const struct sensor_drv_data *fimc_is_sensor_get_drvdata(
			struct device_node *node)
{
	const struct of_device_id *of_id;

	of_id = of_match_node(fimc_is_sensor_of_ids, node);
	return of_id ? of_id->data : NULL;
}
