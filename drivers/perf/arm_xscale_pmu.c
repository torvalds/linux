// SPDX-License-Identifier: GPL-2.0
/*
 * ARMv5 [xscale] Performance counter handling code.
 *
 * Copyright (C) 2010, ARM Ltd., Will Deacon <will.deacon@arm.com>
 *
 * Based on the previous xscale OProfile code.
 *
 * There are two variants of the xscale PMU that we support:
 * 	- xscale1pmu: 2 event counters and a cycle counter
 * 	- xscale2pmu: 4 event counters and a cycle counter
 * The two variants share event definitions, but have different
 * PMU structures.
 */

#include <asm/cputype.h>
#include <asm/irq_regs.h>

#include <linux/of.h>
#include <linux/perf/arm_pmu.h>
#include <linux/platform_device.h>

enum xscale_perf_types {
	XSCALE_PERFCTR_ICACHE_MISS		= 0x00,
	XSCALE_PERFCTR_ICACHE_NO_DELIVER	= 0x01,
	XSCALE_PERFCTR_DATA_STALL		= 0x02,
	XSCALE_PERFCTR_ITLB_MISS		= 0x03,
	XSCALE_PERFCTR_DTLB_MISS		= 0x04,
	XSCALE_PERFCTR_BRANCH			= 0x05,
	XSCALE_PERFCTR_BRANCH_MISS		= 0x06,
	XSCALE_PERFCTR_INSTRUCTION		= 0x07,
	XSCALE_PERFCTR_DCACHE_FULL_STALL	= 0x08,
	XSCALE_PERFCTR_DCACHE_FULL_STALL_CONTIG	= 0x09,
	XSCALE_PERFCTR_DCACHE_ACCESS		= 0x0A,
	XSCALE_PERFCTR_DCACHE_MISS		= 0x0B,
	XSCALE_PERFCTR_DCACHE_WRITE_BACK	= 0x0C,
	XSCALE_PERFCTR_PC_CHANGED		= 0x0D,
	XSCALE_PERFCTR_BCU_REQUEST		= 0x10,
	XSCALE_PERFCTR_BCU_FULL			= 0x11,
	XSCALE_PERFCTR_BCU_DRAIN		= 0x12,
	XSCALE_PERFCTR_BCU_ECC_NO_ELOG		= 0x14,
	XSCALE_PERFCTR_BCU_1_BIT_ERR		= 0x15,
	XSCALE_PERFCTR_RMW			= 0x16,
	/* XSCALE_PERFCTR_CCNT is not hardware defined */
	XSCALE_PERFCTR_CCNT			= 0xFE,
	XSCALE_PERFCTR_UNUSED			= 0xFF,
};

enum xscale_counters {
	XSCALE_CYCLE_COUNTER	= 0,
	XSCALE_COUNTER0,
	XSCALE_COUNTER1,
	XSCALE_COUNTER2,
	XSCALE_COUNTER3,
};
#define XSCALE1_NUM_COUNTERS	3
#define XSCALE2_NUM_COUNTERS	5

static const unsigned xscale_perf_map[PERF_COUNT_HW_MAX] = {
	PERF_MAP_ALL_UNSUPPORTED,
	[PERF_COUNT_HW_CPU_CYCLES]		= XSCALE_PERFCTR_CCNT,
	[PERF_COUNT_HW_INSTRUCTIONS]		= XSCALE_PERFCTR_INSTRUCTION,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= XSCALE_PERFCTR_BRANCH,
	[PERF_COUNT_HW_BRANCH_MISSES]		= XSCALE_PERFCTR_BRANCH_MISS,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= XSCALE_PERFCTR_ICACHE_NO_DELIVER,
};

static const unsigned xscale_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					   [PERF_COUNT_HW_CACHE_OP_MAX]
					   [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	PERF_CACHE_MAP_ALL_UNSUPPORTED,

	[C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]	= XSCALE_PERFCTR_DCACHE_ACCESS,
	[C(L1D)][C(OP_READ)][C(RESULT_MISS)]	= XSCALE_PERFCTR_DCACHE_MISS,
	[C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]	= XSCALE_PERFCTR_DCACHE_ACCESS,
	[C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]	= XSCALE_PERFCTR_DCACHE_MISS,

	[C(L1I)][C(OP_READ)][C(RESULT_MISS)]	= XSCALE_PERFCTR_ICACHE_MISS,

	[C(DTLB)][C(OP_READ)][C(RESULT_MISS)]	= XSCALE_PERFCTR_DTLB_MISS,
	[C(DTLB)][C(OP_WRITE)][C(RESULT_MISS)]	= XSCALE_PERFCTR_DTLB_MISS,

	[C(ITLB)][C(OP_READ)][C(RESULT_MISS)]	= XSCALE_PERFCTR_ITLB_MISS,
	[C(ITLB)][C(OP_WRITE)][C(RESULT_MISS)]	= XSCALE_PERFCTR_ITLB_MISS,
};

#define	XSCALE_PMU_ENABLE	0x001
#define XSCALE_PMN_RESET	0x002
#define	XSCALE_CCNT_RESET	0x004
#define	XSCALE_PMU_RESET	(CCNT_RESET | PMN_RESET)
#define XSCALE_PMU_CNT64	0x008

#define XSCALE1_OVERFLOWED_MASK	0x700
#define XSCALE1_CCOUNT_OVERFLOW	0x400
#define XSCALE1_COUNT0_OVERFLOW	0x100
#define XSCALE1_COUNT1_OVERFLOW	0x200
#define XSCALE1_CCOUNT_INT_EN	0x040
#define XSCALE1_COUNT0_INT_EN	0x010
#define XSCALE1_COUNT1_INT_EN	0x020
#define XSCALE1_COUNT0_EVT_SHFT	12
#define XSCALE1_COUNT0_EVT_MASK	(0xff << XSCALE1_COUNT0_EVT_SHFT)
#define XSCALE1_COUNT1_EVT_SHFT	20
#define XSCALE1_COUNT1_EVT_MASK	(0xff << XSCALE1_COUNT1_EVT_SHFT)

static inline u32
xscale1pmu_read_pmnc(void)
{
	u32 val;
	asm volatile("mrc p14, 0, %0, c0, c0, 0" : "=r" (val));
	return val;
}

static inline void
xscale1pmu_write_pmnc(u32 val)
{
	/* upper 4bits and 7, 11 are write-as-0 */
	val &= 0xffff77f;
	asm volatile("mcr p14, 0, %0, c0, c0, 0" : : "r" (val));
}

static inline int
xscale1_pmnc_counter_has_overflowed(unsigned long pmnc,
					enum xscale_counters counter)
{
	int ret = 0;

	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		ret = pmnc & XSCALE1_CCOUNT_OVERFLOW;
		break;
	case XSCALE_COUNTER0:
		ret = pmnc & XSCALE1_COUNT0_OVERFLOW;
		break;
	case XSCALE_COUNTER1:
		ret = pmnc & XSCALE1_COUNT1_OVERFLOW;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", counter);
	}

	return ret;
}

static irqreturn_t
xscale1pmu_handle_irq(struct arm_pmu *cpu_pmu)
{
	unsigned long pmnc;
	struct perf_sample_data data;
	struct pmu_hw_events *cpuc = this_cpu_ptr(cpu_pmu->hw_events);
	struct pt_regs *regs;
	int idx;

	/*
	 * NOTE: there's an A stepping erratum that states if an overflow
	 *       bit already exists and another occurs, the previous
	 *       Overflow bit gets cleared. There's no workaround.
	 *	 Fixed in B stepping or later.
	 */
	pmnc = xscale1pmu_read_pmnc();

	/*
	 * Write the value back to clear the overflow flags. Overflow
	 * flags remain in pmnc for use below. We also disable the PMU
	 * while we process the interrupt.
	 */
	xscale1pmu_write_pmnc(pmnc & ~XSCALE_PMU_ENABLE);

	if (!(pmnc & XSCALE1_OVERFLOWED_MASK))
		return IRQ_NONE;

	regs = get_irq_regs();

	for_each_set_bit(idx, cpu_pmu->cntr_mask, XSCALE1_NUM_COUNTERS) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		if (!event)
			continue;

		if (!xscale1_pmnc_counter_has_overflowed(pmnc, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event);
		perf_sample_data_init(&data, 0, hwc->last_period);
		if (!armpmu_event_set_period(event))
			continue;

		perf_event_overflow(event, &data, regs);
	}

	irq_work_run();

	/*
	 * Re-enable the PMU.
	 */
	pmnc = xscale1pmu_read_pmnc() | XSCALE_PMU_ENABLE;
	xscale1pmu_write_pmnc(pmnc);

	return IRQ_HANDLED;
}

static void xscale1pmu_enable_event(struct perf_event *event)
{
	unsigned long val, mask, evt;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	switch (idx) {
	case XSCALE_CYCLE_COUNTER:
		mask = 0;
		evt = XSCALE1_CCOUNT_INT_EN;
		break;
	case XSCALE_COUNTER0:
		mask = XSCALE1_COUNT0_EVT_MASK;
		evt = (hwc->config_base << XSCALE1_COUNT0_EVT_SHFT) |
			XSCALE1_COUNT0_INT_EN;
		break;
	case XSCALE_COUNTER1:
		mask = XSCALE1_COUNT1_EVT_MASK;
		evt = (hwc->config_base << XSCALE1_COUNT1_EVT_SHFT) |
			XSCALE1_COUNT1_INT_EN;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	val = xscale1pmu_read_pmnc();
	val &= ~mask;
	val |= evt;
	xscale1pmu_write_pmnc(val);
}

static void xscale1pmu_disable_event(struct perf_event *event)
{
	unsigned long val, mask, evt;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	switch (idx) {
	case XSCALE_CYCLE_COUNTER:
		mask = XSCALE1_CCOUNT_INT_EN;
		evt = 0;
		break;
	case XSCALE_COUNTER0:
		mask = XSCALE1_COUNT0_INT_EN | XSCALE1_COUNT0_EVT_MASK;
		evt = XSCALE_PERFCTR_UNUSED << XSCALE1_COUNT0_EVT_SHFT;
		break;
	case XSCALE_COUNTER1:
		mask = XSCALE1_COUNT1_INT_EN | XSCALE1_COUNT1_EVT_MASK;
		evt = XSCALE_PERFCTR_UNUSED << XSCALE1_COUNT1_EVT_SHFT;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	val = xscale1pmu_read_pmnc();
	val &= ~mask;
	val |= evt;
	xscale1pmu_write_pmnc(val);
}

static int
xscale1pmu_get_event_idx(struct pmu_hw_events *cpuc,
				struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	if (XSCALE_PERFCTR_CCNT == hwc->config_base) {
		if (test_and_set_bit(XSCALE_CYCLE_COUNTER, cpuc->used_mask))
			return -EAGAIN;

		return XSCALE_CYCLE_COUNTER;
	} else {
		if (!test_and_set_bit(XSCALE_COUNTER1, cpuc->used_mask))
			return XSCALE_COUNTER1;

		if (!test_and_set_bit(XSCALE_COUNTER0, cpuc->used_mask))
			return XSCALE_COUNTER0;

		return -EAGAIN;
	}
}

static void xscalepmu_clear_event_idx(struct pmu_hw_events *cpuc,
				     struct perf_event *event)
{
	clear_bit(event->hw.idx, cpuc->used_mask);
}

static void xscale1pmu_start(struct arm_pmu *cpu_pmu)
{
	unsigned long val;

	val = xscale1pmu_read_pmnc();
	val |= XSCALE_PMU_ENABLE;
	xscale1pmu_write_pmnc(val);
}

static void xscale1pmu_stop(struct arm_pmu *cpu_pmu)
{
	unsigned long val;

	val = xscale1pmu_read_pmnc();
	val &= ~XSCALE_PMU_ENABLE;
	xscale1pmu_write_pmnc(val);
}

static inline u64 xscale1pmu_read_counter(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;
	u32 val = 0;

	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		asm volatile("mrc p14, 0, %0, c1, c0, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER0:
		asm volatile("mrc p14, 0, %0, c2, c0, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER1:
		asm volatile("mrc p14, 0, %0, c3, c0, 0" : "=r" (val));
		break;
	}

	return val;
}

static inline void xscale1pmu_write_counter(struct perf_event *event, u64 val)
{
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		asm volatile("mcr p14, 0, %0, c1, c0, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER0:
		asm volatile("mcr p14, 0, %0, c2, c0, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER1:
		asm volatile("mcr p14, 0, %0, c3, c0, 0" : : "r" (val));
		break;
	}
}

static int xscale_map_event(struct perf_event *event)
{
	return armpmu_map_event(event, &xscale_perf_map,
				&xscale_perf_cache_map, 0xFF);
}

static int xscale1pmu_init(struct arm_pmu *cpu_pmu)
{
	cpu_pmu->name		= "armv5_xscale1";
	cpu_pmu->handle_irq	= xscale1pmu_handle_irq;
	cpu_pmu->enable		= xscale1pmu_enable_event;
	cpu_pmu->disable	= xscale1pmu_disable_event;
	cpu_pmu->read_counter	= xscale1pmu_read_counter;
	cpu_pmu->write_counter	= xscale1pmu_write_counter;
	cpu_pmu->get_event_idx	= xscale1pmu_get_event_idx;
	cpu_pmu->clear_event_idx = xscalepmu_clear_event_idx;
	cpu_pmu->start		= xscale1pmu_start;
	cpu_pmu->stop		= xscale1pmu_stop;
	cpu_pmu->map_event	= xscale_map_event;

	bitmap_set(cpu_pmu->cntr_mask, 0, XSCALE1_NUM_COUNTERS);

	return 0;
}

#define XSCALE2_OVERFLOWED_MASK	0x01f
#define XSCALE2_CCOUNT_OVERFLOW	0x001
#define XSCALE2_COUNT0_OVERFLOW	0x002
#define XSCALE2_COUNT1_OVERFLOW	0x004
#define XSCALE2_COUNT2_OVERFLOW	0x008
#define XSCALE2_COUNT3_OVERFLOW	0x010
#define XSCALE2_CCOUNT_INT_EN	0x001
#define XSCALE2_COUNT0_INT_EN	0x002
#define XSCALE2_COUNT1_INT_EN	0x004
#define XSCALE2_COUNT2_INT_EN	0x008
#define XSCALE2_COUNT3_INT_EN	0x010
#define XSCALE2_COUNT0_EVT_SHFT	0
#define XSCALE2_COUNT0_EVT_MASK	(0xff << XSCALE2_COUNT0_EVT_SHFT)
#define XSCALE2_COUNT1_EVT_SHFT	8
#define XSCALE2_COUNT1_EVT_MASK	(0xff << XSCALE2_COUNT1_EVT_SHFT)
#define XSCALE2_COUNT2_EVT_SHFT	16
#define XSCALE2_COUNT2_EVT_MASK	(0xff << XSCALE2_COUNT2_EVT_SHFT)
#define XSCALE2_COUNT3_EVT_SHFT	24
#define XSCALE2_COUNT3_EVT_MASK	(0xff << XSCALE2_COUNT3_EVT_SHFT)

static inline u32
xscale2pmu_read_pmnc(void)
{
	u32 val;
	asm volatile("mrc p14, 0, %0, c0, c1, 0" : "=r" (val));
	/* bits 1-2 and 4-23 are read-unpredictable */
	return val & 0xff000009;
}

static inline void
xscale2pmu_write_pmnc(u32 val)
{
	/* bits 4-23 are write-as-0, 24-31 are write ignored */
	val &= 0xf;
	asm volatile("mcr p14, 0, %0, c0, c1, 0" : : "r" (val));
}

static inline u32
xscale2pmu_read_overflow_flags(void)
{
	u32 val;
	asm volatile("mrc p14, 0, %0, c5, c1, 0" : "=r" (val));
	return val;
}

static inline void
xscale2pmu_write_overflow_flags(u32 val)
{
	asm volatile("mcr p14, 0, %0, c5, c1, 0" : : "r" (val));
}

static inline u32
xscale2pmu_read_event_select(void)
{
	u32 val;
	asm volatile("mrc p14, 0, %0, c8, c1, 0" : "=r" (val));
	return val;
}

static inline void
xscale2pmu_write_event_select(u32 val)
{
	asm volatile("mcr p14, 0, %0, c8, c1, 0" : : "r"(val));
}

static inline u32
xscale2pmu_read_int_enable(void)
{
	u32 val;
	asm volatile("mrc p14, 0, %0, c4, c1, 0" : "=r" (val));
	return val;
}

static void
xscale2pmu_write_int_enable(u32 val)
{
	asm volatile("mcr p14, 0, %0, c4, c1, 0" : : "r" (val));
}

static inline int
xscale2_pmnc_counter_has_overflowed(unsigned long of_flags,
					enum xscale_counters counter)
{
	int ret = 0;

	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		ret = of_flags & XSCALE2_CCOUNT_OVERFLOW;
		break;
	case XSCALE_COUNTER0:
		ret = of_flags & XSCALE2_COUNT0_OVERFLOW;
		break;
	case XSCALE_COUNTER1:
		ret = of_flags & XSCALE2_COUNT1_OVERFLOW;
		break;
	case XSCALE_COUNTER2:
		ret = of_flags & XSCALE2_COUNT2_OVERFLOW;
		break;
	case XSCALE_COUNTER3:
		ret = of_flags & XSCALE2_COUNT3_OVERFLOW;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", counter);
	}

	return ret;
}

static irqreturn_t
xscale2pmu_handle_irq(struct arm_pmu *cpu_pmu)
{
	unsigned long pmnc, of_flags;
	struct perf_sample_data data;
	struct pmu_hw_events *cpuc = this_cpu_ptr(cpu_pmu->hw_events);
	struct pt_regs *regs;
	int idx;

	/* Disable the PMU. */
	pmnc = xscale2pmu_read_pmnc();
	xscale2pmu_write_pmnc(pmnc & ~XSCALE_PMU_ENABLE);

	/* Check the overflow flag register. */
	of_flags = xscale2pmu_read_overflow_flags();
	if (!(of_flags & XSCALE2_OVERFLOWED_MASK))
		return IRQ_NONE;

	/* Clear the overflow bits. */
	xscale2pmu_write_overflow_flags(of_flags);

	regs = get_irq_regs();

	for_each_set_bit(idx, cpu_pmu->cntr_mask, XSCALE2_NUM_COUNTERS) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		if (!event)
			continue;

		if (!xscale2_pmnc_counter_has_overflowed(of_flags, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event);
		perf_sample_data_init(&data, 0, hwc->last_period);
		if (!armpmu_event_set_period(event))
			continue;

		perf_event_overflow(event, &data, regs);
	}

	irq_work_run();

	/*
	 * Re-enable the PMU.
	 */
	pmnc = xscale2pmu_read_pmnc() | XSCALE_PMU_ENABLE;
	xscale2pmu_write_pmnc(pmnc);

	return IRQ_HANDLED;
}

static void xscale2pmu_enable_event(struct perf_event *event)
{
	unsigned long ien, evtsel;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	ien = xscale2pmu_read_int_enable();
	evtsel = xscale2pmu_read_event_select();

	switch (idx) {
	case XSCALE_CYCLE_COUNTER:
		ien |= XSCALE2_CCOUNT_INT_EN;
		break;
	case XSCALE_COUNTER0:
		ien |= XSCALE2_COUNT0_INT_EN;
		evtsel &= ~XSCALE2_COUNT0_EVT_MASK;
		evtsel |= hwc->config_base << XSCALE2_COUNT0_EVT_SHFT;
		break;
	case XSCALE_COUNTER1:
		ien |= XSCALE2_COUNT1_INT_EN;
		evtsel &= ~XSCALE2_COUNT1_EVT_MASK;
		evtsel |= hwc->config_base << XSCALE2_COUNT1_EVT_SHFT;
		break;
	case XSCALE_COUNTER2:
		ien |= XSCALE2_COUNT2_INT_EN;
		evtsel &= ~XSCALE2_COUNT2_EVT_MASK;
		evtsel |= hwc->config_base << XSCALE2_COUNT2_EVT_SHFT;
		break;
	case XSCALE_COUNTER3:
		ien |= XSCALE2_COUNT3_INT_EN;
		evtsel &= ~XSCALE2_COUNT3_EVT_MASK;
		evtsel |= hwc->config_base << XSCALE2_COUNT3_EVT_SHFT;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	xscale2pmu_write_event_select(evtsel);
	xscale2pmu_write_int_enable(ien);
}

static void xscale2pmu_disable_event(struct perf_event *event)
{
	unsigned long ien, evtsel, of_flags;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	ien = xscale2pmu_read_int_enable();
	evtsel = xscale2pmu_read_event_select();

	switch (idx) {
	case XSCALE_CYCLE_COUNTER:
		ien &= ~XSCALE2_CCOUNT_INT_EN;
		of_flags = XSCALE2_CCOUNT_OVERFLOW;
		break;
	case XSCALE_COUNTER0:
		ien &= ~XSCALE2_COUNT0_INT_EN;
		evtsel &= ~XSCALE2_COUNT0_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT0_EVT_SHFT;
		of_flags = XSCALE2_COUNT0_OVERFLOW;
		break;
	case XSCALE_COUNTER1:
		ien &= ~XSCALE2_COUNT1_INT_EN;
		evtsel &= ~XSCALE2_COUNT1_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT1_EVT_SHFT;
		of_flags = XSCALE2_COUNT1_OVERFLOW;
		break;
	case XSCALE_COUNTER2:
		ien &= ~XSCALE2_COUNT2_INT_EN;
		evtsel &= ~XSCALE2_COUNT2_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT2_EVT_SHFT;
		of_flags = XSCALE2_COUNT2_OVERFLOW;
		break;
	case XSCALE_COUNTER3:
		ien &= ~XSCALE2_COUNT3_INT_EN;
		evtsel &= ~XSCALE2_COUNT3_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT3_EVT_SHFT;
		of_flags = XSCALE2_COUNT3_OVERFLOW;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	xscale2pmu_write_event_select(evtsel);
	xscale2pmu_write_int_enable(ien);
	xscale2pmu_write_overflow_flags(of_flags);
}

static int
xscale2pmu_get_event_idx(struct pmu_hw_events *cpuc,
				struct perf_event *event)
{
	int idx = xscale1pmu_get_event_idx(cpuc, event);
	if (idx >= 0)
		goto out;

	if (!test_and_set_bit(XSCALE_COUNTER3, cpuc->used_mask))
		idx = XSCALE_COUNTER3;
	else if (!test_and_set_bit(XSCALE_COUNTER2, cpuc->used_mask))
		idx = XSCALE_COUNTER2;
out:
	return idx;
}

static void xscale2pmu_start(struct arm_pmu *cpu_pmu)
{
	unsigned long val;

	val = xscale2pmu_read_pmnc() & ~XSCALE_PMU_CNT64;
	val |= XSCALE_PMU_ENABLE;
	xscale2pmu_write_pmnc(val);
}

static void xscale2pmu_stop(struct arm_pmu *cpu_pmu)
{
	unsigned long val;

	val = xscale2pmu_read_pmnc();
	val &= ~XSCALE_PMU_ENABLE;
	xscale2pmu_write_pmnc(val);
}

static inline u64 xscale2pmu_read_counter(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;
	u32 val = 0;

	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		asm volatile("mrc p14, 0, %0, c1, c1, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER0:
		asm volatile("mrc p14, 0, %0, c0, c2, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER1:
		asm volatile("mrc p14, 0, %0, c1, c2, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER2:
		asm volatile("mrc p14, 0, %0, c2, c2, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER3:
		asm volatile("mrc p14, 0, %0, c3, c2, 0" : "=r" (val));
		break;
	}

	return val;
}

static inline void xscale2pmu_write_counter(struct perf_event *event, u64 val)
{
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		asm volatile("mcr p14, 0, %0, c1, c1, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER0:
		asm volatile("mcr p14, 0, %0, c0, c2, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER1:
		asm volatile("mcr p14, 0, %0, c1, c2, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER2:
		asm volatile("mcr p14, 0, %0, c2, c2, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER3:
		asm volatile("mcr p14, 0, %0, c3, c2, 0" : : "r" (val));
		break;
	}
}

static int xscale2pmu_init(struct arm_pmu *cpu_pmu)
{
	cpu_pmu->name		= "armv5_xscale2";
	cpu_pmu->handle_irq	= xscale2pmu_handle_irq;
	cpu_pmu->enable		= xscale2pmu_enable_event;
	cpu_pmu->disable	= xscale2pmu_disable_event;
	cpu_pmu->read_counter	= xscale2pmu_read_counter;
	cpu_pmu->write_counter	= xscale2pmu_write_counter;
	cpu_pmu->get_event_idx	= xscale2pmu_get_event_idx;
	cpu_pmu->clear_event_idx = xscalepmu_clear_event_idx;
	cpu_pmu->start		= xscale2pmu_start;
	cpu_pmu->stop		= xscale2pmu_stop;
	cpu_pmu->map_event	= xscale_map_event;

	bitmap_set(cpu_pmu->cntr_mask, 0, XSCALE2_NUM_COUNTERS);

	return 0;
}

static const struct pmu_probe_info xscale_pmu_probe_table[] = {
	XSCALE_PMU_PROBE(ARM_CPU_XSCALE_ARCH_V1, xscale1pmu_init),
	XSCALE_PMU_PROBE(ARM_CPU_XSCALE_ARCH_V2, xscale2pmu_init),
	{ /* sentinel value */ }
};

static int xscale_pmu_device_probe(struct platform_device *pdev)
{
	return arm_pmu_device_probe(pdev, NULL, xscale_pmu_probe_table);
}

static struct platform_driver xscale_pmu_driver = {
	.driver		= {
		.name	= "xscale-pmu",
	},
	.probe		= xscale_pmu_device_probe,
};

builtin_platform_driver(xscale_pmu_driver);
