// SPDX-License-Identifier: GPL-2.0
// Copyright 2023 NXP

#include <linux/bitfield.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/perf_event.h>

/* Performance monitor configuration */
#define PMCFG1  			0x00
#define PMCFG1_RD_TRANS_FILT_EN 	BIT(31)
#define PMCFG1_WR_TRANS_FILT_EN 	BIT(30)
#define PMCFG1_RD_BT_FILT_EN 		BIT(29)
#define PMCFG1_ID_MASK  		GENMASK(17, 0)

#define PMCFG2  			0x04
#define PMCFG2_ID			GENMASK(17, 0)

/* Global control register affects all counters and takes priority over local control registers */
#define PMGC0		0x40
/* Global control register bits */
#define PMGC0_FAC	BIT(31)
#define PMGC0_PMIE	BIT(30)
#define PMGC0_FCECE	BIT(29)

/*
 * 64bit counter0 exclusively dedicated to counting cycles
 * 32bit counters monitor counter-specific events in addition to counting reference events
 */
#define PMLCA(n)	(0x40 + 0x10 + (0x10 * n))
#define PMLCB(n)	(0x40 + 0x14 + (0x10 * n))
#define PMC(n)		(0x40 + 0x18 + (0x10 * n))
/* Local control register bits */
#define PMLCA_FC	BIT(31)
#define PMLCA_CE	BIT(26)
#define PMLCA_EVENT	GENMASK(22, 16)

#define NUM_COUNTERS		11
#define CYCLES_COUNTER		0

#define to_ddr_pmu(p)		container_of(p, struct ddr_pmu, pmu)

#define DDR_PERF_DEV_NAME	"imx9_ddr"
#define DDR_CPUHP_CB_NAME	DDR_PERF_DEV_NAME "_perf_pmu"

static DEFINE_IDA(ddr_ida);

struct imx_ddr_devtype_data {
	const char *identifier;		/* system PMU identifier for userspace */
};

struct ddr_pmu {
	struct pmu pmu;
	void __iomem *base;
	unsigned int cpu;
	struct hlist_node node;
	struct device *dev;
	struct perf_event *events[NUM_COUNTERS];
	int active_events;
	enum cpuhp_state cpuhp_state;
	const struct imx_ddr_devtype_data *devtype_data;
	int irq;
	int id;
};

static const struct imx_ddr_devtype_data imx93_devtype_data = {
	.identifier = "imx93",
};

static const struct of_device_id imx_ddr_pmu_dt_ids[] = {
	{.compatible = "fsl,imx93-ddr-pmu", .data = &imx93_devtype_data},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_ddr_pmu_dt_ids);

static ssize_t ddr_perf_identifier_show(struct device *dev,
					struct device_attribute *attr,
					char *page)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev);

	return sysfs_emit(page, "%s\n", pmu->devtype_data->identifier);
}

static struct device_attribute ddr_perf_identifier_attr =
	__ATTR(identifier, 0444, ddr_perf_identifier_show, NULL);

static struct attribute *ddr_perf_identifier_attrs[] = {
	&ddr_perf_identifier_attr.attr,
	NULL,
};

static struct attribute_group ddr_perf_identifier_attr_group = {
	.attrs = ddr_perf_identifier_attrs,
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

static const struct attribute_group ddr_perf_cpumask_attr_group = {
	.attrs = ddr_perf_cpumask_attrs,
};

static ssize_t ddr_pmu_event_show(struct device *dev,
				  struct device_attribute *attr, char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sysfs_emit(page, "event=0x%02llx\n", pmu_attr->id);
}

#define IMX9_DDR_PMU_EVENT_ATTR(_name, _id)				\
	(&((struct perf_pmu_events_attr[]) {				\
		{ .attr = __ATTR(_name, 0444, ddr_pmu_event_show, NULL),\
		  .id = _id, }						\
	})[0].attr.attr)

static struct attribute *ddr_perf_events_attrs[] = {
	/* counter0 cycles event */
	IMX9_DDR_PMU_EVENT_ATTR(cycles, 0),

	/* reference events for all normal counters, need assert DEBUG19[21] bit */
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ddrc1_rmw_for_ecc, 12),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_rreorder, 13),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_wreorder, 14),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_0, 15),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_1, 16),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_2, 17),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_3, 18),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_4, 19),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_5, 22),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_6, 23),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_7, 24),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_8, 25),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_9, 26),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_10, 27),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_11, 28),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_12, 31),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_13, 59),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_15, 61),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_pm_29, 63),

	/* counter1 specific events */
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_riq_0, 64),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_riq_1, 65),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_riq_2, 66),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_riq_3, 67),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_riq_4, 68),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_riq_5, 69),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_riq_6, 70),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_riq_7, 71),

	/* counter2 specific events */
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_wiq_0, 64),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_wiq_1, 65),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_wiq_2, 66),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_wiq_3, 67),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_wiq_4, 68),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_wiq_5, 69),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_wiq_6, 70),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_ld_wiq_7, 71),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_empty, 72),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pm_rd_trans_filt, 73),

	/* counter3 specific events */
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_collision_0, 64),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_collision_1, 65),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_collision_2, 66),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_collision_3, 67),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_collision_4, 68),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_collision_5, 69),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_collision_6, 70),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_collision_7, 71),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_full, 72),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pm_wr_trans_filt, 73),

	/* counter4 specific events */
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_open_0, 64),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_open_1, 65),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_open_2, 66),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_open_3, 67),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_open_4, 68),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_open_5, 69),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_open_6, 70),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_row_open_7, 71),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_ld_rdq2_rmw, 72),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pm_rd_beat_filt, 73),

	/* counter5 specific events */
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_valid_start_0, 64),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_valid_start_1, 65),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_valid_start_2, 66),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_valid_start_3, 67),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_valid_start_4, 68),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_valid_start_5, 69),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_valid_start_6, 70),
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_valid_start_7, 71),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_ld_rdq1, 72),

	/* counter6 specific events */
	IMX9_DDR_PMU_EVENT_ATTR(ddrc_qx_valid_end_0, 64),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_ld_rdq2, 72),

	/* counter7 specific events */
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_1_2_full, 64),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_ld_wrq0, 65),

	/* counter8 specific events */
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_bias_switched, 64),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_1_4_full, 65),

	/* counter9 specific events */
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_ld_wrq1, 65),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_3_4_full, 66),

	/* counter10 specific events */
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_misc_mrk, 65),
	IMX9_DDR_PMU_EVENT_ATTR(eddrtq_pmon_ld_rdq0, 66),
	NULL,
};

static const struct attribute_group ddr_perf_events_attr_group = {
	.name = "events",
	.attrs = ddr_perf_events_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-7");
PMU_FORMAT_ATTR(counter, "config:8-15");
PMU_FORMAT_ATTR(axi_id, "config1:0-17");
PMU_FORMAT_ATTR(axi_mask, "config2:0-17");

static struct attribute *ddr_perf_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_counter.attr,
	&format_attr_axi_id.attr,
	&format_attr_axi_mask.attr,
	NULL,
};

static const struct attribute_group ddr_perf_format_attr_group = {
	.name = "format",
	.attrs = ddr_perf_format_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&ddr_perf_identifier_attr_group,
	&ddr_perf_cpumask_attr_group,
	&ddr_perf_events_attr_group,
	&ddr_perf_format_attr_group,
	NULL,
};

static void ddr_perf_clear_counter(struct ddr_pmu *pmu, int counter)
{
	if (counter == CYCLES_COUNTER) {
		writel(0, pmu->base + PMC(counter) + 0x4);
		writel(0, pmu->base + PMC(counter));
	} else {
		writel(0, pmu->base + PMC(counter));
	}
}

static u64 ddr_perf_read_counter(struct ddr_pmu *pmu, int counter)
{
	u32 val_lower, val_upper;
	u64 val;

	if (counter != CYCLES_COUNTER) {
		val = readl_relaxed(pmu->base + PMC(counter));
		goto out;
	}

	/* special handling for reading 64bit cycle counter */
	do {
		val_upper = readl_relaxed(pmu->base + PMC(counter) + 0x4);
		val_lower = readl_relaxed(pmu->base + PMC(counter));
	} while (val_upper != readl_relaxed(pmu->base + PMC(counter) + 0x4));

	val = val_upper;
	val = (val << 32);
	val |= val_lower;
out:
	return val;
}

static void ddr_perf_counter_global_config(struct ddr_pmu *pmu, bool enable)
{
	u32 ctrl;

	ctrl = readl_relaxed(pmu->base + PMGC0);

	if (enable) {
		/*
		 * The performance monitor must be reset before event counting
		 * sequences. The performance monitor can be reset by first freezing
		 * one or more counters and then clearing the freeze condition to
		 * allow the counters to count according to the settings in the
		 * performance monitor registers. Counters can be frozen individually
		 * by setting PMLCAn[FC] bits, or simultaneously by setting PMGC0[FAC].
		 * Simply clearing these freeze bits will then allow the performance
		 * monitor to begin counting based on the register settings.
		 */
		ctrl |= PMGC0_FAC;
		writel(ctrl, pmu->base + PMGC0);

		/*
		 * Freeze all counters disabled, interrupt enabled, and freeze
		 * counters on condition enabled.
		 */
		ctrl &= ~PMGC0_FAC;
		ctrl |= PMGC0_PMIE | PMGC0_FCECE;
		writel(ctrl, pmu->base + PMGC0);
	} else {
		ctrl |= PMGC0_FAC;
		ctrl &= ~(PMGC0_PMIE | PMGC0_FCECE);
		writel(ctrl, pmu->base + PMGC0);
	}
}

static void ddr_perf_counter_local_config(struct ddr_pmu *pmu, int config,
				    int counter, bool enable)
{
	u32 ctrl_a;

	ctrl_a = readl_relaxed(pmu->base + PMLCA(counter));

	if (enable) {
		ctrl_a |= PMLCA_FC;
		writel(ctrl_a, pmu->base + PMLCA(counter));

		ddr_perf_clear_counter(pmu, counter);

		/* Freeze counter disabled, condition enabled, and program event.*/
		ctrl_a &= ~PMLCA_FC;
		ctrl_a |= PMLCA_CE;
		ctrl_a &= ~FIELD_PREP(PMLCA_EVENT, 0x7F);
		ctrl_a |= FIELD_PREP(PMLCA_EVENT, (config & 0x000000FF));
		writel(ctrl_a, pmu->base + PMLCA(counter));
	} else {
		/* Freeze counter. */
		ctrl_a |= PMLCA_FC;
		writel(ctrl_a, pmu->base + PMLCA(counter));
	}
}

static void ddr_perf_monitor_config(struct ddr_pmu *pmu, int cfg, int cfg1, int cfg2)
{
	u32 pmcfg1, pmcfg2;
	int event, counter;

	event = cfg & 0x000000FF;
	counter = (cfg & 0x0000FF00) >> 8;

	pmcfg1 = readl_relaxed(pmu->base + PMCFG1);

	if (counter == 2 && event == 73)
		pmcfg1 |= PMCFG1_RD_TRANS_FILT_EN;
	else if (counter == 2 && event != 73)
		pmcfg1 &= ~PMCFG1_RD_TRANS_FILT_EN;

	if (counter == 3 && event == 73)
		pmcfg1 |= PMCFG1_WR_TRANS_FILT_EN;
	else if (counter == 3 && event != 73)
		pmcfg1 &= ~PMCFG1_WR_TRANS_FILT_EN;

	if (counter == 4 && event == 73)
		pmcfg1 |= PMCFG1_RD_BT_FILT_EN;
	else if (counter == 4 && event != 73)
		pmcfg1 &= ~PMCFG1_RD_BT_FILT_EN;

	pmcfg1 &= ~FIELD_PREP(PMCFG1_ID_MASK, 0x3FFFF);
	pmcfg1 |= FIELD_PREP(PMCFG1_ID_MASK, cfg2);
	writel(pmcfg1, pmu->base + PMCFG1);

	pmcfg2 = readl_relaxed(pmu->base + PMCFG2);
	pmcfg2 &= ~FIELD_PREP(PMCFG2_ID, 0x3FFFF);
	pmcfg2 |= FIELD_PREP(PMCFG2_ID, cfg1);
	writel(pmcfg2, pmu->base + PMCFG2);
}

static void ddr_perf_event_update(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;
	u64 new_raw_count;

	new_raw_count = ddr_perf_read_counter(pmu, counter);
	local64_add(new_raw_count, &event->count);

	/* clear counter's value every time */
	ddr_perf_clear_counter(pmu, counter);
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

static void ddr_perf_event_start(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	local64_set(&hwc->prev_count, 0);

	ddr_perf_counter_local_config(pmu, event->attr.config, counter, true);
	hwc->state = 0;
}

static int ddr_perf_event_add(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int cfg = event->attr.config;
	int cfg1 = event->attr.config1;
	int cfg2 = event->attr.config2;
	int counter;

	counter = (cfg & 0x0000FF00) >> 8;

	pmu->events[counter] = event;
	pmu->active_events++;
	hwc->idx = counter;
	hwc->state |= PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		ddr_perf_event_start(event, flags);

	/* read trans, write trans, read beat */
	ddr_perf_monitor_config(pmu, cfg, cfg1, cfg2);

	return 0;
}

static void ddr_perf_event_stop(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	ddr_perf_counter_local_config(pmu, event->attr.config, counter, false);
	ddr_perf_event_update(event);

	hwc->state |= PERF_HES_STOPPED;
}

static void ddr_perf_event_del(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	ddr_perf_event_stop(event, PERF_EF_UPDATE);

	pmu->active_events--;
	hwc->idx = -1;
}

static void ddr_perf_pmu_enable(struct pmu *pmu)
{
	struct ddr_pmu *ddr_pmu = to_ddr_pmu(pmu);

	ddr_perf_counter_global_config(ddr_pmu, true);
}

static void ddr_perf_pmu_disable(struct pmu *pmu)
{
	struct ddr_pmu *ddr_pmu = to_ddr_pmu(pmu);

	ddr_perf_counter_global_config(ddr_pmu, false);
}

static void ddr_perf_init(struct ddr_pmu *pmu, void __iomem *base,
			 struct device *dev)
{
	*pmu = (struct ddr_pmu) {
		.pmu = (struct pmu) {
			.module       = THIS_MODULE,
			.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
			.task_ctx_nr  = perf_invalid_context,
			.attr_groups  = attr_groups,
			.event_init   = ddr_perf_event_init,
			.add          = ddr_perf_event_add,
			.del          = ddr_perf_event_del,
			.start        = ddr_perf_event_start,
			.stop         = ddr_perf_event_stop,
			.read         = ddr_perf_event_update,
			.pmu_enable   = ddr_perf_pmu_enable,
			.pmu_disable  = ddr_perf_pmu_disable,
		},
		.base = base,
		.dev = dev,
	};
}

static irqreturn_t ddr_perf_irq_handler(int irq, void *p)
{
	struct ddr_pmu *pmu = (struct ddr_pmu *)p;
	struct perf_event *event;
	int i;

	/*
	 * Counters can generate an interrupt on an overflow when msb of a
	 * counter changes from 0 to 1. For the interrupt to be signalled,
	 * below condition mush be satisfied:
	 * PMGC0[PMIE] = 1, PMGC0[FCECE] = 1, PMLCAn[CE] = 1
	 * When an interrupt is signalled, PMGC0[FAC] is set by hardware and
	 * all of the registers are frozen.
	 * Software can clear the interrupt condition by resetting the performance
	 * monitor and clearing the most significant bit of the counter that
	 * generate the overflow.
	 */
	for (i = 0; i < NUM_COUNTERS; i++) {
		if (!pmu->events[i])
			continue;

		event = pmu->events[i];

		ddr_perf_event_update(event);
	}

	ddr_perf_counter_global_config(pmu, true);

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

	WARN_ON(irq_set_affinity(pmu->irq, cpumask_of(pmu->cpu)));

	return 0;
}

static int ddr_perf_probe(struct platform_device *pdev)
{
	struct ddr_pmu *pmu;
	void __iomem *base;
	int ret, irq;
	char *name;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	pmu = devm_kzalloc(&pdev->dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	ddr_perf_init(pmu, base, &pdev->dev);

	pmu->devtype_data = of_device_get_match_data(&pdev->dev);

	platform_set_drvdata(pdev, pmu);

	pmu->id = ida_alloc(&ddr_ida, GFP_KERNEL);
	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, DDR_PERF_DEV_NAME "%d", pmu->id);
	if (!name) {
		ret = -ENOMEM;
		goto format_string_err;
	}

	pmu->cpu = raw_smp_processor_id();
	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, DDR_CPUHP_CB_NAME,
				      NULL, ddr_perf_offline_cpu);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add callbacks for multi state\n");
		goto cpuhp_state_err;
	}
	pmu->cpuhp_state = ret;

	/* Register the pmu instance for cpu hotplug */
	ret = cpuhp_state_add_instance_nocalls(pmu->cpuhp_state, &pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug\n", ret);
		goto cpuhp_instance_err;
	}

	/* Request irq */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto ddr_perf_err;
	}

	ret = devm_request_irq(&pdev->dev, irq, ddr_perf_irq_handler,
			       IRQF_NOBALANCING | IRQF_NO_THREAD,
			       DDR_CPUHP_CB_NAME, pmu);
	if (ret < 0) {
		dev_err(&pdev->dev, "Request irq failed: %d", ret);
		goto ddr_perf_err;
	}

	pmu->irq = irq;
	ret = irq_set_affinity(pmu->irq, cpumask_of(pmu->cpu));
	if (ret) {
		dev_err(pmu->dev, "Failed to set interrupt affinity\n");
		goto ddr_perf_err;
	}

	ret = perf_pmu_register(&pmu->pmu, name, -1);
	if (ret)
		goto ddr_perf_err;

	return 0;

ddr_perf_err:
	cpuhp_state_remove_instance_nocalls(pmu->cpuhp_state, &pmu->node);
cpuhp_instance_err:
	cpuhp_remove_multi_state(pmu->cpuhp_state);
cpuhp_state_err:
format_string_err:
	ida_free(&ddr_ida, pmu->id);
	dev_warn(&pdev->dev, "i.MX9 DDR Perf PMU failed (%d), disabled\n", ret);
	return ret;
}

static void ddr_perf_remove(struct platform_device *pdev)
{
	struct ddr_pmu *pmu = platform_get_drvdata(pdev);

	cpuhp_state_remove_instance_nocalls(pmu->cpuhp_state, &pmu->node);
	cpuhp_remove_multi_state(pmu->cpuhp_state);

	perf_pmu_unregister(&pmu->pmu);

	ida_free(&ddr_ida, pmu->id);
}

static struct platform_driver imx_ddr_pmu_driver = {
	.driver         = {
		.name                = "imx9-ddr-pmu",
		.of_match_table      = imx_ddr_pmu_dt_ids,
		.suppress_bind_attrs = true,
	},
	.probe          = ddr_perf_probe,
	.remove_new     = ddr_perf_remove,
};
module_platform_driver(imx_ddr_pmu_driver);

MODULE_AUTHOR("Xu Yang <xu.yang_2@nxp.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DDRC PerfMon for i.MX9 SoCs");
