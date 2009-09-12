/*
 * Performance counter x86 architecture code
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2009 Jaswinder Singh Rajput
 *  Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 *  Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/perf_counter.h>
#include <linux/capability.h>
#include <linux/notifier.h>
#include <linux/hardirq.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/kdebug.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>

#include <asm/apic.h>
#include <asm/stacktrace.h>
#include <asm/nmi.h>

static u64 perf_counter_mask __read_mostly;

struct cpu_hw_counters {
	struct perf_counter	*counters[X86_PMC_IDX_MAX];
	unsigned long		used_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	unsigned long		active_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	unsigned long		interrupts;
	int			enabled;
};

/*
 * struct x86_pmu - generic x86 pmu
 */
struct x86_pmu {
	const char	*name;
	int		version;
	int		(*handle_irq)(struct pt_regs *);
	void		(*disable_all)(void);
	void		(*enable_all)(void);
	void		(*enable)(struct hw_perf_counter *, int);
	void		(*disable)(struct hw_perf_counter *, int);
	unsigned	eventsel;
	unsigned	perfctr;
	u64		(*event_map)(int);
	u64		(*raw_event)(u64);
	int		max_events;
	int		num_counters;
	int		num_counters_fixed;
	int		counter_bits;
	u64		counter_mask;
	u64		max_period;
	u64		intel_ctrl;
};

static struct x86_pmu x86_pmu __read_mostly;

static DEFINE_PER_CPU(struct cpu_hw_counters, cpu_hw_counters) = {
	.enabled = 1,
};

/*
 * Intel PerfMon v3. Used on Core2 and later.
 */
static const u64 intel_perfmon_event_map[] =
{
  [PERF_COUNT_HW_CPU_CYCLES]		= 0x003c,
  [PERF_COUNT_HW_INSTRUCTIONS]		= 0x00c0,
  [PERF_COUNT_HW_CACHE_REFERENCES]	= 0x4f2e,
  [PERF_COUNT_HW_CACHE_MISSES]		= 0x412e,
  [PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x00c4,
  [PERF_COUNT_HW_BRANCH_MISSES]		= 0x00c5,
  [PERF_COUNT_HW_BUS_CYCLES]		= 0x013c,
};

static u64 intel_pmu_event_map(int event)
{
	return intel_perfmon_event_map[event];
}

/*
 * Generalized hw caching related event table, filled
 * in on a per model basis. A value of 0 means
 * 'not supported', -1 means 'event makes no sense on
 * this CPU', any other value means the raw event
 * ID.
 */

#define C(x) PERF_COUNT_HW_CACHE_##x

static u64 __read_mostly hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX];

static const u64 nehalem_hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX] =
{
 [ C(L1D) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0f40, /* L1D_CACHE_LD.MESI            */
		[ C(RESULT_MISS)   ] = 0x0140, /* L1D_CACHE_LD.I_STATE         */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x0f41, /* L1D_CACHE_ST.MESI            */
		[ C(RESULT_MISS)   ] = 0x0141, /* L1D_CACHE_ST.I_STATE         */
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0x014e, /* L1D_PREFETCH.REQUESTS        */
		[ C(RESULT_MISS)   ] = 0x024e, /* L1D_PREFETCH.MISS            */
	},
 },
 [ C(L1I ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0380, /* L1I.READS                    */
		[ C(RESULT_MISS)   ] = 0x0280, /* L1I.MISSES                   */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0x0,
		[ C(RESULT_MISS)   ] = 0x0,
	},
 },
 [ C(LL  ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0324, /* L2_RQSTS.LOADS               */
		[ C(RESULT_MISS)   ] = 0x0224, /* L2_RQSTS.LD_MISS             */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x0c24, /* L2_RQSTS.RFOS                */
		[ C(RESULT_MISS)   ] = 0x0824, /* L2_RQSTS.RFO_MISS            */
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0x4f2e, /* LLC Reference                */
		[ C(RESULT_MISS)   ] = 0x412e, /* LLC Misses                   */
	},
 },
 [ C(DTLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0f40, /* L1D_CACHE_LD.MESI   (alias)  */
		[ C(RESULT_MISS)   ] = 0x0108, /* DTLB_LOAD_MISSES.ANY         */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x0f41, /* L1D_CACHE_ST.MESI   (alias)  */
		[ C(RESULT_MISS)   ] = 0x010c, /* MEM_STORE_RETIRED.DTLB_MISS  */
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0x0,
		[ C(RESULT_MISS)   ] = 0x0,
	},
 },
 [ C(ITLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x01c0, /* INST_RETIRED.ANY_P           */
		[ C(RESULT_MISS)   ] = 0x20c8, /* ITLB_MISS_RETIRED            */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
 [ C(BPU ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x00c4, /* BR_INST_RETIRED.ALL_BRANCHES */
		[ C(RESULT_MISS)   ] = 0x03e8, /* BPU_CLEARS.ANY               */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
};

static const u64 core2_hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX] =
{
 [ C(L1D) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0f40, /* L1D_CACHE_LD.MESI          */
		[ C(RESULT_MISS)   ] = 0x0140, /* L1D_CACHE_LD.I_STATE       */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x0f41, /* L1D_CACHE_ST.MESI          */
		[ C(RESULT_MISS)   ] = 0x0141, /* L1D_CACHE_ST.I_STATE       */
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0x104e, /* L1D_PREFETCH.REQUESTS      */
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(L1I ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0080, /* L1I.READS                  */
		[ C(RESULT_MISS)   ] = 0x0081, /* L1I.MISSES                 */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(LL  ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x4f29, /* L2_LD.MESI                 */
		[ C(RESULT_MISS)   ] = 0x4129, /* L2_LD.ISTATE               */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x4f2A, /* L2_ST.MESI                 */
		[ C(RESULT_MISS)   ] = 0x412A, /* L2_ST.ISTATE               */
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(DTLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0f40, /* L1D_CACHE_LD.MESI  (alias) */
		[ C(RESULT_MISS)   ] = 0x0208, /* DTLB_MISSES.MISS_LD        */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x0f41, /* L1D_CACHE_ST.MESI  (alias) */
		[ C(RESULT_MISS)   ] = 0x0808, /* DTLB_MISSES.MISS_ST        */
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(ITLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x00c0, /* INST_RETIRED.ANY_P         */
		[ C(RESULT_MISS)   ] = 0x1282, /* ITLBMISSES                 */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
 [ C(BPU ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x00c4, /* BR_INST_RETIRED.ANY        */
		[ C(RESULT_MISS)   ] = 0x00c5, /* BP_INST_RETIRED.MISPRED    */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
};

static const u64 atom_hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX] =
{
 [ C(L1D) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x2140, /* L1D_CACHE.LD               */
		[ C(RESULT_MISS)   ] = 0,
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x2240, /* L1D_CACHE.ST               */
		[ C(RESULT_MISS)   ] = 0,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0x0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(L1I ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0380, /* L1I.READS                  */
		[ C(RESULT_MISS)   ] = 0x0280, /* L1I.MISSES                 */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(LL  ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x4f29, /* L2_LD.MESI                 */
		[ C(RESULT_MISS)   ] = 0x4129, /* L2_LD.ISTATE               */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x4f2A, /* L2_ST.MESI                 */
		[ C(RESULT_MISS)   ] = 0x412A, /* L2_ST.ISTATE               */
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(DTLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x2140, /* L1D_CACHE_LD.MESI  (alias) */
		[ C(RESULT_MISS)   ] = 0x0508, /* DTLB_MISSES.MISS_LD        */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x2240, /* L1D_CACHE_ST.MESI  (alias) */
		[ C(RESULT_MISS)   ] = 0x0608, /* DTLB_MISSES.MISS_ST        */
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(ITLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x00c0, /* INST_RETIRED.ANY_P         */
		[ C(RESULT_MISS)   ] = 0x0282, /* ITLB.MISSES                */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
 [ C(BPU ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x00c4, /* BR_INST_RETIRED.ANY        */
		[ C(RESULT_MISS)   ] = 0x00c5, /* BP_INST_RETIRED.MISPRED    */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
};

static u64 intel_pmu_raw_event(u64 event)
{
#define CORE_EVNTSEL_EVENT_MASK		0x000000FFULL
#define CORE_EVNTSEL_UNIT_MASK		0x0000FF00ULL
#define CORE_EVNTSEL_EDGE_MASK		0x00040000ULL
#define CORE_EVNTSEL_INV_MASK		0x00800000ULL
#define CORE_EVNTSEL_COUNTER_MASK	0xFF000000ULL

#define CORE_EVNTSEL_MASK		\
	(CORE_EVNTSEL_EVENT_MASK |	\
	 CORE_EVNTSEL_UNIT_MASK  |	\
	 CORE_EVNTSEL_EDGE_MASK  |	\
	 CORE_EVNTSEL_INV_MASK  |	\
	 CORE_EVNTSEL_COUNTER_MASK)

	return event & CORE_EVNTSEL_MASK;
}

static const u64 amd_hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX] =
{
 [ C(L1D) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0040, /* Data Cache Accesses        */
		[ C(RESULT_MISS)   ] = 0x0041, /* Data Cache Misses          */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x0142, /* Data Cache Refills :system */
		[ C(RESULT_MISS)   ] = 0,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0x0267, /* Data Prefetcher :attempts  */
		[ C(RESULT_MISS)   ] = 0x0167, /* Data Prefetcher :cancelled */
	},
 },
 [ C(L1I ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0080, /* Instruction cache fetches  */
		[ C(RESULT_MISS)   ] = 0x0081, /* Instruction cache misses   */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0x014B, /* Prefetch Instructions :Load */
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(LL  ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x037D, /* Requests to L2 Cache :IC+DC */
		[ C(RESULT_MISS)   ] = 0x037E, /* L2 Cache Misses : IC+DC     */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x017F, /* L2 Fill/Writeback           */
		[ C(RESULT_MISS)   ] = 0,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(DTLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0040, /* Data Cache Accesses        */
		[ C(RESULT_MISS)   ] = 0x0046, /* L1 DTLB and L2 DLTB Miss   */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(ITLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0080, /* Instruction fecthes        */
		[ C(RESULT_MISS)   ] = 0x0085, /* Instr. fetch ITLB misses   */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
 [ C(BPU ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x00c2, /* Retired Branch Instr.      */
		[ C(RESULT_MISS)   ] = 0x00c3, /* Retired Mispredicted BI    */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
};

/*
 * AMD Performance Monitor K7 and later.
 */
static const u64 amd_perfmon_event_map[] =
{
  [PERF_COUNT_HW_CPU_CYCLES]		= 0x0076,
  [PERF_COUNT_HW_INSTRUCTIONS]		= 0x00c0,
  [PERF_COUNT_HW_CACHE_REFERENCES]	= 0x0080,
  [PERF_COUNT_HW_CACHE_MISSES]		= 0x0081,
  [PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x00c4,
  [PERF_COUNT_HW_BRANCH_MISSES]		= 0x00c5,
};

static u64 amd_pmu_event_map(int event)
{
	return amd_perfmon_event_map[event];
}

static u64 amd_pmu_raw_event(u64 event)
{
#define K7_EVNTSEL_EVENT_MASK	0x7000000FFULL
#define K7_EVNTSEL_UNIT_MASK	0x00000FF00ULL
#define K7_EVNTSEL_EDGE_MASK	0x000040000ULL
#define K7_EVNTSEL_INV_MASK	0x000800000ULL
#define K7_EVNTSEL_COUNTER_MASK	0x0FF000000ULL

#define K7_EVNTSEL_MASK			\
	(K7_EVNTSEL_EVENT_MASK |	\
	 K7_EVNTSEL_UNIT_MASK  |	\
	 K7_EVNTSEL_EDGE_MASK  |	\
	 K7_EVNTSEL_INV_MASK   |	\
	 K7_EVNTSEL_COUNTER_MASK)

	return event & K7_EVNTSEL_MASK;
}

/*
 * Propagate counter elapsed time into the generic counter.
 * Can only be executed on the CPU where the counter is active.
 * Returns the delta events processed.
 */
static u64
x86_perf_counter_update(struct perf_counter *counter,
			struct hw_perf_counter *hwc, int idx)
{
	int shift = 64 - x86_pmu.counter_bits;
	u64 prev_raw_count, new_raw_count;
	s64 delta;

	/*
	 * Careful: an NMI might modify the previous counter value.
	 *
	 * Our tactic to handle this is to first atomically read and
	 * exchange a new raw count - then add that new-prev delta
	 * count to the generic counter atomically:
	 */
again:
	prev_raw_count = atomic64_read(&hwc->prev_count);
	rdmsrl(hwc->counter_base + idx, new_raw_count);

	if (atomic64_cmpxchg(&hwc->prev_count, prev_raw_count,
					new_raw_count) != prev_raw_count)
		goto again;

	/*
	 * Now we have the new raw value and have updated the prev
	 * timestamp already. We can now calculate the elapsed delta
	 * (counter-)time and add that to the generic counter.
	 *
	 * Careful, not all hw sign-extends above the physical width
	 * of the count.
	 */
	delta = (new_raw_count << shift) - (prev_raw_count << shift);
	delta >>= shift;

	atomic64_add(delta, &counter->count);
	atomic64_sub(delta, &hwc->period_left);

	return new_raw_count;
}

static atomic_t active_counters;
static DEFINE_MUTEX(pmc_reserve_mutex);

static bool reserve_pmc_hardware(void)
{
	int i;

	if (nmi_watchdog == NMI_LOCAL_APIC)
		disable_lapic_nmi_watchdog();

	for (i = 0; i < x86_pmu.num_counters; i++) {
		if (!reserve_perfctr_nmi(x86_pmu.perfctr + i))
			goto perfctr_fail;
	}

	for (i = 0; i < x86_pmu.num_counters; i++) {
		if (!reserve_evntsel_nmi(x86_pmu.eventsel + i))
			goto eventsel_fail;
	}

	return true;

eventsel_fail:
	for (i--; i >= 0; i--)
		release_evntsel_nmi(x86_pmu.eventsel + i);

	i = x86_pmu.num_counters;

perfctr_fail:
	for (i--; i >= 0; i--)
		release_perfctr_nmi(x86_pmu.perfctr + i);

	if (nmi_watchdog == NMI_LOCAL_APIC)
		enable_lapic_nmi_watchdog();

	return false;
}

static void release_pmc_hardware(void)
{
	int i;

	for (i = 0; i < x86_pmu.num_counters; i++) {
		release_perfctr_nmi(x86_pmu.perfctr + i);
		release_evntsel_nmi(x86_pmu.eventsel + i);
	}

	if (nmi_watchdog == NMI_LOCAL_APIC)
		enable_lapic_nmi_watchdog();
}

static void hw_perf_counter_destroy(struct perf_counter *counter)
{
	if (atomic_dec_and_mutex_lock(&active_counters, &pmc_reserve_mutex)) {
		release_pmc_hardware();
		mutex_unlock(&pmc_reserve_mutex);
	}
}

static inline int x86_pmu_initialized(void)
{
	return x86_pmu.handle_irq != NULL;
}

static inline int
set_ext_hw_attr(struct hw_perf_counter *hwc, struct perf_counter_attr *attr)
{
	unsigned int cache_type, cache_op, cache_result;
	u64 config, val;

	config = attr->config;

	cache_type = (config >>  0) & 0xff;
	if (cache_type >= PERF_COUNT_HW_CACHE_MAX)
		return -EINVAL;

	cache_op = (config >>  8) & 0xff;
	if (cache_op >= PERF_COUNT_HW_CACHE_OP_MAX)
		return -EINVAL;

	cache_result = (config >> 16) & 0xff;
	if (cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	val = hw_cache_event_ids[cache_type][cache_op][cache_result];

	if (val == 0)
		return -ENOENT;

	if (val == -1)
		return -EINVAL;

	hwc->config |= val;

	return 0;
}

/*
 * Setup the hardware configuration for a given attr_type
 */
static int __hw_perf_counter_init(struct perf_counter *counter)
{
	struct perf_counter_attr *attr = &counter->attr;
	struct hw_perf_counter *hwc = &counter->hw;
	int err;

	if (!x86_pmu_initialized())
		return -ENODEV;

	err = 0;
	if (!atomic_inc_not_zero(&active_counters)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&active_counters) == 0 && !reserve_pmc_hardware())
			err = -EBUSY;
		else
			atomic_inc(&active_counters);
		mutex_unlock(&pmc_reserve_mutex);
	}
	if (err)
		return err;

	/*
	 * Generate PMC IRQs:
	 * (keep 'enabled' bit clear for now)
	 */
	hwc->config = ARCH_PERFMON_EVENTSEL_INT;

	/*
	 * Count user and OS events unless requested not to.
	 */
	if (!attr->exclude_user)
		hwc->config |= ARCH_PERFMON_EVENTSEL_USR;
	if (!attr->exclude_kernel)
		hwc->config |= ARCH_PERFMON_EVENTSEL_OS;

	if (!hwc->sample_period) {
		hwc->sample_period = x86_pmu.max_period;
		hwc->last_period = hwc->sample_period;
		atomic64_set(&hwc->period_left, hwc->sample_period);
	}

	counter->destroy = hw_perf_counter_destroy;

	/*
	 * Raw event type provide the config in the event structure
	 */
	if (attr->type == PERF_TYPE_RAW) {
		hwc->config |= x86_pmu.raw_event(attr->config);
		return 0;
	}

	if (attr->type == PERF_TYPE_HW_CACHE)
		return set_ext_hw_attr(hwc, attr);

	if (attr->config >= x86_pmu.max_events)
		return -EINVAL;
	/*
	 * The generic map:
	 */
	hwc->config |= x86_pmu.event_map(attr->config);

	return 0;
}

static void intel_pmu_disable_all(void)
{
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0);
}

static void amd_pmu_disable_all(void)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	int idx;

	if (!cpuc->enabled)
		return;

	cpuc->enabled = 0;
	/*
	 * ensure we write the disable before we start disabling the
	 * counters proper, so that amd_pmu_enable_counter() does the
	 * right thing.
	 */
	barrier();

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		u64 val;

		if (!test_bit(idx, cpuc->active_mask))
			continue;
		rdmsrl(MSR_K7_EVNTSEL0 + idx, val);
		if (!(val & ARCH_PERFMON_EVENTSEL0_ENABLE))
			continue;
		val &= ~ARCH_PERFMON_EVENTSEL0_ENABLE;
		wrmsrl(MSR_K7_EVNTSEL0 + idx, val);
	}
}

void hw_perf_disable(void)
{
	if (!x86_pmu_initialized())
		return;
	return x86_pmu.disable_all();
}

static void intel_pmu_enable_all(void)
{
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, x86_pmu.intel_ctrl);
}

static void amd_pmu_enable_all(void)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	int idx;

	if (cpuc->enabled)
		return;

	cpuc->enabled = 1;
	barrier();

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		u64 val;

		if (!test_bit(idx, cpuc->active_mask))
			continue;
		rdmsrl(MSR_K7_EVNTSEL0 + idx, val);
		if (val & ARCH_PERFMON_EVENTSEL0_ENABLE)
			continue;
		val |= ARCH_PERFMON_EVENTSEL0_ENABLE;
		wrmsrl(MSR_K7_EVNTSEL0 + idx, val);
	}
}

void hw_perf_enable(void)
{
	if (!x86_pmu_initialized())
		return;
	x86_pmu.enable_all();
}

static inline u64 intel_pmu_get_status(void)
{
	u64 status;

	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);

	return status;
}

static inline void intel_pmu_ack_status(u64 ack)
{
	wrmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, ack);
}

static inline void x86_pmu_enable_counter(struct hw_perf_counter *hwc, int idx)
{
	int err;
	err = checking_wrmsrl(hwc->config_base + idx,
			      hwc->config | ARCH_PERFMON_EVENTSEL0_ENABLE);
}

static inline void x86_pmu_disable_counter(struct hw_perf_counter *hwc, int idx)
{
	int err;
	err = checking_wrmsrl(hwc->config_base + idx,
			      hwc->config);
}

static inline void
intel_pmu_disable_fixed(struct hw_perf_counter *hwc, int __idx)
{
	int idx = __idx - X86_PMC_IDX_FIXED;
	u64 ctrl_val, mask;
	int err;

	mask = 0xfULL << (idx * 4);

	rdmsrl(hwc->config_base, ctrl_val);
	ctrl_val &= ~mask;
	err = checking_wrmsrl(hwc->config_base, ctrl_val);
}

static inline void
intel_pmu_disable_counter(struct hw_perf_counter *hwc, int idx)
{
	if (unlikely(hwc->config_base == MSR_ARCH_PERFMON_FIXED_CTR_CTRL)) {
		intel_pmu_disable_fixed(hwc, idx);
		return;
	}

	x86_pmu_disable_counter(hwc, idx);
}

static inline void
amd_pmu_disable_counter(struct hw_perf_counter *hwc, int idx)
{
	x86_pmu_disable_counter(hwc, idx);
}

static DEFINE_PER_CPU(u64, prev_left[X86_PMC_IDX_MAX]);

/*
 * Set the next IRQ period, based on the hwc->period_left value.
 * To be called with the counter disabled in hw:
 */
static int
x86_perf_counter_set_period(struct perf_counter *counter,
			     struct hw_perf_counter *hwc, int idx)
{
	s64 left = atomic64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	int err, ret = 0;

	/*
	 * If we are way outside a reasoable range then just skip forward:
	 */
	if (unlikely(left <= -period)) {
		left = period;
		atomic64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (unlikely(left <= 0)) {
		left += period;
		atomic64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}
	/*
	 * Quirk: certain CPUs dont like it if just 1 event is left:
	 */
	if (unlikely(left < 2))
		left = 2;

	if (left > x86_pmu.max_period)
		left = x86_pmu.max_period;

	per_cpu(prev_left[idx], smp_processor_id()) = left;

	/*
	 * The hw counter starts counting from this counter offset,
	 * mark it to be able to extra future deltas:
	 */
	atomic64_set(&hwc->prev_count, (u64)-left);

	err = checking_wrmsrl(hwc->counter_base + idx,
			     (u64)(-left) & x86_pmu.counter_mask);

	perf_counter_update_userpage(counter);

	return ret;
}

static inline void
intel_pmu_enable_fixed(struct hw_perf_counter *hwc, int __idx)
{
	int idx = __idx - X86_PMC_IDX_FIXED;
	u64 ctrl_val, bits, mask;
	int err;

	/*
	 * Enable IRQ generation (0x8),
	 * and enable ring-3 counting (0x2) and ring-0 counting (0x1)
	 * if requested:
	 */
	bits = 0x8ULL;
	if (hwc->config & ARCH_PERFMON_EVENTSEL_USR)
		bits |= 0x2;
	if (hwc->config & ARCH_PERFMON_EVENTSEL_OS)
		bits |= 0x1;
	bits <<= (idx * 4);
	mask = 0xfULL << (idx * 4);

	rdmsrl(hwc->config_base, ctrl_val);
	ctrl_val &= ~mask;
	ctrl_val |= bits;
	err = checking_wrmsrl(hwc->config_base, ctrl_val);
}

static void intel_pmu_enable_counter(struct hw_perf_counter *hwc, int idx)
{
	if (unlikely(hwc->config_base == MSR_ARCH_PERFMON_FIXED_CTR_CTRL)) {
		intel_pmu_enable_fixed(hwc, idx);
		return;
	}

	x86_pmu_enable_counter(hwc, idx);
}

static void amd_pmu_enable_counter(struct hw_perf_counter *hwc, int idx)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);

	if (cpuc->enabled)
		x86_pmu_enable_counter(hwc, idx);
	else
		x86_pmu_disable_counter(hwc, idx);
}

static int
fixed_mode_idx(struct perf_counter *counter, struct hw_perf_counter *hwc)
{
	unsigned int event;

	if (!x86_pmu.num_counters_fixed)
		return -1;

	event = hwc->config & ARCH_PERFMON_EVENT_MASK;

	if (unlikely(event == x86_pmu.event_map(PERF_COUNT_HW_INSTRUCTIONS)))
		return X86_PMC_IDX_FIXED_INSTRUCTIONS;
	if (unlikely(event == x86_pmu.event_map(PERF_COUNT_HW_CPU_CYCLES)))
		return X86_PMC_IDX_FIXED_CPU_CYCLES;
	if (unlikely(event == x86_pmu.event_map(PERF_COUNT_HW_BUS_CYCLES)))
		return X86_PMC_IDX_FIXED_BUS_CYCLES;

	return -1;
}

/*
 * Find a PMC slot for the freshly enabled / scheduled in counter:
 */
static int x86_pmu_enable(struct perf_counter *counter)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	struct hw_perf_counter *hwc = &counter->hw;
	int idx;

	idx = fixed_mode_idx(counter, hwc);
	if (idx >= 0) {
		/*
		 * Try to get the fixed counter, if that is already taken
		 * then try to get a generic counter:
		 */
		if (test_and_set_bit(idx, cpuc->used_mask))
			goto try_generic;

		hwc->config_base = MSR_ARCH_PERFMON_FIXED_CTR_CTRL;
		/*
		 * We set it so that counter_base + idx in wrmsr/rdmsr maps to
		 * MSR_ARCH_PERFMON_FIXED_CTR0 ... CTR2:
		 */
		hwc->counter_base =
			MSR_ARCH_PERFMON_FIXED_CTR0 - X86_PMC_IDX_FIXED;
		hwc->idx = idx;
	} else {
		idx = hwc->idx;
		/* Try to get the previous generic counter again */
		if (test_and_set_bit(idx, cpuc->used_mask)) {
try_generic:
			idx = find_first_zero_bit(cpuc->used_mask,
						  x86_pmu.num_counters);
			if (idx == x86_pmu.num_counters)
				return -EAGAIN;

			set_bit(idx, cpuc->used_mask);
			hwc->idx = idx;
		}
		hwc->config_base  = x86_pmu.eventsel;
		hwc->counter_base = x86_pmu.perfctr;
	}

	perf_counters_lapic_init();

	x86_pmu.disable(hwc, idx);

	cpuc->counters[idx] = counter;
	set_bit(idx, cpuc->active_mask);

	x86_perf_counter_set_period(counter, hwc, idx);
	x86_pmu.enable(hwc, idx);

	perf_counter_update_userpage(counter);

	return 0;
}

static void x86_pmu_unthrottle(struct perf_counter *counter)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	struct hw_perf_counter *hwc = &counter->hw;

	if (WARN_ON_ONCE(hwc->idx >= X86_PMC_IDX_MAX ||
				cpuc->counters[hwc->idx] != counter))
		return;

	x86_pmu.enable(hwc, hwc->idx);
}

void perf_counter_print_debug(void)
{
	u64 ctrl, status, overflow, pmc_ctrl, pmc_count, prev_left, fixed;
	struct cpu_hw_counters *cpuc;
	unsigned long flags;
	int cpu, idx;

	if (!x86_pmu.num_counters)
		return;

	local_irq_save(flags);

	cpu = smp_processor_id();
	cpuc = &per_cpu(cpu_hw_counters, cpu);

	if (x86_pmu.version >= 2) {
		rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, ctrl);
		rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);
		rdmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, overflow);
		rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR_CTRL, fixed);

		pr_info("\n");
		pr_info("CPU#%d: ctrl:       %016llx\n", cpu, ctrl);
		pr_info("CPU#%d: status:     %016llx\n", cpu, status);
		pr_info("CPU#%d: overflow:   %016llx\n", cpu, overflow);
		pr_info("CPU#%d: fixed:      %016llx\n", cpu, fixed);
	}
	pr_info("CPU#%d: used:       %016llx\n", cpu, *(u64 *)cpuc->used_mask);

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		rdmsrl(x86_pmu.eventsel + idx, pmc_ctrl);
		rdmsrl(x86_pmu.perfctr  + idx, pmc_count);

		prev_left = per_cpu(prev_left[idx], cpu);

		pr_info("CPU#%d:   gen-PMC%d ctrl:  %016llx\n",
			cpu, idx, pmc_ctrl);
		pr_info("CPU#%d:   gen-PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
		pr_info("CPU#%d:   gen-PMC%d left:  %016llx\n",
			cpu, idx, prev_left);
	}
	for (idx = 0; idx < x86_pmu.num_counters_fixed; idx++) {
		rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR0 + idx, pmc_count);

		pr_info("CPU#%d: fixed-PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
	}
	local_irq_restore(flags);
}

static void x86_pmu_disable(struct perf_counter *counter)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	struct hw_perf_counter *hwc = &counter->hw;
	int idx = hwc->idx;

	/*
	 * Must be done before we disable, otherwise the nmi handler
	 * could reenable again:
	 */
	clear_bit(idx, cpuc->active_mask);
	x86_pmu.disable(hwc, idx);

	/*
	 * Make sure the cleared pointer becomes visible before we
	 * (potentially) free the counter:
	 */
	barrier();

	/*
	 * Drain the remaining delta count out of a counter
	 * that we are disabling:
	 */
	x86_perf_counter_update(counter, hwc, idx);
	cpuc->counters[idx] = NULL;
	clear_bit(idx, cpuc->used_mask);

	perf_counter_update_userpage(counter);
}

/*
 * Save and restart an expired counter. Called by NMI contexts,
 * so it has to be careful about preempting normal counter ops:
 */
static int intel_pmu_save_and_restart(struct perf_counter *counter)
{
	struct hw_perf_counter *hwc = &counter->hw;
	int idx = hwc->idx;
	int ret;

	x86_perf_counter_update(counter, hwc, idx);
	ret = x86_perf_counter_set_period(counter, hwc, idx);

	if (counter->state == PERF_COUNTER_STATE_ACTIVE)
		intel_pmu_enable_counter(hwc, idx);

	return ret;
}

static void intel_pmu_reset(void)
{
	unsigned long flags;
	int idx;

	if (!x86_pmu.num_counters)
		return;

	local_irq_save(flags);

	printk("clearing PMU state on CPU#%d\n", smp_processor_id());

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		checking_wrmsrl(x86_pmu.eventsel + idx, 0ull);
		checking_wrmsrl(x86_pmu.perfctr  + idx, 0ull);
	}
	for (idx = 0; idx < x86_pmu.num_counters_fixed; idx++) {
		checking_wrmsrl(MSR_ARCH_PERFMON_FIXED_CTR0 + idx, 0ull);
	}

	local_irq_restore(flags);
}


/*
 * This handler is triggered by the local APIC, so the APIC IRQ handling
 * rules apply:
 */
static int intel_pmu_handle_irq(struct pt_regs *regs)
{
	struct perf_sample_data data;
	struct cpu_hw_counters *cpuc;
	int bit, cpu, loops;
	u64 ack, status;

	data.regs = regs;
	data.addr = 0;

	cpu = smp_processor_id();
	cpuc = &per_cpu(cpu_hw_counters, cpu);

	perf_disable();
	status = intel_pmu_get_status();
	if (!status) {
		perf_enable();
		return 0;
	}

	loops = 0;
again:
	if (++loops > 100) {
		WARN_ONCE(1, "perfcounters: irq loop stuck!\n");
		perf_counter_print_debug();
		intel_pmu_reset();
		perf_enable();
		return 1;
	}

	inc_irq_stat(apic_perf_irqs);
	ack = status;
	for_each_bit(bit, (unsigned long *)&status, X86_PMC_IDX_MAX) {
		struct perf_counter *counter = cpuc->counters[bit];

		clear_bit(bit, (unsigned long *) &status);
		if (!test_bit(bit, cpuc->active_mask))
			continue;

		if (!intel_pmu_save_and_restart(counter))
			continue;

		data.period = counter->hw.last_period;

		if (perf_counter_overflow(counter, 1, &data))
			intel_pmu_disable_counter(&counter->hw, bit);
	}

	intel_pmu_ack_status(ack);

	/*
	 * Repeat if there is more work to be done:
	 */
	status = intel_pmu_get_status();
	if (status)
		goto again;

	perf_enable();

	return 1;
}

static int amd_pmu_handle_irq(struct pt_regs *regs)
{
	struct perf_sample_data data;
	struct cpu_hw_counters *cpuc;
	struct perf_counter *counter;
	struct hw_perf_counter *hwc;
	int cpu, idx, handled = 0;
	u64 val;

	data.regs = regs;
	data.addr = 0;

	cpu = smp_processor_id();
	cpuc = &per_cpu(cpu_hw_counters, cpu);

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		if (!test_bit(idx, cpuc->active_mask))
			continue;

		counter = cpuc->counters[idx];
		hwc = &counter->hw;

		val = x86_perf_counter_update(counter, hwc, idx);
		if (val & (1ULL << (x86_pmu.counter_bits - 1)))
			continue;

		/*
		 * counter overflow
		 */
		handled		= 1;
		data.period	= counter->hw.last_period;

		if (!x86_perf_counter_set_period(counter, hwc, idx))
			continue;

		if (perf_counter_overflow(counter, 1, &data))
			amd_pmu_disable_counter(hwc, idx);
	}

	if (handled)
		inc_irq_stat(apic_perf_irqs);

	return handled;
}

void smp_perf_pending_interrupt(struct pt_regs *regs)
{
	irq_enter();
	ack_APIC_irq();
	inc_irq_stat(apic_pending_irqs);
	perf_counter_do_pending();
	irq_exit();
}

void set_perf_counter_pending(void)
{
	apic->send_IPI_self(LOCAL_PENDING_VECTOR);
}

void perf_counters_lapic_init(void)
{
	if (!x86_pmu_initialized())
		return;

	/*
	 * Always use NMI for PMU
	 */
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}

static int __kprobes
perf_counter_nmi_handler(struct notifier_block *self,
			 unsigned long cmd, void *__args)
{
	struct die_args *args = __args;
	struct pt_regs *regs;

	if (!atomic_read(&active_counters))
		return NOTIFY_DONE;

	switch (cmd) {
	case DIE_NMI:
	case DIE_NMI_IPI:
		break;

	default:
		return NOTIFY_DONE;
	}

	regs = args->regs;

	apic_write(APIC_LVTPC, APIC_DM_NMI);
	/*
	 * Can't rely on the handled return value to say it was our NMI, two
	 * counters could trigger 'simultaneously' raising two back-to-back NMIs.
	 *
	 * If the first NMI handles both, the latter will be empty and daze
	 * the CPU.
	 */
	x86_pmu.handle_irq(regs);

	return NOTIFY_STOP;
}

static __read_mostly struct notifier_block perf_counter_nmi_notifier = {
	.notifier_call		= perf_counter_nmi_handler,
	.next			= NULL,
	.priority		= 1
};

static struct x86_pmu intel_pmu = {
	.name			= "Intel",
	.handle_irq		= intel_pmu_handle_irq,
	.disable_all		= intel_pmu_disable_all,
	.enable_all		= intel_pmu_enable_all,
	.enable			= intel_pmu_enable_counter,
	.disable		= intel_pmu_disable_counter,
	.eventsel		= MSR_ARCH_PERFMON_EVENTSEL0,
	.perfctr		= MSR_ARCH_PERFMON_PERFCTR0,
	.event_map		= intel_pmu_event_map,
	.raw_event		= intel_pmu_raw_event,
	.max_events		= ARRAY_SIZE(intel_perfmon_event_map),
	/*
	 * Intel PMCs cannot be accessed sanely above 32 bit width,
	 * so we install an artificial 1<<31 period regardless of
	 * the generic counter period:
	 */
	.max_period		= (1ULL << 31) - 1,
};

static struct x86_pmu amd_pmu = {
	.name			= "AMD",
	.handle_irq		= amd_pmu_handle_irq,
	.disable_all		= amd_pmu_disable_all,
	.enable_all		= amd_pmu_enable_all,
	.enable			= amd_pmu_enable_counter,
	.disable		= amd_pmu_disable_counter,
	.eventsel		= MSR_K7_EVNTSEL0,
	.perfctr		= MSR_K7_PERFCTR0,
	.event_map		= amd_pmu_event_map,
	.raw_event		= amd_pmu_raw_event,
	.max_events		= ARRAY_SIZE(amd_perfmon_event_map),
	.num_counters		= 4,
	.counter_bits		= 48,
	.counter_mask		= (1ULL << 48) - 1,
	/* use highest bit to detect overflow */
	.max_period		= (1ULL << 47) - 1,
};

static int intel_pmu_init(void)
{
	union cpuid10_edx edx;
	union cpuid10_eax eax;
	unsigned int unused;
	unsigned int ebx;
	int version;

	if (!cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON))
		return -ENODEV;

	/*
	 * Check whether the Architectural PerfMon supports
	 * Branch Misses Retired Event or not.
	 */
	cpuid(10, &eax.full, &ebx, &unused, &edx.full);
	if (eax.split.mask_length <= ARCH_PERFMON_BRANCH_MISSES_RETIRED)
		return -ENODEV;

	version = eax.split.version_id;
	if (version < 2)
		return -ENODEV;

	x86_pmu				= intel_pmu;
	x86_pmu.version			= version;
	x86_pmu.num_counters		= eax.split.num_counters;
	x86_pmu.counter_bits		= eax.split.bit_width;
	x86_pmu.counter_mask		= (1ULL << eax.split.bit_width) - 1;

	/*
	 * Quirk: v2 perfmon does not report fixed-purpose counters, so
	 * assume at least 3 counters:
	 */
	x86_pmu.num_counters_fixed	= max((int)edx.split.num_counters_fixed, 3);

	/*
	 * Install the hw-cache-events table:
	 */
	switch (boot_cpu_data.x86_model) {
	case 15: /* original 65 nm celeron/pentium/core2/xeon, "Merom"/"Conroe" */
	case 22: /* single-core 65 nm celeron/core2solo "Merom-L"/"Conroe-L" */
	case 23: /* current 45 nm celeron/core2/xeon "Penryn"/"Wolfdale" */
	case 29: /* six-core 45 nm xeon "Dunnington" */
		memcpy(hw_cache_event_ids, core2_hw_cache_event_ids,
		       sizeof(hw_cache_event_ids));

		pr_cont("Core2 events, ");
		break;
	default:
	case 26:
		memcpy(hw_cache_event_ids, nehalem_hw_cache_event_ids,
		       sizeof(hw_cache_event_ids));

		pr_cont("Nehalem/Corei7 events, ");
		break;
	case 28:
		memcpy(hw_cache_event_ids, atom_hw_cache_event_ids,
		       sizeof(hw_cache_event_ids));

		pr_cont("Atom events, ");
		break;
	}
	return 0;
}

static int amd_pmu_init(void)
{
	/* Performance-monitoring supported from K7 and later: */
	if (boot_cpu_data.x86 < 6)
		return -ENODEV;

	x86_pmu = amd_pmu;

	/* Events are common for all AMDs */
	memcpy(hw_cache_event_ids, amd_hw_cache_event_ids,
	       sizeof(hw_cache_event_ids));

	return 0;
}

void __init init_hw_perf_counters(void)
{
	int err;

	pr_info("Performance Counters: ");

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		err = intel_pmu_init();
		break;
	case X86_VENDOR_AMD:
		err = amd_pmu_init();
		break;
	default:
		return;
	}
	if (err != 0) {
		pr_cont("no PMU driver, software counters only.\n");
		return;
	}

	pr_cont("%s PMU driver.\n", x86_pmu.name);

	if (x86_pmu.num_counters > X86_PMC_MAX_GENERIC) {
		WARN(1, KERN_ERR "hw perf counters %d > max(%d), clipping!",
		     x86_pmu.num_counters, X86_PMC_MAX_GENERIC);
		x86_pmu.num_counters = X86_PMC_MAX_GENERIC;
	}
	perf_counter_mask = (1 << x86_pmu.num_counters) - 1;
	perf_max_counters = x86_pmu.num_counters;

	if (x86_pmu.num_counters_fixed > X86_PMC_MAX_FIXED) {
		WARN(1, KERN_ERR "hw perf counters fixed %d > max(%d), clipping!",
		     x86_pmu.num_counters_fixed, X86_PMC_MAX_FIXED);
		x86_pmu.num_counters_fixed = X86_PMC_MAX_FIXED;
	}

	perf_counter_mask |=
		((1LL << x86_pmu.num_counters_fixed)-1) << X86_PMC_IDX_FIXED;
	x86_pmu.intel_ctrl = perf_counter_mask;

	perf_counters_lapic_init();
	register_die_notifier(&perf_counter_nmi_notifier);

	pr_info("... version:                 %d\n",     x86_pmu.version);
	pr_info("... bit width:               %d\n",     x86_pmu.counter_bits);
	pr_info("... generic counters:        %d\n",     x86_pmu.num_counters);
	pr_info("... value mask:              %016Lx\n", x86_pmu.counter_mask);
	pr_info("... max period:              %016Lx\n", x86_pmu.max_period);
	pr_info("... fixed-purpose counters:  %d\n",     x86_pmu.num_counters_fixed);
	pr_info("... counter mask:            %016Lx\n", perf_counter_mask);
}

static inline void x86_pmu_read(struct perf_counter *counter)
{
	x86_perf_counter_update(counter, &counter->hw, counter->hw.idx);
}

static const struct pmu pmu = {
	.enable		= x86_pmu_enable,
	.disable	= x86_pmu_disable,
	.read		= x86_pmu_read,
	.unthrottle	= x86_pmu_unthrottle,
};

const struct pmu *hw_perf_counter_init(struct perf_counter *counter)
{
	int err;

	err = __hw_perf_counter_init(counter);
	if (err)
		return ERR_PTR(err);

	return &pmu;
}

/*
 * callchain support
 */

static inline
void callchain_store(struct perf_callchain_entry *entry, u64 ip)
{
	if (entry->nr < PERF_MAX_STACK_DEPTH)
		entry->ip[entry->nr++] = ip;
}

static DEFINE_PER_CPU(struct perf_callchain_entry, irq_entry);
static DEFINE_PER_CPU(struct perf_callchain_entry, nmi_entry);
static DEFINE_PER_CPU(int, in_nmi_frame);


static void
backtrace_warning_symbol(void *data, char *msg, unsigned long symbol)
{
	/* Ignore warnings */
}

static void backtrace_warning(void *data, char *msg)
{
	/* Ignore warnings */
}

static int backtrace_stack(void *data, char *name)
{
	per_cpu(in_nmi_frame, smp_processor_id()) =
			x86_is_stack_id(NMI_STACK, name);

	return 0;
}

static void backtrace_address(void *data, unsigned long addr, int reliable)
{
	struct perf_callchain_entry *entry = data;

	if (per_cpu(in_nmi_frame, smp_processor_id()))
		return;

	if (reliable)
		callchain_store(entry, addr);
}

static const struct stacktrace_ops backtrace_ops = {
	.warning		= backtrace_warning,
	.warning_symbol		= backtrace_warning_symbol,
	.stack			= backtrace_stack,
	.address		= backtrace_address,
};

#include "../dumpstack.h"

static void
perf_callchain_kernel(struct pt_regs *regs, struct perf_callchain_entry *entry)
{
	callchain_store(entry, PERF_CONTEXT_KERNEL);
	callchain_store(entry, regs->ip);

	dump_trace(NULL, regs, NULL, 0, &backtrace_ops, entry);
}

/*
 * best effort, GUP based copy_from_user() that assumes IRQ or NMI context
 */
static unsigned long
copy_from_user_nmi(void *to, const void __user *from, unsigned long n)
{
	unsigned long offset, addr = (unsigned long)from;
	int type = in_nmi() ? KM_NMI : KM_IRQ0;
	unsigned long size, len = 0;
	struct page *page;
	void *map;
	int ret;

	do {
		ret = __get_user_pages_fast(addr, 1, 0, &page);
		if (!ret)
			break;

		offset = addr & (PAGE_SIZE - 1);
		size = min(PAGE_SIZE - offset, n - len);

		map = kmap_atomic(page, type);
		memcpy(to, map+offset, size);
		kunmap_atomic(map, type);
		put_page(page);

		len  += size;
		to   += size;
		addr += size;

	} while (len < n);

	return len;
}

static int copy_stack_frame(const void __user *fp, struct stack_frame *frame)
{
	unsigned long bytes;

	bytes = copy_from_user_nmi(frame, fp, sizeof(*frame));

	return bytes == sizeof(*frame);
}

static void
perf_callchain_user(struct pt_regs *regs, struct perf_callchain_entry *entry)
{
	struct stack_frame frame;
	const void __user *fp;

	if (!user_mode(regs))
		regs = task_pt_regs(current);

	fp = (void __user *)regs->bp;

	callchain_store(entry, PERF_CONTEXT_USER);
	callchain_store(entry, regs->ip);

	while (entry->nr < PERF_MAX_STACK_DEPTH) {
		frame.next_frame	     = NULL;
		frame.return_address = 0;

		if (!copy_stack_frame(fp, &frame))
			break;

		if ((unsigned long)fp < regs->sp)
			break;

		callchain_store(entry, frame.return_address);
		fp = frame.next_frame;
	}
}

static void
perf_do_callchain(struct pt_regs *regs, struct perf_callchain_entry *entry)
{
	int is_user;

	if (!regs)
		return;

	is_user = user_mode(regs);

	if (!current || current->pid == 0)
		return;

	if (is_user && current->state != TASK_RUNNING)
		return;

	if (!is_user)
		perf_callchain_kernel(regs, entry);

	if (current->mm)
		perf_callchain_user(regs, entry);
}

struct perf_callchain_entry *perf_callchain(struct pt_regs *regs)
{
	struct perf_callchain_entry *entry;

	if (in_nmi())
		entry = &__get_cpu_var(nmi_entry);
	else
		entry = &__get_cpu_var(irq_entry);

	entry->nr = 0;

	perf_do_callchain(regs, entry);

	return entry;
}
