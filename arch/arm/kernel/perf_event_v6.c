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

#ifdef CONFIG_CPU_V6
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
	ARMV6_CYCLE_COUNTER = 1,
	ARMV6_COUNTER0,
	ARMV6_COUNTER1,
};

/*
 * The hardware events that we support. We do support cache operations but
 * we have harvard caches and no way to combine instruction and data
 * accesses/misses in hardware.
 */
static const unsigned armv6_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV6_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = ARMV6_PERFCTR_INSTR_EXEC,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES]	    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = ARMV6_PERFCTR_BR_EXEC,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV6_PERFCTR_BR_MISPREDICT,
	[PERF_COUNT_HW_BUS_CYCLES]	    = HW_OP_UNSUPPORTED,
};

static const unsigned armv6_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					  [PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		/*
		 * The performance counters don't differentiate between read
		 * and write accesses/misses so this isn't strictly correct,
		 * but it's the best we can do. Writes and reads get
		 * combined.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV6_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_DCACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV6_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_DCACHE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_ICACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_ICACHE_MISS,
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
		/*
		 * The ARM performance counters can count micro DTLB misses,
		 * micro ITLB misses and main TLB misses. There isn't an event
		 * for TLB misses, so use the micro misses here and if users
		 * want the main TLB misses they can use a raw counter.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_DTLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_DTLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_ITLB_MISS,
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

enum armv6mpcore_perf_types {
	ARMV6MPCORE_PERFCTR_ICACHE_MISS	    = 0x0,
	ARMV6MPCORE_PERFCTR_IBUF_STALL	    = 0x1,
	ARMV6MPCORE_PERFCTR_DDEP_STALL	    = 0x2,
	ARMV6MPCORE_PERFCTR_ITLB_MISS	    = 0x3,
	ARMV6MPCORE_PERFCTR_DTLB_MISS	    = 0x4,
	ARMV6MPCORE_PERFCTR_BR_EXEC	    = 0x5,
	ARMV6MPCORE_PERFCTR_BR_NOTPREDICT   = 0x6,
	ARMV6MPCORE_PERFCTR_BR_MISPREDICT   = 0x7,
	ARMV6MPCORE_PERFCTR_INSTR_EXEC	    = 0x8,
	ARMV6MPCORE_PERFCTR_DCACHE_RDACCESS = 0xA,
	ARMV6MPCORE_PERFCTR_DCACHE_RDMISS   = 0xB,
	ARMV6MPCORE_PERFCTR_DCACHE_WRACCESS = 0xC,
	ARMV6MPCORE_PERFCTR_DCACHE_WRMISS   = 0xD,
	ARMV6MPCORE_PERFCTR_DCACHE_EVICTION = 0xE,
	ARMV6MPCORE_PERFCTR_SW_PC_CHANGE    = 0xF,
	ARMV6MPCORE_PERFCTR_MAIN_TLB_MISS   = 0x10,
	ARMV6MPCORE_PERFCTR_EXPL_MEM_ACCESS = 0x11,
	ARMV6MPCORE_PERFCTR_LSU_FULL_STALL  = 0x12,
	ARMV6MPCORE_PERFCTR_WBUF_DRAINED    = 0x13,
	ARMV6MPCORE_PERFCTR_CPU_CYCLES	    = 0xFF,
};

/*
 * The hardware events that we support. We do support cache operations but
 * we have harvard caches and no way to combine instruction and data
 * accesses/misses in hardware.
 */
static const unsigned armv6mpcore_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV6MPCORE_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = ARMV6MPCORE_PERFCTR_INSTR_EXEC,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES]	    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = ARMV6MPCORE_PERFCTR_BR_EXEC,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV6MPCORE_PERFCTR_BR_MISPREDICT,
	[PERF_COUNT_HW_BUS_CYCLES]	    = HW_OP_UNSUPPORTED,
};

static const unsigned armv6mpcore_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  =
				ARMV6MPCORE_PERFCTR_DCACHE_RDACCESS,
			[C(RESULT_MISS)]    =
				ARMV6MPCORE_PERFCTR_DCACHE_RDMISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  =
				ARMV6MPCORE_PERFCTR_DCACHE_WRACCESS,
			[C(RESULT_MISS)]    =
				ARMV6MPCORE_PERFCTR_DCACHE_WRMISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_ICACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_ICACHE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		/*
		 * The ARM performance counters can count micro DTLB misses,
		 * micro ITLB misses and main TLB misses. There isn't an event
		 * for TLB misses, so use the micro misses here and if users
		 * want the main TLB misses they can use a raw counter.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_DTLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_DTLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_ITLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
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

static inline u32
armv6pmu_read_counter(int counter)
{
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

static inline void
armv6pmu_write_counter(int counter,
		       u32 value)
{
	if (ARMV6_CYCLE_COUNTER == counter)
		asm volatile("mcr   p15, 0, %0, c15, c12, 1" : : "r"(value));
	else if (ARMV6_COUNTER0 == counter)
		asm volatile("mcr   p15, 0, %0, c15, c12, 2" : : "r"(value));
	else if (ARMV6_COUNTER1 == counter)
		asm volatile("mcr   p15, 0, %0, c15, c12, 3" : : "r"(value));
	else
		WARN_ONCE(1, "invalid counter number (%d)\n", counter);
}

static void
armv6pmu_enable_event(struct hw_perf_event *hwc,
		      int idx)
{
	unsigned long val, mask, evt, flags;

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
	spin_lock_irqsave(&pmu_lock, flags);
	val = armv6_pmcr_read();
	val &= ~mask;
	val |= evt;
	armv6_pmcr_write(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static irqreturn_t
armv6pmu_handle_irq(int irq_num,
		    void *dev)
{
	unsigned long pmcr = armv6_pmcr_read();
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
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

	perf_sample_data_init(&data, 0);

	cpuc = &__get_cpu_var(cpu_hw_events);
	for (idx = 0; idx <= armpmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		/*
		 * We have a single interrupt for all counters. Check that
		 * each counter has overflowed before we process it.
		 */
		if (!armv6_pmcr_counter_has_overflowed(pmcr, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event, hwc, idx);
		data.period = event->hw.last_period;
		if (!armpmu_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, 0, &data, regs))
			armpmu->disable(hwc, idx);
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

static void
armv6pmu_start(void)
{
	unsigned long flags, val;

	spin_lock_irqsave(&pmu_lock, flags);
	val = armv6_pmcr_read();
	val |= ARMV6_PMCR_ENABLE;
	armv6_pmcr_write(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void
armv6pmu_stop(void)
{
	unsigned long flags, val;

	spin_lock_irqsave(&pmu_lock, flags);
	val = armv6_pmcr_read();
	val &= ~ARMV6_PMCR_ENABLE;
	armv6_pmcr_write(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static int
armv6pmu_get_event_idx(struct cpu_hw_events *cpuc,
		       struct hw_perf_event *event)
{
	/* Always place a cycle counter into the cycle counter. */
	if (ARMV6_PERFCTR_CPU_CYCLES == event->config_base) {
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

static void
armv6pmu_disable_event(struct hw_perf_event *hwc,
		       int idx)
{
	unsigned long val, mask, evt, flags;

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
	spin_lock_irqsave(&pmu_lock, flags);
	val = armv6_pmcr_read();
	val &= ~mask;
	val |= evt;
	armv6_pmcr_write(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void
armv6mpcore_pmu_disable_event(struct hw_perf_event *hwc,
			      int idx)
{
	unsigned long val, mask, flags, evt = 0;

	if (ARMV6_CYCLE_COUNTER == idx) {
		mask	= ARMV6_PMCR_CCOUNT_IEN;
	} else if (ARMV6_COUNTER0 == idx) {
		mask	= ARMV6_PMCR_COUNT0_IEN;
	} else if (ARMV6_COUNTER1 == idx) {
		mask	= ARMV6_PMCR_COUNT1_IEN;
	} else {
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	/*
	 * Unlike UP ARMv6, we don't have a way of stopping the counters. We
	 * simply disable the interrupt reporting.
	 */
	spin_lock_irqsave(&pmu_lock, flags);
	val = armv6_pmcr_read();
	val &= ~mask;
	val |= evt;
	armv6_pmcr_write(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static const struct arm_pmu armv6pmu = {
	.id			= ARM_PERF_PMU_ID_V6,
	.name			= "v6",
	.handle_irq		= armv6pmu_handle_irq,
	.enable			= armv6pmu_enable_event,
	.disable		= armv6pmu_disable_event,
	.read_counter		= armv6pmu_read_counter,
	.write_counter		= armv6pmu_write_counter,
	.get_event_idx		= armv6pmu_get_event_idx,
	.start			= armv6pmu_start,
	.stop			= armv6pmu_stop,
	.cache_map		= &armv6_perf_cache_map,
	.event_map		= &armv6_perf_map,
	.raw_event_mask		= 0xFF,
	.num_events		= 3,
	.max_period		= (1LLU << 32) - 1,
};

static const struct arm_pmu *__init armv6pmu_init(void)
{
	return &armv6pmu;
}

/*
 * ARMv6mpcore is almost identical to single core ARMv6 with the exception
 * that some of the events have different enumerations and that there is no
 * *hack* to stop the programmable counters. To stop the counters we simply
 * disable the interrupt reporting and update the event. When unthrottling we
 * reset the period and enable the interrupt reporting.
 */
static const struct arm_pmu armv6mpcore_pmu = {
	.id			= ARM_PERF_PMU_ID_V6MP,
	.name			= "v6mpcore",
	.handle_irq		= armv6pmu_handle_irq,
	.enable			= armv6pmu_enable_event,
	.disable		= armv6mpcore_pmu_disable_event,
	.read_counter		= armv6pmu_read_counter,
	.write_counter		= armv6pmu_write_counter,
	.get_event_idx		= armv6pmu_get_event_idx,
	.start			= armv6pmu_start,
	.stop			= armv6pmu_stop,
	.cache_map		= &armv6mpcore_perf_cache_map,
	.event_map		= &armv6mpcore_perf_map,
	.raw_event_mask		= 0xFF,
	.num_events		= 3,
	.max_period		= (1LLU << 32) - 1,
};

static const struct arm_pmu *__init armv6mpcore_pmu_init(void)
{
	return &armv6mpcore_pmu;
}
#else
static const struct arm_pmu *__init armv6pmu_init(void)
{
	return NULL;
}

static const struct arm_pmu *__init armv6mpcore_pmu_init(void)
{
	return NULL;
}
#endif	/* CONFIG_CPU_V6 */
