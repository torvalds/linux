/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2017, 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __STORM_WATCH_H
#define __STORM_WATCH_H
#include <linux/ktime.h>
#include <linux/mutex.h>

/**
 * Data used to track an event storm.
 *
 * @storm_period_ms: The maximum time interval between two events. If this limit
 *                   is exceeded then the event chain will be broken and removed
 *                   from consideration for a storm.
 * @max_storm_count: The number of chained events required to trigger a storm.
 * @storm_count:     The current number of chained events.
 * @last_kt:         Kernel time of the last event seen.
 * @storm_lock:      Mutex lock to protect storm_watch data.
 */
struct storm_watch {
	bool		enabled;
	int		storm_period_ms;
	int		max_storm_count;
	int		storm_count;
	ktime_t		last_kt;
	struct mutex	storm_lock;
};

bool is_storming(struct storm_watch *data);
void reset_storm_count(struct storm_watch *data);
void update_storm_count(struct storm_watch *data, int max_count);
#endif
