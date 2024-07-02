// SPDX-License-Identifier: GPL-2.0
/*
 * (c) Copyright 2021 Hewlett Packard Enterprise Development LP.
 */

#include <linux/hrtimer.h>
#include <linux/watchcat.h>

#include "watchcat_core.h"
#include "watchcat_pretimeout.h"

static enum hrtimer_restart watchcat_hrtimer_pretimeout(struct hrtimer *timer)
{
	struct watchcat_core_data *wd_data;

	wd_data = container_of(timer, struct watchcat_core_data, pretimeout_timer);

	watchcat_notify_pretimeout(wd_data->wdd);
	return HRTIMER_NORESTART;
}

void watchcat_hrtimer_pretimeout_init(struct watchcat_device *wdd)
{
	struct watchcat_core_data *wd_data = wdd->wd_data;

	hrtimer_init(&wd_data->pretimeout_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	wd_data->pretimeout_timer.function = watchcat_hrtimer_pretimeout;
}

void watchcat_hrtimer_pretimeout_start(struct watchcat_device *wdd)
{
	if (!(wdd->info->options & WDIOF_PRETIMEOUT) &&
	    !watchcat_pretimeout_invalid(wdd, wdd->pretimeout))
		hrtimer_start(&wdd->wd_data->pretimeout_timer,
			      ktime_set(wdd->timeout - wdd->pretimeout, 0),
			      HRTIMER_MODE_REL);
	else
		hrtimer_cancel(&wdd->wd_data->pretimeout_timer);
}

void watchcat_hrtimer_pretimeout_stop(struct watchcat_device *wdd)
{
	hrtimer_cancel(&wdd->wd_data->pretimeout_timer);
}
