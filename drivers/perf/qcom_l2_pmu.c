/* Copyright (c) 2015-2017 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/percpu.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <asm/barrier.h>
#include <asm/local64.h>
#include <asm/sysreg.h>

#define MAX_L2_CTRS             9

#define L2PMCR_NUM_EV_SHIFT     11
#define L2PMCR_NUM_EV_MASK      0x1F

#define L2PMCR                  0x400
#define L2PMCNTENCLR            0x403
#define L2PMCNTENSET            0x404
#define L2PMINTENCLR            0x405
#define L2PMINTENSET            0x406
#define L2PMOVSCLR              0x407
#define L2PMOVSSET              0x408
#define L2PMCCNTCR              0x409
#define L2PMCCNTR               0x40A
#define L2PMCCNTSR              0x40C
#define L2PMRESR                0x410
#define IA_L2PMXEVCNTCR_BASE    0x420
#define IA_L2PMXEVCNTR_BASE     0x421
#define IA_L2PMXEVFILTER_BASE   0x423
#define IA_L2PMXEVTYPER_BASE    0x424

#define IA_L2_REG_OFFSET        0x10

#define L2PMXEVFILTER_SUFILTER_ALL      0x000E0000
#define L2PMXEVFILTER_ORGFILTER_IDINDEP 0x00000004
#define L2PMXEVFILTER_ORGFILTER_ALL     0x00000003

#define L2EVTYPER_REG_SHIFT     3

#define L2PMRESR_GROUP_BITS     8
#define L2PMRESR_GROUP_MASK     GENMASK(7, 0)

#define L2CYCLE_CTR_BIT         31
#define L2CYCLE_CTR_RAW_CODE    0xFE

#define L2PMCR_RESET_ALL        0x6
#define L2PMCR_COUNTERS_ENABLE  0x1
#define L2PMCR_COUNTERS_DISABLE 0x0

#define L2PMRESR_EN             BIT_ULL(63)

#define L2_EVT_MASK             0x00000FFF
#define L2_EVT_CODE_MASK        0x00000FF0
#define L2_EVT_GRP_MASK         0x0000000F
#define L2_EVT_CODE_SHIFT       4
#define L2_EVT_GRP_SHIFT        0

#define L2_EVT_CODE(event)   (((event) & L2_EVT_CODE_MASK) >> L2_EVT_CODE_SHIFT)
#define L2_EVT_GROUP(event)  (((event) & L2_EVT_GRP_MASK) >> L2_EVT_GRP_SHIFT)

#define L2_EVT_GROUP_MAX        7

#define L2_COUNTER_RELOAD       BIT_ULL(31)
#define L2_CYCLE_COUNTER_RELOAD BIT_ULL(63)

#define L2CPUSRSELR_EL1         sys_reg(3, 3, 15, 0, 6)
#define L2CPUSRDR_EL1           sys_reg(3, 3, 15, 0, 7)

#define reg_idx(reg, i)         (((i) * IA_L2_REG_OFFSET) + reg##_BASE)

/*
 * Events
 */
#define L2_EVENT_CYCLES                    0xfe
#define L2_EVENT_DCACHE_OPS                0x400
#define L2_EVENT_ICACHE_OPS                0x401
#define L2_EVENT_TLBI                      0x402
#define L2_EVENT_BARRIERS                  0x403
#define L2_EVENT_TOTAL_READS               0x405
#define L2_EVENT_TOTAL_WRITES              0x406
#define L2_EVENT_TOTAL_REQUESTS            0x407
#define L2_EVENT_LDREX                     0x420
#define L2_EVENT_STREX                     0x421
#define L2_EVENT_CLREX                     0x422

static DEFINE_RAW_SPINLOCK(l2_access_lock);

/**
 * set_l2_indirect_reg: write value to an L2 register
 * @reg: Address of L2 register.
 * @value: Value to be written to register.
 *
 * Use architecturally required barriers for ordering between system register
 * accesses
 */
static void set_l2_indirect_reg(u64 reg, u64 val)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&l2_access_lock, flags);
	write_sysreg_s(reg, L2CPUSRSELR_EL1);
	isb();
	write_sysreg_s(val, L2CPUSRDR_EL1);
	isb();
	raw_spin_unlock_irqrestore(&l2_access_lock, flags);
}

/**
 * get_l2_indirect_reg: read an L2 register value
 * @reg: Address of L2 register.
 *
 * Use architecturally required barriers for ordering between system register
 * accesses
 */
static u64 get_l2_indirect_reg(u64 reg)
{
	u64 val;
	unsigned long flags;

	raw_spin_lock_irqsave(&l2_access_lock, flags);
	write_sysreg_s(reg, L2CPUSRSELR_EL1);
	isb();
	val = read_sysreg_s(L2CPUSRDR_EL1);
	raw_spin_unlock_irqrestore(&l2_access_lock, flags);

	return val;
}

struct cluster_pmu;

/*
 * Aggregate PMU. Implements the core pmu functions and manages
 * the hardware PMUs.
 */
struct l2cache_pmu {
	struct hlist_node node;
	u32 num_pmus;
	struct pmu pmu;
	int num_counters;
	cpumask_t cpumask;
	struct platform_device *pdev;
	struct cluster_pmu * __percpu *pmu_cluster;
	struct list_head clusters;
};

/*
 * The cache is made up of one or more clusters, each cluster has its own PMU.
 * Each cluster is associated with one or more CPUs.
 * This structure represents one of the hardware PMUs.
 *
 * Events can be envisioned as a 2-dimensional array. Each column represents
 * a group of events. There are 8 groups. Only one entry from each
 * group can be in use at a time.
 *
 * Events are specified as 0xCCG, where CC is 2 hex digits specifying
 * the code (array row) and G specifies the group (column).
 *
 * In addition there is a cycle counter event specified by L2CYCLE_CTR_RAW_CODE
 * which is outside the above scheme.
 */
struct cluster_pmu {
	struct list_head next;
	struct perf_event *events[MAX_L2_CTRS];
	struct l2cache_pmu *l2cache_pmu;
	DECLARE_BITMAP(used_counters, MAX_L2_CTRS);
	DECLARE_BITMAP(used_groups, L2_EVT_GROUP_MAX + 1);
	int irq;
	int cluster_id;
	/* The CPU that is used for collecting events on this cluster */
	int on_cpu;
	/* All the CPUs associated with this cluster */
	cpumask_t cluster_cpus;
	spinlock_t pmu_lock;
};

#define to_l2cache_pmu(p) (container_of(p, struct l2cache_pmu, pmu))

static u32 l2_cycle_ctr_idx;
static u32 l2_counter_present_mask;

static inline u32 idx_to_reg_bit(u32 idx)
{
	if (idx == l2_cycle_ctr_idx)
		return BIT(L2CYCLE_CTR_BIT);

	return BIT(idx);
}

static inline struct cluster_pmu *get_cluster_pmu(
	struct l2cache_pmu *l2cache_pmu, int cpu)
{
	return *per_cpu_ptr(l2cache_pmu->pmu_cluster, cpu);
}

static void cluster_pmu_reset(void)
{
	/* Reset all counters */
	set_l2_indirect_reg(L2PMCR, L2PMCR_RESET_ALL);
	set_l2_indirect_reg(L2PMCNTENCLR, l2_counter_present_mask);
	set_l2_indirect_reg(L2PMINTENCLR, l2_counter_present_mask);
	set_l2_indirect_reg(L2PMOVSCLR, l2_counter_present_mask);
}

static inline void cluster_pmu_enable(void)
{
	set_l2_indirect_reg(L2PMCR, L2PMCR_COUNTERS_ENABLE);
}

static inline void cluster_pmu_disable(void)
{
	set_l2_indirect_reg(L2PMCR, L2PMCR_COUNTERS_DISABLE);
}

static inline void cluster_pmu_counter_set_value(u32 idx, u64 value)
{
	if (idx == l2_cycle_ctr_idx)
		set_l2_indirect_reg(L2PMCCNTR, value);
	else
		set_l2_indirect_reg(reg_idx(IA_L2PMXEVCNTR, idx), value);
}

static inline u64 cluster_pmu_counter_get_value(u32 idx)
{
	u64 value;

	if (idx == l2_cycle_ctr_idx)
		value = get_l2_indirect_reg(L2PMCCNTR);
	else
		value = get_l2_indirect_reg(reg_idx(IA_L2PMXEVCNTR, idx));

	return value;
}

static inline void cluster_pmu_counter_enable(u32 idx)
{
	set_l2_indirect_reg(L2PMCNTENSET, idx_to_reg_bit(idx));
}

static inline void cluster_pmu_counter_disable(u32 idx)
{
	set_l2_indirect_reg(L2PMCNTENCLR, idx_to_reg_bit(idx));
}

static inline void cluster_pmu_counter_enable_interrupt(u32 idx)
{
	set_l2_indirect_reg(L2PMINTENSET, idx_to_reg_bit(idx));
}

static inline void cluster_pmu_counter_disable_interrupt(u32 idx)
{
	set_l2_indirect_reg(L2PMINTENCLR, idx_to_reg_bit(idx));
}

static inline void cluster_pmu_set_evccntcr(u32 val)
{
	set_l2_indirect_reg(L2PMCCNTCR, val);
}

static inline void cluster_pmu_set_evcntcr(u32 ctr, u32 val)
{
	set_l2_indirect_reg(reg_idx(IA_L2PMXEVCNTCR, ctr), val);
}

static inline void cluster_pmu_set_evtyper(u32 ctr, u32 val)
{
	set_l2_indirect_reg(reg_idx(IA_L2PMXEVTYPER, ctr), val);
}

static void cluster_pmu_set_resr(struct cluster_pmu *cluster,
			       u32 event_group, u32 event_cc)
{
	u64 field;
	u64 resr_val;
	u32 shift;
	unsigned long flags;

	shift = L2PMRESR_GROUP_BITS * event_group;
	field = ((u64)(event_cc & L2PMRESR_GROUP_MASK) << shift);

	spin_lock_irqsave(&cluster->pmu_lock, flags);

	resr_val = get_l2_indirect_reg(L2PMRESR);
	resr_val &= ~(L2PMRESR_GROUP_MASK << shift);
	resr_val |= field;
	resr_val |= L2PMRESR_EN;
	set_l2_indirect_reg(L2PMRESR, resr_val);

	spin_unlock_irqrestore(&cluster->pmu_lock, flags);
}

/*
 * Hardware allows filtering of events based on the originating
 * CPU. Turn this off by setting filter bits to allow events from
 * all CPUS, subunits and ID independent events in this cluster.
 */
static inline void cluster_pmu_set_evfilter_sys_mode(u32 ctr)
{
	u32 val =  L2PMXEVFILTER_SUFILTER_ALL |
		   L2PMXEVFILTER_ORGFILTER_IDINDEP |
		   L2PMXEVFILTER_ORGFILTER_ALL;

	set_l2_indirect_reg(reg_idx(IA_L2PMXEVFILTER, ctr), val);
}

static inline u32 cluster_pmu_getreset_ovsr(void)
{
	u32 result = get_l2_indirect_reg(L2PMOVSSET);

	set_l2_indirect_reg(L2PMOVSCLR, result);
	return result;
}

static inline bool cluster_pmu_has_overflowed(u32 ovsr)
{
	return !!(ovsr & l2_counter_present_mask);
}

static inline bool cluster_pmu_counter_has_overflowed(u32 ovsr, u32 idx)
{
	return !!(ovsr & idx_to_reg_bit(idx));
}

static void l2_cache_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev, now;
	u32 idx = hwc->idx;

	do {
		prev = local64_read(&hwc->prev_count);
		now = cluster_pmu_counter_get_value(idx);
	} while (local64_cmpxchg(&hwc->prev_count, prev, now) != prev);

	/*
	 * The cycle counter is 64-bit, but all other counters are
	 * 32-bit, and we must handle 32-bit overflow explicitly.
	 */
	delta = now - prev;
	if (idx != l2_cycle_ctr_idx)
		delta &= 0xffffffff;

	local64_add(delta, &event->count);
}

static void l2_cache_cluster_set_period(struct cluster_pmu *cluster,
				       struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;
	u64 new;

	/*
	 * We limit the max period to half the max counter value so
	 * that even in the case of extreme interrupt latency the
	 * counter will (hopefully) not wrap past its initial value.
	 */
	if (idx == l2_cycle_ctr_idx)
		new = L2_CYCLE_COUNTER_RELOAD;
	else
		new = L2_COUNTER_RELOAD;

	local64_set(&hwc->prev_count, new);
	cluster_pmu_counter_set_value(idx, new);
}

static int l2_cache_get_event_idx(struct cluster_pmu *cluster,
				   struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx;
	int num_ctrs = cluster->l2cache_pmu->num_counters - 1;
	unsigned int group;

	if (hwc->config_base == L2CYCLE_CTR_RAW_CODE) {
		if (test_and_set_bit(l2_cycle_ctr_idx, cluster->used_counters))
			return -EAGAIN;

		return l2_cycle_ctr_idx;
	}

	idx = find_first_zero_bit(cluster->used_counters, num_ctrs);
	if (idx == num_ctrs)
		/* The counters are all in use. */
		return -EAGAIN;

	/*
	 * Check for column exclusion: event column already in use by another
	 * event. This is for events which are not in the same group.
	 * Conflicting events in the same group are detected in event_init.
	 */
	group = L2_EVT_GROUP(hwc->config_base);
	if (test_bit(group, cluster->used_groups))
		return -EAGAIN;

	set_bit(idx, cluster->used_counters);
	set_bit(group, cluster->used_groups);

	return idx;
}

static void l2_cache_clear_event_idx(struct cluster_pmu *cluster,
				      struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	clear_bit(idx, cluster->used_counters);
	if (hwc->config_base != L2CYCLE_CTR_RAW_CODE)
		clear_bit(L2_EVT_GROUP(hwc->config_base), cluster->used_groups);
}

static irqreturn_t l2_cache_handle_irq(int irq_num, void *data)
{
	struct cluster_pmu *cluster = data;
	int num_counters = cluster->l2cache_pmu->num_counters;
	u32 ovsr;
	int idx;

	ovsr = cluster_pmu_getreset_ovsr();
	if (!cluster_pmu_has_overflowed(ovsr))
		return IRQ_NONE;

	for_each_set_bit(idx, cluster->used_counters, num_counters) {
		struct perf_event *event = cluster->events[idx];
		struct hw_perf_event *hwc;

		if (WARN_ON_ONCE(!event))
			continue;

		if (!cluster_pmu_counter_has_overflowed(ovsr, idx))
			continue;

		l2_cache_event_update(event);
		hwc = &event->hw;

		l2_cache_cluster_set_period(cluster, hwc);
	}

	return IRQ_HANDLED;
}

/*
 * Implementation of abstract pmu functionality required by
 * the core perf events code.
 */

static void l2_cache_pmu_enable(struct pmu *pmu)
{
	/*
	 * Although there is only one PMU (per socket) controlling multiple
	 * physical PMUs (per cluster), because we do not support per-task mode
	 * each event is associated with a CPU. Each event has pmu_enable
	 * called on its CPU, so here it is only necessary to enable the
	 * counters for the current CPU.
	 */

	cluster_pmu_enable();
}

static void l2_cache_pmu_disable(struct pmu *pmu)
{
	cluster_pmu_disable();
}

static int l2_cache_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct cluster_pmu *cluster;
	struct perf_event *sibling;
	struct l2cache_pmu *l2cache_pmu;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	l2cache_pmu = to_l2cache_pmu(event->pmu);

	if (hwc->sample_period) {
		dev_dbg_ratelimited(&l2cache_pmu->pdev->dev,
				    "Sampling not supported\n");
		return -EOPNOTSUPP;
	}

	if (event->cpu < 0) {
		dev_dbg_ratelimited(&l2cache_pmu->pdev->dev,
				    "Per-task mode not supported\n");
		return -EOPNOTSUPP;
	}

	/* We cannot filter accurately so we just don't allow it. */
	if (event->attr.exclude_user || event->attr.exclude_kernel ||
	    event->attr.exclude_hv || event->attr.exclude_idle) {
		dev_dbg_ratelimited(&l2cache_pmu->pdev->dev,
				    "Can't exclude execution levels\n");
		return -EOPNOTSUPP;
	}

	if (((L2_EVT_GROUP(event->attr.config) > L2_EVT_GROUP_MAX) ||
	     ((event->attr.config & ~L2_EVT_MASK) != 0)) &&
	    (event->attr.config != L2CYCLE_CTR_RAW_CODE)) {
		dev_dbg_ratelimited(&l2cache_pmu->pdev->dev,
				    "Invalid config %llx\n",
				    event->attr.config);
		return -EINVAL;
	}

	/* Don't allow groups with mixed PMUs, except for s/w events */
	if (event->group_leader->pmu != event->pmu &&
	    !is_software_event(event->group_leader)) {
		dev_dbg_ratelimited(&l2cache_pmu->pdev->dev,
			 "Can't create mixed PMU group\n");
		return -EINVAL;
	}

	list_for_each_entry(sibling, &event->group_leader->sibling_list,
			    sibling_list)
		if (sibling->pmu != event->pmu &&
		    !is_software_event(sibling)) {
			dev_dbg_ratelimited(&l2cache_pmu->pdev->dev,
				 "Can't create mixed PMU group\n");
			return -EINVAL;
		}

	cluster = get_cluster_pmu(l2cache_pmu, event->cpu);
	if (!cluster) {
		/* CPU has not been initialised */
		dev_dbg_ratelimited(&l2cache_pmu->pdev->dev,
			"CPU%d not associated with L2 cluster\n", event->cpu);
		return -EINVAL;
	}

	/* Ensure all events in a group are on the same cpu */
	if ((event->group_leader != event) &&
	    (cluster->on_cpu != event->group_leader->cpu)) {
		dev_dbg_ratelimited(&l2cache_pmu->pdev->dev,
			 "Can't create group on CPUs %d and %d",
			 event->cpu, event->group_leader->cpu);
		return -EINVAL;
	}

	if ((event != event->group_leader) &&
	    !is_software_event(event->group_leader) &&
	    (L2_EVT_GROUP(event->group_leader->attr.config) ==
	     L2_EVT_GROUP(event->attr.config))) {
		dev_dbg_ratelimited(&l2cache_pmu->pdev->dev,
			 "Column exclusion: conflicting events %llx %llx\n",
		       event->group_leader->attr.config,
		       event->attr.config);
		return -EINVAL;
	}

	list_for_each_entry(sibling, &event->group_leader->sibling_list,
			    sibling_list) {
		if ((sibling != event) &&
		    !is_software_event(sibling) &&
		    (L2_EVT_GROUP(sibling->attr.config) ==
		     L2_EVT_GROUP(event->attr.config))) {
			dev_dbg_ratelimited(&l2cache_pmu->pdev->dev,
			     "Column exclusion: conflicting events %llx %llx\n",
					    sibling->attr.config,
					    event->attr.config);
			return -EINVAL;
		}
	}

	hwc->idx = -1;
	hwc->config_base = event->attr.config;

	/*
	 * Ensure all events are on the same cpu so all events are in the
	 * same cpu context, to avoid races on pmu_enable etc.
	 */
	event->cpu = cluster->on_cpu;

	return 0;
}

static void l2_cache_event_start(struct perf_event *event, int flags)
{
	struct cluster_pmu *cluster;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	u32 config;
	u32 event_cc, event_group;

	hwc->state = 0;

	cluster = get_cluster_pmu(to_l2cache_pmu(event->pmu), event->cpu);

	l2_cache_cluster_set_period(cluster, hwc);

	if (hwc->config_base == L2CYCLE_CTR_RAW_CODE) {
		cluster_pmu_set_evccntcr(0);
	} else {
		config = hwc->config_base;
		event_cc    = L2_EVT_CODE(config);
		event_group = L2_EVT_GROUP(config);

		cluster_pmu_set_evcntcr(idx, 0);
		cluster_pmu_set_evtyper(idx, event_group);
		cluster_pmu_set_resr(cluster, event_group, event_cc);
		cluster_pmu_set_evfilter_sys_mode(idx);
	}

	cluster_pmu_counter_enable_interrupt(idx);
	cluster_pmu_counter_enable(idx);
}

static void l2_cache_event_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (hwc->state & PERF_HES_STOPPED)
		return;

	cluster_pmu_counter_disable_interrupt(idx);
	cluster_pmu_counter_disable(idx);

	if (flags & PERF_EF_UPDATE)
		l2_cache_event_update(event);
	hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static int l2_cache_event_add(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx;
	int err = 0;
	struct cluster_pmu *cluster;

	cluster = get_cluster_pmu(to_l2cache_pmu(event->pmu), event->cpu);

	idx = l2_cache_get_event_idx(cluster, event);
	if (idx < 0)
		return idx;

	hwc->idx = idx;
	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
	cluster->events[idx] = event;
	local64_set(&hwc->prev_count, 0);

	if (flags & PERF_EF_START)
		l2_cache_event_start(event, flags);

	/* Propagate changes to the userspace mapping. */
	perf_event_update_userpage(event);

	return err;
}

static void l2_cache_event_del(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct cluster_pmu *cluster;
	int idx = hwc->idx;

	cluster = get_cluster_pmu(to_l2cache_pmu(event->pmu), event->cpu);

	l2_cache_event_stop(event, flags | PERF_EF_UPDATE);
	cluster->events[idx] = NULL;
	l2_cache_clear_event_idx(cluster, event);

	perf_event_update_userpage(event);
}

static void l2_cache_event_read(struct perf_event *event)
{
	l2_cache_event_update(event);
}

static ssize_t l2_cache_pmu_cpumask_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct l2cache_pmu *l2cache_pmu = to_l2cache_pmu(dev_get_drvdata(dev));

	return cpumap_print_to_pagebuf(true, buf, &l2cache_pmu->cpumask);
}

static struct device_attribute l2_cache_pmu_cpumask_attr =
		__ATTR(cpumask, S_IRUGO, l2_cache_pmu_cpumask_show, NULL);

static struct attribute *l2_cache_pmu_cpumask_attrs[] = {
	&l2_cache_pmu_cpumask_attr.attr,
	NULL,
};

static struct attribute_group l2_cache_pmu_cpumask_group = {
	.attrs = l2_cache_pmu_cpumask_attrs,
};

/* CCG format for perf RAW codes. */
PMU_FORMAT_ATTR(l2_code,   "config:4-11");
PMU_FORMAT_ATTR(l2_group,  "config:0-3");
PMU_FORMAT_ATTR(event,     "config:0-11");

static struct attribute *l2_cache_pmu_formats[] = {
	&format_attr_l2_code.attr,
	&format_attr_l2_group.attr,
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group l2_cache_pmu_format_group = {
	.name = "format",
	.attrs = l2_cache_pmu_formats,
};

static ssize_t l2cache_pmu_event_show(struct device *dev,
				      struct device_attribute *attr, char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sprintf(page, "event=0x%02llx\n", pmu_attr->id);
}

#define L2CACHE_EVENT_ATTR(_name, _id)					     \
	(&((struct perf_pmu_events_attr[]) {				     \
		{ .attr = __ATTR(_name, 0444, l2cache_pmu_event_show, NULL), \
		  .id = _id, }						     \
	})[0].attr.attr)

static struct attribute *l2_cache_pmu_events[] = {
	L2CACHE_EVENT_ATTR(cycles, L2_EVENT_CYCLES),
	L2CACHE_EVENT_ATTR(dcache-ops, L2_EVENT_DCACHE_OPS),
	L2CACHE_EVENT_ATTR(icache-ops, L2_EVENT_ICACHE_OPS),
	L2CACHE_EVENT_ATTR(tlbi, L2_EVENT_TLBI),
	L2CACHE_EVENT_ATTR(barriers, L2_EVENT_BARRIERS),
	L2CACHE_EVENT_ATTR(total-reads, L2_EVENT_TOTAL_READS),
	L2CACHE_EVENT_ATTR(total-writes, L2_EVENT_TOTAL_WRITES),
	L2CACHE_EVENT_ATTR(total-requests, L2_EVENT_TOTAL_REQUESTS),
	L2CACHE_EVENT_ATTR(ldrex, L2_EVENT_LDREX),
	L2CACHE_EVENT_ATTR(strex, L2_EVENT_STREX),
	L2CACHE_EVENT_ATTR(clrex, L2_EVENT_CLREX),
	NULL
};

static struct attribute_group l2_cache_pmu_events_group = {
	.name = "events",
	.attrs = l2_cache_pmu_events,
};

static const struct attribute_group *l2_cache_pmu_attr_grps[] = {
	&l2_cache_pmu_format_group,
	&l2_cache_pmu_cpumask_group,
	&l2_cache_pmu_events_group,
	NULL,
};

/*
 * Generic device handlers
 */

static const struct acpi_device_id l2_cache_pmu_acpi_match[] = {
	{ "QCOM8130", },
	{ }
};

static int get_num_counters(void)
{
	int val;

	val = get_l2_indirect_reg(L2PMCR);

	/*
	 * Read number of counters from L2PMCR and add 1
	 * for the cycle counter.
	 */
	return ((val >> L2PMCR_NUM_EV_SHIFT) & L2PMCR_NUM_EV_MASK) + 1;
}

static struct cluster_pmu *l2_cache_associate_cpu_with_cluster(
	struct l2cache_pmu *l2cache_pmu, int cpu)
{
	u64 mpidr;
	int cpu_cluster_id;
	struct cluster_pmu *cluster = NULL;

	/*
	 * This assumes that the cluster_id is in MPIDR[aff1] for
	 * single-threaded cores, and MPIDR[aff2] for multi-threaded
	 * cores. This logic will have to be updated if this changes.
	 */
	mpidr = read_cpuid_mpidr();
	if (mpidr & MPIDR_MT_BITMASK)
		cpu_cluster_id = MPIDR_AFFINITY_LEVEL(mpidr, 2);
	else
		cpu_cluster_id = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	list_for_each_entry(cluster, &l2cache_pmu->clusters, next) {
		if (cluster->cluster_id != cpu_cluster_id)
			continue;

		dev_info(&l2cache_pmu->pdev->dev,
			 "CPU%d associated with cluster %d\n", cpu,
			 cluster->cluster_id);
		cpumask_set_cpu(cpu, &cluster->cluster_cpus);
		*per_cpu_ptr(l2cache_pmu->pmu_cluster, cpu) = cluster;
		break;
	}

	return cluster;
}

static int l2cache_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct cluster_pmu *cluster;
	struct l2cache_pmu *l2cache_pmu;

	l2cache_pmu = hlist_entry_safe(node, struct l2cache_pmu, node);
	cluster = get_cluster_pmu(l2cache_pmu, cpu);
	if (!cluster) {
		/* First time this CPU has come online */
		cluster = l2_cache_associate_cpu_with_cluster(l2cache_pmu, cpu);
		if (!cluster) {
			/* Only if broken firmware doesn't list every cluster */
			WARN_ONCE(1, "No L2 cache cluster for CPU%d\n", cpu);
			return 0;
		}
	}

	/* If another CPU is managing this cluster, we're done */
	if (cluster->on_cpu != -1)
		return 0;

	/*
	 * All CPUs on this cluster were down, use this one.
	 * Reset to put it into sane state.
	 */
	cluster->on_cpu = cpu;
	cpumask_set_cpu(cpu, &l2cache_pmu->cpumask);
	cluster_pmu_reset();

	WARN_ON(irq_set_affinity(cluster->irq, cpumask_of(cpu)));
	enable_irq(cluster->irq);

	return 0;
}

static int l2cache_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct cluster_pmu *cluster;
	struct l2cache_pmu *l2cache_pmu;
	cpumask_t cluster_online_cpus;
	unsigned int target;

	l2cache_pmu = hlist_entry_safe(node, struct l2cache_pmu, node);
	cluster = get_cluster_pmu(l2cache_pmu, cpu);
	if (!cluster)
		return 0;

	/* If this CPU is not managing the cluster, we're done */
	if (cluster->on_cpu != cpu)
		return 0;

	/* Give up ownership of cluster */
	cpumask_clear_cpu(cpu, &l2cache_pmu->cpumask);
	cluster->on_cpu = -1;

	/* Any other CPU for this cluster which is still online */
	cpumask_and(&cluster_online_cpus, &cluster->cluster_cpus,
		    cpu_online_mask);
	target = cpumask_any_but(&cluster_online_cpus, cpu);
	if (target >= nr_cpu_ids) {
		disable_irq(cluster->irq);
		return 0;
	}

	perf_pmu_migrate_context(&l2cache_pmu->pmu, cpu, target);
	cluster->on_cpu = target;
	cpumask_set_cpu(target, &l2cache_pmu->cpumask);
	WARN_ON(irq_set_affinity(cluster->irq, cpumask_of(target)));

	return 0;
}

static int l2_cache_pmu_probe_cluster(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev->parent);
	struct platform_device *sdev = to_platform_device(dev);
	struct l2cache_pmu *l2cache_pmu = data;
	struct cluster_pmu *cluster;
	struct acpi_device *device;
	unsigned long fw_cluster_id;
	int err;
	int irq;

	if (acpi_bus_get_device(ACPI_HANDLE(dev), &device))
		return -ENODEV;

	if (kstrtoul(device->pnp.unique_id, 10, &fw_cluster_id) < 0) {
		dev_err(&pdev->dev, "unable to read ACPI uid\n");
		return -ENODEV;
	}

	cluster = devm_kzalloc(&pdev->dev, sizeof(*cluster), GFP_KERNEL);
	if (!cluster)
		return -ENOMEM;

	INIT_LIST_HEAD(&cluster->next);
	list_add(&cluster->next, &l2cache_pmu->clusters);
	cluster->cluster_id = fw_cluster_id;

	irq = platform_get_irq(sdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev,
			"Failed to get valid irq for cluster %ld\n",
			fw_cluster_id);
		return irq;
	}
	irq_set_status_flags(irq, IRQ_NOAUTOEN);
	cluster->irq = irq;

	cluster->l2cache_pmu = l2cache_pmu;
	cluster->on_cpu = -1;

	err = devm_request_irq(&pdev->dev, irq, l2_cache_handle_irq,
			       IRQF_NOBALANCING | IRQF_NO_THREAD,
			       "l2-cache-pmu", cluster);
	if (err) {
		dev_err(&pdev->dev,
			"Unable to request IRQ%d for L2 PMU counters\n", irq);
		return err;
	}

	dev_info(&pdev->dev,
		"Registered L2 cache PMU cluster %ld\n", fw_cluster_id);

	spin_lock_init(&cluster->pmu_lock);

	l2cache_pmu->num_pmus++;

	return 0;
}

static int l2_cache_pmu_probe(struct platform_device *pdev)
{
	int err;
	struct l2cache_pmu *l2cache_pmu;

	l2cache_pmu =
		devm_kzalloc(&pdev->dev, sizeof(*l2cache_pmu), GFP_KERNEL);
	if (!l2cache_pmu)
		return -ENOMEM;

	INIT_LIST_HEAD(&l2cache_pmu->clusters);

	platform_set_drvdata(pdev, l2cache_pmu);
	l2cache_pmu->pmu = (struct pmu) {
		/* suffix is instance id for future use with multiple sockets */
		.name		= "l2cache_0",
		.task_ctx_nr    = perf_invalid_context,
		.pmu_enable	= l2_cache_pmu_enable,
		.pmu_disable	= l2_cache_pmu_disable,
		.event_init	= l2_cache_event_init,
		.add		= l2_cache_event_add,
		.del		= l2_cache_event_del,
		.start		= l2_cache_event_start,
		.stop		= l2_cache_event_stop,
		.read		= l2_cache_event_read,
		.attr_groups	= l2_cache_pmu_attr_grps,
	};

	l2cache_pmu->num_counters = get_num_counters();
	l2cache_pmu->pdev = pdev;
	l2cache_pmu->pmu_cluster = devm_alloc_percpu(&pdev->dev,
						     struct cluster_pmu *);
	if (!l2cache_pmu->pmu_cluster)
		return -ENOMEM;

	l2_cycle_ctr_idx = l2cache_pmu->num_counters - 1;
	l2_counter_present_mask = GENMASK(l2cache_pmu->num_counters - 2, 0) |
		BIT(L2CYCLE_CTR_BIT);

	cpumask_clear(&l2cache_pmu->cpumask);

	/* Read cluster info and initialize each cluster */
	err = device_for_each_child(&pdev->dev, l2cache_pmu,
				    l2_cache_pmu_probe_cluster);
	if (err)
		return err;

	if (l2cache_pmu->num_pmus == 0) {
		dev_err(&pdev->dev, "No hardware L2 cache PMUs found\n");
		return -ENODEV;
	}

	err = cpuhp_state_add_instance(CPUHP_AP_PERF_ARM_QCOM_L2_ONLINE,
				       &l2cache_pmu->node);
	if (err) {
		dev_err(&pdev->dev, "Error %d registering hotplug", err);
		return err;
	}

	err = perf_pmu_register(&l2cache_pmu->pmu, l2cache_pmu->pmu.name, -1);
	if (err) {
		dev_err(&pdev->dev, "Error %d registering L2 cache PMU\n", err);
		goto out_unregister;
	}

	dev_info(&pdev->dev, "Registered L2 cache PMU using %d HW PMUs\n",
		 l2cache_pmu->num_pmus);

	return err;

out_unregister:
	cpuhp_state_remove_instance(CPUHP_AP_PERF_ARM_QCOM_L2_ONLINE,
				    &l2cache_pmu->node);
	return err;
}

static int l2_cache_pmu_remove(struct platform_device *pdev)
{
	struct l2cache_pmu *l2cache_pmu =
		to_l2cache_pmu(platform_get_drvdata(pdev));

	perf_pmu_unregister(&l2cache_pmu->pmu);
	cpuhp_state_remove_instance(CPUHP_AP_PERF_ARM_QCOM_L2_ONLINE,
				    &l2cache_pmu->node);
	return 0;
}

static struct platform_driver l2_cache_pmu_driver = {
	.driver = {
		.name = "qcom-l2cache-pmu",
		.acpi_match_table = ACPI_PTR(l2_cache_pmu_acpi_match),
	},
	.probe = l2_cache_pmu_probe,
	.remove = l2_cache_pmu_remove,
};

static int __init register_l2_cache_pmu_driver(void)
{
	int err;

	err = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_QCOM_L2_ONLINE,
				      "AP_PERF_ARM_QCOM_L2_ONLINE",
				      l2cache_pmu_online_cpu,
				      l2cache_pmu_offline_cpu);
	if (err)
		return err;

	return platform_driver_register(&l2_cache_pmu_driver);
}
device_initcall(register_l2_cache_pmu_driver);
