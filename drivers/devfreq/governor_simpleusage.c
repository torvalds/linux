/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/errno.h>
#include <linux/devfreq.h>
#include <linux/math64.h>
#include <linux/pm_qos.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "governor.h"

/* Default constants for DevFreq-Simple-Ondemand (DFSO) */
#define DFSO_UPTHRESHOLD	(50)
#define DFSO_TARGET_PERCENTAGE	(20)
#define DFSO_PROPORTIONAL	(120)

struct devfreq_simple_usage_nb {
	struct list_head list;
	struct notifier_block nb;
	struct devfreq *df;
};

static LIST_HEAD(devfreq_pm_qos_list);
static DEFINE_MUTEX(devfreq_pm_qos_lock);

static int devfreq_simple_usage_nb(struct notifier_block *nb, unsigned long val, void *data)
{
	struct devfreq_simple_usage_nb *simple_usage_nb;
	struct devfreq_simple_usage_data *simple_usage_data;

	simple_usage_nb = container_of(nb, struct devfreq_simple_usage_nb, nb);

	simple_usage_data = simple_usage_nb->df->data;

	simple_usage_data->pm_qos_min = val;

	mutex_lock(&simple_usage_nb->df->lock);
	update_devfreq(simple_usage_nb->df);
	mutex_unlock(&simple_usage_nb->df->lock);

	return NOTIFY_OK;
}

static int devfreq_simple_usage_func(struct devfreq *df, unsigned long *freq)
{
	struct devfreq_dev_status stat;
	int err = df->profile->get_dev_status(df->dev.parent, &stat);
	unsigned long a, b;
	unsigned int dfso_upthreshold = DFSO_UPTHRESHOLD;
	unsigned int dfso_target_percentage = DFSO_TARGET_PERCENTAGE;
	unsigned int dfso_proportional = DFSO_PROPORTIONAL;
	struct devfreq_simple_usage_data *data = df->data;
	unsigned long max = (df->max_freq) ? df->max_freq : 0;

	if (!data->en_monitoring) {
		*freq = pm_qos_request(data->pm_qos_class);
		return 0;
	}

	if (err)
		return err;
	if ((stat.busy_time == 0) || (stat.total_time == 0)) {
		*freq = stat.current_frequency;
		goto freq_calc_done;
	}

	if (data) {
		if (data->upthreshold)
			dfso_upthreshold = data->upthreshold;
		if (data->target_percentage)
			dfso_target_percentage = data->target_percentage;
		if (data->proportional)
			dfso_proportional = data->proportional;
	}

	a = stat.busy_time * dfso_proportional;
	b = div_u64(a, stat.total_time);

	/* If percentage is larger than upthreshold, set with max freq */
	if (b >= data->upthreshold) {
		max = max3((unsigned int)max, data->cal_qos_max, data->pm_qos_min);
		*freq = max;
		return 0;
	}

	b *= stat.current_frequency;

	a = div_u64(b, dfso_target_percentage);

	if (a > data->cal_qos_max)
		a = data->cal_qos_max;

	*freq = (unsigned long) a;

freq_calc_done:
	if (data->pm_qos_min && *freq < data->pm_qos_min)
		*freq = data->pm_qos_min;

	if (df->min_freq && *freq < df->min_freq)
		*freq = df->min_freq;
	if (df->max_freq && *freq > df->max_freq)
		*freq = df->max_freq;

	return 0;
}

static int devfreq_simple_usage_init(struct devfreq *df)
{
	struct devfreq_simple_usage_nb *simple_usage_nb;
	struct devfreq_simple_usage_data *data = df->data;
	int err;

	if (!data)
		return -EINVAL;

	simple_usage_nb = kzalloc(sizeof(*simple_usage_nb), GFP_KERNEL);
	if (!simple_usage_nb)
		return -ENOMEM;

	simple_usage_nb->df = df;
	simple_usage_nb->nb.notifier_call = devfreq_simple_usage_nb;

	err = pm_qos_add_notifier(data->pm_qos_class, &simple_usage_nb->nb);

	if (err < 0)
		goto err;

	mutex_lock(&devfreq_pm_qos_lock);
	list_add_tail(&simple_usage_nb->list, &devfreq_pm_qos_list);
	mutex_unlock(&devfreq_pm_qos_lock);

	return 0;
err:
	kfree(simple_usage_nb);

	return err;
}

static void devfreq_simple_usage_exit(struct devfreq *df)
{
	struct devfreq_simple_usage_nb *simple_usage_nb;
	struct devfreq_simple_usage_data *data;

	mutex_lock(&devfreq_pm_qos_lock);

	list_for_each_entry(simple_usage_nb, &devfreq_pm_qos_list, list) {
		if (simple_usage_nb->df == df) {
			data = simple_usage_nb->df->data;
			pm_qos_remove_notifier(data->pm_qos_class, &simple_usage_nb->nb);
			goto out;
		}
	}
out:
	mutex_unlock(&devfreq_pm_qos_lock);
}

const struct devfreq_governor devfreq_simple_usage = {
	.name = "simple_usage",
	.get_target_freq = devfreq_simple_usage_func,
	.init = devfreq_simple_usage_init,
	.exit = devfreq_simple_usage_exit,
};
