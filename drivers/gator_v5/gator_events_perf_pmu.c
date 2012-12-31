/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/perf_event.h>
#include "gator.h"

// gator_events_armvX.c is used for Linux 2.6.x
#if GATOR_PERF_PMU_SUPPORT

static const char *pmnc_name;
int pmnc_counters;
int ccnt = 0;

#define CNTMAX (6+1)

static DEFINE_MUTEX(perf_mutex);

unsigned long pmnc_enabled[CNTMAX];
unsigned long pmnc_event[CNTMAX];
unsigned long pmnc_count[CNTMAX];
unsigned long pmnc_key[CNTMAX];

static DEFINE_PER_CPU(int[CNTMAX], perfCurr);
static DEFINE_PER_CPU(int[CNTMAX], perfPrev);
static DEFINE_PER_CPU(int[CNTMAX], perfPrevDelta);
static DEFINE_PER_CPU(int[CNTMAX * 2], perfCnt);
static DEFINE_PER_CPU(struct perf_event *[CNTMAX], pevent);
static DEFINE_PER_CPU(struct perf_event_attr *[CNTMAX], pevent_attr);

static void gator_events_perf_pmu_stop(void);

static int gator_events_perf_pmu_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	int i;

	for (i = 0; i < pmnc_counters; i++) {
		char buf[40];
		if (i == 0) {
			snprintf(buf, sizeof buf, "%s_ccnt", pmnc_name);
		} else {
			snprintf(buf, sizeof buf, "%s_cnt%d", pmnc_name, i-1);
		}
		dir = gatorfs_mkdir(sb, root, buf);
		if (!dir) {
			return -1;
		}
		gatorfs_create_ulong(sb, dir, "enabled", &pmnc_enabled[i]);
		gatorfs_create_ulong(sb, dir, "count", &pmnc_count[i]);
		gatorfs_create_ro_ulong(sb, dir, "key", &pmnc_key[i]);
		if (i > 0) {
			gatorfs_create_ulong(sb, dir, "event", &pmnc_event[i]);
		}
	}

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
static void dummy_handler(struct perf_event *event, int unused, struct perf_sample_data *data, struct pt_regs *regs)
#else
static void dummy_handler(struct perf_event *event, struct perf_sample_data *data, struct pt_regs *regs)
#endif
{
// Required as perf_event_create_kernel_counter() requires an overflow handler, even though all we do is poll
}

static int gator_events_perf_pmu_online(int** buffer)
{
	int cnt, len = 0, cpu = smp_processor_id();

	// read the counters and toss the invalid data, return zero instead
	for (cnt = 0; cnt < pmnc_counters; cnt++) {
		struct perf_event * ev = per_cpu(pevent, cpu)[cnt];
		if (ev != NULL && ev->state == PERF_EVENT_STATE_ACTIVE) {
			ev->pmu->read(ev);
			per_cpu(perfPrev, cpu)[cnt] = per_cpu(perfCurr, cpu)[cnt] = local64_read(&ev->count);
			per_cpu(perfPrevDelta, cpu)[cnt] = 0;
			per_cpu(perfCnt, cpu)[len++] = pmnc_key[cnt];
			per_cpu(perfCnt, cpu)[len++] = 0;
		}
	}

	if (buffer)
		*buffer = per_cpu(perfCnt, cpu);

	return len;
}

static void gator_events_perf_pmu_online_dispatch(int cpu)
{
	int cnt;

	for (cnt = 0; cnt < pmnc_counters; cnt++) {
		if (per_cpu(pevent, cpu)[cnt] != NULL || per_cpu(pevent_attr, cpu)[cnt] == 0)
			continue;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
		per_cpu(pevent, cpu)[cnt] = perf_event_create_kernel_counter(per_cpu(pevent_attr, cpu)[cnt], cpu, 0, dummy_handler);
#else
		per_cpu(pevent, cpu)[cnt] = perf_event_create_kernel_counter(per_cpu(pevent_attr, cpu)[cnt], cpu, 0, dummy_handler, 0);
#endif
		if (IS_ERR(per_cpu(pevent, cpu)[cnt])) {
			pr_debug("gator: unable to online a counter on cpu %d\n", cpu);
			per_cpu(pevent, cpu)[cnt] = NULL;
			continue;
		}

		if (per_cpu(pevent, cpu)[cnt]->state != PERF_EVENT_STATE_ACTIVE) {
			pr_debug("gator: inactive counter on cpu %d\n", cpu);
			perf_event_release_kernel(per_cpu(pevent, cpu)[cnt]);
			per_cpu(pevent, cpu)[cnt] = NULL;
			continue;
		}
	}
}

static void gator_events_perf_pmu_offline_dispatch(int cpu)
{
	int cnt;
	struct perf_event * pe;

	for (cnt = 0; cnt < pmnc_counters; cnt++) {
		pe = NULL;
		mutex_lock(&perf_mutex);
		if (per_cpu(pevent, cpu)[cnt]) {
			pe = per_cpu(pevent, cpu)[cnt];
			per_cpu(pevent, cpu)[cnt] = NULL;
		}
		mutex_unlock(&perf_mutex);

		if (pe) {
			perf_event_release_kernel(pe);
		}
	}
}

static int gator_events_perf_pmu_start(void)
{
	int cnt, cpu;
	u32 size = sizeof(struct perf_event_attr);

	for_each_present_cpu(cpu) {
		for (cnt = 0; cnt < pmnc_counters; cnt++) {
			per_cpu(pevent, cpu)[cnt] = NULL;
			if (!pmnc_enabled[cnt]) // Skip disabled counters
				continue;

			per_cpu(perfPrev, cpu)[cnt] = 0;
			per_cpu(perfCurr, cpu)[cnt] = 0;
			per_cpu(perfPrevDelta, cpu)[cnt] = 0;
			per_cpu(pevent_attr, cpu)[cnt] = kmalloc(size, GFP_KERNEL);
			if (!per_cpu(pevent_attr, cpu)[cnt]) {
				gator_events_perf_pmu_stop();
				return -1;
			}

			memset(per_cpu(pevent_attr, cpu)[cnt], 0, size);
			per_cpu(pevent_attr, cpu)[cnt]->type = PERF_TYPE_RAW;
			per_cpu(pevent_attr, cpu)[cnt]->size = size;
			per_cpu(pevent_attr, cpu)[cnt]->config = pmnc_event[cnt];
			per_cpu(pevent_attr, cpu)[cnt]->sample_period = 0;
			per_cpu(pevent_attr, cpu)[cnt]->pinned = 1;

			// handle special case for ccnt
			if (cnt == ccnt) {
				per_cpu(pevent_attr, cpu)[cnt]->type = PERF_TYPE_HARDWARE;
				per_cpu(pevent_attr, cpu)[cnt]->config = PERF_COUNT_HW_CPU_CYCLES;
			}
		}
	}

	return 0;
}

static void gator_events_perf_pmu_stop(void)
{
	unsigned int cnt, cpu;

	for_each_present_cpu(cpu) {
		for (cnt = 0; cnt < pmnc_counters; cnt++) {
			if (per_cpu(pevent_attr, cpu)[cnt]) {
				kfree(per_cpu(pevent_attr, cpu)[cnt]);
				per_cpu(pevent_attr, cpu)[cnt] = NULL;
			}
		}
	}

	for (cnt = 0; cnt < pmnc_counters; cnt++) {
		pmnc_enabled[cnt] = 0;
		pmnc_event[cnt] = 0;
		pmnc_count[cnt] = 0;
	}
}

static int gator_events_perf_pmu_read(int **buffer)
{
	int cnt, delta, len = 0;
	int cpu = smp_processor_id();

	for (cnt = 0; cnt < pmnc_counters; cnt++) {
		struct perf_event * ev = per_cpu(pevent, cpu)[cnt];
		if (ev != NULL && ev->state == PERF_EVENT_STATE_ACTIVE) {
			ev->pmu->read(ev);
			per_cpu(perfCurr, cpu)[cnt] = local64_read(&ev->count);
			delta = per_cpu(perfCurr, cpu)[cnt] - per_cpu(perfPrev, cpu)[cnt];
			if (delta != per_cpu(perfPrevDelta, cpu)[cnt]) {
				per_cpu(perfPrevDelta, cpu)[cnt] = delta;
				per_cpu(perfPrev, cpu)[cnt] = per_cpu(perfCurr, cpu)[cnt];
				per_cpu(perfCnt, cpu)[len++] = pmnc_key[cnt];
				if (delta < 0)
					delta *= -1;
				per_cpu(perfCnt, cpu)[len++] = delta;
			}
		}
	}

	if (buffer)
		*buffer = per_cpu(perfCnt, cpu);

	return len;
}

static struct gator_interface gator_events_perf_pmu_interface = {
	.create_files = gator_events_perf_pmu_create_files,
	.start = gator_events_perf_pmu_start,
	.stop = gator_events_perf_pmu_stop,
	.online = gator_events_perf_pmu_online,
	.online_dispatch = gator_events_perf_pmu_online_dispatch,
	.offline_dispatch = gator_events_perf_pmu_offline_dispatch,
	.read = gator_events_perf_pmu_read,
};

int gator_events_perf_pmu_init(void)
{
	unsigned int cnt;

	switch (gator_cpuid()) {
	case ARM1136:
	case ARM1156:
	case ARM1176:
		pmnc_name = "ARM_ARM11";
		pmnc_counters = 3;
		ccnt = 2;
		break;
	case ARM11MPCORE:
		pmnc_name = "ARM_ARM11MPCore";
		pmnc_counters = 3;
		break;
	case CORTEX_A5:
		pmnc_name = "ARM_Cortex-A5";
		pmnc_counters = 2;
		break;
	case CORTEX_A7:
		pmnc_name = "ARM_Cortex-A7";
		pmnc_counters = 4;
		break;
	case CORTEX_A8:
		pmnc_name = "ARM_Cortex-A8";
		pmnc_counters = 4;
		break;
	case CORTEX_A9:
		pmnc_name = "ARM_Cortex-A9";
		pmnc_counters = 6;
		break;
	case CORTEX_A15:
		pmnc_name = "ARM_Cortex-A15";
		pmnc_counters = 6;
		break;
	case SCORPION:
		pmnc_name = "Scorpion";
		pmnc_counters = 4;
		break;
	case SCORPIONMP:
		pmnc_name = "ScorpionMP";
		pmnc_counters = 4;
		break;
	case KRAITSIM:
	case KRAIT:
		pmnc_name = "Krait";
		pmnc_counters = 4;
		break;
	default:
		return -1;
	}

	pmnc_counters++; // CNT[n] + CCNT

	for (cnt = 0; cnt < CNTMAX; cnt++) {
		pmnc_enabled[cnt] = 0;
		pmnc_event[cnt] = 0;
		pmnc_count[cnt] = 0;
		pmnc_key[cnt] = gator_events_get_key();
	}

	return gator_events_install(&gator_events_perf_pmu_interface);
}

gator_events_init(gator_events_perf_pmu_init);
#endif
