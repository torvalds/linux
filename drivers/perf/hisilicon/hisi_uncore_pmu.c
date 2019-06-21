// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon SoC Hardware event counters support
 *
 * Copyright (C) 2017 Hisilicon Limited
 * Author: Anurup M <anurup.m@huawei.com>
 *         Shaokun Zhang <zhangshaokun@hisilicon.com>
 *
 * This code is based on the uncore PMUs like arm-cci and arm-ccn.
 */
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>

#include <asm/local64.h>

#include "hisi_uncore_pmu.h"

#define HISI_GET_EVENTID(ev) (ev->hw.config_base & 0xff)
#define HISI_MAX_PERIOD(nr) (BIT_ULL(nr) - 1)

/*
 * PMU format attributes
 */
ssize_t hisi_format_sysfs_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);

	return sprintf(buf, "%s\n", (char *)eattr->var);
}

/*
 * PMU event attributes
 */
ssize_t hisi_event_sysfs_show(struct device *dev,
			      struct device_attribute *attr, char *page)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);

	return sprintf(page, "config=0x%lx\n", (unsigned long)eattr->var);
}

/*
 * sysfs cpumask attributes. For uncore PMU, we only have a single CPU to show
 */
ssize_t hisi_cpumask_sysfs_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(dev_get_drvdata(dev));

	return sprintf(buf, "%d\n", hisi_pmu->on_cpu);
}

static bool hisi_validate_event_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(event->pmu);
	/* Include count for the event */
	int counters = 1;

	if (!is_software_event(leader)) {
		/*
		 * We must NOT create groups containing mixed PMUs, although
		 * software events are acceptable
		 */
		if (leader->pmu != event->pmu)
			return false;

		/* Increment counter for the leader */
		if (leader != event)
			counters++;
	}

	for_each_sibling_event(sibling, event->group_leader) {
		if (is_software_event(sibling))
			continue;
		if (sibling->pmu != event->pmu)
			return false;
		/* Increment counter for each sibling */
		counters++;
	}

	/* The group can not count events more than the counters in the HW */
	return counters <= hisi_pmu->num_counters;
}

int hisi_uncore_pmu_counter_valid(struct hisi_pmu *hisi_pmu, int idx)
{
	return idx >= 0 && idx < hisi_pmu->num_counters;
}

int hisi_uncore_pmu_get_event_idx(struct perf_event *event)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(event->pmu);
	unsigned long *used_mask = hisi_pmu->pmu_events.used_mask;
	u32 num_counters = hisi_pmu->num_counters;
	int idx;

	idx = find_first_zero_bit(used_mask, num_counters);
	if (idx == num_counters)
		return -EAGAIN;

	set_bit(idx, used_mask);

	return idx;
}

static void hisi_uncore_pmu_clear_event_idx(struct hisi_pmu *hisi_pmu, int idx)
{
	if (!hisi_uncore_pmu_counter_valid(hisi_pmu, idx)) {
		dev_err(hisi_pmu->dev, "Unsupported event index:%d!\n", idx);
		return;
	}

	clear_bit(idx, hisi_pmu->pmu_events.used_mask);
}

int hisi_uncore_pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hisi_pmu *hisi_pmu;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/*
	 * We do not support sampling as the counters are all
	 * shared by all CPU cores in a CPU die(SCCL). Also we
	 * do not support attach to a task(per-process mode)
	 */
	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;

	/*
	 *  The uncore counters not specific to any CPU, so cannot
	 *  support per-task
	 */
	if (event->cpu < 0)
		return -EINVAL;

	/*
	 * Validate if the events in group does not exceed the
	 * available counters in hardware.
	 */
	if (!hisi_validate_event_group(event))
		return -EINVAL;

	hisi_pmu = to_hisi_pmu(event->pmu);
	if (event->attr.config > hisi_pmu->check_event)
		return -EINVAL;

	if (hisi_pmu->on_cpu == -1)
		return -EINVAL;
	/*
	 * We don't assign an index until we actually place the event onto
	 * hardware. Use -1 to signify that we haven't decided where to put it
	 * yet.
	 */
	hwc->idx		= -1;
	hwc->config_base	= event->attr.config;

	/* Enforce to use the same CPU for all events in this PMU */
	event->cpu = hisi_pmu->on_cpu;

	return 0;
}

/*
 * Set the counter to count the event that we're interested in,
 * and enable interrupt and counter.
 */
static void hisi_uncore_pmu_enable_event(struct perf_event *event)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hisi_pmu->ops->write_evtype(hisi_pmu, hwc->idx,
				    HISI_GET_EVENTID(event));

	hisi_pmu->ops->enable_counter_int(hisi_pmu, hwc);
	hisi_pmu->ops->enable_counter(hisi_pmu, hwc);
}

/*
 * Disable counter and interrupt.
 */
static void hisi_uncore_pmu_disable_event(struct perf_event *event)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hisi_pmu->ops->disable_counter(hisi_pmu, hwc);
	hisi_pmu->ops->disable_counter_int(hisi_pmu, hwc);
}

void hisi_uncore_pmu_set_event_period(struct perf_event *event)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	/*
	 * The HiSilicon PMU counters support 32 bits or 48 bits, depending on
	 * the PMU. We reduce it to 2^(counter_bits - 1) to account for the
	 * extreme interrupt latency. So we could hopefully handle the overflow
	 * interrupt before another 2^(counter_bits - 1) events occur and the
	 * counter overtakes its previous value.
	 */
	u64 val = BIT_ULL(hisi_pmu->counter_bits - 1);

	local64_set(&hwc->prev_count, val);
	/* Write start value to the hardware event counter */
	hisi_pmu->ops->write_counter(hisi_pmu, hwc, val);
}

void hisi_uncore_pmu_event_update(struct perf_event *event)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev_raw_count, new_raw_count;

	do {
		/* Read the count from the counter register */
		new_raw_count = hisi_pmu->ops->read_counter(hisi_pmu, hwc);
		prev_raw_count = local64_read(&hwc->prev_count);
	} while (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
				 new_raw_count) != prev_raw_count);
	/*
	 * compute the delta
	 */
	delta = (new_raw_count - prev_raw_count) &
		HISI_MAX_PERIOD(hisi_pmu->counter_bits);
	local64_add(delta, &event->count);
}

void hisi_uncore_pmu_start(struct perf_event *event, int flags)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));
	hwc->state = 0;
	hisi_uncore_pmu_set_event_period(event);

	if (flags & PERF_EF_RELOAD) {
		u64 prev_raw_count =  local64_read(&hwc->prev_count);

		hisi_pmu->ops->write_counter(hisi_pmu, hwc, prev_raw_count);
	}

	hisi_uncore_pmu_enable_event(event);
	perf_event_update_userpage(event);
}

void hisi_uncore_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	hisi_uncore_pmu_disable_event(event);
	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	if (hwc->state & PERF_HES_UPTODATE)
		return;

	/* Read hardware counter and update the perf counter statistics */
	hisi_uncore_pmu_event_update(event);
	hwc->state |= PERF_HES_UPTODATE;
}

int hisi_uncore_pmu_add(struct perf_event *event, int flags)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	/* Get an available counter index for counting */
	idx = hisi_pmu->ops->get_event_idx(event);
	if (idx < 0)
		return idx;

	event->hw.idx = idx;
	hisi_pmu->pmu_events.hw_events[idx] = event;

	if (flags & PERF_EF_START)
		hisi_uncore_pmu_start(event, PERF_EF_RELOAD);

	return 0;
}

void hisi_uncore_pmu_del(struct perf_event *event, int flags)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hisi_uncore_pmu_stop(event, PERF_EF_UPDATE);
	hisi_uncore_pmu_clear_event_idx(hisi_pmu, hwc->idx);
	perf_event_update_userpage(event);
	hisi_pmu->pmu_events.hw_events[hwc->idx] = NULL;
}

void hisi_uncore_pmu_read(struct perf_event *event)
{
	/* Read hardware counter and update the perf counter statistics */
	hisi_uncore_pmu_event_update(event);
}

void hisi_uncore_pmu_enable(struct pmu *pmu)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(pmu);
	int enabled = bitmap_weight(hisi_pmu->pmu_events.used_mask,
				    hisi_pmu->num_counters);

	if (!enabled)
		return;

	hisi_pmu->ops->start_counters(hisi_pmu);
}

void hisi_uncore_pmu_disable(struct pmu *pmu)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(pmu);

	hisi_pmu->ops->stop_counters(hisi_pmu);
}

/*
 * Read Super CPU cluster and CPU cluster ID from MPIDR_EL1.
 * If multi-threading is supported, CCL_ID is the low 3-bits in MPIDR[Aff2]
 * and SCCL_ID is the upper 5-bits of Aff2 field; if not, SCCL_ID
 * is in MPIDR[Aff2] and CCL_ID is in MPIDR[Aff1].
 */
static void hisi_read_sccl_and_ccl_id(int *sccl_id, int *ccl_id)
{
	u64 mpidr = read_cpuid_mpidr();

	if (mpidr & MPIDR_MT_BITMASK) {
		int aff2 = MPIDR_AFFINITY_LEVEL(mpidr, 2);

		if (sccl_id)
			*sccl_id = aff2 >> 3;
		if (ccl_id)
			*ccl_id = aff2 & 0x7;
	} else {
		if (sccl_id)
			*sccl_id = MPIDR_AFFINITY_LEVEL(mpidr, 2);
		if (ccl_id)
			*ccl_id = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	}
}

/*
 * Check whether the CPU is associated with this uncore PMU
 */
static bool hisi_pmu_cpu_is_associated_pmu(struct hisi_pmu *hisi_pmu)
{
	int sccl_id, ccl_id;

	if (hisi_pmu->ccl_id == -1) {
		/* If CCL_ID is -1, the PMU only shares the same SCCL */
		hisi_read_sccl_and_ccl_id(&sccl_id, NULL);

		return sccl_id == hisi_pmu->sccl_id;
	}

	hisi_read_sccl_and_ccl_id(&sccl_id, &ccl_id);

	return sccl_id == hisi_pmu->sccl_id && ccl_id == hisi_pmu->ccl_id;
}

int hisi_uncore_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hisi_pmu *hisi_pmu = hlist_entry_safe(node, struct hisi_pmu,
						     node);

	if (!hisi_pmu_cpu_is_associated_pmu(hisi_pmu))
		return 0;

	cpumask_set_cpu(cpu, &hisi_pmu->associated_cpus);

	/* If another CPU is already managing this PMU, simply return. */
	if (hisi_pmu->on_cpu != -1)
		return 0;

	/* Use this CPU in cpumask for event counting */
	hisi_pmu->on_cpu = cpu;

	/* Overflow interrupt also should use the same CPU */
	WARN_ON(irq_set_affinity(hisi_pmu->irq, cpumask_of(cpu)));

	return 0;
}

int hisi_uncore_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hisi_pmu *hisi_pmu = hlist_entry_safe(node, struct hisi_pmu,
						     node);
	cpumask_t pmu_online_cpus;
	unsigned int target;

	if (!cpumask_test_and_clear_cpu(cpu, &hisi_pmu->associated_cpus))
		return 0;

	/* Nothing to do if this CPU doesn't own the PMU */
	if (hisi_pmu->on_cpu != cpu)
		return 0;

	/* Give up ownership of the PMU */
	hisi_pmu->on_cpu = -1;

	/* Choose a new CPU to migrate ownership of the PMU to */
	cpumask_and(&pmu_online_cpus, &hisi_pmu->associated_cpus,
		    cpu_online_mask);
	target = cpumask_any_but(&pmu_online_cpus, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&hisi_pmu->pmu, cpu, target);
	/* Use this CPU for event counting */
	hisi_pmu->on_cpu = target;
	WARN_ON(irq_set_affinity(hisi_pmu->irq, cpumask_of(target)));

	return 0;
}
