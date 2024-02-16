// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/export.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/units.h>
#include <asm/errno.h>
#include "adf_admin.h"
#include "adf_accel_devices.h"
#include "adf_clock.h"
#include "adf_common_drv.h"

#define MEASURE_CLOCK_RETRIES 10
#define MEASURE_CLOCK_DELAY_US 10000
#define ME_CLK_DIVIDER 16
#define MEASURE_CLOCK_DELTA_THRESHOLD_US 100

static inline u64 timespec_to_us(const struct timespec64 *ts)
{
	return (u64)DIV_ROUND_CLOSEST_ULL(timespec64_to_ns(ts), NSEC_PER_USEC);
}

static inline u64 timespec_to_ms(const struct timespec64 *ts)
{
	return (u64)DIV_ROUND_CLOSEST_ULL(timespec64_to_ns(ts), NSEC_PER_MSEC);
}

u64 adf_clock_get_current_time(void)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	return timespec_to_ms(&ts);
}

static int measure_clock(struct adf_accel_dev *accel_dev, u32 *frequency)
{
	struct timespec64 ts1, ts2, ts3, ts4;
	u64 timestamp1, timestamp2, temp;
	u32 delta_us, tries;
	int ret;

	tries = MEASURE_CLOCK_RETRIES;
	do {
		ktime_get_real_ts64(&ts1);
		ret = adf_get_fw_timestamp(accel_dev, &timestamp1);
		if (ret) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to get fw timestamp\n");
			return ret;
		}
		ktime_get_real_ts64(&ts2);
		delta_us = timespec_to_us(&ts2) - timespec_to_us(&ts1);
	} while (delta_us > MEASURE_CLOCK_DELTA_THRESHOLD_US && --tries);

	if (!tries) {
		dev_err(&GET_DEV(accel_dev), "Excessive clock measure delay\n");
		return -ETIMEDOUT;
	}

	fsleep(MEASURE_CLOCK_DELAY_US);

	tries = MEASURE_CLOCK_RETRIES;
	do {
		ktime_get_real_ts64(&ts3);
		if (adf_get_fw_timestamp(accel_dev, &timestamp2)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to get fw timestamp\n");
			return -EIO;
		}
		ktime_get_real_ts64(&ts4);
		delta_us = timespec_to_us(&ts4) - timespec_to_us(&ts3);
	} while (delta_us > MEASURE_CLOCK_DELTA_THRESHOLD_US && --tries);

	if (!tries) {
		dev_err(&GET_DEV(accel_dev), "Excessive clock measure delay\n");
		return -ETIMEDOUT;
	}

	delta_us = timespec_to_us(&ts3) - timespec_to_us(&ts1);
	if (!delta_us)
		return -EINVAL;

	temp = (timestamp2 - timestamp1) * ME_CLK_DIVIDER * 10;
	temp = DIV_ROUND_CLOSEST_ULL(temp, delta_us);
	/*
	 * Enclose the division to allow the preprocessor to precalculate it,
	 * and avoid promoting r-value to 64-bit before division.
	 */
	*frequency = temp * (HZ_PER_MHZ / 10);

	return 0;
}

/**
 * adf_dev_measure_clock() - measures device clock frequency
 * @accel_dev: Pointer to acceleration device.
 * @frequency: Pointer to variable where result will be stored
 * @min: Minimal allowed frequency value
 * @max: Maximal allowed frequency value
 *
 * If the measurement result will go beyond the min/max thresholds the value
 * will take the value of the crossed threshold.
 *
 * This algorithm compares the device firmware timestamp with the kernel
 * timestamp. So we can't expect too high accuracy from this measurement.
 *
 * Return:
 * * 0 - measurement succeed
 * * -ETIMEDOUT - measurement failed
 */
int adf_dev_measure_clock(struct adf_accel_dev *accel_dev,
			  u32 *frequency, u32 min, u32 max)
{
	int ret;
	u32 freq;

	ret = measure_clock(accel_dev, &freq);
	if (ret)
		return ret;

	*frequency = clamp(freq, min, max);

	if (*frequency != freq)
		dev_warn(&GET_DEV(accel_dev),
			 "Measured clock %d Hz is out of range, assuming %d\n",
			 freq, *frequency);
	return 0;
}
EXPORT_SYMBOL_GPL(adf_dev_measure_clock);
