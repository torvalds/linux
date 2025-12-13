// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon SoC Hardware event counters support
 *
 * Copyright (C) 2017 HiSilicon Limited
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
#include <linux/property.h>

#include <asm/cputype.h>
#include <asm/local64.h>

#include "hisi_uncore_pmu.h"

#define HISI_MAX_PERIOD(nr) (GENMASK_ULL((nr) - 1, 0))

/*
 * PMU event attributes
 */
ssize_t hisi_event_sysfs_show(struct device *dev,
			      struct device_attribute *attr, char *page)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);

	return sysfs_emit(page, "config=0x%lx\n", (unsigned long)eattr->var);
}
EXPORT_SYMBOL_NS_GPL(hisi_event_sysfs_show, "HISI_PMU");

/*
 * sysfs cpumask attributes. For uncore PMU, we only have a single CPU to show
 */
ssize_t hisi_cpumask_sysfs_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(dev_get_drvdata(dev));

	return sysfs_emit(buf, "%d\n", hisi_pmu->on_cpu);
}
EXPORT_SYMBOL_NS_GPL(hisi_cpumask_sysfs_show, "HISI_PMU");

static DEVICE_ATTR(cpumask, 0444, hisi_cpumask_sysfs_show, NULL);

static ssize_t hisi_associated_cpus_sysfs_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(dev_get_drvdata(dev));

	return cpumap_print_to_pagebuf(true, buf, &hisi_pmu->associated_cpus);
}
static DEVICE_ATTR(associated_cpus, 0444, hisi_associated_cpus_sysfs_show, NULL);

static struct attribute *hisi_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	&dev_attr_associated_cpus.attr,
	NULL
};

const struct attribute_group hisi_pmu_cpumask_attr_group = {
	.attrs = hisi_pmu_cpumask_attrs,
};
EXPORT_SYMBOL_NS_GPL(hisi_pmu_cpumask_attr_group, "HISI_PMU");

ssize_t hisi_uncore_pmu_identifier_attr_show(struct device *dev,
					     struct device_attribute *attr,
					     char *page)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(dev_get_drvdata(dev));

	return sysfs_emit(page, "0x%08x\n", hisi_pmu->identifier);
}
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_identifier_attr_show, "HISI_PMU");

static struct device_attribute hisi_pmu_identifier_attr =
	__ATTR(identifier, 0444, hisi_uncore_pmu_identifier_attr_show, NULL);

static struct attribute *hisi_pmu_identifier_attrs[] = {
	&hisi_pmu_identifier_attr.attr,
	NULL
};

const struct attribute_group hisi_pmu_identifier_group = {
	.attrs = hisi_pmu_identifier_attrs,
};
EXPORT_SYMBOL_NS_GPL(hisi_pmu_identifier_group, "HISI_PMU");

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
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_get_event_idx, "HISI_PMU");

static void hisi_uncore_pmu_clear_event_idx(struct hisi_pmu *hisi_pmu, int idx)
{
	clear_bit(idx, hisi_pmu->pmu_events.used_mask);
}

irqreturn_t hisi_uncore_pmu_isr(int irq, void *data)
{
	struct hisi_pmu *hisi_pmu = data;
	struct perf_event *event;
	unsigned long overflown;
	int idx;

	overflown = hisi_pmu->ops->get_int_status(hisi_pmu);
	if (!overflown)
		return IRQ_NONE;

	/*
	 * Find the counter index which overflowed if the bit was set
	 * and handle it.
	 */
	for_each_set_bit(idx, &overflown, hisi_pmu->num_counters) {
		/* Write 1 to clear the IRQ status flag */
		hisi_pmu->ops->clear_int_status(hisi_pmu, idx);
		/* Get the corresponding event struct */
		event = hisi_pmu->pmu_events.hw_events[idx];
		if (!event)
			continue;

		hisi_uncore_pmu_event_update(event);
		hisi_uncore_pmu_set_event_period(event);
	}

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_isr, "HISI_PMU");

int hisi_uncore_pmu_init_irq(struct hisi_pmu *hisi_pmu,
			     struct platform_device *pdev)
{
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, hisi_uncore_pmu_isr,
			       IRQF_NOBALANCING | IRQF_NO_THREAD,
			       dev_name(&pdev->dev), hisi_pmu);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Fail to request IRQ: %d ret: %d.\n", irq, ret);
		return ret;
	}

	hisi_pmu->irq = irq;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_init_irq, "HISI_PMU");

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
	if ((event->attr.config & HISI_EVENTID_MASK) > hisi_pmu->check_event)
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

	if (hisi_pmu->ops->check_filter && hisi_pmu->ops->check_filter(event))
		return -EINVAL;

	/* Enforce to use the same CPU for all events in this PMU */
	event->cpu = hisi_pmu->on_cpu;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_event_init, "HISI_PMU");

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

	if (hisi_pmu->ops->enable_filter)
		hisi_pmu->ops->enable_filter(event);

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

	if (hisi_pmu->ops->disable_filter)
		hisi_pmu->ops->disable_filter(event);
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
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_set_event_period, "HISI_PMU");

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
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_event_update, "HISI_PMU");

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
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_start, "HISI_PMU");

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
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_stop, "HISI_PMU");

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
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_add, "HISI_PMU");

void hisi_uncore_pmu_del(struct perf_event *event, int flags)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hisi_uncore_pmu_stop(event, PERF_EF_UPDATE);
	hisi_uncore_pmu_clear_event_idx(hisi_pmu, hwc->idx);
	perf_event_update_userpage(event);
	hisi_pmu->pmu_events.hw_events[hwc->idx] = NULL;
}
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_del, "HISI_PMU");

void hisi_uncore_pmu_read(struct perf_event *event)
{
	/* Read hardware counter and update the perf counter statistics */
	hisi_uncore_pmu_event_update(event);
}
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_read, "HISI_PMU");

void hisi_uncore_pmu_enable(struct pmu *pmu)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(pmu);
	bool enabled = !bitmap_empty(hisi_pmu->pmu_events.used_mask,
				    hisi_pmu->num_counters);

	if (!enabled)
		return;

	hisi_pmu->ops->start_counters(hisi_pmu);
}
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_enable, "HISI_PMU");

void hisi_uncore_pmu_disable(struct pmu *pmu)
{
	struct hisi_pmu *hisi_pmu = to_hisi_pmu(pmu);

	hisi_pmu->ops->stop_counters(hisi_pmu);
}
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_disable, "HISI_PMU");


/*
 * The Super CPU Cluster (SCCL) and CPU Cluster (CCL) IDs can be
 * determined from the MPIDR_EL1, but the encoding varies by CPU:
 *
 * - For MT variants of TSV110:
 *   SCCL is Aff2[7:3], CCL is Aff2[2:0]
 *
 * - For other MT parts:
 *   SCCL is Aff3[7:0], CCL is Aff2[7:0]
 *
 * - For non-MT parts:
 *   SCCL is Aff2[7:0], CCL is Aff1[7:0]
 */
static void hisi_read_sccl_and_ccl_id(int *scclp, int *cclp)
{
	u64 mpidr = read_cpuid_mpidr();
	int aff3 = MPIDR_AFFINITY_LEVEL(mpidr, 3);
	int aff2 = MPIDR_AFFINITY_LEVEL(mpidr, 2);
	int aff1 = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	bool mt = mpidr & MPIDR_MT_BITMASK;
	int sccl, ccl;

	if (mt && read_cpuid_part_number() == HISI_CPU_PART_TSV110) {
		sccl = aff2 >> 3;
		ccl = aff2 & 0x7;
	} else if (mt) {
		sccl = aff3;
		ccl = aff2;
	} else {
		sccl = aff2;
		ccl = aff1;
	}

	if (scclp)
		*scclp = sccl;
	if (cclp)
		*cclp = ccl;
}

/*
 * Check whether the CPU is associated with this uncore PMU
 */
static bool hisi_pmu_cpu_is_associated_pmu(struct hisi_pmu *hisi_pmu)
{
	struct hisi_pmu_topology *topo = &hisi_pmu->topo;
	int sccl_id, ccl_id;

	if (topo->ccl_id == -1) {
		/* If CCL_ID is -1, the PMU only shares the same SCCL */
		hisi_read_sccl_and_ccl_id(&sccl_id, NULL);

		return sccl_id == topo->sccl_id;
	}

	hisi_read_sccl_and_ccl_id(&sccl_id, &ccl_id);

	return sccl_id == topo->sccl_id && ccl_id == topo->ccl_id;
}

int hisi_uncore_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hisi_pmu *hisi_pmu = hlist_entry_safe(node, struct hisi_pmu,
						     node);

	/*
	 * If the CPU is not associated to PMU, initialize the hisi_pmu->on_cpu
	 * based on the locality if it hasn't been initialized yet. For PMUs
	 * do have associated CPUs, it'll be updated later.
	 */
	if (!hisi_pmu_cpu_is_associated_pmu(hisi_pmu)) {
		if (hisi_pmu->on_cpu != -1)
			return 0;

		hisi_pmu->on_cpu = cpumask_local_spread(0, dev_to_node(hisi_pmu->dev));
		if (hisi_pmu->irq > 0)
			WARN_ON(irq_set_affinity(hisi_pmu->irq,
						 cpumask_of(hisi_pmu->on_cpu)));
		return 0;
	}

	cpumask_set_cpu(cpu, &hisi_pmu->associated_cpus);

	/* If another associated CPU is already managing this PMU, simply return. */
	if (hisi_pmu->on_cpu != -1 &&
	    cpumask_test_cpu(hisi_pmu->on_cpu, &hisi_pmu->associated_cpus))
		return 0;

	/* Use this CPU in cpumask for event counting */
	hisi_pmu->on_cpu = cpu;

	/* Overflow interrupt also should use the same CPU */
	if (hisi_pmu->irq > 0)
		WARN_ON(irq_set_affinity(hisi_pmu->irq, cpumask_of(cpu)));

	return 0;
}
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_online_cpu, "HISI_PMU");

int hisi_uncore_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hisi_pmu *hisi_pmu = hlist_entry_safe(node, struct hisi_pmu,
						     node);
	unsigned int target;

	/* Nothing to do if this CPU doesn't own the PMU */
	if (hisi_pmu->on_cpu != cpu)
		return 0;

	/* Give up ownership of the PMU */
	hisi_pmu->on_cpu = -1;

	/*
	 * Migrate ownership of the PMU to a new CPU chosen from PMU's online
	 * associated CPUs if possible, if no associated CPU online then
	 * migrate to one online CPU.
	 */
	target = cpumask_any_and_but(&hisi_pmu->associated_cpus,
				     cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		target = cpumask_any_but(cpu_online_mask, cpu);

	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&hisi_pmu->pmu, cpu, target);
	/* Use this CPU for event counting */
	hisi_pmu->on_cpu = target;

	if (hisi_pmu->irq > 0)
		WARN_ON(irq_set_affinity(hisi_pmu->irq, cpumask_of(target)));

	return 0;
}
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_offline_cpu, "HISI_PMU");

/*
 * Retrieve the topology information from the firmware for the hisi_pmu device.
 * The topology ID will be -1 if we cannot initialize it, it may either due to
 * the PMU doesn't locate on this certain topology or the firmware needs to be
 * fixed.
 */
void hisi_uncore_pmu_init_topology(struct hisi_pmu *hisi_pmu, struct device *dev)
{
	struct hisi_pmu_topology *topo = &hisi_pmu->topo;

	topo->sccl_id = -1;
	topo->ccl_id = -1;
	topo->index_id = -1;
	topo->sub_id = -1;

	if (device_property_read_u32(dev, "hisilicon,scl-id", &topo->sccl_id))
		dev_dbg(dev, "no scl-id present\n");

	if (device_property_read_u32(dev, "hisilicon,ccl-id", &topo->ccl_id))
		dev_dbg(dev, "no ccl-id present\n");

	if (device_property_read_u32(dev, "hisilicon,idx-id", &topo->index_id))
		dev_dbg(dev, "no idx-id present\n");

	if (device_property_read_u32(dev, "hisilicon,sub-id", &topo->sub_id))
		dev_dbg(dev, "no sub-id present\n");
}
EXPORT_SYMBOL_NS_GPL(hisi_uncore_pmu_init_topology, "HISI_PMU");

void hisi_pmu_init(struct hisi_pmu *hisi_pmu, struct module *module)
{
	struct pmu *pmu = &hisi_pmu->pmu;

	pmu->module             = module;
	pmu->parent             = hisi_pmu->dev;
	pmu->task_ctx_nr        = perf_invalid_context;
	pmu->event_init         = hisi_uncore_pmu_event_init;
	pmu->pmu_enable         = hisi_uncore_pmu_enable;
	pmu->pmu_disable        = hisi_uncore_pmu_disable;
	pmu->add                = hisi_uncore_pmu_add;
	pmu->del                = hisi_uncore_pmu_del;
	pmu->start              = hisi_uncore_pmu_start;
	pmu->stop               = hisi_uncore_pmu_stop;
	pmu->read               = hisi_uncore_pmu_read;
	pmu->attr_groups        = hisi_pmu->pmu_events.attr_groups;
	pmu->capabilities       = PERF_PMU_CAP_NO_EXCLUDE;
}
EXPORT_SYMBOL_NS_GPL(hisi_pmu_init, "HISI_PMU");

MODULE_DESCRIPTION("HiSilicon SoC uncore Performance Monitor driver framework");
MODULE_LICENSE("GPL v2");
