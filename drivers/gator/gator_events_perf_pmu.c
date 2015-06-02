/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "gator.h"

/* gator_events_armvX.c is used for Linux 2.6.x */
#if GATOR_PERF_PMU_SUPPORT

#include <linux/io.h>
#ifdef CONFIG_OF
#include <linux/of_address.h>
#endif
#include <linux/perf_event.h>
#include <linux/slab.h>

extern bool event_based_sampling;

/* Maximum number of per-core counters - currently reserves enough space for two full hardware PMUs for big.LITTLE */
#define CNTMAX 16
#define CCI_400 4
#define CCI_500 8
#define CCN_5XX 8
/* Maximum number of uncore counters */
/* + 1 for the cci-400 cycles counter */
/* cci-500 has no cycles counter */
/* + 1 for the CCN-5xx cycles counter */
#define UCCNT (CCI_400 + 1 + CCI_500 + CCN_5XX + 1)

/* Default to 0 if unable to probe the revision which was the previous behavior */
#define DEFAULT_CCI_REVISION 0

/* A gator_attr is needed for every counter */
struct gator_attr {
	/* Set once in gator_events_perf_pmu_*_init - the name of the event in the gatorfs */
	char name[40];
	/* Exposed in gatorfs - set by gatord to enable this counter */
	unsigned long enabled;
	/* Set once in gator_events_perf_pmu_*_init - the perf type to use, see perf_type_id in the perf_event.h header file. */
	unsigned long type;
	/* Exposed in gatorfs - set by gatord to select the event to collect */
	unsigned long event;
	/* Exposed in gatorfs - set by gatord with the sample period to use and enable EBS for this counter */
	unsigned long count;
	/* Exposed as read only in gatorfs - set once in __attr_init as the key to use in the APC data */
	unsigned long key;
};

/* Per-core counter attributes */
static struct gator_attr attrs[CNTMAX];
/* Number of initialized per-core counters */
static int attr_count;
/* Uncore counter attributes */
static struct gator_attr uc_attrs[UCCNT];
/* Number of initialized uncore counters */
static int uc_attr_count;

struct gator_event {
	uint32_t curr;
	uint32_t prev;
	uint32_t prev_delta;
	bool zero;
	struct perf_event *pevent;
	struct perf_event_attr *pevent_attr;
};

static DEFINE_PER_CPU(struct gator_event[CNTMAX], events);
static struct gator_event uc_events[UCCNT];
static DEFINE_PER_CPU(int[(CNTMAX + UCCNT)*2], perf_cnt);

static void gator_events_perf_pmu_stop(void);

static int __create_files(struct super_block *sb, struct dentry *root, struct gator_attr *const attr)
{
	struct dentry *dir;

	if (attr->name[0] == '\0')
		return 0;
	dir = gatorfs_mkdir(sb, root, attr->name);
	if (!dir)
		return -1;
	gatorfs_create_ulong(sb, dir, "enabled", &attr->enabled);
	gatorfs_create_ulong(sb, dir, "count", &attr->count);
	gatorfs_create_ro_ulong(sb, dir, "key", &attr->key);
	gatorfs_create_ulong(sb, dir, "event", &attr->event);

	return 0;
}

static int gator_events_perf_pmu_create_files(struct super_block *sb, struct dentry *root)
{
	int cnt;

	for (cnt = 0; cnt < attr_count; cnt++) {
		if (__create_files(sb, root, &attrs[cnt]) != 0)
			return -1;
	}

	for (cnt = 0; cnt < uc_attr_count; cnt++) {
		if (__create_files(sb, root, &uc_attrs[cnt]) != 0)
			return -1;
	}

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
static void ebs_overflow_handler(struct perf_event *event, int unused, struct perf_sample_data *data, struct pt_regs *regs)
#else
static void ebs_overflow_handler(struct perf_event *event, struct perf_sample_data *data, struct pt_regs *regs)
#endif
{
	gator_backtrace_handler(regs);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
static void dummy_handler(struct perf_event *event, int unused, struct perf_sample_data *data, struct pt_regs *regs)
#else
static void dummy_handler(struct perf_event *event, struct perf_sample_data *data, struct pt_regs *regs)
#endif
{
	/* Required as perf_event_create_kernel_counter() requires an overflow handler, even though all we do is poll */
}

static int gator_events_perf_pmu_read(int **buffer, bool sched_switch);

static int gator_events_perf_pmu_online(int **buffer, bool migrate)
{
	return gator_events_perf_pmu_read(buffer, false);
}

static void __online_dispatch(int cpu, bool migrate, struct gator_attr *const attr, struct gator_event *const event)
{
	perf_overflow_handler_t handler;

	event->zero = true;

	if (event->pevent != NULL || event->pevent_attr == 0 || migrate)
		return;

	if (attr->count > 0)
		handler = ebs_overflow_handler;
	else
		handler = dummy_handler;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
	event->pevent = perf_event_create_kernel_counter(event->pevent_attr, cpu, 0, handler);
#else
	event->pevent = perf_event_create_kernel_counter(event->pevent_attr, cpu, 0, handler, 0);
#endif
	if (IS_ERR(event->pevent)) {
		pr_debug("gator: unable to online a counter on cpu %d\n", cpu);
		event->pevent = NULL;
		return;
	}

	if (event->pevent->state != PERF_EVENT_STATE_ACTIVE) {
		pr_debug("gator: inactive counter on cpu %d\n", cpu);
		perf_event_release_kernel(event->pevent);
		event->pevent = NULL;
		return;
	}
}

static void gator_events_perf_pmu_online_dispatch(int cpu, bool migrate)
{
	int cnt;

	cpu = pcpu_to_lcpu(cpu);

	for (cnt = 0; cnt < attr_count; cnt++)
		__online_dispatch(cpu, migrate, &attrs[cnt], &per_cpu(events, cpu)[cnt]);

	if (cpu == 0) {
		for (cnt = 0; cnt < uc_attr_count; cnt++)
			__online_dispatch(cpu, migrate, &uc_attrs[cnt], &uc_events[cnt]);
	}
}

static void __offline_dispatch(int cpu, struct gator_event *const event)
{
	struct perf_event *pe = NULL;

	if (event->pevent) {
		pe = event->pevent;
		event->pevent = NULL;
	}

	if (pe)
		perf_event_release_kernel(pe);
}

static void gator_events_perf_pmu_offline_dispatch(int cpu, bool migrate)
{
	int cnt;

	if (migrate)
		return;
	cpu = pcpu_to_lcpu(cpu);

	for (cnt = 0; cnt < attr_count; cnt++)
		__offline_dispatch(cpu, &per_cpu(events, cpu)[cnt]);

	if (cpu == 0) {
		for (cnt = 0; cnt < uc_attr_count; cnt++)
			__offline_dispatch(cpu, &uc_events[cnt]);
	}
}

static int __check_ebs(struct gator_attr *const attr)
{
	if (attr->count > 0) {
		if (!event_based_sampling) {
			event_based_sampling = true;
		} else {
			pr_warning("gator: Only one ebs counter is allowed\n");
			return -1;
		}
	}

	return 0;
}

static int __start(struct gator_attr *const attr, struct gator_event *const event)
{
	u32 size = sizeof(struct perf_event_attr);

	event->pevent = NULL;
	/* Skip disabled counters */
	if (!attr->enabled)
		return 0;

	event->prev = 0;
	event->curr = 0;
	event->prev_delta = 0;
	event->pevent_attr = kmalloc(size, GFP_KERNEL);
	if (!event->pevent_attr) {
		gator_events_perf_pmu_stop();
		return -1;
	}

	memset(event->pevent_attr, 0, size);
	event->pevent_attr->type = attr->type;
	event->pevent_attr->size = size;
	event->pevent_attr->config = attr->event;
	event->pevent_attr->sample_period = attr->count;
	event->pevent_attr->pinned = 1;

	return 0;
}

static int gator_events_perf_pmu_start(void)
{
	int cnt, cpu;

	event_based_sampling = false;
	for (cnt = 0; cnt < attr_count; cnt++) {
		if (__check_ebs(&attrs[cnt]) != 0)
			return -1;
	}

	for (cnt = 0; cnt < uc_attr_count; cnt++) {
		if (__check_ebs(&uc_attrs[cnt]) != 0)
			return -1;
	}

	for_each_present_cpu(cpu) {
		for (cnt = 0; cnt < attr_count; cnt++) {
			if (__start(&attrs[cnt], &per_cpu(events, cpu)[cnt]) != 0)
				return -1;
		}
	}

	for (cnt = 0; cnt < uc_attr_count; cnt++) {
		if (__start(&uc_attrs[cnt], &uc_events[cnt]) != 0)
			return -1;
	}

	return 0;
}

static void __event_stop(struct gator_event *const event)
{
	kfree(event->pevent_attr);
	event->pevent_attr = NULL;
}

static void __attr_stop(struct gator_attr *const attr)
{
	attr->enabled = 0;
	attr->event = 0;
	attr->count = 0;
}

static void gator_events_perf_pmu_stop(void)
{
	unsigned int cnt, cpu;

	for_each_present_cpu(cpu) {
		for (cnt = 0; cnt < attr_count; cnt++)
			__event_stop(&per_cpu(events, cpu)[cnt]);
	}

	for (cnt = 0; cnt < uc_attr_count; cnt++)
		__event_stop(&uc_events[cnt]);

	for (cnt = 0; cnt < attr_count; cnt++)
		__attr_stop(&attrs[cnt]);

	for (cnt = 0; cnt < uc_attr_count; cnt++)
		__attr_stop(&uc_attrs[cnt]);
}

static void __read(int *const len, int cpu, struct gator_attr *const attr, struct gator_event *const event)
{
	uint32_t delta;
	struct perf_event *const ev = event->pevent;

	if (ev != NULL && ev->state == PERF_EVENT_STATE_ACTIVE) {
		/* After creating the perf counter in __online_dispatch, there
		 * is a race condition between gator_events_perf_pmu_online and
		 * gator_events_perf_pmu_read. So have
		 * gator_events_perf_pmu_online call gator_events_perf_pmu_read
		 * and in __read check to see if it's the first call after
		 * __online_dispatch and if so, run the online code.
		 */
		if (event->zero) {
			ev->pmu->read(ev);
			event->prev = event->curr = local64_read(&ev->count);
			event->prev_delta = 0;
			per_cpu(perf_cnt, cpu)[(*len)++] = attr->key;
			per_cpu(perf_cnt, cpu)[(*len)++] = 0;
			event->zero = false;
		} else {
			ev->pmu->read(ev);
			event->curr = local64_read(&ev->count);
			delta = event->curr - event->prev;
			if (delta != 0 || delta != event->prev_delta) {
				event->prev_delta = delta;
				event->prev = event->curr;
				per_cpu(perf_cnt, cpu)[(*len)++] = attr->key;
				per_cpu(perf_cnt, cpu)[(*len)++] = delta;
			}
		}
	}
}

static int gator_events_perf_pmu_read(int **buffer, bool sched_switch)
{
	int cnt, len = 0;
	const int cpu = get_logical_cpu();

	for (cnt = 0; cnt < attr_count; cnt++)
		__read(&len, cpu, &attrs[cnt], &per_cpu(events, cpu)[cnt]);

	if (cpu == 0) {
		for (cnt = 0; cnt < uc_attr_count; cnt++)
			__read(&len, cpu, &uc_attrs[cnt], &uc_events[cnt]);
	}

	if (buffer)
		*buffer = per_cpu(perf_cnt, cpu);

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

static void __attr_init(struct gator_attr *const attr)
{
	attr->name[0] = '\0';
	attr->enabled = 0;
	attr->type = 0;
	attr->event = 0;
	attr->count = 0;
	attr->key = gator_events_get_key();
}

#ifdef CONFIG_OF

static const struct of_device_id arm_cci_matches[] = {
	{.compatible = "arm,cci-400" },
	{},
};

static int probe_cci_revision(void)
{
	struct device_node *np;
	struct resource res;
	void __iomem *cci_ctrl_base;
	int rev;
	int ret = DEFAULT_CCI_REVISION;

	np = of_find_matching_node(NULL, arm_cci_matches);
	if (!np)
		return ret;

	if (of_address_to_resource(np, 0, &res))
		goto node_put;

	cci_ctrl_base = ioremap(res.start, resource_size(&res));

	rev = (readl_relaxed(cci_ctrl_base + 0xfe8) >> 4) & 0xf;

	if (rev <= 4)
		ret = 0;
	else if (rev <= 6)
		ret = 1;

	iounmap(cci_ctrl_base);

 node_put:
	of_node_put(np);

	return ret;
}

#else

static int probe_cci_revision(void)
{
	return DEFAULT_CCI_REVISION;
}

#endif

static void gator_events_perf_pmu_uncore_init(const char *const name, const int type, const int count, const bool has_cycles_counter)
{
	int cnt;

	if (has_cycles_counter) {
		snprintf(uc_attrs[uc_attr_count].name, sizeof(uc_attrs[uc_attr_count].name), "%s_ccnt", name);
		uc_attrs[uc_attr_count].type = type;
		++uc_attr_count;
	}

	for (cnt = 0; cnt < count; ++cnt, ++uc_attr_count) {
		struct gator_attr *const attr = &uc_attrs[uc_attr_count];

		snprintf(attr->name, sizeof(attr->name), "%s_cnt%d", name, cnt);
		attr->type = type;
	}
}

static void gator_events_perf_pmu_cci_400_init(const int type)
{
	const char *cci_name;

	switch (probe_cci_revision()) {
	case 0:
		cci_name = "CCI_400";
		break;
	case 1:
		cci_name = "CCI_400-r1";
		break;
	default:
		pr_debug("gator: unrecognized cci-400 revision\n");
		return;
	}

	gator_events_perf_pmu_uncore_init(cci_name, type, CCI_400, true);
}

static void gator_events_perf_pmu_cpu_init(const struct gator_cpu *const gator_cpu, const int type)
{
	int cnt;

	snprintf(attrs[attr_count].name, sizeof(attrs[attr_count].name), "%s_ccnt", gator_cpu->pmnc_name);
	attrs[attr_count].type = type;
	++attr_count;

	for (cnt = 0; cnt < gator_cpu->pmnc_counters; ++cnt, ++attr_count) {
		struct gator_attr *const attr = &attrs[attr_count];

		snprintf(attr->name, sizeof(attr->name), "%s_cnt%d", gator_cpu->pmnc_name, cnt);
		attr->type = type;
	}
}

int gator_events_perf_pmu_init(void)
{
	struct perf_event_attr pea;
	struct perf_event *pe;
	const struct gator_cpu *gator_cpu;
	int type;
	int cpu;
	int cnt;
	bool found_cpu = false;

	for (cnt = 0; cnt < CNTMAX; cnt++)
		__attr_init(&attrs[cnt]);
	for (cnt = 0; cnt < UCCNT; cnt++)
		__attr_init(&uc_attrs[cnt]);

	memset(&pea, 0, sizeof(pea));
	pea.size = sizeof(pea);
	pea.config = 0xFF;
	attr_count = 0;
	uc_attr_count = 0;
	for (type = PERF_TYPE_MAX; type < 0x20; ++type) {
		pea.type = type;

		/* A particular PMU may work on some but not all cores, so try on each core */
		pe = NULL;
		for_each_present_cpu(cpu) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
			pe = perf_event_create_kernel_counter(&pea, cpu, 0, dummy_handler);
#else
			pe = perf_event_create_kernel_counter(&pea, cpu, 0, dummy_handler, 0);
#endif
			if (!IS_ERR(pe))
				break;
		}
		/* Assume that valid PMUs are contiguous */
		if (IS_ERR(pe)) {
			pea.config = 0xff00;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
			pe = perf_event_create_kernel_counter(&pea, 0, 0, dummy_handler);
#else
			pe = perf_event_create_kernel_counter(&pea, 0, 0, dummy_handler, 0);
#endif
			if (IS_ERR(pe))
				break;
		}

		if (pe->pmu != NULL && type == pe->pmu->type) {
			if (strcmp("CCI", pe->pmu->name) == 0 || strcmp("CCI_400", pe->pmu->name) == 0 || strcmp("CCI_400-r1", pe->pmu->name) == 0) {
				gator_events_perf_pmu_cci_400_init(type);
			} else if (strcmp("CCI_500", pe->pmu->name) == 0) {
				gator_events_perf_pmu_uncore_init("CCI_500", type, CCI_500, false);
			} else if (strcmp("ccn", pe->pmu->name) == 0) {
				gator_events_perf_pmu_uncore_init("ARM_CCN_5XX", type, CCN_5XX, true);
			} else if ((gator_cpu = gator_find_cpu_by_pmu_name(pe->pmu->name)) != NULL) {
				found_cpu = true;
				gator_events_perf_pmu_cpu_init(gator_cpu, type);
			}
			/* Initialize gator_attrs for dynamic PMUs here */
		}

		perf_event_release_kernel(pe);
	}

	if (!found_cpu) {
		const struct gator_cpu *gator_cpu = gator_find_cpu_by_cpuid(gator_cpuid());

		if (gator_cpu == NULL) {
			gator_cpu = gator_find_cpu_by_cpuid(OTHER);
			if (gator_cpu == NULL) {
				pr_err("gator: Didn't find cpu\n");
				return -1;
			}
		}
		gator_events_perf_pmu_cpu_init(gator_cpu, PERF_TYPE_RAW);
	}

	/* Initialize gator_attrs for non-dynamic PMUs here */

	if (attr_count > CNTMAX) {
		pr_err("gator: Too many perf counters\n");
		return -1;
	}

	if (uc_attr_count > UCCNT) {
		pr_err("gator: Too many perf uncore counters\n");
		return -1;
	}

	return gator_events_install(&gator_events_perf_pmu_interface);
}

#endif
