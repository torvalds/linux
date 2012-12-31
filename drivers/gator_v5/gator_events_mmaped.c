/*
 * Example events provider
 *
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Similar entries to those below must be present in the events.xml file.
 * To add them to the events.xml, create an events-mmap.xml with the 
 * following contents and rebuild gatord:
 *
 * <counter_set name="mmaped_cntX">
 *   <counter name="mmaped_cnt0"/>
 *   <counter name="mmaped_cnt1"/>
 *   <counter name="mmaped_cnt2"/>
 * </counter_set>
 * <category name="mmaped" counter_set="mmaped_cntX" per_cpu="no">
 *   <event event="0x0" title="Simulated" name="Sine" display="maximum" average_selection="yes" description="Sort-of-sine"/>
 *   <event event="0x1" title="Simulated" name="Triangle" display="maximum" average_selection="yes" description="Triangular wave"/>
 *   <event event="0x2" title="Simulated" name="PWM" display="maximum" average_selection="yes" description="PWM Signal"/>
 * </category>
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/ratelimit.h>

#include "gator.h"

#define MMAPED_COUNTERS_NUM 3

static struct {
	unsigned long enabled;
	unsigned long event;
	unsigned long key;
} mmaped_counters[MMAPED_COUNTERS_NUM];

static int mmaped_buffer[MMAPED_COUNTERS_NUM * 2];

#ifdef TODO
static void __iomem *mmaped_base;
#endif

#ifndef TODO
static s64 prev_time;
#endif

/* Adds mmaped_cntX directories and enabled, event, and key files to /dev/gator/events */
static int gator_events_mmaped_create_files(struct super_block *sb,
		struct dentry *root)
{
	int i;

	for (i = 0; i < MMAPED_COUNTERS_NUM; i++) {
		char buf[16];
		struct dentry *dir;

		snprintf(buf, sizeof(buf), "mmaped_cnt%d", i);
		dir = gatorfs_mkdir(sb, root, buf);
		if (WARN_ON(!dir))
			return -1;
		gatorfs_create_ulong(sb, dir, "enabled",
				&mmaped_counters[i].enabled);
		gatorfs_create_ulong(sb, dir, "event",
				&mmaped_counters[i].event);
		gatorfs_create_ro_ulong(sb, dir, "key",
				&mmaped_counters[i].key);
	}

	return 0;
}

static int gator_events_mmaped_start(void)
{
#ifdef TODO
	for (i = 0; i < MMAPED_COUNTERS_NUM; i++)
		writel(mmaped_counters[i].event,
				mmaped_base + COUNTERS_CONFIG_OFFSET[i]);

	writel(ENABLED, COUNTERS_CONTROL_OFFSET);
#endif

#ifndef TODO
	struct timespec ts;
	getnstimeofday(&ts);
	prev_time = timespec_to_ns(&ts);
#endif

	return 0;
}

static void gator_events_mmaped_stop(void)
{
#ifdef TODO
	writel(DISABLED, COUNTERS_CONTROL_OFFSET);
#endif
}

#ifndef TODO
/* This function "simulates" counters, generating values of fancy
 * functions like sine or triangle... */
static int mmaped_simulate(int counter, int delta_in_us)
{
	int result = 0;

	switch (counter) {
	case 0: /* sort-of-sine */
		{
			static int t = 0;
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
	case 1: /* triangle */
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
	case 2: /* PWM signal */
		{
			static int t, dc, x;

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
#endif

static int gator_events_mmaped_read(int **buffer)
{
	int i;
	int len = 0;
#ifndef TODO
	int delta_in_us;
	struct timespec ts;
	s64 time;
#endif

	/* System wide counters - read from one core only */
	if (smp_processor_id())
		return 0;

#ifndef TODO	
	getnstimeofday(&ts);
	time = timespec_to_ns(&ts);
	delta_in_us = (int)(time - prev_time) / 1000;
	prev_time = time;
#endif

	for (i = 0; i < MMAPED_COUNTERS_NUM; i++) {
		if (mmaped_counters[i].enabled) {
			mmaped_buffer[len++] = mmaped_counters[i].key;
#ifdef TODO
			mmaped_buffer[len++] = readl(mmaped_base +
					COUNTERS_VALUE_OFFSET[i]);
#else
			mmaped_buffer[len++] = mmaped_simulate(
					mmaped_counters[i].event, delta_in_us);
#endif
		}
	}
	
	if (buffer)
		*buffer = mmaped_buffer;

	return len;
}

static struct gator_interface gator_events_mmaped_interface = {
	.create_files = gator_events_mmaped_create_files,
	.start = gator_events_mmaped_start,
	.stop = gator_events_mmaped_stop,
	.read = gator_events_mmaped_read,
};

/* Must not be static! */
int __init gator_events_mmaped_init(void)
{
	int i;

#ifdef TODO
	mmaped_base = ioremap(COUNTERS_PHYS_ADDR, SZ_4K);
	if (!mmaped_base)
		return -ENOMEM;	
#endif

	for (i = 0; i < MMAPED_COUNTERS_NUM; i++) {
		mmaped_counters[i].enabled = 0;
		mmaped_counters[i].key = gator_events_get_key();
	}

	return gator_events_install(&gator_events_mmaped_interface);
}
gator_events_init(gator_events_mmaped_init);
