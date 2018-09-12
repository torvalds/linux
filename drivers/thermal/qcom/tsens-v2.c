// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 * Copyright (c) 2018, Linaro Limited
 */

#include <linux/regmap.h>
#include <linux/bitops.h>
#include "tsens.h"

#define STATUS_OFFSET		0xa0
#define LAST_TEMP_MASK		0xfff
#define STATUS_VALID_BIT	BIT(21)

static int get_temp_tsens_v2(struct tsens_device *tmdev, int id, int *temp)
{
	struct tsens_sensor *s = &tmdev->sensor[id];
	u32 code;
	unsigned int status_reg;
	u32 last_temp = 0, last_temp2 = 0, last_temp3 = 0;
	int ret;

	status_reg = tmdev->tm_offset + STATUS_OFFSET + s->hw_id * 4;
	ret = regmap_read(tmdev->tm_map, status_reg, &code);
	if (ret)
		return ret;
	last_temp = code & LAST_TEMP_MASK;
	if (code & STATUS_VALID_BIT)
		goto done;

	/* Try a second time */
	ret = regmap_read(tmdev->tm_map, status_reg, &code);
	if (ret)
		return ret;
	if (code & STATUS_VALID_BIT) {
		last_temp = code & LAST_TEMP_MASK;
		goto done;
	} else {
		last_temp2 = code & LAST_TEMP_MASK;
	}

	/* Try a third/last time */
	ret = regmap_read(tmdev->tm_map, status_reg, &code);
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
	/* Convert temperature from deciCelsius to milliCelsius */
	*temp = sign_extend32(last_temp, fls(LAST_TEMP_MASK) - 1) * 100;

	return 0;
}

static const struct tsens_ops ops_generic_v2 = {
	.init		= init_common,
	.get_temp	= get_temp_tsens_v2,
};

const struct tsens_data data_tsens_v2 = {
	.ops            = &ops_generic_v2,
};

/* Kept around for backward compatibility with old msm8996.dtsi */
const struct tsens_data data_8996 = {
	.num_sensors	= 13,
	.ops		= &ops_generic_v2,
};
