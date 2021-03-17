/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 Collabora ltd. */

#ifndef __PANFROST_DEVFREQ_H__
#define __PANFROST_DEVFREQ_H__

#include <linux/spinlock.h>
#include <linux/ktime.h>

struct devfreq;
struct opp_table;
struct thermal_cooling_device;

struct panfrost_device;

struct panfrost_devfreq {
	struct devfreq *devfreq;
	struct opp_table *regulators_opp_table;
	struct thermal_cooling_device *cooling;
	bool opp_of_table_added;

	ktime_t busy_time;
	ktime_t idle_time;
	ktime_t time_last_update;
	int busy_count;
	/*
	 * Protect busy_time, idle_time, time_last_update and busy_count
	 * because these can be updated concurrently between multiple jobs.
	 */
	spinlock_t lock;
};

int panfrost_devfreq_init(struct panfrost_device *pfdev);
void panfrost_devfreq_fini(struct panfrost_device *pfdev);

void panfrost_devfreq_resume(struct panfrost_device *pfdev);
void panfrost_devfreq_suspend(struct panfrost_device *pfdev);

void panfrost_devfreq_record_busy(struct panfrost_devfreq *devfreq);
void panfrost_devfreq_record_idle(struct panfrost_devfreq *devfreq);

#endif /* __PANFROST_DEVFREQ_H__ */
