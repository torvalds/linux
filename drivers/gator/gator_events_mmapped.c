/*
 * Example events provider
 *
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Similar entries to those below must be present in the events.xml file.
 * To add them to the events.xml, create an events-mmap.xml with the
 * following contents and rebuild gatord:
 *
 * <category name="mmapped">
 *   <event counter="mmapped_cnt0" title="Simulated1" name="Sine" display="maximum" class="absolute" description="Sort-of-sine"/>
 *   <event counter="mmapped_cnt1" title="Simulated2" name="Triangle" display="maximum" class="absolute" description="Triangular wave"/>
 *   <event counter="mmapped_cnt2" title="Simulated3" name="PWM" display="maximum" class="absolute" description="PWM Signal"/>
 * </category>
 *
 * When adding custom events, be sure to do the following:
 * - add any needed .c files to the gator driver Makefile
 * - call gator_events_install in the events init function
 * - add the init function to GATOR_EVENTS_LIST in gator_main.c
 * - add a new events-*.xml file to the gator daemon and rebuild
 *
 * Troubleshooting:
 * - verify the new events are part of events.xml, which is created when building the daemon
 * - verify the new events exist at /dev/gator/events/ once gatord is launched
 * - verify the counter name in the XML matches the name at /dev/gator/events
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/ratelimit.h>

#include "gator.h"

#define MMAPPED_COUNTERS_NUM 3

static int mmapped_global_enabled;

static struct {
	unsigned long enabled;
	unsigned long key;
} mmapped_counters[MMAPPED_COUNTERS_NUM];

static int mmapped_buffer[MMAPPED_COUNTERS_NUM * 2];

static s64 prev_time;

/* Adds mmapped_cntX directories and enabled, event, and key files to /dev/gator/events */
static int gator_events_mmapped_create_files(struct super_block *sb,
					     struct dentry *root)
{
	int i;

	for (i = 0; i < MMAPPED_COUNTERS_NUM; i++) {
		char buf[16];
		struct dentry *dir;

		snprintf(buf, sizeof(buf), "mmapped_cnt%d", i);
		dir = gatorfs_mkdir(sb, root, buf);
		if (WARN_ON(!dir))
			return -1;
		gatorfs_create_ulong(sb, dir, "enabled",
				     &mmapped_counters[i].enabled);
		gatorfs_create_ro_ulong(sb, dir, "key",
					&mmapped_counters[i].key);
	}

	return 0;
}

static int gator_events_mmapped_start(void)
{
	int i;
	struct timespec ts;

	getnstimeofday(&ts);
	prev_time = timespec_to_ns(&ts);

	mmapped_global_enabled = 0;
	for (i = 0; i < MMAPPED_COUNTERS_NUM; i++) {
		if (mmapped_counters[i].enabled) {
			mmapped_global_enabled = 1;
			break;
		}
	}

	return 0;
}

static void gator_events_mmapped_stop(void)
{
}

/* This function "simulates" counters, generating values of fancy
 * functions like sine or triangle... */
static int mmapped_simulate(int counter, int delta_in_us)
{
	int result = 0;

	switch (counter) {
	case 0:		/* sort-of-sine */
		{
			static int t;
			int x;

			t += delta_in_us;
			if (t > 2048000)
				t = 0;

			if (t % 1024000 < 512000)
				x = 512000 - (t % 512000);
			else
				x = t % 512000;

			result = 32 * x / 512000;
			result = result * result;

			if (t < 1024000)
				result = 1922 - result;
		}
		break;
	case 1:		/* triangle */
		{
			static int v, d = 1;

			v = v + d * delta_in_us;
			if (v < 0) {
				v = 0;
				d = 1;
			} else if (v > 1000000) {
				v = 1000000;
				d = -1;
			}

			result = v;
		}
		break;
	case 2:		/* PWM signal */
		{
			static int dc, x, t;

			t += delta_in_us;
			if (t > 1000000)
				t = 0;
			if (x / 1000000 != (x + delta_in_us) / 1000000)
				dc = (dc + 100000) % 1000000;
			x += delta_in_us;

			result = t < dc ? 0 : 10;
		}
		break;
	}

	return result;
}

static int gator_events_mmapped_read(int **buffer, bool sched_switch)
{
	int i;
	int len = 0;
	int delta_in_us;
	struct timespec ts;
	s64 time;

	/* System wide counters - read from one core only */
	if (!on_primary_core() || !mmapped_global_enabled)
		return 0;

	getnstimeofday(&ts);
	time = timespec_to_ns(&ts);
	delta_in_us = (int)(time - prev_time) / 1000;
	prev_time = time;

	for (i = 0; i < MMAPPED_COUNTERS_NUM; i++) {
		if (mmapped_counters[i].enabled) {
			mmapped_buffer[len++] = mmapped_counters[i].key;
			mmapped_buffer[len++] =
			    mmapped_simulate(i, delta_in_us);
		}
	}

	if (buffer)
		*buffer = mmapped_buffer;

	return len;
}

static struct gator_interface gator_events_mmapped_interface = {
	.create_files = gator_events_mmapped_create_files,
	.start = gator_events_mmapped_start,
	.stop = gator_events_mmapped_stop,
	.read = gator_events_mmapped_read,
};

/* Must not be static! */
int __init gator_events_mmapped_init(void)
{
	int i;

	for (i = 0; i < MMAPPED_COUNTERS_NUM; i++) {
		mmapped_counters[i].enabled = 0;
		mmapped_counters[i].key = gator_events_get_key();
	}

	return gator_events_install(&gator_events_mmapped_interface);
}
