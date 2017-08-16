/*
 * Simple heartbeat STM source driver
 * Copyright (c) 2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Heartbeat STM source will send repetitive messages over STM devices to a
 * trace host.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/stm.h>

#define STM_HEARTBEAT_MAX	32

static int nr_devs = 4;
static int interval_ms = 10;

module_param(nr_devs, int, 0400);
module_param(interval_ms, int, 0600);

static struct stm_heartbeat {
	struct stm_source_data	data;
	struct hrtimer		hrtimer;
	unsigned int		active;
} stm_heartbeat[STM_HEARTBEAT_MAX];

static const char str[] = "heartbeat stm source driver is here to serve you";

static enum hrtimer_restart stm_heartbeat_hrtimer_handler(struct hrtimer *hr)
{
	struct stm_heartbeat *heartbeat = container_of(hr, struct stm_heartbeat,
						       hrtimer);

	stm_source_write(&heartbeat->data, 0, str, sizeof str);
	if (heartbeat->active)
		hrtimer_forward_now(hr, ms_to_ktime(interval_ms));

	return heartbeat->active ? HRTIMER_RESTART : HRTIMER_NORESTART;
}

static int stm_heartbeat_link(struct stm_source_data *data)
{
	struct stm_heartbeat *heartbeat =
		container_of(data, struct stm_heartbeat, data);

	heartbeat->active = 1;
	hrtimer_start(&heartbeat->hrtimer, ms_to_ktime(interval_ms),
		      HRTIMER_MODE_ABS);

	return 0;
}

static void stm_heartbeat_unlink(struct stm_source_data *data)
{
	struct stm_heartbeat *heartbeat =
		container_of(data, struct stm_heartbeat, data);

	heartbeat->active = 0;
	hrtimer_cancel(&heartbeat->hrtimer);
}

static int stm_heartbeat_init(void)
{
	int i, ret = -ENOMEM;

	if (nr_devs < 0 || nr_devs > STM_HEARTBEAT_MAX)
		return -EINVAL;

	for (i = 0; i < nr_devs; i++) {
		stm_heartbeat[i].data.name =
			kasprintf(GFP_KERNEL, "heartbeat.%d", i);
		if (!stm_heartbeat[i].data.name)
			goto fail_unregister;

		stm_heartbeat[i].data.nr_chans	= 1;
		stm_heartbeat[i].data.link		= stm_heartbeat_link;
		stm_heartbeat[i].data.unlink	= stm_heartbeat_unlink;
		hrtimer_init(&stm_heartbeat[i].hrtimer, CLOCK_MONOTONIC,
			     HRTIMER_MODE_ABS);
		stm_heartbeat[i].hrtimer.function =
			stm_heartbeat_hrtimer_handler;

		ret = stm_source_register_device(NULL, &stm_heartbeat[i].data);
		if (ret)
			goto fail_free;
	}

	return 0;

fail_unregister:
	for (i--; i >= 0; i--) {
		stm_source_unregister_device(&stm_heartbeat[i].data);
fail_free:
		kfree(stm_heartbeat[i].data.name);
	}

	return ret;
}

static void stm_heartbeat_exit(void)
{
	int i;

	for (i = 0; i < nr_devs; i++) {
		stm_source_unregister_device(&stm_heartbeat[i].data);
		kfree(stm_heartbeat[i].data.name);
	}
}

module_init(stm_heartbeat_init);
module_exit(stm_heartbeat_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("stm_heartbeat driver");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@linux.intel.com>");
