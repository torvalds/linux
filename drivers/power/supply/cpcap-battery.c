/*
 * Battery driver for CPCAP PMIC
 *
 * Copyright (C) 2017 Tony Lindgren <tony@atomide.com>
 *
 * Some parts of the code based on earlie Motorola mapphone Linux kernel
 * drivers:
 *
 * Copyright (C) 2009-2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

#include <linux/iio/consumer.h>
#include <linux/iio/types.h>
#include <linux/mfd/motorola-cpcap.h>

/*
 * Register bit defines for CPCAP_REG_BPEOL. Some of these seem to
 * map to MC13783UG.pdf "Table 5-19. Register 13, Power Control 0"
 * to enable BATTDETEN, LOBAT and EOL features. We currently use
 * LOBAT interrupts instead of EOL.
 */
#define CPCAP_REG_BPEOL_BIT_EOL9	BIT(9)	/* Set for EOL irq */
#define CPCAP_REG_BPEOL_BIT_EOL8	BIT(8)	/* Set for EOL irq */
#define CPCAP_REG_BPEOL_BIT_UNKNOWN7	BIT(7)
#define CPCAP_REG_BPEOL_BIT_UNKNOWN6	BIT(6)
#define CPCAP_REG_BPEOL_BIT_UNKNOWN5	BIT(5)
#define CPCAP_REG_BPEOL_BIT_EOL_MULTI	BIT(4)	/* Set for multiple EOL irqs */
#define CPCAP_REG_BPEOL_BIT_UNKNOWN3	BIT(3)
#define CPCAP_REG_BPEOL_BIT_UNKNOWN2	BIT(2)
#define CPCAP_REG_BPEOL_BIT_BATTDETEN	BIT(1)	/* Enable battery detect */
#define CPCAP_REG_BPEOL_BIT_EOLSEL	BIT(0)	/* BPDET = 0, EOL = 1 */

/*
 * Register bit defines for CPCAP_REG_CCC1. These seem similar to the twl6030
 * coulomb counter registers rather than the mc13892 registers. Both twl6030
 * and mc13892 set bits 2 and 1 to reset and clear registers. But mc13892
 * sets bit 0 to start the coulomb counter while twl6030 sets bit 0 to stop
 * the coulomb counter like cpcap does. So for now, we use the twl6030 style
 * naming for the registers.
 */
#define CPCAP_REG_CCC1_ACTIVE_MODE1	BIT(4)	/* Update rate */
#define CPCAP_REG_CCC1_ACTIVE_MODE0	BIT(3)	/* Update rate */
#define CPCAP_REG_CCC1_AUTOCLEAR	BIT(2)	/* Resets sample registers */
#define CPCAP_REG_CCC1_CAL_EN		BIT(1)	/* Clears after write in 1s */
#define CPCAP_REG_CCC1_PAUSE		BIT(0)	/* Stop counters, allow write */
#define CPCAP_REG_CCC1_RESET_MASK	(CPCAP_REG_CCC1_AUTOCLEAR | \
					 CPCAP_REG_CCC1_CAL_EN)

#define CPCAP_REG_CCCC2_RATE1		BIT(5)
#define CPCAP_REG_CCCC2_RATE0		BIT(4)
#define CPCAP_REG_CCCC2_ENABLE		BIT(3)

#define CPCAP_BATTERY_CC_SAMPLE_PERIOD_MS	250

enum {
	CPCAP_BATTERY_IIO_BATTDET,
	CPCAP_BATTERY_IIO_VOLTAGE,
	CPCAP_BATTERY_IIO_CHRG_CURRENT,
	CPCAP_BATTERY_IIO_BATT_CURRENT,
	CPCAP_BATTERY_IIO_NR,
};

enum cpcap_battery_irq_action {
	CPCAP_BATTERY_IRQ_ACTION_NONE,
	CPCAP_BATTERY_IRQ_ACTION_CC_CAL_DONE,
	CPCAP_BATTERY_IRQ_ACTION_BATTERY_LOW,
	CPCAP_BATTERY_IRQ_ACTION_POWEROFF,
};

struct cpcap_interrupt_desc {
	const char *name;
	struct list_head node;
	int irq;
	enum cpcap_battery_irq_action action;
};

struct cpcap_battery_config {
	int cd_factor;
	struct power_supply_info info;
	struct power_supply_battery_info bat;
};

struct cpcap_coulomb_counter_data {
	s32 sample;		/* 24 or 32 bits */
	s32 accumulator;
	s16 offset;		/* 9 bits */
	s16 integrator;		/* 13 or 16 bits */
};

enum cpcap_battery_state {
	CPCAP_BATTERY_STATE_PREVIOUS,
	CPCAP_BATTERY_STATE_LATEST,
	CPCAP_BATTERY_STATE_NR,
};

struct cpcap_battery_state_data {
	int voltage;
	int current_ua;
	int counter_uah;
	int temperature;
	ktime_t time;
	struct cpcap_coulomb_counter_data cc;
};

struct cpcap_battery_ddata {
	struct device *dev;
	struct regmap *reg;
	struct list_head irq_list;
	struct iio_channel *channels[CPCAP_BATTERY_IIO_NR];
	struct power_supply *psy;
	struct cpcap_battery_config config;
	struct cpcap_battery_state_data state[CPCAP_BATTERY_STATE_NR];
	u32 cc_lsb;		/* μAms per LSB */
	atomic_t active;
	int status;
	u16 vendor;
};

#define CPCAP_NO_BATTERY	-400

static struct cpcap_battery_state_data *
cpcap_battery_get_state(struct cpcap_battery_ddata *ddata,
			enum cpcap_battery_state state)
{
	if (state >= CPCAP_BATTERY_STATE_NR)
		return NULL;

	return &ddata->state[state];
}

static struct cpcap_battery_state_data *
cpcap_battery_latest(struct cpcap_battery_ddata *ddata)
{
	return cpcap_battery_get_state(ddata, CPCAP_BATTERY_STATE_LATEST);
}

static struct cpcap_battery_state_data *
cpcap_battery_previous(struct cpcap_battery_ddata *ddata)
{
	return cpcap_battery_get_state(ddata, CPCAP_BATTERY_STATE_PREVIOUS);
}

static int cpcap_charger_battery_temperature(struct cpcap_battery_ddata *ddata,
					     int *value)
{
	struct iio_channel *channel;
	int error;

	channel = ddata->channels[CPCAP_BATTERY_IIO_BATTDET];
	error = iio_read_channel_processed(channel, value);
	if (error < 0) {
		dev_warn(ddata->dev, "%s failed: %i\n", __func__, error);
		*value = CPCAP_NO_BATTERY;

		return error;
	}

	*value /= 100;

	return 0;
}

static int cpcap_battery_get_voltage(struct cpcap_battery_ddata *ddata)
{
	struct iio_channel *channel;
	int error, value = 0;

	channel = ddata->channels[CPCAP_BATTERY_IIO_VOLTAGE];
	error = iio_read_channel_processed(channel, &value);
	if (error < 0) {
		dev_warn(ddata->dev, "%s failed: %i\n", __func__, error);

		return 0;
	}

	return value * 1000;
}

static int cpcap_battery_get_current(struct cpcap_battery_ddata *ddata)
{
	struct iio_channel *channel;
	int error, value = 0;

	channel = ddata->channels[CPCAP_BATTERY_IIO_BATT_CURRENT];
	error = iio_read_channel_processed(channel, &value);
	if (error < 0) {
		dev_warn(ddata->dev, "%s failed: %i\n", __func__, error);

		return 0;
	}

	return value * 1000;
}

/**
 * cpcap_battery_cc_raw_div - calculate and divide coulomb counter μAms values
 * @ddata: device driver data
 * @sample: coulomb counter sample value
 * @accumulator: coulomb counter integrator value
 * @offset: coulomb counter offset value
 * @divider: conversion divider
 *
 * Note that cc_lsb and cc_dur values are from Motorola Linux kernel
 * function data_get_avg_curr_ua() and seem to be based on measured test
 * results. It also has the following comment:
 *
 * Adjustment factors are applied here as a temp solution per the test
 * results. Need to work out a formal solution for this adjustment.
 *
 * A coulomb counter for similar hardware seems to be documented in
 * "TWL6030 Gas Gauging Basics (Rev. A)" swca095a.pdf in chapter
 * "10 Calculating Accumulated Current". We however follow what the
 * Motorola mapphone Linux kernel is doing as there may be either a
 * TI or ST coulomb counter in the PMIC.
 */
static int cpcap_battery_cc_raw_div(struct cpcap_battery_ddata *ddata,
				    s32 sample, s32 accumulator,
				    s16 offset, u32 divider)
{
	s64 acc;

	if (!divider)
		return 0;

	acc = accumulator;
	acc -= (s64)sample * offset;
	acc *= ddata->cc_lsb;
	acc *= -1;
	acc = div_s64(acc, divider);

	return acc;
}

/* 3600000μAms = 1μAh */
static int cpcap_battery_cc_to_uah(struct cpcap_battery_ddata *ddata,
				   s32 sample, s32 accumulator,
				   s16 offset)
{
	return cpcap_battery_cc_raw_div(ddata, sample,
					accumulator, offset,
					3600000);
}

static int cpcap_battery_cc_to_ua(struct cpcap_battery_ddata *ddata,
				  s32 sample, s32 accumulator,
				  s16 offset)
{
	return cpcap_battery_cc_raw_div(ddata, sample,
					accumulator, offset,
					sample *
					CPCAP_BATTERY_CC_SAMPLE_PERIOD_MS);
}

/**
 * cpcap_battery_read_accumulated - reads cpcap coulomb counter
 * @ddata: device driver data
 * @ccd: coulomb counter values
 *
 * Based on Motorola mapphone kernel function data_read_regs().
 * Looking at the registers, the coulomb counter seems similar to
 * the coulomb counter in TWL6030. See "TWL6030 Gas Gauging Basics
 * (Rev. A) swca095a.pdf for "10 Calculating Accumulated Current".
 *
 * Note that swca095a.pdf instructs to stop the coulomb counter
 * before reading to avoid values changing. Motorola mapphone
 * Linux kernel does not do it, so let's assume they've verified
 * the data produced is correct.
 */
static int
cpcap_battery_read_accumulated(struct cpcap_battery_ddata *ddata,
			       struct cpcap_coulomb_counter_data *ccd)
{
	u16 buf[7];	/* CPCAP_REG_CCS1 to CCI */
	int error;

	ccd->sample = 0;
	ccd->accumulator = 0;
	ccd->offset = 0;
	ccd->integrator = 0;

	/* Read coulomb counter register range */
	error = regmap_bulk_read(ddata->reg, CPCAP_REG_CCS1,
				 buf, ARRAY_SIZE(buf));
	if (error)
		return 0;

	/* Sample value CPCAP_REG_CCS1 & 2 */
	ccd->sample = (buf[1] & 0x0fff) << 16;
	ccd->sample |= buf[0];
	if (ddata->vendor == CPCAP_VENDOR_TI)
		ccd->sample = sign_extend32(24, ccd->sample);

	/* Accumulator value CPCAP_REG_CCA1 & 2 */
	ccd->accumulator = ((s16)buf[3]) << 16;
	ccd->accumulator |= buf[2];

	/*
	 * Coulomb counter calibration offset is CPCAP_REG_CCM,
	 * REG_CCO seems unused
	 */
	ccd->offset = buf[4];
	ccd->offset = sign_extend32(ccd->offset, 9);

	/* Integrator register CPCAP_REG_CCI */
	if (ddata->vendor == CPCAP_VENDOR_TI)
		ccd->integrator = sign_extend32(buf[6], 13);
	else
		ccd->integrator = (s16)buf[6];

	return cpcap_battery_cc_to_uah(ddata,
				       ccd->sample,
				       ccd->accumulator,
				       ccd->offset);
}

/**
 * cpcap_battery_cc_get_avg_current - read cpcap coulumb counter
 * @ddata: cpcap battery driver device data
 */
static int cpcap_battery_cc_get_avg_current(struct cpcap_battery_ddata *ddata)
{
	int value, acc, error;
	s32 sample;
	s16 offset;

	/* Coulomb counter integrator */
	error = regmap_read(ddata->reg, CPCAP_REG_CCI, &value);
	if (error)
		return error;

	if (ddata->vendor == CPCAP_VENDOR_TI) {
		acc = sign_extend32(value, 13);
		sample = 1;
	} else {
		acc = (s16)value;
		sample = 4;
	}

	/* Coulomb counter calibration offset  */
	error = regmap_read(ddata->reg, CPCAP_REG_CCM, &value);
	if (error)
		return error;

	offset = sign_extend32(value, 9);

	return cpcap_battery_cc_to_ua(ddata, sample, acc, offset);
}

static bool cpcap_battery_full(struct cpcap_battery_ddata *ddata)
{
	struct cpcap_battery_state_data *state = cpcap_battery_latest(ddata);

	if (state->voltage >=
	    (ddata->config.bat.constant_charge_voltage_max_uv - 18000))
		return true;

	return false;
}

static int cpcap_battery_update_status(struct cpcap_battery_ddata *ddata)
{
	struct cpcap_battery_state_data state, *latest, *previous;
	ktime_t now;
	int error;

	memset(&state, 0, sizeof(state));
	now = ktime_get();

	latest = cpcap_battery_latest(ddata);
	if (latest) {
		s64 delta_ms = ktime_to_ms(ktime_sub(now, latest->time));

		if (delta_ms < CPCAP_BATTERY_CC_SAMPLE_PERIOD_MS)
			return delta_ms;
	}

	state.time = now;
	state.voltage = cpcap_battery_get_voltage(ddata);
	state.current_ua = cpcap_battery_get_current(ddata);
	state.counter_uah = cpcap_battery_read_accumulated(ddata, &state.cc);

	error = cpcap_charger_battery_temperature(ddata,
						  &state.temperature);
	if (error)
		return error;

	previous = cpcap_battery_previous(ddata);
	memcpy(previous, latest, sizeof(*previous));
	memcpy(latest, &state, sizeof(*latest));

	return 0;
}

static enum power_supply_property cpcap_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_TEMP,
};

static int cpcap_battery_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct cpcap_battery_ddata *ddata = power_supply_get_drvdata(psy);
	struct cpcap_battery_state_data *latest, *previous;
	u32 sample;
	s32 accumulator;
	int cached;
	s64 tmp;

	cached = cpcap_battery_update_status(ddata);
	if (cached < 0)
		return cached;

	latest = cpcap_battery_latest(ddata);
	previous = cpcap_battery_previous(ddata);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		if (latest->temperature > CPCAP_NO_BATTERY)
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (cpcap_battery_full(ddata)) {
			val->intval = POWER_SUPPLY_STATUS_FULL;
			break;
		}
		if (cpcap_battery_cc_get_avg_current(ddata) < 0)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = ddata->config.info.technology;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = cpcap_battery_get_voltage(ddata);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = ddata->config.info.voltage_max_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = ddata->config.info.voltage_min_design;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = ddata->config.bat.constant_charge_voltage_max_uv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		sample = latest->cc.sample - previous->cc.sample;
		if (!sample) {
			val->intval = cpcap_battery_cc_get_avg_current(ddata);
			break;
		}
		accumulator = latest->cc.accumulator - previous->cc.accumulator;
		val->intval = cpcap_battery_cc_to_ua(ddata, sample,
						     accumulator,
						     latest->cc.offset);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = latest->current_ua;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = latest->counter_uah;
		break;
	case POWER_SUPPLY_PROP_POWER_NOW:
		tmp = (latest->voltage / 10000) * latest->current_ua;
		val->intval = div64_s64(tmp, 100);
		break;
	case POWER_SUPPLY_PROP_POWER_AVG:
		sample = latest->cc.sample - previous->cc.sample;
		if (!sample) {
			tmp = cpcap_battery_cc_get_avg_current(ddata);
			tmp *= (latest->voltage / 10000);
			val->intval = div64_s64(tmp, 100);
			break;
		}
		accumulator = latest->cc.accumulator - previous->cc.accumulator;
		tmp = cpcap_battery_cc_to_ua(ddata, sample, accumulator,
					     latest->cc.offset);
		tmp *= ((latest->voltage + previous->voltage) / 20000);
		val->intval = div64_s64(tmp, 100);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		if (cpcap_battery_full(ddata))
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (latest->voltage >= 3750000)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
		else if (latest->voltage >= 3300000)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		else if (latest->voltage > 3100000)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else if (latest->voltage <= 3100000)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = ddata->config.info.charge_full_design;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = latest->temperature;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cpcap_battery_update_charger(struct cpcap_battery_ddata *ddata,
					int const_charge_voltage)
{
	union power_supply_propval prop;
	union power_supply_propval val;
	struct power_supply *charger;
	int error;

	charger = power_supply_get_by_name("usb");
	if (!charger)
		return -ENODEV;

	error = power_supply_get_property(charger,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
				&prop);
	if (error)
		goto out_put;

	/* Allow charger const voltage lower than battery const voltage */
	if (const_charge_voltage > prop.intval)
		goto out_put;

	val.intval = const_charge_voltage;

	error = power_supply_set_property(charger,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
			&val);
out_put:
	power_supply_put(charger);

	return error;
}

static int cpcap_battery_set_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      const union power_supply_propval *val)
{
	struct cpcap_battery_ddata *ddata = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		if (val->intval < ddata->config.info.voltage_min_design)
			return -EINVAL;
		if (val->intval > ddata->config.info.voltage_max_design)
			return -EINVAL;

		ddata->config.bat.constant_charge_voltage_max_uv = val->intval;

		return cpcap_battery_update_charger(ddata, val->intval);
	default:
		return -EINVAL;
	}

	return 0;
}

static int cpcap_battery_property_is_writeable(struct power_supply *psy,
					       enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return 1;
	default:
		return 0;
	}
}

static irqreturn_t cpcap_battery_irq_thread(int irq, void *data)
{
	struct cpcap_battery_ddata *ddata = data;
	struct cpcap_battery_state_data *latest;
	struct cpcap_interrupt_desc *d;

	if (!atomic_read(&ddata->active))
		return IRQ_NONE;

	list_for_each_entry(d, &ddata->irq_list, node) {
		if (irq == d->irq)
			break;
	}

	if (!d)
		return IRQ_NONE;

	latest = cpcap_battery_latest(ddata);

	switch (d->action) {
	case CPCAP_BATTERY_IRQ_ACTION_CC_CAL_DONE:
		dev_info(ddata->dev, "Coulomb counter calibration done\n");
		break;
	case CPCAP_BATTERY_IRQ_ACTION_BATTERY_LOW:
		if (latest->current_ua >= 0)
			dev_warn(ddata->dev, "Battery low at %imV!\n",
				latest->voltage / 1000);
		break;
	case CPCAP_BATTERY_IRQ_ACTION_POWEROFF:
		if (latest->current_ua >= 0 && latest->voltage <= 3200000) {
			dev_emerg(ddata->dev,
				  "Battery empty at %imV, powering off\n",
				  latest->voltage / 1000);
			orderly_poweroff(true);
		}
		break;
	default:
		break;
	}

	power_supply_changed(ddata->psy);

	return IRQ_HANDLED;
}

static int cpcap_battery_init_irq(struct platform_device *pdev,
				  struct cpcap_battery_ddata *ddata,
				  const char *name)
{
	struct cpcap_interrupt_desc *d;
	int irq, error;

	irq = platform_get_irq_byname(pdev, name);
	if (irq < 0)
		return irq;

	error = devm_request_threaded_irq(ddata->dev, irq, NULL,
					  cpcap_battery_irq_thread,
					  IRQF_SHARED | IRQF_ONESHOT,
					  name, ddata);
	if (error) {
		dev_err(ddata->dev, "could not get irq %s: %i\n",
			name, error);

		return error;
	}

	d = devm_kzalloc(ddata->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->name = name;
	d->irq = irq;

	if (!strncmp(name, "cccal", 5))
		d->action = CPCAP_BATTERY_IRQ_ACTION_CC_CAL_DONE;
	else if (!strncmp(name, "lowbph", 6))
		d->action = CPCAP_BATTERY_IRQ_ACTION_BATTERY_LOW;
	else if (!strncmp(name, "lowbpl", 6))
		d->action = CPCAP_BATTERY_IRQ_ACTION_POWEROFF;

	list_add(&d->node, &ddata->irq_list);

	return 0;
}

static int cpcap_battery_init_interrupts(struct platform_device *pdev,
					 struct cpcap_battery_ddata *ddata)
{
	static const char * const cpcap_battery_irqs[] = {
		"eol", "lowbph", "lowbpl",
		"chrgcurr1", "battdetb"
	};
	int i, error;

	for (i = 0; i < ARRAY_SIZE(cpcap_battery_irqs); i++) {
		error = cpcap_battery_init_irq(pdev, ddata,
					       cpcap_battery_irqs[i]);
		if (error)
			return error;
	}

	/* Enable calibration interrupt if already available in dts */
	cpcap_battery_init_irq(pdev, ddata, "cccal");

	/* Enable low battery interrupts for 3.3V high and 3.1V low */
	error = regmap_update_bits(ddata->reg, CPCAP_REG_BPEOL,
				   0xffff,
				   CPCAP_REG_BPEOL_BIT_BATTDETEN);
	if (error)
		return error;

	return 0;
}

static int cpcap_battery_init_iio(struct cpcap_battery_ddata *ddata)
{
	const char * const names[CPCAP_BATTERY_IIO_NR] = {
		"battdetb", "battp", "chg_isense", "batti",
	};
	int error, i;

	for (i = 0; i < CPCAP_BATTERY_IIO_NR; i++) {
		ddata->channels[i] = devm_iio_channel_get(ddata->dev,
							  names[i]);
		if (IS_ERR(ddata->channels[i])) {
			error = PTR_ERR(ddata->channels[i]);
			goto out_err;
		}

		if (!ddata->channels[i]->indio_dev) {
			error = -ENXIO;
			goto out_err;
		}
	}

	return 0;

out_err:
	return dev_err_probe(ddata->dev, error,
			     "could not initialize VBUS or ID IIO\n");
}

/* Calibrate coulomb counter */
static int cpcap_battery_calibrate(struct cpcap_battery_ddata *ddata)
{
	int error, ccc1, value;
	unsigned long timeout;

	error = regmap_read(ddata->reg, CPCAP_REG_CCC1, &ccc1);
	if (error)
		return error;

	timeout = jiffies + msecs_to_jiffies(6000);

	/* Start calibration */
	error = regmap_update_bits(ddata->reg, CPCAP_REG_CCC1,
				   0xffff,
				   CPCAP_REG_CCC1_CAL_EN);
	if (error)
		goto restore;

	while (time_before(jiffies, timeout)) {
		error = regmap_read(ddata->reg, CPCAP_REG_CCC1, &value);
		if (error)
			goto restore;

		if (!(value & CPCAP_REG_CCC1_CAL_EN))
			break;

		error = regmap_read(ddata->reg, CPCAP_REG_CCM, &value);
		if (error)
			goto restore;

		msleep(300);
	}

	/* Read calibration offset from CCM */
	error = regmap_read(ddata->reg, CPCAP_REG_CCM, &value);
	if (error)
		goto restore;

	dev_info(ddata->dev, "calibration done: 0x%04x\n", value);

restore:
	if (error)
		dev_err(ddata->dev, "%s: error %i\n", __func__, error);

	error = regmap_update_bits(ddata->reg, CPCAP_REG_CCC1,
				   0xffff, ccc1);
	if (error)
		dev_err(ddata->dev, "%s: restore error %i\n",
			__func__, error);

	return error;
}

/*
 * Based on the values from Motorola mapphone Linux kernel. In the
 * the Motorola mapphone Linux kernel tree the value for pm_cd_factor
 * is passed to the kernel via device tree. If it turns out to be
 * something device specific we can consider that too later.
 *
 * And looking at the battery full and shutdown values for the stock
 * kernel on droid 4, full is 4351000 and software initiates shutdown
 * at 3078000. The device will die around 2743000.
 */
static const struct cpcap_battery_config cpcap_battery_default_data = {
	.cd_factor = 0x3cc,
	.info.technology = POWER_SUPPLY_TECHNOLOGY_LION,
	.info.voltage_max_design = 4351000,
	.info.voltage_min_design = 3100000,
	.info.charge_full_design = 1740000,
	.bat.constant_charge_voltage_max_uv = 4200000,
};

#ifdef CONFIG_OF
static const struct of_device_id cpcap_battery_id_table[] = {
	{
		.compatible = "motorola,cpcap-battery",
		.data = &cpcap_battery_default_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, cpcap_battery_id_table);
#endif

static int cpcap_battery_probe(struct platform_device *pdev)
{
	struct power_supply_desc *psy_desc;
	struct cpcap_battery_ddata *ddata;
	const struct of_device_id *match;
	struct power_supply_config psy_cfg = {};
	int error;

	match = of_match_device(of_match_ptr(cpcap_battery_id_table),
				&pdev->dev);
	if (!match)
		return -EINVAL;

	if (!match->data) {
		dev_err(&pdev->dev, "no configuration data found\n");

		return -ENODEV;
	}

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	INIT_LIST_HEAD(&ddata->irq_list);
	ddata->dev = &pdev->dev;
	memcpy(&ddata->config, match->data, sizeof(ddata->config));

	ddata->reg = dev_get_regmap(ddata->dev->parent, NULL);
	if (!ddata->reg)
		return -ENODEV;

	error = cpcap_get_vendor(ddata->dev, ddata->reg, &ddata->vendor);
	if (error)
		return error;

	switch (ddata->vendor) {
	case CPCAP_VENDOR_ST:
		ddata->cc_lsb = 95374;	/* μAms per LSB */
		break;
	case CPCAP_VENDOR_TI:
		ddata->cc_lsb = 91501;	/* μAms per LSB */
		break;
	default:
		return -EINVAL;
	}
	ddata->cc_lsb = (ddata->cc_lsb * ddata->config.cd_factor) / 1000;

	platform_set_drvdata(pdev, ddata);

	error = cpcap_battery_init_interrupts(pdev, ddata);
	if (error)
		return error;

	error = cpcap_battery_init_iio(ddata);
	if (error)
		return error;

	psy_desc = devm_kzalloc(ddata->dev, sizeof(*psy_desc), GFP_KERNEL);
	if (!psy_desc)
		return -ENOMEM;

	psy_desc->name = "battery";
	psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	psy_desc->properties = cpcap_battery_props;
	psy_desc->num_properties = ARRAY_SIZE(cpcap_battery_props);
	psy_desc->get_property = cpcap_battery_get_property;
	psy_desc->set_property = cpcap_battery_set_property;
	psy_desc->property_is_writeable = cpcap_battery_property_is_writeable;

	psy_cfg.of_node = pdev->dev.of_node;
	psy_cfg.drv_data = ddata;

	ddata->psy = devm_power_supply_register(ddata->dev, psy_desc,
						&psy_cfg);
	error = PTR_ERR_OR_ZERO(ddata->psy);
	if (error) {
		dev_err(ddata->dev, "failed to register power supply\n");
		return error;
	}

	atomic_set(&ddata->active, 1);

	error = cpcap_battery_calibrate(ddata);
	if (error)
		return error;

	return 0;
}

static int cpcap_battery_remove(struct platform_device *pdev)
{
	struct cpcap_battery_ddata *ddata = platform_get_drvdata(pdev);
	int error;

	atomic_set(&ddata->active, 0);
	error = regmap_update_bits(ddata->reg, CPCAP_REG_BPEOL,
				   0xffff, 0);
	if (error)
		dev_err(&pdev->dev, "could not disable: %i\n", error);

	return 0;
}

static struct platform_driver cpcap_battery_driver = {
	.driver	= {
		.name		= "cpcap_battery",
		.of_match_table = of_match_ptr(cpcap_battery_id_table),
	},
	.probe	= cpcap_battery_probe,
	.remove = cpcap_battery_remove,
};
module_platform_driver(cpcap_battery_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Tony Lindgren <tony@atomide.com>");
MODULE_DESCRIPTION("CPCAP PMIC Battery Driver");
