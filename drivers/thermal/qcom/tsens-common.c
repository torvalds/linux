// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include "tsens.h"

/* SROT */
#define TSENS_EN		BIT(0)

/* TM */
#define STATUS_OFFSET		0x30
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
	unsigned int status_reg;
	int last_temp = 0, ret;

	status_reg = tmdev->tm_offset + STATUS_OFFSET + s->hw_id * SN_ADDR_OFFSET;
	ret = regmap_read(tmdev->tm_map, status_reg, &code);
	if (ret)
		return ret;
	last_temp = code & SN_ST_TEMP_MASK;

	*temp = code_to_degc(last_temp, s) * 1000;

	return 0;
}

static const struct regmap_config tsens_config = {
	.name		= "tm",
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

static const struct regmap_config tsens_srot_config = {
	.name		= "srot",
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

int __init init_common(struct tsens_device *tmdev)
{
	void __iomem *tm_base, *srot_base;
	struct resource *res;
	u32 code;
	int ret;
	struct platform_device *op = of_find_device_by_node(tmdev->dev->of_node);
	u16 ctrl_offset = tmdev->reg_offsets[SROT_CTRL_OFFSET];

	if (!op)
		return -EINVAL;

	if (op->num_resources > 1) {
		/* DT with separate SROT and TM address space */
		tmdev->tm_offset = 0;
		res = platform_get_resource(op, IORESOURCE_MEM, 1);
		srot_base = devm_ioremap_resource(&op->dev, res);
		if (IS_ERR(srot_base))
			return PTR_ERR(srot_base);

		tmdev->srot_map = devm_regmap_init_mmio(tmdev->dev, srot_base,
							&tsens_srot_config);
		if (IS_ERR(tmdev->srot_map))
			return PTR_ERR(tmdev->srot_map);

	} else {
		/* old DTs where SROT and TM were in a contiguous 2K block */
		tmdev->tm_offset = 0x1000;
	}

	res = platform_get_resource(op, IORESOURCE_MEM, 0);
	tm_base = devm_ioremap_resource(&op->dev, res);
	if (IS_ERR(tm_base))
		return PTR_ERR(tm_base);

	tmdev->tm_map = devm_regmap_init_mmio(tmdev->dev, tm_base, &tsens_config);
	if (IS_ERR(tmdev->tm_map))
		return PTR_ERR(tmdev->tm_map);

	if (tmdev->srot_map) {
		ret = regmap_read(tmdev->srot_map, ctrl_offset, &code);
		if (ret)
			return ret;
		if (!(code & TSENS_EN)) {
			dev_err(tmdev->dev, "tsens device is not enabled\n");
			return -ENODEV;
		}
	}

	return 0;
}
