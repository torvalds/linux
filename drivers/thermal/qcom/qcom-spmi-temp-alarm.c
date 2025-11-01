// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2015, 2017, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>

#include "../thermal_hwmon.h"

#define QPNP_TM_REG_DIG_MINOR		0x00
#define QPNP_TM_REG_DIG_MAJOR		0x01
#define QPNP_TM_REG_TYPE		0x04
#define QPNP_TM_REG_SUBTYPE		0x05
#define QPNP_TM_REG_STATUS		0x08
#define QPNP_TM_REG_IRQ_STATUS		0x10
#define QPNP_TM_REG_SHUTDOWN_CTRL1	0x40
#define QPNP_TM_REG_ALARM_CTRL		0x46

/* TEMP_DAC_STGx registers are only present for TEMP_GEN2 v2.0 */
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

/* IRQ status only needed for TEMP_ALARM_LITE */
#define IRQ_STATUS_MASK			BIT(0)

#define SHUTDOWN_CTRL1_OVERRIDE_STAGE2	BIT(6)
#define SHUTDOWN_CTRL1_THRESHOLD_MASK	GENMASK(1, 0)

#define SHUTDOWN_CTRL1_RATE_25HZ	BIT(3)

#define ALARM_CTRL_FORCE_ENABLE		BIT(7)

#define LITE_TEMP_CFG_THRESHOLD_MASK	GENMASK(3, 2)

#define THRESH_COUNT			4
#define STAGE_COUNT			3

enum overtemp_stage {
	STAGE1 = 0,
	STAGE2,
	STAGE3,
};

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

static const long temp_lite_warning_map[THRESH_COUNT] = {
	115000, 125000, 135000, 145000
};

static const long temp_lite_shutdown_map[THRESH_COUNT] = {
	135000, 145000, 160000, 175000
};

/* Temperature in Milli Celsius reported during stage 0 if no ADC is present */
#define DEFAULT_TEMP			37000

struct qpnp_tm_chip;

struct spmi_temp_alarm_data {
	const struct thermal_zone_device_ops *ops;
	const long (*temp_map)[THRESH_COUNT][STAGE_COUNT];
	int (*sync_thresholds)(struct qpnp_tm_chip *chip);
	int (*get_temp_stage)(struct qpnp_tm_chip *chip);
	int (*configure_trip_temps)(struct qpnp_tm_chip *chip);
};

struct qpnp_tm_chip {
	struct regmap			*map;
	struct device			*dev;
	struct thermal_zone_device	*tz_dev;
	const struct spmi_temp_alarm_data *data;
	unsigned int			subtype;
	long				temp;
	unsigned int			stage;
	unsigned int			base;
	unsigned int			ntrips;
	/* protects .thresh, .stage and chip registers */
	struct mutex			lock;
	bool				initialized;
	bool				require_stage2_shutdown;
	long				temp_thresh_map[STAGE_COUNT];

	struct iio_channel		*adc;
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
	if (stage == 0 || stage > STAGE_COUNT)
		return 0;

	return chip->temp_thresh_map[stage - 1];
}

/**
 * qpnp_tm_gen1_get_temp_stage() - return over-temperature stage
 * @chip:		Pointer to the qpnp_tm chip
 *
 * Return: stage on success, or errno on failure.
 */
static int qpnp_tm_gen1_get_temp_stage(struct qpnp_tm_chip *chip)
{
	int ret;
	u8 reg;

	ret = qpnp_tm_read(chip, QPNP_TM_REG_STATUS, &reg);
	if (ret < 0)
		return ret;

	return FIELD_GET(STATUS_GEN1_STAGE_MASK, reg);
}

/**
 * qpnp_tm_gen2_get_temp_stage() - return over-temperature stage
 * @chip:		Pointer to the qpnp_tm chip
 *
 * Return: stage on success, or errno on failure.
 */
static int qpnp_tm_gen2_get_temp_stage(struct qpnp_tm_chip *chip)
{
	int ret;
	u8 reg;

	ret = qpnp_tm_read(chip, QPNP_TM_REG_STATUS, &reg);
	if (ret < 0)
		return ret;

	ret = FIELD_GET(STATUS_GEN2_STATE_MASK, reg);

	return alarm_state_map[ret];
}

/**
 * qpnp_tm_lite_get_temp_stage() - return over-temperature stage
 * @chip:		Pointer to the qpnp_tm chip
 *
 * Return: alarm interrupt state on success, or errno on failure.
 */
static int qpnp_tm_lite_get_temp_stage(struct qpnp_tm_chip *chip)
{
	u8 reg = 0;
	int ret;

	ret = qpnp_tm_read(chip, QPNP_TM_REG_IRQ_STATUS, &reg);
	if (ret < 0)
		return ret;

	return FIELD_GET(IRQ_STATUS_MASK, reg);
}

/*
 * This function updates the internal temp value based on the
 * current thermal stage and threshold as well as the previous stage
 */
static int qpnp_tm_update_temp_no_adc(struct qpnp_tm_chip *chip)
{
	unsigned int stage_new, stage_old;
	int ret;

	WARN_ON(!mutex_is_locked(&chip->lock));

	ret = chip->data->get_temp_stage(chip);
	if (ret < 0)
		return ret;
	stage_new = ret;
	stage_old = chip->stage;

	if (stage_new > stage_old) {
		/* increasing stage, use lower bound */
		chip->temp = qpnp_tm_decode_temp(chip, stage_new)
				+ TEMP_STAGE_HYSTERESIS;
	} else if (stage_new < stage_old) {
		/* decreasing stage, use upper bound */
		chip->temp = qpnp_tm_decode_temp(chip, stage_new + 1)
				- TEMP_STAGE_HYSTERESIS;
	}

	chip->stage = stage_new;

	return 0;
}

static int qpnp_tm_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct qpnp_tm_chip *chip = thermal_zone_device_priv(tz);
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

static int qpnp_tm_update_critical_trip_temp(struct qpnp_tm_chip *chip,
					     int temp)
{
	long stage2_threshold_min = (*chip->data->temp_map)[THRESH_MIN][STAGE2];
	long stage2_threshold_max = (*chip->data->temp_map)[THRESH_MAX][STAGE2];
	bool disable_stage2_shutdown = false;
	u8 reg, threshold;

	WARN_ON(!mutex_is_locked(&chip->lock));

	/*
	 * Default: Stage 2 and Stage 3 shutdown enabled, thresholds at
	 * lowest threshold set, monitoring at 25Hz
	 */
	reg = SHUTDOWN_CTRL1_RATE_25HZ;

	if (temp == THERMAL_TEMP_INVALID ||
	    temp < stage2_threshold_min) {
		threshold = THRESH_MIN;
		goto skip;
	}

	if (temp <= stage2_threshold_max) {
		threshold = THRESH_MAX -
			((stage2_threshold_max - temp) /
			 TEMP_THRESH_STEP);
		disable_stage2_shutdown = true;
	} else {
		threshold = THRESH_MAX;

		if (chip->adc)
			disable_stage2_shutdown = true;
		else
			dev_warn(chip->dev,
				 "No ADC is configured and critical temperature %d mC is above the maximum stage 2 threshold of %ld mC! Configuring stage 2 shutdown at %ld mC.\n",
				 temp, stage2_threshold_max, stage2_threshold_max);
	}

skip:
	memcpy(chip->temp_thresh_map, chip->data->temp_map[threshold],
		sizeof(chip->temp_thresh_map));
	reg |= threshold;
	if (disable_stage2_shutdown && !chip->require_stage2_shutdown)
		reg |= SHUTDOWN_CTRL1_OVERRIDE_STAGE2;

	return qpnp_tm_write(chip, QPNP_TM_REG_SHUTDOWN_CTRL1, reg);
}

static int qpnp_tm_set_trip_temp(struct thermal_zone_device *tz,
				 const struct thermal_trip *trip, int temp)
{
	struct qpnp_tm_chip *chip = thermal_zone_device_priv(tz);
	int ret;

	if (trip->type != THERMAL_TRIP_CRITICAL)
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

static int qpnp_tm_gen2_rev2_set_temp_thresh(struct qpnp_tm_chip *chip, unsigned int trip, int temp)
{
	int ret, temp_cfg;
	u8 reg;

	WARN_ON(!mutex_is_locked(&chip->lock));

	if (trip >= STAGE_COUNT) {
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

	chip->temp_thresh_map[trip] = temp_cfg;

	return 0;
}

static int qpnp_tm_gen2_rev2_set_trip_temp(struct thermal_zone_device *tz,
					   const struct thermal_trip *trip, int temp)
{
	unsigned int trip_index = THERMAL_TRIP_PRIV_TO_INT(trip->priv);
	struct qpnp_tm_chip *chip = thermal_zone_device_priv(tz);
	int ret;

	mutex_lock(&chip->lock);
	ret = qpnp_tm_gen2_rev2_set_temp_thresh(chip, trip_index, temp);
	mutex_unlock(&chip->lock);

	return ret;
}

static const struct thermal_zone_device_ops qpnp_tm_gen2_rev2_sensor_ops = {
	.get_temp = qpnp_tm_get_temp,
	.set_trip_temp = qpnp_tm_gen2_rev2_set_trip_temp,
};

static int qpnp_tm_lite_set_temp_thresh(struct qpnp_tm_chip *chip, unsigned int trip, int temp)
{
	int ret, temp_cfg, i;
	const long *temp_map;
	u8 reg, thresh;
	u16 addr;

	WARN_ON(!mutex_is_locked(&chip->lock));

	if (trip >= STAGE_COUNT) {
		dev_err(chip->dev, "invalid TEMP_LITE trip = %d\n", trip);
		return -EINVAL;
	}

	switch (trip) {
	case 0:
		temp_map = temp_lite_warning_map;
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
		temp_map = temp_lite_shutdown_map;
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

	if (temp_cfg == chip->temp_thresh_map[trip])
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

	chip->temp_thresh_map[trip] = temp_cfg;

	return 0;
}

static int qpnp_tm_lite_set_trip_temp(struct thermal_zone_device *tz,
				      const struct thermal_trip *trip, int temp)
{
	unsigned int trip_index = THERMAL_TRIP_PRIV_TO_INT(trip->priv);
	struct qpnp_tm_chip *chip = thermal_zone_device_priv(tz);
	int ret;

	mutex_lock(&chip->lock);
	ret = qpnp_tm_lite_set_temp_thresh(chip, trip_index, temp);
	mutex_unlock(&chip->lock);

	return ret;
}

static const struct thermal_zone_device_ops qpnp_tm_lite_sensor_ops = {
	.get_temp = qpnp_tm_get_temp,
	.set_trip_temp = qpnp_tm_lite_set_trip_temp,
};

static irqreturn_t qpnp_tm_isr(int irq, void *data)
{
	struct qpnp_tm_chip *chip = data;

	thermal_zone_device_update(chip->tz_dev, THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

/* Read the hardware default stage threshold temperatures */
static int qpnp_tm_sync_thresholds(struct qpnp_tm_chip *chip)
{
	u8 reg, threshold;
	int ret;

	ret = qpnp_tm_read(chip, QPNP_TM_REG_SHUTDOWN_CTRL1, &reg);
	if (ret < 0)
		return ret;

	threshold = reg & SHUTDOWN_CTRL1_THRESHOLD_MASK;
	memcpy(chip->temp_thresh_map, chip->data->temp_map[threshold],
		sizeof(chip->temp_thresh_map));

	return ret;
}

static int qpnp_tm_configure_trip_temp(struct qpnp_tm_chip *chip)
{
	int crit_temp, ret;

	ret = thermal_zone_get_crit_temp(chip->tz_dev, &crit_temp);
	if (ret)
		crit_temp = THERMAL_TEMP_INVALID;

	mutex_lock(&chip->lock);
	ret = qpnp_tm_update_critical_trip_temp(chip, crit_temp);
	mutex_unlock(&chip->lock);

	return ret;
}

/* Configure TEMP_DAC registers based on DT thermal_zone trips */
static int qpnp_tm_gen2_rev2_configure_trip_temps_cb(struct thermal_trip *trip, void *data)
{
	struct qpnp_tm_chip *chip = data;
	int ret;

	mutex_lock(&chip->lock);
	trip->priv = THERMAL_INT_TO_TRIP_PRIV(chip->ntrips);
	ret = qpnp_tm_gen2_rev2_set_temp_thresh(chip, chip->ntrips, trip->temperature);
	chip->ntrips++;
	mutex_unlock(&chip->lock);

	return ret;
}

static int qpnp_tm_gen2_rev2_configure_trip_temps(struct qpnp_tm_chip *chip)
{
	int ret, i;

	ret = thermal_zone_for_each_trip(chip->tz_dev,
					 qpnp_tm_gen2_rev2_configure_trip_temps_cb, chip);
	if (ret < 0)
		return ret;

	/* Verify that trips are strictly increasing. */
	for (i = 1; i < STAGE_COUNT; i++) {
		if (chip->temp_thresh_map[i] <= chip->temp_thresh_map[i - 1]) {
			dev_err(chip->dev, "Threshold %d=%ld <= threshold %d=%ld\n",
				i, chip->temp_thresh_map[i], i - 1,
				chip->temp_thresh_map[i - 1]);
			return -EINVAL;
		}
	}

	return 0;
}

/* Read the hardware default TEMP_DAC stage threshold temperatures */
static int qpnp_tm_gen2_rev2_sync_thresholds(struct qpnp_tm_chip *chip)
{
	int ret, i;
	u8 reg = 0;

	for (i = 0; i < STAGE_COUNT; i++) {
		ret = qpnp_tm_read(chip, QPNP_TM_REG_TEMP_DAC_STG1 + i, &reg);
		if (ret < 0)
			return ret;

		chip->temp_thresh_map[i] = TEMP_DAC_REG_TO_TEMP(reg);
	}

	return 0;
}

/* Configure TEMP_LITE registers based on DT thermal_zone trips */
static int qpnp_tm_lite_configure_trip_temps_cb(struct thermal_trip *trip, void *data)
{
	struct qpnp_tm_chip *chip = data;
	int ret;

	mutex_lock(&chip->lock);
	trip->priv = THERMAL_INT_TO_TRIP_PRIV(chip->ntrips);
	ret = qpnp_tm_lite_set_temp_thresh(chip, chip->ntrips, trip->temperature);
	chip->ntrips++;
	mutex_unlock(&chip->lock);

	return ret;
}

static int qpnp_tm_lite_configure_trip_temps(struct qpnp_tm_chip *chip)
{
	int ret;

	ret = thermal_zone_for_each_trip(chip->tz_dev, qpnp_tm_lite_configure_trip_temps_cb, chip);
	if (ret < 0)
		return ret;

	/* Verify that trips are strictly increasing. */
	if (chip->temp_thresh_map[2] <= chip->temp_thresh_map[0]) {
		dev_err(chip->dev, "Threshold 2=%ld <= threshold 0=%ld\n",
			chip->temp_thresh_map[2], chip->temp_thresh_map[0]);
		return -EINVAL;
	}

	return 0;
}

/* Read the hardware default TEMP_LITE stage threshold temperatures */
static int qpnp_tm_lite_sync_thresholds(struct qpnp_tm_chip *chip)
{
	int ret, thresh;
	u8 reg = 0;

	/*
	 * Store the warning trip temp in temp_thresh_map[0] and the shutdown trip
	 * temp in temp_thresh_map[2].  The second trip point is purely in software
	 * to facilitate a controlled shutdown after the warning threshold is
	 * crossed but before the automatic hardware shutdown threshold is
	 * crossed.  Thus, there is no register to read for the second trip
	 * point.
	 */
	ret = qpnp_tm_read(chip, QPNP_TM_REG_LITE_TEMP_CFG1, &reg);
	if (ret < 0)
		return ret;

	thresh = FIELD_GET(LITE_TEMP_CFG_THRESHOLD_MASK, reg);
	chip->temp_thresh_map[0] = temp_lite_warning_map[thresh];

	ret = qpnp_tm_read(chip, QPNP_TM_REG_LITE_TEMP_CFG2, &reg);
	if (ret < 0)
		return ret;

	thresh = FIELD_GET(LITE_TEMP_CFG_THRESHOLD_MASK, reg);
	chip->temp_thresh_map[2] = temp_lite_shutdown_map[thresh];

	return 0;
}

static const struct spmi_temp_alarm_data spmi_temp_alarm_data = {
	.ops = &qpnp_tm_sensor_ops,
	.temp_map = &temp_map_gen1,
	.sync_thresholds = qpnp_tm_sync_thresholds,
	.configure_trip_temps = qpnp_tm_configure_trip_temp,
	.get_temp_stage = qpnp_tm_gen1_get_temp_stage,
};

static const struct spmi_temp_alarm_data spmi_temp_alarm_gen2_data = {
	.ops = &qpnp_tm_sensor_ops,
	.temp_map = &temp_map_gen1,
	.sync_thresholds = qpnp_tm_sync_thresholds,
	.configure_trip_temps = qpnp_tm_configure_trip_temp,
	.get_temp_stage = qpnp_tm_gen2_get_temp_stage,
};

static const struct spmi_temp_alarm_data spmi_temp_alarm_gen2_rev1_data = {
	.ops = &qpnp_tm_sensor_ops,
	.temp_map = &temp_map_gen2_v1,
	.sync_thresholds = qpnp_tm_sync_thresholds,
	.configure_trip_temps = qpnp_tm_configure_trip_temp,
	.get_temp_stage = qpnp_tm_gen2_get_temp_stage,
};

static const struct spmi_temp_alarm_data spmi_temp_alarm_gen2_rev2_data = {
	.ops = &qpnp_tm_gen2_rev2_sensor_ops,
	.sync_thresholds = qpnp_tm_gen2_rev2_sync_thresholds,
	.configure_trip_temps = qpnp_tm_gen2_rev2_configure_trip_temps,
	.get_temp_stage = qpnp_tm_gen2_get_temp_stage,
};

static const struct spmi_temp_alarm_data spmi_temp_alarm_lite_data = {
	.ops = &qpnp_tm_lite_sensor_ops,
	.sync_thresholds = qpnp_tm_lite_sync_thresholds,
	.configure_trip_temps = qpnp_tm_lite_configure_trip_temps,
	.get_temp_stage = qpnp_tm_lite_get_temp_stage,
};

/*
 * This function initializes the internal temp value based on only the
 * current thermal stage and threshold.
 */
static int qpnp_tm_threshold_init(struct qpnp_tm_chip *chip)
{
	int ret;

	ret = chip->data->sync_thresholds(chip);
	if (ret < 0)
		return ret;

	ret = chip->data->get_temp_stage(chip);
	if (ret < 0)
		return ret;
	chip->stage = ret;
	chip->temp = DEFAULT_TEMP;

	if (chip->stage)
		chip->temp = qpnp_tm_decode_temp(chip, chip->stage);

	return ret;
}

/* This function initializes threshold control and disables shutdown override. */
static int qpnp_tm_init(struct qpnp_tm_chip *chip)
{
	int ret;
	u8 reg;

	ret = chip->data->configure_trip_temps(chip);
	if (ret < 0)
		return ret;

	/* Enable the thermal alarm PMIC module in always-on mode. */
	reg = ALARM_CTRL_FORCE_ENABLE;
	ret = qpnp_tm_write(chip, QPNP_TM_REG_ALARM_CTRL, reg);

	chip->initialized = true;

	return ret;
}

static int qpnp_tm_probe(struct platform_device *pdev)
{
	struct qpnp_tm_chip *chip;
	struct device_node *node;
	u8 type, subtype, dig_major, dig_minor;
	u32 res, dig_revision;
	int ret, irq;

	node = pdev->dev.of_node;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;

	mutex_init(&chip->lock);

	chip->map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->map)
		return -ENXIO;

	ret = of_property_read_u32(node, "reg", &res);
	if (ret < 0)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

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
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "could not read type\n");

	ret = qpnp_tm_read(chip, QPNP_TM_REG_SUBTYPE, &subtype);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "could not read subtype\n");

	ret = qpnp_tm_read(chip, QPNP_TM_REG_DIG_MAJOR, &dig_major);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "could not read dig_major\n");

	ret = qpnp_tm_read(chip, QPNP_TM_REG_DIG_MINOR, &dig_minor);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "could not read dig_minor\n");

	if (type != QPNP_TM_TYPE || (subtype != QPNP_TM_SUBTYPE_GEN1
				     && subtype != QPNP_TM_SUBTYPE_GEN2
				     && subtype != QPNP_TM_SUBTYPE_LITE)) {
		dev_err(&pdev->dev, "invalid type 0x%02x or subtype 0x%02x\n",
			type, subtype);
		return -ENODEV;
	}

	chip->subtype = subtype;
	if (subtype == QPNP_TM_SUBTYPE_GEN1)
		chip->data = &spmi_temp_alarm_data;
	else if (subtype == QPNP_TM_SUBTYPE_GEN2 && dig_major == 0)
		chip->data = &spmi_temp_alarm_gen2_data;
	else if (subtype == QPNP_TM_SUBTYPE_GEN2 && dig_major == 1)
		chip->data = &spmi_temp_alarm_gen2_rev1_data;
	else if (subtype == QPNP_TM_SUBTYPE_GEN2 && dig_major >= 2)
		chip->data = &spmi_temp_alarm_gen2_rev2_data;
	else if (subtype == QPNP_TM_SUBTYPE_LITE)
		chip->data = &spmi_temp_alarm_lite_data;
	else
		return -ENODEV;

	if (chip->subtype == QPNP_TM_SUBTYPE_GEN2) {
		dig_revision = (dig_major << 8) | dig_minor;
		/*
		 * Check if stage 2 automatic partial shutdown must remain
		 * enabled to avoid potential repeated faults upon reaching
		 * over-temperature stage 3.
		 */
		switch (dig_revision) {
		case 0x0001:
		case 0x0002:
		case 0x0100:
		case 0x0101:
			chip->require_stage2_shutdown = true;
			break;
		}
	}

	ret = qpnp_tm_threshold_init(chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "threshold init failed\n");

	/*
	 * Register the sensor before initializing the hardware to be able to
	 * read the trip points. get_temp() returns the default temperature
	 * before the hardware initialization is completed.
	 */
	chip->tz_dev = devm_thermal_of_zone_register(
		&pdev->dev, 0, chip, chip->data->ops);
	if (IS_ERR(chip->tz_dev))
		return dev_err_probe(&pdev->dev, PTR_ERR(chip->tz_dev),
				     "failed to register sensor\n");

	ret = qpnp_tm_init(chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "init failed\n");

	devm_thermal_add_hwmon_sysfs(&pdev->dev, chip->tz_dev);

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL, qpnp_tm_isr,
					IRQF_ONESHOT, node->name, chip);
	if (ret < 0)
		return ret;

	thermal_zone_device_update(chip->tz_dev, THERMAL_EVENT_UNSPECIFIED);

	return 0;
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
	},
	.probe  = qpnp_tm_probe,
};
module_platform_driver(qpnp_tm_driver);

MODULE_ALIAS("platform:spmi-temp-alarm");
MODULE_DESCRIPTION("QPNP PMIC Temperature Alarm driver");
MODULE_LICENSE("GPL v2");
