// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/thermal.h>
#include "tsens.h"

#define CAL_MDEGC		30000

#define CONFIG_ADDR		0x3640
#define CONFIG_ADDR_8660	0x3620
/* CONFIG_ADDR bitmasks */
#define CONFIG			0x9b
#define CONFIG_MASK		0xf
#define CONFIG_8660		1
#define CONFIG_SHIFT_8660	28
#define CONFIG_MASK_8660	(3 << CONFIG_SHIFT_8660)

#define STATUS_CNTL_ADDR_8064	0x3660
#define CNTL_ADDR		0x3620
/* CNTL_ADDR bitmasks */
#define EN			BIT(0)
#define SW_RST			BIT(1)
#define SENSOR0_EN		BIT(3)
#define SLP_CLK_ENA		BIT(26)
#define SLP_CLK_ENA_8660	BIT(24)
#define MEASURE_PERIOD		1
#define SENSOR0_SHIFT		3

/* INT_STATUS_ADDR bitmasks */
#define MIN_STATUS_MASK		BIT(0)
#define LOWER_STATUS_CLR	BIT(1)
#define UPPER_STATUS_CLR	BIT(2)
#define MAX_STATUS_MASK		BIT(3)

#define THRESHOLD_ADDR		0x3624
/* THRESHOLD_ADDR bitmasks */
#define THRESHOLD_MAX_LIMIT_SHIFT	24
#define THRESHOLD_MIN_LIMIT_SHIFT	16
#define THRESHOLD_UPPER_LIMIT_SHIFT	8
#define THRESHOLD_LOWER_LIMIT_SHIFT	0

/* Initial temperature threshold values */
#define LOWER_LIMIT_TH		0x50
#define UPPER_LIMIT_TH		0xdf
#define MIN_LIMIT_TH		0x0
#define MAX_LIMIT_TH		0xff

#define S0_STATUS_ADDR		0x3628
#define INT_STATUS_ADDR		0x363c
#define TRDY_MASK		BIT(7)
#define TIMEOUT_US		100

static int suspend_8960(struct tsens_device *tmdev)
{
	int ret;
	unsigned int mask;
	struct regmap *map = tmdev->map;

	ret = regmap_read(map, THRESHOLD_ADDR, &tmdev->ctx.threshold);
	if (ret)
		return ret;

	ret = regmap_read(map, CNTL_ADDR, &tmdev->ctx.control);
	if (ret)
		return ret;

	if (tmdev->num_sensors > 1)
		mask = SLP_CLK_ENA | EN;
	else
		mask = SLP_CLK_ENA_8660 | EN;

	ret = regmap_update_bits(map, CNTL_ADDR, mask, 0);
	if (ret)
		return ret;

	return 0;
}

static int resume_8960(struct tsens_device *tmdev)
{
	int ret;
	struct regmap *map = tmdev->map;

	ret = regmap_update_bits(map, CNTL_ADDR, SW_RST, SW_RST);
	if (ret)
		return ret;

	/*
	 * Separate CONFIG restore is not needed only for 8660 as
	 * config is part of CTRL Addr and its restored as such
	 */
	if (tmdev->num_sensors > 1) {
		ret = regmap_update_bits(map, CONFIG_ADDR, CONFIG_MASK, CONFIG);
		if (ret)
			return ret;
	}

	ret = regmap_write(map, THRESHOLD_ADDR, tmdev->ctx.threshold);
	if (ret)
		return ret;

	ret = regmap_write(map, CNTL_ADDR, tmdev->ctx.control);
	if (ret)
		return ret;

	return 0;
}

static int enable_8960(struct tsens_device *tmdev, int id)
{
	int ret;
	u32 reg, mask;

	ret = regmap_read(tmdev->map, CNTL_ADDR, &reg);
	if (ret)
		return ret;

	mask = BIT(id + SENSOR0_SHIFT);
	ret = regmap_write(tmdev->map, CNTL_ADDR, reg | SW_RST);
	if (ret)
		return ret;

	if (tmdev->num_sensors > 1)
		reg |= mask | SLP_CLK_ENA | EN;
	else
		reg |= mask | SLP_CLK_ENA_8660 | EN;

	ret = regmap_write(tmdev->map, CNTL_ADDR, reg);
	if (ret)
		return ret;

	return 0;
}

static void disable_8960(struct tsens_device *tmdev)
{
	int ret;
	u32 reg_cntl;
	u32 mask;

	mask = GENMASK(tmdev->num_sensors - 1, 0);
	mask <<= SENSOR0_SHIFT;
	mask |= EN;

	ret = regmap_read(tmdev->map, CNTL_ADDR, &reg_cntl);
	if (ret)
		return;

	reg_cntl &= ~mask;

	if (tmdev->num_sensors > 1)
		reg_cntl &= ~SLP_CLK_ENA;
	else
		reg_cntl &= ~SLP_CLK_ENA_8660;

	regmap_write(tmdev->map, CNTL_ADDR, reg_cntl);
}

static int init_8960(struct tsens_device *tmdev)
{
	int ret, i;
	u32 reg_cntl;

	tmdev->map = dev_get_regmap(tmdev->dev, NULL);
	if (!tmdev->map)
		return -ENODEV;

	/*
	 * The status registers for each sensor are discontiguous
	 * because some SoCs have 5 sensors while others have more
	 * but the control registers stay in the same place, i.e
	 * directly after the first 5 status registers.
	 */
	for (i = 0; i < tmdev->num_sensors; i++) {
		if (i >= 5)
			tmdev->sensor[i].status = S0_STATUS_ADDR + 40;
		tmdev->sensor[i].status += i * 4;
	}

	reg_cntl = SW_RST;
	ret = regmap_update_bits(tmdev->map, CNTL_ADDR, SW_RST, reg_cntl);
	if (ret)
		return ret;

	if (tmdev->num_sensors > 1) {
		reg_cntl |= SLP_CLK_ENA | (MEASURE_PERIOD << 18);
		reg_cntl &= ~SW_RST;
		ret = regmap_update_bits(tmdev->map, CONFIG_ADDR,
					 CONFIG_MASK, CONFIG);
	} else {
		reg_cntl |= SLP_CLK_ENA_8660 | (MEASURE_PERIOD << 16);
		reg_cntl &= ~CONFIG_MASK_8660;
		reg_cntl |= CONFIG_8660 << CONFIG_SHIFT_8660;
	}

	reg_cntl |= GENMASK(tmdev->num_sensors - 1, 0) << SENSOR0_SHIFT;
	ret = regmap_write(tmdev->map, CNTL_ADDR, reg_cntl);
	if (ret)
		return ret;

	reg_cntl |= EN;
	ret = regmap_write(tmdev->map, CNTL_ADDR, reg_cntl);
	if (ret)
		return ret;

	return 0;
}

static int calibrate_8960(struct tsens_device *tmdev)
{
	int i;
	char *data;

	ssize_t num_read = tmdev->num_sensors;
	struct tsens_sensor *s = tmdev->sensor;

	data = qfprom_read(tmdev->dev, "calib");
	if (IS_ERR(data))
		data = qfprom_read(tmdev->dev, "calib_backup");
	if (IS_ERR(data))
		return PTR_ERR(data);

	for (i = 0; i < num_read; i++, s++)
		s->offset = data[i];

	return 0;
}

/* Temperature on y axis and ADC-code on x-axis */
static inline int code_to_mdegC(u32 adc_code, const struct tsens_sensor *s)
{
	int slope, offset;

	slope = thermal_zone_get_slope(s->tzd);
	offset = CAL_MDEGC - slope * s->offset;

	return adc_code * slope + offset;
}

static int get_temp_8960(struct tsens_device *tmdev, int id, int *temp)
{
	int ret;
	u32 code, trdy;
	const struct tsens_sensor *s = &tmdev->sensor[id];
	unsigned long timeout;

	timeout = jiffies + usecs_to_jiffies(TIMEOUT_US);
	do {
		ret = regmap_read(tmdev->map, INT_STATUS_ADDR, &trdy);
		if (ret)
			return ret;
		if (!(trdy & TRDY_MASK))
			continue;
		ret = regmap_read(tmdev->map, s->status, &code);
		if (ret)
			return ret;
		*temp = code_to_mdegC(code, s);
		return 0;
	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

static const struct tsens_ops ops_8960 = {
	.init		= init_8960,
	.calibrate	= calibrate_8960,
	.get_temp	= get_temp_8960,
	.enable		= enable_8960,
	.disable	= disable_8960,
	.suspend	= suspend_8960,
	.resume		= resume_8960,
};

const struct tsens_data data_8960 = {
	.num_sensors	= 11,
	.ops		= &ops_8960,
};
