// SPDX-License-Identifier: GPL-2.0-only
/*
 * StarFive's StarLink PMU driver
 *
 * Copyright (C) 2023 StarFive Technology Co., Ltd.
 *
 * Author: Ji Sheng Teoh <jisheng.teoh@starfivetech.com>
 *
 */

#define STARLINK_PMU_PDEV_NAME	"starfive_starlink_pmu"
#define pr_fmt(fmt)	STARLINK_PMU_PDEV_NAME ": " fmt

#include <linux/bitmap.h>
#include <linux/cpu_pm.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#define STARLINK_PMU_MAX_COUNTERS			64
#define STARLINK_PMU_NUM_COUNTERS			16
#define STARLINK_PMU_IDX_CYCLE_COUNTER			63

#define STARLINK_PMU_EVENT_SELECT			0x060
#define STARLINK_PMU_EVENT_COUNTER			0x160
#define STARLINK_PMU_COUNTER_MASK			GENMASK_ULL(63, 0)
#define STARLINK_PMU_CYCLE_COUNTER			0x058

#define STARLINK_PMU_CONTROL				0x040
#define STARLINK_PMU_GLOBAL_ENABLE			BIT_ULL(0)

#define STARLINK_PMU_INTERRUPT_ENABLE			0x050
#define STARLINK_PMU_COUNTER_OVERFLOW_STATUS		0x048
#define STARLINK_PMU_CYCLE_OVERFLOW_MASK		BIT_ULL(63)

#define STARLINK_CYCLES				0x058
#define CACHE_READ_REQUEST			0x04000701
#define CACHE_WRITE_REQUEST			0x03000001
#define CACHE_RELEASE_REQUEST			0x0003e001
#define CACHE_READ_HIT				0x00901202
#define CACHE_READ_MISS				0x04008002
#define CACHE_WRITE_HIT				0x006c0002
#define CACHE_WRITE_MISS			0x03000002
#define CACHE_WRITEBACK				0x00000403

#define to_starlink_pmu(p) (container_of(p, struct starlink_pmu, pmu))

#define STARLINK_FORMAT_ATTR(_name, _config)				      \
	(&((struct dev_ext_attribute[]) {				      \
		{ .attr = __ATTR(_name, 0444, starlink_pmu_sysfs_format_show, NULL), \
		  .var = (void *)_config, }				      \
	})[0].attr.attr)

#define STARLINK_EVENT_ATTR(_name, _id)					     \
	PMU_EVENT_ATTR_ID(_name, starlink_pmu_sysfs_event_show, _id)

static int starlink_pmu_cpuhp_state;

struct starlink_hw_events {
	struct perf_event	*events[STARLINK_PMU_MAX_COUNTERS];
	DECLARE_BITMAP(used_mask, STARLINK_PMU_MAX_COUNTERS);
};

struct starlink_pmu {
	struct pmu					pmu;
	struct starlink_hw_events			__percpu *hw_events;
	struct hlist_node				node;
	struct notifier_block				starlink_pmu_pm_nb;
	void __iomem					*pmu_base;
	cpumask_t					cpumask;
	int						irq;
};

static ssize_t
starlink_pmu_sysfs_format_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct dev_ext_attribute *eattr = container_of(attr,
						       struct dev_ext_attribute, attr);

	return sysfs_emit(buf, "%s\n", (char *)eattr->var);
}

static struct attribute *starlink_pmu_format_attrs[] = {
	STARLINK_FORMAT_ATTR(event, "config:0-31"),
	NULL
};

static const struct attribute_group starlink_pmu_format_attr_group = {
	.name = "format",
	.attrs = starlink_pmu_format_attrs,
};

static ssize_t
starlink_pmu_sysfs_event_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct perf_pmu_events_attr *eattr = container_of(attr,
							  struct perf_pmu_events_attr, attr);

	return sysfs_emit(buf, "event=0x%02llx\n", eattr->id);
}

static struct attribute *starlink_pmu_event_attrs[] = {
	STARLINK_EVENT_ATTR(cycles, STARLINK_CYCLES),
	STARLINK_EVENT_ATTR(read_request, CACHE_READ_REQUEST),
	STARLINK_EVENT_ATTR(write_request, CACHE_WRITE_REQUEST),
	STARLINK_EVENT_ATTR(release_request, CACHE_RELEASE_REQUEST),
	STARLINK_EVENT_ATTR(read_hit, CACHE_READ_HIT),
	STARLINK_EVENT_ATTR(read_miss, CACHE_READ_MISS),
	STARLINK_EVENT_ATTR(write_hit, CACHE_WRITE_HIT),
	STARLINK_EVENT_ATTR(write_miss, CACHE_WRITE_MISS),
	STARLINK_EVENT_ATTR(writeback, CACHE_WRITEBACK),
	NULL
};

static const struct attribute_group starlink_pmu_events_attr_group = {
	.name = "events",
	.attrs = starlink_pmu_event_attrs,
};

static ssize_t
cpumask_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct starlink_pmu *starlink_pmu = to_starlink_pmu(dev_get_drvdata(dev));

	return cpumap_print_to_pagebuf(true, buf, &starlink_pmu->cpumask);
}

static DEVICE_ATTR_RO(cpumask);

static struct attribute *starlink_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static const struct attribute_group starlink_pmu_cpumask_attr_group = {
	.attrs = starlink_pmu_cpumask_attrs,
};

static const struct attribute_group *starlink_pmu_attr_groups[] = {
	&starlink_pmu_format_attr_group,
	&starlink_pmu_events_attr_group,
	&starlink_pmu_cpumask_attr_group,
	NULL
};

static void starlink_pmu_set_event_period(struct perf_event *event)
{
	struct starlink_pmu *starlink_pmu = to_starlink_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = event->hw.idx;

	/*
	 * Program counter to half of it's max count to handle
	 * cases of extreme interrupt latency.
	 */
	u64 val = STARLINK_PMU_COUNTER_MASK >> 1;

	local64_set(&hwc->prev_count, val);
	if (hwc->config == STARLINK_CYCLES)
		writeq(val, starlink_pmu->pmu_base + STARLINK_PMU_CYCLE_COUNTER);
	else
		writeq(val, starlink_pmu->pmu_base + STARLINK_PMU_EVENT_COUNTER +
		       idx * sizeof(u64));
}

static void starlink_pmu_counter_start(struct perf_event *event,
				       struct starlink_pmu *starlink_pmu)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = event->hw.idx;
	u64 val;

	/*
	 * Enable counter overflow interrupt[63:0],
	 * which is mapped as follow:
	 *
	 * event counter 0	- Bit [0]
	 * event counter 1	- Bit [1]
	 * ...
	 * cycle counter	- Bit [63]
	 */
	val = readq(starlink_pmu->pmu_base + STARLINK_PMU_INTERRUPT_ENABLE);

	if (hwc->config == STARLINK_CYCLES) {
		/*
		 * Cycle count has its dedicated register, and it starts
		 * counting as soon as STARLINK_PMU_GLOBAL_ENABLE is set.
		 */
		val |= STARLINK_PMU_CYCLE_OVERFLOW_MASK;
	} else {
		writeq(event->hw.config, starlink_pmu->pmu_base +
		       STARLINK_PMU_EVENT_SELECT + idx * sizeof(u64));

		val |= BIT_ULL(idx);
	}

	writeq(val, starlink_pmu->pmu_base + STARLINK_PMU_INTERRUPT_ENABLE);

	writeq(STARLINK_PMU_GLOBAL_ENABLE, starlink_pmu->pmu_base +
	       STARLINK_PMU_CONTROL);
}

static void starlink_pmu_counter_stop(struct perf_event *event,
				      struct starlink_pmu *starlink_pmu)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = event->hw.idx;
	u64 val;

	val = readq(starlink_pmu->pmu_base + STARLINK_PMU_CONTROL);
	val &= ~STARLINK_PMU_GLOBAL_ENABLE;
	writeq(val, starlink_pmu->pmu_base + STARLINK_PMU_CONTROL);

	val = readq(starlink_pmu->pmu_base + STARLINK_PMU_INTERRUPT_ENABLE);
	if (hwc->config == STARLINK_CYCLES)
		val &= ~STARLINK_PMU_CYCLE_OVERFLOW_MASK;
	else
		val &= ~BIT_ULL(idx);

	writeq(val, starlink_pmu->pmu_base + STARLINK_PMU_INTERRUPT_ENABLE);
}

static void starlink_pmu_update(struct perf_event *event)
{
	struct starlink_pmu *starlink_pmu = to_starlink_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	u64 prev_raw_count, new_raw_count;
	u64 oldval;
	u64 delta;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		if (hwc->config == STARLINK_CYCLES)
			new_raw_count = readq(starlink_pmu->pmu_base +
					      STARLINK_PMU_CYCLE_COUNTER);
		else
			new_raw_count = readq(starlink_pmu->pmu_base +
					      STARLINK_PMU_EVENT_COUNTER +
					      idx * sizeof(u64));
		oldval = local64_cmpxchg(&hwc->prev_count, prev_raw_count,
					 new_raw_count);
	} while (oldval != prev_raw_count);

	delta = (new_raw_count - prev_raw_count) & STARLINK_PMU_COUNTER_MASK;
	local64_add(delta, &event->count);
}

static void starlink_pmu_start(struct perf_event *event, int flags)
{
	struct starlink_pmu *starlink_pmu = to_starlink_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	hwc->state = 0;

	starlink_pmu_set_event_period(event);
	starlink_pmu_counter_start(event, starlink_pmu);

	perf_event_update_userpage(event);
}

static void starlink_pmu_stop(struct perf_event *event, int flags)
{
	struct starlink_pmu *starlink_pmu = to_starlink_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->state & PERF_HES_STOPPED)
		return;

	starlink_pmu_counter_stop(event, starlink_pmu);
	starlink_pmu_update(event);
	hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static int starlink_pmu_add(struct perf_event *event, int flags)
{
	struct starlink_pmu *starlink_pmu = to_starlink_pmu(event->pmu);
	struct starlink_hw_events *hw_events =
					this_cpu_ptr(starlink_pmu->hw_events);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long *used_mask = hw_events->used_mask;
	u32 n_events = STARLINK_PMU_NUM_COUNTERS;
	int idx;

	/*
	 * Cycle counter has dedicated register to hold counter value.
	 * Event other than cycle count has to be enabled through
	 * event select register, and assigned with independent counter
	 * as they appear.
	 */

	if (hwc->config == STARLINK_CYCLES) {
		idx = STARLINK_PMU_IDX_CYCLE_COUNTER;
	} else {
		idx = find_first_zero_bit(used_mask, n_events);
		/* All counter are in use */
		if (idx < 0)
			return idx;

		set_bit(idx, used_mask);
	}

	hwc->idx = idx;
	hw_events->events[idx] = event;
	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		starlink_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);

	return 0;
}

static void starlink_pmu_del(struct perf_event *event, int flags)
{
	struct starlink_pmu *starlink_pmu = to_starlink_pmu(event->pmu);
	struct starlink_hw_events *hw_events =
					this_cpu_ptr(starlink_pmu->hw_events);
	struct hw_perf_event *hwc = &event->hw;

	starlink_pmu_stop(event, PERF_EF_UPDATE);
	hw_events->events[hwc->idx] = NULL;
	clear_bit(hwc->idx, hw_events->used_mask);

	perf_event_update_userpage(event);
}

static bool starlink_pmu_validate_event_group(struct perf_event *event)
{
	struct perf_event *leader = event->group_leader;
	struct perf_event *sibling;
	int counter = 1;

	/*
	 * Ensure hardware events in the group are on the same PMU,
	 * software events are acceptable.
	 */
	if (event->group_leader->pmu != event->pmu &&
	    !is_software_event(event->group_leader))
		return false;

	for_each_sibling_event(sibling, leader) {
		if (sibling->pmu != event->pmu && !is_software_event(sibling))
			return false;

		counter++;
	}

	return counter <= STARLINK_PMU_NUM_COUNTERS;
}

static int starlink_pmu_event_init(struct perf_event *event)
{
	struct starlink_pmu *starlink_pmu = to_starlink_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	/*
	 * Sampling is not supported, as counters are shared
	 * by all CPU.
	 */
	if (hwc->sample_period)
		return -EOPNOTSUPP;

	/*
	 * Per-task and attach to a task are not supported,
	 * as uncore events are not specific to any CPU.
	 */
	if (event->cpu < 0 || event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;

	if (!starlink_pmu_validate_event_group(event))
		return -EINVAL;

	hwc->idx = -1;
	hwc->config = event->attr.config;
	event->cpu = cpumask_first(&starlink_pmu->cpumask);

	return 0;
}

static irqreturn_t starlink_pmu_handle_irq(int irq_num, void *data)
{
	struct starlink_pmu *starlink_pmu = data;
	struct starlink_hw_events *hw_events =
			this_cpu_ptr(starlink_pmu->hw_events);
	bool handled = false;
	int idx;
	u64 overflow_status;

	for (idx = 0; idx < STARLINK_PMU_MAX_COUNTERS; idx++) {
		struct perf_event *event = hw_events->events[idx];

		if (!event)
			continue;

		overflow_status = readq(starlink_pmu->pmu_base +
					STARLINK_PMU_COUNTER_OVERFLOW_STATUS);
		if (!(overflow_status & BIT_ULL(idx)))
			continue;

		writeq(BIT_ULL(idx), starlink_pmu->pmu_base +
		       STARLINK_PMU_COUNTER_OVERFLOW_STATUS);

		starlink_pmu_update(event);
		starlink_pmu_set_event_period(event);
		handled = true;
	}
	return IRQ_RETVAL(handled);
}

static int starlink_setup_irqs(struct starlink_pmu *starlink_pmu,
			       struct platform_device *pdev)
{
	int ret, irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;

	ret = devm_request_irq(&pdev->dev, irq, starlink_pmu_handle_irq,
			       0, STARLINK_PMU_PDEV_NAME, starlink_pmu);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to request IRQ\n");

	starlink_pmu->irq = irq;

	return 0;
}

static int starlink_pmu_pm_notify(struct notifier_block *b,
				  unsigned long cmd, void *v)
{
	struct starlink_pmu *starlink_pmu = container_of(b, struct starlink_pmu,
							 starlink_pmu_pm_nb);
	struct starlink_hw_events *hw_events =
					this_cpu_ptr(starlink_pmu->hw_events);
	int enabled = bitmap_weight(hw_events->used_mask,
				    STARLINK_PMU_MAX_COUNTERS);
	struct perf_event *event;
	int idx;

	if (!enabled)
		return NOTIFY_OK;

	for (idx = 0; idx < STARLINK_PMU_MAX_COUNTERS; idx++) {
		event = hw_events->events[idx];
		if (!event)
			continue;

		switch (cmd) {
		case CPU_PM_ENTER:
			/* Stop and update the counter */
			starlink_pmu_stop(event, PERF_EF_UPDATE);
			break;
		case CPU_PM_EXIT:
		case CPU_PM_ENTER_FAILED:
			/* Restore and enable the counter */
			starlink_pmu_start(event, PERF_EF_RELOAD);
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static int starlink_pmu_pm_register(struct starlink_pmu *starlink_pmu)
{
	if (!IS_ENABLED(CONFIG_CPU_PM))
		return 0;

	starlink_pmu->starlink_pmu_pm_nb.notifier_call = starlink_pmu_pm_notify;
	return cpu_pm_register_notifier(&starlink_pmu->starlink_pmu_pm_nb);
}

static void starlink_pmu_pm_unregister(struct starlink_pmu *starlink_pmu)
{
	if (!IS_ENABLED(CONFIG_CPU_PM))
		return;

	cpu_pm_unregister_notifier(&starlink_pmu->starlink_pmu_pm_nb);
}

static void starlink_pmu_destroy(struct starlink_pmu *starlink_pmu)
{
	starlink_pmu_pm_unregister(starlink_pmu);
	cpuhp_state_remove_instance(starlink_pmu_cpuhp_state,
				    &starlink_pmu->node);
}

static int starlink_pmu_probe(struct platform_device *pdev)
{
	struct starlink_pmu *starlink_pmu;
	struct starlink_hw_events *hw_events;
	struct resource *res;
	int cpuid, i, ret;

	starlink_pmu = devm_kzalloc(&pdev->dev, sizeof(*starlink_pmu), GFP_KERNEL);
	if (!starlink_pmu)
		return -ENOMEM;

	starlink_pmu->pmu_base =
			devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(starlink_pmu->pmu_base))
		return PTR_ERR(starlink_pmu->pmu_base);

	starlink_pmu->hw_events = alloc_percpu_gfp(struct starlink_hw_events,
						   GFP_KERNEL);
	if (!starlink_pmu->hw_events) {
		dev_err(&pdev->dev, "Failed to allocate per-cpu PMU data\n");
		return -ENOMEM;
	}

	for_each_possible_cpu(cpuid) {
		hw_events = per_cpu_ptr(starlink_pmu->hw_events, cpuid);
		for (i = 0; i < STARLINK_PMU_MAX_COUNTERS; i++)
			hw_events->events[i] = NULL;
	}

	ret = starlink_setup_irqs(starlink_pmu, pdev);
	if (ret)
		return ret;

	ret = cpuhp_state_add_instance(starlink_pmu_cpuhp_state,
				       &starlink_pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register hotplug\n");
		return ret;
	}

	ret = starlink_pmu_pm_register(starlink_pmu);
	if (ret) {
		cpuhp_state_remove_instance(starlink_pmu_cpuhp_state,
					    &starlink_pmu->node);
		return ret;
	}

	starlink_pmu->pmu = (struct pmu) {
		.task_ctx_nr	= perf_invalid_context,
		.event_init	= starlink_pmu_event_init,
		.add		= starlink_pmu_add,
		.del		= starlink_pmu_del,
		.start		= starlink_pmu_start,
		.stop		= starlink_pmu_stop,
		.read		= starlink_pmu_update,
		.attr_groups	= starlink_pmu_attr_groups,
	};

	ret = perf_pmu_register(&starlink_pmu->pmu, STARLINK_PMU_PDEV_NAME, -1);
	if (ret)
		starlink_pmu_destroy(starlink_pmu);

	return ret;
}

static const struct of_device_id starlink_pmu_of_match[] = {
	{ .compatible = "starfive,jh8100-starlink-pmu" },
	{}
};
MODULE_DEVICE_TABLE(of, starlink_pmu_of_match);

static struct platform_driver starlink_pmu_driver = {
	.driver = {
		.name	= STARLINK_PMU_PDEV_NAME,
		.of_match_table = starlink_pmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = starlink_pmu_probe,
};

static int
starlink_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct starlink_pmu *starlink_pmu = hlist_entry_safe(node,
							     struct starlink_pmu,
							     node);

	if (cpumask_empty(&starlink_pmu->cpumask))
		cpumask_set_cpu(cpu, &starlink_pmu->cpumask);

	WARN_ON(irq_set_affinity(starlink_pmu->irq, cpumask_of(cpu)));

	return 0;
}

static int
starlink_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct starlink_pmu *starlink_pmu = hlist_entry_safe(node,
							     struct starlink_pmu,
							     node);
	unsigned int target;

	if (!cpumask_test_and_clear_cpu(cpu, &starlink_pmu->cpumask))
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&starlink_pmu->pmu, cpu, target);

	cpumask_set_cpu(target, &starlink_pmu->cpumask);
	WARN_ON(irq_set_affinity(starlink_pmu->irq, cpumask_of(target)));

	return 0;
}

static int __init starlink_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "soc/starfive/starlink_pmu:online",
				      starlink_pmu_online_cpu,
				      starlink_pmu_offline_cpu);
	if (ret < 0)
		return ret;

	starlink_pmu_cpuhp_state = ret;

	return platform_driver_register(&starlink_pmu_driver);
}

device_initcall(starlink_pmu_init);
