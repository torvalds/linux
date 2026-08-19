// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Invensense, Inc.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>

#include <linux/iio/common/inv_sensors_timestamp.h>

/* compute min and max following jitter in per mille */
#define INV_SENSORS_TIMESTAMP_MIN(_val, _jitter)		\
	(((_val) * (1000 - (_jitter))) / 1000)
#define INV_SENSORS_TIMESTAMP_MAX(_val, _jitter)		\
	(((_val) * (1000 + (_jitter))) / 1000)

/* minimum timestamp delta between 2 interrupts for measuring period (20ms) */
#define INV_SENSORS_MIN_IT_DELTA	(20 * NSEC_PER_MSEC)

/* Add a new value inside an accumulator and update the estimate value */
static void inv_update_acc(struct inv_sensors_timestamp_acc *acc, u32 val)
{
	u64 sum = 0;
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

void inv_sensors_timestamp_init(struct inv_sensors_timestamp *ts,
				const struct inv_sensors_timestamp_chip *chip)
{
	memset(ts, 0, sizeof(*ts));

	/* save chip parameters and compute min and max clock period */
	ts->chip = *chip;
	ts->min_period = INV_SENSORS_TIMESTAMP_MIN(chip->clock_period, chip->jitter);
	ts->max_period = INV_SENSORS_TIMESTAMP_MAX(chip->clock_period, chip->jitter);

	/* current multiplier and period values after reset */
	ts->mult = chip->init_period / chip->clock_period;
	ts->period = chip->init_period;

	/* use theoretical value for chip period */
	inv_update_acc(&ts->chip_period, chip->clock_period);
}
EXPORT_SYMBOL_NS_GPL(inv_sensors_timestamp_init, "IIO_INV_SENSORS_TIMESTAMP");

int inv_sensors_timestamp_update_odr(struct inv_sensors_timestamp *ts,
				     u32 period, bool fifo)
{
	u32 mult;

	/* when FIFO is on, prevent odr change if one is already pending */
	if (fifo && ts->new_mult != 0)
		return -EAGAIN;

	mult = period / ts->chip.clock_period;
	if (mult != ts->mult)
		ts->new_mult = mult;

	/* When FIFO is off, directly apply the new ODR */
	if (!fifo)
		inv_sensors_timestamp_apply_odr(ts, 0, 0, 0);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(inv_sensors_timestamp_update_odr, "IIO_INV_SENSORS_TIMESTAMP");

static bool inv_validate_period(struct inv_sensors_timestamp *ts, u32 period)
{
	u32 period_min, period_max;

	/* check that period is acceptable */
	period_min = ts->min_period * ts->mult;
	period_max = ts->max_period * ts->mult;
	if (period > period_min && period < period_max)
		return true;
	else
		return false;
}

static bool inv_update_chip_period(struct inv_sensors_timestamp *ts,
				   u32 period)
{
	u32 new_chip_period;

	if (!inv_validate_period(ts, period))
		return false;

	/* update chip internal period estimation */
	new_chip_period = period / ts->mult;
	inv_update_acc(&ts->chip_period, new_chip_period);

	return true;
}

static u32 inv_align_timestamp_it(struct inv_sensors_timestamp *ts,
				  unsigned int sample_nb)
{
	const s64 period_min = (s64)ts->min_period * ts->mult;
	const s64 period_max = (s64)ts->max_period * ts->mult;
	s64 new_period;

	/* compute new period aligning last timestamp with interrupt timestamp */
	new_period = div_s64(ts->it.up - ts->timestamp, sample_nb);

	/* ensure that period never overflows the jitter */
	return clamp(new_period, period_min, period_max);
}

void inv_sensors_timestamp_interrupt(struct inv_sensors_timestamp *ts,
				     size_t sample_nb, s64 timestamp)
{
	struct inv_sensors_timestamp_interval *it;
	s64 delta, delta_threshold, interval;
	u32 period;
	bool valid = false;

	if (sample_nb == 0)
		return;

	/* no previous data, compute theoretical value from interrupt */
	if (ts->timestamp == 0) {
		/* elapsed time: sensor period * sensor samples number */
		interval = (s64)ts->period * (s64)sample_nb;
		ts->timestamp = timestamp - interval;
	}

	/* update delta timestamps and estimated period */
	it = &ts->delta;
	ts->delta_counter += sample_nb;
	delta = timestamp - it->up;
	delta_threshold = INV_SENSORS_TIMESTAMP_MIN(INV_SENSORS_MIN_IT_DELTA, ts->chip.jitter);
	if (delta >= delta_threshold) {
		it->lo = it->up;
		it->up = timestamp;
		if (it->lo != 0) {
			/* compute period: delta time divided by number of samples */
			delta = it->up - it->lo;
			period = div_s64(delta, ts->delta_counter);
			inv_update_chip_period(ts, period);
		}
		ts->delta_counter = 0;
	}

	/* update interrupt timestamp and compute chip and sensor periods */
	it = &ts->it;
	it->lo = it->up;
	it->up = timestamp;
	delta = it->up - it->lo;
	if (it->lo != 0) {
		/* compute period and check validity */
		period = div_s64(delta, sample_nb);
		valid = inv_validate_period(ts, period);
	}

	/* if interrupt interval is valid, sync with interrupt timestamp */
	if (valid)
		ts->period = inv_align_timestamp_it(ts, sample_nb);
	else
		ts->period = ts->mult * ts->chip_period.val;
}
EXPORT_SYMBOL_NS_GPL(inv_sensors_timestamp_interrupt, "IIO_INV_SENSORS_TIMESTAMP");

void inv_sensors_timestamp_apply_odr(struct inv_sensors_timestamp *ts,
				     u32 fifo_period, size_t fifo_nb,
				     unsigned int fifo_no)
{
	s64 interval;
	u32 fifo_mult;

	if (ts->new_mult == 0)
		return;

	/* update to new multiplier and update period */
	ts->mult = ts->new_mult;
	ts->new_mult = 0;
	ts->period = ts->mult * ts->chip_period.val;

	/*
	 * After ODR change the time interval with the previous sample is
	 * undertermined (depends when the change occurs). So we compute the
	 * timestamp from the current interrupt using the new FIFO period, the
	 * total number of samples and the current sample numero.
	 */
	if (ts->timestamp != 0) {
		/* compute measured fifo period */
		fifo_mult = fifo_period / ts->chip.clock_period;
		fifo_period = fifo_mult * ts->chip_period.val;
		/* computes time interval between interrupt and this sample */
		interval = (s64)(fifo_nb - fifo_no) * (s64)fifo_period;
		ts->timestamp = ts->it.up - interval;
	}
}
EXPORT_SYMBOL_NS_GPL(inv_sensors_timestamp_apply_odr, "IIO_INV_SENSORS_TIMESTAMP");

MODULE_AUTHOR("InvenSense, Inc.");
MODULE_DESCRIPTION("InvenSense sensors timestamp module");
MODULE_LICENSE("GPL");
