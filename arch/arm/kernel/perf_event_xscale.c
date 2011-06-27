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

#ifdef CONFIG_CPU_XSCALE
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
	XSCALE_CYCLE_COUNTER	= 1,
	XSCALE_COUNTER0,
	XSCALE_COUNTER1,
	XSCALE_COUNTER2,
	XSCALE_COUNTER3,
};

static const unsigned xscale_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = XSCALE_PERFCTR_CCNT,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = XSCALE_PERFCTR_INSTRUCTION,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES]	    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = XSCALE_PERFCTR_BRANCH,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = XSCALE_PERFCTR_BRANCH_MISS,
	[PERF_COUNT_HW_BUS_CYCLES]	    = HW_OP_UNSUPPORTED,
};

static const unsigned xscale_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					   [PERF_COUNT_HW_CACHE_OP_MAX]
					   [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= XSCALE_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_DCACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= XSCALE_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_DCACHE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_ICACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_ICACHE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_DTLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_DTLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_ITLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
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
xscale1pmu_handle_irq(int irq_num, void *dev)
{
	unsigned long pmnc;
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
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

	perf_sample_data_init(&data, 0);

	cpuc = &__get_cpu_var(cpu_hw_events);
	for (idx = 0; idx <= armpmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		if (!xscale1_pmnc_counter_has_overflowed(pmnc, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event, hwc, idx, 1);
		data.period = event->hw.last_period;
		if (!armpmu_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, &data, regs))
			armpmu->disable(hwc, idx);
	}

	irq_work_run();

	/*
	 * Re-enable the PMU.
	 */
	pmnc = xscale1pmu_read_pmnc() | XSCALE_PMU_ENABLE;
	xscale1pmu_write_pmnc(pmnc);

	return IRQ_HANDLED;
}

static void
xscale1pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long val, mask, evt, flags;

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

	raw_spin_lock_irqsave(&pmu_lock, flags);
	val = xscale1pmu_read_pmnc();
	val &= ~mask;
	val |= evt;
	xscale1pmu_write_pmnc(val);
	raw_spin_unlock_irqrestore(&pmu_lock, flags);
}

static void
xscale1pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long val, mask, evt, flags;

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

	raw_spin_lock_irqsave(&pmu_lock, flags);
	val = xscale1pmu_read_pmnc();
	val &= ~mask;
	val |= evt;
	xscale1pmu_write_pmnc(val);
	raw_spin_unlock_irqrestore(&pmu_lock, flags);
}

static int
xscale1pmu_get_event_idx(struct cpu_hw_events *cpuc,
			struct hw_perf_event *event)
{
	if (XSCALE_PERFCTR_CCNT == event->config_base) {
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

static void
xscale1pmu_start(void)
{
	unsigned long flags, val;

	raw_spin_lock_irqsave(&pmu_lock, flags);
	val = xscale1pmu_read_pmnc();
	val |= XSCALE_PMU_ENABLE;
	xscale1pmu_write_pmnc(val);
	raw_spin_unlock_irqrestore(&pmu_lock, flags);
}

static void
xscale1pmu_stop(void)
{
	unsigned long flags, val;

	raw_spin_lock_irqsave(&pmu_lock, flags);
	val = xscale1pmu_read_pmnc();
	val &= ~XSCALE_PMU_ENABLE;
	xscale1pmu_write_pmnc(val);
	raw_spin_unlock_irqrestore(&pmu_lock, flags);
}

static inline u32
xscale1pmu_read_counter(int counter)
{
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

static inline void
xscale1pmu_write_counter(int counter, u32 val)
{
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

static const struct arm_pmu xscale1pmu = {
	.id		= ARM_PERF_PMU_ID_XSCALE1,
	.name		= "xscale1",
	.handle_irq	= xscale1pmu_handle_irq,
	.enable		= xscale1pmu_enable_event,
	.disable	= xscale1pmu_disable_event,
	.read_counter	= xscale1pmu_read_counter,
	.write_counter	= xscale1pmu_write_counter,
	.get_event_idx	= xscale1pmu_get_event_idx,
	.start		= xscale1pmu_start,
	.stop		= xscale1pmu_stop,
	.cache_map	= &xscale_perf_cache_map,
	.event_map	= &xscale_perf_map,
	.raw_event_mask	= 0xFF,
	.num_events	= 3,
	.max_period	= (1LLU << 32) - 1,
};

static const struct arm_pmu *__init xscale1pmu_init(void)
{
	return &xscale1pmu;
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
xscale2pmu_handle_irq(int irq_num, void *dev)
{
	unsigned long pmnc, of_flags;
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
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

	perf_sample_data_init(&data, 0);

	cpuc = &__get_cpu_var(cpu_hw_events);
	for (idx = 0; idx <= armpmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		if (!xscale2_pmnc_counter_has_overflowed(pmnc, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event, hwc, idx, 1);
		data.period = event->hw.last_period;
		if (!armpmu_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, &data, regs))
			armpmu->disable(hwc, idx);
	}

	irq_work_run();

	/*
	 * Re-enable the PMU.
	 */
	pmnc = xscale2pmu_read_pmnc() | XSCALE_PMU_ENABLE;
	xscale2pmu_write_pmnc(pmnc);

	return IRQ_HANDLED;
}

static void
xscale2pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags, ien, evtsel;

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

	raw_spin_lock_irqsave(&pmu_lock, flags);
	xscale2pmu_write_event_select(evtsel);
	xscale2pmu_write_int_enable(ien);
	raw_spin_unlock_irqrestore(&pmu_lock, flags);
}

static void
xscale2pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags, ien, evtsel;

	ien = xscale2pmu_read_int_enable();
	evtsel = xscale2pmu_read_event_select();

	switch (idx) {
	case XSCALE_CYCLE_COUNTER:
		ien &= ~XSCALE2_CCOUNT_INT_EN;
		break;
	case XSCALE_COUNTER0:
		ien &= ~XSCALE2_COUNT0_INT_EN;
		evtsel &= ~XSCALE2_COUNT0_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT0_EVT_SHFT;
		break;
	case XSCALE_COUNTER1:
		ien &= ~XSCALE2_COUNT1_INT_EN;
		evtsel &= ~XSCALE2_COUNT1_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT1_EVT_SHFT;
		break;
	case XSCALE_COUNTER2:
		ien &= ~XSCALE2_COUNT2_INT_EN;
		evtsel &= ~XSCALE2_COUNT2_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT2_EVT_SHFT;
		break;
	case XSCALE_COUNTER3:
		ien &= ~XSCALE2_COUNT3_INT_EN;
		evtsel &= ~XSCALE2_COUNT3_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT3_EVT_SHFT;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	raw_spin_lock_irqsave(&pmu_lock, flags);
	xscale2pmu_write_event_select(evtsel);
	xscale2pmu_write_int_enable(ien);
	raw_spin_unlock_irqrestore(&pmu_lock, flags);
}

static int
xscale2pmu_get_event_idx(struct cpu_hw_events *cpuc,
			struct hw_perf_event *event)
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

static void
xscale2pmu_start(void)
{
	unsigned long flags, val;

	raw_spin_lock_irqsave(&pmu_lock, flags);
	val = xscale2pmu_read_pmnc() & ~XSCALE_PMU_CNT64;
	val |= XSCALE_PMU_ENABLE;
	xscale2pmu_write_pmnc(val);
	raw_spin_unlock_irqrestore(&pmu_lock, flags);
}

static void
xscale2pmu_stop(void)
{
	unsigned long flags, val;

	raw_spin_lock_irqsave(&pmu_lock, flags);
	val = xscale2pmu_read_pmnc();
	val &= ~XSCALE_PMU_ENABLE;
	xscale2pmu_write_pmnc(val);
	raw_spin_unlock_irqrestore(&pmu_lock, flags);
}

static inline u32
xscale2pmu_read_counter(int counter)
{
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

static inline void
xscale2pmu_write_counter(int counter, u32 val)
{
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

static const struct arm_pmu xscale2pmu = {
	.id		= ARM_PERF_PMU_ID_XSCALE2,
	.name		= "xscale2",
	.handle_irq	= xscale2pmu_handle_irq,
	.enable		= xscale2pmu_enable_event,
	.disable	= xscale2pmu_disable_event,
	.read_counter	= xscale2pmu_read_counter,
	.write_counter	= xscale2pmu_write_counter,
	.get_event_idx	= xscale2pmu_get_event_idx,
	.start		= xscale2pmu_start,
	.stop		= xscale2pmu_stop,
	.cache_map	= &xscale_perf_cache_map,
	.event_map	= &xscale_perf_map,
	.raw_event_mask	= 0xFF,
	.num_events	= 5,
	.max_period	= (1LLU << 32) - 1,
};

static const struct arm_pmu *__init xscale2pmu_init(void)
{
	return &xscale2pmu;
}
#else
static const struct arm_pmu *__init xscale1pmu_init(void)
{
	return NULL;
}

static const struct arm_pmu *__init xscale2pmu_init(void)
{
	return NULL;
}
#endif	/* CONFIG_CPU_XSCALE */
