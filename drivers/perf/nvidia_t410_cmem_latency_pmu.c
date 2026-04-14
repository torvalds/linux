// SPDX-License-Identifier: GPL-2.0
/*
 * NVIDIA Tegra410 CPU Memory (CMEM) Latency PMU driver.
 *
 * Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>

#define NUM_INSTANCES    14

/* Register offsets. */
#define CMEM_LAT_CG_CTRL         0x800
#define CMEM_LAT_CTRL            0x808
#define CMEM_LAT_STATUS          0x810
#define CMEM_LAT_CYCLE_CNTR      0x818
#define CMEM_LAT_MC0_REQ_CNTR    0x820
#define CMEM_LAT_MC0_AOR_CNTR    0x830
#define CMEM_LAT_MC1_REQ_CNTR    0x838
#define CMEM_LAT_MC1_AOR_CNTR    0x848
#define CMEM_LAT_MC2_REQ_CNTR    0x850
#define CMEM_LAT_MC2_AOR_CNTR    0x860

/* CMEM_LAT_CTRL values. */
#define CMEM_LAT_CTRL_DISABLE    0x0ULL
#define CMEM_LAT_CTRL_ENABLE     0x1ULL
#define CMEM_LAT_CTRL_CLR        0x2ULL

/* CMEM_LAT_CG_CTRL values. */
#define CMEM_LAT_CG_CTRL_DISABLE    0x0ULL
#define CMEM_LAT_CG_CTRL_ENABLE     0x1ULL

/* CMEM_LAT_STATUS register field. */
#define CMEM_LAT_STATUS_CYCLE_OVF      BIT(0)
#define CMEM_LAT_STATUS_MC0_AOR_OVF    BIT(1)
#define CMEM_LAT_STATUS_MC0_REQ_OVF    BIT(3)
#define CMEM_LAT_STATUS_MC1_AOR_OVF    BIT(4)
#define CMEM_LAT_STATUS_MC1_REQ_OVF    BIT(6)
#define CMEM_LAT_STATUS_MC2_AOR_OVF    BIT(7)
#define CMEM_LAT_STATUS_MC2_REQ_OVF    BIT(9)

/* Events. */
#define CMEM_LAT_EVENT_CYCLES    0x0
#define CMEM_LAT_EVENT_REQ       0x1
#define CMEM_LAT_EVENT_AOR       0x2

#define CMEM_LAT_NUM_EVENTS           0x3
#define CMEM_LAT_MASK_EVENT           0x3
#define CMEM_LAT_MAX_ACTIVE_EVENTS    32

#define CMEM_LAT_ACTIVE_CPU_MASK        0x0
#define CMEM_LAT_ASSOCIATED_CPU_MASK    0x1

static unsigned long cmem_lat_pmu_cpuhp_state;

struct cmem_lat_pmu_hw_events {
	struct perf_event *events[CMEM_LAT_MAX_ACTIVE_EVENTS];
	DECLARE_BITMAP(used_ctrs, CMEM_LAT_MAX_ACTIVE_EVENTS);
};

struct cmem_lat_pmu {
	struct pmu pmu;
	struct device *dev;
	const char *name;
	const char *identifier;
	void __iomem *base_broadcast;
	void __iomem *base[NUM_INSTANCES];
	cpumask_t associated_cpus;
	cpumask_t active_cpu;
	struct hlist_node node;
	struct cmem_lat_pmu_hw_events hw_events;
};

#define to_cmem_lat_pmu(p) \
	container_of(p, struct cmem_lat_pmu, pmu)


/* Get event type from perf_event. */
static inline u32 get_event_type(struct perf_event *event)
{
	return (event->attr.config) & CMEM_LAT_MASK_EVENT;
}

/* PMU operations. */
static int cmem_lat_pmu_get_event_idx(struct cmem_lat_pmu_hw_events *hw_events,
				struct perf_event *event)
{
	unsigned int idx;

	idx = find_first_zero_bit(hw_events->used_ctrs, CMEM_LAT_MAX_ACTIVE_EVENTS);
	if (idx >= CMEM_LAT_MAX_ACTIVE_EVENTS)
		return -EAGAIN;

	set_bit(idx, hw_events->used_ctrs);

	return idx;
}

static bool cmem_lat_pmu_validate_event(struct pmu *pmu,
				 struct cmem_lat_pmu_hw_events *hw_events,
				 struct perf_event *event)
{
	int ret;

	if (is_software_event(event))
		return true;

	/* Reject groups spanning multiple HW PMUs. */
	if (event->pmu != pmu)
		return false;

	ret = cmem_lat_pmu_get_event_idx(hw_events, event);
	if (ret < 0)
		return false;

	return true;
}

/* Make sure the group of events can be scheduled at once on the PMU. */
static bool cmem_lat_pmu_validate_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct cmem_lat_pmu_hw_events fake_hw_events;

	if (event->group_leader == event)
		return true;

	memset(&fake_hw_events, 0, sizeof(fake_hw_events));

	if (!cmem_lat_pmu_validate_event(event->pmu, &fake_hw_events, leader))
		return false;

	for_each_sibling_event(sibling, leader) {
		if (!cmem_lat_pmu_validate_event(event->pmu, &fake_hw_events, sibling))
			return false;
	}

	return cmem_lat_pmu_validate_event(event->pmu, &fake_hw_events, event);
}

static int cmem_lat_pmu_event_init(struct perf_event *event)
{
	struct cmem_lat_pmu *cmem_lat_pmu = to_cmem_lat_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u32 event_type = get_event_type(event);

	if (event->attr.type != event->pmu->type ||
	    event_type >= CMEM_LAT_NUM_EVENTS)
		return -ENOENT;

	/*
	 * Sampling, per-process mode, and per-task counters are not supported
	 * since this PMU is shared across all CPUs.
	 */
	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK) {
		dev_dbg(cmem_lat_pmu->pmu.dev,
				"Can't support sampling and per-process mode\n");
		return -EOPNOTSUPP;
	}

	if (event->cpu < 0) {
		dev_dbg(cmem_lat_pmu->pmu.dev, "Can't support per-task counters\n");
		return -EINVAL;
	}

	/*
	 * Make sure the CPU assignment is on one of the CPUs associated with
	 * this PMU.
	 */
	if (!cpumask_test_cpu(event->cpu, &cmem_lat_pmu->associated_cpus)) {
		dev_dbg(cmem_lat_pmu->pmu.dev,
				"Requested cpu is not associated with the PMU\n");
		return -EINVAL;
	}

	/* Enforce the current active CPU to handle the events in this PMU. */
	event->cpu = cpumask_first(&cmem_lat_pmu->active_cpu);
	if (event->cpu >= nr_cpu_ids)
		return -EINVAL;

	if (!cmem_lat_pmu_validate_group(event))
		return -EINVAL;

	hwc->idx = -1;
	hwc->config = event_type;

	return 0;
}

static u64 cmem_lat_pmu_read_status(struct cmem_lat_pmu *cmem_lat_pmu,
				   unsigned int inst)
{
	return readq(cmem_lat_pmu->base[inst] + CMEM_LAT_STATUS);
}

static u64 cmem_lat_pmu_read_cycle_counter(struct perf_event *event)
{
	const unsigned int instance = 0;
	u64 status;
	struct cmem_lat_pmu *cmem_lat_pmu = to_cmem_lat_pmu(event->pmu);
	struct device *dev = cmem_lat_pmu->dev;

	/*
	 * Use the reading from first instance since all instances are
	 * identical.
	 */
	status = cmem_lat_pmu_read_status(cmem_lat_pmu, instance);
	if (status & CMEM_LAT_STATUS_CYCLE_OVF)
		dev_warn(dev, "Cycle counter overflow\n");

	return readq(cmem_lat_pmu->base[instance] + CMEM_LAT_CYCLE_CNTR);
}

static u64 cmem_lat_pmu_read_req_counter(struct perf_event *event)
{
	unsigned int i;
	u64 status, val = 0;
	struct cmem_lat_pmu *cmem_lat_pmu = to_cmem_lat_pmu(event->pmu);
	struct device *dev = cmem_lat_pmu->dev;

	/* Sum up the counts from all instances. */
	for (i = 0; i < NUM_INSTANCES; i++) {
		status = cmem_lat_pmu_read_status(cmem_lat_pmu, i);
		if (status & CMEM_LAT_STATUS_MC0_REQ_OVF)
			dev_warn(dev, "MC0 request counter overflow\n");
		if (status & CMEM_LAT_STATUS_MC1_REQ_OVF)
			dev_warn(dev, "MC1 request counter overflow\n");
		if (status & CMEM_LAT_STATUS_MC2_REQ_OVF)
			dev_warn(dev, "MC2 request counter overflow\n");

		val += readq(cmem_lat_pmu->base[i] + CMEM_LAT_MC0_REQ_CNTR);
		val += readq(cmem_lat_pmu->base[i] + CMEM_LAT_MC1_REQ_CNTR);
		val += readq(cmem_lat_pmu->base[i] + CMEM_LAT_MC2_REQ_CNTR);
	}

	return val;
}

static u64 cmem_lat_pmu_read_aor_counter(struct perf_event *event)
{
	unsigned int i;
	u64 status, val = 0;
	struct cmem_lat_pmu *cmem_lat_pmu = to_cmem_lat_pmu(event->pmu);
	struct device *dev = cmem_lat_pmu->dev;

	/* Sum up the counts from all instances. */
	for (i = 0; i < NUM_INSTANCES; i++) {
		status = cmem_lat_pmu_read_status(cmem_lat_pmu, i);
		if (status & CMEM_LAT_STATUS_MC0_AOR_OVF)
			dev_warn(dev, "MC0 AOR counter overflow\n");
		if (status & CMEM_LAT_STATUS_MC1_AOR_OVF)
			dev_warn(dev, "MC1 AOR counter overflow\n");
		if (status & CMEM_LAT_STATUS_MC2_AOR_OVF)
			dev_warn(dev, "MC2 AOR counter overflow\n");

		val += readq(cmem_lat_pmu->base[i] + CMEM_LAT_MC0_AOR_CNTR);
		val += readq(cmem_lat_pmu->base[i] + CMEM_LAT_MC1_AOR_CNTR);
		val += readq(cmem_lat_pmu->base[i] + CMEM_LAT_MC2_AOR_CNTR);
	}

	return val;
}

static u64 (*read_counter_fn[CMEM_LAT_NUM_EVENTS])(struct perf_event *) = {
	[CMEM_LAT_EVENT_CYCLES] = cmem_lat_pmu_read_cycle_counter,
	[CMEM_LAT_EVENT_REQ] = cmem_lat_pmu_read_req_counter,
	[CMEM_LAT_EVENT_AOR] = cmem_lat_pmu_read_aor_counter,
};

static void cmem_lat_pmu_event_update(struct perf_event *event)
{
	u32 event_type;
	u64 prev, now;
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->state & PERF_HES_STOPPED)
		return;

	event_type = hwc->config;

	do {
		prev = local64_read(&hwc->prev_count);
		now = read_counter_fn[event_type](event);
	} while (local64_cmpxchg(&hwc->prev_count, prev, now) != prev);

	local64_add(now - prev, &event->count);

	hwc->state |= PERF_HES_UPTODATE;
}

static void cmem_lat_pmu_start(struct perf_event *event, int pmu_flags)
{
	event->hw.state = 0;
}

static void cmem_lat_pmu_stop(struct perf_event *event, int pmu_flags)
{
	event->hw.state |= PERF_HES_STOPPED;
}

static int cmem_lat_pmu_add(struct perf_event *event, int flags)
{
	struct cmem_lat_pmu *cmem_lat_pmu = to_cmem_lat_pmu(event->pmu);
	struct cmem_lat_pmu_hw_events *hw_events = &cmem_lat_pmu->hw_events;
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	if (WARN_ON_ONCE(!cpumask_test_cpu(smp_processor_id(),
					   &cmem_lat_pmu->associated_cpus)))
		return -ENOENT;

	idx = cmem_lat_pmu_get_event_idx(hw_events, event);
	if (idx < 0)
		return idx;

	hw_events->events[idx] = event;
	hwc->idx = idx;
	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	if (flags & PERF_EF_START)
		cmem_lat_pmu_start(event, PERF_EF_RELOAD);

	/* Propagate changes to the userspace mapping. */
	perf_event_update_userpage(event);

	return 0;
}

static void cmem_lat_pmu_del(struct perf_event *event, int flags)
{
	struct cmem_lat_pmu *cmem_lat_pmu = to_cmem_lat_pmu(event->pmu);
	struct cmem_lat_pmu_hw_events *hw_events = &cmem_lat_pmu->hw_events;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	cmem_lat_pmu_stop(event, PERF_EF_UPDATE);

	hw_events->events[idx] = NULL;

	clear_bit(idx, hw_events->used_ctrs);

	perf_event_update_userpage(event);
}

static void cmem_lat_pmu_read(struct perf_event *event)
{
	cmem_lat_pmu_event_update(event);
}

static inline void cmem_lat_pmu_cg_ctrl(struct cmem_lat_pmu *cmem_lat_pmu,
										u64 val)
{
	writeq(val, cmem_lat_pmu->base_broadcast + CMEM_LAT_CG_CTRL);
}

static inline void cmem_lat_pmu_ctrl(struct cmem_lat_pmu *cmem_lat_pmu, u64 val)
{
	writeq(val, cmem_lat_pmu->base_broadcast + CMEM_LAT_CTRL);
}

static void cmem_lat_pmu_enable(struct pmu *pmu)
{
	bool disabled;
	struct cmem_lat_pmu *cmem_lat_pmu = to_cmem_lat_pmu(pmu);

	disabled = bitmap_empty(cmem_lat_pmu->hw_events.used_ctrs,
							CMEM_LAT_MAX_ACTIVE_EVENTS);

	if (disabled)
		return;

	/* Enable all the counters. */
	cmem_lat_pmu_cg_ctrl(cmem_lat_pmu, CMEM_LAT_CG_CTRL_ENABLE);
	cmem_lat_pmu_ctrl(cmem_lat_pmu, CMEM_LAT_CTRL_ENABLE);
}

static void cmem_lat_pmu_disable(struct pmu *pmu)
{
	int idx;
	struct cmem_lat_pmu *cmem_lat_pmu = to_cmem_lat_pmu(pmu);

	/* Disable all the counters. */
	cmem_lat_pmu_ctrl(cmem_lat_pmu, CMEM_LAT_CTRL_DISABLE);

	/*
	 * The counters will start from 0 again on restart.
	 * Update the events immediately to avoid losing the counts.
	 */
	for_each_set_bit(idx, cmem_lat_pmu->hw_events.used_ctrs,
						CMEM_LAT_MAX_ACTIVE_EVENTS) {
		struct perf_event *event = cmem_lat_pmu->hw_events.events[idx];

		if (!event)
			continue;

		cmem_lat_pmu_event_update(event);

		local64_set(&event->hw.prev_count, 0ULL);
	}

	cmem_lat_pmu_ctrl(cmem_lat_pmu, CMEM_LAT_CTRL_CLR);
	cmem_lat_pmu_cg_ctrl(cmem_lat_pmu, CMEM_LAT_CG_CTRL_DISABLE);
}

/* PMU identifier attribute. */

static ssize_t cmem_lat_pmu_identifier_show(struct device *dev,
					 struct device_attribute *attr,
					 char *page)
{
	struct cmem_lat_pmu *cmem_lat_pmu = to_cmem_lat_pmu(dev_get_drvdata(dev));

	return sysfs_emit(page, "%s\n", cmem_lat_pmu->identifier);
}

static struct device_attribute cmem_lat_pmu_identifier_attr =
	__ATTR(identifier, 0444, cmem_lat_pmu_identifier_show, NULL);

static struct attribute *cmem_lat_pmu_identifier_attrs[] = {
	&cmem_lat_pmu_identifier_attr.attr,
	NULL
};

static struct attribute_group cmem_lat_pmu_identifier_attr_group = {
	.attrs = cmem_lat_pmu_identifier_attrs,
};

/* Format attributes. */

#define NV_PMU_EXT_ATTR(_name, _func, _config)			\
	(&((struct dev_ext_attribute[]){				\
		{							\
			.attr = __ATTR(_name, 0444, _func, NULL),	\
			.var = (void *)_config				\
		}							\
	})[0].attr.attr)

static struct attribute *cmem_lat_pmu_formats[] = {
	NV_PMU_EXT_ATTR(event, device_show_string, "config:0-1"),
	NULL
};

static const struct attribute_group cmem_lat_pmu_format_group = {
	.name = "format",
	.attrs = cmem_lat_pmu_formats,
};

/* Event attributes. */

static ssize_t cmem_lat_pmu_sysfs_event_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, typeof(*pmu_attr), attr);
	return sysfs_emit(buf, "event=0x%llx\n", pmu_attr->id);
}

#define NV_PMU_EVENT_ATTR(_name, _config)	\
	PMU_EVENT_ATTR_ID(_name, cmem_lat_pmu_sysfs_event_show, _config)

static struct attribute *cmem_lat_pmu_events[] = {
	NV_PMU_EVENT_ATTR(cycles, CMEM_LAT_EVENT_CYCLES),
	NV_PMU_EVENT_ATTR(rd_req, CMEM_LAT_EVENT_REQ),
	NV_PMU_EVENT_ATTR(rd_cum_outs, CMEM_LAT_EVENT_AOR),
	NULL
};

static const struct attribute_group cmem_lat_pmu_events_group = {
	.name = "events",
	.attrs = cmem_lat_pmu_events,
};

/* Cpumask attributes. */

static ssize_t cmem_lat_pmu_cpumask_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct pmu *pmu = dev_get_drvdata(dev);
	struct cmem_lat_pmu *cmem_lat_pmu = to_cmem_lat_pmu(pmu);
	struct dev_ext_attribute *eattr =
		container_of(attr, struct dev_ext_attribute, attr);
	unsigned long mask_id = (unsigned long)eattr->var;
	const cpumask_t *cpumask;

	switch (mask_id) {
	case CMEM_LAT_ACTIVE_CPU_MASK:
		cpumask = &cmem_lat_pmu->active_cpu;
		break;
	case CMEM_LAT_ASSOCIATED_CPU_MASK:
		cpumask = &cmem_lat_pmu->associated_cpus;
		break;
	default:
		return 0;
	}
	return cpumap_print_to_pagebuf(true, buf, cpumask);
}

#define NV_PMU_CPUMASK_ATTR(_name, _config)			\
	NV_PMU_EXT_ATTR(_name, cmem_lat_pmu_cpumask_show,	\
				(unsigned long)_config)

static struct attribute *cmem_lat_pmu_cpumask_attrs[] = {
	NV_PMU_CPUMASK_ATTR(cpumask, CMEM_LAT_ACTIVE_CPU_MASK),
	NV_PMU_CPUMASK_ATTR(associated_cpus, CMEM_LAT_ASSOCIATED_CPU_MASK),
	NULL
};

static const struct attribute_group cmem_lat_pmu_cpumask_attr_group = {
	.attrs = cmem_lat_pmu_cpumask_attrs,
};

/* Per PMU device attribute groups. */

static const struct attribute_group *cmem_lat_pmu_attr_groups[] = {
	&cmem_lat_pmu_identifier_attr_group,
	&cmem_lat_pmu_format_group,
	&cmem_lat_pmu_events_group,
	&cmem_lat_pmu_cpumask_attr_group,
	NULL
};

static int cmem_lat_pmu_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct cmem_lat_pmu *cmem_lat_pmu =
		hlist_entry_safe(node, struct cmem_lat_pmu, node);

	if (!cpumask_test_cpu(cpu, &cmem_lat_pmu->associated_cpus))
		return 0;

	/* If the PMU is already managed, there is nothing to do */
	if (!cpumask_empty(&cmem_lat_pmu->active_cpu))
		return 0;

	/* Use this CPU for event counting */
	cpumask_set_cpu(cpu, &cmem_lat_pmu->active_cpu);

	return 0;
}

static int cmem_lat_pmu_cpu_teardown(unsigned int cpu, struct hlist_node *node)
{
	unsigned int dst;

	struct cmem_lat_pmu *cmem_lat_pmu =
		hlist_entry_safe(node, struct cmem_lat_pmu, node);

	/* Nothing to do if this CPU doesn't own the PMU */
	if (!cpumask_test_and_clear_cpu(cpu, &cmem_lat_pmu->active_cpu))
		return 0;

	/* Choose a new CPU to migrate ownership of the PMU to */
	dst = cpumask_any_and_but(&cmem_lat_pmu->associated_cpus,
				  cpu_online_mask, cpu);
	if (dst >= nr_cpu_ids)
		return 0;

	/* Use this CPU for event counting */
	perf_pmu_migrate_context(&cmem_lat_pmu->pmu, cpu, dst);
	cpumask_set_cpu(dst, &cmem_lat_pmu->active_cpu);

	return 0;
}

static int cmem_lat_pmu_get_cpus(struct cmem_lat_pmu *cmem_lat_pmu,
				unsigned int socket)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (cpu_to_node(cpu) == socket)
			cpumask_set_cpu(cpu, &cmem_lat_pmu->associated_cpus);
	}

	if (cpumask_empty(&cmem_lat_pmu->associated_cpus)) {
		dev_dbg(cmem_lat_pmu->dev,
			"No cpu associated with PMU socket-%u\n", socket);
		return -ENODEV;
	}

	return 0;
}

static int cmem_lat_pmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acpi_device *acpi_dev;
	struct cmem_lat_pmu *cmem_lat_pmu;
	char *name, *uid_str;
	int ret, i;
	u32 socket;

	acpi_dev = ACPI_COMPANION(dev);
	if (!acpi_dev)
		return -ENODEV;

	uid_str = acpi_device_uid(acpi_dev);
	if (!uid_str)
		return -ENODEV;

	ret = kstrtou32(uid_str, 0, &socket);
	if (ret)
		return ret;

	cmem_lat_pmu = devm_kzalloc(dev, sizeof(*cmem_lat_pmu), GFP_KERNEL);
	name = devm_kasprintf(dev, GFP_KERNEL, "nvidia_cmem_latency_pmu_%u", socket);
	if (!cmem_lat_pmu || !name)
		return -ENOMEM;

	cmem_lat_pmu->dev = dev;
	cmem_lat_pmu->name = name;
	cmem_lat_pmu->identifier = acpi_device_hid(acpi_dev);
	platform_set_drvdata(pdev, cmem_lat_pmu);

	cmem_lat_pmu->pmu = (struct pmu) {
		.parent		= &pdev->dev,
		.task_ctx_nr	= perf_invalid_context,
		.pmu_enable	= cmem_lat_pmu_enable,
		.pmu_disable	= cmem_lat_pmu_disable,
		.event_init	= cmem_lat_pmu_event_init,
		.add		= cmem_lat_pmu_add,
		.del		= cmem_lat_pmu_del,
		.start		= cmem_lat_pmu_start,
		.stop		= cmem_lat_pmu_stop,
		.read		= cmem_lat_pmu_read,
		.attr_groups	= cmem_lat_pmu_attr_groups,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE |
					PERF_PMU_CAP_NO_INTERRUPT,
	};

	/* Map the address of all the instances. */
	for (i = 0; i < NUM_INSTANCES; i++) {
		cmem_lat_pmu->base[i] = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(cmem_lat_pmu->base[i])) {
			dev_err(dev, "Failed map address for instance %d\n", i);
			return PTR_ERR(cmem_lat_pmu->base[i]);
		}
	}

	/* Map broadcast address. */
	cmem_lat_pmu->base_broadcast = devm_platform_ioremap_resource(pdev,
										NUM_INSTANCES);
	if (IS_ERR(cmem_lat_pmu->base_broadcast)) {
		dev_err(dev, "Failed map broadcast address\n");
		return PTR_ERR(cmem_lat_pmu->base_broadcast);
	}

	ret = cmem_lat_pmu_get_cpus(cmem_lat_pmu, socket);
	if (ret)
		return ret;

	ret = cpuhp_state_add_instance(cmem_lat_pmu_cpuhp_state,
				       &cmem_lat_pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug\n", ret);
		return ret;
	}

	cmem_lat_pmu_cg_ctrl(cmem_lat_pmu, CMEM_LAT_CG_CTRL_ENABLE);
	cmem_lat_pmu_ctrl(cmem_lat_pmu, CMEM_LAT_CTRL_CLR);
	cmem_lat_pmu_cg_ctrl(cmem_lat_pmu, CMEM_LAT_CG_CTRL_DISABLE);

	ret = perf_pmu_register(&cmem_lat_pmu->pmu, name, -1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register PMU: %d\n", ret);
		cpuhp_state_remove_instance(cmem_lat_pmu_cpuhp_state,
					    &cmem_lat_pmu->node);
		return ret;
	}

	dev_dbg(&pdev->dev, "Registered %s PMU\n", name);

	return 0;
}

static void cmem_lat_pmu_device_remove(struct platform_device *pdev)
{
	struct cmem_lat_pmu *cmem_lat_pmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&cmem_lat_pmu->pmu);
	cpuhp_state_remove_instance(cmem_lat_pmu_cpuhp_state,
				    &cmem_lat_pmu->node);
}

static const struct acpi_device_id cmem_lat_pmu_acpi_match[] = {
	{ "NVDA2021" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cmem_lat_pmu_acpi_match);

static struct platform_driver cmem_lat_pmu_driver = {
	.driver = {
		.name = "nvidia-t410-cmem-latency-pmu",
		.acpi_match_table = ACPI_PTR(cmem_lat_pmu_acpi_match),
		.suppress_bind_attrs = true,
	},
	.probe = cmem_lat_pmu_probe,
	.remove = cmem_lat_pmu_device_remove,
};

static int __init cmem_lat_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "perf/nvidia/cmem_latency:online",
				      cmem_lat_pmu_cpu_online,
				      cmem_lat_pmu_cpu_teardown);
	if (ret < 0)
		return ret;

	cmem_lat_pmu_cpuhp_state = ret;

	return platform_driver_register(&cmem_lat_pmu_driver);
}

static void __exit cmem_lat_pmu_exit(void)
{
	platform_driver_unregister(&cmem_lat_pmu_driver);
	cpuhp_remove_multi_state(cmem_lat_pmu_cpuhp_state);
}

module_init(cmem_lat_pmu_init);
module_exit(cmem_lat_pmu_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NVIDIA Tegra410 CPU Memory Latency PMU driver");
MODULE_AUTHOR("Besar Wicaksono <bwicaksono@nvidia.com>");
