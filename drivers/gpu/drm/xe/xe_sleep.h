/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_SLEEP_H_
#define _XE_SLEEP_H_

#include <linux/delay.h>
#include <linux/math64.h>

/**
 * xe_sleep_relaxed_ms() - Sleep for an approximate time.
 * @delay_ms: time in msec to sleep
 *
 * For smaller timeouts, sleep with 0.5ms accuracy.
 */
static inline void xe_sleep_relaxed_ms(unsigned int delay_ms)
{
	unsigned long min_us, max_us;

	if (!delay_ms)
		return;

	if (delay_ms > 20) {
		msleep(delay_ms);
		return;
	}

	min_us = mul_u32_u32(delay_ms, 1000);
	max_us = min_us + 500;

	usleep_range(min_us, max_us);
}

/**
 * xe_sleep_exponential_ms() - Sleep for a exponentially increased time.
 * @sleep_period_ms: current time in msec to sleep
 * @max_sleep_ms: maximum time in msec to sleep
 *
 * Sleep for the @sleep_period_ms and exponentially increase this time for the
 * next loop, unless reaching the @max_sleep_ms limit.
 *
 * Return: approximate time in msec the task was delayed.
 */
static inline unsigned int xe_sleep_exponential_ms(unsigned int *sleep_period_ms,
						   unsigned int max_sleep_ms)
{
	unsigned int delay_ms = *sleep_period_ms;
	unsigned int next_delay_ms = 2 * delay_ms;

	xe_sleep_relaxed_ms(delay_ms);
	*sleep_period_ms = min(next_delay_ms, max_sleep_ms);
	return delay_ms;
}

#endif
