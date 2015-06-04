/*
 * CCI cache coherent interconnect driver
 *
 * Copyright (C) 2013 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/arm-cci.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>

static void __iomem *cci_ctrl_base;
static unsigned long cci_ctrl_phys;

#ifdef CONFIG_ARM_CCI400_PORT_CTRL
struct cci_nb_ports {
	unsigned int nb_ace;
	unsigned int nb_ace_lite;
};

static const struct cci_nb_ports cci400_ports = {
	.nb_ace = 2,
	.nb_ace_lite = 3
};

#define CCI400_PORTS_DATA	(&cci400_ports)
#else
#define CCI400_PORTS_DATA	(NULL)
#endif

static const struct of_device_id arm_cci_matches[] = {
#ifdef CONFIG_ARM_CCI400_COMMON
	{.compatible = "arm,cci-400", .data = CCI400_PORTS_DATA },
#endif
	{},
};

#ifdef CONFIG_ARM_CCI400_PMU

#define DRIVER_NAME		"CCI-400"
#define DRIVER_NAME_PMU		DRIVER_NAME " PMU"

#define CCI_PMCR		0x0100
#define CCI_PID2		0x0fe8

#define CCI_PMCR_CEN		0x00000001
#define CCI_PMCR_NCNT_MASK	0x0000f800
#define CCI_PMCR_NCNT_SHIFT	11

#define CCI_PID2_REV_MASK	0xf0
#define CCI_PID2_REV_SHIFT	4

#define CCI_PMU_EVT_SEL		0x000
#define CCI_PMU_CNTR		0x004
#define CCI_PMU_CNTR_CTRL	0x008
#define CCI_PMU_OVRFLW		0x00c

#define CCI_PMU_OVRFLW_FLAG	1

#define CCI_PMU_CNTR_BASE(idx)	((idx) * SZ_4K)

#define CCI_PMU_CNTR_MASK	((1ULL << 32) -1)

#define CCI_PMU_EVENT_MASK		0xffUL
#define CCI_PMU_EVENT_SOURCE(event)	((event >> 5) & 0x7)
#define CCI_PMU_EVENT_CODE(event)	(event & 0x1f)

#define CCI_PMU_MAX_HW_EVENTS 5   /* CCI PMU has 4 counters + 1 cycle counter */

/* Types of interfaces that can generate events */
enum {
	CCI_IF_SLAVE,
	CCI_IF_MASTER,
	CCI_IF_MAX,
};

struct event_range {
	u32 min;
	u32 max;
};

struct cci_pmu_hw_events {
	struct perf_event *events[CCI_PMU_MAX_HW_EVENTS];
	unsigned long used_mask[BITS_TO_LONGS(CCI_PMU_MAX_HW_EVENTS)];
	raw_spinlock_t pmu_lock;
};

struct cci_pmu_model {
	char *name;
	struct event_range event_ranges[CCI_IF_MAX];
};

static struct cci_pmu_model cci_pmu_models[];

struct cci_pmu {
	void __iomem *base;
	struct pmu pmu;
	int nr_irqs;
	int irqs[CCI_PMU_MAX_HW_EVENTS];
	unsigned long active_irqs;
	const struct cci_pmu_model *model;
	struct cci_pmu_hw_events hw_events;
	struct platform_device *plat_device;
	int num_events;
	atomic_t active_events;
	struct mutex reserve_mutex;
	cpumask_t cpus;
};
static struct cci_pmu *pmu;

#define to_cci_pmu(c)	(container_of(c, struct cci_pmu, pmu))

/* Port ids */
#define CCI_PORT_S0	0
#define CCI_PORT_S1	1
#define CCI_PORT_S2	2
#define CCI_PORT_S3	3
#define CCI_PORT_S4	4
#define CCI_PORT_M0	5
#define CCI_PORT_M1	6
#define CCI_PORT_M2	7

#define CCI_REV_R0		0
#define CCI_REV_R1		1
#define CCI_REV_R1_PX		5

/*
 * Instead of an event id to monitor CCI cycles, a dedicated counter is
 * provided. Use 0xff to represent CCI cycles and hope that no future revisions
 * make use of this event in hardware.
 */
enum cci400_perf_events {
	CCI_PMU_CYCLES = 0xff
};

#define CCI_PMU_CYCLE_CNTR_IDX		0
#define CCI_PMU_CNTR0_IDX		1
#define CCI_PMU_CNTR_LAST(cci_pmu)	(CCI_PMU_CYCLE_CNTR_IDX + cci_pmu->num_events - 1)

/*
 * CCI PMU event id is an 8-bit value made of two parts - bits 7:5 for one of 8
 * ports and bits 4:0 are event codes. There are different event codes
 * associated with each port type.
 *
 * Additionally, the range of events associated with the port types changed
 * between Rev0 and Rev1.
 *
 * The constants below define the range of valid codes for each port type for
 * the different revisions and are used to validate the event to be monitored.
 */

#define CCI_REV_R0_SLAVE_PORT_MIN_EV	0x00
#define CCI_REV_R0_SLAVE_PORT_MAX_EV	0x13
#define CCI_REV_R0_MASTER_PORT_MIN_EV	0x14
#define CCI_REV_R0_MASTER_PORT_MAX_EV	0x1a

#define CCI_REV_R1_SLAVE_PORT_MIN_EV	0x00
#define CCI_REV_R1_SLAVE_PORT_MAX_EV	0x14
#define CCI_REV_R1_MASTER_PORT_MIN_EV	0x00
#define CCI_REV_R1_MASTER_PORT_MAX_EV	0x11

static int pmu_validate_hw_event(unsigned long hw_event)
{
	u8 ev_source = CCI_PMU_EVENT_SOURCE(hw_event);
	u8 ev_code = CCI_PMU_EVENT_CODE(hw_event);
	int if_type;

	if (hw_event & ~CCI_PMU_EVENT_MASK)
		return -ENOENT;

	switch (ev_source) {
	case CCI_PORT_S0:
	case CCI_PORT_S1:
	case CCI_PORT_S2:
	case CCI_PORT_S3:
	case CCI_PORT_S4:
		/* Slave Interface */
		if_type = CCI_IF_SLAVE;
		break;
	case CCI_PORT_M0:
	case CCI_PORT_M1:
	case CCI_PORT_M2:
		/* Master Interface */
		if_type = CCI_IF_MASTER;
		break;
	default:
		return -ENOENT;
	}

	if (ev_code >= pmu->model->event_ranges[if_type].min &&
		ev_code <= pmu->model->event_ranges[if_type].max)
		return hw_event;

	return -ENOENT;
}

static int probe_cci_revision(void)
{
	int rev;
	rev = readl_relaxed(cci_ctrl_base + CCI_PID2) & CCI_PID2_REV_MASK;
	rev >>= CCI_PID2_REV_SHIFT;

	if (rev < CCI_REV_R1_PX)
		return CCI_REV_R0;
	else
		return CCI_REV_R1;
}

static const struct cci_pmu_model *probe_cci_model(struct platform_device *pdev)
{
	if (platform_has_secure_cci_access())
		return &cci_pmu_models[probe_cci_revision()];
	return NULL;
}

static int pmu_is_valid_counter(struct cci_pmu *cci_pmu, int idx)
{
	return CCI_PMU_CYCLE_CNTR_IDX <= idx &&
		idx <= CCI_PMU_CNTR_LAST(cci_pmu);
}

static u32 pmu_read_register(int idx, unsigned int offset)
{
	return readl_relaxed(pmu->base + CCI_PMU_CNTR_BASE(idx) + offset);
}

static void pmu_write_register(u32 value, int idx, unsigned int offset)
{
	return writel_relaxed(value, pmu->base + CCI_PMU_CNTR_BASE(idx) + offset);
}

static void pmu_disable_counter(int idx)
{
	pmu_write_register(0, idx, CCI_PMU_CNTR_CTRL);
}

static void pmu_enable_counter(int idx)
{
	pmu_write_register(1, idx, CCI_PMU_CNTR_CTRL);
}

static void pmu_set_event(int idx, unsigned long event)
{
	pmu_write_register(event, idx, CCI_PMU_EVT_SEL);
}

static u32 pmu_get_max_counters(void)
{
	u32 n_cnts = (readl_relaxed(cci_ctrl_base + CCI_PMCR) &
		      CCI_PMCR_NCNT_MASK) >> CCI_PMCR_NCNT_SHIFT;

	/* add 1 for cycle counter */
	return n_cnts + 1;
}

static int pmu_get_event_idx(struct cci_pmu_hw_events *hw, struct perf_event *event)
{
	struct cci_pmu *cci_pmu = to_cci_pmu(event->pmu);
	struct hw_perf_event *hw_event = &event->hw;
	unsigned long cci_event = hw_event->config_base;
	int idx;

	if (cci_event == CCI_PMU_CYCLES) {
		if (test_and_set_bit(CCI_PMU_CYCLE_CNTR_IDX, hw->used_mask))
			return -EAGAIN;

		return CCI_PMU_CYCLE_CNTR_IDX;
	}

	for (idx = CCI_PMU_CNTR0_IDX; idx <= CCI_PMU_CNTR_LAST(cci_pmu); ++idx)
		if (!test_and_set_bit(idx, hw->used_mask))
			return idx;

	/* No counters available */
	return -EAGAIN;
}

static int pmu_map_event(struct perf_event *event)
{
	int mapping;
	unsigned long config = event->attr.config;

	if (event->attr.type < PERF_TYPE_MAX)
		return -ENOENT;

	if (config == CCI_PMU_CYCLES)
		mapping = config;
	else
		mapping = pmu_validate_hw_event(config);

	return mapping;
}

static int pmu_request_irq(struct cci_pmu *cci_pmu, irq_handler_t handler)
{
	int i;
	struct platform_device *pmu_device = cci_pmu->plat_device;

	if (unlikely(!pmu_device))
		return -ENODEV;

	if (pmu->nr_irqs < 1) {
		dev_err(&pmu_device->dev, "no irqs for CCI PMUs defined\n");
		return -ENODEV;
	}

	/*
	 * Register all available CCI PMU interrupts. In the interrupt handler
	 * we iterate over the counters checking for interrupt source (the
	 * overflowing counter) and clear it.
	 *
	 * This should allow handling of non-unique interrupt for the counters.
	 */
	for (i = 0; i < pmu->nr_irqs; i++) {
		int err = request_irq(pmu->irqs[i], handler, IRQF_SHARED,
				"arm-cci-pmu", cci_pmu);
		if (err) {
			dev_err(&pmu_device->dev, "unable to request IRQ%d for ARM CCI PMU counters\n",
				pmu->irqs[i]);
			return err;
		}

		set_bit(i, &pmu->active_irqs);
	}

	return 0;
}

static void pmu_free_irq(struct cci_pmu *cci_pmu)
{
	int i;

	for (i = 0; i < pmu->nr_irqs; i++) {
		if (!test_and_clear_bit(i, &pmu->active_irqs))
			continue;

		free_irq(pmu->irqs[i], cci_pmu);
	}
}

static u32 pmu_read_counter(struct perf_event *event)
{
	struct cci_pmu *cci_pmu = to_cci_pmu(event->pmu);
	struct hw_perf_event *hw_counter = &event->hw;
	int idx = hw_counter->idx;
	u32 value;

	if (unlikely(!pmu_is_valid_counter(cci_pmu, idx))) {
		dev_err(&cci_pmu->plat_device->dev, "Invalid CCI PMU counter %d\n", idx);
		return 0;
	}
	value = pmu_read_register(idx, CCI_PMU_CNTR);

	return value;
}

static void pmu_write_counter(struct perf_event *event, u32 value)
{
	struct cci_pmu *cci_pmu = to_cci_pmu(event->pmu);
	struct hw_perf_event *hw_counter = &event->hw;
	int idx = hw_counter->idx;

	if (unlikely(!pmu_is_valid_counter(cci_pmu, idx)))
		dev_err(&cci_pmu->plat_device->dev, "Invalid CCI PMU counter %d\n", idx);
	else
		pmu_write_register(value, idx, CCI_PMU_CNTR);
}

static u64 pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev_raw_count, new_raw_count;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = pmu_read_counter(event);
	} while (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
		 new_raw_count) != prev_raw_count);

	delta = (new_raw_count - prev_raw_count) & CCI_PMU_CNTR_MASK;

	local64_add(delta, &event->count);

	return new_raw_count;
}

static void pmu_read(struct perf_event *event)
{
	pmu_event_update(event);
}

void pmu_event_set_period(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	/*
	 * The CCI PMU counters have a period of 2^32. To account for the
	 * possiblity of extreme interrupt latency we program for a period of
	 * half that. Hopefully we can handle the interrupt before another 2^31
	 * events occur and the counter overtakes its previous value.
	 */
	u64 val = 1ULL << 31;
	local64_set(&hwc->prev_count, val);
	pmu_write_counter(event, val);
}

static irqreturn_t pmu_handle_irq(int irq_num, void *dev)
{
	unsigned long flags;
	struct cci_pmu *cci_pmu = dev;
	struct cci_pmu_hw_events *events = &pmu->hw_events;
	int idx, handled = IRQ_NONE;

	raw_spin_lock_irqsave(&events->pmu_lock, flags);
	/*
	 * Iterate over counters and update the corresponding perf events.
	 * This should work regardless of whether we have per-counter overflow
	 * interrupt or a combined overflow interrupt.
	 */
	for (idx = CCI_PMU_CYCLE_CNTR_IDX; idx <= CCI_PMU_CNTR_LAST(cci_pmu); idx++) {
		struct perf_event *event = events->events[idx];
		struct hw_perf_event *hw_counter;

		if (!event)
			continue;

		hw_counter = &event->hw;

		/* Did this counter overflow? */
		if (!(pmu_read_register(idx, CCI_PMU_OVRFLW) &
		      CCI_PMU_OVRFLW_FLAG))
			continue;

		pmu_write_register(CCI_PMU_OVRFLW_FLAG, idx, CCI_PMU_OVRFLW);

		pmu_event_update(event);
		pmu_event_set_period(event);
		handled = IRQ_HANDLED;
	}
	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);

	return IRQ_RETVAL(handled);
}

static int cci_pmu_get_hw(struct cci_pmu *cci_pmu)
{
	int ret = pmu_request_irq(cci_pmu, pmu_handle_irq);
	if (ret) {
		pmu_free_irq(cci_pmu);
		return ret;
	}
	return 0;
}

static void cci_pmu_put_hw(struct cci_pmu *cci_pmu)
{
	pmu_free_irq(cci_pmu);
}

static void hw_perf_event_destroy(struct perf_event *event)
{
	struct cci_pmu *cci_pmu = to_cci_pmu(event->pmu);
	atomic_t *active_events = &cci_pmu->active_events;
	struct mutex *reserve_mutex = &cci_pmu->reserve_mutex;

	if (atomic_dec_and_mutex_lock(active_events, reserve_mutex)) {
		cci_pmu_put_hw(cci_pmu);
		mutex_unlock(reserve_mutex);
	}
}

static void cci_pmu_enable(struct pmu *pmu)
{
	struct cci_pmu *cci_pmu = to_cci_pmu(pmu);
	struct cci_pmu_hw_events *hw_events = &cci_pmu->hw_events;
	int enabled = bitmap_weight(hw_events->used_mask, cci_pmu->num_events);
	unsigned long flags;
	u32 val;

	if (!enabled)
		return;

	raw_spin_lock_irqsave(&hw_events->pmu_lock, flags);

	/* Enable all the PMU counters. */
	val = readl_relaxed(cci_ctrl_base + CCI_PMCR) | CCI_PMCR_CEN;
	writel(val, cci_ctrl_base + CCI_PMCR);
	raw_spin_unlock_irqrestore(&hw_events->pmu_lock, flags);

}

static void cci_pmu_disable(struct pmu *pmu)
{
	struct cci_pmu *cci_pmu = to_cci_pmu(pmu);
	struct cci_pmu_hw_events *hw_events = &cci_pmu->hw_events;
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&hw_events->pmu_lock, flags);

	/* Disable all the PMU counters. */
	val = readl_relaxed(cci_ctrl_base + CCI_PMCR) & ~CCI_PMCR_CEN;
	writel(val, cci_ctrl_base + CCI_PMCR);
	raw_spin_unlock_irqrestore(&hw_events->pmu_lock, flags);
}

static void cci_pmu_start(struct perf_event *event, int pmu_flags)
{
	struct cci_pmu *cci_pmu = to_cci_pmu(event->pmu);
	struct cci_pmu_hw_events *hw_events = &cci_pmu->hw_events;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	unsigned long flags;

	/*
	 * To handle interrupt latency, we always reprogram the period
	 * regardlesss of PERF_EF_RELOAD.
	 */
	if (pmu_flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));

	hwc->state = 0;

	if (unlikely(!pmu_is_valid_counter(cci_pmu, idx))) {
		dev_err(&cci_pmu->plat_device->dev, "Invalid CCI PMU counter %d\n", idx);
		return;
	}

	raw_spin_lock_irqsave(&hw_events->pmu_lock, flags);

	/* Configure the event to count, unless you are counting cycles */
	if (idx != CCI_PMU_CYCLE_CNTR_IDX)
		pmu_set_event(idx, hwc->config_base);

	pmu_event_set_period(event);
	pmu_enable_counter(idx);

	raw_spin_unlock_irqrestore(&hw_events->pmu_lock, flags);
}

static void cci_pmu_stop(struct perf_event *event, int pmu_flags)
{
	struct cci_pmu *cci_pmu = to_cci_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (hwc->state & PERF_HES_STOPPED)
		return;

	if (unlikely(!pmu_is_valid_counter(cci_pmu, idx))) {
		dev_err(&cci_pmu->plat_device->dev, "Invalid CCI PMU counter %d\n", idx);
		return;
	}

	/*
	 * We always reprogram the counter, so ignore PERF_EF_UPDATE. See
	 * cci_pmu_start()
	 */
	pmu_disable_counter(idx);
	pmu_event_update(event);
	hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static int cci_pmu_add(struct perf_event *event, int flags)
{
	struct cci_pmu *cci_pmu = to_cci_pmu(event->pmu);
	struct cci_pmu_hw_events *hw_events = &cci_pmu->hw_events;
	struct hw_perf_event *hwc = &event->hw;
	int idx;
	int err = 0;

	perf_pmu_disable(event->pmu);

	/* If we don't have a space for the counter then finish early. */
	idx = pmu_get_event_idx(hw_events, event);
	if (idx < 0) {
		err = idx;
		goto out;
	}

	event->hw.idx = idx;
	hw_events->events[idx] = event;

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
	if (flags & PERF_EF_START)
		cci_pmu_start(event, PERF_EF_RELOAD);

	/* Propagate our changes to the userspace mapping. */
	perf_event_update_userpage(event);

out:
	perf_pmu_enable(event->pmu);
	return err;
}

static void cci_pmu_del(struct perf_event *event, int flags)
{
	struct cci_pmu *cci_pmu = to_cci_pmu(event->pmu);
	struct cci_pmu_hw_events *hw_events = &cci_pmu->hw_events;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	cci_pmu_stop(event, PERF_EF_UPDATE);
	hw_events->events[idx] = NULL;
	clear_bit(idx, hw_events->used_mask);

	perf_event_update_userpage(event);
}

static int
validate_event(struct pmu *cci_pmu,
               struct cci_pmu_hw_events *hw_events,
               struct perf_event *event)
{
	if (is_software_event(event))
		return 1;

	/*
	 * Reject groups spanning multiple HW PMUs (e.g. CPU + CCI). The
	 * core perf code won't check that the pmu->ctx == leader->ctx
	 * until after pmu->event_init(event).
	 */
	if (event->pmu != cci_pmu)
		return 0;

	if (event->state < PERF_EVENT_STATE_OFF)
		return 1;

	if (event->state == PERF_EVENT_STATE_OFF && !event->attr.enable_on_exec)
		return 1;

	return pmu_get_event_idx(hw_events, event) >= 0;
}

static int
validate_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct cci_pmu_hw_events fake_pmu = {
		/*
		 * Initialise the fake PMU. We only need to populate the
		 * used_mask for the purposes of validation.
		 */
		.used_mask = { 0 },
	};

	if (!validate_event(event->pmu, &fake_pmu, leader))
		return -EINVAL;

	list_for_each_entry(sibling, &leader->sibling_list, group_entry) {
		if (!validate_event(event->pmu, &fake_pmu, sibling))
			return -EINVAL;
	}

	if (!validate_event(event->pmu, &fake_pmu, event))
		return -EINVAL;

	return 0;
}

static int
__hw_perf_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int mapping;

	mapping = pmu_map_event(event);

	if (mapping < 0) {
		pr_debug("event %x:%llx not supported\n", event->attr.type,
			 event->attr.config);
		return mapping;
	}

	/*
	 * We don't assign an index until we actually place the event onto
	 * hardware. Use -1 to signify that we haven't decided where to put it
	 * yet.
	 */
	hwc->idx		= -1;
	hwc->config_base	= 0;
	hwc->config		= 0;
	hwc->event_base		= 0;

	/*
	 * Store the event encoding into the config_base field.
	 */
	hwc->config_base	    |= (unsigned long)mapping;

	/*
	 * Limit the sample_period to half of the counter width. That way, the
	 * new counter value is far less likely to overtake the previous one
	 * unless you have some serious IRQ latency issues.
	 */
	hwc->sample_period  = CCI_PMU_CNTR_MASK >> 1;
	hwc->last_period    = hwc->sample_period;
	local64_set(&hwc->period_left, hwc->sample_period);

	if (event->group_leader != event) {
		if (validate_group(event) != 0)
			return -EINVAL;
	}

	return 0;
}

static int cci_pmu_event_init(struct perf_event *event)
{
	struct cci_pmu *cci_pmu = to_cci_pmu(event->pmu);
	atomic_t *active_events = &cci_pmu->active_events;
	int err = 0;
	int cpu;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* Shared by all CPUs, no meaningful state to sample */
	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;

	/* We have no filtering of any kind */
	if (event->attr.exclude_user	||
	    event->attr.exclude_kernel	||
	    event->attr.exclude_hv	||
	    event->attr.exclude_idle	||
	    event->attr.exclude_host	||
	    event->attr.exclude_guest)
		return -EINVAL;

	/*
	 * Following the example set by other "uncore" PMUs, we accept any CPU
	 * and rewrite its affinity dynamically rather than having perf core
	 * handle cpu == -1 and pid == -1 for this case.
	 *
	 * The perf core will pin online CPUs for the duration of this call and
	 * the event being installed into its context, so the PMU's CPU can't
	 * change under our feet.
	 */
	cpu = cpumask_first(&cci_pmu->cpus);
	if (event->cpu < 0 || cpu < 0)
		return -EINVAL;
	event->cpu = cpu;

	event->destroy = hw_perf_event_destroy;
	if (!atomic_inc_not_zero(active_events)) {
		mutex_lock(&cci_pmu->reserve_mutex);
		if (atomic_read(active_events) == 0)
			err = cci_pmu_get_hw(cci_pmu);
		if (!err)
			atomic_inc(active_events);
		mutex_unlock(&cci_pmu->reserve_mutex);
	}
	if (err)
		return err;

	err = __hw_perf_event_init(event);
	if (err)
		hw_perf_event_destroy(event);

	return err;
}

static ssize_t pmu_attr_cpumask_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int n = scnprintf(buf, PAGE_SIZE - 1, "%*pbl",
			  cpumask_pr_args(&pmu->cpus));
	buf[n++] = '\n';
	buf[n] = '\0';
	return n;
}

static DEVICE_ATTR(cpumask, S_IRUGO, pmu_attr_cpumask_show, NULL);

static struct attribute *pmu_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group pmu_attr_group = {
	.attrs = pmu_attrs,
};

static const struct attribute_group *pmu_attr_groups[] = {
	&pmu_attr_group,
	NULL
};

static int cci_pmu_init(struct cci_pmu *cci_pmu, struct platform_device *pdev)
{
	char *name = cci_pmu->model->name;
	cci_pmu->pmu = (struct pmu) {
		.name		= cci_pmu->model->name,
		.task_ctx_nr	= perf_invalid_context,
		.pmu_enable	= cci_pmu_enable,
		.pmu_disable	= cci_pmu_disable,
		.event_init	= cci_pmu_event_init,
		.add		= cci_pmu_add,
		.del		= cci_pmu_del,
		.start		= cci_pmu_start,
		.stop		= cci_pmu_stop,
		.read		= pmu_read,
		.attr_groups	= pmu_attr_groups,
	};

	cci_pmu->plat_device = pdev;
	cci_pmu->num_events = pmu_get_max_counters();

	return perf_pmu_register(&cci_pmu->pmu, name, -1);
}

static int cci_pmu_cpu_notifier(struct notifier_block *self,
				unsigned long action, void *hcpu)
{
	unsigned int cpu = (long)hcpu;
	unsigned int target;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DOWN_PREPARE:
		if (!cpumask_test_and_clear_cpu(cpu, &pmu->cpus))
			break;
		target = cpumask_any_but(cpu_online_mask, cpu);
		if (target < 0) // UP, last CPU
			break;
		/*
		 * TODO: migrate context once core races on event->ctx have
		 * been fixed.
		 */
		cpumask_set_cpu(target, &pmu->cpus);
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block cci_pmu_cpu_nb = {
	.notifier_call	= cci_pmu_cpu_notifier,
	/*
	 * to migrate uncore events, our notifier should be executed
	 * before perf core's notifier.
	 */
	.priority	= CPU_PRI_PERF + 1,
};

static struct cci_pmu_model cci_pmu_models[] = {
	[CCI_REV_R0] = {
		.name = "CCI_400",
		.event_ranges = {
			[CCI_IF_SLAVE] = {
				CCI_REV_R0_SLAVE_PORT_MIN_EV,
				CCI_REV_R0_SLAVE_PORT_MAX_EV,
			},
			[CCI_IF_MASTER] = {
				CCI_REV_R0_MASTER_PORT_MIN_EV,
				CCI_REV_R0_MASTER_PORT_MAX_EV,
			},
		},
	},
	[CCI_REV_R1] = {
		.name = "CCI_400_r1",
		.event_ranges = {
			[CCI_IF_SLAVE] = {
				CCI_REV_R1_SLAVE_PORT_MIN_EV,
				CCI_REV_R1_SLAVE_PORT_MAX_EV,
			},
			[CCI_IF_MASTER] = {
				CCI_REV_R1_MASTER_PORT_MIN_EV,
				CCI_REV_R1_MASTER_PORT_MAX_EV,
			},
		},
	},
};

static const struct of_device_id arm_cci_pmu_matches[] = {
	{
		.compatible = "arm,cci-400-pmu",
		.data	= NULL,
	},
	{
		.compatible = "arm,cci-400-pmu,r0",
		.data	= &cci_pmu_models[CCI_REV_R0],
	},
	{
		.compatible = "arm,cci-400-pmu,r1",
		.data	= &cci_pmu_models[CCI_REV_R1],
	},
	{},
};

static inline const struct cci_pmu_model *get_cci_model(struct platform_device *pdev)
{
	const struct of_device_id *match = of_match_node(arm_cci_pmu_matches,
							pdev->dev.of_node);
	if (!match)
		return NULL;
	if (match->data)
		return match->data;

	dev_warn(&pdev->dev, "DEPRECATED compatible property,"
			 "requires secure access to CCI registers");
	return probe_cci_model(pdev);
}

static bool is_duplicate_irq(int irq, int *irqs, int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++)
		if (irq == irqs[i])
			return true;

	return false;
}

static int cci_pmu_probe(struct platform_device *pdev)
{
	struct resource *res;
	int i, ret, irq;
	const struct cci_pmu_model *model;

	model = get_cci_model(pdev);
	if (!model) {
		dev_warn(&pdev->dev, "CCI PMU version not supported\n");
		return -ENODEV;
	}

	pmu = devm_kzalloc(&pdev->dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	pmu->model = model;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pmu->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pmu->base))
		return -ENOMEM;

	/*
	 * CCI PMU has 5 overflow signals - one per counter; but some may be tied
	 * together to a common interrupt.
	 */
	pmu->nr_irqs = 0;
	for (i = 0; i < CCI_PMU_MAX_HW_EVENTS; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			break;

		if (is_duplicate_irq(irq, pmu->irqs, pmu->nr_irqs))
			continue;

		pmu->irqs[pmu->nr_irqs++] = irq;
	}

	/*
	 * Ensure that the device tree has as many interrupts as the number
	 * of counters.
	 */
	if (i < CCI_PMU_MAX_HW_EVENTS) {
		dev_warn(&pdev->dev, "In-correct number of interrupts: %d, should be %d\n",
			i, CCI_PMU_MAX_HW_EVENTS);
		return -EINVAL;
	}

	raw_spin_lock_init(&pmu->hw_events.pmu_lock);
	mutex_init(&pmu->reserve_mutex);
	atomic_set(&pmu->active_events, 0);
	cpumask_set_cpu(smp_processor_id(), &pmu->cpus);

	ret = register_cpu_notifier(&cci_pmu_cpu_nb);
	if (ret)
		return ret;

	ret = cci_pmu_init(pmu, pdev);
	if (ret)
		return ret;

	pr_info("ARM %s PMU driver probed", pmu->model->name);
	return 0;
}

static int cci_platform_probe(struct platform_device *pdev)
{
	if (!cci_probed())
		return -ENODEV;

	return of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
}

static struct platform_driver cci_pmu_driver = {
	.driver = {
		   .name = DRIVER_NAME_PMU,
		   .of_match_table = arm_cci_pmu_matches,
		  },
	.probe = cci_pmu_probe,
};

static struct platform_driver cci_platform_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = arm_cci_matches,
		  },
	.probe = cci_platform_probe,
};

static int __init cci_platform_init(void)
{
	int ret;

	ret = platform_driver_register(&cci_pmu_driver);
	if (ret)
		return ret;

	return platform_driver_register(&cci_platform_driver);
}

#else /* !CONFIG_ARM_CCI400_PMU */

static int __init cci_platform_init(void)
{
	return 0;
}

#endif /* CONFIG_ARM_CCI400_PMU */

#ifdef CONFIG_ARM_CCI400_PORT_CTRL

#define CCI_PORT_CTRL		0x0
#define CCI_CTRL_STATUS		0xc

#define CCI_ENABLE_SNOOP_REQ	0x1
#define CCI_ENABLE_DVM_REQ	0x2
#define CCI_ENABLE_REQ		(CCI_ENABLE_SNOOP_REQ | CCI_ENABLE_DVM_REQ)

enum cci_ace_port_type {
	ACE_INVALID_PORT = 0x0,
	ACE_PORT,
	ACE_LITE_PORT,
};

struct cci_ace_port {
	void __iomem *base;
	unsigned long phys;
	enum cci_ace_port_type type;
	struct device_node *dn;
};

static struct cci_ace_port *ports;
static unsigned int nb_cci_ports;

struct cpu_port {
	u64 mpidr;
	u32 port;
};

/*
 * Use the port MSB as valid flag, shift can be made dynamic
 * by computing number of bits required for port indexes.
 * Code disabling CCI cpu ports runs with D-cache invalidated
 * and SCTLR bit clear so data accesses must be kept to a minimum
 * to improve performance; for now shift is left static to
 * avoid one more data access while disabling the CCI port.
 */
#define PORT_VALID_SHIFT	31
#define PORT_VALID		(0x1 << PORT_VALID_SHIFT)

static inline void init_cpu_port(struct cpu_port *port, u32 index, u64 mpidr)
{
	port->port = PORT_VALID | index;
	port->mpidr = mpidr;
}

static inline bool cpu_port_is_valid(struct cpu_port *port)
{
	return !!(port->port & PORT_VALID);
}

static inline bool cpu_port_match(struct cpu_port *port, u64 mpidr)
{
	return port->mpidr == (mpidr & MPIDR_HWID_BITMASK);
}

static struct cpu_port cpu_port[NR_CPUS];

/**
 * __cci_ace_get_port - Function to retrieve the port index connected to
 *			a cpu or device.
 *
 * @dn: device node of the device to look-up
 * @type: port type
 *
 * Return value:
 *	- CCI port index if success
 *	- -ENODEV if failure
 */
static int __cci_ace_get_port(struct device_node *dn, int type)
{
	int i;
	bool ace_match;
	struct device_node *cci_portn;

	cci_portn = of_parse_phandle(dn, "cci-control-port", 0);
	for (i = 0; i < nb_cci_ports; i++) {
		ace_match = ports[i].type == type;
		if (ace_match && cci_portn == ports[i].dn)
			return i;
	}
	return -ENODEV;
}

int cci_ace_get_port(struct device_node *dn)
{
	return __cci_ace_get_port(dn, ACE_LITE_PORT);
}
EXPORT_SYMBOL_GPL(cci_ace_get_port);

static void cci_ace_init_ports(void)
{
	int port, cpu;
	struct device_node *cpun;

	/*
	 * Port index look-up speeds up the function disabling ports by CPU,
	 * since the logical to port index mapping is done once and does
	 * not change after system boot.
	 * The stashed index array is initialized for all possible CPUs
	 * at probe time.
	 */
	for_each_possible_cpu(cpu) {
		/* too early to use cpu->of_node */
		cpun = of_get_cpu_node(cpu, NULL);

		if (WARN(!cpun, "Missing cpu device node\n"))
			continue;

		port = __cci_ace_get_port(cpun, ACE_PORT);
		if (port < 0)
			continue;

		init_cpu_port(&cpu_port[cpu], port, cpu_logical_map(cpu));
	}

	for_each_possible_cpu(cpu) {
		WARN(!cpu_port_is_valid(&cpu_port[cpu]),
			"CPU %u does not have an associated CCI port\n",
			cpu);
	}
}
/*
 * Functions to enable/disable a CCI interconnect slave port
 *
 * They are called by low-level power management code to disable slave
 * interfaces snoops and DVM broadcast.
 * Since they may execute with cache data allocation disabled and
 * after the caches have been cleaned and invalidated the functions provide
 * no explicit locking since they may run with D-cache disabled, so normal
 * cacheable kernel locks based on ldrex/strex may not work.
 * Locking has to be provided by BSP implementations to ensure proper
 * operations.
 */

/**
 * cci_port_control() - function to control a CCI port
 *
 * @port: index of the port to setup
 * @enable: if true enables the port, if false disables it
 */
static void notrace cci_port_control(unsigned int port, bool enable)
{
	void __iomem *base = ports[port].base;

	writel_relaxed(enable ? CCI_ENABLE_REQ : 0, base + CCI_PORT_CTRL);
	/*
	 * This function is called from power down procedures
	 * and must not execute any instruction that might
	 * cause the processor to be put in a quiescent state
	 * (eg wfi). Hence, cpu_relax() can not be added to this
	 * read loop to optimize power, since it might hide possibly
	 * disruptive operations.
	 */
	while (readl_relaxed(cci_ctrl_base + CCI_CTRL_STATUS) & 0x1)
			;
}

/**
 * cci_disable_port_by_cpu() - function to disable a CCI port by CPU
 *			       reference
 *
 * @mpidr: mpidr of the CPU whose CCI port should be disabled
 *
 * Disabling a CCI port for a CPU implies disabling the CCI port
 * controlling that CPU cluster. Code disabling CPU CCI ports
 * must make sure that the CPU running the code is the last active CPU
 * in the cluster ie all other CPUs are quiescent in a low power state.
 *
 * Return:
 *	0 on success
 *	-ENODEV on port look-up failure
 */
int notrace cci_disable_port_by_cpu(u64 mpidr)
{
	int cpu;
	bool is_valid;
	for (cpu = 0; cpu < nr_cpu_ids; cpu++) {
		is_valid = cpu_port_is_valid(&cpu_port[cpu]);
		if (is_valid && cpu_port_match(&cpu_port[cpu], mpidr)) {
			cci_port_control(cpu_port[cpu].port, false);
			return 0;
		}
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(cci_disable_port_by_cpu);

/**
 * cci_enable_port_for_self() - enable a CCI port for calling CPU
 *
 * Enabling a CCI port for the calling CPU implies enabling the CCI
 * port controlling that CPU's cluster. Caller must make sure that the
 * CPU running the code is the first active CPU in the cluster and all
 * other CPUs are quiescent in a low power state  or waiting for this CPU
 * to complete the CCI initialization.
 *
 * Because this is called when the MMU is still off and with no stack,
 * the code must be position independent and ideally rely on callee
 * clobbered registers only.  To achieve this we must code this function
 * entirely in assembler.
 *
 * On success this returns with the proper CCI port enabled.  In case of
 * any failure this never returns as the inability to enable the CCI is
 * fatal and there is no possible recovery at this stage.
 */
asmlinkage void __naked cci_enable_port_for_self(void)
{
	asm volatile ("\n"
"	.arch armv7-a\n"
"	mrc	p15, 0, r0, c0, c0, 5	@ get MPIDR value \n"
"	and	r0, r0, #"__stringify(MPIDR_HWID_BITMASK)" \n"
"	adr	r1, 5f \n"
"	ldr	r2, [r1] \n"
"	add	r1, r1, r2		@ &cpu_port \n"
"	add	ip, r1, %[sizeof_cpu_port] \n"

	/* Loop over the cpu_port array looking for a matching MPIDR */
"1:	ldr	r2, [r1, %[offsetof_cpu_port_mpidr_lsb]] \n"
"	cmp	r2, r0 			@ compare MPIDR \n"
"	bne	2f \n"

	/* Found a match, now test port validity */
"	ldr	r3, [r1, %[offsetof_cpu_port_port]] \n"
"	tst	r3, #"__stringify(PORT_VALID)" \n"
"	bne	3f \n"

	/* no match, loop with the next cpu_port entry */
"2:	add	r1, r1, %[sizeof_struct_cpu_port] \n"
"	cmp	r1, ip			@ done? \n"
"	blo	1b \n"

	/* CCI port not found -- cheaply try to stall this CPU */
"cci_port_not_found: \n"
"	wfi \n"
"	wfe \n"
"	b	cci_port_not_found \n"

	/* Use matched port index to look up the corresponding ports entry */
"3:	bic	r3, r3, #"__stringify(PORT_VALID)" \n"
"	adr	r0, 6f \n"
"	ldmia	r0, {r1, r2} \n"
"	sub	r1, r1, r0 		@ virt - phys \n"
"	ldr	r0, [r0, r2] 		@ *(&ports) \n"
"	mov	r2, %[sizeof_struct_ace_port] \n"
"	mla	r0, r2, r3, r0		@ &ports[index] \n"
"	sub	r0, r0, r1		@ virt_to_phys() \n"

	/* Enable the CCI port */
"	ldr	r0, [r0, %[offsetof_port_phys]] \n"
"	mov	r3, %[cci_enable_req]\n"		   
"	str	r3, [r0, #"__stringify(CCI_PORT_CTRL)"] \n"

	/* poll the status reg for completion */
"	adr	r1, 7f \n"
"	ldr	r0, [r1] \n"
"	ldr	r0, [r0, r1]		@ cci_ctrl_base \n"
"4:	ldr	r1, [r0, #"__stringify(CCI_CTRL_STATUS)"] \n"
"	tst	r1, %[cci_control_status_bits] \n"			
"	bne	4b \n"

"	mov	r0, #0 \n"
"	bx	lr \n"

"	.align	2 \n"
"5:	.word	cpu_port - . \n"
"6:	.word	. \n"
"	.word	ports - 6b \n"
"7:	.word	cci_ctrl_phys - . \n"
	: :
	[sizeof_cpu_port] "i" (sizeof(cpu_port)),
	[cci_enable_req] "i" cpu_to_le32(CCI_ENABLE_REQ),
	[cci_control_status_bits] "i" cpu_to_le32(1),
#ifndef __ARMEB__
	[offsetof_cpu_port_mpidr_lsb] "i" (offsetof(struct cpu_port, mpidr)),
#else
	[offsetof_cpu_port_mpidr_lsb] "i" (offsetof(struct cpu_port, mpidr)+4),
#endif
	[offsetof_cpu_port_port] "i" (offsetof(struct cpu_port, port)),
	[sizeof_struct_cpu_port] "i" (sizeof(struct cpu_port)),
	[sizeof_struct_ace_port] "i" (sizeof(struct cci_ace_port)),
	[offsetof_port_phys] "i" (offsetof(struct cci_ace_port, phys)) );

	unreachable();
}

/**
 * __cci_control_port_by_device() - function to control a CCI port by device
 *				    reference
 *
 * @dn: device node pointer of the device whose CCI port should be
 *      controlled
 * @enable: if true enables the port, if false disables it
 *
 * Return:
 *	0 on success
 *	-ENODEV on port look-up failure
 */
int notrace __cci_control_port_by_device(struct device_node *dn, bool enable)
{
	int port;

	if (!dn)
		return -ENODEV;

	port = __cci_ace_get_port(dn, ACE_LITE_PORT);
	if (WARN_ONCE(port < 0, "node %s ACE lite port look-up failure\n",
				dn->full_name))
		return -ENODEV;
	cci_port_control(port, enable);
	return 0;
}
EXPORT_SYMBOL_GPL(__cci_control_port_by_device);

/**
 * __cci_control_port_by_index() - function to control a CCI port by port index
 *
 * @port: port index previously retrieved with cci_ace_get_port()
 * @enable: if true enables the port, if false disables it
 *
 * Return:
 *	0 on success
 *	-ENODEV on port index out of range
 *	-EPERM if operation carried out on an ACE PORT
 */
int notrace __cci_control_port_by_index(u32 port, bool enable)
{
	if (port >= nb_cci_ports || ports[port].type == ACE_INVALID_PORT)
		return -ENODEV;
	/*
	 * CCI control for ports connected to CPUS is extremely fragile
	 * and must be made to go through a specific and controlled
	 * interface (ie cci_disable_port_by_cpu(); control by general purpose
	 * indexing is therefore disabled for ACE ports.
	 */
	if (ports[port].type == ACE_PORT)
		return -EPERM;

	cci_port_control(port, enable);
	return 0;
}
EXPORT_SYMBOL_GPL(__cci_control_port_by_index);

static const struct of_device_id arm_cci_ctrl_if_matches[] = {
	{.compatible = "arm,cci-400-ctrl-if", },
	{},
};

static int cci_probe_ports(struct device_node *np)
{
	struct cci_nb_ports const *cci_config;
	int ret, i, nb_ace = 0, nb_ace_lite = 0;
	struct device_node *cp;
	struct resource res;
	const char *match_str;
	bool is_ace;


	cci_config = of_match_node(arm_cci_matches, np)->data;
	if (!cci_config)
		return -ENODEV;

	nb_cci_ports = cci_config->nb_ace + cci_config->nb_ace_lite;

	ports = kcalloc(nb_cci_ports, sizeof(*ports), GFP_KERNEL);
	if (!ports)
		return -ENOMEM;

	for_each_child_of_node(np, cp) {
		if (!of_match_node(arm_cci_ctrl_if_matches, cp))
			continue;

		i = nb_ace + nb_ace_lite;

		if (i >= nb_cci_ports)
			break;

		if (of_property_read_string(cp, "interface-type",
					&match_str)) {
			WARN(1, "node %s missing interface-type property\n",
				  cp->full_name);
			continue;
		}
		is_ace = strcmp(match_str, "ace") == 0;
		if (!is_ace && strcmp(match_str, "ace-lite")) {
			WARN(1, "node %s containing invalid interface-type property, skipping it\n",
					cp->full_name);
			continue;
		}

		ret = of_address_to_resource(cp, 0, &res);
		if (!ret) {
			ports[i].base = ioremap(res.start, resource_size(&res));
			ports[i].phys = res.start;
		}
		if (ret || !ports[i].base) {
			WARN(1, "unable to ioremap CCI port %d\n", i);
			continue;
		}

		if (is_ace) {
			if (WARN_ON(nb_ace >= cci_config->nb_ace))
				continue;
			ports[i].type = ACE_PORT;
			++nb_ace;
		} else {
			if (WARN_ON(nb_ace_lite >= cci_config->nb_ace_lite))
				continue;
			ports[i].type = ACE_LITE_PORT;
			++nb_ace_lite;
		}
		ports[i].dn = cp;
	}

	 /* initialize a stashed array of ACE ports to speed-up look-up */
	cci_ace_init_ports();

	/*
	 * Multi-cluster systems may need this data when non-coherent, during
	 * cluster power-up/power-down. Make sure it reaches main memory.
	 */
	sync_cache_w(&cci_ctrl_base);
	sync_cache_w(&cci_ctrl_phys);
	sync_cache_w(&ports);
	sync_cache_w(&cpu_port);
	__sync_cache_range_w(ports, sizeof(*ports) * nb_cci_ports);
	pr_info("ARM CCI driver probed\n");

	return 0;
}
#else /* !CONFIG_ARM_CCI400_PORT_CTRL */
static inline int cci_probe_ports(struct device_node *np)
{
	return 0;
}
#endif /* CONFIG_ARM_CCI400_PORT_CTRL */

static int cci_probe(void)
{
	int ret;
	struct device_node *np;
	struct resource res;

	np = of_find_matching_node(NULL, arm_cci_matches);
	if(!np || !of_device_is_available(np))
		return -ENODEV;

	ret = of_address_to_resource(np, 0, &res);
	if (!ret) {
		cci_ctrl_base = ioremap(res.start, resource_size(&res));
		cci_ctrl_phys =	res.start;
	}
	if (ret || !cci_ctrl_base) {
		WARN(1, "unable to ioremap CCI ctrl\n");
		return -ENXIO;
	}

	return cci_probe_ports(np);
}

static int cci_init_status = -EAGAIN;
static DEFINE_MUTEX(cci_probing);

static int cci_init(void)
{
	if (cci_init_status != -EAGAIN)
		return cci_init_status;

	mutex_lock(&cci_probing);
	if (cci_init_status == -EAGAIN)
		cci_init_status = cci_probe();
	mutex_unlock(&cci_probing);
	return cci_init_status;
}

/*
 * To sort out early init calls ordering a helper function is provided to
 * check if the CCI driver has beed initialized. Function check if the driver
 * has been initialized, if not it calls the init function that probes
 * the driver and updates the return value.
 */
bool cci_probed(void)
{
	return cci_init() == 0;
}
EXPORT_SYMBOL_GPL(cci_probed);

early_initcall(cci_init);
core_initcall(cci_platform_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ARM CCI support");
