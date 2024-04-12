// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARM DynamIQ Shared Unit (DSU) PMU driver
 *
 * Copyright (C) ARM Limited, 2017.
 *
 * Based on ARM CCI-PMU, ARMv8 PMU-v3 drivers.
 */

#define PMUNAME		"arm_dsu"
#define DRVNAME		PMUNAME "_pmu"
#define pr_fmt(fmt)	DRVNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <asm/arm_dsu_pmu.h>
#include <asm/local64.h>

/* PMU event codes */
#define DSU_PMU_EVT_CYCLES		0x11
#define DSU_PMU_EVT_CHAIN		0x1e

#define DSU_PMU_MAX_COMMON_EVENTS	0x40

#define DSU_PMU_MAX_HW_CNTRS		32
#define DSU_PMU_HW_COUNTER_MASK		(DSU_PMU_MAX_HW_CNTRS - 1)

#define CLUSTERPMCR_E			BIT(0)
#define CLUSTERPMCR_P			BIT(1)
#define CLUSTERPMCR_C			BIT(2)
#define CLUSTERPMCR_N_SHIFT		11
#define CLUSTERPMCR_N_MASK		0x1f
#define CLUSTERPMCR_IDCODE_SHIFT	16
#define CLUSTERPMCR_IDCODE_MASK		0xff
#define CLUSTERPMCR_IMP_SHIFT		24
#define CLUSTERPMCR_IMP_MASK		0xff
#define CLUSTERPMCR_RES_MASK		0x7e8
#define CLUSTERPMCR_RES_VAL		0x40

#define DSU_ACTIVE_CPU_MASK		0x0
#define DSU_ASSOCIATED_CPU_MASK		0x1

/*
 * We use the index of the counters as they appear in the counter
 * bit maps in the PMU registers (e.g CLUSTERPMSELR).
 * i.e,
 *	counter 0	- Bit 0
 *	counter 1	- Bit 1
 *	...
 *	Cycle counter	- Bit 31
 */
#define DSU_PMU_IDX_CYCLE_COUNTER	31

/* All event counters are 32bit, with a 64bit Cycle counter */
#define DSU_PMU_COUNTER_WIDTH(idx)	\
	(((idx) == DSU_PMU_IDX_CYCLE_COUNTER) ? 64 : 32)

#define DSU_PMU_COUNTER_MASK(idx)	\
	GENMASK_ULL((DSU_PMU_COUNTER_WIDTH((idx)) - 1), 0)

#define DSU_EXT_ATTR(_name, _func, _config)		\
	(&((struct dev_ext_attribute[]) {				\
		{							\
			.attr = __ATTR(_name, 0444, _func, NULL),	\
			.var = (void *)_config				\
		}							\
	})[0].attr.attr)

#define DSU_EVENT_ATTR(_name, _config)		\
	DSU_EXT_ATTR(_name, dsu_pmu_sysfs_event_show, (unsigned long)_config)

#define DSU_FORMAT_ATTR(_name, _config)		\
	DSU_EXT_ATTR(_name, dsu_pmu_sysfs_format_show, (char *)_config)

#define DSU_CPUMASK_ATTR(_name, _config)	\
	DSU_EXT_ATTR(_name, dsu_pmu_cpumask_show, (unsigned long)_config)

struct dsu_hw_events {
	DECLARE_BITMAP(used_mask, DSU_PMU_MAX_HW_CNTRS);
	struct perf_event	*events[DSU_PMU_MAX_HW_CNTRS];
};

/*
 * struct dsu_pmu	- DSU PMU descriptor
 *
 * @pmu_lock		: Protects accesses to DSU PMU register from normal vs
 *			  interrupt handler contexts.
 * @hw_events		: Holds the event counter state.
 * @associated_cpus	: CPUs attached to the DSU.
 * @active_cpu		: CPU to which the PMU is bound for accesses.
 * @cpuhp_node		: Node for CPU hotplug notifier link.
 * @num_counters	: Number of event counters implemented by the PMU,
 *			  excluding the cycle counter.
 * @irq			: Interrupt line for counter overflow.
 * @cpmceid_bitmap	: Bitmap for the availability of architected common
 *			  events (event_code < 0x40).
 */
struct dsu_pmu {
	struct pmu			pmu;
	struct device			*dev;
	raw_spinlock_t			pmu_lock;
	struct dsu_hw_events		hw_events;
	cpumask_t			associated_cpus;
	cpumask_t			active_cpu;
	struct hlist_node		cpuhp_node;
	s8				num_counters;
	int				irq;
	DECLARE_BITMAP(cpmceid_bitmap, DSU_PMU_MAX_COMMON_EVENTS);
};

static unsigned long dsu_pmu_cpuhp_state;

static inline struct dsu_pmu *to_dsu_pmu(struct pmu *pmu)
{
	return container_of(pmu, struct dsu_pmu, pmu);
}

static ssize_t dsu_pmu_sysfs_event_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct dev_ext_attribute *eattr = container_of(attr,
					struct dev_ext_attribute, attr);
	return sysfs_emit(buf, "event=0x%lx\n", (unsigned long)eattr->var);
}

static ssize_t dsu_pmu_sysfs_format_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct dev_ext_attribute *eattr = container_of(attr,
					struct dev_ext_attribute, attr);
	return sysfs_emit(buf, "%s\n", (char *)eattr->var);
}

static ssize_t dsu_pmu_cpumask_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct pmu *pmu = dev_get_drvdata(dev);
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(pmu);
	struct dev_ext_attribute *eattr = container_of(attr,
					struct dev_ext_attribute, attr);
	unsigned long mask_id = (unsigned long)eattr->var;
	const cpumask_t *cpumask;

	switch (mask_id) {
	case DSU_ACTIVE_CPU_MASK:
		cpumask = &dsu_pmu->active_cpu;
		break;
	case DSU_ASSOCIATED_CPU_MASK:
		cpumask = &dsu_pmu->associated_cpus;
		break;
	default:
		return 0;
	}
	return cpumap_print_to_pagebuf(true, buf, cpumask);
}

static struct attribute *dsu_pmu_format_attrs[] = {
	DSU_FORMAT_ATTR(event, "config:0-31"),
	NULL,
};

static const struct attribute_group dsu_pmu_format_attr_group = {
	.name = "format",
	.attrs = dsu_pmu_format_attrs,
};

static struct attribute *dsu_pmu_event_attrs[] = {
	DSU_EVENT_ATTR(cycles, 0x11),
	DSU_EVENT_ATTR(bus_access, 0x19),
	DSU_EVENT_ATTR(memory_error, 0x1a),
	DSU_EVENT_ATTR(bus_cycles, 0x1d),
	DSU_EVENT_ATTR(l3d_cache_allocate, 0x29),
	DSU_EVENT_ATTR(l3d_cache_refill, 0x2a),
	DSU_EVENT_ATTR(l3d_cache, 0x2b),
	DSU_EVENT_ATTR(l3d_cache_wb, 0x2c),
	NULL,
};

static umode_t
dsu_pmu_event_attr_is_visible(struct kobject *kobj, struct attribute *attr,
				int unused)
{
	struct pmu *pmu = dev_get_drvdata(kobj_to_dev(kobj));
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(pmu);
	struct dev_ext_attribute *eattr = container_of(attr,
					struct dev_ext_attribute, attr.attr);
	unsigned long evt = (unsigned long)eattr->var;

	return test_bit(evt, dsu_pmu->cpmceid_bitmap) ? attr->mode : 0;
}

static const struct attribute_group dsu_pmu_events_attr_group = {
	.name = "events",
	.attrs = dsu_pmu_event_attrs,
	.is_visible = dsu_pmu_event_attr_is_visible,
};

static struct attribute *dsu_pmu_cpumask_attrs[] = {
	DSU_CPUMASK_ATTR(cpumask, DSU_ACTIVE_CPU_MASK),
	DSU_CPUMASK_ATTR(associated_cpus, DSU_ASSOCIATED_CPU_MASK),
	NULL,
};

static const struct attribute_group dsu_pmu_cpumask_attr_group = {
	.attrs = dsu_pmu_cpumask_attrs,
};

static const struct attribute_group *dsu_pmu_attr_groups[] = {
	&dsu_pmu_cpumask_attr_group,
	&dsu_pmu_events_attr_group,
	&dsu_pmu_format_attr_group,
	NULL,
};

static int dsu_pmu_get_online_cpu_any_but(struct dsu_pmu *dsu_pmu, int cpu)
{
	struct cpumask online_supported;

	cpumask_and(&online_supported,
			 &dsu_pmu->associated_cpus, cpu_online_mask);
	return cpumask_any_but(&online_supported, cpu);
}

static inline bool dsu_pmu_counter_valid(struct dsu_pmu *dsu_pmu, u32 idx)
{
	return (idx < dsu_pmu->num_counters) ||
	       (idx == DSU_PMU_IDX_CYCLE_COUNTER);
}

static inline u64 dsu_pmu_read_counter(struct perf_event *event)
{
	u64 val;
	unsigned long flags;
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(event->pmu);
	int idx = event->hw.idx;

	if (WARN_ON(!cpumask_test_cpu(smp_processor_id(),
				 &dsu_pmu->associated_cpus)))
		return 0;

	if (!dsu_pmu_counter_valid(dsu_pmu, idx)) {
		dev_err(event->pmu->dev,
			"Trying reading invalid counter %d\n", idx);
		return 0;
	}

	raw_spin_lock_irqsave(&dsu_pmu->pmu_lock, flags);
	if (idx == DSU_PMU_IDX_CYCLE_COUNTER)
		val = __dsu_pmu_read_pmccntr();
	else
		val = __dsu_pmu_read_counter(idx);
	raw_spin_unlock_irqrestore(&dsu_pmu->pmu_lock, flags);

	return val;
}

static void dsu_pmu_write_counter(struct perf_event *event, u64 val)
{
	unsigned long flags;
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(event->pmu);
	int idx = event->hw.idx;

	if (WARN_ON(!cpumask_test_cpu(smp_processor_id(),
			 &dsu_pmu->associated_cpus)))
		return;

	if (!dsu_pmu_counter_valid(dsu_pmu, idx)) {
		dev_err(event->pmu->dev,
			"writing to invalid counter %d\n", idx);
		return;
	}

	raw_spin_lock_irqsave(&dsu_pmu->pmu_lock, flags);
	if (idx == DSU_PMU_IDX_CYCLE_COUNTER)
		__dsu_pmu_write_pmccntr(val);
	else
		__dsu_pmu_write_counter(idx, val);
	raw_spin_unlock_irqrestore(&dsu_pmu->pmu_lock, flags);
}

static int dsu_pmu_get_event_idx(struct dsu_hw_events *hw_events,
				 struct perf_event *event)
{
	int idx;
	unsigned long evtype = event->attr.config;
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(event->pmu);
	unsigned long *used_mask = hw_events->used_mask;

	if (evtype == DSU_PMU_EVT_CYCLES) {
		if (test_and_set_bit(DSU_PMU_IDX_CYCLE_COUNTER, used_mask))
			return -EAGAIN;
		return DSU_PMU_IDX_CYCLE_COUNTER;
	}

	idx = find_first_zero_bit(used_mask, dsu_pmu->num_counters);
	if (idx >= dsu_pmu->num_counters)
		return -EAGAIN;
	set_bit(idx, hw_events->used_mask);
	return idx;
}

static void dsu_pmu_enable_counter(struct dsu_pmu *dsu_pmu, int idx)
{
	__dsu_pmu_counter_interrupt_enable(idx);
	__dsu_pmu_enable_counter(idx);
}

static void dsu_pmu_disable_counter(struct dsu_pmu *dsu_pmu, int idx)
{
	__dsu_pmu_disable_counter(idx);
	__dsu_pmu_counter_interrupt_disable(idx);
}

static inline void dsu_pmu_set_event(struct dsu_pmu *dsu_pmu,
					struct perf_event *event)
{
	int idx = event->hw.idx;
	unsigned long flags;

	if (!dsu_pmu_counter_valid(dsu_pmu, idx)) {
		dev_err(event->pmu->dev,
			"Trying to set invalid counter %d\n", idx);
		return;
	}

	raw_spin_lock_irqsave(&dsu_pmu->pmu_lock, flags);
	__dsu_pmu_set_event(idx, event->hw.config_base);
	raw_spin_unlock_irqrestore(&dsu_pmu->pmu_lock, flags);
}

static void dsu_pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev_count, new_count;

	do {
		/* We may also be called from the irq handler */
		prev_count = local64_read(&hwc->prev_count);
		new_count = dsu_pmu_read_counter(event);
	} while (local64_cmpxchg(&hwc->prev_count, prev_count, new_count) !=
			prev_count);
	delta = (new_count - prev_count) & DSU_PMU_COUNTER_MASK(hwc->idx);
	local64_add(delta, &event->count);
}

static void dsu_pmu_read(struct perf_event *event)
{
	dsu_pmu_event_update(event);
}

static inline u32 dsu_pmu_get_reset_overflow(void)
{
	return __dsu_pmu_get_reset_overflow();
}

/*
 * dsu_pmu_set_event_period: Set the period for the counter.
 *
 * All DSU PMU event counters, except the cycle counter are 32bit
 * counters. To handle cases of extreme interrupt latency, we program
 * the counter with half of the max count for the counters.
 */
static void dsu_pmu_set_event_period(struct perf_event *event)
{
	int idx = event->hw.idx;
	u64 val = DSU_PMU_COUNTER_MASK(idx) >> 1;

	local64_set(&event->hw.prev_count, val);
	dsu_pmu_write_counter(event, val);
}

static irqreturn_t dsu_pmu_handle_irq(int irq_num, void *dev)
{
	int i;
	bool handled = false;
	struct dsu_pmu *dsu_pmu = dev;
	struct dsu_hw_events *hw_events = &dsu_pmu->hw_events;
	unsigned long overflow;

	overflow = dsu_pmu_get_reset_overflow();
	if (!overflow)
		return IRQ_NONE;

	for_each_set_bit(i, &overflow, DSU_PMU_MAX_HW_CNTRS) {
		struct perf_event *event = hw_events->events[i];

		if (!event)
			continue;
		dsu_pmu_event_update(event);
		dsu_pmu_set_event_period(event);
		handled = true;
	}

	return IRQ_RETVAL(handled);
}

static void dsu_pmu_start(struct perf_event *event, int pmu_flags)
{
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(event->pmu);

	/* We always reprogram the counter */
	if (pmu_flags & PERF_EF_RELOAD)
		WARN_ON(!(event->hw.state & PERF_HES_UPTODATE));
	dsu_pmu_set_event_period(event);
	if (event->hw.idx != DSU_PMU_IDX_CYCLE_COUNTER)
		dsu_pmu_set_event(dsu_pmu, event);
	event->hw.state = 0;
	dsu_pmu_enable_counter(dsu_pmu, event->hw.idx);
}

static void dsu_pmu_stop(struct perf_event *event, int pmu_flags)
{
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(event->pmu);

	if (event->hw.state & PERF_HES_STOPPED)
		return;
	dsu_pmu_disable_counter(dsu_pmu, event->hw.idx);
	dsu_pmu_event_update(event);
	event->hw.state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static int dsu_pmu_add(struct perf_event *event, int flags)
{
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(event->pmu);
	struct dsu_hw_events *hw_events = &dsu_pmu->hw_events;
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	if (WARN_ON_ONCE(!cpumask_test_cpu(smp_processor_id(),
					   &dsu_pmu->associated_cpus)))
		return -ENOENT;

	idx = dsu_pmu_get_event_idx(hw_events, event);
	if (idx < 0)
		return idx;

	hwc->idx = idx;
	hw_events->events[idx] = event;
	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	if (flags & PERF_EF_START)
		dsu_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
	return 0;
}

static void dsu_pmu_del(struct perf_event *event, int flags)
{
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(event->pmu);
	struct dsu_hw_events *hw_events = &dsu_pmu->hw_events;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	dsu_pmu_stop(event, PERF_EF_UPDATE);
	hw_events->events[idx] = NULL;
	clear_bit(idx, hw_events->used_mask);
	perf_event_update_userpage(event);
}

static void dsu_pmu_enable(struct pmu *pmu)
{
	u32 pmcr;
	unsigned long flags;
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(pmu);

	/* If no counters are added, skip enabling the PMU */
	if (bitmap_empty(dsu_pmu->hw_events.used_mask, DSU_PMU_MAX_HW_CNTRS))
		return;

	raw_spin_lock_irqsave(&dsu_pmu->pmu_lock, flags);
	pmcr = __dsu_pmu_read_pmcr();
	pmcr |= CLUSTERPMCR_E;
	__dsu_pmu_write_pmcr(pmcr);
	raw_spin_unlock_irqrestore(&dsu_pmu->pmu_lock, flags);
}

static void dsu_pmu_disable(struct pmu *pmu)
{
	u32 pmcr;
	unsigned long flags;
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(pmu);

	raw_spin_lock_irqsave(&dsu_pmu->pmu_lock, flags);
	pmcr = __dsu_pmu_read_pmcr();
	pmcr &= ~CLUSTERPMCR_E;
	__dsu_pmu_write_pmcr(pmcr);
	raw_spin_unlock_irqrestore(&dsu_pmu->pmu_lock, flags);
}

static bool dsu_pmu_validate_event(struct pmu *pmu,
				  struct dsu_hw_events *hw_events,
				  struct perf_event *event)
{
	if (is_software_event(event))
		return true;
	/* Reject groups spanning multiple HW PMUs. */
	if (event->pmu != pmu)
		return false;
	return dsu_pmu_get_event_idx(hw_events, event) >= 0;
}

/*
 * Make sure the group of events can be scheduled at once
 * on the PMU.
 */
static bool dsu_pmu_validate_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct dsu_hw_events fake_hw;

	if (event->group_leader == event)
		return true;

	memset(fake_hw.used_mask, 0, sizeof(fake_hw.used_mask));
	if (!dsu_pmu_validate_event(event->pmu, &fake_hw, leader))
		return false;
	for_each_sibling_event(sibling, leader) {
		if (!dsu_pmu_validate_event(event->pmu, &fake_hw, sibling))
			return false;
	}
	return dsu_pmu_validate_event(event->pmu, &fake_hw, event);
}

static int dsu_pmu_event_init(struct perf_event *event)
{
	struct dsu_pmu *dsu_pmu = to_dsu_pmu(event->pmu);

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* We don't support sampling */
	if (is_sampling_event(event)) {
		dev_dbg(dsu_pmu->pmu.dev, "Can't support sampling events\n");
		return -EOPNOTSUPP;
	}

	/* We cannot support task bound events */
	if (event->cpu < 0 || event->attach_state & PERF_ATTACH_TASK) {
		dev_dbg(dsu_pmu->pmu.dev, "Can't support per-task counters\n");
		return -EINVAL;
	}

	if (has_branch_stack(event)) {
		dev_dbg(dsu_pmu->pmu.dev, "Can't support filtering\n");
		return -EINVAL;
	}

	if (!cpumask_test_cpu(event->cpu, &dsu_pmu->associated_cpus)) {
		dev_dbg(dsu_pmu->pmu.dev,
			 "Requested cpu is not associated with the DSU\n");
		return -EINVAL;
	}
	/*
	 * Choose the current active CPU to read the events. We don't want
	 * to migrate the event contexts, irq handling etc to the requested
	 * CPU. As long as the requested CPU is within the same DSU, we
	 * are fine.
	 */
	event->cpu = cpumask_first(&dsu_pmu->active_cpu);
	if (event->cpu >= nr_cpu_ids)
		return -EINVAL;
	if (!dsu_pmu_validate_group(event))
		return -EINVAL;

	event->hw.config_base = event->attr.config;
	return 0;
}

static struct dsu_pmu *dsu_pmu_alloc(struct platform_device *pdev)
{
	struct dsu_pmu *dsu_pmu;

	dsu_pmu = devm_kzalloc(&pdev->dev, sizeof(*dsu_pmu), GFP_KERNEL);
	if (!dsu_pmu)
		return ERR_PTR(-ENOMEM);

	raw_spin_lock_init(&dsu_pmu->pmu_lock);
	/*
	 * Initialise the number of counters to -1, until we probe
	 * the real number on a connected CPU.
	 */
	dsu_pmu->num_counters = -1;
	return dsu_pmu;
}

/*
 * dsu_pmu_dt_get_cpus: Get the list of CPUs in the cluster
 * from device tree.
 */
static int dsu_pmu_dt_get_cpus(struct device *dev, cpumask_t *mask)
{
	int i = 0, n, cpu;
	struct device_node *cpu_node;

	n = of_count_phandle_with_args(dev->of_node, "cpus", NULL);
	if (n <= 0)
		return -ENODEV;
	for (; i < n; i++) {
		cpu_node = of_parse_phandle(dev->of_node, "cpus", i);
		if (!cpu_node)
			break;
		cpu = of_cpu_node_to_id(cpu_node);
		of_node_put(cpu_node);
		/*
		 * We have to ignore the failures here and continue scanning
		 * the list to handle cases where the nr_cpus could be capped
		 * in the running kernel.
		 */
		if (cpu < 0)
			continue;
		cpumask_set_cpu(cpu, mask);
	}
	return 0;
}

/*
 * dsu_pmu_acpi_get_cpus: Get the list of CPUs in the cluster
 * from ACPI.
 */
static int dsu_pmu_acpi_get_cpus(struct device *dev, cpumask_t *mask)
{
#ifdef CONFIG_ACPI
	struct acpi_device *parent_adev = acpi_dev_parent(ACPI_COMPANION(dev));
	int cpu;

	/*
	 * A dsu pmu node is inside a cluster parent node along with cpu nodes.
	 * We need to find out all cpus that have the same parent with this pmu.
	 */
	for_each_possible_cpu(cpu) {
		struct acpi_device *acpi_dev;
		struct device *cpu_dev = get_cpu_device(cpu);

		if (!cpu_dev)
			continue;

		acpi_dev = ACPI_COMPANION(cpu_dev);
		if (acpi_dev && acpi_dev_parent(acpi_dev) == parent_adev)
			cpumask_set_cpu(cpu, mask);
	}
#endif

	return 0;
}

/*
 * dsu_pmu_probe_pmu: Probe the PMU details on a CPU in the cluster.
 */
static void dsu_pmu_probe_pmu(struct dsu_pmu *dsu_pmu)
{
	u64 num_counters;
	u32 cpmceid[2];

	num_counters = (__dsu_pmu_read_pmcr() >> CLUSTERPMCR_N_SHIFT) &
						CLUSTERPMCR_N_MASK;
	/* We can only support up to 31 independent counters */
	if (WARN_ON(num_counters > 31))
		num_counters = 31;
	dsu_pmu->num_counters = num_counters;
	if (!dsu_pmu->num_counters)
		return;
	cpmceid[0] = __dsu_pmu_read_pmceid(0);
	cpmceid[1] = __dsu_pmu_read_pmceid(1);
	bitmap_from_arr32(dsu_pmu->cpmceid_bitmap, cpmceid,
			  DSU_PMU_MAX_COMMON_EVENTS);
}

static void dsu_pmu_set_active_cpu(int cpu, struct dsu_pmu *dsu_pmu)
{
	cpumask_set_cpu(cpu, &dsu_pmu->active_cpu);
	if (irq_set_affinity(dsu_pmu->irq, &dsu_pmu->active_cpu))
		pr_warn("Failed to set irq affinity to %d\n", cpu);
}

/*
 * dsu_pmu_init_pmu: Initialise the DSU PMU configurations if
 * we haven't done it already.
 */
static void dsu_pmu_init_pmu(struct dsu_pmu *dsu_pmu)
{
	if (dsu_pmu->num_counters == -1)
		dsu_pmu_probe_pmu(dsu_pmu);
	/* Reset the interrupt overflow mask */
	dsu_pmu_get_reset_overflow();
}

static int dsu_pmu_device_probe(struct platform_device *pdev)
{
	int irq, rc;
	struct dsu_pmu *dsu_pmu;
	struct fwnode_handle *fwnode = dev_fwnode(&pdev->dev);
	char *name;
	static atomic_t pmu_idx = ATOMIC_INIT(-1);

	dsu_pmu = dsu_pmu_alloc(pdev);
	if (IS_ERR(dsu_pmu))
		return PTR_ERR(dsu_pmu);

	if (is_of_node(fwnode))
		rc = dsu_pmu_dt_get_cpus(&pdev->dev, &dsu_pmu->associated_cpus);
	else if (is_acpi_device_node(fwnode))
		rc = dsu_pmu_acpi_get_cpus(&pdev->dev, &dsu_pmu->associated_cpus);
	else
		return -ENOENT;

	if (rc) {
		dev_warn(&pdev->dev, "Failed to parse the CPUs\n");
		return rc;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_%d",
				PMUNAME, atomic_inc_return(&pmu_idx));
	if (!name)
		return -ENOMEM;
	rc = devm_request_irq(&pdev->dev, irq, dsu_pmu_handle_irq,
			      IRQF_NOBALANCING, name, dsu_pmu);
	if (rc) {
		dev_warn(&pdev->dev, "Failed to request IRQ %d\n", irq);
		return rc;
	}

	dsu_pmu->irq = irq;
	platform_set_drvdata(pdev, dsu_pmu);
	rc = cpuhp_state_add_instance(dsu_pmu_cpuhp_state,
						&dsu_pmu->cpuhp_node);
	if (rc)
		return rc;

	dsu_pmu->pmu = (struct pmu) {
		.task_ctx_nr	= perf_invalid_context,
		.module		= THIS_MODULE,
		.pmu_enable	= dsu_pmu_enable,
		.pmu_disable	= dsu_pmu_disable,
		.event_init	= dsu_pmu_event_init,
		.add		= dsu_pmu_add,
		.del		= dsu_pmu_del,
		.start		= dsu_pmu_start,
		.stop		= dsu_pmu_stop,
		.read		= dsu_pmu_read,

		.attr_groups	= dsu_pmu_attr_groups,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
	};

	rc = perf_pmu_register(&dsu_pmu->pmu, name, -1);
	if (rc) {
		cpuhp_state_remove_instance(dsu_pmu_cpuhp_state,
						 &dsu_pmu->cpuhp_node);
	}

	return rc;
}

static void dsu_pmu_device_remove(struct platform_device *pdev)
{
	struct dsu_pmu *dsu_pmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&dsu_pmu->pmu);
	cpuhp_state_remove_instance(dsu_pmu_cpuhp_state, &dsu_pmu->cpuhp_node);
}

static const struct of_device_id dsu_pmu_of_match[] = {
	{ .compatible = "arm,dsu-pmu", },
	{},
};
MODULE_DEVICE_TABLE(of, dsu_pmu_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id dsu_pmu_acpi_match[] = {
	{ "ARMHD500", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, dsu_pmu_acpi_match);
#endif

static struct platform_driver dsu_pmu_driver = {
	.driver = {
		.name	= DRVNAME,
		.of_match_table = of_match_ptr(dsu_pmu_of_match),
		.acpi_match_table = ACPI_PTR(dsu_pmu_acpi_match),
		.suppress_bind_attrs = true,
	},
	.probe = dsu_pmu_device_probe,
	.remove_new = dsu_pmu_device_remove,
};

static int dsu_pmu_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct dsu_pmu *dsu_pmu = hlist_entry_safe(node, struct dsu_pmu,
						   cpuhp_node);

	if (!cpumask_test_cpu(cpu, &dsu_pmu->associated_cpus))
		return 0;

	/* If the PMU is already managed, there is nothing to do */
	if (!cpumask_empty(&dsu_pmu->active_cpu))
		return 0;

	dsu_pmu_init_pmu(dsu_pmu);
	dsu_pmu_set_active_cpu(cpu, dsu_pmu);

	return 0;
}

static int dsu_pmu_cpu_teardown(unsigned int cpu, struct hlist_node *node)
{
	int dst;
	struct dsu_pmu *dsu_pmu = hlist_entry_safe(node, struct dsu_pmu,
						   cpuhp_node);

	if (!cpumask_test_and_clear_cpu(cpu, &dsu_pmu->active_cpu))
		return 0;

	dst = dsu_pmu_get_online_cpu_any_but(dsu_pmu, cpu);
	/* If there are no active CPUs in the DSU, leave IRQ disabled */
	if (dst >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&dsu_pmu->pmu, cpu, dst);
	dsu_pmu_set_active_cpu(dst, dsu_pmu);

	return 0;
}

static int __init dsu_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
					DRVNAME,
					dsu_pmu_cpu_online,
					dsu_pmu_cpu_teardown);
	if (ret < 0)
		return ret;
	dsu_pmu_cpuhp_state = ret;
	ret = platform_driver_register(&dsu_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(dsu_pmu_cpuhp_state);

	return ret;
}

static void __exit dsu_pmu_exit(void)
{
	platform_driver_unregister(&dsu_pmu_driver);
	cpuhp_remove_multi_state(dsu_pmu_cpuhp_state);
}

module_init(dsu_pmu_init);
module_exit(dsu_pmu_exit);

MODULE_DESCRIPTION("Perf driver for ARM DynamIQ Shared Unit");
MODULE_AUTHOR("Suzuki K Poulose <suzuki.poulose@arm.com>");
MODULE_LICENSE("GPL v2");
