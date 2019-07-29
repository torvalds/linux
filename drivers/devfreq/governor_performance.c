/*
 *  linux/drivers/devfreq/governor_performance.c
 *
 *  Copyright (C) 2011 Samsung Electronics
 *	MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/devfreq.h>
#include <linux/module.h>
#include "governor.h"

static int devfreq_performance_func(struct devfreq *df,
				    unsigned long *freq)
{
	/*
	 * target callback should be able to get floor value as
	 * said in devfreq.h
	 */
	*freq = DEVFREQ_MAX_FREQ;
	return 0;
}

static int devfreq_performance_handler(struct devfreq *devfreq,
				unsigned int event, void *data)
{
	int ret = 0;

	if (event == DEVFREQ_GOV_START) {
		mutex_lock(&devfreq->lock);
		ret = update_devfreq(devfreq);
		mutex_unlock(&devfreq->lock);
	}

	return ret;
}

static struct devfreq_governor devfreq_performance = {
	.name = DEVFREQ_GOV_PERFORMANCE,
	.get_target_freq = devfreq_performance_func,
	.event_handler = devfreq_performance_handler,
};

static int __init devfreq_performance_init(void)
{
	return devfreq_add_governor(&devfreq_performance);
}
subsys_initcall(devfreq_performance_init);

static void __exit devfreq_performance_exit(void)
{
	int ret;

	ret = devfreq_remove_governor(&devfreq_performance);
	if (ret)
		pr_err("%s: failed remove governor %d\n", __func__, ret);

	return;
}
module_exit(devfreq_performance_exit);
MODULE_LICENSE("GPL");
