// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Invensense, Inc.
 */

#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/math64.h>

#include "inv_icm42600.h"
#include "inv_icm42600_timestamp.h"

/* internal chip period is 32kHz, 31250ns */
#define INV_ICM42600_TIMESTAMP_PERIOD		31250
/* allow a jitter of +/- 2% */
#define INV_ICM42600_TIMESTAMP_JITTER		2
/* compute min and max periods accepted */
#define INV_ICM42600_TIMESTAMP_MIN_PERIOD(_p)		\
	(((_p) * (100 - INV_ICM42600_TIMESTAMP_JITTER)) / 100)
#define INV_ICM42600_TIMESTAMP_MAX_PERIOD(_p)		\
	(((_p) * (100 + INV_ICM42600_TIMESTAMP_JITTER)) / 100)

/* Add a new value inside an accumulator and update the estimate value */
static void inv_update_acc(struct inv_icm42600_timestamp_acc *acc, uint32_t val)
{
	uint64_t sum = 0;
	size_t i;

	acc->values[acc->idx++] = val;
	if (acc->idx >= ARRAY_SIZE(acc->values))
		acc->idx = 0;

	/* compute the mean of all stored values, use 0 as empty slot */
	for (i = 0; i < ARRAY_SIZE(acc->values); ++i) {
		if (acc->values[i] == 0)
			break;
		sum += acc->values[i];
	}

	acc->val = div_u64(sum, i);
}

void inv_icm42600_timestamp_init(struct inv_icm42600_timestamp *ts,
				 uint32_t period)
{
	/* initial odr for sensor after reset is 1kHz */
	const uint32_t default_period = 1000000;

	/* current multiplier and period values after reset */
	ts->mult = default_period / INV_ICM42600_TIMESTAMP_PERIOD;
	ts->period = default_period;
	/* new set multiplier is the one from chip initialization */
	ts->new_mult = period / INV_ICM42600_TIMESTAMP_PERIOD;

	/* use theoretical value for chip period */
	inv_update_acc(&ts->chip_period, INV_ICM42600_TIMESTAMP_PERIOD);
}

int inv_icm42600_timestamp_setup(struct inv_icm42600_state *st)
{
	unsigned int val;

	/* enable timestamp register */
	val = INV_ICM42600_TMST_CONFIG_TMST_TO_REGS_EN |
	      INV_ICM42600_TMST_CONFIG_TMST_EN;
	return regmap_update_bits(st->map, INV_ICM42600_REG_TMST_CONFIG,
				  INV_ICM42600_TMST_CONFIG_MASK, val);
}

int inv_icm42600_timestamp_update_odr(struct inv_icm42600_timestamp *ts,
				      uint32_t period, bool fifo)
{
	/* when FIFO is on, prevent odr change if one is already pending */
	if (fifo && ts->new_mult != 0)
		return -EAGAIN;

	ts->new_mult = period / INV_ICM42600_TIMESTAMP_PERIOD;

	return 0;
}

static bool inv_validate_period(uint32_t period, uint32_t mult)
{
	const uint32_t chip_period = INV_ICM42600_TIMESTAMP_PERIOD;
	uint32_t period_min, period_max;

	/* check that period is acceptable */
	period_min = INV_ICM42600_TIMESTAMP_MIN_PERIOD(chip_period) * mult;
	period_max = INV_ICM42600_TIMESTAMP_MAX_PERIOD(chip_period) * mult;
	if (period > period_min && period < period_max)
		return true;
	else
		return false;
}

static bool inv_update_chip_period(struct inv_icm42600_timestamp *ts,
				   uint32_t mult, uint32_t period)
{
	uint32_t new_chip_period;

	if (!inv_validate_period(period, mult))
		return false;

	/* update chip internal period estimation */
	new_chip_period = period / mult;
	inv_update_acc(&ts->chip_period, new_chip_period);
	ts->period = ts->mult * ts->chip_period.val;

	return true;
}

static void inv_align_timestamp_it(struct inv_icm42600_timestamp *ts)
{
	int64_t delta, jitter;
	int64_t adjust;

	/* delta time between last sample and last interrupt */
	delta = ts->it.lo - ts->timestamp;

	/* adjust timestamp while respecting jitter */
	jitter = div_s64((int64_t)ts->period * INV_ICM42600_TIMESTAMP_JITTER, 100);
	if (delta > jitter)
		adjust = jitter;
	else if (delta < -jitter)
		adjust = -jitter;
	else
		adjust = 0;

	ts->timestamp += adjust;
}

void inv_icm42600_timestamp_interrupt(struct inv_icm42600_timestamp *ts,
				      uint32_t fifo_period, size_t fifo_nb,
				      size_t sensor_nb, int64_t timestamp)
{
	struct inv_icm42600_timestamp_interval *it;
	int64_t delta, interval;
	const uint32_t fifo_mult = fifo_period / INV_ICM42600_TIMESTAMP_PERIOD;
	uint32_t period = ts->period;
	bool valid = false;

	if (fifo_nb == 0)
		return;

	/* update interrupt timestamp and compute chip and sensor periods */
	it = &ts->it;
	it->lo = it->up;
	it->up = timestamp;
	delta = it->up - it->lo;
	if (it->lo != 0) {
		/* compute period: delta time divided by number of samples */
		period = div_s64(delta, fifo_nb);
		valid = inv_update_chip_period(ts, fifo_mult, period);
	}

	/* no previous data, compute theoritical value from interrupt */
	if (ts->timestamp == 0) {
		/* elapsed time: sensor period * sensor samples number */
		interval = (int64_t)ts->period * (int64_t)sensor_nb;
		ts->timestamp = it->up - interval;
		return;
	}

	/* if interrupt interval is valid, sync with interrupt timestamp */
	if (valid)
		inv_align_timestamp_it(ts);
}

void inv_icm42600_timestamp_apply_odr(struct inv_icm42600_timestamp *ts,
				      uint32_t fifo_period, size_t fifo_nb,
				      unsigned int fifo_no)
{
	int64_t interval;
	uint32_t fifo_mult;

	if (ts->new_mult == 0)
		return;

	/* update to new multiplier and update period */
	ts->mult = ts->new_mult;
	ts->new_mult = 0;
	ts->period = ts->mult * ts->chip_period.val;

	/*
	 * After ODR change the time interval with the previous sample is
	 * undertermined (depends when the change occures). So we compute the
	 * timestamp from the current interrupt using the new FIFO period, the
	 * total number of samples and the current sample numero.
	 */
	if (ts->timestamp != 0) {
		/* compute measured fifo period */
		fifo_mult = fifo_period / INV_ICM42600_TIMESTAMP_PERIOD;
		fifo_period = fifo_mult * ts->chip_period.val;
		/* computes time interval between interrupt and this sample */
		interval = (int64_t)(fifo_nb - fifo_no) * (int64_t)fifo_period;
		ts->timestamp = ts->it.up - interval;
	}
}
