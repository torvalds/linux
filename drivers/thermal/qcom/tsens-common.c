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

#include <linux/err.h>
#include <linux/io.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include "tsens.h"

#define S0_ST_ADDR		0x1030
#define SN_ADDR_OFFSET		0x4
#define SN_ST_TEMP_MASK		0x3ff
#define CAL_DEGC_PT1		30
#define CAL_DEGC_PT2		120
#define SLOPE_FACTOR		1000
#define SLOPE_DEFAULT		3200

char *qfprom_read(struct device *dev, const char *cname)
{
	struct nvmem_cell *cell;
	ssize_t data;
	char *ret;

	cell = nvmem_cell_get(dev, cname);
	if (IS_ERR(cell))
		return ERR_CAST(cell);

	ret = nvmem_cell_read(cell, &data);
	nvmem_cell_put(cell);

	return ret;
}

/*
 * Use this function on devices where slope and offset calculations
 * depend on calibration data read from qfprom. On others the slope
 * and offset values are derived from tz->tzp->slope and tz->tzp->offset
 * resp.
 */
void compute_intercept_slope(struct tsens_device *tmdev, u32 *p1,
			     u32 *p2, u32 mode)
{
	int i;
	int num, den;

	for (i = 0; i < tmdev->num_sensors; i++) {
		dev_dbg(tmdev->dev,
			"sensor%d - data_point1:%#x data_point2:%#x\n",
			i, p1[i], p2[i]);

		tmdev->sensor[i].slope = SLOPE_DEFAULT;
		if (mode == TWO_PT_CALIB) {
			/*
			 * slope (m) = adc_code2 - adc_code1 (y2 - y1)/
			 *	temp_120_degc - temp_30_degc (x2 - x1)
			 */
			num = p2[i] - p1[i];
			num *= SLOPE_FACTOR;
			den = CAL_DEGC_PT2 - CAL_DEGC_PT1;
			tmdev->sensor[i].slope = num / den;
		}

		tmdev->sensor[i].offset = (p1[i] * SLOPE_FACTOR) -
				(CAL_DEGC_PT1 *
				tmdev->sensor[i].slope);
		dev_dbg(tmdev->dev, "offset:%d\n", tmdev->sensor[i].offset);
	}
}

static inline int code_to_degc(u32 adc_code, const struct tsens_sensor *s)
{
	int degc, num, den;

	num = (adc_code * SLOPE_FACTOR) - s->offset;
	den = s->slope;

	if (num > 0)
		degc = num + (den / 2);
	else if (num < 0)
		degc = num - (den / 2);
	else
		degc = num;

	degc /= den;

	return degc;
}

int get_temp_common(struct tsens_device *tmdev, int id, int *temp)
{
	struct tsens_sensor *s = &tmdev->sensor[id];
	u32 code;
	unsigned int sensor_addr;
	int last_temp = 0, ret;

	sensor_addr = S0_ST_ADDR + s->hw_id * SN_ADDR_OFFSET;
	ret = regmap_read(tmdev->map, sensor_addr, &code);
	if (ret)
		return ret;
	last_temp = code & SN_ST_TEMP_MASK;

	*temp = code_to_degc(last_temp, s) * 1000;

	return 0;
}

static const struct regmap_config tsens_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

int __init init_common(struct tsens_device *tmdev)
{
	void __iomem *base;

	base = of_iomap(tmdev->dev->of_node, 0);
	if (IS_ERR(base))
		return -EINVAL;

	tmdev->map = devm_regmap_init_mmio(tmdev->dev, base, &tsens_config);
	if (!tmdev->map) {
		iounmap(base);
		return -ENODEV;
	}

	return 0;
}
