// SPDX-License-Identifier: GPL-2.0
/*
 * ARMv6 Performance counter handling code.
 *
 * Copyright (C) 2009 picoChip Designs, Ltd., Jamie Iles
 *
 * ARMv6 has 2 configurable performance counters and a single cycle counter.
 * They all share a single reset bit but can be written to zero so we can use
 * that for a reset.
 *
 * The counters can't be individually enabled or disabled so when we remove
 * one event and replace it with another we could get spurious counts from the
 * wrong event. However, we can take advantage of the fact that the
 * performance counters can export events to the event bus, and the event bus
 * itself can be monitored. This requires that we *don't* export the events to
 * the event bus. The procedure for disabling a configurable counter is:
 *	- change the counter to count the ETMEXTOUT[0] signal (0x20). This
 *	  effectively stops the counter from counting.
 *	- disable the counter's interrupt generation (each counter has it's
 *	  own interrupt enable bit).
 * Once stopped, the counter value can be written as 0 to reset.
 *
 * To enable a counter:
 *	- enable the counter's interrupt generation.
 *	- set the new event type.
 *
 * Note: the dedicated cycle counter only counts cycles and can't be
 * enabled/disabled independently of the others. When we want to disable the
 * cycle counter, we have to just disable the interrupt reporting and start
 * ignoring that counter. When re-enabling, we have to reset the value and
 * enable the interrupt.
 */

#if defined(CONFIG_CPU_V6) || defined(CONFIG_CPU_V6K)

#include <asm/cputype.h>
#include <asm/irq_regs.h>

#include <linux/of.h>
#include <linux/perf/arm_pmu.h>
#include <linux/platform_device.h>

enum armv6_perf_types {
	ARMV6_PERFCTR_ICACHE_MISS	    = 0x0,
	ARMV6_PERFCTR_IBUF_STALL	    = 0x1,
	ARMV6_PERFCTR_DDEP_STALL	    = 0x2,
	ARMV6_PERFCTR_ITLB_MISS		    = 0x3,
	ARMV6_PERFCTR_DTLB_MISS		    = 0x4,
	ARMV6_PERFCTR_BR_EXEC		    = 0x5,
	ARMV6_PERFCTR_BR_MISPREDICT	    = 0x6,
	ARMV6_PERFCTR_INSTR_EXEC	    = 0x7,
	ARMV6_PERFCTR_DCACHE_HIT	    = 0x9,
	ARMV6_PERFCTR_DCACHE_ACCESS	    = 0xA,
	ARMV6_PERFCTR_DCACHE_MISS	    = 0xB,
	ARMV6_PERFCTR_DCACHE_WBACK	    = 0xC,
	ARMV6_PERFCTR_SW_PC_CHANGE	    = 0xD,
	ARMV6_PERFCTR_MAIN_TLB_MISS	    = 0xF,
	ARMV6_PERFCTR_EXPL_D_ACCESS	    = 0x10,
	ARMV6_PERFCTR_LSU_FULL_STALL	    = 0x11,
	ARMV6_PERFCTR_WBUF_DRAINED	    = 0x12,
	ARMV6_PERFCTR_CPU_CYCLES	    = 0xFF,
	ARMV6_PERFCTR_NOP		    = 0x20,
};

enum armv6_counters {
	ARMV6_CYCLE_COUNTER = 0,
	ARMV6_COUNTER0,
	ARMV6_COUNTER1,
};

/*
 * The hardware events that we support. We do support cache operations but
 * we have harvard caches and no way to combine instruction and data
 * accesses/misses in hardware.
 */
static const unsigned armv6_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV6_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV6_PERFCTR_INSTR_EXEC,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= ARMV6_PERFCTR_BR_EXEC,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV6_PERFCTR_BR_MISPREDICT,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= ARMV6_PERFCTR_IBUF_STALL,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= ARMV6_PERFCTR_LSU_FULL_STALL,
};

static const unsigned armv6_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					  [PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	/*
	 * The performance counters don't differentiate between read and write
	 * accesses/misses so this isn't strictly correct, but it's the best we
	 * can do. Writes and reads get combined.
	 */
	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= ARMV6_PERFCTR_DCACHE_ACCESS,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= ARMV6_PERFCTR_DCACHE_MISS,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= ARMV6_PERFCTR_DCACHE_ACCESS,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV6_PERFCTR_DCACHE_MISS,

	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= ARMV6_PERFCTR_ICACHE_MISS,

	/*
	 * The ARM performance counters can count micro DTLB misses, micro ITLB
	 * misses and main TLB misses. There isn't an event for TLB misses, so
	 * use the micro misses here and if users want the main TLB misses they
	 * can use a raw counter.
	 */
	[C(DTLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV6_PERFCTR_DTLB_MISS,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV6_PERFCTR_DTLB_MISS,

	[C(ITLB)][C(OP_READ)][C(RESULT_MISS)]	= ARMV6_PERFCTR_ITLB_MISS,
	[C(ITLB)][C(OP_WRITE)][C(RESULT_MISS)]	= ARMV6_PERFCTR_ITLB_MISS,
};

static inline unsigned long
armv6_pmcr_read(void)
{
	u32 val;
	asm volatile("mrc   p15, 0, %0, c15, c12, 0" : "=r"(val));
	return val;
}

static inline void
armv6_pmcr_write(unsigned long val)
{
	asm volatile("mcr   p15, 0, %0, c15, c12, 0" : : "r"(val));
}

#define ARMV6_PMCR_ENABLE		(1 << 0)
#define ARMV6_PMCR_CTR01_RESET		(1 << 1)
#define ARMV6_PMCR_CCOUNT_RESET		(1 << 2)
#define ARMV6_PMCR_CCOUNT_DIV		(1 << 3)
#define ARMV6_PMCR_COUNT0_IEN		(1 << 4)
#define ARMV6_PMCR_COUNT1_IEN		(1 << 5)
#define ARMV6_PMCR_CCOUNT_IEN		(1 << 6)
#define ARMV6_PMCR_COUNT0_OVERFLOW	(1 << 8)
#define ARMV6_PMCR_COUNT1_OVERFLOW	(1 << 9)
#define ARMV6_PMCR_CCOUNT_OVERFLOW	(1 << 10)
#define ARMV6_PMCR_EVT_COUNT0_SHIFT	20
#define ARMV6_PMCR_EVT_COUNT0_MASK	(0xFF << ARMV6_PMCR_EVT_COUNT0_SHIFT)
#define ARMV6_PMCR_EVT_COUNT1_SHIFT	12
#define ARMV6_PMCR_EVT_COUNT1_MASK	(0xFF << ARMV6_PMCR_EVT_COUNT1_SHIFT)

#define ARMV6_PMCR_OVERFLOWED_MASK \
	(ARMV6_PMCR_COUNT0_OVERFLOW | ARMV6_PMCR_COUNT1_OVERFLOW | \
	 ARMV6_PMCR_CCOUNT_OVERFLOW)

static inline int
armv6_pmcr_has_overflowed(unsigned long pmcr)
{
	return pmcr & ARMV6_PMCR_OVERFLOWED_MASK;
}

static inline int
armv6_pmcr_counter_has_overflowed(unsigned long pmcr,
				  enum armv6_counters counter)
{
	int ret = 0;

	if (ARMV6_CYCLE_COUNTER == counter)
		ret = pmcr & ARMV6_PMCR_CCOUNT_OVERFLOW;
	else if (ARMV6_COUNTER0 == counter)
		ret = pmcr & ARMV6_PMCR_COUNT0_OVERFLOW;
	else if (ARMV6_COUNTER1 == counter)
		ret = pmcr & ARMV6_PMCR_COUNT1_OVERFLOW;
	else
		WARN_ONCE(1, "invalid counter number (%d)\n", counter);

	return ret;
}

static inline u64 armv6pmu_read_counter(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;
	unsigned long value = 0;

	if (ARMV6_CYCLE_COUNTER == counter)
		asm volatile("mrc   p15, 0, %0, c15, c12, 1" : "=r"(value));
	else if (ARMV6_COUNTER0 == counter)
		asm volatile("mrc   p15, 0, %0, c15, c12, 2" : "=r"(value));
	else if (ARMV6_COUNTER1 == counter)
		asm volatile("mrc   p15, 0, %0, c15, c12, 3" : "=r"(value));
	else
		WARN_ONCE(1, "invalid counter number (%d)\n", counter);

	return value;
}

static inline void armv6pmu_write_counter(struct perf_event *event, u64 value)
{
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	if (ARMV6_CYCLE_COUNTER == counter)
		asm volatile("mcr   p15, 0, %0, c15, c12, 1" : : "r"(value));
	else if (ARMV6_COUNTER0 == counter)
		asm volatile("mcr   p15, 0, %0, c15, c12, 2" : : "r"(value));
	else if (ARMV6_COUNTER1 == counter)
		asm volatile("mcr   p15, 0, %0, c15, c12, 3" : : "r"(value));
	else
		WARN_ONCE(1, "invalid counter number (%d)\n", counter);
}

static void armv6pmu_enable_event(struct perf_event *event)
{
	unsigned long val, mask, evt;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (ARMV6_CYCLE_COUNTER == idx) {
		mask	= 0;
		evt	= ARMV6_PMCR_CCOUNT_IEN;
	} else if (ARMV6_COUNTER0 == idx) {
		mask	= ARMV6_PMCR_EVT_COUNT0_MASK;
		evt	= (hwc->config_base << ARMV6_PMCR_EVT_COUNT0_SHIFT) |
			  ARMV6_PMCR_COUNT0_IEN;
	} else if (ARMV6_COUNTER1 == idx) {
		mask	= ARMV6_PMCR_EVT_COUNT1_MASK;
		evt	= (hwc->config_base << ARMV6_PMCR_EVT_COUNT1_SHIFT) |
			  ARMV6_PMCR_COUNT1_IEN;
	} else {
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	/*
	 * Mask out the current event and set the counter to count the event
	 * that we're interested in.
	 */
	val = armv6_pmcr_read();
	val &= ~mask;
	val |= evt;
	armv6_pmcr_write(val);
}

static irqreturn_t
armv6pmu_handle_irq(struct arm_pmu *cpu_pmu)
{
	unsigned long pmcr = armv6_pmcr_read();
	struct perf_sample_data data;
	struct pmu_hw_events *cpuc = this_cpu_ptr(cpu_pmu->hw_events);
	struct pt_regs *regs;
	int idx;

	if (!armv6_pmcr_has_overflowed(pmcr))
		return IRQ_NONE;

	regs = get_irq_regs();

	/*
	 * The interrupts are cleared by writing the overflow flags back to
	 * the control register. All of the other bits don't have any effect
	 * if they are rewritten, so write the whole value back.
	 */
	armv6_pmcr_write(pmcr);

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
		if (!armv6_pmcr_counter_has_overflowed(pmcr, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event);
		perf_sample_data_init(&data, 0, hwc->last_period);
		if (!armpmu_event_set_period(event))
			continue;

		if (perf_event_overflow(event, &data, regs))
			cpu_pmu->disable(event);
	}

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

static void armv6pmu_start(struct arm_pmu *cpu_pmu)
{
	unsigned long val;

	val = armv6_pmcr_read();
	val |= ARMV6_PMCR_ENABLE;
	armv6_pmcr_write(val);
}

static void armv6pmu_stop(struct arm_pmu *cpu_pmu)
{
	unsigned long val;

	val = armv6_pmcr_read();
	val &= ~ARMV6_PMCR_ENABLE;
	armv6_pmcr_write(val);
}

static int
armv6pmu_get_event_idx(struct pmu_hw_events *cpuc,
				struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	/* Always place a cycle counter into the cycle counter. */
	if (ARMV6_PERFCTR_CPU_CYCLES == hwc->config_base) {
		if (test_and_set_bit(ARMV6_CYCLE_COUNTER, cpuc->used_mask))
			return -EAGAIN;

		return ARMV6_CYCLE_COUNTER;
	} else {
		/*
		 * For anything other than a cycle counter, try and use
		 * counter0 and counter1.
		 */
		if (!test_and_set_bit(ARMV6_COUNTER1, cpuc->used_mask))
			return ARMV6_COUNTER1;

		if (!test_and_set_bit(ARMV6_COUNTER0, cpuc->used_mask))
			return ARMV6_COUNTER0;

		/* The counters are all in use. */
		return -EAGAIN;
	}
}

static void armv6pmu_clear_event_idx(struct pmu_hw_events *cpuc,
				     struct perf_event *event)
{
	clear_bit(event->hw.idx, cpuc->used_mask);
}

static void armv6pmu_disable_event(struct perf_event *event)
{
	unsigned long val, mask, evt;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (ARMV6_CYCLE_COUNTER == idx) {
		mask	= ARMV6_PMCR_CCOUNT_IEN;
		evt	= 0;
	} else if (ARMV6_COUNTER0 == idx) {
		mask	= ARMV6_PMCR_COUNT0_IEN | ARMV6_PMCR_EVT_COUNT0_MASK;
		evt	= ARMV6_PERFCTR_NOP << ARMV6_PMCR_EVT_COUNT0_SHIFT;
	} else if (ARMV6_COUNTER1 == idx) {
		mask	= ARMV6_PMCR_COUNT1_IEN | ARMV6_PMCR_EVT_COUNT1_MASK;
		evt	= ARMV6_PERFCTR_NOP << ARMV6_PMCR_EVT_COUNT1_SHIFT;
	} else {
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	/*
	 * Mask out the current event and set the counter to count the number
	 * of ETM bus signal assertion cycles. The external reporting should
	 * be disabled and so this should never increment.
	 */
	val = armv6_pmcr_read();
	val &= ~mask;
	val |= evt;
	armv6_pmcr_write(val);
}

static int armv6_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &armv6_perf_map,
				&armv6_perf_cache_map, 0xFF);
}

static void armv6pmu_init(struct arm_pmu *cpu_pmu)
{
	cpu_pmu->handle_irq	= armv6pmu_handle_irq;
	cpu_pmu->enable		= armv6pmu_enable_event;
	cpu_pmu->disable	= armv6pmu_disable_event;
	cpu_pmu->read_counter	= armv6pmu_read_counter;
	cpu_pmu->write_counter	= armv6pmu_write_counter;
	cpu_pmu->get_event_idx	= armv6pmu_get_event_idx;
	cpu_pmu->clear_event_idx = armv6pmu_clear_event_idx;
	cpu_pmu->start		= armv6pmu_start;
	cpu_pmu->stop		= armv6pmu_stop;
	cpu_pmu->map_event	= armv6_map_event;
	cpu_pmu->num_events	= 3;
}

static int armv6_1136_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv6pmu_init(cpu_pmu);
	cpu_pmu->name		= "armv6_1136";
	return 0;
}

static int armv6_1156_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv6pmu_init(cpu_pmu);
	cpu_pmu->name		= "armv6_1156";
	return 0;
}

static int armv6_1176_pmu_init(struct arm_pmu *cpu_pmu)
{
	armv6pmu_init(cpu_pmu);
	cpu_pmu->name		= "armv6_1176";
	return 0;
}

static const struct of_device_id armv6_pmu_of_device_ids[] = {
	{.compatible = "arm,arm1176-pmu",	.data = armv6_1176_pmu_init},
	{.compatible = "arm,arm1136-pmu",	.data = armv6_1136_pmu_init},
	{ /* sentinel value */ }
};

static const struct pmu_probe_info armv6_pmu_probe_table[] = {
	ARM_PMU_PROBE(ARM_CPU_PART_ARM1136, armv6_1136_pmu_init),
	ARM_PMU_PROBE(ARM_CPU_PART_ARM1156, armv6_1156_pmu_init),
	ARM_PMU_PROBE(ARM_CPU_PART_ARM1176, armv6_1176_pmu_init),
	{ /* sentinel value */ }
};

static int armv6_pmu_device_probe(struct platform_device *pdev)
{
	return arm_pmu_device_probe(pdev, armv6_pmu_of_device_ids,
				    armv6_pmu_probe_table);
}

static struct platform_driver armv6_pmu_driver = {
	.driver		= {
		.name	= "armv6-pmu",
		.of_match_table = armv6_pmu_of_device_ids,
	},
	.probe		= armv6_pmu_device_probe,
};

builtin_platform_driver(armv6_pmu_driver);
#endif	/* CONFIG_CPU_V6 || CONFIG_CPU_V6K */
