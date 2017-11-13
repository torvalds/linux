/*
 * Driver for the L3 cache PMUs in Qualcomm Technologies chips.
 *
 * The driver supports a distributed cache architecture where the overall
 * cache for a socket is comprised of multiple slices each with its own PMU.
 * Access to each individual PMU is provided even though all CPUs share all
 * the slices. User space needs to aggregate to individual counts to provide
 * a global picture.
 *
 * See Documentation/perf/qcom_l3_pmu.txt for more details.
 *
 * Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
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
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>

/*
 * General constants
 */

/* Number of counters on each PMU */
#define L3_NUM_COUNTERS  8
/* Mask for the event type field within perf_event_attr.config and EVTYPE reg */
#define L3_EVTYPE_MASK   0xFF
/*
 * Bit position of the 'long counter' flag within perf_event_attr.config.
 * Reserve some space between the event type and this flag to allow expansion
 * in the event type field.
 */
#define L3_EVENT_LC_BIT  32

/*
 * Register offsets
 */

/* Perfmon registers */
#define L3_HML3_PM_CR       0x000
#define L3_HML3_PM_EVCNTR(__cntr) (0x420 + ((__cntr) & 0x7) * 8)
#define L3_HML3_PM_CNTCTL(__cntr) (0x120 + ((__cntr) & 0x7) * 8)
#define L3_HML3_PM_EVTYPE(__cntr) (0x220 + ((__cntr) & 0x7) * 8)
#define L3_HML3_PM_FILTRA   0x300
#define L3_HML3_PM_FILTRB   0x308
#define L3_HML3_PM_FILTRC   0x310
#define L3_HML3_PM_FILTRAM  0x304
#define L3_HML3_PM_FILTRBM  0x30C
#define L3_HML3_PM_FILTRCM  0x314

/* Basic counter registers */
#define L3_M_BC_CR         0x500
#define L3_M_BC_SATROLL_CR 0x504
#define L3_M_BC_CNTENSET   0x508
#define L3_M_BC_CNTENCLR   0x50C
#define L3_M_BC_INTENSET   0x510
#define L3_M_BC_INTENCLR   0x514
#define L3_M_BC_GANG       0x718
#define L3_M_BC_OVSR       0x740
#define L3_M_BC_IRQCTL     0x96C

/*
 * Bit field definitions
 */

/* L3_HML3_PM_CR */
#define PM_CR_RESET           (0)

/* L3_HML3_PM_XCNTCTL/L3_HML3_PM_CNTCTLx */
#define PMCNT_RESET           (0)

/* L3_HML3_PM_EVTYPEx */
#define EVSEL(__val)          ((__val) & L3_EVTYPE_MASK)

/* Reset value for all the filter registers */
#define PM_FLTR_RESET         (0)

/* L3_M_BC_CR */
#define BC_RESET              (1UL << 1)
#define BC_ENABLE             (1UL << 0)

/* L3_M_BC_SATROLL_CR */
#define BC_SATROLL_CR_RESET   (0)

/* L3_M_BC_CNTENSET */
#define PMCNTENSET(__cntr)    (1UL << ((__cntr) & 0x7))

/* L3_M_BC_CNTENCLR */
#define PMCNTENCLR(__cntr)    (1UL << ((__cntr) & 0x7))
#define BC_CNTENCLR_RESET     (0xFF)

/* L3_M_BC_INTENSET */
#define PMINTENSET(__cntr)    (1UL << ((__cntr) & 0x7))

/* L3_M_BC_INTENCLR */
#define PMINTENCLR(__cntr)    (1UL << ((__cntr) & 0x7))
#define BC_INTENCLR_RESET     (0xFF)

/* L3_M_BC_GANG */
#define GANG_EN(__cntr)       (1UL << ((__cntr) & 0x7))
#define BC_GANG_RESET         (0)

/* L3_M_BC_OVSR */
#define PMOVSRCLR(__cntr)     (1UL << ((__cntr) & 0x7))
#define PMOVSRCLR_RESET       (0xFF)

/* L3_M_BC_IRQCTL */
#define PMIRQONMSBEN(__cntr)  (1UL << ((__cntr) & 0x7))
#define BC_IRQCTL_RESET       (0x0)

/*
 * Events
 */

#define L3_EVENT_CYCLES		0x01
#define L3_EVENT_READ_HIT		0x20
#define L3_EVENT_READ_MISS		0x21
#define L3_EVENT_READ_HIT_D		0x22
#define L3_EVENT_READ_MISS_D		0x23
#define L3_EVENT_WRITE_HIT		0x24
#define L3_EVENT_WRITE_MISS		0x25

/*
 * Decoding of settings from perf_event_attr
 *
 * The config format for perf events is:
 * - config: bits 0-7: event type
 *           bit  32:  HW counter size requested, 0: 32 bits, 1: 64 bits
 */

static inline u32 get_event_type(struct perf_event *event)
{
	return (event->attr.config) & L3_EVTYPE_MASK;
}

static inline bool event_uses_long_counter(struct perf_event *event)
{
	return !!(event->attr.config & BIT_ULL(L3_EVENT_LC_BIT));
}

static inline int event_num_counters(struct perf_event *event)
{
	return event_uses_long_counter(event) ? 2 : 1;
}

/*
 * Main PMU, inherits from the core perf PMU type
 */
struct l3cache_pmu {
	struct pmu		pmu;
	struct hlist_node	node;
	void __iomem		*regs;
	struct perf_event	*events[L3_NUM_COUNTERS];
	unsigned long		used_mask[BITS_TO_LONGS(L3_NUM_COUNTERS)];
	cpumask_t		cpumask;
};

#define to_l3cache_pmu(p) (container_of(p, struct l3cache_pmu, pmu))

/*
 * Type used to group hardware counter operations
 *
 * Used to implement two types of hardware counters, standard (32bits) and
 * long (64bits). The hardware supports counter chaining which we use to
 * implement long counters. This support is exposed via the 'lc' flag field
 * in perf_event_attr.config.
 */
struct l3cache_event_ops {
	/* Called to start event monitoring */
	void (*start)(struct perf_event *event);
	/* Called to stop event monitoring */
	void (*stop)(struct perf_event *event, int flags);
	/* Called to update the perf_event */
	void (*update)(struct perf_event *event);
};

/*
 * Implementation of long counter operations
 *
 * 64bit counters are implemented by chaining two of the 32bit physical
 * counters. The PMU only supports chaining of adjacent even/odd pairs
 * and for simplicity the driver always configures the odd counter to
 * count the overflows of the lower-numbered even counter. Note that since
 * the resulting hardware counter is 64bits no IRQs are required to maintain
 * the software counter which is also 64bits.
 */

static void qcom_l3_cache__64bit_counter_start(struct perf_event *event)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(event->pmu);
	int idx = event->hw.idx;
	u32 evsel = get_event_type(event);
	u32 gang;

	/* Set the odd counter to count the overflows of the even counter */
	gang = readl_relaxed(l3pmu->regs + L3_M_BC_GANG);
	gang |= GANG_EN(idx + 1);
	writel_relaxed(gang, l3pmu->regs + L3_M_BC_GANG);

	/* Initialize the hardware counters and reset prev_count*/
	local64_set(&event->hw.prev_count, 0);
	writel_relaxed(0, l3pmu->regs + L3_HML3_PM_EVCNTR(idx + 1));
	writel_relaxed(0, l3pmu->regs + L3_HML3_PM_EVCNTR(idx));

	/*
	 * Set the event types, the upper half must use zero and the lower
	 * half the actual event type
	 */
	writel_relaxed(EVSEL(0), l3pmu->regs + L3_HML3_PM_EVTYPE(idx + 1));
	writel_relaxed(EVSEL(evsel), l3pmu->regs + L3_HML3_PM_EVTYPE(idx));

	/* Finally, enable the counters */
	writel_relaxed(PMCNT_RESET, l3pmu->regs + L3_HML3_PM_CNTCTL(idx + 1));
	writel_relaxed(PMCNTENSET(idx + 1), l3pmu->regs + L3_M_BC_CNTENSET);
	writel_relaxed(PMCNT_RESET, l3pmu->regs + L3_HML3_PM_CNTCTL(idx));
	writel_relaxed(PMCNTENSET(idx), l3pmu->regs + L3_M_BC_CNTENSET);
}

static void qcom_l3_cache__64bit_counter_stop(struct perf_event *event,
					      int flags)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(event->pmu);
	int idx = event->hw.idx;
	u32 gang = readl_relaxed(l3pmu->regs + L3_M_BC_GANG);

	/* Disable the counters */
	writel_relaxed(PMCNTENCLR(idx), l3pmu->regs + L3_M_BC_CNTENCLR);
	writel_relaxed(PMCNTENCLR(idx + 1), l3pmu->regs + L3_M_BC_CNTENCLR);

	/* Disable chaining */
	writel_relaxed(gang & ~GANG_EN(idx + 1), l3pmu->regs + L3_M_BC_GANG);
}

static void qcom_l3_cache__64bit_counter_update(struct perf_event *event)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(event->pmu);
	int idx = event->hw.idx;
	u32 hi, lo;
	u64 prev, new;

	do {
		prev = local64_read(&event->hw.prev_count);
		do {
			hi = readl_relaxed(l3pmu->regs + L3_HML3_PM_EVCNTR(idx + 1));
			lo = readl_relaxed(l3pmu->regs + L3_HML3_PM_EVCNTR(idx));
		} while (hi != readl_relaxed(l3pmu->regs + L3_HML3_PM_EVCNTR(idx + 1)));
		new = ((u64)hi << 32) | lo;
	} while (local64_cmpxchg(&event->hw.prev_count, prev, new) != prev);

	local64_add(new - prev, &event->count);
}

static const struct l3cache_event_ops event_ops_long = {
	.start = qcom_l3_cache__64bit_counter_start,
	.stop = qcom_l3_cache__64bit_counter_stop,
	.update = qcom_l3_cache__64bit_counter_update,
};

/*
 * Implementation of standard counter operations
 *
 * 32bit counters use a single physical counter and a hardware feature that
 * asserts the overflow IRQ on the toggling of the most significant bit in
 * the counter. This feature allows the counters to be left free-running
 * without needing the usual reprogramming required to properly handle races
 * during concurrent calls to update.
 */

static void qcom_l3_cache__32bit_counter_start(struct perf_event *event)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(event->pmu);
	int idx = event->hw.idx;
	u32 evsel = get_event_type(event);
	u32 irqctl = readl_relaxed(l3pmu->regs + L3_M_BC_IRQCTL);

	/* Set the counter to assert the overflow IRQ on MSB toggling */
	writel_relaxed(irqctl | PMIRQONMSBEN(idx), l3pmu->regs + L3_M_BC_IRQCTL);

	/* Initialize the hardware counter and reset prev_count*/
	local64_set(&event->hw.prev_count, 0);
	writel_relaxed(0, l3pmu->regs + L3_HML3_PM_EVCNTR(idx));

	/* Set the event type */
	writel_relaxed(EVSEL(evsel), l3pmu->regs + L3_HML3_PM_EVTYPE(idx));

	/* Enable interrupt generation by this counter */
	writel_relaxed(PMINTENSET(idx), l3pmu->regs + L3_M_BC_INTENSET);

	/* Finally, enable the counter */
	writel_relaxed(PMCNT_RESET, l3pmu->regs + L3_HML3_PM_CNTCTL(idx));
	writel_relaxed(PMCNTENSET(idx), l3pmu->regs + L3_M_BC_CNTENSET);
}

static void qcom_l3_cache__32bit_counter_stop(struct perf_event *event,
					      int flags)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(event->pmu);
	int idx = event->hw.idx;
	u32 irqctl = readl_relaxed(l3pmu->regs + L3_M_BC_IRQCTL);

	/* Disable the counter */
	writel_relaxed(PMCNTENCLR(idx), l3pmu->regs + L3_M_BC_CNTENCLR);

	/* Disable interrupt generation by this counter */
	writel_relaxed(PMINTENCLR(idx), l3pmu->regs + L3_M_BC_INTENCLR);

	/* Set the counter to not assert the overflow IRQ on MSB toggling */
	writel_relaxed(irqctl & ~PMIRQONMSBEN(idx), l3pmu->regs + L3_M_BC_IRQCTL);
}

static void qcom_l3_cache__32bit_counter_update(struct perf_event *event)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(event->pmu);
	int idx = event->hw.idx;
	u32 prev, new;

	do {
		prev = local64_read(&event->hw.prev_count);
		new = readl_relaxed(l3pmu->regs + L3_HML3_PM_EVCNTR(idx));
	} while (local64_cmpxchg(&event->hw.prev_count, prev, new) != prev);

	local64_add(new - prev, &event->count);
}

static const struct l3cache_event_ops event_ops_std = {
	.start = qcom_l3_cache__32bit_counter_start,
	.stop = qcom_l3_cache__32bit_counter_stop,
	.update = qcom_l3_cache__32bit_counter_update,
};

/* Retrieve the appropriate operations for the given event */
static
const struct l3cache_event_ops *l3cache_event_get_ops(struct perf_event *event)
{
	if (event_uses_long_counter(event))
		return &event_ops_long;
	else
		return &event_ops_std;
}

/*
 * Top level PMU functions.
 */

static inline void qcom_l3_cache__init(struct l3cache_pmu *l3pmu)
{
	int i;

	writel_relaxed(BC_RESET, l3pmu->regs + L3_M_BC_CR);

	/*
	 * Use writel for the first programming command to ensure the basic
	 * counter unit is stopped before proceeding
	 */
	writel(BC_SATROLL_CR_RESET, l3pmu->regs + L3_M_BC_SATROLL_CR);

	writel_relaxed(BC_CNTENCLR_RESET, l3pmu->regs + L3_M_BC_CNTENCLR);
	writel_relaxed(BC_INTENCLR_RESET, l3pmu->regs + L3_M_BC_INTENCLR);
	writel_relaxed(PMOVSRCLR_RESET, l3pmu->regs + L3_M_BC_OVSR);
	writel_relaxed(BC_GANG_RESET, l3pmu->regs + L3_M_BC_GANG);
	writel_relaxed(BC_IRQCTL_RESET, l3pmu->regs + L3_M_BC_IRQCTL);
	writel_relaxed(PM_CR_RESET, l3pmu->regs + L3_HML3_PM_CR);

	for (i = 0; i < L3_NUM_COUNTERS; ++i) {
		writel_relaxed(PMCNT_RESET, l3pmu->regs + L3_HML3_PM_CNTCTL(i));
		writel_relaxed(EVSEL(0), l3pmu->regs + L3_HML3_PM_EVTYPE(i));
	}

	writel_relaxed(PM_FLTR_RESET, l3pmu->regs + L3_HML3_PM_FILTRA);
	writel_relaxed(PM_FLTR_RESET, l3pmu->regs + L3_HML3_PM_FILTRAM);
	writel_relaxed(PM_FLTR_RESET, l3pmu->regs + L3_HML3_PM_FILTRB);
	writel_relaxed(PM_FLTR_RESET, l3pmu->regs + L3_HML3_PM_FILTRBM);
	writel_relaxed(PM_FLTR_RESET, l3pmu->regs + L3_HML3_PM_FILTRC);
	writel_relaxed(PM_FLTR_RESET, l3pmu->regs + L3_HML3_PM_FILTRCM);

	/*
	 * Use writel here to ensure all programming commands are done
	 *  before proceeding
	 */
	writel(BC_ENABLE, l3pmu->regs + L3_M_BC_CR);
}

static irqreturn_t qcom_l3_cache__handle_irq(int irq_num, void *data)
{
	struct l3cache_pmu *l3pmu = data;
	/* Read the overflow status register */
	long status = readl_relaxed(l3pmu->regs + L3_M_BC_OVSR);
	int idx;

	if (status == 0)
		return IRQ_NONE;

	/* Clear the bits we read on the overflow status register */
	writel_relaxed(status, l3pmu->regs + L3_M_BC_OVSR);

	for_each_set_bit(idx, &status, L3_NUM_COUNTERS) {
		struct perf_event *event;
		const struct l3cache_event_ops *ops;

		event = l3pmu->events[idx];
		if (!event)
			continue;

		/*
		 * Since the IRQ is not enabled for events using long counters
		 * we should never see one of those here, however, be consistent
		 * and use the ops indirections like in the other operations.
		 */

		ops = l3cache_event_get_ops(event);
		ops->update(event);
	}

	return IRQ_HANDLED;
}

/*
 * Implementation of abstract pmu functionality required by
 * the core perf events code.
 */

static void qcom_l3_cache__pmu_enable(struct pmu *pmu)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(pmu);

	/* Ensure the other programming commands are observed before enabling */
	wmb();

	writel_relaxed(BC_ENABLE, l3pmu->regs + L3_M_BC_CR);
}

static void qcom_l3_cache__pmu_disable(struct pmu *pmu)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(pmu);

	writel_relaxed(0, l3pmu->regs + L3_M_BC_CR);

	/* Ensure the basic counter unit is stopped before proceeding */
	wmb();
}

/*
 * We must NOT create groups containing events from multiple hardware PMUs,
 * although mixing different software and hardware PMUs is allowed.
 */
static bool qcom_l3_cache__validate_event_group(struct perf_event *event)
{
	struct perf_event *leader = event->group_leader;
	struct perf_event *sibling;
	int counters = 0;

	if (leader->pmu != event->pmu && !is_software_event(leader))
		return false;

	counters = event_num_counters(event);
	counters += event_num_counters(leader);

	list_for_each_entry(sibling, &leader->sibling_list, sibling_list) {
		if (is_software_event(sibling))
			continue;
		if (sibling->pmu != event->pmu)
			return false;
		counters += event_num_counters(sibling);
	}

	/*
	 * If the group requires more counters than the HW has, it
	 * cannot ever be scheduled.
	 */
	return counters <= L3_NUM_COUNTERS;
}

static int qcom_l3_cache__event_init(struct perf_event *event)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	/*
	 * Is the event for this PMU?
	 */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/*
	 * There are no per-counter mode filters in the PMU.
	 */
	if (event->attr.exclude_user || event->attr.exclude_kernel ||
	    event->attr.exclude_hv || event->attr.exclude_idle)
		return -EINVAL;

	/*
	 * Sampling not supported since these events are not core-attributable.
	 */
	if (hwc->sample_period)
		return -EINVAL;

	/*
	 * Task mode not available, we run the counters as socket counters,
	 * not attributable to any CPU and therefore cannot attribute per-task.
	 */
	if (event->cpu < 0)
		return -EINVAL;

	/* Validate the group */
	if (!qcom_l3_cache__validate_event_group(event))
		return -EINVAL;

	hwc->idx = -1;

	/*
	 * Many perf core operations (eg. events rotation) operate on a
	 * single CPU context. This is obvious for CPU PMUs, where one
	 * expects the same sets of events being observed on all CPUs,
	 * but can lead to issues for off-core PMUs, like this one, where
	 * each event could be theoretically assigned to a different CPU.
	 * To mitigate this, we enforce CPU assignment to one designated
	 * processor (the one described in the "cpumask" attribute exported
	 * by the PMU device). perf user space tools honor this and avoid
	 * opening more than one copy of the events.
	 */
	event->cpu = cpumask_first(&l3pmu->cpumask);

	return 0;
}

static void qcom_l3_cache__event_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	const struct l3cache_event_ops *ops = l3cache_event_get_ops(event);

	hwc->state = 0;
	ops->start(event);
}

static void qcom_l3_cache__event_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	const struct l3cache_event_ops *ops = l3cache_event_get_ops(event);

	if (hwc->state & PERF_HES_STOPPED)
		return;

	ops->stop(event, flags);
	if (flags & PERF_EF_UPDATE)
		ops->update(event);
	hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static int qcom_l3_cache__event_add(struct perf_event *event, int flags)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int order = event_uses_long_counter(event) ? 1 : 0;
	int idx;

	/*
	 * Try to allocate a counter.
	 */
	idx = bitmap_find_free_region(l3pmu->used_mask, L3_NUM_COUNTERS, order);
	if (idx < 0)
		/* The counters are all in use. */
		return -EAGAIN;

	hwc->idx = idx;
	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
	l3pmu->events[idx] = event;

	if (flags & PERF_EF_START)
		qcom_l3_cache__event_start(event, 0);

	/* Propagate changes to the userspace mapping. */
	perf_event_update_userpage(event);

	return 0;
}

static void qcom_l3_cache__event_del(struct perf_event *event, int flags)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int order = event_uses_long_counter(event) ? 1 : 0;

	/* Stop and clean up */
	qcom_l3_cache__event_stop(event,  flags | PERF_EF_UPDATE);
	l3pmu->events[hwc->idx] = NULL;
	bitmap_release_region(l3pmu->used_mask, hwc->idx, order);

	/* Propagate changes to the userspace mapping. */
	perf_event_update_userpage(event);
}

static void qcom_l3_cache__event_read(struct perf_event *event)
{
	const struct l3cache_event_ops *ops = l3cache_event_get_ops(event);

	ops->update(event);
}

/*
 * Add sysfs attributes
 *
 * We export:
 * - formats, used by perf user space and other tools to configure events
 * - events, used by perf user space and other tools to create events
 *   symbolically, e.g.:
 *     perf stat -a -e l3cache_0_0/event=read-miss/ ls
 *     perf stat -a -e l3cache_0_0/event=0x21/ ls
 * - cpumask, used by perf user space and other tools to know on which CPUs
 *   to open the events
 */

/* formats */

static ssize_t l3cache_pmu_format_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);
	return sprintf(buf, "%s\n", (char *) eattr->var);
}

#define L3CACHE_PMU_FORMAT_ATTR(_name, _config)				      \
	(&((struct dev_ext_attribute[]) {				      \
		{ .attr = __ATTR(_name, 0444, l3cache_pmu_format_show, NULL), \
		  .var = (void *) _config, }				      \
	})[0].attr.attr)

static struct attribute *qcom_l3_cache_pmu_formats[] = {
	L3CACHE_PMU_FORMAT_ATTR(event, "config:0-7"),
	L3CACHE_PMU_FORMAT_ATTR(lc, "config:" __stringify(L3_EVENT_LC_BIT)),
	NULL,
};

static struct attribute_group qcom_l3_cache_pmu_format_group = {
	.name = "format",
	.attrs = qcom_l3_cache_pmu_formats,
};

/* events */

static ssize_t l3cache_pmu_event_show(struct device *dev,
				     struct device_attribute *attr, char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sprintf(page, "event=0x%02llx\n", pmu_attr->id);
}

#define L3CACHE_EVENT_ATTR(_name, _id)					     \
	(&((struct perf_pmu_events_attr[]) {				     \
		{ .attr = __ATTR(_name, 0444, l3cache_pmu_event_show, NULL), \
		  .id = _id, }						     \
	})[0].attr.attr)

static struct attribute *qcom_l3_cache_pmu_events[] = {
	L3CACHE_EVENT_ATTR(cycles, L3_EVENT_CYCLES),
	L3CACHE_EVENT_ATTR(read-hit, L3_EVENT_READ_HIT),
	L3CACHE_EVENT_ATTR(read-miss, L3_EVENT_READ_MISS),
	L3CACHE_EVENT_ATTR(read-hit-d-side, L3_EVENT_READ_HIT_D),
	L3CACHE_EVENT_ATTR(read-miss-d-side, L3_EVENT_READ_MISS_D),
	L3CACHE_EVENT_ATTR(write-hit, L3_EVENT_WRITE_HIT),
	L3CACHE_EVENT_ATTR(write-miss, L3_EVENT_WRITE_MISS),
	NULL
};

static struct attribute_group qcom_l3_cache_pmu_events_group = {
	.name = "events",
	.attrs = qcom_l3_cache_pmu_events,
};

/* cpumask */

static ssize_t qcom_l3_cache_pmu_cpumask_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct l3cache_pmu *l3pmu = to_l3cache_pmu(dev_get_drvdata(dev));

	return cpumap_print_to_pagebuf(true, buf, &l3pmu->cpumask);
}

static DEVICE_ATTR(cpumask, 0444, qcom_l3_cache_pmu_cpumask_show, NULL);

static struct attribute *qcom_l3_cache_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group qcom_l3_cache_pmu_cpumask_attr_group = {
	.attrs = qcom_l3_cache_pmu_cpumask_attrs,
};

/*
 * Per PMU device attribute groups
 */
static const struct attribute_group *qcom_l3_cache_pmu_attr_grps[] = {
	&qcom_l3_cache_pmu_format_group,
	&qcom_l3_cache_pmu_events_group,
	&qcom_l3_cache_pmu_cpumask_attr_group,
	NULL,
};

/*
 * Probing functions and data.
 */

static int qcom_l3_cache_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct l3cache_pmu *l3pmu = hlist_entry_safe(node, struct l3cache_pmu, node);

	/* If there is not a CPU/PMU association pick this CPU */
	if (cpumask_empty(&l3pmu->cpumask))
		cpumask_set_cpu(cpu, &l3pmu->cpumask);

	return 0;
}

static int qcom_l3_cache_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct l3cache_pmu *l3pmu = hlist_entry_safe(node, struct l3cache_pmu, node);
	unsigned int target;

	if (!cpumask_test_and_clear_cpu(cpu, &l3pmu->cpumask))
		return 0;
	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;
	perf_pmu_migrate_context(&l3pmu->pmu, cpu, target);
	cpumask_set_cpu(target, &l3pmu->cpumask);
	return 0;
}

static int qcom_l3_cache_pmu_probe(struct platform_device *pdev)
{
	struct l3cache_pmu *l3pmu;
	struct acpi_device *acpi_dev;
	struct resource *memrc;
	int ret;
	char *name;

	/* Initialize the PMU data structures */

	acpi_dev = ACPI_COMPANION(&pdev->dev);
	if (!acpi_dev)
		return -ENODEV;

	l3pmu = devm_kzalloc(&pdev->dev, sizeof(*l3pmu), GFP_KERNEL);
	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "l3cache_%s_%s",
		      acpi_dev->parent->pnp.unique_id, acpi_dev->pnp.unique_id);
	if (!l3pmu || !name)
		return -ENOMEM;

	l3pmu->pmu = (struct pmu) {
		.task_ctx_nr	= perf_invalid_context,

		.pmu_enable	= qcom_l3_cache__pmu_enable,
		.pmu_disable	= qcom_l3_cache__pmu_disable,
		.event_init	= qcom_l3_cache__event_init,
		.add		= qcom_l3_cache__event_add,
		.del		= qcom_l3_cache__event_del,
		.start		= qcom_l3_cache__event_start,
		.stop		= qcom_l3_cache__event_stop,
		.read		= qcom_l3_cache__event_read,

		.attr_groups	= qcom_l3_cache_pmu_attr_grps,
	};

	memrc = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	l3pmu->regs = devm_ioremap_resource(&pdev->dev, memrc);
	if (IS_ERR(l3pmu->regs)) {
		dev_err(&pdev->dev, "Can't map PMU @%pa\n", &memrc->start);
		return PTR_ERR(l3pmu->regs);
	}

	qcom_l3_cache__init(l3pmu);

	ret = platform_get_irq(pdev, 0);
	if (ret <= 0)
		return ret;

	ret = devm_request_irq(&pdev->dev, ret, qcom_l3_cache__handle_irq, 0,
			       name, l3pmu);
	if (ret) {
		dev_err(&pdev->dev, "Request for IRQ failed for slice @%pa\n",
			&memrc->start);
		return ret;
	}

	/* Add this instance to the list used by the offline callback */
	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_ARM_QCOM_L3_ONLINE, &l3pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug", ret);
		return ret;
	}

	ret = perf_pmu_register(&l3pmu->pmu, name, -1);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register L3 cache PMU (%d)\n", ret);
		return ret;
	}

	dev_info(&pdev->dev, "Registered %s, type: %d\n", name, l3pmu->pmu.type);

	return 0;
}

static const struct acpi_device_id qcom_l3_cache_pmu_acpi_match[] = {
	{ "QCOM8081", },
	{ }
};
MODULE_DEVICE_TABLE(acpi, qcom_l3_cache_pmu_acpi_match);

static struct platform_driver qcom_l3_cache_pmu_driver = {
	.driver = {
		.name = "qcom-l3cache-pmu",
		.acpi_match_table = ACPI_PTR(qcom_l3_cache_pmu_acpi_match),
	},
	.probe = qcom_l3_cache_pmu_probe,
};

static int __init register_qcom_l3_cache_pmu_driver(void)
{
	int ret;

	/* Install a hook to update the reader CPU in case it goes offline */
	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_QCOM_L3_ONLINE,
				      "perf/qcom/l3cache:online",
				      qcom_l3_cache_pmu_online_cpu,
				      qcom_l3_cache_pmu_offline_cpu);
	if (ret)
		return ret;

	return platform_driver_register(&qcom_l3_cache_pmu_driver);
}
device_initcall(register_qcom_l3_cache_pmu_driver);
