/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/platform_device.h>
#include <linux/regmap.h>
#include "tsens.h"

#define STATUS_OFFSET	0x10a0
#define LAST_TEMP_MASK	0xfff
#define STATUS_VALID_BIT	BIT(21)
#define CODE_SIGN_BIT		BIT(11)

static int get_temp_8996(struct tsens_device *tmdev, int id, int *temp)
{
	struct tsens_sensor *s = &tmdev->sensor[id];
	u32 code;
	unsigned int sensor_addr;
	int last_temp = 0, last_temp2 = 0, last_temp3 = 0, ret;

	sensor_addr = STATUS_OFFSET + s->hw_id * 4;
	ret = regmap_read(tmdev->map, sensor_addr, &code);
	if (ret)
		return ret;
	last_temp = code & LAST_TEMP_MASK;
	if (code & STATUS_VALID_BIT)
		goto done;

	/* Try a second time */
	ret = regmap_read(tmdev->map, sensor_addr, &code);
	if (ret)
		return ret;
	if (code & STATUS_VALID_BIT) {
		last_temp = code & LAST_TEMP_MASK;
		goto done;
	} else {
		last_temp2 = code & LAST_TEMP_MASK;
	}

	/* Try a third/last time */
	ret = regmap_read(tmdev->map, sensor_addr, &code);
	if (ret)
		return ret;
	if (code & STATUS_VALID_BIT) {
		last_temp = code & LAST_TEMP_MASK;
		goto done;
	} else {
		last_temp3 = code & LAST_TEMP_MASK;
	}

	if (last_temp == last_temp2)
		last_temp = last_temp2;
	else if (last_temp2 == last_temp3)
		last_temp = last_temp3;
done:
	/* Code sign bit is the sign extension for a negative value */
	if (last_temp & CODE_SIGN_BIT)
		last_temp |= ~CODE_SIGN_BIT;

	/* Temperatures are in deciCelicius */
	*temp = last_temp * 100;

	return 0;
}

const struct tsens_ops ops_8996 = {
	.init		= init_common,
	.get_temp	= get_temp_8996,
};

const struct tsens_data data_8996 = {
	.num_sensors	= 13,
	.ops		= &ops_8996,
};
