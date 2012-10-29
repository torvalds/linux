/*
 *  linux/drivers/devfreq/governor_simpleondemand.c
 *
 *  Copyright (C) 2011 Samsung Electronics
 *	MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/devfreq.h>
#include <linux/math64.h>
#include "governor.h"

/* Default constants for DevFreq-Simple-Ondemand (DFSO) */
#define DFSO_UPTHRESHOLD	(90)
#define DFSO_DOWNDIFFERENCTIAL	(5)
static int devfreq_simple_ondemand_func(struct devfreq *df,
					unsigned long *freq)
{
	struct devfreq_dev_status stat;
	int err = df->profile->get_dev_status(df->dev.parent, &stat);
	unsigned long long a, b;
	unsigned int dfso_upthreshold = DFSO_UPTHRESHOLD;
	unsigned int dfso_downdifferential = DFSO_DOWNDIFFERENCTIAL;
	struct devfreq_simple_ondemand_data *data = df->data;
	unsigned long max = (df->max_freq) ? df->max_freq : UINT_MAX;

	if (err)
		return err;

	if (data) {
		if (data->upthreshold)
			dfso_upthreshold = data->upthreshold;
		if (data->downdifferential)
			dfso_downdifferential = data->downdifferential;
	}
	if (dfso_upthreshold > 100 ||
	    dfso_upthreshold < dfso_downdifferential)
		return -EINVAL;

	/* Assume MAX if it is going to be divided by zero */
	if (stat.total_time == 0) {
		*freq = max;
		return 0;
	}

	/* Prevent overflow */
	if (stat.busy_time >= (1 << 24) || stat.total_time >= (1 << 24)) {
		stat.busy_time >>= 7;
		stat.total_time >>= 7;
	}

	/* Set MAX if it's busy enough */
	if (stat.busy_time * 100 >
	    stat.total_time * dfso_upthreshold) {
		*freq = max;
		return 0;
	}

	/* Set MAX if we do not know the initial frequency */
	if (stat.current_frequency == 0) {
		*freq = max;
		return 0;
	}

	/* Keep the current frequency */
	if (stat.busy_time * 100 >
	    stat.total_time * (dfso_upthreshold - dfso_downdifferential)) {
		*freq = stat.current_frequency;
		return 0;
	}

	/* Set the desired frequency based on the load */
	a = stat.busy_time;
	a *= stat.current_frequency;
	b = div_u64(a, stat.total_time);
	b *= 100;
	b = div_u64(b, (dfso_upthreshold - dfso_downdifferential / 2));
	*freq = (unsigned long) b;

	if (df->min_freq && *freq < df->min_freq)
		*freq = df->min_freq;
	if (df->max_freq && *freq > df->max_freq)
		*freq = df->max_freq;

	return 0;
}

static int devfreq_simple_ondemand_handler(struct devfreq *devfreq,
				unsigned int event, void *data)
{
	switch (event) {
	case DEVFREQ_GOV_START:
		devfreq_monitor_start(devfreq);
		break;

	case DEVFREQ_GOV_STOP:
		devfreq_monitor_stop(devfreq);
		break;

	case DEVFREQ_GOV_INTERVAL:
		devfreq_interval_update(devfreq, (unsigned int *)data);
		break;

	case DEVFREQ_GOV_SUSPEND:
		devfreq_monitor_suspend(devfreq);
		break;

	case DEVFREQ_GOV_RESUME:
		devfreq_monitor_resume(devfreq);
		break;

	default:
		break;
	}

	return 0;
}

const struct devfreq_governor devfreq_simple_ondemand = {
	.name = "simple_ondemand",
	.get_target_freq = devfreq_simple_ondemand_func,
	.event_handler = devfreq_simple_ondemand_handler,
};

static int __init devfreq_simple_ondemand_init(void)
{
	return devfreq_add_governor(&devfreq_simple_ondemand);
}
subsys_initcall(devfreq_simple_ondemand_init);

static void __exit devfreq_simple_ondemand_exit(void)
{
	int ret;

	ret = devfreq_remove_governor(&devfreq_simple_ondemand);
	if (ret)
		pr_err("%s: failed remove governor %d\n", __func__, ret);

	return;
}
module_exit(devfreq_simple_ondemand_exit);
