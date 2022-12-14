/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * governor.h - internal header for devfreq governors.
 *
 * Copyright (C) 2011 Samsung Electronics
 *	MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This header is for devfreq governors in drivers/devfreq/
 */

#ifndef _GOVERNOR_H
#define _GOVERNOR_H

#include <linux/devfreq.h>

#define DEVFREQ_NAME_LEN			16

#define to_devfreq(DEV)	container_of((DEV), struct devfreq, dev)

/* Devfreq events */
#define DEVFREQ_GOV_START			0x1
#define DEVFREQ_GOV_STOP			0x2
#define DEVFREQ_GOV_UPDATE_INTERVAL		0x3
#define DEVFREQ_GOV_SUSPEND			0x4
#define DEVFREQ_GOV_RESUME			0x5

#define DEVFREQ_MIN_FREQ			0
#define DEVFREQ_MAX_FREQ			ULONG_MAX

/*
 * Definition of the governor feature flags
 * - DEVFREQ_GOV_FLAG_IMMUTABLE
 *   : This governor is never changeable to other governors.
 * - DEVFREQ_GOV_FLAG_IRQ_DRIVEN
 *   : The devfreq won't schedule the work for this governor.
 */
#define DEVFREQ_GOV_FLAG_IMMUTABLE			BIT(0)
#define DEVFREQ_GOV_FLAG_IRQ_DRIVEN			BIT(1)

/*
 * Definition of governor attribute flags except for common sysfs attributes
 * - DEVFREQ_GOV_ATTR_POLLING_INTERVAL
 *   : Indicate polling_interval sysfs attribute
 * - DEVFREQ_GOV_ATTR_TIMER
 *   : Indicate timer sysfs attribute
 */
#define DEVFREQ_GOV_ATTR_POLLING_INTERVAL		BIT(0)
#define DEVFREQ_GOV_ATTR_TIMER				BIT(1)

/**
 * struct devfreq_cpu_data - Hold the per-cpu data
 * @node:	list node
 * @dev:	reference to cpu device.
 * @first_cpu:	the cpumask of the first cpu of a policy.
 * @opp_table:	reference to cpu opp table.
 * @cur_freq:	the current frequency of the cpu.
 * @min_freq:	the min frequency of the cpu.
 * @max_freq:	the max frequency of the cpu.
 *
 * This structure stores the required cpu_data of a cpu.
 * This is auto-populated by the governor.
 */
struct devfreq_cpu_data {
	struct list_head node;

	struct device *dev;
	unsigned int first_cpu;

	struct opp_table *opp_table;
	unsigned int cur_freq;
	unsigned int min_freq;
	unsigned int max_freq;
};

/**
 * struct devfreq_governor - Devfreq policy governor
 * @node:		list node - contains registered devfreq governors
 * @name:		Governor's name
 * @attrs:		Governor's sysfs attribute flags
 * @flags:		Governor's feature flags
 * @get_target_freq:	Returns desired operating frequency for the device.
 *			Basically, get_target_freq will run
 *			devfreq_dev_profile.get_dev_status() to get the
 *			status of the device (load = busy_time / total_time).
 * @event_handler:      Callback for devfreq core framework to notify events
 *                      to governors. Events include per device governor
 *                      init and exit, opp changes out of devfreq, suspend
 *                      and resume of per device devfreq during device idle.
 *
 * Note that the callbacks are called with devfreq->lock locked by devfreq.
 */
struct devfreq_governor {
	struct list_head node;

	const char name[DEVFREQ_NAME_LEN];
	const u64 attrs;
	const u64 flags;
	int (*get_target_freq)(struct devfreq *this, unsigned long *freq);
	int (*event_handler)(struct devfreq *devfreq,
				unsigned int event, void *data);
};

void devfreq_monitor_start(struct devfreq *devfreq);
void devfreq_monitor_stop(struct devfreq *devfreq);
void devfreq_monitor_suspend(struct devfreq *devfreq);
void devfreq_monitor_resume(struct devfreq *devfreq);
void devfreq_update_interval(struct devfreq *devfreq, unsigned int *delay);

int devfreq_add_governor(struct devfreq_governor *governor);
int devfreq_remove_governor(struct devfreq_governor *governor);

int devm_devfreq_add_governor(struct device *dev,
			      struct devfreq_governor *governor);

int devfreq_update_status(struct devfreq *devfreq, unsigned long freq);
int devfreq_update_target(struct devfreq *devfreq, unsigned long freq);
void devfreq_get_freq_range(struct devfreq *devfreq, unsigned long *min_freq,
			    unsigned long *max_freq);

static inline int devfreq_update_stats(struct devfreq *df)
{
	if (!df->profile->get_dev_status)
		return -EINVAL;

	return df->profile->get_dev_status(df->dev.parent, &df->last_status);
}
#endif /* _GOVERNOR_H */
