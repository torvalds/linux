/*
 * Performance events x86 architecture code
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2009 Jaswinder Singh Rajput
 *  Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 *  Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *  Copyright (C) 2009 Intel Corporation, <markus.t.metzger@intel.com>
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/perf_event.h>
#include <linux/capability.h>
#include <linux/notifier.h>
#include <linux/hardirq.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/kdebug.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>
#include <linux/cpu.h>

#include <asm/apic.h>
#include <asm/stacktrace.h>
#include <asm/nmi.h>

static u64 perf_event_mask __read_mostly;

/* The maximal number of PEBS events: */
#define MAX_PEBS_EVENTS	4

/* The size of a BTS record in bytes: */
#define BTS_RECORD_SIZE		24

/* The size of a per-cpu BTS buffer in bytes: */
#define BTS_BUFFER_SIZE		(BTS_RECORD_SIZE * 2048)

/* The BTS overflow threshold in bytes from the end of the buffer: */
#define BTS_OVFL_TH		(BTS_RECORD_SIZE * 128)


/*
 * Bits in the debugctlmsr controlling branch tracing.
 */
#define X86_DEBUGCTL_TR			(1 << 6)
#define X86_DEBUGCTL_BTS		(1 << 7)
#define X86_DEBUGCTL_BTINT		(1 << 8)
#define X86_DEBUGCTL_BTS_OFF_OS		(1 << 9)
#define X86_DEBUGCTL_BTS_OFF_USR	(1 << 10)

/*
 * A debug store configuration.
 *
 * We only support architectures that use 64bit fields.
 */
struct debug_store {
	u64	bts_buffer_base;
	u64	bts_index;
	u64	bts_absolute_maximum;
	u64	bts_interrupt_threshold;
	u64	pebs_buffer_base;
	u64	pebs_index;
	u64	pebs_absolute_maximum;
	u64	pebs_interrupt_threshold;
	u64	pebs_event_reset[MAX_PEBS_EVENTS];
};

struct cpu_hw_events {
	struct perf_event	*events[X86_PMC_IDX_MAX];
	unsigned long		used_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	unsigned long		active_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	unsigned long		interrupts;
	int			enabled;
	struct debug_store	*ds;
};

struct event_constraint {
	unsigned long	idxmsk[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	int		code;
};

#define EVENT_CONSTRAINT(c, m) { .code = (c), .idxmsk[0] = (m) }
#define EVENT_CONSTRAINT_END  { .code = 0, .idxmsk[0] = 0 }

#define for_each_event_constraint(e, c) \
	for ((e) = (c); (e)->idxmsk[0]; (e)++)


/*
 * struct x86_pmu - generic x86 pmu
 */
struct x86_pmu {
	const char	*name;
	int		version;
	int		(*handle_irq)(struct pt_regs *);
	void		(*disable_all)(void);
	void		(*enable_all)(void);
	void		(*enable)(struct hw_perf_event *, int);
	void		(*disable)(struct hw_perf_event *, int);
	unsigned	eventsel;
	unsigned	perfctr;
	u64		(*event_map)(int);
	u64		(*raw_event)(u64);
	int		max_events;
	int		num_events;
	int		num_events_fixed;
	int		event_bits;
	u64		event_mask;
	int		apic;
	u64		max_period;
	u64		intel_ctrl;
	void		(*enable_bts)(u64 config);
	void		(*disable_bts)(void);
	int		(*get_event_idx)(struct cpu_hw_events *cpuc,
					 struct hw_perf_event *hwc);
};

static struct x86_pmu x86_pmu __read_mostly;

static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events) = {
	.enabled = 1,
};

static const struct event_constraint *event_constraints;

/*
 * Not sure about some of these
 */
static const u64 p6_perfmon_event_map[] =
{
  [PERF_COUNT_HW_CPU_CYCLES]		= 0x0079,
  [PERF_COUNT_HW_INSTRUCTIONS]		= 0x00c0,
  [PERF_COUNT_HW_CACHE_REFERENCES]	= 0x0f2e,
  [PERF_COUNT_HW_CACHE_MISSES]		= 0x012e,
  [PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x00c4,
  [PERF_COUNT_HW_BRANCH_MISSES]		= 0x00c5,
  [PERF_COUNT_HW_BUS_CYCLES]		= 0x0062,
};

static u64 p6_pmu_event_map(int hw_event)
{
	return p6_perfmon_event_map[hw_event];
}

/*
 * Event setting that is specified not to count anything.
 * We use this to effectively disable a counter.
 *
 * L2_RQSTS with 0 MESI unit mask.
 */
#define P6_NOP_EVENT			0x0000002EULL

static u64 p6_pmu_raw_event(u64 hw_event)
{
#define P6_EVNTSEL_EVENT_MASK		0x000000FFULL
#define P6_EVNTSEL_UNIT_MASK		0x0000FF00ULL
#define P6_EVNTSEL_EDGE_MASK		0x00040000ULL
#define P6_EVNTSEL_INV_MASK		0x00800000ULL
#define P6_EVNTSEL_REG_MASK		0xFF000000ULL

#define P6_EVNTSEL_MASK			\
	(P6_EVNTSEL_EVENT_MASK |	\
	 P6_EVNTSEL_UNIT_MASK  |	\
	 P6_EVNTSEL_EDGE_MASK  |	\
	 P6_EVNTSEL_INV_MASK   |	\
	 P6_EVNTSEL_REG_MASK)

	return hw_event & P6_EVNTSEL_MASK;
}

static const struct event_constraint intel_p6_event_constraints[] =
{
	EVENT_CONSTRAINT(0xc1, 0x1),	/* FLOPS */
	EVENT_CONSTRAINT(0x10, 0x1),	/* FP_COMP_OPS_EXE */
	EVENT_CONSTRAINT(0x11, 0x1),	/* FP_ASSIST */
	EVENT_CONSTRAINT(0x12, 0x2),	/* MUL */
	EVENT_CONSTRAINT(0x13, 0x2),	/* DIV */
	EVENT_CONSTRAINT(0x14, 0x1),	/* CYCLES_DIV_BUSY */
	EVENT_CONSTRAINT_END
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

static const struct event_constraint intel_core_event_constraints[] =
{
	EVENT_CONSTRAINT(0x10, 0x1),	/* FP_COMP_OPS_EXE */
	EVENT_CONSTRAINT(0x11, 0x2),	/* FP_ASSIST */
	EVENT_CONSTRAINT(0x12, 0x2),	/* MUL */
	EVENT_CONSTRAINT(0x13, 0x2),	/* DIV */
	EVENT_CONSTRAINT(0x14, 0x1),	/* CYCLES_DIV_BUSY */
	EVENT_CONSTRAINT(0x18, 0x1),	/* IDLE_DURING_DIV */
	EVENT_CONSTRAINT(0x19, 0x2),	/* DELAYED_BYPASS */
	EVENT_CONSTRAINT(0xa1, 0x1),	/* RS_UOPS_DISPATCH_CYCLES */
	EVENT_CONSTRAINT(0xcb, 0x1),	/* MEM_LOAD_RETIRED */
	EVENT_CONSTRAINT_END
};

static const struct event_constraint intel_nehalem_event_constraints[] =
{
	EVENT_CONSTRAINT(0x40, 0x3),	/* L1D_CACHE_LD */
	EVENT_CONSTRAINT(0x41, 0x3),	/* L1D_CACHE_ST */
	EVENT_CONSTRAINT(0x42, 0x3),	/* L1D_CACHE_LOCK */
	EVENT_CONSTRAINT(0x43, 0x3),	/* L1D_ALL_REF */
	EVENT_CONSTRAINT(0x4e, 0x3),	/* L1D_PREFETCH */
	EVENT_CONSTRAINT(0x4c, 0x3),	/* LOAD_HIT_PRE */
	EVENT_CONSTRAINT(0x51, 0x3),	/* L1D */
	EVENT_CONSTRAINT(0x52, 0x3),	/* L1D_CACHE_PREFETCH_LOCK_FB_HIT */
	EVENT_CONSTRAINT(0x53, 0x3),	/* L1D_CACHE_LOCK_FB_HIT */
	EVENT_CONSTRAINT(0xc5, 0x3),	/* CACHE_LOCK_CYCLES */
	EVENT_CONSTRAINT_END
};

static u64 intel_pmu_event_map(int hw_event)
{
	return intel_perfmon_event_map[hw_event];
}

/*
 * Generalized hw caching related hw_event table, filled
 * in on a per model basis. A value of 0 means
 * 'not supported', -1 means 'hw_event makes no sense on
 * this CPU', any other value means the raw hw_event
 * ID.
 */

#define C(x) PERF_COUNT_HW_CACHE_##x

static u64 __read_mostly hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX];

static __initconst u64 nehalem_hw_cache_event_ids
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

static __initconst u64 core2_hw_cache_event_ids
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

static __initconst u64 atom_hw_cache_event_ids
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

static u64 intel_pmu_raw_event(u64 hw_event)
{
#define CORE_EVNTSEL_EVENT_MASK		0x000000FFULL
#define CORE_EVNTSEL_UNIT_MASK		0x0000FF00ULL
#define CORE_EVNTSEL_EDGE_MASK		0x00040000ULL
#define CORE_EVNTSEL_INV_MASK		0x00800000ULL
#define CORE_EVNTSEL_REG_MASK		0xFF000000ULL

#define CORE_EVNTSEL_MASK		\
	(CORE_EVNTSEL_EVENT_MASK |	\
	 CORE_EVNTSEL_UNIT_MASK  |	\
	 CORE_EVNTSEL_EDGE_MASK  |	\
	 CORE_EVNTSEL_INV_MASK  |	\
	 CORE_EVNTSEL_REG_MASK)

	return hw_event & CORE_EVNTSEL_MASK;
}

static __initconst u64 amd_hw_cache_event_ids
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

static u64 amd_pmu_event_map(int hw_event)
{
	return amd_perfmon_event_map[hw_event];
}

static u64 amd_pmu_raw_event(u64 hw_event)
{
#define K7_EVNTSEL_EVENT_MASK	0x7000000FFULL
#define K7_EVNTSEL_UNIT_MASK	0x00000FF00ULL
#define K7_EVNTSEL_EDGE_MASK	0x000040000ULL
#define K7_EVNTSEL_INV_MASK	0x000800000ULL
#define K7_EVNTSEL_REG_MASK	0x0FF000000ULL

#define K7_EVNTSEL_MASK			\
	(K7_EVNTSEL_EVENT_MASK |	\
	 K7_EVNTSEL_UNIT_MASK  |	\
	 K7_EVNTSEL_EDGE_MASK  |	\
	 K7_EVNTSEL_INV_MASK   |	\
	 K7_EVNTSEL_REG_MASK)

	return hw_event & K7_EVNTSEL_MASK;
}

/*
 * Propagate event elapsed time into the generic event.
 * Can only be executed on the CPU where the event is active.
 * Returns the delta events processed.
 */
static u64
x86_perf_event_update(struct perf_event *event,
			struct hw_perf_event *hwc, int idx)
{
	int shift = 64 - x86_pmu.event_bits;
	u64 prev_raw_count, new_raw_count;
	s64 delta;

	if (idx == X86_PMC_IDX_FIXED_BTS)
		return 0;

	/*
	 * Careful: an NMI might modify the previous event value.
	 *
	 * Our tactic to handle this is to first atomically read and
	 * exchange a new raw count - then add that new-prev delta
	 * count to the generic event atomically:
	 */
again:
	prev_raw_count = atomic64_read(&hwc->prev_count);
	rdmsrl(hwc->event_base + idx, new_raw_count);

	if (atomic64_cmpxchg(&hwc->prev_count, prev_raw_count,
					new_raw_count) != prev_raw_count)
		goto again;

	/*
	 * Now we have the new raw value and have updated the prev
	 * timestamp already. We can now calculate the elapsed delta
	 * (event-)time and add that to the generic event.
	 *
	 * Careful, not all hw sign-extends above the physical width
	 * of the count.
	 */
	delta = (new_raw_count << shift) - (prev_raw_count << shift);
	delta >>= shift;

	atomic64_add(delta, &event->count);
	atomic64_sub(delta, &hwc->period_left);

	return new_raw_count;
}

static atomic_t active_events;
static DEFINE_MUTEX(pmc_reserve_mutex);

static bool reserve_pmc_hardware(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	int i;

	if (nmi_watchdog == NMI_LOCAL_APIC)
		disable_lapic_nmi_watchdog();

	for (i = 0; i < x86_pmu.num_events; i++) {
		if (!reserve_perfctr_nmi(x86_pmu.perfctr + i))
			goto perfctr_fail;
	}

	for (i = 0; i < x86_pmu.num_events; i++) {
		if (!reserve_evntsel_nmi(x86_pmu.eventsel + i))
			goto eventsel_fail;
	}
#endif

	return true;

#ifdef CONFIG_X86_LOCAL_APIC
eventsel_fail:
	for (i--; i >= 0; i--)
		release_evntsel_nmi(x86_pmu.eventsel + i);

	i = x86_pmu.num_events;

perfctr_fail:
	for (i--; i >= 0; i--)
		release_perfctr_nmi(x86_pmu.perfctr + i);

	if (nmi_watchdog == NMI_LOCAL_APIC)
		enable_lapic_nmi_watchdog();

	return false;
#endif
}

static void release_pmc_hardware(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	int i;

	for (i = 0; i < x86_pmu.num_events; i++) {
		release_perfctr_nmi(x86_pmu.perfctr + i);
		release_evntsel_nmi(x86_pmu.eventsel + i);
	}

	if (nmi_watchdog == NMI_LOCAL_APIC)
		enable_lapic_nmi_watchdog();
#endif
}

static inline bool bts_available(void)
{
	return x86_pmu.enable_bts != NULL;
}

static inline void init_debug_store_on_cpu(int cpu)
{
	struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;

	if (!ds)
		return;

	wrmsr_on_cpu(cpu, MSR_IA32_DS_AREA,
		     (u32)((u64)(unsigned long)ds),
		     (u32)((u64)(unsigned long)ds >> 32));
}

static inline void fini_debug_store_on_cpu(int cpu)
{
	if (!per_cpu(cpu_hw_events, cpu).ds)
		return;

	wrmsr_on_cpu(cpu, MSR_IA32_DS_AREA, 0, 0);
}

static void release_bts_hardware(void)
{
	int cpu;

	if (!bts_available())
		return;

	get_online_cpus();

	for_each_online_cpu(cpu)
		fini_debug_store_on_cpu(cpu);

	for_each_possible_cpu(cpu) {
		struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;

		if (!ds)
			continue;

		per_cpu(cpu_hw_events, cpu).ds = NULL;

		kfree((void *)(unsigned long)ds->bts_buffer_base);
		kfree(ds);
	}

	put_online_cpus();
}

static int reserve_bts_hardware(void)
{
	int cpu, err = 0;

	if (!bts_available())
		return 0;

	get_online_cpus();

	for_each_possible_cpu(cpu) {
		struct debug_store *ds;
		void *buffer;

		err = -ENOMEM;
		buffer = kzalloc(BTS_BUFFER_SIZE, GFP_KERNEL);
		if (unlikely(!buffer))
			break;

		ds = kzalloc(sizeof(*ds), GFP_KERNEL);
		if (unlikely(!ds)) {
			kfree(buffer);
			break;
		}

		ds->bts_buffer_base = (u64)(unsigned long)buffer;
		ds->bts_index = ds->bts_buffer_base;
		ds->bts_absolute_maximum =
			ds->bts_buffer_base + BTS_BUFFER_SIZE;
		ds->bts_interrupt_threshold =
			ds->bts_absolute_maximum - BTS_OVFL_TH;

		per_cpu(cpu_hw_events, cpu).ds = ds;
		err = 0;
	}

	if (err)
		release_bts_hardware();
	else {
		for_each_online_cpu(cpu)
			init_debug_store_on_cpu(cpu);
	}

	put_online_cpus();

	return err;
}

static void hw_perf_event_destroy(struct perf_event *event)
{
	if (atomic_dec_and_mutex_lock(&active_events, &pmc_reserve_mutex)) {
		release_pmc_hardware();
		release_bts_hardware();
		mutex_unlock(&pmc_reserve_mutex);
	}
}

static inline int x86_pmu_initialized(void)
{
	return x86_pmu.handle_irq != NULL;
}

static inline int
set_ext_hw_attr(struct hw_perf_event *hwc, struct perf_event_attr *attr)
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

static void intel_pmu_enable_bts(u64 config)
{
	unsigned long debugctlmsr;

	debugctlmsr = get_debugctlmsr();

	debugctlmsr |= X86_DEBUGCTL_TR;
	debugctlmsr |= X86_DEBUGCTL_BTS;
	debugctlmsr |= X86_DEBUGCTL_BTINT;

	if (!(config & ARCH_PERFMON_EVENTSEL_OS))
		debugctlmsr |= X86_DEBUGCTL_BTS_OFF_OS;

	if (!(config & ARCH_PERFMON_EVENTSEL_USR))
		debugctlmsr |= X86_DEBUGCTL_BTS_OFF_USR;

	update_debugctlmsr(debugctlmsr);
}

static void intel_pmu_disable_bts(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	unsigned long debugctlmsr;

	if (!cpuc->ds)
		return;

	debugctlmsr = get_debugctlmsr();

	debugctlmsr &=
		~(X86_DEBUGCTL_TR | X86_DEBUGCTL_BTS | X86_DEBUGCTL_BTINT |
		  X86_DEBUGCTL_BTS_OFF_OS | X86_DEBUGCTL_BTS_OFF_USR);

	update_debugctlmsr(debugctlmsr);
}

/*
 * Setup the hardware configuration for a given attr_type
 */
static int __hw_perf_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	u64 config;
	int err;

	if (!x86_pmu_initialized())
		return -ENODEV;

	err = 0;
	if (!atomic_inc_not_zero(&active_events)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&active_events) == 0) {
			if (!reserve_pmc_hardware())
				err = -EBUSY;
			else
				err = reserve_bts_hardware();
		}
		if (!err)
			atomic_inc(&active_events);
		mutex_unlock(&pmc_reserve_mutex);
	}
	if (err)
		return err;

	event->destroy = hw_perf_event_destroy;

	/*
	 * Generate PMC IRQs:
	 * (keep 'enabled' bit clear for now)
	 */
	hwc->config = ARCH_PERFMON_EVENTSEL_INT;

	hwc->idx = -1;

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
	} else {
		/*
		 * If we have a PMU initialized but no APIC
		 * interrupts, we cannot sample hardware
		 * events (user-space has to fall back and
		 * sample via a hrtimer based software event):
		 */
		if (!x86_pmu.apic)
			return -EOPNOTSUPP;
	}

	/*
	 * Raw hw_event type provide the config in the hw_event structure
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
	config = x86_pmu.event_map(attr->config);

	if (config == 0)
		return -ENOENT;

	if (config == -1LL)
		return -EINVAL;

	/*
	 * Branch tracing:
	 */
	if ((attr->config == PERF_COUNT_HW_BRANCH_INSTRUCTIONS) &&
	    (hwc->sample_period == 1)) {
		/* BTS is not supported by this architecture. */
		if (!bts_available())
			return -EOPNOTSUPP;

		/* BTS is currently only allowed for user-mode. */
		if (hwc->config & ARCH_PERFMON_EVENTSEL_OS)
			return -EOPNOTSUPP;
	}

	hwc->config |= config;

	return 0;
}

static void p6_pmu_disable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	u64 val;

	if (!cpuc->enabled)
		return;

	cpuc->enabled = 0;
	barrier();

	/* p6 only has one enable register */
	rdmsrl(MSR_P6_EVNTSEL0, val);
	val &= ~ARCH_PERFMON_EVENTSEL0_ENABLE;
	wrmsrl(MSR_P6_EVNTSEL0, val);
}

static void intel_pmu_disable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!cpuc->enabled)
		return;

	cpuc->enabled = 0;
	barrier();

	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0);

	if (test_bit(X86_PMC_IDX_FIXED_BTS, cpuc->active_mask))
		intel_pmu_disable_bts();
}

static void amd_pmu_disable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	int idx;

	if (!cpuc->enabled)
		return;

	cpuc->enabled = 0;
	/*
	 * ensure we write the disable before we start disabling the
	 * events proper, so that amd_pmu_enable_event() does the
	 * right thing.
	 */
	barrier();

	for (idx = 0; idx < x86_pmu.num_events; idx++) {
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

static void p6_pmu_enable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	unsigned long val;

	if (cpuc->enabled)
		return;

	cpuc->enabled = 1;
	barrier();

	/* p6 only has one enable register */
	rdmsrl(MSR_P6_EVNTSEL0, val);
	val |= ARCH_PERFMON_EVENTSEL0_ENABLE;
	wrmsrl(MSR_P6_EVNTSEL0, val);
}

static void intel_pmu_enable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->enabled)
		return;

	cpuc->enabled = 1;
	barrier();

	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, x86_pmu.intel_ctrl);

	if (test_bit(X86_PMC_IDX_FIXED_BTS, cpuc->active_mask)) {
		struct perf_event *event =
			cpuc->events[X86_PMC_IDX_FIXED_BTS];

		if (WARN_ON_ONCE(!event))
			return;

		intel_pmu_enable_bts(event->hw.config);
	}
}

static void amd_pmu_enable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	int idx;

	if (cpuc->enabled)
		return;

	cpuc->enabled = 1;
	barrier();

	for (idx = 0; idx < x86_pmu.num_events; idx++) {
		struct perf_event *event = cpuc->events[idx];
		u64 val;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		val = event->hw.config;
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

static inline void x86_pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	(void)checking_wrmsrl(hwc->config_base + idx,
			      hwc->config | ARCH_PERFMON_EVENTSEL0_ENABLE);
}

static inline void x86_pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	(void)checking_wrmsrl(hwc->config_base + idx, hwc->config);
}

static inline void
intel_pmu_disable_fixed(struct hw_perf_event *hwc, int __idx)
{
	int idx = __idx - X86_PMC_IDX_FIXED;
	u64 ctrl_val, mask;

	mask = 0xfULL << (idx * 4);

	rdmsrl(hwc->config_base, ctrl_val);
	ctrl_val &= ~mask;
	(void)checking_wrmsrl(hwc->config_base, ctrl_val);
}

static inline void
p6_pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	u64 val = P6_NOP_EVENT;

	if (cpuc->enabled)
		val |= ARCH_PERFMON_EVENTSEL0_ENABLE;

	(void)checking_wrmsrl(hwc->config_base + idx, val);
}

static inline void
intel_pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	if (unlikely(idx == X86_PMC_IDX_FIXED_BTS)) {
		intel_pmu_disable_bts();
		return;
	}

	if (unlikely(hwc->config_base == MSR_ARCH_PERFMON_FIXED_CTR_CTRL)) {
		intel_pmu_disable_fixed(hwc, idx);
		return;
	}

	x86_pmu_disable_event(hwc, idx);
}

static inline void
amd_pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	x86_pmu_disable_event(hwc, idx);
}

static DEFINE_PER_CPU(u64 [X86_PMC_IDX_MAX], pmc_prev_left);

/*
 * Set the next IRQ period, based on the hwc->period_left value.
 * To be called with the event disabled in hw:
 */
static int
x86_perf_event_set_period(struct perf_event *event,
			     struct hw_perf_event *hwc, int idx)
{
	s64 left = atomic64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	int err, ret = 0;

	if (idx == X86_PMC_IDX_FIXED_BTS)
		return 0;

	/*
	 * If we are way outside a reasonable range then just skip forward:
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
	 * Quirk: certain CPUs dont like it if just 1 hw_event is left:
	 */
	if (unlikely(left < 2))
		left = 2;

	if (left > x86_pmu.max_period)
		left = x86_pmu.max_period;

	per_cpu(pmc_prev_left[idx], smp_processor_id()) = left;

	/*
	 * The hw event starts counting from this event offset,
	 * mark it to be able to extra future deltas:
	 */
	atomic64_set(&hwc->prev_count, (u64)-left);

	err = checking_wrmsrl(hwc->event_base + idx,
			     (u64)(-left) & x86_pmu.event_mask);

	perf_event_update_userpage(event);

	return ret;
}

static inline void
intel_pmu_enable_fixed(struct hw_perf_event *hwc, int __idx)
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

	/*
	 * ANY bit is supported in v3 and up
	 */
	if (x86_pmu.version > 2 && hwc->config & ARCH_PERFMON_EVENTSEL_ANY)
		bits |= 0x4;

	bits <<= (idx * 4);
	mask = 0xfULL << (idx * 4);

	rdmsrl(hwc->config_base, ctrl_val);
	ctrl_val &= ~mask;
	ctrl_val |= bits;
	err = checking_wrmsrl(hwc->config_base, ctrl_val);
}

static void p6_pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	u64 val;

	val = hwc->config;
	if (cpuc->enabled)
		val |= ARCH_PERFMON_EVENTSEL0_ENABLE;

	(void)checking_wrmsrl(hwc->config_base + idx, val);
}


static void intel_pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	if (unlikely(idx == X86_PMC_IDX_FIXED_BTS)) {
		if (!__get_cpu_var(cpu_hw_events).enabled)
			return;

		intel_pmu_enable_bts(hwc->config);
		return;
	}

	if (unlikely(hwc->config_base == MSR_ARCH_PERFMON_FIXED_CTR_CTRL)) {
		intel_pmu_enable_fixed(hwc, idx);
		return;
	}

	x86_pmu_enable_event(hwc, idx);
}

static void amd_pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->enabled)
		x86_pmu_enable_event(hwc, idx);
}

static int fixed_mode_idx(struct hw_perf_event *hwc)
{
	unsigned int hw_event;

	hw_event = hwc->config & ARCH_PERFMON_EVENT_MASK;

	if (unlikely((hw_event ==
		      x86_pmu.event_map(PERF_COUNT_HW_BRANCH_INSTRUCTIONS)) &&
		     (hwc->sample_period == 1)))
		return X86_PMC_IDX_FIXED_BTS;

	if (!x86_pmu.num_events_fixed)
		return -1;

	/*
	 * fixed counters do not take all possible filters
	 */
	if (hwc->config & ARCH_PERFMON_EVENT_FILTER_MASK)
		return -1;

	if (unlikely(hw_event == x86_pmu.event_map(PERF_COUNT_HW_INSTRUCTIONS)))
		return X86_PMC_IDX_FIXED_INSTRUCTIONS;
	if (unlikely(hw_event == x86_pmu.event_map(PERF_COUNT_HW_CPU_CYCLES)))
		return X86_PMC_IDX_FIXED_CPU_CYCLES;
	if (unlikely(hw_event == x86_pmu.event_map(PERF_COUNT_HW_BUS_CYCLES)))
		return X86_PMC_IDX_FIXED_BUS_CYCLES;

	return -1;
}

/*
 * generic counter allocator: get next free counter
 */
static int
gen_get_event_idx(struct cpu_hw_events *cpuc, struct hw_perf_event *hwc)
{
	int idx;

	idx = find_first_zero_bit(cpuc->used_mask, x86_pmu.num_events);
	return idx == x86_pmu.num_events ? -1 : idx;
}

/*
 * intel-specific counter allocator: check event constraints
 */
static int
intel_get_event_idx(struct cpu_hw_events *cpuc, struct hw_perf_event *hwc)
{
	const struct event_constraint *event_constraint;
	int i, code;

	if (!event_constraints)
		goto skip;

	code = hwc->config & CORE_EVNTSEL_EVENT_MASK;

	for_each_event_constraint(event_constraint, event_constraints) {
		if (code == event_constraint->code) {
			for_each_bit(i, event_constraint->idxmsk, X86_PMC_IDX_MAX) {
				if (!test_and_set_bit(i, cpuc->used_mask))
					return i;
			}
			return -1;
		}
	}
skip:
	return gen_get_event_idx(cpuc, hwc);
}

static int
x86_schedule_event(struct cpu_hw_events *cpuc, struct hw_perf_event *hwc)
{
	int idx;

	idx = fixed_mode_idx(hwc);
	if (idx == X86_PMC_IDX_FIXED_BTS) {
		/* BTS is already occupied. */
		if (test_and_set_bit(idx, cpuc->used_mask))
			return -EAGAIN;

		hwc->config_base	= 0;
		hwc->event_base		= 0;
		hwc->idx		= idx;
	} else if (idx >= 0) {
		/*
		 * Try to get the fixed event, if that is already taken
		 * then try to get a generic event:
		 */
		if (test_and_set_bit(idx, cpuc->used_mask))
			goto try_generic;

		hwc->config_base = MSR_ARCH_PERFMON_FIXED_CTR_CTRL;
		/*
		 * We set it so that event_base + idx in wrmsr/rdmsr maps to
		 * MSR_ARCH_PERFMON_FIXED_CTR0 ... CTR2:
		 */
		hwc->event_base =
			MSR_ARCH_PERFMON_FIXED_CTR0 - X86_PMC_IDX_FIXED;
		hwc->idx = idx;
	} else {
		idx = hwc->idx;
		/* Try to get the previous generic event again */
		if (idx == -1 || test_and_set_bit(idx, cpuc->used_mask)) {
try_generic:
			idx = x86_pmu.get_event_idx(cpuc, hwc);
			if (idx == -1)
				return -EAGAIN;

			set_bit(idx, cpuc->used_mask);
			hwc->idx = idx;
		}
		hwc->config_base = x86_pmu.eventsel;
		hwc->event_base  = x86_pmu.perfctr;
	}

	return idx;
}

/*
 * Find a PMC slot for the freshly enabled / scheduled in event:
 */
static int x86_pmu_enable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	idx = x86_schedule_event(cpuc, hwc);
	if (idx < 0)
		return idx;

	perf_events_lapic_init();

	x86_pmu.disable(hwc, idx);

	cpuc->events[idx] = event;
	set_bit(idx, cpuc->active_mask);

	x86_perf_event_set_period(event, hwc, idx);
	x86_pmu.enable(hwc, idx);

	perf_event_update_userpage(event);

	return 0;
}

static void x86_pmu_unthrottle(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	if (WARN_ON_ONCE(hwc->idx >= X86_PMC_IDX_MAX ||
				cpuc->events[hwc->idx] != event))
		return;

	x86_pmu.enable(hwc, hwc->idx);
}

void perf_event_print_debug(void)
{
	u64 ctrl, status, overflow, pmc_ctrl, pmc_count, prev_left, fixed;
	struct cpu_hw_events *cpuc;
	unsigned long flags;
	int cpu, idx;

	if (!x86_pmu.num_events)
		return;

	local_irq_save(flags);

	cpu = smp_processor_id();
	cpuc = &per_cpu(cpu_hw_events, cpu);

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

	for (idx = 0; idx < x86_pmu.num_events; idx++) {
		rdmsrl(x86_pmu.eventsel + idx, pmc_ctrl);
		rdmsrl(x86_pmu.perfctr  + idx, pmc_count);

		prev_left = per_cpu(pmc_prev_left[idx], cpu);

		pr_info("CPU#%d:   gen-PMC%d ctrl:  %016llx\n",
			cpu, idx, pmc_ctrl);
		pr_info("CPU#%d:   gen-PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
		pr_info("CPU#%d:   gen-PMC%d left:  %016llx\n",
			cpu, idx, prev_left);
	}
	for (idx = 0; idx < x86_pmu.num_events_fixed; idx++) {
		rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR0 + idx, pmc_count);

		pr_info("CPU#%d: fixed-PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
	}
	local_irq_restore(flags);
}

static void intel_pmu_drain_bts_buffer(struct cpu_hw_events *cpuc)
{
	struct debug_store *ds = cpuc->ds;
	struct bts_record {
		u64	from;
		u64	to;
		u64	flags;
	};
	struct perf_event *event = cpuc->events[X86_PMC_IDX_FIXED_BTS];
	struct bts_record *at, *top;
	struct perf_output_handle handle;
	struct perf_event_header header;
	struct perf_sample_data data;
	struct pt_regs regs;

	if (!event)
		return;

	if (!ds)
		return;

	at  = (struct bts_record *)(unsigned long)ds->bts_buffer_base;
	top = (struct bts_record *)(unsigned long)ds->bts_index;

	if (top <= at)
		return;

	ds->bts_index = ds->bts_buffer_base;


	data.period	= event->hw.last_period;
	data.addr	= 0;
	data.raw	= NULL;
	regs.ip		= 0;

	/*
	 * Prepare a generic sample, i.e. fill in the invariant fields.
	 * We will overwrite the from and to address before we output
	 * the sample.
	 */
	perf_prepare_sample(&header, &data, event, &regs);

	if (perf_output_begin(&handle, event,
			      header.size * (top - at), 1, 1))
		return;

	for (; at < top; at++) {
		data.ip		= at->from;
		data.addr	= at->to;

		perf_output_sample(&handle, &header, &data, event);
	}

	perf_output_end(&handle);

	/* There's new data available. */
	event->hw.interrupts++;
	event->pending_kill = POLL_IN;
}

static void x86_pmu_disable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	/*
	 * Must be done before we disable, otherwise the nmi handler
	 * could reenable again:
	 */
	clear_bit(idx, cpuc->active_mask);
	x86_pmu.disable(hwc, idx);

	/*
	 * Make sure the cleared pointer becomes visible before we
	 * (potentially) free the event:
	 */
	barrier();

	/*
	 * Drain the remaining delta count out of a event
	 * that we are disabling:
	 */
	x86_perf_event_update(event, hwc, idx);

	/* Drain the remaining BTS records. */
	if (unlikely(idx == X86_PMC_IDX_FIXED_BTS))
		intel_pmu_drain_bts_buffer(cpuc);

	cpuc->events[idx] = NULL;
	clear_bit(idx, cpuc->used_mask);

	perf_event_update_userpage(event);
}

/*
 * Save and restart an expired event. Called by NMI contexts,
 * so it has to be careful about preempting normal event ops:
 */
static int intel_pmu_save_and_restart(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	int ret;

	x86_perf_event_update(event, hwc, idx);
	ret = x86_perf_event_set_period(event, hwc, idx);

	if (event->state == PERF_EVENT_STATE_ACTIVE)
		intel_pmu_enable_event(hwc, idx);

	return ret;
}

static void intel_pmu_reset(void)
{
	struct debug_store *ds = __get_cpu_var(cpu_hw_events).ds;
	unsigned long flags;
	int idx;

	if (!x86_pmu.num_events)
		return;

	local_irq_save(flags);

	printk("clearing PMU state on CPU#%d\n", smp_processor_id());

	for (idx = 0; idx < x86_pmu.num_events; idx++) {
		checking_wrmsrl(x86_pmu.eventsel + idx, 0ull);
		checking_wrmsrl(x86_pmu.perfctr  + idx, 0ull);
	}
	for (idx = 0; idx < x86_pmu.num_events_fixed; idx++) {
		checking_wrmsrl(MSR_ARCH_PERFMON_FIXED_CTR0 + idx, 0ull);
	}
	if (ds)
		ds->bts_index = ds->bts_buffer_base;

	local_irq_restore(flags);
}

static int p6_pmu_handle_irq(struct pt_regs *regs)
{
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct perf_event *event;
	struct hw_perf_event *hwc;
	int idx, handled = 0;
	u64 val;

	data.addr = 0;
	data.raw = NULL;

	cpuc = &__get_cpu_var(cpu_hw_events);

	for (idx = 0; idx < x86_pmu.num_events; idx++) {
		if (!test_bit(idx, cpuc->active_mask))
			continue;

		event = cpuc->events[idx];
		hwc = &event->hw;

		val = x86_perf_event_update(event, hwc, idx);
		if (val & (1ULL << (x86_pmu.event_bits - 1)))
			continue;

		/*
		 * event overflow
		 */
		handled		= 1;
		data.period	= event->hw.last_period;

		if (!x86_perf_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, 1, &data, regs))
			p6_pmu_disable_event(hwc, idx);
	}

	if (handled)
		inc_irq_stat(apic_perf_irqs);

	return handled;
}

/*
 * This handler is triggered by the local APIC, so the APIC IRQ handling
 * rules apply:
 */
static int intel_pmu_handle_irq(struct pt_regs *regs)
{
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	int bit, loops;
	u64 ack, status;

	data.addr = 0;
	data.raw = NULL;

	cpuc = &__get_cpu_var(cpu_hw_events);

	perf_disable();
	intel_pmu_drain_bts_buffer(cpuc);
	status = intel_pmu_get_status();
	if (!status) {
		perf_enable();
		return 0;
	}

	loops = 0;
again:
	if (++loops > 100) {
		WARN_ONCE(1, "perfevents: irq loop stuck!\n");
		perf_event_print_debug();
		intel_pmu_reset();
		perf_enable();
		return 1;
	}

	inc_irq_stat(apic_perf_irqs);
	ack = status;
	for_each_bit(bit, (unsigned long *)&status, X86_PMC_IDX_MAX) {
		struct perf_event *event = cpuc->events[bit];

		clear_bit(bit, (unsigned long *) &status);
		if (!test_bit(bit, cpuc->active_mask))
			continue;

		if (!intel_pmu_save_and_restart(event))
			continue;

		data.period = event->hw.last_period;

		if (perf_event_overflow(event, 1, &data, regs))
			intel_pmu_disable_event(&event->hw, bit);
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
	struct cpu_hw_events *cpuc;
	struct perf_event *event;
	struct hw_perf_event *hwc;
	int idx, handled = 0;
	u64 val;

	data.addr = 0;
	data.raw = NULL;

	cpuc = &__get_cpu_var(cpu_hw_events);

	for (idx = 0; idx < x86_pmu.num_events; idx++) {
		if (!test_bit(idx, cpuc->active_mask))
			continue;

		event = cpuc->events[idx];
		hwc = &event->hw;

		val = x86_perf_event_update(event, hwc, idx);
		if (val & (1ULL << (x86_pmu.event_bits - 1)))
			continue;

		/*
		 * event overflow
		 */
		handled		= 1;
		data.period	= event->hw.last_period;

		if (!x86_perf_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, 1, &data, regs))
			amd_pmu_disable_event(hwc, idx);
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
	perf_event_do_pending();
	irq_exit();
}

void set_perf_event_pending(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	if (!x86_pmu.apic || !x86_pmu_initialized())
		return;

	apic->send_IPI_self(LOCAL_PENDING_VECTOR);
#endif
}

void perf_events_lapic_init(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	if (!x86_pmu.apic || !x86_pmu_initialized())
		return;

	/*
	 * Always use NMI for PMU
	 */
	apic_write(APIC_LVTPC, APIC_DM_NMI);
#endif
}

static int __kprobes
perf_event_nmi_handler(struct notifier_block *self,
			 unsigned long cmd, void *__args)
{
	struct die_args *args = __args;
	struct pt_regs *regs;

	if (!atomic_read(&active_events))
		return NOTIFY_DONE;

	switch (cmd) {
	case DIE_NMI:
	case DIE_NMI_IPI:
		break;

	default:
		return NOTIFY_DONE;
	}

	regs = args->regs;

#ifdef CONFIG_X86_LOCAL_APIC
	apic_write(APIC_LVTPC, APIC_DM_NMI);
#endif
	/*
	 * Can't rely on the handled return value to say it was our NMI, two
	 * events could trigger 'simultaneously' raising two back-to-back NMIs.
	 *
	 * If the first NMI handles both, the latter will be empty and daze
	 * the CPU.
	 */
	x86_pmu.handle_irq(regs);

	return NOTIFY_STOP;
}

static __read_mostly struct notifier_block perf_event_nmi_notifier = {
	.notifier_call		= perf_event_nmi_handler,
	.next			= NULL,
	.priority		= 1
};

static __initconst struct x86_pmu p6_pmu = {
	.name			= "p6",
	.handle_irq		= p6_pmu_handle_irq,
	.disable_all		= p6_pmu_disable_all,
	.enable_all		= p6_pmu_enable_all,
	.enable			= p6_pmu_enable_event,
	.disable		= p6_pmu_disable_event,
	.eventsel		= MSR_P6_EVNTSEL0,
	.perfctr		= MSR_P6_PERFCTR0,
	.event_map		= p6_pmu_event_map,
	.raw_event		= p6_pmu_raw_event,
	.max_events		= ARRAY_SIZE(p6_perfmon_event_map),
	.apic			= 1,
	.max_period		= (1ULL << 31) - 1,
	.version		= 0,
	.num_events		= 2,
	/*
	 * Events have 40 bits implemented. However they are designed such
	 * that bits [32-39] are sign extensions of bit 31. As such the
	 * effective width of a event for P6-like PMU is 32 bits only.
	 *
	 * See IA-32 Intel Architecture Software developer manual Vol 3B
	 */
	.event_bits		= 32,
	.event_mask		= (1ULL << 32) - 1,
	.get_event_idx		= intel_get_event_idx,
};

static __initconst struct x86_pmu intel_pmu = {
	.name			= "Intel",
	.handle_irq		= intel_pmu_handle_irq,
	.disable_all		= intel_pmu_disable_all,
	.enable_all		= intel_pmu_enable_all,
	.enable			= intel_pmu_enable_event,
	.disable		= intel_pmu_disable_event,
	.eventsel		= MSR_ARCH_PERFMON_EVENTSEL0,
	.perfctr		= MSR_ARCH_PERFMON_PERFCTR0,
	.event_map		= intel_pmu_event_map,
	.raw_event		= intel_pmu_raw_event,
	.max_events		= ARRAY_SIZE(intel_perfmon_event_map),
	.apic			= 1,
	/*
	 * Intel PMCs cannot be accessed sanely above 32 bit width,
	 * so we install an artificial 1<<31 period regardless of
	 * the generic event period:
	 */
	.max_period		= (1ULL << 31) - 1,
	.enable_bts		= intel_pmu_enable_bts,
	.disable_bts		= intel_pmu_disable_bts,
	.get_event_idx		= intel_get_event_idx,
};

static __initconst struct x86_pmu amd_pmu = {
	.name			= "AMD",
	.handle_irq		= amd_pmu_handle_irq,
	.disable_all		= amd_pmu_disable_all,
	.enable_all		= amd_pmu_enable_all,
	.enable			= amd_pmu_enable_event,
	.disable		= amd_pmu_disable_event,
	.eventsel		= MSR_K7_EVNTSEL0,
	.perfctr		= MSR_K7_PERFCTR0,
	.event_map		= amd_pmu_event_map,
	.raw_event		= amd_pmu_raw_event,
	.max_events		= ARRAY_SIZE(amd_perfmon_event_map),
	.num_events		= 4,
	.event_bits		= 48,
	.event_mask		= (1ULL << 48) - 1,
	.apic			= 1,
	/* use highest bit to detect overflow */
	.max_period		= (1ULL << 47) - 1,
	.get_event_idx		= gen_get_event_idx,
};

static __init int p6_pmu_init(void)
{
	switch (boot_cpu_data.x86_model) {
	case 1:
	case 3:  /* Pentium Pro */
	case 5:
	case 6:  /* Pentium II */
	case 7:
	case 8:
	case 11: /* Pentium III */
		event_constraints = intel_p6_event_constraints;
		break;
	case 9:
	case 13:
		/* Pentium M */
		event_constraints = intel_p6_event_constraints;
		break;
	default:
		pr_cont("unsupported p6 CPU model %d ",
			boot_cpu_data.x86_model);
		return -ENODEV;
	}

	x86_pmu = p6_pmu;

	return 0;
}

static __init int intel_pmu_init(void)
{
	union cpuid10_edx edx;
	union cpuid10_eax eax;
	unsigned int unused;
	unsigned int ebx;
	int version;

	if (!cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON)) {
		/* check for P6 processor family */
	   if (boot_cpu_data.x86 == 6) {
		return p6_pmu_init();
	   } else {
		return -ENODEV;
	   }
	}

	/*
	 * Check whether the Architectural PerfMon supports
	 * Branch Misses Retired hw_event or not.
	 */
	cpuid(10, &eax.full, &ebx, &unused, &edx.full);
	if (eax.split.mask_length <= ARCH_PERFMON_BRANCH_MISSES_RETIRED)
		return -ENODEV;

	version = eax.split.version_id;
	if (version < 2)
		return -ENODEV;

	x86_pmu				= intel_pmu;
	x86_pmu.version			= version;
	x86_pmu.num_events		= eax.split.num_events;
	x86_pmu.event_bits		= eax.split.bit_width;
	x86_pmu.event_mask		= (1ULL << eax.split.bit_width) - 1;

	/*
	 * Quirk: v2 perfmon does not report fixed-purpose events, so
	 * assume at least 3 events:
	 */
	x86_pmu.num_events_fixed	= max((int)edx.split.num_events_fixed, 3);

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
		event_constraints = intel_core_event_constraints;
		break;
	default:
	case 26:
		memcpy(hw_cache_event_ids, nehalem_hw_cache_event_ids,
		       sizeof(hw_cache_event_ids));

		event_constraints = intel_nehalem_event_constraints;
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

static __init int amd_pmu_init(void)
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

static void __init pmu_check_apic(void)
{
	if (cpu_has_apic)
		return;

	x86_pmu.apic = 0;
	pr_info("no APIC, boot with the \"lapic\" boot parameter to force-enable it.\n");
	pr_info("no hardware sampling interrupt available.\n");
}

void __init init_hw_perf_events(void)
{
	int err;

	pr_info("Performance Events: ");

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
		pr_cont("no PMU driver, software events only.\n");
		return;
	}

	pmu_check_apic();

	pr_cont("%s PMU driver.\n", x86_pmu.name);

	if (x86_pmu.num_events > X86_PMC_MAX_GENERIC) {
		WARN(1, KERN_ERR "hw perf events %d > max(%d), clipping!",
		     x86_pmu.num_events, X86_PMC_MAX_GENERIC);
		x86_pmu.num_events = X86_PMC_MAX_GENERIC;
	}
	perf_event_mask = (1 << x86_pmu.num_events) - 1;
	perf_max_events = x86_pmu.num_events;

	if (x86_pmu.num_events_fixed > X86_PMC_MAX_FIXED) {
		WARN(1, KERN_ERR "hw perf events fixed %d > max(%d), clipping!",
		     x86_pmu.num_events_fixed, X86_PMC_MAX_FIXED);
		x86_pmu.num_events_fixed = X86_PMC_MAX_FIXED;
	}

	perf_event_mask |=
		((1LL << x86_pmu.num_events_fixed)-1) << X86_PMC_IDX_FIXED;
	x86_pmu.intel_ctrl = perf_event_mask;

	perf_events_lapic_init();
	register_die_notifier(&perf_event_nmi_notifier);

	pr_info("... version:                %d\n",     x86_pmu.version);
	pr_info("... bit width:              %d\n",     x86_pmu.event_bits);
	pr_info("... generic registers:      %d\n",     x86_pmu.num_events);
	pr_info("... value mask:             %016Lx\n", x86_pmu.event_mask);
	pr_info("... max period:             %016Lx\n", x86_pmu.max_period);
	pr_info("... fixed-purpose events:   %d\n",     x86_pmu.num_events_fixed);
	pr_info("... event mask:             %016Lx\n", perf_event_mask);
}

static inline void x86_pmu_read(struct perf_event *event)
{
	x86_perf_event_update(event, &event->hw, event->hw.idx);
}

static const struct pmu pmu = {
	.enable		= x86_pmu_enable,
	.disable	= x86_pmu_disable,
	.read		= x86_pmu_read,
	.unthrottle	= x86_pmu_unthrottle,
};

static int
validate_event(struct cpu_hw_events *cpuc, struct perf_event *event)
{
	struct hw_perf_event fake_event = event->hw;

	if (event->pmu && event->pmu != &pmu)
		return 0;

	return x86_schedule_event(cpuc, &fake_event) >= 0;
}

static int validate_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct cpu_hw_events fake_pmu;

	memset(&fake_pmu, 0, sizeof(fake_pmu));

	if (!validate_event(&fake_pmu, leader))
		return -ENOSPC;

	list_for_each_entry(sibling, &leader->sibling_list, group_entry) {
		if (!validate_event(&fake_pmu, sibling))
			return -ENOSPC;
	}

	if (!validate_event(&fake_pmu, event))
		return -ENOSPC;

	return 0;
}

const struct pmu *hw_perf_event_init(struct perf_event *event)
{
	int err;

	err = __hw_perf_event_init(event);
	if (!err) {
		if (event->group_leader != event)
			err = validate_group(event);
	}
	if (err) {
		if (event->destroy)
			event->destroy(event);
		return ERR_PTR(err);
	}

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

static DEFINE_PER_CPU(struct perf_callchain_entry, pmc_irq_entry);
static DEFINE_PER_CPU(struct perf_callchain_entry, pmc_nmi_entry);
static DEFINE_PER_CPU(int, in_ignored_frame);


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
	per_cpu(in_ignored_frame, smp_processor_id()) =
			x86_is_stack_id(NMI_STACK, name) ||
			x86_is_stack_id(DEBUG_STACK, name);

	return 0;
}

static void backtrace_address(void *data, unsigned long addr, int reliable)
{
	struct perf_callchain_entry *entry = data;

	if (per_cpu(in_ignored_frame, smp_processor_id()))
		return;

	if (reliable)
		callchain_store(entry, addr);
}

static const struct stacktrace_ops backtrace_ops = {
	.warning		= backtrace_warning,
	.warning_symbol		= backtrace_warning_symbol,
	.stack			= backtrace_stack,
	.address		= backtrace_address,
	.walk_stack		= print_context_stack_bp,
};

#include "../dumpstack.h"

static void
perf_callchain_kernel(struct pt_regs *regs, struct perf_callchain_entry *entry)
{
	callchain_store(entry, PERF_CONTEXT_KERNEL);
	callchain_store(entry, regs->ip);

	dump_trace(NULL, regs, NULL, regs->bp, &backtrace_ops, entry);
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
		entry = &__get_cpu_var(pmc_nmi_entry);
	else
		entry = &__get_cpu_var(pmc_irq_entry);

	entry->nr = 0;

	perf_do_callchain(regs, entry);

	return entry;
}

void hw_perf_event_setup_online(int cpu)
{
	init_debug_store_on_cpu(cpu);
}
