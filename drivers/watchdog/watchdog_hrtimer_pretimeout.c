// SPDX-License-Identifier: GPL-2.0
/*
 * (c) Copyright 2021 Hewlett Packard Enterprise Development LP.
 */

#include <linux/hrtimer.h>
#include <linux/watchdog.h>

#include "watchdog_core.h"
#include "watchdog_pretimeout.h"

static enum hrtimer_restart watchdog_hrtimer_pretimeout(struct hrtimer *timer)
{
	struct watchdog_core_data *wd_data;

	wd_data = container_of(timer, struct watchdog_core_data, pretimeout_timer);

	watchdog_notify_pretimeout(wd_data->wdd);
	return HRTIMER_NORESTART;
}

void watchdog_hrtimer_pretimeout_init(struct watchdog_device *wdd)
{
	struct watchdog_core_data *wd_data = wdd->wd_data;

	hrtimer_init(&wd_data->pretimeout_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	wd_data->pretimeout_timer.function = watchdog_hrtimer_pretimeout;
}

void watchdog_hrtimer_pretimeout_start(struct watchdog_device *wdd)
{
	if (!(wdd->info->options & WDIOF_PRETIMEOUT) &&
	    !watchdog_pretimeout_invalid(wdd, wdd->pretimeout))
		hrtimer_start(&wdd->wd_data->pretimeout_timer,
			      ktime_set(wdd->timeout - wdd->pretimeout, 0),
			      HRTIMER_MODE_REL);
	else
		hrtimer_cancel(&wdd->wd_data->pretimeout_timer);
}

void watchdog_hrtimer_pretimeout_stop(struct watchdog_device *wdd)
{
	hrtimer_cancel(&wdd->wd_data->pretimeout_timer);
}
