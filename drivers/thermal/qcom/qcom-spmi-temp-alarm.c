// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2015, 2017, 2020-2021, The Linux Foundation.
 * All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>

#include "../thermal_core.h"
#include "../thermal_hwmon.h"

#define QPNP_TM_REG_DIG_MINOR		0x00
#define QPNP_TM_REG_DIG_MAJOR		0x01
#define QPNP_TM_REG_TYPE		0x04
#define QPNP_TM_REG_SUBTYPE		0x05
#define QPNP_TM_REG_STATUS		0x08
#define QPNP_TM_REG_IRQ_STATUS		0x10
#define QPNP_TM_REG_SHUTDOWN_CTRL1	0x40
#define QPNP_TM_REG_ALARM_CTRL		0x46
/* TEMP_DAC_* registers are only present for TEMP_GEN2 v2.0 */
#define QPNP_TM_REG_TEMP_DAC_STG1	0x47
#define QPNP_TM_REG_TEMP_DAC_STG2	0x48
#define QPNP_TM_REG_TEMP_DAC_STG3	0x49
#define QPNP_TM_REG_LITE_TEMP_CFG1	0x50
#define QPNP_TM_REG_LITE_TEMP_CFG2	0x51

#define QPNP_TM_TYPE			0x09
#define QPNP_TM_SUBTYPE_GEN1		0x08
#define QPNP_TM_SUBTYPE_GEN2		0x09
#define QPNP_TM_SUBTYPE_LITE		0xC0

#define STATUS_GEN1_STAGE_MASK		GENMASK(1, 0)
#define STATUS_GEN2_STATE_MASK		GENMASK(6, 4)
#define STATUS_GEN2_STATE_SHIFT		4

/* IRQ status only needed for TEMP_ALARM_LITE */
#define IRQ_STATUS_MASK			BIT(0)

#define SHUTDOWN_CTRL1_OVERRIDE_S2	BIT(6)
#define SHUTDOWN_CTRL1_THRESHOLD_MASK	GENMASK(1, 0)

#define SHUTDOWN_CTRL1_RATE_25HZ	BIT(3)

#define ALARM_CTRL_FORCE_ENABLE		BIT(7)

#define LITE_TEMP_CFG_THRESHOLD_MASK	GENMASK(3, 2)

#define THRESH_COUNT			4
#define STAGE_COUNT			3

/* Over-temperature trip point values in mC */
static const long temp_map_gen1[THRESH_COUNT][STAGE_COUNT] = {
	{ 105000, 125000, 145000 },
	{ 110000, 130000, 150000 },
	{ 115000, 135000, 155000 },
	{ 120000, 140000, 160000 },
};

static const long temp_map_gen2_v1[THRESH_COUNT][STAGE_COUNT] = {
	{  90000, 110000, 140000 },
	{  95000, 115000, 145000 },
	{ 100000, 120000, 150000 },
	{ 105000, 125000, 155000 },
};

#define TEMP_THRESH_STEP		5000 /* Threshold step: 5 C */

#define THRESH_MIN			0
#define THRESH_MAX			3

#define TEMP_STAGE_HYSTERESIS		2000

/*
 * For TEMP_GEN2 v2.0, TEMP_DAC_STG1/2/3 registers are used to set the threshold
 * for each stage independently.
 * TEMP_DAC_STG* = 0 --> 80 C
 * Each 8 step increase in TEMP_DAC_STG* value corresponds to 5 C (5000 mC).
 */
#define TEMP_DAC_MIN			80000
#define TEMP_DAC_SCALE_NUM		8
#define TEMP_DAC_SCALE_DEN		5000

#define TEMP_DAC_TEMP_TO_REG(temp) \
	(((temp) - TEMP_DAC_MIN) * TEMP_DAC_SCALE_NUM / TEMP_DAC_SCALE_DEN)
#define TEMP_DAC_REG_TO_TEMP(reg) \
	(TEMP_DAC_MIN + (reg) * TEMP_DAC_SCALE_DEN / TEMP_DAC_SCALE_NUM)

static const long temp_dac_max[STAGE_COUNT] = {
	119375, 159375, 159375
};

/*
 * TEMP_ALARM_LITE has two stages: warning and shutdown with independently
 * configured threshold temperatures.
 */

static const long temp_map_lite_warning[THRESH_COUNT] = {
	115000, 125000, 135000, 145000
};

static const long temp_map_lite_shutdown[THRESH_COUNT] = {
	135000, 145000, 160000, 175000
};

/* Temperature in Milli Celsius reported during stage 0 if no ADC is present */
#define DEFAULT_TEMP			37000

struct qpnp_tm_chip {
	struct regmap			*map;
	struct device			*dev;
	struct thermal_zone_device	*tz_dev;
	unsigned int			subtype;
	unsigned int			dig_revision;
	long				temp;
	unsigned int			thresh;
	unsigned int			stage;
	unsigned int			prev_stage;
	unsigned int			base;
	int				irq;
	/* protects .thresh, .stage and chip registers */
	struct mutex			lock;
	bool				initialized;

	struct iio_channel		*adc;
	const long			(*temp_map)[THRESH_COUNT][STAGE_COUNT];

	bool				has_temp_dac;
	long				temp_dac_map[STAGE_COUNT];
};

/* This array maps from GEN2 alarm state to GEN1 alarm stage */
static const unsigned int alarm_state_map[8] = {0, 1, 1, 2, 2, 3, 3, 3};

static int qpnp_tm_read(struct qpnp_tm_chip *chip, u16 addr, u8 *data)
{
	unsigned int val;
	int ret;

	ret = regmap_read(chip->map, chip->base + addr, &val);
	if (ret < 0)
		return ret;

	*data = val;
	return 0;
}

static int qpnp_tm_write(struct qpnp_tm_chip *chip, u16 addr, u8 data)
{
	return regmap_write(chip->map, chip->base + addr, data);
}

/**
 * qpnp_tm_decode_temp() - return temperature in mC corresponding to the
 *		specified over-temperature stage
 * @chip:		Pointer to the qpnp_tm chip
 * @stage:		Over-temperature stage
 *
 * Return: temperature in mC
 */
static long qpnp_tm_decode_temp(struct qpnp_tm_chip *chip, unsigned int stage)
{
	if (chip->has_temp_dac || chip->subtype == QPNP_TM_SUBTYPE_LITE) {
		if (stage == 0 || stage > STAGE_COUNT)
			return 0;

		return chip->temp_dac_map[stage - 1];
	}

	if (!chip->temp_map || chip->thresh >= THRESH_COUNT || stage == 0 ||
	    stage > STAGE_COUNT)
		return 0;

	return (*chip->temp_map)[chip->thresh][stage - 1];
}

/**
 * qpnp_tm_get_temp_stage() - return over-temperature stage
 * @chip:		Pointer to the qpnp_tm chip
 *
 * Return: stage (GEN1), state (GEN2), or alarm interrupt state (LITE) on
 *	   success; or errno on failure.
 */
static int qpnp_tm_get_temp_stage(struct qpnp_tm_chip *chip)
{
	int ret;
	u16 addr = QPNP_TM_REG_STATUS;
	u8 reg = 0;

	if (chip->subtype == QPNP_TM_SUBTYPE_LITE)
		addr = QPNP_TM_REG_IRQ_STATUS;

	ret = qpnp_tm_read(chip, addr, &reg);
	if (ret < 0)
		return ret;

	if (chip->subtype == QPNP_TM_SUBTYPE_GEN1)
		ret = reg & STATUS_GEN1_STAGE_MASK;
	else if (chip->subtype == QPNP_TM_SUBTYPE_LITE)
		ret = reg & IRQ_STATUS_MASK;
	else
		ret = (reg & STATUS_GEN2_STATE_MASK) >> STATUS_GEN2_STATE_SHIFT;

	return ret;
}

/*
 * This function updates the internal temp value based on the
 * current thermal stage and threshold as well as the previous stage
 */
static int qpnp_tm_update_temp_no_adc(struct qpnp_tm_chip *chip)
{
	unsigned int stage, stage_new, stage_old;
	int ret;

	WARN_ON(!mutex_is_locked(&chip->lock));

	ret = qpnp_tm_get_temp_stage(chip);
	if (ret < 0)
		return ret;
	stage = ret;

	if (chip->subtype == QPNP_TM_SUBTYPE_GEN1
	    || chip->subtype == QPNP_TM_SUBTYPE_LITE) {
		stage_new = stage;
		stage_old = chip->stage;
	} else {
		stage_new = alarm_state_map[stage];
		stage_old = alarm_state_map[chip->stage];
	}

	if (stage_new > stage_old) {
		/* increasing stage, use lower bound */
		chip->temp = qpnp_tm_decode_temp(chip, stage_new)
				+ TEMP_STAGE_HYSTERESIS;
	} else if (stage_new < stage_old) {
		/* decreasing stage, use upper bound */
		chip->temp = qpnp_tm_decode_temp(chip, stage_new + 1)
				- TEMP_STAGE_HYSTERESIS;
	}

	chip->stage = stage;

	return 0;
}

static int qpnp_tm_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct qpnp_tm_chip *chip = tz->devdata;
	int ret, mili_celsius;

	if (!temp)
		return -EINVAL;

	if (!chip->initialized) {
		*temp = DEFAULT_TEMP;
		return 0;
	}

	if (!chip->adc) {
		mutex_lock(&chip->lock);
		ret = qpnp_tm_update_temp_no_adc(chip);
		mutex_unlock(&chip->lock);
		if (ret < 0)
			return ret;
	} else {
		ret = iio_read_channel_processed(chip->adc, &mili_celsius);
		if (ret < 0)
			return ret;

		chip->temp = mili_celsius;
	}

	*temp = chip->temp;

	return 0;
}

static int qpnp_tm_set_temp_dac_thresh(struct qpnp_tm_chip *chip, int trip,
				       int temp)
{
	int ret, temp_cfg;
	u8 reg;

	if (trip < 0 || trip >= STAGE_COUNT) {
		dev_err(chip->dev, "invalid TEMP_DAC trip = %d\n", trip);
		return -EINVAL;
	} else if (temp < TEMP_DAC_MIN || temp > temp_dac_max[trip]) {
		dev_err(chip->dev, "invalid TEMP_DAC temp = %d\n", temp);
		return -EINVAL;
	}

	reg = TEMP_DAC_TEMP_TO_REG(temp);
	temp_cfg = TEMP_DAC_REG_TO_TEMP(reg);

	ret = qpnp_tm_write(chip, QPNP_TM_REG_TEMP_DAC_STG1 + trip, reg);
	if (ret < 0) {
		dev_err(chip->dev, "TEMP_DAC_STG write failed, ret=%d\n", ret);
		return ret;
	}

	chip->temp_dac_map[trip] = temp_cfg;

	return 0;
}

static int qpnp_tm_set_temp_lite_thresh(struct qpnp_tm_chip *chip, int trip,
				       int temp)
{
	int ret, temp_cfg, i;
	const long *temp_map;
	u16 addr;
	u8 reg, thresh;

	if (trip < 0 || trip >= STAGE_COUNT) {
		dev_err(chip->dev, "invalid TEMP_LITE trip = %d\n", trip);
		return -EINVAL;
	}

	switch (trip) {
	case 0:
		temp_map = temp_map_lite_warning;
		addr = QPNP_TM_REG_LITE_TEMP_CFG1;
		break;
	case 1:
		/*
		 * The second trip point is purely in software to facilitate
		 * a controlled shutdown after the warning threshold is crossed
		 * but before the automatic hardware shutdown threshold is
		 * crossed.
		 */
		return 0;
	case 2:
		temp_map = temp_map_lite_shutdown;
		addr = QPNP_TM_REG_LITE_TEMP_CFG2;
		break;
	default:
		return 0;
	}

	if (temp < temp_map[THRESH_MIN] || temp > temp_map[THRESH_MAX]) {
		dev_err(chip->dev, "invalid TEMP_LITE temp = %d\n", temp);
		return -EINVAL;
	}

	thresh = 0;
	temp_cfg = temp_map[thresh];
	for (i = THRESH_MAX; i >= THRESH_MIN; i--) {
		if (temp >= temp_map[i]) {
			thresh = i;
			temp_cfg = temp_map[i];
			break;
		}
	}

	if (temp_cfg == chip->temp_dac_map[trip])
		return 0;

	ret = qpnp_tm_read(chip, addr, &reg);
	if (ret < 0) {
		dev_err(chip->dev, "LITE_TEMP_CFG read failed, ret=%d\n", ret);
		return ret;
	}

	reg &= ~LITE_TEMP_CFG_THRESHOLD_MASK;
	reg |= FIELD_PREP(LITE_TEMP_CFG_THRESHOLD_MASK, thresh);

	ret = qpnp_tm_write(chip, addr, reg);
	if (ret < 0) {
		dev_err(chip->dev, "LITE_TEMP_CFG write failed, ret=%d\n", ret);
		return ret;
	}

	chip->temp_dac_map[trip] = temp_cfg;

	return 0;
}

static int qpnp_tm_update_critical_trip_temp(struct qpnp_tm_chip *chip,
					     int temp)
{
	long stage2_threshold_min = (*chip->temp_map)[THRESH_MIN][1];
	long stage2_threshold_max = (*chip->temp_map)[THRESH_MAX][1];
	bool disable_s2_shutdown = false;
	bool require_s2_shutdown = false;
	u8 reg;

	WARN_ON(!mutex_is_locked(&chip->lock));

	/*
	 * Default: S2 and S3 shutdown enabled, thresholds at
	 * lowest threshold set, monitoring at 25Hz
	 */
	reg = SHUTDOWN_CTRL1_RATE_25HZ;

	if (temp == THERMAL_TEMP_INVALID ||
	    temp < stage2_threshold_min) {
		chip->thresh = THRESH_MIN;
		goto skip;
	}

	if (temp <= stage2_threshold_max) {
		chip->thresh = THRESH_MAX -
			((stage2_threshold_max - temp) /
			 TEMP_THRESH_STEP);
		disable_s2_shutdown = true;
	} else {
		chip->thresh = THRESH_MAX;

		if (chip->adc)
			disable_s2_shutdown = true;
		else
			dev_warn(chip->dev,
				 "No ADC is configured and critical temperature %d mC is above the maximum stage 2 threshold of %ld mC! Configuring stage 2 shutdown at %ld mC.\n",
				 temp, stage2_threshold_max, stage2_threshold_max);
	}

	if (chip->subtype == QPNP_TM_SUBTYPE_GEN2) {
		/*
		 * Check if stage 2 automatic partial shutdown must remain
		 * enabled to avoid potential repeated faults upon reaching
		 * over-temperature stage 3.
		 */
		switch (chip->dig_revision) {
		case 0x0001:
		case 0x0002:
		case 0x0100:
		case 0x0101:
			require_s2_shutdown = true;
			break;
		}
	}

skip:
	reg |= chip->thresh;
	if (disable_s2_shutdown && !require_s2_shutdown)
		reg |= SHUTDOWN_CTRL1_OVERRIDE_S2;

	return qpnp_tm_write(chip, QPNP_TM_REG_SHUTDOWN_CTRL1, reg);
}

static int qpnp_tm_set_trip_temp(struct thermal_zone_device *tz, int trip, int temp)
{
	struct qpnp_tm_chip *chip = tz->devdata;
	const struct thermal_trip *trip_points;
	int ret;

	trip_points = of_thermal_get_trip_points(chip->tz_dev);
	if (!trip_points)
		return -EINVAL;

	if (trip_points[trip].type != THERMAL_TRIP_CRITICAL)
		return 0;

	mutex_lock(&chip->lock);
	ret = qpnp_tm_update_critical_trip_temp(chip, temp);
	mutex_unlock(&chip->lock);

	return ret;
}

static const struct thermal_zone_device_ops qpnp_tm_sensor_ops = {
	.get_temp = qpnp_tm_get_temp,
	.set_trip_temp = qpnp_tm_set_trip_temp,
};

static int qpnp_tm_set_temp_dac_trip_temp(struct thermal_zone_device *tz, int trip, int temp)
{
	struct qpnp_tm_chip *chip = tz->devdata;
	int ret;

	mutex_lock(&chip->lock);
	ret = qpnp_tm_set_temp_dac_thresh(chip, trip, temp);
	mutex_unlock(&chip->lock);

	return ret;
}

static const struct thermal_zone_device_ops qpnp_tm_sensor_temp_dac_ops = {
	.get_temp = qpnp_tm_get_temp,
	.set_trip_temp = qpnp_tm_set_temp_dac_trip_temp,
};

static int qpnp_tm_set_temp_lite_trip_temp(struct thermal_zone_device *tz, int trip, int temp)
{
	struct qpnp_tm_chip *chip = tz->devdata;
	int ret;

	mutex_lock(&chip->lock);
	ret = qpnp_tm_set_temp_lite_thresh(chip, trip, temp);
	mutex_unlock(&chip->lock);

	return ret;
}

static const struct thermal_zone_device_ops qpnp_tm_sensor_temp_lite_ops = {
	.get_temp = qpnp_tm_get_temp,
	.set_trip_temp = qpnp_tm_set_temp_lite_trip_temp,
};

static irqreturn_t qpnp_tm_isr(int irq, void *data)
{
	struct qpnp_tm_chip *chip = data;

	thermal_zone_device_update(chip->tz_dev, THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

static int qpnp_tm_get_critical_trip_temp(struct qpnp_tm_chip *chip)
{
	int ntrips;
	const struct thermal_trip *trips;
	int i;

	ntrips = of_thermal_get_ntrips(chip->tz_dev);
	if (ntrips <= 0)
		return THERMAL_TEMP_INVALID;

	trips = of_thermal_get_trip_points(chip->tz_dev);
	if (!trips)
		return THERMAL_TEMP_INVALID;

	for (i = 0; i < ntrips; i++) {
		if (of_thermal_is_trip_valid(chip->tz_dev, i) &&
		    trips[i].type == THERMAL_TRIP_CRITICAL)
			return trips[i].temperature;
	}

	return THERMAL_TEMP_INVALID;
}

/* Configure TEMP_DAC registers based on DT thermal_zone trips */
static int qpnp_tm_temp_dac_update_trip_temps(struct qpnp_tm_chip *chip)
{
	const struct thermal_trip *trips;
	int ret, ntrips, i;

	ntrips = of_thermal_get_ntrips(chip->tz_dev);
	/* Keep hardware defaults if no DT trips are defined. */
	if (ntrips <= 0)
		return 0;

	trips = of_thermal_get_trip_points(chip->tz_dev);
	if (!trips)
		return -EINVAL;

	for (i = 0; i < ntrips; i++) {
		if (of_thermal_is_trip_valid(chip->tz_dev, i)) {
			ret = qpnp_tm_set_temp_dac_thresh(chip, i,
							  trips[i].temperature);
			if (ret < 0)
				return ret;
		}
	}

	/* Verify that trips are strictly increasing. */
	for (i = 1; i < STAGE_COUNT; i++) {
		if (chip->temp_dac_map[i] <= chip->temp_dac_map[i - 1]) {
			dev_err(chip->dev, "Threshold %d=%ld <= threshold %d=%ld\n",
				i, chip->temp_dac_map[i], i - 1,
				chip->temp_dac_map[i - 1]);
			return -EINVAL;
		}
	}

	return 0;
}

/* Read the hardware default TEMP_DAC stage threshold temperatures */
static int qpnp_tm_temp_dac_init(struct qpnp_tm_chip *chip)
{
	int ret, i;
	u8 reg = 0;

	for (i = 0; i < STAGE_COUNT; i++) {
		ret = qpnp_tm_read(chip, QPNP_TM_REG_TEMP_DAC_STG1 + i, &reg);
		if (ret < 0)
			return ret;

		chip->temp_dac_map[i] = TEMP_DAC_REG_TO_TEMP(reg);
	}

	return 0;
}

/* Configure TEMP_LITE registers based on DT thermal_zone trips */
static int qpnp_tm_temp_lite_update_trip_temps(struct qpnp_tm_chip *chip)
{
	const struct thermal_trip *trips;
	int ret, ntrips, i;

	ntrips = of_thermal_get_ntrips(chip->tz_dev);
	/* Keep hardware defaults if no DT trips are defined. */
	if (ntrips <= 0)
		return 0;

	trips = of_thermal_get_trip_points(chip->tz_dev);
	if (!trips)
		return -EINVAL;

	for (i = 0; i < ntrips; i++) {
		if (of_thermal_is_trip_valid(chip->tz_dev, i)) {
			ret = qpnp_tm_set_temp_lite_thresh(chip, i,
							  trips[i].temperature);
			if (ret < 0)
				return ret;
		}
	}

	/* Verify that trips are strictly increasing. */
	if (chip->temp_dac_map[2] <= chip->temp_dac_map[0]) {
		dev_err(chip->dev, "Threshold 2=%ld <= threshold 0=%ld\n",
			chip->temp_dac_map[2], chip->temp_dac_map[0]);
		return -EINVAL;
	}

	return 0;
}

/* Read the hardware default TEMP_LITE stage threshold temperatures */
static int qpnp_tm_temp_lite_init(struct qpnp_tm_chip *chip)
{
	int ret, thresh;
	u8 reg = 0;

	/*
	 * Store the warning trip temp in temp_dac_map[0] and the shutdown trip
	 * temp in temp_dac_map[2].  The second trip point is purely in software
	 * to facilitate a controlled shutdown after the warning threshold is
	 * crossed but before the automatic hardware shutdown threshold is
	 * crossed.  Thus, there is no register to read for the second trip
	 * point.
	 */
	ret = qpnp_tm_read(chip, QPNP_TM_REG_LITE_TEMP_CFG1, &reg);
	if (ret < 0)
		return ret;

	thresh = FIELD_GET(LITE_TEMP_CFG_THRESHOLD_MASK, reg);
	chip->temp_dac_map[0] = temp_map_lite_warning[thresh];

	ret = qpnp_tm_read(chip, QPNP_TM_REG_LITE_TEMP_CFG2, &reg);
	if (ret < 0)
		return ret;

	thresh = FIELD_GET(LITE_TEMP_CFG_THRESHOLD_MASK, reg);
	chip->temp_dac_map[2] = temp_map_lite_shutdown[thresh];

	return 0;
}

/*
 * This function initializes the internal temp value based on only the
 * current thermal stage and threshold. Setup threshold control and
 * disable shutdown override.
 */
static int qpnp_tm_init(struct qpnp_tm_chip *chip)
{
	unsigned int stage;
	int ret;
	u8 reg = 0;
	int crit_temp;

	mutex_lock(&chip->lock);

	ret = qpnp_tm_read(chip, QPNP_TM_REG_SHUTDOWN_CTRL1, &reg);
	if (ret < 0)
		goto out;

	chip->thresh = reg & SHUTDOWN_CTRL1_THRESHOLD_MASK;
	chip->temp = DEFAULT_TEMP;

	ret = qpnp_tm_get_temp_stage(chip);
	if (ret < 0)
		goto out;
	chip->stage = ret;

	stage = (chip->subtype == QPNP_TM_SUBTYPE_GEN1
		 || chip->subtype == QPNP_TM_SUBTYPE_LITE)
			? chip->stage : alarm_state_map[chip->stage];

	if (stage)
		chip->temp = qpnp_tm_decode_temp(chip, stage);

	if (chip->subtype == QPNP_TM_SUBTYPE_LITE) {
		ret = qpnp_tm_temp_lite_update_trip_temps(chip);
		if (ret < 0)
			goto out;
	} else if (chip->has_temp_dac) {
		ret = qpnp_tm_temp_dac_update_trip_temps(chip);
		if (ret < 0)
			goto out;
	} else {
		crit_temp = qpnp_tm_get_critical_trip_temp(chip);
		ret = qpnp_tm_update_critical_trip_temp(chip, crit_temp);
		if (ret < 0)
			goto out;
	}

	/* Enable the thermal alarm PMIC module in always-on mode. */
	reg = ALARM_CTRL_FORCE_ENABLE;
	ret = qpnp_tm_write(chip, QPNP_TM_REG_ALARM_CTRL, reg);

	chip->initialized = true;

out:
	mutex_unlock(&chip->lock);
	return ret;
}

static int qpnp_tm_probe(struct platform_device *pdev)
{
	struct qpnp_tm_chip *chip;
	struct device_node *node;
	const struct thermal_zone_device_ops *ops;
	u8 type, subtype, dig_major, dig_minor;
	u32 res;
	int ret;

	node = pdev->dev.of_node;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, chip);
	chip->dev = &pdev->dev;

	mutex_init(&chip->lock);

	chip->map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->map)
		return -ENXIO;

	ret = of_property_read_u32(node, "reg", &res);
	if (ret < 0)
		return ret;

	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0)
		return chip->irq;

	/* ADC based measurements are optional */
	chip->adc = devm_iio_channel_get(&pdev->dev, "thermal");
	if (IS_ERR(chip->adc)) {
		ret = PTR_ERR(chip->adc);
		chip->adc = NULL;
		if (ret == -EPROBE_DEFER)
			return ret;
	}

	chip->base = res;

	ret = qpnp_tm_read(chip, QPNP_TM_REG_TYPE, &type);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not read type\n");
		return ret;
	}

	ret = qpnp_tm_read(chip, QPNP_TM_REG_SUBTYPE, &subtype);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not read subtype\n");
		return ret;
	}

	ret = qpnp_tm_read(chip, QPNP_TM_REG_DIG_MAJOR, &dig_major);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not read dig_major\n");
		return ret;
	}

	ret = qpnp_tm_read(chip, QPNP_TM_REG_DIG_MINOR, &dig_minor);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not read dig_minor\n");
		return ret;
	}

	chip->dig_revision = (dig_major << 8) | dig_minor;

	if (type != QPNP_TM_TYPE || (subtype != QPNP_TM_SUBTYPE_GEN1
				     && subtype != QPNP_TM_SUBTYPE_GEN2
				     && subtype != QPNP_TM_SUBTYPE_LITE)) {
		dev_err(&pdev->dev, "invalid type 0x%02x or subtype 0x%02x\n",
			type, subtype);
		return -ENODEV;
	}

	ops = &qpnp_tm_sensor_ops;
	chip->subtype = subtype;
	if (subtype == QPNP_TM_SUBTYPE_GEN2 && dig_major >= 2)
		chip->has_temp_dac = true;
	else if (subtype == QPNP_TM_SUBTYPE_GEN2 && dig_major >= 1)
		chip->temp_map = &temp_map_gen2_v1;
	else if (subtype == QPNP_TM_SUBTYPE_GEN1)
		chip->temp_map = &temp_map_gen1;

	if (chip->has_temp_dac) {
		ops = &qpnp_tm_sensor_temp_dac_ops;
		ret = qpnp_tm_temp_dac_init(chip);
		if (ret < 0)
			return ret;
	}

	if (chip->subtype == QPNP_TM_SUBTYPE_LITE) {
		ops = &qpnp_tm_sensor_temp_lite_ops;
		ret = qpnp_tm_temp_lite_init(chip);
		if (ret < 0)
			return ret;
	}

	/*
	 * Register the sensor before initializing the hardware to be able to
	 * read the trip points. get_temp() returns the default temperature
	 * before the hardware initialization is completed.
	 */
	chip->tz_dev = devm_thermal_of_zone_register(
		&pdev->dev, 0, chip, ops);
	if (IS_ERR(chip->tz_dev)) {
		dev_err(&pdev->dev, "failed to register sensor\n");
		return PTR_ERR(chip->tz_dev);
	}

	ret = qpnp_tm_init(chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "init failed\n");
		return ret;
	}

	if (devm_thermal_add_hwmon_sysfs(chip->tz_dev))
		dev_warn(&pdev->dev,
			 "Failed to add hwmon sysfs attributes\n");

	ret = devm_request_threaded_irq(&pdev->dev, chip->irq, NULL,
					qpnp_tm_isr, IRQF_ONESHOT,
					node->name, chip);
	if (ret < 0)
		return ret;

	thermal_zone_device_update(chip->tz_dev, THERMAL_EVENT_UNSPECIFIED);

	return 0;
}

static int qpnp_tm_restore(struct device *dev)
{
	int ret = 0;
	struct qpnp_tm_chip *chip = dev_get_drvdata(dev);
	struct device_node *node = dev->of_node;
	unsigned long flags;

	if (chip->subtype == QPNP_TM_SUBTYPE_GEN2)
		flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
	else
		flags = IRQF_TRIGGER_RISING;

	if (chip->irq > 0) {
		ret = devm_request_threaded_irq(dev, chip->irq, NULL,
			qpnp_tm_isr, flags | IRQF_ONESHOT, node->name, chip);
		if (ret < 0)
			return ret;
	}

	ret = qpnp_tm_init(chip);
	if (ret < 0)
		dev_err(dev, "init failed\n");

	return ret;
}

static int qpnp_tm_freeze(struct device *dev)
{
	struct qpnp_tm_chip *chip = dev_get_drvdata(dev);

	if (chip->irq > 0)
		devm_free_irq(dev, chip->irq, chip);

	return 0;
}

static const struct dev_pm_ops qpnp_tm_pm_ops = {
	.freeze = qpnp_tm_freeze,
	.restore = qpnp_tm_restore,
};

static void qpnp_tm_shutdown(struct platform_device *pdev)
{
	struct qpnp_tm_chip *chip = platform_get_drvdata(pdev);

	if (chip->irq > 0)
		devm_free_irq(chip->dev, chip->irq, chip);
}

static const struct of_device_id qpnp_tm_match_table[] = {
	{ .compatible = "qcom,spmi-temp-alarm" },
	{ }
};
MODULE_DEVICE_TABLE(of, qpnp_tm_match_table);

static struct platform_driver qpnp_tm_driver = {
	.driver = {
		.name = "spmi-temp-alarm",
		.of_match_table = qpnp_tm_match_table,
		.pm = &qpnp_tm_pm_ops,
	},
	.probe  = qpnp_tm_probe,
	.shutdown = qpnp_tm_shutdown,
};
module_platform_driver(qpnp_tm_driver);

MODULE_ALIAS("platform:spmi-temp-alarm");
MODULE_DESCRIPTION("QPNP PMIC Temperature Alarm driver");
MODULE_LICENSE("GPL v2");
