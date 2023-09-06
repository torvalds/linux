// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2015, 2017, 2020, The Linux Foundation. All rights reserved.
 */

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

#define QPNP_TM_REG_DIG_MAJOR		0x01
#define QPNP_TM_REG_TYPE		0x04
#define QPNP_TM_REG_SUBTYPE		0x05
#define QPNP_TM_REG_STATUS		0x08
#define QPNP_TM_REG_SHUTDOWN_CTRL1	0x40
#define QPNP_TM_REG_ALARM_CTRL		0x46

#define QPNP_TM_TYPE			0x09
#define QPNP_TM_SUBTYPE_GEN1		0x08
#define QPNP_TM_SUBTYPE_GEN2		0x09

#define STATUS_GEN1_STAGE_MASK		GENMASK(1, 0)
#define STATUS_GEN2_STATE_MASK		GENMASK(6, 4)
#define STATUS_GEN2_STATE_SHIFT		4

#define SHUTDOWN_CTRL1_OVERRIDE_S2	BIT(6)
#define SHUTDOWN_CTRL1_THRESHOLD_MASK	GENMASK(1, 0)

#define SHUTDOWN_CTRL1_RATE_25HZ	BIT(3)

#define ALARM_CTRL_FORCE_ENABLE		BIT(7)

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

/* Temperature in Milli Celsius reported during stage 0 if no ADC is present */
#define DEFAULT_TEMP			37000

struct qpnp_tm_chip {
	struct regmap			*map;
	struct device			*dev;
	struct thermal_zone_device	*tz_dev;
	unsigned int			subtype;
	long				temp;
	unsigned int			thresh;
	unsigned int			stage;
	unsigned int			prev_stage;
	unsigned int			base;
	/* protects .thresh, .stage and chip registers */
	struct mutex			lock;
	bool				initialized;

	struct iio_channel		*adc;
	const long			(*temp_map)[THRESH_COUNT][STAGE_COUNT];
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
	if (!chip->temp_map || chip->thresh >= THRESH_COUNT || stage == 0 ||
	    stage > STAGE_COUNT)
		return 0;

	return (*chip->temp_map)[chip->thresh][stage - 1];
}

/**
 * qpnp_tm_get_temp_stage() - return over-temperature stage
 * @chip:		Pointer to the qpnp_tm chip
 *
 * Return: stage (GEN1) or state (GEN2) on success, or errno on failure.
 */
static int qpnp_tm_get_temp_stage(struct qpnp_tm_chip *chip)
{
	int ret;
	u8 reg = 0;

	ret = qpnp_tm_read(chip, QPNP_TM_REG_STATUS, &reg);
	if (ret < 0)
		return ret;

	if (chip->subtype == QPNP_TM_SUBTYPE_GEN1)
		ret = reg & STATUS_GEN1_STAGE_MASK;
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

	if (chip->subtype == QPNP_TM_SUBTYPE_GEN1) {
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
	long stage2_threshold_min = (*chip->temp_map)[THRESH_MIN][1];
	long stage2_threshold_max = (*chip->temp_map)[THRESH_MAX][1];
	bool disable_s2_shutdown = false;
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

skip:
	reg |= chip->thresh;
	if (disable_s2_shutdown)
		reg |= SHUTDOWN_CTRL1_OVERRIDE_S2;

	return qpnp_tm_write(chip, QPNP_TM_REG_SHUTDOWN_CTRL1, reg);
}

static int qpnp_tm_set_trip_temp(struct thermal_zone_device *tz, int trip_id, int temp)
{
	struct qpnp_tm_chip *chip = thermal_zone_device_priv(tz);
	struct thermal_trip trip;
	int ret;

	ret = __thermal_zone_get_trip(chip->tz_dev, trip_id, &trip);
	if (ret)
		return ret;

	if (trip.type != THERMAL_TRIP_CRITICAL)
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

static irqreturn_t qpnp_tm_isr(int irq, void *data)
{
	struct qpnp_tm_chip *chip = data;

	thermal_zone_device_update(chip->tz_dev, THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

static int qpnp_tm_get_critical_trip_temp(struct qpnp_tm_chip *chip)
{
	struct thermal_trip trip;
	int i, ret;

	for (i = 0; i < thermal_zone_get_num_trips(chip->tz_dev); i++) {

		ret = thermal_zone_get_trip(chip->tz_dev, i, &trip);
		if (ret)
			continue;

		if (trip.type == THERMAL_TRIP_CRITICAL)
			return trip.temperature;
	}

	return THERMAL_TEMP_INVALID;
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

	stage = chip->subtype == QPNP_TM_SUBTYPE_GEN1
		? chip->stage : alarm_state_map[chip->stage];

	if (stage)
		chip->temp = qpnp_tm_decode_temp(chip, stage);

	mutex_unlock(&chip->lock);

	crit_temp = qpnp_tm_get_critical_trip_temp(chip);

	mutex_lock(&chip->lock);

	ret = qpnp_tm_update_critical_trip_temp(chip, crit_temp);
	if (ret < 0)
		goto out;

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
	u8 type, subtype, dig_major;
	u32 res;
	int ret, irq;

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

	if (type != QPNP_TM_TYPE || (subtype != QPNP_TM_SUBTYPE_GEN1
				     && subtype != QPNP_TM_SUBTYPE_GEN2)) {
		dev_err(&pdev->dev, "invalid type 0x%02x or subtype 0x%02x\n",
			type, subtype);
		return -ENODEV;
	}

	chip->subtype = subtype;
	if (subtype == QPNP_TM_SUBTYPE_GEN2 && dig_major >= 1)
		chip->temp_map = &temp_map_gen2_v1;
	else
		chip->temp_map = &temp_map_gen1;

	/*
	 * Register the sensor before initializing the hardware to be able to
	 * read the trip points. get_temp() returns the default temperature
	 * before the hardware initialization is completed.
	 */
	chip->tz_dev = devm_thermal_of_zone_register(
		&pdev->dev, 0, chip, &qpnp_tm_sensor_ops);
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
