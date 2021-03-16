/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2020 Martin Blumenstingl <martin.blumenstingl@googlemail.com> */

#ifndef __LIMA_DEVFREQ_H__
#define __LIMA_DEVFREQ_H__

#include <linux/devfreq.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>

struct devfreq;
struct opp_table;
struct thermal_cooling_device;

struct lima_device;

struct lima_devfreq {
	struct devfreq *devfreq;
	struct opp_table *clkname_opp_table;
	struct opp_table *regulators_opp_table;
	struct thermal_cooling_device *cooling;
	struct devfreq_simple_ondemand_data gov_data;

	ktime_t busy_time;
	ktime_t idle_time;
	ktime_t time_last_update;
	int busy_count;
	/*
	 * Protect busy_time, idle_time, time_last_update and busy_count
	 * because these can be updated concurrently, for example by the GP
	 * and PP interrupts.
	 */
	spinlock_t lock;
};

int lima_devfreq_init(struct lima_device *ldev);
void lima_devfreq_fini(struct lima_device *ldev);

void lima_devfreq_record_busy(struct lima_devfreq *devfreq);
void lima_devfreq_record_idle(struct lima_devfreq *devfreq);

int lima_devfreq_resume(struct lima_devfreq *devfreq);
int lima_devfreq_suspend(struct lima_devfreq *devfreq);

#endif
