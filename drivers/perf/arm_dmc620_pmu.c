// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARM DMC-620 memory controller PMU driver
 *
 * Copyright (C) 2020 Ampere Computing LLC.
 */

#define DMC620_PMUNAME		"arm_dmc620"
#define DMC620_DRVNAME		DMC620_PMUNAME "_pmu"
#define pr_fmt(fmt)		DMC620_DRVNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/refcount.h>

#define DMC620_PA_SHIFT					12
#define DMC620_CNT_INIT					0x80000000
#define DMC620_CNT_MAX_PERIOD				0xffffffff
#define DMC620_PMU_CLKDIV2_MAX_COUNTERS			8
#define DMC620_PMU_CLK_MAX_COUNTERS			2
#define DMC620_PMU_MAX_COUNTERS				\
	(DMC620_PMU_CLKDIV2_MAX_COUNTERS + DMC620_PMU_CLK_MAX_COUNTERS)

/*
 * The PMU registers start at 0xA00 in the DMC-620 memory map, and these
 * offsets are relative to that base.
 *
 * Each counter has a group of control/value registers, and the
 * DMC620_PMU_COUNTERn offsets are within a counter group.
 *
 * The counter registers groups start at 0xA10.
 */
#define DMC620_PMU_OVERFLOW_STATUS_CLKDIV2		0x8
#define  DMC620_PMU_OVERFLOW_STATUS_CLKDIV2_MASK	\
		(DMC620_PMU_CLKDIV2_MAX_COUNTERS - 1)
#define DMC620_PMU_OVERFLOW_STATUS_CLK			0xC
#define  DMC620_PMU_OVERFLOW_STATUS_CLK_MASK		\
		(DMC620_PMU_CLK_MAX_COUNTERS - 1)
#define DMC620_PMU_COUNTERS_BASE			0x10
#define DMC620_PMU_COUNTERn_MASK_31_00			0x0
#define DMC620_PMU_COUNTERn_MASK_63_32			0x4
#define DMC620_PMU_COUNTERn_MATCH_31_00			0x8
#define DMC620_PMU_COUNTERn_MATCH_63_32			0xC
#define DMC620_PMU_COUNTERn_CONTROL			0x10
#define  DMC620_PMU_COUNTERn_CONTROL_ENABLE		BIT(0)
#define  DMC620_PMU_COUNTERn_CONTROL_INVERT		BIT(1)
#define  DMC620_PMU_COUNTERn_CONTROL_EVENT_MUX		GENMASK(6, 2)
#define  DMC620_PMU_COUNTERn_CONTROL_INCR_MUX		GENMASK(8, 7)
#define DMC620_PMU_COUNTERn_VALUE			0x20
/* Offset of the registers for a given counter, relative to 0xA00 */
#define DMC620_PMU_COUNTERn_OFFSET(n) \
	(DMC620_PMU_COUNTERS_BASE + 0x28 * (n))

/*
 * dmc620_pmu_irqs_lock: protects dmc620_pmu_irqs list
 * dmc620_pmu_node_lock: protects pmus_node lists in all dmc620_pmu instances
 */
static DEFINE_MUTEX(dmc620_pmu_irqs_lock);
static DEFINE_MUTEX(dmc620_pmu_node_lock);
static LIST_HEAD(dmc620_pmu_irqs);

struct dmc620_pmu_irq {
	struct hlist_node node;
	struct list_head pmus_node;
	struct list_head irqs_node;
	refcount_t refcount;
	unsigned int irq_num;
	unsigned int cpu;
};

struct dmc620_pmu {
	struct pmu pmu;

	void __iomem *base;
	struct dmc620_pmu_irq *irq;
	struct list_head pmus_node;

	/*
	 * We put all clkdiv2 and clk counters to a same array.
	 * The first DMC620_PMU_CLKDIV2_MAX_COUNTERS bits belong to
	 * clkdiv2 counters, the last DMC620_PMU_CLK_MAX_COUNTERS
	 * belong to clk counters.
	 */
	DECLARE_BITMAP(used_mask, DMC620_PMU_MAX_COUNTERS);
	struct perf_event *events[DMC620_PMU_MAX_COUNTERS];
};

#define to_dmc620_pmu(p) (container_of(p, struct dmc620_pmu, pmu))

static int cpuhp_state_num;

struct dmc620_pmu_event_attr {
	struct device_attribute attr;
	u8 clkdiv2;
	u8 eventid;
};

static ssize_t
dmc620_pmu_event_show(struct device *dev,
			   struct device_attribute *attr, char *page)
{
	struct dmc620_pmu_event_attr *eattr;

	eattr = container_of(attr, typeof(*eattr), attr);

	return sysfs_emit(page, "event=0x%x,clkdiv2=0x%x\n", eattr->eventid, eattr->clkdiv2);
}

#define DMC620_PMU_EVENT_ATTR(_name, _eventid, _clkdiv2)		\
	(&((struct dmc620_pmu_event_attr[]) {{				\
		.attr = __ATTR(_name, 0444, dmc620_pmu_event_show, NULL),	\
		.clkdiv2 = _clkdiv2,						\
		.eventid = _eventid,					\
	}})[0].attr.attr)

static struct attribute *dmc620_pmu_events_attrs[] = {
	/* clkdiv2 events list */
	DMC620_PMU_EVENT_ATTR(clkdiv2_cycle_count, 0x0, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_allocate, 0x1, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_queue_depth, 0x2, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_waiting_for_wr_data, 0x3, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_read_backlog, 0x4, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_waiting_for_mi, 0x5, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_hazard_resolution, 0x6, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_enqueue, 0x7, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_arbitrate, 0x8, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_lrank_turnaround_activate, 0x9, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_prank_turnaround_activate, 0xa, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_read_depth, 0xb, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_write_depth, 0xc, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_highigh_qos_depth, 0xd, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_high_qos_depth, 0xe, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_medium_qos_depth, 0xf, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_low_qos_depth, 0x10, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_activate, 0x11, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_rdwr, 0x12, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_refresh, 0x13, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_training_request, 0x14, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_t_mac_tracker, 0x15, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_bk_fsm_tracker, 0x16, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_bk_open_tracker, 0x17, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_ranks_in_pwr_down, 0x18, 1),
	DMC620_PMU_EVENT_ATTR(clkdiv2_ranks_in_sref, 0x19, 1),

	/* clk events list */
	DMC620_PMU_EVENT_ATTR(clk_cycle_count, 0x0, 0),
	DMC620_PMU_EVENT_ATTR(clk_request, 0x1, 0),
	DMC620_PMU_EVENT_ATTR(clk_upload_stall, 0x2, 0),
	NULL,
};

static const struct attribute_group dmc620_pmu_events_attr_group = {
	.name = "events",
	.attrs = dmc620_pmu_events_attrs,
};

/* User ABI */
#define ATTR_CFG_FLD_mask_CFG		config
#define ATTR_CFG_FLD_mask_LO		0
#define ATTR_CFG_FLD_mask_HI		44
#define ATTR_CFG_FLD_match_CFG		config1
#define ATTR_CFG_FLD_match_LO		0
#define ATTR_CFG_FLD_match_HI		44
#define ATTR_CFG_FLD_invert_CFG		config2
#define ATTR_CFG_FLD_invert_LO		0
#define ATTR_CFG_FLD_invert_HI		0
#define ATTR_CFG_FLD_incr_CFG		config2
#define ATTR_CFG_FLD_incr_LO		1
#define ATTR_CFG_FLD_incr_HI		2
#define ATTR_CFG_FLD_event_CFG		config2
#define ATTR_CFG_FLD_event_LO		3
#define ATTR_CFG_FLD_event_HI		8
#define ATTR_CFG_FLD_clkdiv2_CFG	config2
#define ATTR_CFG_FLD_clkdiv2_LO		9
#define ATTR_CFG_FLD_clkdiv2_HI		9

#define __GEN_PMU_FORMAT_ATTR(cfg, lo, hi)			\
	(lo) == (hi) ? #cfg ":" #lo "\n" : #cfg ":" #lo "-" #hi

#define _GEN_PMU_FORMAT_ATTR(cfg, lo, hi)			\
	__GEN_PMU_FORMAT_ATTR(cfg, lo, hi)

#define GEN_PMU_FORMAT_ATTR(name)				\
	PMU_FORMAT_ATTR(name,					\
	_GEN_PMU_FORMAT_ATTR(ATTR_CFG_FLD_##name##_CFG,		\
			     ATTR_CFG_FLD_##name##_LO,		\
			     ATTR_CFG_FLD_##name##_HI))

#define _ATTR_CFG_GET_FLD(attr, cfg, lo, hi)			\
	((((attr)->cfg) >> lo) & GENMASK_ULL(hi - lo, 0))

#define ATTR_CFG_GET_FLD(attr, name)				\
	_ATTR_CFG_GET_FLD(attr,					\
			  ATTR_CFG_FLD_##name##_CFG,		\
			  ATTR_CFG_FLD_##name##_LO,		\
			  ATTR_CFG_FLD_##name##_HI)

GEN_PMU_FORMAT_ATTR(mask);
GEN_PMU_FORMAT_ATTR(match);
GEN_PMU_FORMAT_ATTR(invert);
GEN_PMU_FORMAT_ATTR(incr);
GEN_PMU_FORMAT_ATTR(event);
GEN_PMU_FORMAT_ATTR(clkdiv2);

static struct attribute *dmc620_pmu_formats_attrs[] = {
	&format_attr_mask.attr,
	&format_attr_match.attr,
	&format_attr_invert.attr,
	&format_attr_incr.attr,
	&format_attr_event.attr,
	&format_attr_clkdiv2.attr,
	NULL,
};

static const struct attribute_group dmc620_pmu_format_attr_group = {
	.name	= "format",
	.attrs	= dmc620_pmu_formats_attrs,
};

static ssize_t dmc620_pmu_cpumask_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct dmc620_pmu *dmc620_pmu = to_dmc620_pmu(dev_get_drvdata(dev));

	return cpumap_print_to_pagebuf(true, buf,
				       cpumask_of(dmc620_pmu->irq->cpu));
}

static struct device_attribute dmc620_pmu_cpumask_attr =
	__ATTR(cpumask, 0444, dmc620_pmu_cpumask_show, NULL);

static struct attribute *dmc620_pmu_cpumask_attrs[] = {
	&dmc620_pmu_cpumask_attr.attr,
	NULL,
};

static const struct attribute_group dmc620_pmu_cpumask_attr_group = {
	.attrs = dmc620_pmu_cpumask_attrs,
};

static const struct attribute_group *dmc620_pmu_attr_groups[] = {
	&dmc620_pmu_events_attr_group,
	&dmc620_pmu_format_attr_group,
	&dmc620_pmu_cpumask_attr_group,
	NULL,
};

static inline
u32 dmc620_pmu_creg_read(struct dmc620_pmu *dmc620_pmu,
			unsigned int idx, unsigned int reg)
{
	return readl(dmc620_pmu->base + DMC620_PMU_COUNTERn_OFFSET(idx) + reg);
}

static inline
void dmc620_pmu_creg_write(struct dmc620_pmu *dmc620_pmu,
			unsigned int idx, unsigned int reg, u32 val)
{
	writel(val, dmc620_pmu->base + DMC620_PMU_COUNTERn_OFFSET(idx) + reg);
}

static
unsigned int dmc620_event_to_counter_control(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	unsigned int reg = 0;

	reg |= FIELD_PREP(DMC620_PMU_COUNTERn_CONTROL_INVERT,
			ATTR_CFG_GET_FLD(attr, invert));
	reg |= FIELD_PREP(DMC620_PMU_COUNTERn_CONTROL_EVENT_MUX,
			ATTR_CFG_GET_FLD(attr, event));
	reg |= FIELD_PREP(DMC620_PMU_COUNTERn_CONTROL_INCR_MUX,
			ATTR_CFG_GET_FLD(attr, incr));

	return reg;
}

static int dmc620_get_event_idx(struct perf_event *event)
{
	struct dmc620_pmu *dmc620_pmu = to_dmc620_pmu(event->pmu);
	int idx, start_idx, end_idx;

	if (ATTR_CFG_GET_FLD(&event->attr, clkdiv2)) {
		start_idx = 0;
		end_idx = DMC620_PMU_CLKDIV2_MAX_COUNTERS;
	} else {
		start_idx = DMC620_PMU_CLKDIV2_MAX_COUNTERS;
		end_idx = DMC620_PMU_MAX_COUNTERS;
	}

	for (idx = start_idx; idx < end_idx; ++idx) {
		if (!test_and_set_bit(idx, dmc620_pmu->used_mask))
			return idx;
	}

	/* The counters are all in use. */
	return -EAGAIN;
}

static inline
u64 dmc620_pmu_read_counter(struct perf_event *event)
{
	struct dmc620_pmu *dmc620_pmu = to_dmc620_pmu(event->pmu);

	return dmc620_pmu_creg_read(dmc620_pmu,
				    event->hw.idx, DMC620_PMU_COUNTERn_VALUE);
}

static void dmc620_pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev_count, new_count;

	do {
		/* We may also be called from the irq handler */
		prev_count = local64_read(&hwc->prev_count);
		new_count = dmc620_pmu_read_counter(event);
	} while (local64_cmpxchg(&hwc->prev_count,
			prev_count, new_count) != prev_count);
	delta = (new_count - prev_count) & DMC620_CNT_MAX_PERIOD;
	local64_add(delta, &event->count);
}

static void dmc620_pmu_event_set_period(struct perf_event *event)
{
	struct dmc620_pmu *dmc620_pmu = to_dmc620_pmu(event->pmu);

	local64_set(&event->hw.prev_count, DMC620_CNT_INIT);
	dmc620_pmu_creg_write(dmc620_pmu,
			      event->hw.idx, DMC620_PMU_COUNTERn_VALUE, DMC620_CNT_INIT);
}

static void dmc620_pmu_enable_counter(struct perf_event *event)
{
	struct dmc620_pmu *dmc620_pmu = to_dmc620_pmu(event->pmu);
	u32 reg;

	reg = dmc620_event_to_counter_control(event) | DMC620_PMU_COUNTERn_CONTROL_ENABLE;
	dmc620_pmu_creg_write(dmc620_pmu,
			      event->hw.idx, DMC620_PMU_COUNTERn_CONTROL, reg);
}

static void dmc620_pmu_disable_counter(struct perf_event *event)
{
	struct dmc620_pmu *dmc620_pmu = to_dmc620_pmu(event->pmu);

	dmc620_pmu_creg_write(dmc620_pmu,
			      event->hw.idx, DMC620_PMU_COUNTERn_CONTROL, 0);
}

static irqreturn_t dmc620_pmu_handle_irq(int irq_num, void *data)
{
	struct dmc620_pmu_irq *irq = data;
	struct dmc620_pmu *dmc620_pmu;
	irqreturn_t ret = IRQ_NONE;

	rcu_read_lock();
	list_for_each_entry_rcu(dmc620_pmu, &irq->pmus_node, pmus_node) {
		unsigned long status;
		struct perf_event *event;
		unsigned int idx;

		/*
		 * HW doesn't provide a control to atomically disable all counters.
		 * To prevent race condition (overflow happens while clearing status register),
		 * disable all events before continuing
		 */
		for (idx = 0; idx < DMC620_PMU_MAX_COUNTERS; idx++) {
			event = dmc620_pmu->events[idx];
			if (!event)
				continue;
			dmc620_pmu_disable_counter(event);
		}

		status = readl(dmc620_pmu->base + DMC620_PMU_OVERFLOW_STATUS_CLKDIV2);
		status |= (readl(dmc620_pmu->base + DMC620_PMU_OVERFLOW_STATUS_CLK) <<
				DMC620_PMU_CLKDIV2_MAX_COUNTERS);
		if (status) {
			for_each_set_bit(idx, &status,
					DMC620_PMU_MAX_COUNTERS) {
				event = dmc620_pmu->events[idx];
				if (WARN_ON_ONCE(!event))
					continue;
				dmc620_pmu_event_update(event);
				dmc620_pmu_event_set_period(event);
			}

			if (status & DMC620_PMU_OVERFLOW_STATUS_CLKDIV2_MASK)
				writel(0, dmc620_pmu->base + DMC620_PMU_OVERFLOW_STATUS_CLKDIV2);

			if ((status >> DMC620_PMU_CLKDIV2_MAX_COUNTERS) &
				DMC620_PMU_OVERFLOW_STATUS_CLK_MASK)
				writel(0, dmc620_pmu->base + DMC620_PMU_OVERFLOW_STATUS_CLK);
		}

		for (idx = 0; idx < DMC620_PMU_MAX_COUNTERS; idx++) {
			event = dmc620_pmu->events[idx];
			if (!event)
				continue;
			if (!(event->hw.state & PERF_HES_STOPPED))
				dmc620_pmu_enable_counter(event);
		}

		ret = IRQ_HANDLED;
	}
	rcu_read_unlock();

	return ret;
}

static struct dmc620_pmu_irq *__dmc620_pmu_get_irq(int irq_num)
{
	struct dmc620_pmu_irq *irq;
	int ret;

	list_for_each_entry(irq, &dmc620_pmu_irqs, irqs_node)
		if (irq->irq_num == irq_num && refcount_inc_not_zero(&irq->refcount))
			return irq;

	irq = kzalloc(sizeof(*irq), GFP_KERNEL);
	if (!irq)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&irq->pmus_node);

	/* Pick one CPU to be the preferred one to use */
	irq->cpu = raw_smp_processor_id();
	refcount_set(&irq->refcount, 1);

	ret = request_irq(irq_num, dmc620_pmu_handle_irq,
			  IRQF_NOBALANCING | IRQF_NO_THREAD,
			  "dmc620-pmu", irq);
	if (ret)
		goto out_free_aff;

	ret = irq_set_affinity(irq_num, cpumask_of(irq->cpu));
	if (ret)
		goto out_free_irq;

	ret = cpuhp_state_add_instance_nocalls(cpuhp_state_num, &irq->node);
	if (ret)
		goto out_free_irq;

	irq->irq_num = irq_num;
	list_add(&irq->irqs_node, &dmc620_pmu_irqs);

	return irq;

out_free_irq:
	free_irq(irq_num, irq);
out_free_aff:
	kfree(irq);
	return ERR_PTR(ret);
}

static int dmc620_pmu_get_irq(struct dmc620_pmu *dmc620_pmu, int irq_num)
{
	struct dmc620_pmu_irq *irq;

	mutex_lock(&dmc620_pmu_irqs_lock);
	irq = __dmc620_pmu_get_irq(irq_num);
	mutex_unlock(&dmc620_pmu_irqs_lock);

	if (IS_ERR(irq))
		return PTR_ERR(irq);

	dmc620_pmu->irq = irq;
	mutex_lock(&dmc620_pmu_node_lock);
	list_add_rcu(&dmc620_pmu->pmus_node, &irq->pmus_node);
	mutex_unlock(&dmc620_pmu_node_lock);

	return 0;
}

static void dmc620_pmu_put_irq(struct dmc620_pmu *dmc620_pmu)
{
	struct dmc620_pmu_irq *irq = dmc620_pmu->irq;

	mutex_lock(&dmc620_pmu_node_lock);
	list_del_rcu(&dmc620_pmu->pmus_node);
	mutex_unlock(&dmc620_pmu_node_lock);

	mutex_lock(&dmc620_pmu_irqs_lock);
	if (!refcount_dec_and_test(&irq->refcount)) {
		mutex_unlock(&dmc620_pmu_irqs_lock);
		return;
	}

	list_del(&irq->irqs_node);
	mutex_unlock(&dmc620_pmu_irqs_lock);

	free_irq(irq->irq_num, irq);
	cpuhp_state_remove_instance_nocalls(cpuhp_state_num, &irq->node);
	kfree(irq);
}

static int dmc620_pmu_event_init(struct perf_event *event)
{
	struct dmc620_pmu *dmc620_pmu = to_dmc620_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	struct perf_event *sibling;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/*
	 * DMC 620 PMUs are shared across all cpus and cannot
	 * support task bound and sampling events.
	 */
	if (is_sampling_event(event) ||
		event->attach_state & PERF_ATTACH_TASK) {
		dev_dbg(dmc620_pmu->pmu.dev,
			"Can't support per-task counters\n");
		return -EOPNOTSUPP;
	}

	/*
	 * Many perf core operations (eg. events rotation) operate on a
	 * single CPU context. This is obvious for CPU PMUs, where one
	 * expects the same sets of events being observed on all CPUs,
	 * but can lead to issues for off-core PMUs, where each
	 * event could be theoretically assigned to a different CPU. To
	 * mitigate this, we enforce CPU assignment to one, selected
	 * processor.
	 */
	event->cpu = dmc620_pmu->irq->cpu;
	if (event->cpu < 0)
		return -EINVAL;

	hwc->idx = -1;

	if (event->group_leader == event)
		return 0;

	/*
	 * We can't atomically disable all HW counters so only one event allowed,
	 * although software events are acceptable.
	 */
	if (!is_software_event(event->group_leader))
		return -EINVAL;

	for_each_sibling_event(sibling, event->group_leader) {
		if (sibling != event &&
				!is_software_event(sibling))
			return -EINVAL;
	}

	return 0;
}

static void dmc620_pmu_read(struct perf_event *event)
{
	dmc620_pmu_event_update(event);
}

static void dmc620_pmu_start(struct perf_event *event, int flags)
{
	event->hw.state = 0;
	dmc620_pmu_event_set_period(event);
	dmc620_pmu_enable_counter(event);
}

static void dmc620_pmu_stop(struct perf_event *event, int flags)
{
	if (event->hw.state & PERF_HES_STOPPED)
		return;

	dmc620_pmu_disable_counter(event);
	dmc620_pmu_event_update(event);
	event->hw.state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static int dmc620_pmu_add(struct perf_event *event, int flags)
{
	struct dmc620_pmu *dmc620_pmu = to_dmc620_pmu(event->pmu);
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	int idx;
	u64 reg;

	idx = dmc620_get_event_idx(event);
	if (idx < 0)
		return idx;

	hwc->idx = idx;
	dmc620_pmu->events[idx] = event;
	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	reg = ATTR_CFG_GET_FLD(attr, mask);
	dmc620_pmu_creg_write(dmc620_pmu,
			      idx, DMC620_PMU_COUNTERn_MASK_31_00, lower_32_bits(reg));
	dmc620_pmu_creg_write(dmc620_pmu,
			      idx, DMC620_PMU_COUNTERn_MASK_63_32, upper_32_bits(reg));

	reg = ATTR_CFG_GET_FLD(attr, match);
	dmc620_pmu_creg_write(dmc620_pmu,
			      idx, DMC620_PMU_COUNTERn_MATCH_31_00, lower_32_bits(reg));
	dmc620_pmu_creg_write(dmc620_pmu,
			      idx, DMC620_PMU_COUNTERn_MATCH_63_32, upper_32_bits(reg));

	if (flags & PERF_EF_START)
		dmc620_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
	return 0;
}

static void dmc620_pmu_del(struct perf_event *event, int flags)
{
	struct dmc620_pmu *dmc620_pmu = to_dmc620_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	dmc620_pmu_stop(event, PERF_EF_UPDATE);
	dmc620_pmu->events[idx] = NULL;
	clear_bit(idx, dmc620_pmu->used_mask);
	perf_event_update_userpage(event);
}

static int dmc620_pmu_cpu_teardown(unsigned int cpu,
				   struct hlist_node *node)
{
	struct dmc620_pmu_irq *irq;
	struct dmc620_pmu *dmc620_pmu;
	unsigned int target;

	irq = hlist_entry_safe(node, struct dmc620_pmu_irq, node);
	if (cpu != irq->cpu)
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	/* We're only reading, but this isn't the place to be involving RCU */
	mutex_lock(&dmc620_pmu_node_lock);
	list_for_each_entry(dmc620_pmu, &irq->pmus_node, pmus_node)
		perf_pmu_migrate_context(&dmc620_pmu->pmu, irq->cpu, target);
	mutex_unlock(&dmc620_pmu_node_lock);

	WARN_ON(irq_set_affinity(irq->irq_num, cpumask_of(target)));
	irq->cpu = target;

	return 0;
}

static int dmc620_pmu_device_probe(struct platform_device *pdev)
{
	struct dmc620_pmu *dmc620_pmu;
	struct resource *res;
	char *name;
	int irq_num;
	int i, ret;

	dmc620_pmu = devm_kzalloc(&pdev->dev,
			sizeof(struct dmc620_pmu), GFP_KERNEL);
	if (!dmc620_pmu)
		return -ENOMEM;

	platform_set_drvdata(pdev, dmc620_pmu);

	dmc620_pmu->pmu = (struct pmu) {
		.module = THIS_MODULE,
		.parent		= &pdev->dev,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
		.task_ctx_nr	= perf_invalid_context,
		.event_init	= dmc620_pmu_event_init,
		.add		= dmc620_pmu_add,
		.del		= dmc620_pmu_del,
		.start		= dmc620_pmu_start,
		.stop		= dmc620_pmu_stop,
		.read		= dmc620_pmu_read,
		.attr_groups	= dmc620_pmu_attr_groups,
	};

	dmc620_pmu->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(dmc620_pmu->base))
		return PTR_ERR(dmc620_pmu->base);

	/* Make sure device is reset before enabling interrupt */
	for (i = 0; i < DMC620_PMU_MAX_COUNTERS; i++)
		dmc620_pmu_creg_write(dmc620_pmu, i, DMC620_PMU_COUNTERn_CONTROL, 0);
	writel(0, dmc620_pmu->base + DMC620_PMU_OVERFLOW_STATUS_CLKDIV2);
	writel(0, dmc620_pmu->base + DMC620_PMU_OVERFLOW_STATUS_CLK);

	irq_num = platform_get_irq(pdev, 0);
	if (irq_num < 0)
		return irq_num;

	ret = dmc620_pmu_get_irq(dmc620_pmu, irq_num);
	if (ret)
		return ret;

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
				  "%s_%llx", DMC620_PMUNAME,
				  (u64)(res->start >> DMC620_PA_SHIFT));
	if (!name) {
		dev_err(&pdev->dev,
			  "Create name failed, PMU @%pa\n", &res->start);
		ret = -ENOMEM;
		goto out_teardown_dev;
	}

	ret = perf_pmu_register(&dmc620_pmu->pmu, name, -1);
	if (ret)
		goto out_teardown_dev;

	return 0;

out_teardown_dev:
	dmc620_pmu_put_irq(dmc620_pmu);
	synchronize_rcu();
	return ret;
}

static void dmc620_pmu_device_remove(struct platform_device *pdev)
{
	struct dmc620_pmu *dmc620_pmu = platform_get_drvdata(pdev);

	dmc620_pmu_put_irq(dmc620_pmu);

	/* perf will synchronise RCU before devres can free dmc620_pmu */
	perf_pmu_unregister(&dmc620_pmu->pmu);
}

static const struct acpi_device_id dmc620_acpi_match[] = {
	{ "ARMHD620", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, dmc620_acpi_match);
static struct platform_driver dmc620_pmu_driver = {
	.driver	= {
		.name		= DMC620_DRVNAME,
		.acpi_match_table = dmc620_acpi_match,
		.suppress_bind_attrs = true,
	},
	.probe	= dmc620_pmu_device_probe,
	.remove_new = dmc620_pmu_device_remove,
};

static int __init dmc620_pmu_init(void)
{
	int ret;

	cpuhp_state_num = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      DMC620_DRVNAME,
				      NULL,
				      dmc620_pmu_cpu_teardown);
	if (cpuhp_state_num < 0)
		return cpuhp_state_num;

	ret = platform_driver_register(&dmc620_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(cpuhp_state_num);

	return ret;
}

static void __exit dmc620_pmu_exit(void)
{
	platform_driver_unregister(&dmc620_pmu_driver);
	cpuhp_remove_multi_state(cpuhp_state_num);
}

module_init(dmc620_pmu_init);
module_exit(dmc620_pmu_exit);

MODULE_DESCRIPTION("Perf driver for the ARM DMC-620 memory controller");
MODULE_AUTHOR("Tuan Phan <tuanphan@os.amperecomputing.com");
MODULE_LICENSE("GPL v2");
