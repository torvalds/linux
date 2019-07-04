// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2017 NXP
 * Copyright 2016 Freescale Semiconductor, Inc.
 */

#include <linux/bitfield.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/perf_event.h>
#include <linux/slab.h>

#define COUNTER_CNTL		0x0
#define COUNTER_READ		0x20

#define COUNTER_DPCR1		0x30

#define CNTL_OVER		0x1
#define CNTL_CLEAR		0x2
#define CNTL_EN			0x4
#define CNTL_EN_MASK		0xFFFFFFFB
#define CNTL_CLEAR_MASK		0xFFFFFFFD
#define CNTL_OVER_MASK		0xFFFFFFFE

#define CNTL_CSV_SHIFT		24
#define CNTL_CSV_MASK		(0xFF << CNTL_CSV_SHIFT)

#define EVENT_CYCLES_ID		0
#define EVENT_CYCLES_COUNTER	0
#define NUM_COUNTERS		4

#define to_ddr_pmu(p)		container_of(p, struct ddr_pmu, pmu)

#define DDR_PERF_DEV_NAME	"imx8_ddr"
#define DDR_CPUHP_CB_NAME	DDR_PERF_DEV_NAME "_perf_pmu"

static DEFINE_IDA(ddr_ida);

static const struct of_device_id imx_ddr_pmu_dt_ids[] = {
	{ .compatible = "fsl,imx8-ddr-pmu",},
	{ .compatible = "fsl,imx8m-ddr-pmu",},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_ddr_pmu_dt_ids);

struct ddr_pmu {
	struct pmu pmu;
	void __iomem *base;
	unsigned int cpu;
	struct	hlist_node node;
	struct	device *dev;
	struct perf_event *events[NUM_COUNTERS];
	int active_events;
	enum cpuhp_state cpuhp_state;
	int irq;
	int id;
};

static ssize_t ddr_perf_cpumask_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev);

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(pmu->cpu));
}

static struct device_attribute ddr_perf_cpumask_attr =
	__ATTR(cpumask, 0444, ddr_perf_cpumask_show, NULL);

static struct attribute *ddr_perf_cpumask_attrs[] = {
	&ddr_perf_cpumask_attr.attr,
	NULL,
};

static struct attribute_group ddr_perf_cpumask_attr_group = {
	.attrs = ddr_perf_cpumask_attrs,
};

static ssize_t
ddr_pmu_event_show(struct device *dev, struct device_attribute *attr,
		   char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sprintf(page, "event=0x%02llx\n", pmu_attr->id);
}

#define IMX8_DDR_PMU_EVENT_ATTR(_name, _id)				\
	(&((struct perf_pmu_events_attr[]) {				\
		{ .attr = __ATTR(_name, 0444, ddr_pmu_event_show, NULL),\
		  .id = _id, }						\
	})[0].attr.attr)

static struct attribute *ddr_perf_events_attrs[] = {
	IMX8_DDR_PMU_EVENT_ATTR(cycles, EVENT_CYCLES_ID),
	IMX8_DDR_PMU_EVENT_ATTR(selfresh, 0x01),
	IMX8_DDR_PMU_EVENT_ATTR(read-accesses, 0x04),
	IMX8_DDR_PMU_EVENT_ATTR(write-accesses, 0x05),
	IMX8_DDR_PMU_EVENT_ATTR(read-queue-depth, 0x08),
	IMX8_DDR_PMU_EVENT_ATTR(write-queue-depth, 0x09),
	IMX8_DDR_PMU_EVENT_ATTR(lp-read-credit-cnt, 0x10),
	IMX8_DDR_PMU_EVENT_ATTR(hp-read-credit-cnt, 0x11),
	IMX8_DDR_PMU_EVENT_ATTR(write-credit-cnt, 0x12),
	IMX8_DDR_PMU_EVENT_ATTR(read-command, 0x20),
	IMX8_DDR_PMU_EVENT_ATTR(write-command, 0x21),
	IMX8_DDR_PMU_EVENT_ATTR(read-modify-write-command, 0x22),
	IMX8_DDR_PMU_EVENT_ATTR(hp-read, 0x23),
	IMX8_DDR_PMU_EVENT_ATTR(hp-req-nocredit, 0x24),
	IMX8_DDR_PMU_EVENT_ATTR(hp-xact-credit, 0x25),
	IMX8_DDR_PMU_EVENT_ATTR(lp-req-nocredit, 0x26),
	IMX8_DDR_PMU_EVENT_ATTR(lp-xact-credit, 0x27),
	IMX8_DDR_PMU_EVENT_ATTR(wr-xact-credit, 0x29),
	IMX8_DDR_PMU_EVENT_ATTR(read-cycles, 0x2a),
	IMX8_DDR_PMU_EVENT_ATTR(write-cycles, 0x2b),
	IMX8_DDR_PMU_EVENT_ATTR(read-write-transition, 0x30),
	IMX8_DDR_PMU_EVENT_ATTR(precharge, 0x31),
	IMX8_DDR_PMU_EVENT_ATTR(activate, 0x32),
	IMX8_DDR_PMU_EVENT_ATTR(load-mode, 0x33),
	IMX8_DDR_PMU_EVENT_ATTR(perf-mwr, 0x34),
	IMX8_DDR_PMU_EVENT_ATTR(read, 0x35),
	IMX8_DDR_PMU_EVENT_ATTR(read-activate, 0x36),
	IMX8_DDR_PMU_EVENT_ATTR(refresh, 0x37),
	IMX8_DDR_PMU_EVENT_ATTR(write, 0x38),
	IMX8_DDR_PMU_EVENT_ATTR(raw-hazard, 0x39),
	NULL,
};

static struct attribute_group ddr_perf_events_attr_group = {
	.name = "events",
	.attrs = ddr_perf_events_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-7");

static struct attribute *ddr_perf_format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group ddr_perf_format_attr_group = {
	.name = "format",
	.attrs = ddr_perf_format_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&ddr_perf_events_attr_group,
	&ddr_perf_format_attr_group,
	&ddr_perf_cpumask_attr_group,
	NULL,
};

static u32 ddr_perf_alloc_counter(struct ddr_pmu *pmu, int event)
{
	int i;

	/*
	 * Always map cycle event to counter 0
	 * Cycles counter is dedicated for cycle event
	 * can't used for the other events
	 */
	if (event == EVENT_CYCLES_ID) {
		if (pmu->events[EVENT_CYCLES_COUNTER] == NULL)
			return EVENT_CYCLES_COUNTER;
		else
			return -ENOENT;
	}

	for (i = 1; i < NUM_COUNTERS; i++) {
		if (pmu->events[i] == NULL)
			return i;
	}

	return -ENOENT;
}

static void ddr_perf_free_counter(struct ddr_pmu *pmu, int counter)
{
	pmu->events[counter] = NULL;
}

static u32 ddr_perf_read_counter(struct ddr_pmu *pmu, int counter)
{
	return readl_relaxed(pmu->base + COUNTER_READ + counter * 4);
}

static int ddr_perf_event_init(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	struct perf_event *sibling;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;

	if (event->cpu < 0) {
		dev_warn(pmu->dev, "Can't provide per-task data!\n");
		return -EOPNOTSUPP;
	}

	/*
	 * We must NOT create groups containing mixed PMUs, although software
	 * events are acceptable (for example to create a CCN group
	 * periodically read when a hrtimer aka cpu-clock leader triggers).
	 */
	if (event->group_leader->pmu != event->pmu &&
			!is_software_event(event->group_leader))
		return -EINVAL;

	for_each_sibling_event(sibling, event->group_leader) {
		if (sibling->pmu != event->pmu &&
				!is_software_event(sibling))
			return -EINVAL;
	}

	event->cpu = pmu->cpu;
	hwc->idx = -1;

	return 0;
}


static void ddr_perf_event_update(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev_raw_count, new_raw_count;
	int counter = hwc->idx;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = ddr_perf_read_counter(pmu, counter);
	} while (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			new_raw_count) != prev_raw_count);

	delta = (new_raw_count - prev_raw_count) & 0xFFFFFFFF;

	local64_add(delta, &event->count);
}

static void ddr_perf_counter_enable(struct ddr_pmu *pmu, int config,
				  int counter, bool enable)
{
	u8 reg = counter * 4 + COUNTER_CNTL;
	int val;

	if (enable) {
		/*
		 * must disable first, then enable again
		 * otherwise, cycle counter will not work
		 * if previous state is enabled.
		 */
		writel(0, pmu->base + reg);
		val = CNTL_EN | CNTL_CLEAR;
		val |= FIELD_PREP(CNTL_CSV_MASK, config);
		writel(val, pmu->base + reg);
	} else {
		/* Disable counter */
		writel(0, pmu->base + reg);
	}
}

static void ddr_perf_event_start(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	local64_set(&hwc->prev_count, 0);

	ddr_perf_counter_enable(pmu, event->attr.config, counter, true);

	hwc->state = 0;
}

static int ddr_perf_event_add(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter;
	int cfg = event->attr.config;

	counter = ddr_perf_alloc_counter(pmu, cfg);
	if (counter < 0) {
		dev_dbg(pmu->dev, "There are not enough counters\n");
		return -EOPNOTSUPP;
	}

	pmu->events[counter] = event;
	pmu->active_events++;
	hwc->idx = counter;

	hwc->state |= PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		ddr_perf_event_start(event, flags);

	return 0;
}

static void ddr_perf_event_stop(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	ddr_perf_counter_enable(pmu, event->attr.config, counter, false);
	ddr_perf_event_update(event);

	hwc->state |= PERF_HES_STOPPED;
}

static void ddr_perf_event_del(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	ddr_perf_event_stop(event, PERF_EF_UPDATE);

	ddr_perf_free_counter(pmu, counter);
	pmu->active_events--;
	hwc->idx = -1;
}

static void ddr_perf_pmu_enable(struct pmu *pmu)
{
	struct ddr_pmu *ddr_pmu = to_ddr_pmu(pmu);

	/* enable cycle counter if cycle is not active event list */
	if (ddr_pmu->events[EVENT_CYCLES_COUNTER] == NULL)
		ddr_perf_counter_enable(ddr_pmu,
				      EVENT_CYCLES_ID,
				      EVENT_CYCLES_COUNTER,
				      true);
}

static void ddr_perf_pmu_disable(struct pmu *pmu)
{
	struct ddr_pmu *ddr_pmu = to_ddr_pmu(pmu);

	if (ddr_pmu->events[EVENT_CYCLES_COUNTER] == NULL)
		ddr_perf_counter_enable(ddr_pmu,
				      EVENT_CYCLES_ID,
				      EVENT_CYCLES_COUNTER,
				      false);
}

static int ddr_perf_init(struct ddr_pmu *pmu, void __iomem *base,
			 struct device *dev)
{
	*pmu = (struct ddr_pmu) {
		.pmu = (struct pmu) {
			.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
			.task_ctx_nr = perf_invalid_context,
			.attr_groups = attr_groups,
			.event_init  = ddr_perf_event_init,
			.add	     = ddr_perf_event_add,
			.del	     = ddr_perf_event_del,
			.start	     = ddr_perf_event_start,
			.stop	     = ddr_perf_event_stop,
			.read	     = ddr_perf_event_update,
			.pmu_enable  = ddr_perf_pmu_enable,
			.pmu_disable = ddr_perf_pmu_disable,
		},
		.base = base,
		.dev = dev,
	};

	pmu->id = ida_simple_get(&ddr_ida, 0, 0, GFP_KERNEL);
	return pmu->id;
}

static irqreturn_t ddr_perf_irq_handler(int irq, void *p)
{
	int i;
	struct ddr_pmu *pmu = (struct ddr_pmu *) p;
	struct perf_event *event, *cycle_event = NULL;

	/* all counter will stop if cycle counter disabled */
	ddr_perf_counter_enable(pmu,
			      EVENT_CYCLES_ID,
			      EVENT_CYCLES_COUNTER,
			      false);
	/*
	 * When the cycle counter overflows, all counters are stopped,
	 * and an IRQ is raised. If any other counter overflows, it
	 * continues counting, and no IRQ is raised.
	 *
	 * Cycles occur at least 4 times as often as other events, so we
	 * can update all events on a cycle counter overflow and not
	 * lose events.
	 *
	 */
	for (i = 0; i < NUM_COUNTERS; i++) {

		if (!pmu->events[i])
			continue;

		event = pmu->events[i];

		ddr_perf_event_update(event);

		if (event->hw.idx == EVENT_CYCLES_COUNTER)
			cycle_event = event;
	}

	ddr_perf_counter_enable(pmu,
			      EVENT_CYCLES_ID,
			      EVENT_CYCLES_COUNTER,
			      true);
	if (cycle_event)
		ddr_perf_event_update(cycle_event);

	return IRQ_HANDLED;
}

static int ddr_perf_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct ddr_pmu *pmu = hlist_entry_safe(node, struct ddr_pmu, node);
	int target;

	if (cpu != pmu->cpu)
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&pmu->pmu, cpu, target);
	pmu->cpu = target;

	WARN_ON(irq_set_affinity_hint(pmu->irq, cpumask_of(pmu->cpu)));

	return 0;
}

static int ddr_perf_probe(struct platform_device *pdev)
{
	struct ddr_pmu *pmu;
	struct device_node *np;
	void __iomem *base;
	char *name;
	int num;
	int ret;
	int irq;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	np = pdev->dev.of_node;

	pmu = devm_kzalloc(&pdev->dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	num = ddr_perf_init(pmu, base, &pdev->dev);

	platform_set_drvdata(pdev, pmu);

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, DDR_PERF_DEV_NAME "%d",
			      num);
	if (!name)
		return -ENOMEM;

	pmu->cpu = raw_smp_processor_id();
	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      DDR_CPUHP_CB_NAME,
				      NULL,
				      ddr_perf_offline_cpu);

	if (ret < 0) {
		dev_err(&pdev->dev, "cpuhp_setup_state_multi failed\n");
		goto ddr_perf_err;
	}

	pmu->cpuhp_state = ret;

	/* Register the pmu instance for cpu hotplug */
	cpuhp_state_add_instance_nocalls(pmu->cpuhp_state, &pmu->node);

	/* Request irq */
	irq = of_irq_get(np, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get irq: %d", irq);
		ret = irq;
		goto ddr_perf_err;
	}

	ret = devm_request_irq(&pdev->dev, irq,
					ddr_perf_irq_handler,
					IRQF_NOBALANCING | IRQF_NO_THREAD,
					DDR_CPUHP_CB_NAME,
					pmu);
	if (ret < 0) {
		dev_err(&pdev->dev, "Request irq failed: %d", ret);
		goto ddr_perf_err;
	}

	pmu->irq = irq;
	ret = irq_set_affinity_hint(pmu->irq, cpumask_of(pmu->cpu));
	if (ret) {
		dev_err(pmu->dev, "Failed to set interrupt affinity!\n");
		goto ddr_perf_err;
	}

	ret = perf_pmu_register(&pmu->pmu, name, -1);
	if (ret)
		goto ddr_perf_err;

	return 0;

ddr_perf_err:
	if (pmu->cpuhp_state)
		cpuhp_state_remove_instance_nocalls(pmu->cpuhp_state, &pmu->node);

	ida_simple_remove(&ddr_ida, pmu->id);
	dev_warn(&pdev->dev, "i.MX8 DDR Perf PMU failed (%d), disabled\n", ret);
	return ret;
}

static int ddr_perf_remove(struct platform_device *pdev)
{
	struct ddr_pmu *pmu = platform_get_drvdata(pdev);

	cpuhp_state_remove_instance_nocalls(pmu->cpuhp_state, &pmu->node);
	irq_set_affinity_hint(pmu->irq, NULL);

	perf_pmu_unregister(&pmu->pmu);

	ida_simple_remove(&ddr_ida, pmu->id);
	return 0;
}

static struct platform_driver imx_ddr_pmu_driver = {
	.driver         = {
		.name   = "imx-ddr-pmu",
		.of_match_table = imx_ddr_pmu_dt_ids,
	},
	.probe          = ddr_perf_probe,
	.remove         = ddr_perf_remove,
};

module_platform_driver(imx_ddr_pmu_driver);
MODULE_LICENSE("GPL v2");
