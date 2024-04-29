// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 Intel Corporation. All rights rsvd. */

#include <linux/sched/task.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include "idxd.h"
#include "perfmon.h"

static ssize_t cpumask_show(struct device *dev, struct device_attribute *attr,
			    char *buf);

static cpumask_t		perfmon_dsa_cpu_mask;
static bool			cpuhp_set_up;
static enum cpuhp_state		cpuhp_slot;

/*
 * perf userspace reads this attribute to determine which cpus to open
 * counters on.  It's connected to perfmon_dsa_cpu_mask, which is
 * maintained by the cpu hotplug handlers.
 */
static DEVICE_ATTR_RO(cpumask);

static struct attribute *perfmon_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group cpumask_attr_group = {
	.attrs = perfmon_cpumask_attrs,
};

/*
 * These attributes specify the bits in the config word that the perf
 * syscall uses to pass the event ids and categories to perfmon.
 */
DEFINE_PERFMON_FORMAT_ATTR(event_category, "config:0-3");
DEFINE_PERFMON_FORMAT_ATTR(event, "config:4-31");

/*
 * These attributes specify the bits in the config1 word that the perf
 * syscall uses to pass filter data to perfmon.
 */
DEFINE_PERFMON_FORMAT_ATTR(filter_wq, "config1:0-31");
DEFINE_PERFMON_FORMAT_ATTR(filter_tc, "config1:32-39");
DEFINE_PERFMON_FORMAT_ATTR(filter_pgsz, "config1:40-43");
DEFINE_PERFMON_FORMAT_ATTR(filter_sz, "config1:44-51");
DEFINE_PERFMON_FORMAT_ATTR(filter_eng, "config1:52-59");

#define PERFMON_FILTERS_START	2
#define PERFMON_FILTERS_MAX	5

static struct attribute *perfmon_format_attrs[] = {
	&format_attr_idxd_event_category.attr,
	&format_attr_idxd_event.attr,
	&format_attr_idxd_filter_wq.attr,
	&format_attr_idxd_filter_tc.attr,
	&format_attr_idxd_filter_pgsz.attr,
	&format_attr_idxd_filter_sz.attr,
	&format_attr_idxd_filter_eng.attr,
	NULL,
};

static struct attribute_group perfmon_format_attr_group = {
	.name = "format",
	.attrs = perfmon_format_attrs,
};

static const struct attribute_group *perfmon_attr_groups[] = {
	&perfmon_format_attr_group,
	&cpumask_attr_group,
	NULL,
};

static ssize_t cpumask_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &perfmon_dsa_cpu_mask);
}

static bool is_idxd_event(struct idxd_pmu *idxd_pmu, struct perf_event *event)
{
	return &idxd_pmu->pmu == event->pmu;
}

static int perfmon_collect_events(struct idxd_pmu *idxd_pmu,
				  struct perf_event *leader,
				  bool do_grp)
{
	struct perf_event *event;
	int n, max_count;

	max_count = idxd_pmu->n_counters;
	n = idxd_pmu->n_events;

	if (n >= max_count)
		return -EINVAL;

	if (is_idxd_event(idxd_pmu, leader)) {
		idxd_pmu->event_list[n] = leader;
		idxd_pmu->event_list[n]->hw.idx = n;
		n++;
	}

	if (!do_grp)
		return n;

	for_each_sibling_event(event, leader) {
		if (!is_idxd_event(idxd_pmu, event) ||
		    event->state <= PERF_EVENT_STATE_OFF)
			continue;

		if (n >= max_count)
			return -EINVAL;

		idxd_pmu->event_list[n] = event;
		idxd_pmu->event_list[n]->hw.idx = n;
		n++;
	}

	return n;
}

static void perfmon_assign_hw_event(struct idxd_pmu *idxd_pmu,
				    struct perf_event *event, int idx)
{
	struct idxd_device *idxd = idxd_pmu->idxd;
	struct hw_perf_event *hwc = &event->hw;

	hwc->idx = idx;
	hwc->config_base = ioread64(CNTRCFG_REG(idxd, idx));
	hwc->event_base = ioread64(CNTRCFG_REG(idxd, idx));
}

static int perfmon_assign_event(struct idxd_pmu *idxd_pmu,
				struct perf_event *event)
{
	int i;

	for (i = 0; i < IDXD_PMU_EVENT_MAX; i++)
		if (!test_and_set_bit(i, idxd_pmu->used_mask))
			return i;

	return -EINVAL;
}

/*
 * Check whether there are enough counters to satisfy that all the
 * events in the group can actually be scheduled at the same time.
 *
 * To do this, create a fake idxd_pmu object so the event collection
 * and assignment functions can be used without affecting the internal
 * state of the real idxd_pmu object.
 */
static int perfmon_validate_group(struct idxd_pmu *pmu,
				  struct perf_event *event)
{
	struct perf_event *leader = event->group_leader;
	struct idxd_pmu *fake_pmu;
	int i, ret = 0, n, idx;

	fake_pmu = kzalloc(sizeof(*fake_pmu), GFP_KERNEL);
	if (!fake_pmu)
		return -ENOMEM;

	fake_pmu->pmu.name = pmu->pmu.name;
	fake_pmu->n_counters = pmu->n_counters;

	n = perfmon_collect_events(fake_pmu, leader, true);
	if (n < 0) {
		ret = n;
		goto out;
	}

	fake_pmu->n_events = n;
	n = perfmon_collect_events(fake_pmu, event, false);
	if (n < 0) {
		ret = n;
		goto out;
	}

	fake_pmu->n_events = n;

	for (i = 0; i < n; i++) {
		event = fake_pmu->event_list[i];

		idx = perfmon_assign_event(fake_pmu, event);
		if (idx < 0) {
			ret = idx;
			goto out;
		}
	}
out:
	kfree(fake_pmu);

	return ret;
}

static int perfmon_pmu_event_init(struct perf_event *event)
{
	struct idxd_device *idxd;
	int ret = 0;

	idxd = event_to_idxd(event);
	event->hw.idx = -1;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* sampling not supported */
	if (event->attr.sample_period)
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	if (event->pmu != &idxd->idxd_pmu->pmu)
		return -EINVAL;

	event->hw.event_base = ioread64(PERFMON_TABLE_OFFSET(idxd));
	event->cpu = idxd->idxd_pmu->cpu;
	event->hw.config = event->attr.config;

	if (event->group_leader != event)
		 /* non-group events have themselves as leader */
		ret = perfmon_validate_group(idxd->idxd_pmu, event);

	return ret;
}

static inline u64 perfmon_pmu_read_counter(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct idxd_device *idxd;
	int cntr = hwc->idx;

	idxd = event_to_idxd(event);

	return ioread64(CNTRDATA_REG(idxd, cntr));
}

static void perfmon_pmu_event_update(struct perf_event *event)
{
	struct idxd_device *idxd = event_to_idxd(event);
	u64 prev_raw_count, new_raw_count, delta, p, n;
	int shift = 64 - idxd->idxd_pmu->counter_width;
	struct hw_perf_event *hwc = &event->hw;

	prev_raw_count = local64_read(&hwc->prev_count);
	do {
		new_raw_count = perfmon_pmu_read_counter(event);
	} while (!local64_try_cmpxchg(&hwc->prev_count,
				      &prev_raw_count, new_raw_count));
	n = (new_raw_count << shift);
	p = (prev_raw_count << shift);

	delta = ((n - p) >> shift);

	local64_add(delta, &event->count);
}

void perfmon_counter_overflow(struct idxd_device *idxd)
{
	int i, n_counters, max_loop = OVERFLOW_SIZE;
	struct perf_event *event;
	unsigned long ovfstatus;

	n_counters = min(idxd->idxd_pmu->n_counters, OVERFLOW_SIZE);

	ovfstatus = ioread32(OVFSTATUS_REG(idxd));

	/*
	 * While updating overflowed counters, other counters behind
	 * them could overflow and be missed in a given pass.
	 * Normally this could happen at most n_counters times, but in
	 * theory a tiny counter width could result in continual
	 * overflows and endless looping.  max_loop provides a
	 * failsafe in that highly unlikely case.
	 */
	while (ovfstatus && max_loop--) {
		/* Figure out which counter(s) overflowed */
		for_each_set_bit(i, &ovfstatus, n_counters) {
			unsigned long ovfstatus_clear = 0;

			/* Update event->count for overflowed counter */
			event = idxd->idxd_pmu->event_list[i];
			perfmon_pmu_event_update(event);
			/* Writing 1 to OVFSTATUS bit clears it */
			set_bit(i, &ovfstatus_clear);
			iowrite32(ovfstatus_clear, OVFSTATUS_REG(idxd));
		}

		ovfstatus = ioread32(OVFSTATUS_REG(idxd));
	}

	/*
	 * Should never happen.  If so, it means a counter(s) looped
	 * around twice while this handler was running.
	 */
	WARN_ON_ONCE(ovfstatus);
}

static inline void perfmon_reset_config(struct idxd_device *idxd)
{
	iowrite32(CONFIG_RESET, PERFRST_REG(idxd));
	iowrite32(0, OVFSTATUS_REG(idxd));
	iowrite32(0, PERFFRZ_REG(idxd));
}

static inline void perfmon_reset_counters(struct idxd_device *idxd)
{
	iowrite32(CNTR_RESET, PERFRST_REG(idxd));
}

static inline void perfmon_reset(struct idxd_device *idxd)
{
	perfmon_reset_config(idxd);
	perfmon_reset_counters(idxd);
}

static void perfmon_pmu_event_start(struct perf_event *event, int mode)
{
	u32 flt_wq, flt_tc, flt_pg_sz, flt_xfer_sz, flt_eng = 0;
	u64 cntr_cfg, cntrdata, event_enc, event_cat = 0;
	struct hw_perf_event *hwc = &event->hw;
	union filter_cfg flt_cfg;
	union event_cfg event_cfg;
	struct idxd_device *idxd;
	int cntr;

	idxd = event_to_idxd(event);

	event->hw.idx = hwc->idx;
	cntr = hwc->idx;

	/* Obtain event category and event value from user space */
	event_cfg.val = event->attr.config;
	flt_cfg.val = event->attr.config1;
	event_cat = event_cfg.event_cat;
	event_enc = event_cfg.event_enc;

	/* Obtain filter configuration from user space */
	flt_wq = flt_cfg.wq;
	flt_tc = flt_cfg.tc;
	flt_pg_sz = flt_cfg.pg_sz;
	flt_xfer_sz = flt_cfg.xfer_sz;
	flt_eng = flt_cfg.eng;

	if (flt_wq && test_bit(FLT_WQ, &idxd->idxd_pmu->supported_filters))
		iowrite32(flt_wq, FLTCFG_REG(idxd, cntr, FLT_WQ));
	if (flt_tc && test_bit(FLT_TC, &idxd->idxd_pmu->supported_filters))
		iowrite32(flt_tc, FLTCFG_REG(idxd, cntr, FLT_TC));
	if (flt_pg_sz && test_bit(FLT_PG_SZ, &idxd->idxd_pmu->supported_filters))
		iowrite32(flt_pg_sz, FLTCFG_REG(idxd, cntr, FLT_PG_SZ));
	if (flt_xfer_sz && test_bit(FLT_XFER_SZ, &idxd->idxd_pmu->supported_filters))
		iowrite32(flt_xfer_sz, FLTCFG_REG(idxd, cntr, FLT_XFER_SZ));
	if (flt_eng && test_bit(FLT_ENG, &idxd->idxd_pmu->supported_filters))
		iowrite32(flt_eng, FLTCFG_REG(idxd, cntr, FLT_ENG));

	/* Read the start value */
	cntrdata = ioread64(CNTRDATA_REG(idxd, cntr));
	local64_set(&event->hw.prev_count, cntrdata);

	/* Set counter to event/category */
	cntr_cfg = event_cat << CNTRCFG_CATEGORY_SHIFT;
	cntr_cfg |= event_enc << CNTRCFG_EVENT_SHIFT;
	/* Set interrupt on overflow and counter enable bits */
	cntr_cfg |= (CNTRCFG_IRQ_OVERFLOW | CNTRCFG_ENABLE);

	iowrite64(cntr_cfg, CNTRCFG_REG(idxd, cntr));
}

static void perfmon_pmu_event_stop(struct perf_event *event, int mode)
{
	struct hw_perf_event *hwc = &event->hw;
	struct idxd_device *idxd;
	int i, cntr = hwc->idx;
	u64 cntr_cfg;

	idxd = event_to_idxd(event);

	/* remove this event from event list */
	for (i = 0; i < idxd->idxd_pmu->n_events; i++) {
		if (event != idxd->idxd_pmu->event_list[i])
			continue;

		for (++i; i < idxd->idxd_pmu->n_events; i++)
			idxd->idxd_pmu->event_list[i - 1] = idxd->idxd_pmu->event_list[i];
		--idxd->idxd_pmu->n_events;
		break;
	}

	cntr_cfg = ioread64(CNTRCFG_REG(idxd, cntr));
	cntr_cfg &= ~CNTRCFG_ENABLE;
	iowrite64(cntr_cfg, CNTRCFG_REG(idxd, cntr));

	if (mode == PERF_EF_UPDATE)
		perfmon_pmu_event_update(event);

	event->hw.idx = -1;
	clear_bit(cntr, idxd->idxd_pmu->used_mask);
}

static void perfmon_pmu_event_del(struct perf_event *event, int mode)
{
	perfmon_pmu_event_stop(event, PERF_EF_UPDATE);
}

static int perfmon_pmu_event_add(struct perf_event *event, int flags)
{
	struct idxd_device *idxd = event_to_idxd(event);
	struct idxd_pmu *idxd_pmu = idxd->idxd_pmu;
	struct hw_perf_event *hwc = &event->hw;
	int idx, n;

	n = perfmon_collect_events(idxd_pmu, event, false);
	if (n < 0)
		return n;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;
	if (!(flags & PERF_EF_START))
		hwc->state |= PERF_HES_ARCH;

	idx = perfmon_assign_event(idxd_pmu, event);
	if (idx < 0)
		return idx;

	perfmon_assign_hw_event(idxd_pmu, event, idx);

	if (flags & PERF_EF_START)
		perfmon_pmu_event_start(event, 0);

	idxd_pmu->n_events = n;

	return 0;
}

static void enable_perfmon_pmu(struct idxd_device *idxd)
{
	iowrite32(COUNTER_UNFREEZE, PERFFRZ_REG(idxd));
}

static void disable_perfmon_pmu(struct idxd_device *idxd)
{
	iowrite32(COUNTER_FREEZE, PERFFRZ_REG(idxd));
}

static void perfmon_pmu_enable(struct pmu *pmu)
{
	struct idxd_device *idxd = pmu_to_idxd(pmu);

	enable_perfmon_pmu(idxd);
}

static void perfmon_pmu_disable(struct pmu *pmu)
{
	struct idxd_device *idxd = pmu_to_idxd(pmu);

	disable_perfmon_pmu(idxd);
}

static void skip_filter(int i)
{
	int j;

	for (j = i; j < PERFMON_FILTERS_MAX; j++)
		perfmon_format_attrs[PERFMON_FILTERS_START + j] =
			perfmon_format_attrs[PERFMON_FILTERS_START + j + 1];
}

static void idxd_pmu_init(struct idxd_pmu *idxd_pmu)
{
	int i;

	for (i = 0 ; i < PERFMON_FILTERS_MAX; i++) {
		if (!test_bit(i, &idxd_pmu->supported_filters))
			skip_filter(i);
	}

	idxd_pmu->pmu.name		= idxd_pmu->name;
	idxd_pmu->pmu.attr_groups	= perfmon_attr_groups;
	idxd_pmu->pmu.task_ctx_nr	= perf_invalid_context;
	idxd_pmu->pmu.event_init	= perfmon_pmu_event_init;
	idxd_pmu->pmu.pmu_enable	= perfmon_pmu_enable,
	idxd_pmu->pmu.pmu_disable	= perfmon_pmu_disable,
	idxd_pmu->pmu.add		= perfmon_pmu_event_add;
	idxd_pmu->pmu.del		= perfmon_pmu_event_del;
	idxd_pmu->pmu.start		= perfmon_pmu_event_start;
	idxd_pmu->pmu.stop		= perfmon_pmu_event_stop;
	idxd_pmu->pmu.read		= perfmon_pmu_event_update;
	idxd_pmu->pmu.capabilities	= PERF_PMU_CAP_NO_EXCLUDE;
	idxd_pmu->pmu.module		= THIS_MODULE;
}

void perfmon_pmu_remove(struct idxd_device *idxd)
{
	if (!idxd->idxd_pmu)
		return;

	cpuhp_state_remove_instance(cpuhp_slot, &idxd->idxd_pmu->cpuhp_node);
	perf_pmu_unregister(&idxd->idxd_pmu->pmu);
	kfree(idxd->idxd_pmu);
	idxd->idxd_pmu = NULL;
}

static int perf_event_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct idxd_pmu *idxd_pmu;

	idxd_pmu = hlist_entry_safe(node, typeof(*idxd_pmu), cpuhp_node);

	/* select the first online CPU as the designated reader */
	if (cpumask_empty(&perfmon_dsa_cpu_mask)) {
		cpumask_set_cpu(cpu, &perfmon_dsa_cpu_mask);
		idxd_pmu->cpu = cpu;
	}

	return 0;
}

static int perf_event_cpu_offline(unsigned int cpu, struct hlist_node *node)
{
	struct idxd_pmu *idxd_pmu;
	unsigned int target;

	idxd_pmu = hlist_entry_safe(node, typeof(*idxd_pmu), cpuhp_node);

	if (!cpumask_test_and_clear_cpu(cpu, &perfmon_dsa_cpu_mask))
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);
	/* migrate events if there is a valid target */
	if (target < nr_cpu_ids) {
		cpumask_set_cpu(target, &perfmon_dsa_cpu_mask);
		perf_pmu_migrate_context(&idxd_pmu->pmu, cpu, target);
	}

	return 0;
}

int perfmon_pmu_init(struct idxd_device *idxd)
{
	union idxd_perfcap perfcap;
	struct idxd_pmu *idxd_pmu;
	int rc = -ENODEV;

	/*
	 * perfmon module initialization failed, nothing to do
	 */
	if (!cpuhp_set_up)
		return -ENODEV;

	/*
	 * If perfmon_offset or num_counters is 0, it means perfmon is
	 * not supported on this hardware.
	 */
	if (idxd->perfmon_offset == 0)
		return -ENODEV;

	idxd_pmu = kzalloc(sizeof(*idxd_pmu), GFP_KERNEL);
	if (!idxd_pmu)
		return -ENOMEM;

	idxd_pmu->idxd = idxd;
	idxd->idxd_pmu = idxd_pmu;

	if (idxd->data->type == IDXD_TYPE_DSA) {
		rc = sprintf(idxd_pmu->name, "dsa%d", idxd->id);
		if (rc < 0)
			goto free;
	} else if (idxd->data->type == IDXD_TYPE_IAX) {
		rc = sprintf(idxd_pmu->name, "iax%d", idxd->id);
		if (rc < 0)
			goto free;
	} else {
		goto free;
	}

	perfmon_reset(idxd);

	perfcap.bits = ioread64(PERFCAP_REG(idxd));

	/*
	 * If total perf counter is 0, stop further registration.
	 * This is necessary in order to support driver running on
	 * guest which does not have pmon support.
	 */
	if (perfcap.num_perf_counter == 0)
		goto free;

	/* A counter width of 0 means it can't count */
	if (perfcap.counter_width == 0)
		goto free;

	/* Overflow interrupt and counter freeze support must be available */
	if (!perfcap.overflow_interrupt || !perfcap.counter_freeze)
		goto free;

	/* Number of event categories cannot be 0 */
	if (perfcap.num_event_category == 0)
		goto free;

	/*
	 * We don't support per-counter capabilities for now.
	 */
	if (perfcap.cap_per_counter)
		goto free;

	idxd_pmu->n_event_categories = perfcap.num_event_category;
	idxd_pmu->supported_event_categories = perfcap.global_event_category;
	idxd_pmu->per_counter_caps_supported = perfcap.cap_per_counter;

	/* check filter capability.  If 0, then filters are not supported */
	idxd_pmu->supported_filters = perfcap.filter;
	if (perfcap.filter)
		idxd_pmu->n_filters = hweight8(perfcap.filter);

	/* Store the total number of counters categories, and counter width */
	idxd_pmu->n_counters = perfcap.num_perf_counter;
	idxd_pmu->counter_width = perfcap.counter_width;

	idxd_pmu_init(idxd_pmu);

	rc = perf_pmu_register(&idxd_pmu->pmu, idxd_pmu->name, -1);
	if (rc)
		goto free;

	rc = cpuhp_state_add_instance(cpuhp_slot, &idxd_pmu->cpuhp_node);
	if (rc) {
		perf_pmu_unregister(&idxd->idxd_pmu->pmu);
		goto free;
	}
out:
	return rc;
free:
	kfree(idxd_pmu);
	idxd->idxd_pmu = NULL;

	goto out;
}

void __init perfmon_init(void)
{
	int rc = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
					 "driver/dma/idxd/perf:online",
					 perf_event_cpu_online,
					 perf_event_cpu_offline);
	if (WARN_ON(rc < 0))
		return;

	cpuhp_slot = rc;
	cpuhp_set_up = true;
}

void __exit perfmon_exit(void)
{
	if (cpuhp_set_up)
		cpuhp_remove_multi_state(cpuhp_slot);
}
