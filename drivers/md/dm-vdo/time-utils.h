/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_TIME_UTILS_H
#define UDS_TIME_UTILS_H

#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/types.h>

static inline s64 ktime_to_seconds(ktime_t reltime)
{
	return reltime / NSEC_PER_SEC;
}

static inline ktime_t current_time_ns(clockid_t clock)
{
	return clock == CLOCK_MONOTONIC ? ktime_get_ns() : ktime_get_real_ns();
}

static inline ktime_t current_time_us(void)
{
	return current_time_ns(CLOCK_REALTIME) / NSEC_PER_USEC;
}

#endif /* UDS_TIME_UTILS_H */
