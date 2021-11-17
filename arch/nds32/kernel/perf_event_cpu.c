// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008-2017 Andes Technology Corporation
 *
 * Reference ARMv7: Jean Pihet <jpihet@mvista.com>
 * 2010 (c) MontaVista Software, LLC.
 */

#include <linux/perf_event.h>
#include <linux/bitmap.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/pm_runtime.h>
#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <linux/sched/clock.h>
#include <linux/percpu-defs.h>

#include <asm/pmu.h>
#include <asm/irq_regs.h>
#include <asm/nds32.h>
#include <asm/stacktrace.h>
#include <asm/perf_event.h>
#include <nds32_intrinsic.h>

/* Set at runtime when we know what CPU type we are. */
static struct nds32_pmu *cpu_pmu;

static DEFINE_PER_CPU(struct pmu_hw_events, cpu_hw_events);
static void nds32_pmu_start(struct nds32_pmu *cpu_pmu);
static void nds32_pmu_stop(struct nds32_pmu *cpu_pmu);
static struct platform_device_id cpu_pmu_plat_device_ids[] = {
	{.name = "nds32-pfm"},
	{},
};

static int nds32_pmu_map_cache_event(const unsigned int (*cache_map)
				  [PERF_COUNT_HW_CACHE_MAX]
				  [PERF_COUNT_HW_CACHE_OP_MAX]
				  [PERF_COUNT_HW_CACHE_RESULT_MAX], u64 config)
{
	unsigned int cache_type, cache_op, cache_result, ret;

	cache_type = (config >> 0) & 0xff;
	if (cache_type >= PERF_COUNT_HW_CACHE_MAX)
		return -EINVAL;

	cache_op = (config >> 8) & 0xff;
	if (cache_op >= PERF_COUNT_HW_CACHE_OP_MAX)
		return -EINVAL;

	cache_result = (config >> 16) & 0xff;
	if (cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	ret = (int)(*cache_map)[cache_type][cache_op][cache_result];

	if (ret == CACHE_OP_UNSUPPORTED)
		return -ENOENT;

	return ret;
}

static int
nds32_pmu_map_hw_event(const unsigned int (*event_map)[PERF_COUNT_HW_MAX],
		       u64 config)
{
	int mapping;

	if (config >= PERF_COUNT_HW_MAX)
		return -ENOENT;

	mapping = (*event_map)[config];
	return mapping == HW_OP_UNSUPPORTED ? -ENOENT : mapping;
}

static int nds32_pmu_map_raw_event(u32 raw_event_mask, u64 config)
{
	int ev_type = (int)(config & raw_event_mask);
	int idx = config >> 8;

	switch (idx) {
	case 0:
		ev_type = PFM_OFFSET_MAGIC_0 + ev_type;
		if (ev_type >= SPAV3_0_SEL_LAST || ev_type <= SPAV3_0_SEL_BASE)
			return -ENOENT;
		break;
	case 1:
		ev_type = PFM_OFFSET_MAGIC_1 + ev_type;
		if (ev_type >= SPAV3_1_SEL_LAST || ev_type <= SPAV3_1_SEL_BASE)
			return -ENOENT;
		break;
	case 2:
		ev_type = PFM_OFFSET_MAGIC_2 + ev_type;
		if (ev_type >= SPAV3_2_SEL_LAST || ev_type <= SPAV3_2_SEL_BASE)
			return -ENOENT;
		break;
	default:
		return -ENOENT;
	}

	return ev_type;
}

int
nds32_pmu_map_event(struct perf_event *event,
		    const unsigned int (*event_map)[PERF_COUNT_HW_MAX],
		    const unsigned int (*cache_map)
		    [PERF_COUNT_HW_CACHE_MAX]
		    [PERF_COUNT_HW_CACHE_OP_MAX]
		    [PERF_COUNT_HW_CACHE_RESULT_MAX], u32 raw_event_mask)
{
	u64 config = event->attr.config;

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		return nds32_pmu_map_hw_event(event_map, config);
	case PERF_TYPE_HW_CACHE:
		return nds32_pmu_map_cache_event(cache_map, config);
	case PERF_TYPE_RAW:
		return nds32_pmu_map_raw_event(raw_event_mask, config);
	}

	return -ENOENT;
}

static int nds32_spav3_map_event(struct perf_event *event)
{
	return nds32_pmu_map_event(event, &nds32_pfm_perf_map,
				&nds32_pfm_perf_cache_map, SOFTWARE_EVENT_MASK);
}

static inline u32 nds32_pfm_getreset_flags(void)
{
	/* Read overflow status */
	u32 val = __nds32__mfsr(NDS32_SR_PFM_CTL);
	u32 old_val = val;

	/* Write overflow bit to clear status, and others keep it 0 */
	u32 ov_flag = PFM_CTL_OVF[0] | PFM_CTL_OVF[1] | PFM_CTL_OVF[2];

	__nds32__mtsr(val | ov_flag, NDS32_SR_PFM_CTL);

	return old_val;
}

static inline int nds32_pfm_has_overflowed(u32 pfm)
{
	u32 ov_flag = PFM_CTL_OVF[0] | PFM_CTL_OVF[1] | PFM_CTL_OVF[2];

	return pfm & ov_flag;
}

static inline int nds32_pfm_counter_has_overflowed(u32 pfm, int idx)
{
	u32 mask = 0;

	switch (idx) {
	case 0:
		mask = PFM_CTL_OVF[0];
		break;
	case 1:
		mask = PFM_CTL_OVF[1];
		break;
	case 2:
		mask = PFM_CTL_OVF[2];
		break;
	default:
		pr_err("%s index wrong\n", __func__);
		break;
	}
	return pfm & mask;
}

/*
 * Set the next IRQ period, based on the hwc->period_left value.
 * To be called with the event disabled in hw:
 */
int nds32_pmu_event_set_period(struct perf_event *event)
{
	struct nds32_pmu *nds32_pmu = to_nds32_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	s64 left = local64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	int ret = 0;

	/* The period may have been changed by PERF_EVENT_IOC_PERIOD */
	if (unlikely(period != hwc->last_period))
		left = period - (hwc->last_period - left);

	if (unlikely(left <= -period)) {
		left = period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (unlikely(left <= 0)) {
		left += period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (left > (s64)nds32_pmu->max_period)
		left = nds32_pmu->max_period;

	/*
	 * The hw event starts counting from this event offset,
	 * mark it to be able to extract future "deltas":
	 */
	local64_set(&hwc->prev_count, (u64)(-left));

	nds32_pmu->write_counter(event, (u64)(-left) & nds32_pmu->max_period);

	perf_event_update_userpage(event);

	return ret;
}

static irqreturn_t nds32_pmu_handle_irq(int irq_num, void *dev)
{
	u32 pfm;
	struct perf_sample_data data;
	struct nds32_pmu *cpu_pmu = (struct nds32_pmu *)dev;
	struct pmu_hw_events *cpuc = cpu_pmu->get_hw_events();
	struct pt_regs *regs;
	int idx;
	/*
	 * Get and reset the IRQ flags
	 */
	pfm = nds32_pfm_getreset_flags();

	/*
	 * Did an overflow occur?
	 */
	if (!nds32_pfm_has_overflowed(pfm))
		return IRQ_NONE;

	/*
	 * Handle the counter(s) overflow(s)
	 */
	regs = get_irq_regs();

	nds32_pmu_stop(cpu_pmu);
	for (idx = 0; idx < cpu_pmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		/* Ignore if we don't have an event. */
		if (!event)
			continue;

		/*
		 * We have a single interrupt for all counters. Check that
		 * each counter has overflowed before we process it.
		 */
		if (!nds32_pfm_counter_has_overflowed(pfm, idx))
			continue;

		hwc = &event->hw;
		nds32_pmu_event_update(event);
		perf_sample_data_init(&data, 0, hwc->last_period);
		if (!nds32_pmu_event_set_period(event))
			continue;

		if (perf_event_overflow(event, &data, regs))
			cpu_pmu->disable(event);
	}
	nds32_pmu_start(cpu_pmu);
	/*
	 * Handle the pending perf events.
	 *
	 * Note: this call *must* be run with interrupts disabled. For
	 * platforms that can have the PMU interrupts raised as an NMI, this
	 * will not work.
	 */
	irq_work_run();

	return IRQ_HANDLED;
}

static inline int nds32_pfm_counter_valid(struct nds32_pmu *cpu_pmu, int idx)
{
	return ((idx >= 0) && (idx < cpu_pmu->num_events));
}

static inline int nds32_pfm_disable_counter(int idx)
{
	unsigned int val = __nds32__mfsr(NDS32_SR_PFM_CTL);
	u32 mask = 0;

	mask = PFM_CTL_EN[idx];
	val &= ~mask;
	val &= ~(PFM_CTL_OVF[0] | PFM_CTL_OVF[1] | PFM_CTL_OVF[2]);
	__nds32__mtsr_isb(val, NDS32_SR_PFM_CTL);
	return idx;
}

/*
 * Add an event filter to a given event.
 */
static int nds32_pmu_set_event_filter(struct hw_perf_event *event,
				      struct perf_event_attr *attr)
{
	unsigned long config_base = 0;
	int idx = event->idx;
	unsigned long no_kernel_tracing = 0;
	unsigned long no_user_tracing = 0;
	/* If index is -1, do not do anything */
	if (idx == -1)
		return 0;

	no_kernel_tracing = PFM_CTL_KS[idx];
	no_user_tracing = PFM_CTL_KU[idx];
	/*
	 * Default: enable both kernel and user mode tracing.
	 */
	if (attr->exclude_user)
		config_base |= no_user_tracing;

	if (attr->exclude_kernel)
		config_base |= no_kernel_tracing;

	/*
	 * Install the filter into config_base as this is used to
	 * construct the event type.
	 */
	event->config_base |= config_base;
	return 0;
}

static inline void nds32_pfm_write_evtsel(int idx, u32 evnum)
{
	u32 offset = 0;
	u32 ori_val = __nds32__mfsr(NDS32_SR_PFM_CTL);
	u32 ev_mask = 0;
	u32 no_kernel_mask = 0;
	u32 no_user_mask = 0;
	u32 val;

	offset = PFM_CTL_OFFSEL[idx];
	/* Clear previous mode selection, and write new one */
	no_kernel_mask = PFM_CTL_KS[idx];
	no_user_mask = PFM_CTL_KU[idx];
	ori_val &= ~no_kernel_mask;
	ori_val &= ~no_user_mask;
	if (evnum & no_kernel_mask)
		ori_val |= no_kernel_mask;

	if (evnum & no_user_mask)
		ori_val |= no_user_mask;

	/* Clear previous event selection */
	ev_mask = PFM_CTL_SEL[idx];
	ori_val &= ~ev_mask;
	evnum &= SOFTWARE_EVENT_MASK;

	/* undo the linear mapping */
	evnum = get_converted_evet_hw_num(evnum);
	val = ori_val | (evnum << offset);
	val &= ~(PFM_CTL_OVF[0] | PFM_CTL_OVF[1] | PFM_CTL_OVF[2]);
	__nds32__mtsr_isb(val, NDS32_SR_PFM_CTL);
}

static inline int nds32_pfm_enable_counter(int idx)
{
	unsigned int val = __nds32__mfsr(NDS32_SR_PFM_CTL);
	u32 mask = 0;

	mask = PFM_CTL_EN[idx];
	val |= mask;
	val &= ~(PFM_CTL_OVF[0] | PFM_CTL_OVF[1] | PFM_CTL_OVF[2]);
	__nds32__mtsr_isb(val, NDS32_SR_PFM_CTL);
	return idx;
}

static inline int nds32_pfm_enable_intens(int idx)
{
	unsigned int val = __nds32__mfsr(NDS32_SR_PFM_CTL);
	u32 mask = 0;

	mask = PFM_CTL_IE[idx];
	val |= mask;
	val &= ~(PFM_CTL_OVF[0] | PFM_CTL_OVF[1] | PFM_CTL_OVF[2]);
	__nds32__mtsr_isb(val, NDS32_SR_PFM_CTL);
	return idx;
}

static inline int nds32_pfm_disable_intens(int idx)
{
	unsigned int val = __nds32__mfsr(NDS32_SR_PFM_CTL);
	u32 mask = 0;

	mask = PFM_CTL_IE[idx];
	val &= ~mask;
	val &= ~(PFM_CTL_OVF[0] | PFM_CTL_OVF[1] | PFM_CTL_OVF[2]);
	__nds32__mtsr_isb(val, NDS32_SR_PFM_CTL);
	return idx;
}

static int event_requires_mode_exclusion(struct perf_event_attr *attr)
{
	/* Other modes NDS32 does not support */
	return attr->exclude_user || attr->exclude_kernel;
}

static void nds32_pmu_enable_event(struct perf_event *event)
{
	unsigned long flags;
	unsigned int evnum = 0;
	struct hw_perf_event *hwc = &event->hw;
	struct nds32_pmu *cpu_pmu = to_nds32_pmu(event->pmu);
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();
	int idx = hwc->idx;

	if (!nds32_pfm_counter_valid(cpu_pmu, idx)) {
		pr_err("CPU enabling wrong pfm counter IRQ enable\n");
		return;
	}

	/*
	 * Enable counter and interrupt, and set the counter to count
	 * the event that we're interested in.
	 */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/*
	 * Disable counter
	 */
	nds32_pfm_disable_counter(idx);

	/*
	 * Check whether we need to exclude the counter from certain modes.
	 */
	if ((!cpu_pmu->set_event_filter ||
	     cpu_pmu->set_event_filter(hwc, &event->attr)) &&
	     event_requires_mode_exclusion(&event->attr)) {
		pr_notice
		("NDS32 performance counters do not support mode exclusion\n");
		hwc->config_base = 0;
	}
	/* Write event */
	evnum = hwc->config_base;
	nds32_pfm_write_evtsel(idx, evnum);

	/*
	 * Enable interrupt for this counter
	 */
	nds32_pfm_enable_intens(idx);

	/*
	 * Enable counter
	 */
	nds32_pfm_enable_counter(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void nds32_pmu_disable_event(struct perf_event *event)
{
	unsigned long flags;
	struct hw_perf_event *hwc = &event->hw;
	struct nds32_pmu *cpu_pmu = to_nds32_pmu(event->pmu);
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();
	int idx = hwc->idx;

	if (!nds32_pfm_counter_valid(cpu_pmu, idx)) {
		pr_err("CPU disabling wrong pfm counter IRQ enable %d\n", idx);
		return;
	}

	/*
	 * Disable counter and interrupt
	 */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/*
	 * Disable counter
	 */
	nds32_pfm_disable_counter(idx);

	/*
	 * Disable interrupt for this counter
	 */
	nds32_pfm_disable_intens(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static inline u32 nds32_pmu_read_counter(struct perf_event *event)
{
	struct nds32_pmu *cpu_pmu = to_nds32_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	u32 count = 0;

	if (!nds32_pfm_counter_valid(cpu_pmu, idx)) {
		pr_err("CPU reading wrong counter %d\n", idx);
	} else {
		switch (idx) {
		case PFMC0:
			count = __nds32__mfsr(NDS32_SR_PFMC0);
			break;
		case PFMC1:
			count = __nds32__mfsr(NDS32_SR_PFMC1);
			break;
		case PFMC2:
			count = __nds32__mfsr(NDS32_SR_PFMC2);
			break;
		default:
			pr_err
			    ("%s: CPU has no performance counters %d\n",
			     __func__, idx);
		}
	}
	return count;
}

static inline void nds32_pmu_write_counter(struct perf_event *event, u32 value)
{
	struct nds32_pmu *cpu_pmu = to_nds32_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (!nds32_pfm_counter_valid(cpu_pmu, idx)) {
		pr_err("CPU writing wrong counter %d\n", idx);
	} else {
		switch (idx) {
		case PFMC0:
			__nds32__mtsr_isb(value, NDS32_SR_PFMC0);
			break;
		case PFMC1:
			__nds32__mtsr_isb(value, NDS32_SR_PFMC1);
			break;
		case PFMC2:
			__nds32__mtsr_isb(value, NDS32_SR_PFMC2);
			break;
		default:
			pr_err
			    ("%s: CPU has no performance counters %d\n",
			     __func__, idx);
		}
	}
}

static int nds32_pmu_get_event_idx(struct pmu_hw_events *cpuc,
				   struct perf_event *event)
{
	int idx;
	struct hw_perf_event *hwc = &event->hw;
	/*
	 * Current implementation maps cycles, instruction count and cache-miss
	 * to specific counter.
	 * However, multiple of the 3 counters are able to count these events.
	 *
	 *
	 * SOFTWARE_EVENT_MASK mask for getting event num ,
	 * This is defined by Jia-Rung, you can change the polocies.
	 * However, do not exceed 8 bits. This is hardware specific.
	 * The last number is SPAv3_2_SEL_LAST.
	 */
	unsigned long evtype = hwc->config_base & SOFTWARE_EVENT_MASK;

	idx = get_converted_event_idx(evtype);
	/*
	 * Try to get the counter for correpsonding event
	 */
	if (evtype == SPAV3_0_SEL_TOTAL_CYCLES) {
		if (!test_and_set_bit(idx, cpuc->used_mask))
			return idx;
		if (!test_and_set_bit(NDS32_IDX_COUNTER0, cpuc->used_mask))
			return NDS32_IDX_COUNTER0;
		if (!test_and_set_bit(NDS32_IDX_COUNTER1, cpuc->used_mask))
			return NDS32_IDX_COUNTER1;
	} else if (evtype == SPAV3_1_SEL_COMPLETED_INSTRUCTION) {
		if (!test_and_set_bit(idx, cpuc->used_mask))
			return idx;
		else if (!test_and_set_bit(NDS32_IDX_COUNTER1, cpuc->used_mask))
			return NDS32_IDX_COUNTER1;
		else if (!test_and_set_bit
			 (NDS32_IDX_CYCLE_COUNTER, cpuc->used_mask))
			return NDS32_IDX_CYCLE_COUNTER;
	} else {
		if (!test_and_set_bit(idx, cpuc->used_mask))
			return idx;
	}
	return -EAGAIN;
}

static void nds32_pmu_start(struct nds32_pmu *cpu_pmu)
{
	unsigned long flags;
	unsigned int val;
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/* Enable all counters , NDS PFM has 3 counters */
	val = __nds32__mfsr(NDS32_SR_PFM_CTL);
	val |= (PFM_CTL_EN[0] | PFM_CTL_EN[1] | PFM_CTL_EN[2]);
	val &= ~(PFM_CTL_OVF[0] | PFM_CTL_OVF[1] | PFM_CTL_OVF[2]);
	__nds32__mtsr_isb(val, NDS32_SR_PFM_CTL);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void nds32_pmu_stop(struct nds32_pmu *cpu_pmu)
{
	unsigned long flags;
	unsigned int val;
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/* Disable all counters , NDS PFM has 3 counters */
	val = __nds32__mfsr(NDS32_SR_PFM_CTL);
	val &= ~(PFM_CTL_EN[0] | PFM_CTL_EN[1] | PFM_CTL_EN[2]);
	val &= ~(PFM_CTL_OVF[0] | PFM_CTL_OVF[1] | PFM_CTL_OVF[2]);
	__nds32__mtsr_isb(val, NDS32_SR_PFM_CTL);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void nds32_pmu_reset(void *info)
{
	u32 val = 0;

	val |= (PFM_CTL_OVF[0] | PFM_CTL_OVF[1] | PFM_CTL_OVF[2]);
	__nds32__mtsr(val, NDS32_SR_PFM_CTL);
	__nds32__mtsr(0, NDS32_SR_PFM_CTL);
	__nds32__mtsr(0, NDS32_SR_PFMC0);
	__nds32__mtsr(0, NDS32_SR_PFMC1);
	__nds32__mtsr(0, NDS32_SR_PFMC2);
}

static void nds32_pmu_init(struct nds32_pmu *cpu_pmu)
{
	cpu_pmu->handle_irq = nds32_pmu_handle_irq;
	cpu_pmu->enable = nds32_pmu_enable_event;
	cpu_pmu->disable = nds32_pmu_disable_event;
	cpu_pmu->read_counter = nds32_pmu_read_counter;
	cpu_pmu->write_counter = nds32_pmu_write_counter;
	cpu_pmu->get_event_idx = nds32_pmu_get_event_idx;
	cpu_pmu->start = nds32_pmu_start;
	cpu_pmu->stop = nds32_pmu_stop;
	cpu_pmu->reset = nds32_pmu_reset;
	cpu_pmu->max_period = 0xFFFFFFFF;	/* Maximum counts */
};

static u32 nds32_read_num_pfm_events(void)
{
	/* NDS32 SPAv3 PMU support 3 counter */
	return 3;
}

static int device_pmu_init(struct nds32_pmu *cpu_pmu)
{
	nds32_pmu_init(cpu_pmu);
	/*
	 * This name should be devive-specific name, whatever you like :)
	 * I think "PMU" will be a good generic name.
	 */
	cpu_pmu->name = "nds32v3-pmu";
	cpu_pmu->map_event = nds32_spav3_map_event;
	cpu_pmu->num_events = nds32_read_num_pfm_events();
	cpu_pmu->set_event_filter = nds32_pmu_set_event_filter;
	return 0;
}

/*
 * CPU PMU identification and probing.
 */
static int probe_current_pmu(struct nds32_pmu *pmu)
{
	int ret;

	get_cpu();
	ret = -ENODEV;
	/*
	 * If ther are various CPU types with its own PMU, initialize with
	 *
	 * the corresponding one
	 */
	device_pmu_init(pmu);
	put_cpu();
	return ret;
}

static void nds32_pmu_enable(struct pmu *pmu)
{
	struct nds32_pmu *nds32_pmu = to_nds32_pmu(pmu);
	struct pmu_hw_events *hw_events = nds32_pmu->get_hw_events();
	int enabled = bitmap_weight(hw_events->used_mask,
				    nds32_pmu->num_events);

	if (enabled)
		nds32_pmu->start(nds32_pmu);
}

static void nds32_pmu_disable(struct pmu *pmu)
{
	struct nds32_pmu *nds32_pmu = to_nds32_pmu(pmu);

	nds32_pmu->stop(nds32_pmu);
}

static void nds32_pmu_release_hardware(struct nds32_pmu *nds32_pmu)
{
	nds32_pmu->free_irq(nds32_pmu);
	pm_runtime_put_sync(&nds32_pmu->plat_device->dev);
}

static irqreturn_t nds32_pmu_dispatch_irq(int irq, void *dev)
{
	struct nds32_pmu *nds32_pmu = (struct nds32_pmu *)dev;
	int ret;
	u64 start_clock, finish_clock;

	start_clock = local_clock();
	ret = nds32_pmu->handle_irq(irq, dev);
	finish_clock = local_clock();

	perf_sample_event_took(finish_clock - start_clock);
	return ret;
}

static int nds32_pmu_reserve_hardware(struct nds32_pmu *nds32_pmu)
{
	int err;
	struct platform_device *pmu_device = nds32_pmu->plat_device;

	if (!pmu_device)
		return -ENODEV;

	pm_runtime_get_sync(&pmu_device->dev);
	err = nds32_pmu->request_irq(nds32_pmu, nds32_pmu_dispatch_irq);
	if (err) {
		nds32_pmu_release_hardware(nds32_pmu);
		return err;
	}

	return 0;
}

static int
validate_event(struct pmu *pmu, struct pmu_hw_events *hw_events,
	       struct perf_event *event)
{
	struct nds32_pmu *nds32_pmu = to_nds32_pmu(event->pmu);

	if (is_software_event(event))
		return 1;

	if (event->pmu != pmu)
		return 0;

	if (event->state < PERF_EVENT_STATE_OFF)
		return 1;

	if (event->state == PERF_EVENT_STATE_OFF && !event->attr.enable_on_exec)
		return 1;

	return nds32_pmu->get_event_idx(hw_events, event) >= 0;
}

static int validate_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct pmu_hw_events fake_pmu;
	DECLARE_BITMAP(fake_used_mask, MAX_COUNTERS);
	/*
	 * Initialize the fake PMU. We only need to populate the
	 * used_mask for the purposes of validation.
	 */
	memset(fake_used_mask, 0, sizeof(fake_used_mask));

	if (!validate_event(event->pmu, &fake_pmu, leader))
		return -EINVAL;

	for_each_sibling_event(sibling, leader) {
		if (!validate_event(event->pmu, &fake_pmu, sibling))
			return -EINVAL;
	}

	if (!validate_event(event->pmu, &fake_pmu, event))
		return -EINVAL;

	return 0;
}

static int __hw_perf_event_init(struct perf_event *event)
{
	struct nds32_pmu *nds32_pmu = to_nds32_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int mapping;

	mapping = nds32_pmu->map_event(event);

	if (mapping < 0) {
		pr_debug("event %x:%llx not supported\n", event->attr.type,
			 event->attr.config);
		return mapping;
	}

	/*
	 * We don't assign an index until we actually place the event onto
	 * hardware. Use -1 to signify that we haven't decided where to put it
	 * yet. For SMP systems, each core has it's own PMU so we can't do any
	 * clever allocation or constraints checking at this point.
	 */
	hwc->idx = -1;
	hwc->config_base = 0;
	hwc->config = 0;
	hwc->event_base = 0;

	/*
	 * Check whether we need to exclude the counter from certain modes.
	 */
	if ((!nds32_pmu->set_event_filter ||
	     nds32_pmu->set_event_filter(hwc, &event->attr)) &&
	    event_requires_mode_exclusion(&event->attr)) {
		pr_debug
			("NDS performance counters do not support mode exclusion\n");
		return -EOPNOTSUPP;
	}

	/*
	 * Store the event encoding into the config_base field.
	 */
	hwc->config_base |= (unsigned long)mapping;

	if (!hwc->sample_period) {
		/*
		 * For non-sampling runs, limit the sample_period to half
		 * of the counter width. That way, the new counter value
		 * is far less likely to overtake the previous one unless
		 * you have some serious IRQ latency issues.
		 */
		hwc->sample_period = nds32_pmu->max_period >> 1;
		hwc->last_period = hwc->sample_period;
		local64_set(&hwc->period_left, hwc->sample_period);
	}

	if (event->group_leader != event) {
		if (validate_group(event) != 0)
			return -EINVAL;
	}

	return 0;
}

static int nds32_pmu_event_init(struct perf_event *event)
{
	struct nds32_pmu *nds32_pmu = to_nds32_pmu(event->pmu);
	int err = 0;
	atomic_t *active_events = &nds32_pmu->active_events;

	/* does not support taken branch sampling */
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	if (nds32_pmu->map_event(event) == -ENOENT)
		return -ENOENT;

	if (!atomic_inc_not_zero(active_events)) {
		if (atomic_read(active_events) == 0) {
			/* Register irq handler */
			err = nds32_pmu_reserve_hardware(nds32_pmu);
		}

		if (!err)
			atomic_inc(active_events);
	}

	if (err)
		return err;

	err = __hw_perf_event_init(event);

	return err;
}

static void nds32_start(struct perf_event *event, int flags)
{
	struct nds32_pmu *nds32_pmu = to_nds32_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	/*
	 * NDS pmu always has to reprogram the period, so ignore
	 * PERF_EF_RELOAD, see the comment below.
	 */
	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));

	hwc->state = 0;
	/* Set the period for the event. */
	nds32_pmu_event_set_period(event);

	nds32_pmu->enable(event);
}

static int nds32_pmu_add(struct perf_event *event, int flags)
{
	struct nds32_pmu *nds32_pmu = to_nds32_pmu(event->pmu);
	struct pmu_hw_events *hw_events = nds32_pmu->get_hw_events();
	struct hw_perf_event *hwc = &event->hw;
	int idx;
	int err = 0;

	perf_pmu_disable(event->pmu);

	/* If we don't have a space for the counter then finish early. */
	idx = nds32_pmu->get_event_idx(hw_events, event);
	if (idx < 0) {
		err = idx;
		goto out;
	}

	/*
	 * If there is an event in the counter we are going to use then make
	 * sure it is disabled.
	 */
	event->hw.idx = idx;
	nds32_pmu->disable(event);
	hw_events->events[idx] = event;

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
	if (flags & PERF_EF_START)
		nds32_start(event, PERF_EF_RELOAD);

	/* Propagate our changes to the userspace mapping. */
	perf_event_update_userpage(event);

out:
	perf_pmu_enable(event->pmu);
	return err;
}

u64 nds32_pmu_event_update(struct perf_event *event)
{
	struct nds32_pmu *nds32_pmu = to_nds32_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev_raw_count, new_raw_count;

again:
	prev_raw_count = local64_read(&hwc->prev_count);
	new_raw_count = nds32_pmu->read_counter(event);

	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			    new_raw_count) != prev_raw_count) {
		goto again;
	}
	/*
	 * Whether overflow or not, "unsigned substraction"
	 * will always get their delta
	 */
	delta = (new_raw_count - prev_raw_count) & nds32_pmu->max_period;

	local64_add(delta, &event->count);
	local64_sub(delta, &hwc->period_left);

	return new_raw_count;
}

static void nds32_stop(struct perf_event *event, int flags)
{
	struct nds32_pmu *nds32_pmu = to_nds32_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	/*
	 * NDS pmu always has to update the counter, so ignore
	 * PERF_EF_UPDATE, see comments in nds32_start().
	 */
	if (!(hwc->state & PERF_HES_STOPPED)) {
		nds32_pmu->disable(event);
		nds32_pmu_event_update(event);
		hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
	}
}

static void nds32_pmu_del(struct perf_event *event, int flags)
{
	struct nds32_pmu *nds32_pmu = to_nds32_pmu(event->pmu);
	struct pmu_hw_events *hw_events = nds32_pmu->get_hw_events();
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	nds32_stop(event, PERF_EF_UPDATE);
	hw_events->events[idx] = NULL;
	clear_bit(idx, hw_events->used_mask);

	perf_event_update_userpage(event);
}

static void nds32_pmu_read(struct perf_event *event)
{
	nds32_pmu_event_update(event);
}

/* Please refer to SPAv3 for more hardware specific details */
PMU_FORMAT_ATTR(event, "config:0-63");

static struct attribute *nds32_arch_formats_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group nds32_pmu_format_group = {
	.name = "format",
	.attrs = nds32_arch_formats_attr,
};

static ssize_t nds32_pmu_cpumask_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return 0;
}

static DEVICE_ATTR(cpus, 0444, nds32_pmu_cpumask_show, NULL);

static struct attribute *nds32_pmu_common_attrs[] = {
	&dev_attr_cpus.attr,
	NULL,
};

static struct attribute_group nds32_pmu_common_group = {
	.attrs = nds32_pmu_common_attrs,
};

static const struct attribute_group *nds32_pmu_attr_groups[] = {
	&nds32_pmu_format_group,
	&nds32_pmu_common_group,
	NULL,
};

static void nds32_init(struct nds32_pmu *nds32_pmu)
{
	atomic_set(&nds32_pmu->active_events, 0);

	nds32_pmu->pmu = (struct pmu) {
		.pmu_enable = nds32_pmu_enable,
		.pmu_disable = nds32_pmu_disable,
		.attr_groups = nds32_pmu_attr_groups,
		.event_init = nds32_pmu_event_init,
		.add = nds32_pmu_add,
		.del = nds32_pmu_del,
		.start = nds32_start,
		.stop = nds32_stop,
		.read = nds32_pmu_read,
	};
}

int nds32_pmu_register(struct nds32_pmu *nds32_pmu, int type)
{
	nds32_init(nds32_pmu);
	pm_runtime_enable(&nds32_pmu->plat_device->dev);
	pr_info("enabled with %s PMU driver, %d counters available\n",
		nds32_pmu->name, nds32_pmu->num_events);
	return perf_pmu_register(&nds32_pmu->pmu, nds32_pmu->name, type);
}

static struct pmu_hw_events *cpu_pmu_get_cpu_events(void)
{
	return this_cpu_ptr(&cpu_hw_events);
}

static int cpu_pmu_request_irq(struct nds32_pmu *cpu_pmu, irq_handler_t handler)
{
	int err, irq, irqs;
	struct platform_device *pmu_device = cpu_pmu->plat_device;

	if (!pmu_device)
		return -ENODEV;

	irqs = min(pmu_device->num_resources, num_possible_cpus());
	if (irqs < 1) {
		pr_err("no irqs for PMUs defined\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pmu_device, 0);
	err = request_irq(irq, handler, IRQF_NOBALANCING, "nds32-pfm",
			  cpu_pmu);
	if (err) {
		pr_err("unable to request IRQ%d for NDS PMU counters\n",
		       irq);
		return err;
	}
	return 0;
}

static void cpu_pmu_free_irq(struct nds32_pmu *cpu_pmu)
{
	int irq;
	struct platform_device *pmu_device = cpu_pmu->plat_device;

	irq = platform_get_irq(pmu_device, 0);
	if (irq >= 0)
		free_irq(irq, cpu_pmu);
}

static void cpu_pmu_init(struct nds32_pmu *cpu_pmu)
{
	int cpu;
	struct pmu_hw_events *events = &per_cpu(cpu_hw_events, cpu);

	raw_spin_lock_init(&events->pmu_lock);

	cpu_pmu->get_hw_events = cpu_pmu_get_cpu_events;
	cpu_pmu->request_irq = cpu_pmu_request_irq;
	cpu_pmu->free_irq = cpu_pmu_free_irq;

	/* Ensure the PMU has sane values out of reset. */
	if (cpu_pmu->reset)
		on_each_cpu(cpu_pmu->reset, cpu_pmu, 1);
}

static const struct of_device_id cpu_pmu_of_device_ids[] = {
	{.compatible = "andestech,nds32v3-pmu",
	 .data = device_pmu_init},
	{},
};

static int cpu_pmu_device_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	int (*init_fn)(struct nds32_pmu *nds32_pmu);
	struct device_node *node = pdev->dev.of_node;
	struct nds32_pmu *pmu;
	int ret = -ENODEV;

	if (cpu_pmu) {
		pr_notice("[perf] attempt to register multiple PMU devices!\n");
		return -ENOSPC;
	}

	pmu = kzalloc(sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	of_id = of_match_node(cpu_pmu_of_device_ids, pdev->dev.of_node);
	if (node && of_id) {
		init_fn = of_id->data;
		ret = init_fn(pmu);
	} else {
		ret = probe_current_pmu(pmu);
	}

	if (ret) {
		pr_notice("[perf] failed to probe PMU!\n");
		goto out_free;
	}

	cpu_pmu = pmu;
	cpu_pmu->plat_device = pdev;
	cpu_pmu_init(cpu_pmu);
	ret = nds32_pmu_register(cpu_pmu, PERF_TYPE_RAW);

	if (!ret)
		return 0;

out_free:
	pr_notice("[perf] failed to register PMU devices!\n");
	kfree(pmu);
	return ret;
}

static struct platform_driver cpu_pmu_driver = {
	.driver = {
		   .name = "nds32-pfm",
		   .of_match_table = cpu_pmu_of_device_ids,
		   },
	.probe = cpu_pmu_device_probe,
	.id_table = cpu_pmu_plat_device_ids,
};

static int __init register_pmu_driver(void)
{
	int err = 0;

	err = platform_driver_register(&cpu_pmu_driver);
	if (err)
		pr_notice("[perf] PMU initialization failed\n");
	else
		pr_notice("[perf] PMU initialization done\n");

	return err;
}

device_initcall(register_pmu_driver);

/*
 * References: arch/nds32/kernel/traps.c:__dump()
 * You will need to know the NDS ABI first.
 */
static int unwind_frame_kernel(struct stackframe *frame)
{
	int graph = 0;
#ifdef CONFIG_FRAME_POINTER
	/* 0x3 means misalignment */
	if (!kstack_end((void *)frame->fp) &&
	    !((unsigned long)frame->fp & 0x3) &&
	    ((unsigned long)frame->fp >= TASK_SIZE)) {
		/*
		 *	The array index is based on the ABI, the below graph
		 *	illustrate the reasons.
		 *	Function call procedure: "smw" and "lmw" will always
		 *	update SP and FP for you automatically.
		 *
		 *	Stack                                 Relative Address
		 *	|  |                                          0
		 *	----
		 *	|LP| <-- SP(before smw)  <-- FP(after smw)   -1
		 *	----
		 *	|FP|                                         -2
		 *	----
		 *	|  | <-- SP(after smw)                       -3
		 */
		frame->lp = ((unsigned long *)frame->fp)[-1];
		frame->fp = ((unsigned long *)frame->fp)[FP_OFFSET];
		/* make sure CONFIG_FUNCTION_GRAPH_TRACER is turned on */
		if (__kernel_text_address(frame->lp))
			frame->lp = ftrace_graph_ret_addr
						(NULL, &graph, frame->lp, NULL);

		return 0;
	} else {
		return -EPERM;
	}
#else
	/*
	 * You can refer to arch/nds32/kernel/traps.c:__dump()
	 * Treat "sp" as "fp", but the "sp" is one frame ahead of "fp".
	 * And, the "sp" is not always correct.
	 *
	 *   Stack                                 Relative Address
	 *   |  |                                          0
	 *   ----
	 *   |LP| <-- SP(before smw)                      -1
	 *   ----
	 *   |  | <-- SP(after smw)                       -2
	 *   ----
	 */
	if (!kstack_end((void *)frame->sp)) {
		frame->lp = ((unsigned long *)frame->sp)[1];
		/* TODO: How to deal with the value in first
		 * "sp" is not correct?
		 */
		if (__kernel_text_address(frame->lp))
			frame->lp = ftrace_graph_ret_addr
						(tsk, &graph, frame->lp, NULL);

		frame->sp = ((unsigned long *)frame->sp) + 1;

		return 0;
	} else {
		return -EPERM;
	}
#endif
}

static void notrace
walk_stackframe(struct stackframe *frame,
		int (*fn_record)(struct stackframe *, void *),
		void *data)
{
	while (1) {
		int ret;

		if (fn_record(frame, data))
			break;

		ret = unwind_frame_kernel(frame);
		if (ret < 0)
			break;
	}
}

/*
 * Gets called by walk_stackframe() for every stackframe. This will be called
 * whist unwinding the stackframe and is like a subroutine return so we use
 * the PC.
 */
static int callchain_trace(struct stackframe *fr, void *data)
{
	struct perf_callchain_entry_ctx *entry = data;

	perf_callchain_store(entry, fr->lp);
	return 0;
}

/*
 * Get the return address for a single stackframe and return a pointer to the
 * next frame tail.
 */
static unsigned long
user_backtrace(struct perf_callchain_entry_ctx *entry, unsigned long fp)
{
	struct frame_tail buftail;
	unsigned long lp = 0;
	unsigned long *user_frame_tail =
		(unsigned long *)(fp - (unsigned long)sizeof(buftail));

	/* Check accessibility of one struct frame_tail beyond */
	if (!access_ok(user_frame_tail, sizeof(buftail)))
		return 0;
	if (__copy_from_user_inatomic
		(&buftail, user_frame_tail, sizeof(buftail)))
		return 0;

	/*
	 * Refer to unwind_frame_kernel() for more illurstration
	 */
	lp = buftail.stack_lp;  /* ((unsigned long *)fp)[-1] */
	fp = buftail.stack_fp;  /* ((unsigned long *)fp)[FP_OFFSET] */
	perf_callchain_store(entry, lp);
	return fp;
}

static unsigned long
user_backtrace_opt_size(struct perf_callchain_entry_ctx *entry,
			unsigned long fp)
{
	struct frame_tail_opt_size buftail;
	unsigned long lp = 0;

	unsigned long *user_frame_tail =
		(unsigned long *)(fp - (unsigned long)sizeof(buftail));

	/* Check accessibility of one struct frame_tail beyond */
	if (!access_ok(user_frame_tail, sizeof(buftail)))
		return 0;
	if (__copy_from_user_inatomic
		(&buftail, user_frame_tail, sizeof(buftail)))
		return 0;

	/*
	 * Refer to unwind_frame_kernel() for more illurstration
	 */
	lp = buftail.stack_lp;  /* ((unsigned long *)fp)[-1] */
	fp = buftail.stack_fp;  /* ((unsigned long *)fp)[FP_OFFSET] */

	perf_callchain_store(entry, lp);
	return fp;
}

/*
 * This will be called when the target is in user mode
 * This function will only be called when we use
 * "PERF_SAMPLE_CALLCHAIN" in
 * kernel/events/core.c:perf_prepare_sample()
 *
 * How to trigger perf_callchain_[user/kernel] :
 * $ perf record -e cpu-clock --call-graph fp ./program
 * $ perf report --call-graph
 */
unsigned long leaf_fp;
void
perf_callchain_user(struct perf_callchain_entry_ctx *entry,
		    struct pt_regs *regs)
{
	unsigned long fp = 0;
	unsigned long gp = 0;
	unsigned long lp = 0;
	unsigned long sp = 0;
	unsigned long *user_frame_tail;

	leaf_fp = 0;

	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		/* We don't support guest os callchain now */
		return;
	}

	perf_callchain_store(entry, regs->ipc);
	fp = regs->fp;
	gp = regs->gp;
	lp = regs->lp;
	sp = regs->sp;
	if (entry->nr < PERF_MAX_STACK_DEPTH &&
	    (unsigned long)fp && !((unsigned long)fp & 0x7) && fp > sp) {
		user_frame_tail =
			(unsigned long *)(fp - (unsigned long)sizeof(fp));

		if (!access_ok(user_frame_tail, sizeof(fp)))
			return;

		if (__copy_from_user_inatomic
			(&leaf_fp, user_frame_tail, sizeof(fp)))
			return;

		if (leaf_fp == lp) {
			/*
			 * Maybe this is non leaf function
			 * with optimize for size,
			 * or maybe this is the function
			 * with optimize for size
			 */
			struct frame_tail buftail;

			user_frame_tail =
				(unsigned long *)(fp -
					(unsigned long)sizeof(buftail));

			if (!access_ok(user_frame_tail, sizeof(buftail)))
				return;

			if (__copy_from_user_inatomic
				(&buftail, user_frame_tail, sizeof(buftail)))
				return;

			if (buftail.stack_fp == gp) {
				/* non leaf function with optimize
				 * for size condition
				 */
				struct frame_tail_opt_size buftail_opt_size;

				user_frame_tail =
					(unsigned long *)(fp - (unsigned long)
						sizeof(buftail_opt_size));

				if (!access_ok(user_frame_tail,
					       sizeof(buftail_opt_size)))
					return;

				if (__copy_from_user_inatomic
				   (&buftail_opt_size, user_frame_tail,
				   sizeof(buftail_opt_size)))
					return;

				perf_callchain_store(entry, lp);
				fp = buftail_opt_size.stack_fp;

				while ((entry->nr < PERF_MAX_STACK_DEPTH) &&
				       (unsigned long)fp &&
						!((unsigned long)fp & 0x7) &&
						fp > sp) {
					sp = fp;
					fp = user_backtrace_opt_size(entry, fp);
				}

			} else {
				/* this is the function
				 * without optimize for size
				 */
				fp = buftail.stack_fp;
				perf_callchain_store(entry, lp);
				while ((entry->nr < PERF_MAX_STACK_DEPTH) &&
				       (unsigned long)fp &&
						!((unsigned long)fp & 0x7) &&
						fp > sp) {
					sp = fp;
					fp = user_backtrace(entry, fp);
				}
			}
		} else {
			/* this is leaf function */
			fp = leaf_fp;
			perf_callchain_store(entry, lp);

			/* previous function callcahin  */
			while ((entry->nr < PERF_MAX_STACK_DEPTH) &&
			       (unsigned long)fp &&
				   !((unsigned long)fp & 0x7) && fp > sp) {
				sp = fp;
				fp = user_backtrace(entry, fp);
			}
		}
		return;
	}
}

/* This will be called when the target is in kernel mode */
void
perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
		      struct pt_regs *regs)
{
	struct stackframe fr;

	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		/* We don't support guest os callchain now */
		return;
	}
	fr.fp = regs->fp;
	fr.lp = regs->lp;
	fr.sp = regs->sp;
	walk_stackframe(&fr, callchain_trace, entry);
}

unsigned long perf_instruction_pointer(struct pt_regs *regs)
{
	/* However, NDS32 does not support virtualization */
	if (perf_guest_cbs && perf_guest_cbs->is_in_guest())
		return perf_guest_cbs->get_guest_ip();

	return instruction_pointer(regs);
}

unsigned long perf_misc_flags(struct pt_regs *regs)
{
	int misc = 0;

	/* However, NDS32 does not support virtualization */
	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		if (perf_guest_cbs->is_user_mode())
			misc |= PERF_RECORD_MISC_GUEST_USER;
		else
			misc |= PERF_RECORD_MISC_GUEST_KERNEL;
	} else {
		if (user_mode(regs))
			misc |= PERF_RECORD_MISC_USER;
		else
			misc |= PERF_RECORD_MISC_KERNEL;
	}

	return misc;
}
