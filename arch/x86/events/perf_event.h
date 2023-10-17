/*
 * Performance events x86 architecture header
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2009 Jaswinder Singh Rajput
 *  Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 *  Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra
 *  Copyright (C) 2009 Intel Corporation, <markus.t.metzger@intel.com>
 *  Copyright (C) 2009 Google, Inc., Stephane Eranian
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/perf_event.h>

#include <asm/fpu/xstate.h>
#include <asm/intel_ds.h>
#include <asm/cpu.h>

/* To enable MSR tracing please use the generic trace points. */

/*
 *          |   NHM/WSM    |      SNB     |
 * register -------------------------------
 *          |  HT  | no HT |  HT  | no HT |
 *-----------------------------------------
 * offcore  | core | core  | cpu  | core  |
 * lbr_sel  | core | core  | cpu  | core  |
 * ld_lat   | cpu  | core  | cpu  | core  |
 *-----------------------------------------
 *
 * Given that there is a small number of shared regs,
 * we can pre-allocate their slot in the per-cpu
 * per-core reg tables.
 */
enum extra_reg_type {
	EXTRA_REG_NONE		= -1, /* not used */

	EXTRA_REG_RSP_0		= 0,  /* offcore_response_0 */
	EXTRA_REG_RSP_1		= 1,  /* offcore_response_1 */
	EXTRA_REG_LBR		= 2,  /* lbr_select */
	EXTRA_REG_LDLAT		= 3,  /* ld_lat_threshold */
	EXTRA_REG_FE		= 4,  /* fe_* */
	EXTRA_REG_SNOOP_0	= 5,  /* snoop response 0 */
	EXTRA_REG_SNOOP_1	= 6,  /* snoop response 1 */

	EXTRA_REG_MAX		      /* number of entries needed */
};

struct event_constraint {
	union {
		unsigned long	idxmsk[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
		u64		idxmsk64;
	};
	u64		code;
	u64		cmask;
	int		weight;
	int		overlap;
	int		flags;
	unsigned int	size;
};

static inline bool constraint_match(struct event_constraint *c, u64 ecode)
{
	return ((ecode & c->cmask) - c->code) <= (u64)c->size;
}

#define PERF_ARCH(name, val)	\
	PERF_X86_EVENT_##name = val,

/*
 * struct hw_perf_event.flags flags
 */
enum {
#include "perf_event_flags.h"
};

#undef PERF_ARCH

#define PERF_ARCH(name, val)						\
	static_assert((PERF_X86_EVENT_##name & PERF_EVENT_FLAG_ARCH) ==	\
		      PERF_X86_EVENT_##name);

#include "perf_event_flags.h"

#undef PERF_ARCH

static inline bool is_topdown_count(struct perf_event *event)
{
	return event->hw.flags & PERF_X86_EVENT_TOPDOWN;
}

static inline bool is_metric_event(struct perf_event *event)
{
	u64 config = event->attr.config;

	return ((config & ARCH_PERFMON_EVENTSEL_EVENT) == 0) &&
		((config & INTEL_ARCH_EVENT_MASK) >= INTEL_TD_METRIC_RETIRING)  &&
		((config & INTEL_ARCH_EVENT_MASK) <= INTEL_TD_METRIC_MAX);
}

static inline bool is_slots_event(struct perf_event *event)
{
	return (event->attr.config & INTEL_ARCH_EVENT_MASK) == INTEL_TD_SLOTS;
}

static inline bool is_topdown_event(struct perf_event *event)
{
	return is_metric_event(event) || is_slots_event(event);
}

struct amd_nb {
	int nb_id;  /* NorthBridge id */
	int refcnt; /* reference count */
	struct perf_event *owners[X86_PMC_IDX_MAX];
	struct event_constraint event_constraints[X86_PMC_IDX_MAX];
};

#define PEBS_COUNTER_MASK	((1ULL << MAX_PEBS_EVENTS) - 1)
#define PEBS_PMI_AFTER_EACH_RECORD BIT_ULL(60)
#define PEBS_OUTPUT_OFFSET	61
#define PEBS_OUTPUT_MASK	(3ull << PEBS_OUTPUT_OFFSET)
#define PEBS_OUTPUT_PT		(1ull << PEBS_OUTPUT_OFFSET)
#define PEBS_VIA_PT_MASK	(PEBS_OUTPUT_PT | PEBS_PMI_AFTER_EACH_RECORD)

/*
 * Flags PEBS can handle without an PMI.
 *
 * TID can only be handled by flushing at context switch.
 * REGS_USER can be handled for events limited to ring 3.
 *
 */
#define LARGE_PEBS_FLAGS \
	(PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_ADDR | \
	PERF_SAMPLE_ID | PERF_SAMPLE_CPU | PERF_SAMPLE_STREAM_ID | \
	PERF_SAMPLE_DATA_SRC | PERF_SAMPLE_IDENTIFIER | \
	PERF_SAMPLE_TRANSACTION | PERF_SAMPLE_PHYS_ADDR | \
	PERF_SAMPLE_REGS_INTR | PERF_SAMPLE_REGS_USER | \
	PERF_SAMPLE_PERIOD | PERF_SAMPLE_CODE_PAGE_SIZE | \
	PERF_SAMPLE_WEIGHT_TYPE)

#define PEBS_GP_REGS			\
	((1ULL << PERF_REG_X86_AX)    | \
	 (1ULL << PERF_REG_X86_BX)    | \
	 (1ULL << PERF_REG_X86_CX)    | \
	 (1ULL << PERF_REG_X86_DX)    | \
	 (1ULL << PERF_REG_X86_DI)    | \
	 (1ULL << PERF_REG_X86_SI)    | \
	 (1ULL << PERF_REG_X86_SP)    | \
	 (1ULL << PERF_REG_X86_BP)    | \
	 (1ULL << PERF_REG_X86_IP)    | \
	 (1ULL << PERF_REG_X86_FLAGS) | \
	 (1ULL << PERF_REG_X86_R8)    | \
	 (1ULL << PERF_REG_X86_R9)    | \
	 (1ULL << PERF_REG_X86_R10)   | \
	 (1ULL << PERF_REG_X86_R11)   | \
	 (1ULL << PERF_REG_X86_R12)   | \
	 (1ULL << PERF_REG_X86_R13)   | \
	 (1ULL << PERF_REG_X86_R14)   | \
	 (1ULL << PERF_REG_X86_R15))

/*
 * Per register state.
 */
struct er_account {
	raw_spinlock_t      lock;	/* per-core: protect structure */
	u64                 config;	/* extra MSR config */
	u64                 reg;	/* extra MSR number */
	atomic_t            ref;	/* reference count */
};

/*
 * Per core/cpu state
 *
 * Used to coordinate shared registers between HT threads or
 * among events on a single PMU.
 */
struct intel_shared_regs {
	struct er_account       regs[EXTRA_REG_MAX];
	int                     refcnt;		/* per-core: #HT threads */
	unsigned                core_id;	/* per-core: core id */
};

enum intel_excl_state_type {
	INTEL_EXCL_UNUSED    = 0, /* counter is unused */
	INTEL_EXCL_SHARED    = 1, /* counter can be used by both threads */
	INTEL_EXCL_EXCLUSIVE = 2, /* counter can be used by one thread only */
};

struct intel_excl_states {
	enum intel_excl_state_type state[X86_PMC_IDX_MAX];
	bool sched_started; /* true if scheduling has started */
};

struct intel_excl_cntrs {
	raw_spinlock_t	lock;

	struct intel_excl_states states[2];

	union {
		u16	has_exclusive[2];
		u32	exclusive_present;
	};

	int		refcnt;		/* per-core: #HT threads */
	unsigned	core_id;	/* per-core: core id */
};

struct x86_perf_task_context;
#define MAX_LBR_ENTRIES		32

enum {
	LBR_FORMAT_32		= 0x00,
	LBR_FORMAT_LIP		= 0x01,
	LBR_FORMAT_EIP		= 0x02,
	LBR_FORMAT_EIP_FLAGS	= 0x03,
	LBR_FORMAT_EIP_FLAGS2	= 0x04,
	LBR_FORMAT_INFO		= 0x05,
	LBR_FORMAT_TIME		= 0x06,
	LBR_FORMAT_INFO2	= 0x07,
	LBR_FORMAT_MAX_KNOWN    = LBR_FORMAT_INFO2,
};

enum {
	X86_PERF_KFREE_SHARED = 0,
	X86_PERF_KFREE_EXCL   = 1,
	X86_PERF_KFREE_MAX
};

struct cpu_hw_events {
	/*
	 * Generic x86 PMC bits
	 */
	struct perf_event	*events[X86_PMC_IDX_MAX]; /* in counter order */
	unsigned long		active_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	unsigned long		dirty[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	int			enabled;

	int			n_events; /* the # of events in the below arrays */
	int			n_added;  /* the # last events in the below arrays;
					     they've never been enabled yet */
	int			n_txn;    /* the # last events in the below arrays;
					     added in the current transaction */
	int			n_txn_pair;
	int			n_txn_metric;
	int			assign[X86_PMC_IDX_MAX]; /* event to counter assignment */
	u64			tags[X86_PMC_IDX_MAX];

	struct perf_event	*event_list[X86_PMC_IDX_MAX]; /* in enabled order */
	struct event_constraint	*event_constraint[X86_PMC_IDX_MAX];

	int			n_excl; /* the number of exclusive events */

	unsigned int		txn_flags;
	int			is_fake;

	/*
	 * Intel DebugStore bits
	 */
	struct debug_store	*ds;
	void			*ds_pebs_vaddr;
	void			*ds_bts_vaddr;
	u64			pebs_enabled;
	int			n_pebs;
	int			n_large_pebs;
	int			n_pebs_via_pt;
	int			pebs_output;

	/* Current super set of events hardware configuration */
	u64			pebs_data_cfg;
	u64			active_pebs_data_cfg;
	int			pebs_record_size;

	/* Intel Fixed counter configuration */
	u64			fixed_ctrl_val;
	u64			active_fixed_ctrl_val;

	/*
	 * Intel LBR bits
	 */
	int				lbr_users;
	int				lbr_pebs_users;
	struct perf_branch_stack	lbr_stack;
	struct perf_branch_entry	lbr_entries[MAX_LBR_ENTRIES];
	union {
		struct er_account		*lbr_sel;
		struct er_account		*lbr_ctl;
	};
	u64				br_sel;
	void				*last_task_ctx;
	int				last_log_id;
	int				lbr_select;
	void				*lbr_xsave;

	/*
	 * Intel host/guest exclude bits
	 */
	u64				intel_ctrl_guest_mask;
	u64				intel_ctrl_host_mask;
	struct perf_guest_switch_msr	guest_switch_msrs[X86_PMC_IDX_MAX];

	/*
	 * Intel checkpoint mask
	 */
	u64				intel_cp_status;

	/*
	 * manage shared (per-core, per-cpu) registers
	 * used on Intel NHM/WSM/SNB
	 */
	struct intel_shared_regs	*shared_regs;
	/*
	 * manage exclusive counter access between hyperthread
	 */
	struct event_constraint *constraint_list; /* in enable order */
	struct intel_excl_cntrs		*excl_cntrs;
	int excl_thread_id; /* 0 or 1 */

	/*
	 * SKL TSX_FORCE_ABORT shadow
	 */
	u64				tfa_shadow;

	/*
	 * Perf Metrics
	 */
	/* number of accepted metrics events */
	int				n_metric;

	/*
	 * AMD specific bits
	 */
	struct amd_nb			*amd_nb;
	int				brs_active; /* BRS is enabled */

	/* Inverted mask of bits to clear in the perf_ctr ctrl registers */
	u64				perf_ctr_virt_mask;
	int				n_pair; /* Large increment events */

	void				*kfree_on_online[X86_PERF_KFREE_MAX];

	struct pmu			*pmu;
};

#define __EVENT_CONSTRAINT_RANGE(c, e, n, m, w, o, f) {	\
	{ .idxmsk64 = (n) },		\
	.code = (c),			\
	.size = (e) - (c),		\
	.cmask = (m),			\
	.weight = (w),			\
	.overlap = (o),			\
	.flags = f,			\
}

#define __EVENT_CONSTRAINT(c, n, m, w, o, f) \
	__EVENT_CONSTRAINT_RANGE(c, c, n, m, w, o, f)

#define EVENT_CONSTRAINT(c, n, m)	\
	__EVENT_CONSTRAINT(c, n, m, HWEIGHT(n), 0, 0)

/*
 * The constraint_match() function only works for 'simple' event codes
 * and not for extended (AMD64_EVENTSEL_EVENT) events codes.
 */
#define EVENT_CONSTRAINT_RANGE(c, e, n, m) \
	__EVENT_CONSTRAINT_RANGE(c, e, n, m, HWEIGHT(n), 0, 0)

#define INTEL_EXCLEVT_CONSTRAINT(c, n)	\
	__EVENT_CONSTRAINT(c, n, ARCH_PERFMON_EVENTSEL_EVENT, HWEIGHT(n),\
			   0, PERF_X86_EVENT_EXCL)

/*
 * The overlap flag marks event constraints with overlapping counter
 * masks. This is the case if the counter mask of such an event is not
 * a subset of any other counter mask of a constraint with an equal or
 * higher weight, e.g.:
 *
 *  c_overlaps = EVENT_CONSTRAINT_OVERLAP(0, 0x09, 0);
 *  c_another1 = EVENT_CONSTRAINT(0, 0x07, 0);
 *  c_another2 = EVENT_CONSTRAINT(0, 0x38, 0);
 *
 * The event scheduler may not select the correct counter in the first
 * cycle because it needs to know which subsequent events will be
 * scheduled. It may fail to schedule the events then. So we set the
 * overlap flag for such constraints to give the scheduler a hint which
 * events to select for counter rescheduling.
 *
 * Care must be taken as the rescheduling algorithm is O(n!) which
 * will increase scheduling cycles for an over-committed system
 * dramatically.  The number of such EVENT_CONSTRAINT_OVERLAP() macros
 * and its counter masks must be kept at a minimum.
 */
#define EVENT_CONSTRAINT_OVERLAP(c, n, m)	\
	__EVENT_CONSTRAINT(c, n, m, HWEIGHT(n), 1, 0)

/*
 * Constraint on the Event code.
 */
#define INTEL_EVENT_CONSTRAINT(c, n)	\
	EVENT_CONSTRAINT(c, n, ARCH_PERFMON_EVENTSEL_EVENT)

/*
 * Constraint on a range of Event codes
 */
#define INTEL_EVENT_CONSTRAINT_RANGE(c, e, n)			\
	EVENT_CONSTRAINT_RANGE(c, e, n, ARCH_PERFMON_EVENTSEL_EVENT)

/*
 * Constraint on the Event code + UMask + fixed-mask
 *
 * filter mask to validate fixed counter events.
 * the following filters disqualify for fixed counters:
 *  - inv
 *  - edge
 *  - cnt-mask
 *  - in_tx
 *  - in_tx_checkpointed
 *  The other filters are supported by fixed counters.
 *  The any-thread option is supported starting with v3.
 */
#define FIXED_EVENT_FLAGS (X86_RAW_EVENT_MASK|HSW_IN_TX|HSW_IN_TX_CHECKPOINTED)
#define FIXED_EVENT_CONSTRAINT(c, n)	\
	EVENT_CONSTRAINT(c, (1ULL << (32+n)), FIXED_EVENT_FLAGS)

/*
 * The special metric counters do not actually exist. They are calculated from
 * the combination of the FxCtr3 + MSR_PERF_METRICS.
 *
 * The special metric counters are mapped to a dummy offset for the scheduler.
 * The sharing between multiple users of the same metric without multiplexing
 * is not allowed, even though the hardware supports that in principle.
 */

#define METRIC_EVENT_CONSTRAINT(c, n)					\
	EVENT_CONSTRAINT(c, (1ULL << (INTEL_PMC_IDX_METRIC_BASE + n)),	\
			 INTEL_ARCH_EVENT_MASK)

/*
 * Constraint on the Event code + UMask
 */
#define INTEL_UEVENT_CONSTRAINT(c, n)	\
	EVENT_CONSTRAINT(c, n, INTEL_ARCH_EVENT_MASK)

/* Constraint on specific umask bit only + event */
#define INTEL_UBIT_EVENT_CONSTRAINT(c, n)	\
	EVENT_CONSTRAINT(c, n, ARCH_PERFMON_EVENTSEL_EVENT|(c))

/* Like UEVENT_CONSTRAINT, but match flags too */
#define INTEL_FLAGS_UEVENT_CONSTRAINT(c, n)	\
	EVENT_CONSTRAINT(c, n, INTEL_ARCH_EVENT_MASK|X86_ALL_EVENT_FLAGS)

#define INTEL_EXCLUEVT_CONSTRAINT(c, n)	\
	__EVENT_CONSTRAINT(c, n, INTEL_ARCH_EVENT_MASK, \
			   HWEIGHT(n), 0, PERF_X86_EVENT_EXCL)

#define INTEL_PLD_CONSTRAINT(c, n)	\
	__EVENT_CONSTRAINT(c, n, INTEL_ARCH_EVENT_MASK|X86_ALL_EVENT_FLAGS, \
			   HWEIGHT(n), 0, PERF_X86_EVENT_PEBS_LDLAT)

#define INTEL_PSD_CONSTRAINT(c, n)	\
	__EVENT_CONSTRAINT(c, n, INTEL_ARCH_EVENT_MASK|X86_ALL_EVENT_FLAGS, \
			   HWEIGHT(n), 0, PERF_X86_EVENT_PEBS_STLAT)

#define INTEL_PST_CONSTRAINT(c, n)	\
	__EVENT_CONSTRAINT(c, n, INTEL_ARCH_EVENT_MASK|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, PERF_X86_EVENT_PEBS_ST)

#define INTEL_HYBRID_LAT_CONSTRAINT(c, n)	\
	__EVENT_CONSTRAINT(c, n, INTEL_ARCH_EVENT_MASK|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, PERF_X86_EVENT_PEBS_LAT_HYBRID)

/* Event constraint, but match on all event flags too. */
#define INTEL_FLAGS_EVENT_CONSTRAINT(c, n) \
	EVENT_CONSTRAINT(c, n, ARCH_PERFMON_EVENTSEL_EVENT|X86_ALL_EVENT_FLAGS)

#define INTEL_FLAGS_EVENT_CONSTRAINT_RANGE(c, e, n)			\
	EVENT_CONSTRAINT_RANGE(c, e, n, ARCH_PERFMON_EVENTSEL_EVENT|X86_ALL_EVENT_FLAGS)

/* Check only flags, but allow all event/umask */
#define INTEL_ALL_EVENT_CONSTRAINT(code, n)	\
	EVENT_CONSTRAINT(code, n, X86_ALL_EVENT_FLAGS)

/* Check flags and event code, and set the HSW store flag */
#define INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_ST(code, n) \
	__EVENT_CONSTRAINT(code, n, 			\
			  ARCH_PERFMON_EVENTSEL_EVENT|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, PERF_X86_EVENT_PEBS_ST_HSW)

/* Check flags and event code, and set the HSW load flag */
#define INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_LD(code, n) \
	__EVENT_CONSTRAINT(code, n,			\
			  ARCH_PERFMON_EVENTSEL_EVENT|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, PERF_X86_EVENT_PEBS_LD_HSW)

#define INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_LD_RANGE(code, end, n) \
	__EVENT_CONSTRAINT_RANGE(code, end, n,				\
			  ARCH_PERFMON_EVENTSEL_EVENT|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, PERF_X86_EVENT_PEBS_LD_HSW)

#define INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_XLD(code, n) \
	__EVENT_CONSTRAINT(code, n,			\
			  ARCH_PERFMON_EVENTSEL_EVENT|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, \
			  PERF_X86_EVENT_PEBS_LD_HSW|PERF_X86_EVENT_EXCL)

/* Check flags and event code/umask, and set the HSW store flag */
#define INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(code, n) \
	__EVENT_CONSTRAINT(code, n, 			\
			  INTEL_ARCH_EVENT_MASK|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, PERF_X86_EVENT_PEBS_ST_HSW)

#define INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_XST(code, n) \
	__EVENT_CONSTRAINT(code, n,			\
			  INTEL_ARCH_EVENT_MASK|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, \
			  PERF_X86_EVENT_PEBS_ST_HSW|PERF_X86_EVENT_EXCL)

/* Check flags and event code/umask, and set the HSW load flag */
#define INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(code, n) \
	__EVENT_CONSTRAINT(code, n, 			\
			  INTEL_ARCH_EVENT_MASK|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, PERF_X86_EVENT_PEBS_LD_HSW)

#define INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_XLD(code, n) \
	__EVENT_CONSTRAINT(code, n,			\
			  INTEL_ARCH_EVENT_MASK|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, \
			  PERF_X86_EVENT_PEBS_LD_HSW|PERF_X86_EVENT_EXCL)

/* Check flags and event code/umask, and set the HSW N/A flag */
#define INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_NA(code, n) \
	__EVENT_CONSTRAINT(code, n, 			\
			  INTEL_ARCH_EVENT_MASK|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, PERF_X86_EVENT_PEBS_NA_HSW)


/*
 * We define the end marker as having a weight of -1
 * to enable blacklisting of events using a counter bitmask
 * of zero and thus a weight of zero.
 * The end marker has a weight that cannot possibly be
 * obtained from counting the bits in the bitmask.
 */
#define EVENT_CONSTRAINT_END { .weight = -1 }

/*
 * Check for end marker with weight == -1
 */
#define for_each_event_constraint(e, c)	\
	for ((e) = (c); (e)->weight != -1; (e)++)

/*
 * Extra registers for specific events.
 *
 * Some events need large masks and require external MSRs.
 * Those extra MSRs end up being shared for all events on
 * a PMU and sometimes between PMU of sibling HT threads.
 * In either case, the kernel needs to handle conflicting
 * accesses to those extra, shared, regs. The data structure
 * to manage those registers is stored in cpu_hw_event.
 */
struct extra_reg {
	unsigned int		event;
	unsigned int		msr;
	u64			config_mask;
	u64			valid_mask;
	int			idx;  /* per_xxx->regs[] reg index */
	bool			extra_msr_access;
};

#define EVENT_EXTRA_REG(e, ms, m, vm, i) {	\
	.event = (e),			\
	.msr = (ms),			\
	.config_mask = (m),		\
	.valid_mask = (vm),		\
	.idx = EXTRA_REG_##i,		\
	.extra_msr_access = true,	\
	}

#define INTEL_EVENT_EXTRA_REG(event, msr, vm, idx)	\
	EVENT_EXTRA_REG(event, msr, ARCH_PERFMON_EVENTSEL_EVENT, vm, idx)

#define INTEL_UEVENT_EXTRA_REG(event, msr, vm, idx) \
	EVENT_EXTRA_REG(event, msr, ARCH_PERFMON_EVENTSEL_EVENT | \
			ARCH_PERFMON_EVENTSEL_UMASK, vm, idx)

#define INTEL_UEVENT_PEBS_LDLAT_EXTRA_REG(c) \
	INTEL_UEVENT_EXTRA_REG(c, \
			       MSR_PEBS_LD_LAT_THRESHOLD, \
			       0xffff, \
			       LDLAT)

#define EVENT_EXTRA_END EVENT_EXTRA_REG(0, 0, 0, 0, RSP_0)

union perf_capabilities {
	struct {
		u64	lbr_format:6;
		u64	pebs_trap:1;
		u64	pebs_arch_reg:1;
		u64	pebs_format:4;
		u64	smm_freeze:1;
		/*
		 * PMU supports separate counter range for writing
		 * values > 32bit.
		 */
		u64	full_width_write:1;
		u64     pebs_baseline:1;
		u64	perf_metrics:1;
		u64	pebs_output_pt_available:1;
		u64	pebs_timing_info:1;
		u64	anythread_deprecated:1;
	};
	u64	capabilities;
};

struct x86_pmu_quirk {
	struct x86_pmu_quirk *next;
	void (*func)(void);
};

union x86_pmu_config {
	struct {
		u64 event:8,
		    umask:8,
		    usr:1,
		    os:1,
		    edge:1,
		    pc:1,
		    interrupt:1,
		    __reserved1:1,
		    en:1,
		    inv:1,
		    cmask:8,
		    event2:4,
		    __reserved2:4,
		    go:1,
		    ho:1;
	} bits;
	u64 value;
};

#define X86_CONFIG(args...) ((union x86_pmu_config){.bits = {args}}).value

enum {
	x86_lbr_exclusive_lbr,
	x86_lbr_exclusive_bts,
	x86_lbr_exclusive_pt,
	x86_lbr_exclusive_max,
};

#define PERF_PEBS_DATA_SOURCE_MAX	0x10
#define PERF_PEBS_DATA_SOURCE_MASK	(PERF_PEBS_DATA_SOURCE_MAX - 1)

struct x86_hybrid_pmu {
	struct pmu			pmu;
	const char			*name;
	u8				cpu_type;
	cpumask_t			supported_cpus;
	union perf_capabilities		intel_cap;
	u64				intel_ctrl;
	int				max_pebs_events;
	int				num_counters;
	int				num_counters_fixed;
	struct event_constraint		unconstrained;

	u64				hw_cache_event_ids
					[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX];
	u64				hw_cache_extra_regs
					[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX];
	struct event_constraint		*event_constraints;
	struct event_constraint		*pebs_constraints;
	struct extra_reg		*extra_regs;

	unsigned int			late_ack	:1,
					mid_ack		:1,
					enabled_ack	:1;

	u64				pebs_data_source[PERF_PEBS_DATA_SOURCE_MAX];
};

static __always_inline struct x86_hybrid_pmu *hybrid_pmu(struct pmu *pmu)
{
	return container_of(pmu, struct x86_hybrid_pmu, pmu);
}

extern struct static_key_false perf_is_hybrid;
#define is_hybrid()		static_branch_unlikely(&perf_is_hybrid)

#define hybrid(_pmu, _field)				\
(*({							\
	typeof(&x86_pmu._field) __Fp = &x86_pmu._field;	\
							\
	if (is_hybrid() && (_pmu))			\
		__Fp = &hybrid_pmu(_pmu)->_field;	\
							\
	__Fp;						\
}))

#define hybrid_var(_pmu, _var)				\
(*({							\
	typeof(&_var) __Fp = &_var;			\
							\
	if (is_hybrid() && (_pmu))			\
		__Fp = &hybrid_pmu(_pmu)->_var;		\
							\
	__Fp;						\
}))

#define hybrid_bit(_pmu, _field)			\
({							\
	bool __Fp = x86_pmu._field;			\
							\
	if (is_hybrid() && (_pmu))			\
		__Fp = hybrid_pmu(_pmu)->_field;	\
							\
	__Fp;						\
})

enum hybrid_pmu_type {
	hybrid_big		= 0x40,
	hybrid_small		= 0x20,

	hybrid_big_small	= hybrid_big | hybrid_small,
};

#define X86_HYBRID_PMU_ATOM_IDX		0
#define X86_HYBRID_PMU_CORE_IDX		1

#define X86_HYBRID_NUM_PMUS		2

/*
 * struct x86_pmu - generic x86 pmu
 */
struct x86_pmu {
	/*
	 * Generic x86 PMC bits
	 */
	const char	*name;
	int		version;
	int		(*handle_irq)(struct pt_regs *);
	void		(*disable_all)(void);
	void		(*enable_all)(int added);
	void		(*enable)(struct perf_event *);
	void		(*disable)(struct perf_event *);
	void		(*assign)(struct perf_event *event, int idx);
	void		(*add)(struct perf_event *);
	void		(*del)(struct perf_event *);
	void		(*read)(struct perf_event *event);
	int		(*set_period)(struct perf_event *event);
	u64		(*update)(struct perf_event *event);
	int		(*hw_config)(struct perf_event *event);
	int		(*schedule_events)(struct cpu_hw_events *cpuc, int n, int *assign);
	unsigned	eventsel;
	unsigned	perfctr;
	int		(*addr_offset)(int index, bool eventsel);
	int		(*rdpmc_index)(int index);
	u64		(*event_map)(int);
	int		max_events;
	int		num_counters;
	int		num_counters_fixed;
	int		cntval_bits;
	u64		cntval_mask;
	union {
			unsigned long events_maskl;
			unsigned long events_mask[BITS_TO_LONGS(ARCH_PERFMON_EVENTS_COUNT)];
	};
	int		events_mask_len;
	int		apic;
	u64		max_period;
	struct event_constraint *
			(*get_event_constraints)(struct cpu_hw_events *cpuc,
						 int idx,
						 struct perf_event *event);

	void		(*put_event_constraints)(struct cpu_hw_events *cpuc,
						 struct perf_event *event);

	void		(*start_scheduling)(struct cpu_hw_events *cpuc);

	void		(*commit_scheduling)(struct cpu_hw_events *cpuc, int idx, int cntr);

	void		(*stop_scheduling)(struct cpu_hw_events *cpuc);

	struct event_constraint *event_constraints;
	struct x86_pmu_quirk *quirks;
	void		(*limit_period)(struct perf_event *event, s64 *l);

	/* PMI handler bits */
	unsigned int	late_ack		:1,
			mid_ack			:1,
			enabled_ack		:1;
	/*
	 * sysfs attrs
	 */
	int		attr_rdpmc_broken;
	int		attr_rdpmc;
	struct attribute **format_attrs;

	ssize_t		(*events_sysfs_show)(char *page, u64 config);
	const struct attribute_group **attr_update;

	unsigned long	attr_freeze_on_smi;

	/*
	 * CPU Hotplug hooks
	 */
	int		(*cpu_prepare)(int cpu);
	void		(*cpu_starting)(int cpu);
	void		(*cpu_dying)(int cpu);
	void		(*cpu_dead)(int cpu);

	void		(*check_microcode)(void);
	void		(*sched_task)(struct perf_event_pmu_context *pmu_ctx,
				      bool sched_in);

	/*
	 * Intel Arch Perfmon v2+
	 */
	u64			intel_ctrl;
	union perf_capabilities intel_cap;

	/*
	 * Intel DebugStore bits
	 */
	unsigned int	bts			:1,
			bts_active		:1,
			pebs			:1,
			pebs_active		:1,
			pebs_broken		:1,
			pebs_prec_dist		:1,
			pebs_no_tlb		:1,
			pebs_no_isolation	:1,
			pebs_block		:1,
			pebs_ept		:1;
	int		pebs_record_size;
	int		pebs_buffer_size;
	int		max_pebs_events;
	void		(*drain_pebs)(struct pt_regs *regs, struct perf_sample_data *data);
	struct event_constraint *pebs_constraints;
	void		(*pebs_aliases)(struct perf_event *event);
	u64		(*pebs_latency_data)(struct perf_event *event, u64 status);
	unsigned long	large_pebs_flags;
	u64		rtm_abort_event;
	u64		pebs_capable;

	/*
	 * Intel LBR
	 */
	unsigned int	lbr_tos, lbr_from, lbr_to,
			lbr_info, lbr_nr;	   /* LBR base regs and size */
	union {
		u64	lbr_sel_mask;		   /* LBR_SELECT valid bits */
		u64	lbr_ctl_mask;		   /* LBR_CTL valid bits */
	};
	union {
		const int	*lbr_sel_map;	   /* lbr_select mappings */
		int		*lbr_ctl_map;	   /* LBR_CTL mappings */
	};
	bool		lbr_double_abort;	   /* duplicated lbr aborts */
	bool		lbr_pt_coexist;		   /* (LBR|BTS) may coexist with PT */

	unsigned int	lbr_has_info:1;
	unsigned int	lbr_has_tsx:1;
	unsigned int	lbr_from_flags:1;
	unsigned int	lbr_to_cycles:1;

	/*
	 * Intel Architectural LBR CPUID Enumeration
	 */
	unsigned int	lbr_depth_mask:8;
	unsigned int	lbr_deep_c_reset:1;
	unsigned int	lbr_lip:1;
	unsigned int	lbr_cpl:1;
	unsigned int	lbr_filter:1;
	unsigned int	lbr_call_stack:1;
	unsigned int	lbr_mispred:1;
	unsigned int	lbr_timed_lbr:1;
	unsigned int	lbr_br_type:1;

	void		(*lbr_reset)(void);
	void		(*lbr_read)(struct cpu_hw_events *cpuc);
	void		(*lbr_save)(void *ctx);
	void		(*lbr_restore)(void *ctx);

	/*
	 * Intel PT/LBR/BTS are exclusive
	 */
	atomic_t	lbr_exclusive[x86_lbr_exclusive_max];

	/*
	 * Intel perf metrics
	 */
	int		num_topdown_events;

	/*
	 * perf task context (i.e. struct perf_event_pmu_context::task_ctx_data)
	 * switch helper to bridge calls from perf/core to perf/x86.
	 * See struct pmu::swap_task_ctx() usage for examples;
	 */
	void		(*swap_task_ctx)(struct perf_event_pmu_context *prev_epc,
					 struct perf_event_pmu_context *next_epc);

	/*
	 * AMD bits
	 */
	unsigned int	amd_nb_constraints : 1;
	u64		perf_ctr_pair_en;

	/*
	 * Extra registers for events
	 */
	struct extra_reg *extra_regs;
	unsigned int flags;

	/*
	 * Intel host/guest support (KVM)
	 */
	struct perf_guest_switch_msr *(*guest_get_msrs)(int *nr, void *data);

	/*
	 * Check period value for PERF_EVENT_IOC_PERIOD ioctl.
	 */
	int (*check_period) (struct perf_event *event, u64 period);

	int (*aux_output_match) (struct perf_event *event);

	void (*filter)(struct pmu *pmu, int cpu, bool *ret);
	/*
	 * Hybrid support
	 *
	 * Most PMU capabilities are the same among different hybrid PMUs.
	 * The global x86_pmu saves the architecture capabilities, which
	 * are available for all PMUs. The hybrid_pmu only includes the
	 * unique capabilities.
	 */
	int				num_hybrid_pmus;
	struct x86_hybrid_pmu		*hybrid_pmu;
	u8 (*get_hybrid_cpu_type)	(void);
};

struct x86_perf_task_context_opt {
	int lbr_callstack_users;
	int lbr_stack_state;
	int log_id;
};

struct x86_perf_task_context {
	u64 lbr_sel;
	int tos;
	int valid_lbrs;
	struct x86_perf_task_context_opt opt;
	struct lbr_entry lbr[MAX_LBR_ENTRIES];
};

struct x86_perf_task_context_arch_lbr {
	struct x86_perf_task_context_opt opt;
	struct lbr_entry entries[];
};

/*
 * Add padding to guarantee the 64-byte alignment of the state buffer.
 *
 * The structure is dynamically allocated. The size of the LBR state may vary
 * based on the number of LBR registers.
 *
 * Do not put anything after the LBR state.
 */
struct x86_perf_task_context_arch_lbr_xsave {
	struct x86_perf_task_context_opt		opt;

	union {
		struct xregs_state			xsave;
		struct {
			struct fxregs_state		i387;
			struct xstate_header		header;
			struct arch_lbr_state		lbr;
		} __attribute__ ((packed, aligned (XSAVE_ALIGNMENT)));
	};
};

#define x86_add_quirk(func_)						\
do {									\
	static struct x86_pmu_quirk __quirk __initdata = {		\
		.func = func_,						\
	};								\
	__quirk.next = x86_pmu.quirks;					\
	x86_pmu.quirks = &__quirk;					\
} while (0)

/*
 * x86_pmu flags
 */
#define PMU_FL_NO_HT_SHARING	0x1 /* no hyper-threading resource sharing */
#define PMU_FL_HAS_RSP_1	0x2 /* has 2 equivalent offcore_rsp regs   */
#define PMU_FL_EXCL_CNTRS	0x4 /* has exclusive counter requirements  */
#define PMU_FL_EXCL_ENABLED	0x8 /* exclusive counter active */
#define PMU_FL_PEBS_ALL		0x10 /* all events are valid PEBS events */
#define PMU_FL_TFA		0x20 /* deal with TSX force abort */
#define PMU_FL_PAIR		0x40 /* merge counters for large incr. events */
#define PMU_FL_INSTR_LATENCY	0x80 /* Support Instruction Latency in PEBS Memory Info Record */
#define PMU_FL_MEM_LOADS_AUX	0x100 /* Require an auxiliary event for the complete memory info */
#define PMU_FL_RETIRE_LATENCY	0x200 /* Support Retire Latency in PEBS */

#define EVENT_VAR(_id)  event_attr_##_id
#define EVENT_PTR(_id) &event_attr_##_id.attr.attr

#define EVENT_ATTR(_name, _id)						\
static struct perf_pmu_events_attr EVENT_VAR(_id) = {			\
	.attr		= __ATTR(_name, 0444, events_sysfs_show, NULL),	\
	.id		= PERF_COUNT_HW_##_id,				\
	.event_str	= NULL,						\
};

#define EVENT_ATTR_STR(_name, v, str)					\
static struct perf_pmu_events_attr event_attr_##v = {			\
	.attr		= __ATTR(_name, 0444, events_sysfs_show, NULL),	\
	.id		= 0,						\
	.event_str	= str,						\
};

#define EVENT_ATTR_STR_HT(_name, v, noht, ht)				\
static struct perf_pmu_events_ht_attr event_attr_##v = {		\
	.attr		= __ATTR(_name, 0444, events_ht_sysfs_show, NULL),\
	.id		= 0,						\
	.event_str_noht	= noht,						\
	.event_str_ht	= ht,						\
}

#define EVENT_ATTR_STR_HYBRID(_name, v, str, _pmu)			\
static struct perf_pmu_events_hybrid_attr event_attr_##v = {		\
	.attr		= __ATTR(_name, 0444, events_hybrid_sysfs_show, NULL),\
	.id		= 0,						\
	.event_str	= str,						\
	.pmu_type	= _pmu,						\
}

#define FORMAT_HYBRID_PTR(_id) (&format_attr_hybrid_##_id.attr.attr)

#define FORMAT_ATTR_HYBRID(_name, _pmu)					\
static struct perf_pmu_format_hybrid_attr format_attr_hybrid_##_name = {\
	.attr		= __ATTR_RO(_name),				\
	.pmu_type	= _pmu,						\
}

struct pmu *x86_get_pmu(unsigned int cpu);
extern struct x86_pmu x86_pmu __read_mostly;

DECLARE_STATIC_CALL(x86_pmu_set_period, *x86_pmu.set_period);
DECLARE_STATIC_CALL(x86_pmu_update,     *x86_pmu.update);

static __always_inline struct x86_perf_task_context_opt *task_context_opt(void *ctx)
{
	if (static_cpu_has(X86_FEATURE_ARCH_LBR))
		return &((struct x86_perf_task_context_arch_lbr *)ctx)->opt;

	return &((struct x86_perf_task_context *)ctx)->opt;
}

static inline bool x86_pmu_has_lbr_callstack(void)
{
	return  x86_pmu.lbr_sel_map &&
		x86_pmu.lbr_sel_map[PERF_SAMPLE_BRANCH_CALL_STACK_SHIFT] > 0;
}

DECLARE_PER_CPU(struct cpu_hw_events, cpu_hw_events);
DECLARE_PER_CPU(u64 [X86_PMC_IDX_MAX], pmc_prev_left);

int x86_perf_event_set_period(struct perf_event *event);

/*
 * Generalized hw caching related hw_event table, filled
 * in on a per model basis. A value of 0 means
 * 'not supported', -1 means 'hw_event makes no sense on
 * this CPU', any other value means the raw hw_event
 * ID.
 */

#define C(x) PERF_COUNT_HW_CACHE_##x

extern u64 __read_mostly hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX];
extern u64 __read_mostly hw_cache_extra_regs
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX];

u64 x86_perf_event_update(struct perf_event *event);

static inline unsigned int x86_pmu_config_addr(int index)
{
	return x86_pmu.eventsel + (x86_pmu.addr_offset ?
				   x86_pmu.addr_offset(index, true) : index);
}

static inline unsigned int x86_pmu_event_addr(int index)
{
	return x86_pmu.perfctr + (x86_pmu.addr_offset ?
				  x86_pmu.addr_offset(index, false) : index);
}

static inline int x86_pmu_rdpmc_index(int index)
{
	return x86_pmu.rdpmc_index ? x86_pmu.rdpmc_index(index) : index;
}

bool check_hw_exists(struct pmu *pmu, int num_counters,
		     int num_counters_fixed);

int x86_add_exclusive(unsigned int what);

void x86_del_exclusive(unsigned int what);

int x86_reserve_hardware(void);

void x86_release_hardware(void);

int x86_pmu_max_precise(void);

void hw_perf_lbr_event_destroy(struct perf_event *event);

int x86_setup_perfctr(struct perf_event *event);

int x86_pmu_hw_config(struct perf_event *event);

void x86_pmu_disable_all(void);

static inline bool has_amd_brs(struct hw_perf_event *hwc)
{
	return hwc->flags & PERF_X86_EVENT_AMD_BRS;
}

static inline bool is_counter_pair(struct hw_perf_event *hwc)
{
	return hwc->flags & PERF_X86_EVENT_PAIR;
}

static inline void __x86_pmu_enable_event(struct hw_perf_event *hwc,
					  u64 enable_mask)
{
	u64 disable_mask = __this_cpu_read(cpu_hw_events.perf_ctr_virt_mask);

	if (hwc->extra_reg.reg)
		wrmsrl(hwc->extra_reg.reg, hwc->extra_reg.config);

	/*
	 * Add enabled Merge event on next counter
	 * if large increment event being enabled on this counter
	 */
	if (is_counter_pair(hwc))
		wrmsrl(x86_pmu_config_addr(hwc->idx + 1), x86_pmu.perf_ctr_pair_en);

	wrmsrl(hwc->config_base, (hwc->config | enable_mask) & ~disable_mask);
}

void x86_pmu_enable_all(int added);

int perf_assign_events(struct event_constraint **constraints, int n,
			int wmin, int wmax, int gpmax, int *assign);
int x86_schedule_events(struct cpu_hw_events *cpuc, int n, int *assign);

void x86_pmu_stop(struct perf_event *event, int flags);

static inline void x86_pmu_disable_event(struct perf_event *event)
{
	u64 disable_mask = __this_cpu_read(cpu_hw_events.perf_ctr_virt_mask);
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config & ~disable_mask);

	if (is_counter_pair(hwc))
		wrmsrl(x86_pmu_config_addr(hwc->idx + 1), 0);
}

void x86_pmu_enable_event(struct perf_event *event);

int x86_pmu_handle_irq(struct pt_regs *regs);

void x86_pmu_show_pmu_cap(int num_counters, int num_counters_fixed,
			  u64 intel_ctrl);

extern struct event_constraint emptyconstraint;

extern struct event_constraint unconstrained;

static inline bool kernel_ip(unsigned long ip)
{
#ifdef CONFIG_X86_32
	return ip > PAGE_OFFSET;
#else
	return (long)ip < 0;
#endif
}

/*
 * Not all PMUs provide the right context information to place the reported IP
 * into full context. Specifically segment registers are typically not
 * supplied.
 *
 * Assuming the address is a linear address (it is for IBS), we fake the CS and
 * vm86 mode using the known zero-based code segment and 'fix up' the registers
 * to reflect this.
 *
 * Intel PEBS/LBR appear to typically provide the effective address, nothing
 * much we can do about that but pray and treat it like a linear address.
 */
static inline void set_linear_ip(struct pt_regs *regs, unsigned long ip)
{
	regs->cs = kernel_ip(ip) ? __KERNEL_CS : __USER_CS;
	if (regs->flags & X86_VM_MASK)
		regs->flags ^= (PERF_EFLAGS_VM | X86_VM_MASK);
	regs->ip = ip;
}

/*
 * x86control flow change classification
 * x86control flow changes include branches, interrupts, traps, faults
 */
enum {
	X86_BR_NONE		= 0,      /* unknown */

	X86_BR_USER		= 1 << 0, /* branch target is user */
	X86_BR_KERNEL		= 1 << 1, /* branch target is kernel */

	X86_BR_CALL		= 1 << 2, /* call */
	X86_BR_RET		= 1 << 3, /* return */
	X86_BR_SYSCALL		= 1 << 4, /* syscall */
	X86_BR_SYSRET		= 1 << 5, /* syscall return */
	X86_BR_INT		= 1 << 6, /* sw interrupt */
	X86_BR_IRET		= 1 << 7, /* return from interrupt */
	X86_BR_JCC		= 1 << 8, /* conditional */
	X86_BR_JMP		= 1 << 9, /* jump */
	X86_BR_IRQ		= 1 << 10,/* hw interrupt or trap or fault */
	X86_BR_IND_CALL		= 1 << 11,/* indirect calls */
	X86_BR_ABORT		= 1 << 12,/* transaction abort */
	X86_BR_IN_TX		= 1 << 13,/* in transaction */
	X86_BR_NO_TX		= 1 << 14,/* not in transaction */
	X86_BR_ZERO_CALL	= 1 << 15,/* zero length call */
	X86_BR_CALL_STACK	= 1 << 16,/* call stack */
	X86_BR_IND_JMP		= 1 << 17,/* indirect jump */

	X86_BR_TYPE_SAVE	= 1 << 18,/* indicate to save branch type */

};

#define X86_BR_PLM (X86_BR_USER | X86_BR_KERNEL)
#define X86_BR_ANYTX (X86_BR_NO_TX | X86_BR_IN_TX)

#define X86_BR_ANY       \
	(X86_BR_CALL    |\
	 X86_BR_RET     |\
	 X86_BR_SYSCALL |\
	 X86_BR_SYSRET  |\
	 X86_BR_INT     |\
	 X86_BR_IRET    |\
	 X86_BR_JCC     |\
	 X86_BR_JMP	 |\
	 X86_BR_IRQ	 |\
	 X86_BR_ABORT	 |\
	 X86_BR_IND_CALL |\
	 X86_BR_IND_JMP  |\
	 X86_BR_ZERO_CALL)

#define X86_BR_ALL (X86_BR_PLM | X86_BR_ANY)

#define X86_BR_ANY_CALL		 \
	(X86_BR_CALL		|\
	 X86_BR_IND_CALL	|\
	 X86_BR_ZERO_CALL	|\
	 X86_BR_SYSCALL		|\
	 X86_BR_IRQ		|\
	 X86_BR_INT)

int common_branch_type(int type);
int branch_type(unsigned long from, unsigned long to, int abort);
int branch_type_fused(unsigned long from, unsigned long to, int abort,
		      int *offset);

ssize_t x86_event_sysfs_show(char *page, u64 config, u64 event);
ssize_t intel_event_sysfs_show(char *page, u64 config);

ssize_t events_sysfs_show(struct device *dev, struct device_attribute *attr,
			  char *page);
ssize_t events_ht_sysfs_show(struct device *dev, struct device_attribute *attr,
			  char *page);
ssize_t events_hybrid_sysfs_show(struct device *dev,
				 struct device_attribute *attr,
				 char *page);

static inline bool fixed_counter_disabled(int i, struct pmu *pmu)
{
	u64 intel_ctrl = hybrid(pmu, intel_ctrl);

	return !(intel_ctrl >> (i + INTEL_PMC_IDX_FIXED));
}

#ifdef CONFIG_CPU_SUP_AMD

int amd_pmu_init(void);

int amd_pmu_lbr_init(void);
void amd_pmu_lbr_reset(void);
void amd_pmu_lbr_read(void);
void amd_pmu_lbr_add(struct perf_event *event);
void amd_pmu_lbr_del(struct perf_event *event);
void amd_pmu_lbr_sched_task(struct perf_event_pmu_context *pmu_ctx, bool sched_in);
void amd_pmu_lbr_enable_all(void);
void amd_pmu_lbr_disable_all(void);
int amd_pmu_lbr_hw_config(struct perf_event *event);

#ifdef CONFIG_PERF_EVENTS_AMD_BRS

#define AMD_FAM19H_BRS_EVENT 0xc4 /* RETIRED_TAKEN_BRANCH_INSTRUCTIONS */

int amd_brs_init(void);
void amd_brs_disable(void);
void amd_brs_enable(void);
void amd_brs_enable_all(void);
void amd_brs_disable_all(void);
void amd_brs_drain(void);
void amd_brs_lopwr_init(void);
int amd_brs_hw_config(struct perf_event *event);
void amd_brs_reset(void);

static inline void amd_pmu_brs_add(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	perf_sched_cb_inc(event->pmu);
	cpuc->lbr_users++;
	/*
	 * No need to reset BRS because it is reset
	 * on brs_enable() and it is saturating
	 */
}

static inline void amd_pmu_brs_del(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	cpuc->lbr_users--;
	WARN_ON_ONCE(cpuc->lbr_users < 0);

	perf_sched_cb_dec(event->pmu);
}

void amd_pmu_brs_sched_task(struct perf_event_pmu_context *pmu_ctx, bool sched_in);
#else
static inline int amd_brs_init(void)
{
	return 0;
}
static inline void amd_brs_disable(void) {}
static inline void amd_brs_enable(void) {}
static inline void amd_brs_drain(void) {}
static inline void amd_brs_lopwr_init(void) {}
static inline void amd_brs_disable_all(void) {}
static inline int amd_brs_hw_config(struct perf_event *event)
{
	return 0;
}
static inline void amd_brs_reset(void) {}

static inline void amd_pmu_brs_add(struct perf_event *event)
{
}

static inline void amd_pmu_brs_del(struct perf_event *event)
{
}

static inline void amd_pmu_brs_sched_task(struct perf_event_pmu_context *pmu_ctx, bool sched_in)
{
}

static inline void amd_brs_enable_all(void)
{
}

#endif

#else /* CONFIG_CPU_SUP_AMD */

static inline int amd_pmu_init(void)
{
	return 0;
}

static inline int amd_brs_init(void)
{
	return -EOPNOTSUPP;
}

static inline void amd_brs_drain(void)
{
}

static inline void amd_brs_enable_all(void)
{
}

static inline void amd_brs_disable_all(void)
{
}
#endif /* CONFIG_CPU_SUP_AMD */

static inline int is_pebs_pt(struct perf_event *event)
{
	return !!(event->hw.flags & PERF_X86_EVENT_PEBS_VIA_PT);
}

#ifdef CONFIG_CPU_SUP_INTEL

static inline bool intel_pmu_has_bts_period(struct perf_event *event, u64 period)
{
	struct hw_perf_event *hwc = &event->hw;
	unsigned int hw_event, bts_event;

	if (event->attr.freq)
		return false;

	hw_event = hwc->config & INTEL_ARCH_EVENT_MASK;
	bts_event = x86_pmu.event_map(PERF_COUNT_HW_BRANCH_INSTRUCTIONS);

	return hw_event == bts_event && period == 1;
}

static inline bool intel_pmu_has_bts(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	return intel_pmu_has_bts_period(event, hwc->sample_period);
}

static __always_inline void __intel_pmu_pebs_disable_all(void)
{
	wrmsrl(MSR_IA32_PEBS_ENABLE, 0);
}

static __always_inline void __intel_pmu_arch_lbr_disable(void)
{
	wrmsrl(MSR_ARCH_LBR_CTL, 0);
}

static __always_inline void __intel_pmu_lbr_disable(void)
{
	u64 debugctl;

	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	debugctl &= ~(DEBUGCTLMSR_LBR | DEBUGCTLMSR_FREEZE_LBRS_ON_PMI);
	wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
}

int intel_pmu_save_and_restart(struct perf_event *event);

struct event_constraint *
x86_get_event_constraints(struct cpu_hw_events *cpuc, int idx,
			  struct perf_event *event);

extern int intel_cpuc_prepare(struct cpu_hw_events *cpuc, int cpu);
extern void intel_cpuc_finish(struct cpu_hw_events *cpuc);

int intel_pmu_init(void);

void init_debug_store_on_cpu(int cpu);

void fini_debug_store_on_cpu(int cpu);

void release_ds_buffers(void);

void reserve_ds_buffers(void);

void release_lbr_buffers(void);

void reserve_lbr_buffers(void);

extern struct event_constraint bts_constraint;
extern struct event_constraint vlbr_constraint;

void intel_pmu_enable_bts(u64 config);

void intel_pmu_disable_bts(void);

int intel_pmu_drain_bts_buffer(void);

u64 adl_latency_data_small(struct perf_event *event, u64 status);

u64 mtl_latency_data_small(struct perf_event *event, u64 status);

extern struct event_constraint intel_core2_pebs_event_constraints[];

extern struct event_constraint intel_atom_pebs_event_constraints[];

extern struct event_constraint intel_slm_pebs_event_constraints[];

extern struct event_constraint intel_glm_pebs_event_constraints[];

extern struct event_constraint intel_glp_pebs_event_constraints[];

extern struct event_constraint intel_grt_pebs_event_constraints[];

extern struct event_constraint intel_nehalem_pebs_event_constraints[];

extern struct event_constraint intel_westmere_pebs_event_constraints[];

extern struct event_constraint intel_snb_pebs_event_constraints[];

extern struct event_constraint intel_ivb_pebs_event_constraints[];

extern struct event_constraint intel_hsw_pebs_event_constraints[];

extern struct event_constraint intel_bdw_pebs_event_constraints[];

extern struct event_constraint intel_skl_pebs_event_constraints[];

extern struct event_constraint intel_icl_pebs_event_constraints[];

extern struct event_constraint intel_spr_pebs_event_constraints[];

struct event_constraint *intel_pebs_constraints(struct perf_event *event);

void intel_pmu_pebs_add(struct perf_event *event);

void intel_pmu_pebs_del(struct perf_event *event);

void intel_pmu_pebs_enable(struct perf_event *event);

void intel_pmu_pebs_disable(struct perf_event *event);

void intel_pmu_pebs_enable_all(void);

void intel_pmu_pebs_disable_all(void);

void intel_pmu_pebs_sched_task(struct perf_event_pmu_context *pmu_ctx, bool sched_in);

void intel_pmu_auto_reload_read(struct perf_event *event);

void intel_pmu_store_pebs_lbrs(struct lbr_entry *lbr);

void intel_ds_init(void);

void intel_pmu_lbr_swap_task_ctx(struct perf_event_pmu_context *prev_epc,
				 struct perf_event_pmu_context *next_epc);

void intel_pmu_lbr_sched_task(struct perf_event_pmu_context *pmu_ctx, bool sched_in);

u64 lbr_from_signext_quirk_wr(u64 val);

void intel_pmu_lbr_reset(void);

void intel_pmu_lbr_reset_32(void);

void intel_pmu_lbr_reset_64(void);

void intel_pmu_lbr_add(struct perf_event *event);

void intel_pmu_lbr_del(struct perf_event *event);

void intel_pmu_lbr_enable_all(bool pmi);

void intel_pmu_lbr_disable_all(void);

void intel_pmu_lbr_read(void);

void intel_pmu_lbr_read_32(struct cpu_hw_events *cpuc);

void intel_pmu_lbr_read_64(struct cpu_hw_events *cpuc);

void intel_pmu_lbr_save(void *ctx);

void intel_pmu_lbr_restore(void *ctx);

void intel_pmu_lbr_init_core(void);

void intel_pmu_lbr_init_nhm(void);

void intel_pmu_lbr_init_atom(void);

void intel_pmu_lbr_init_slm(void);

void intel_pmu_lbr_init_snb(void);

void intel_pmu_lbr_init_hsw(void);

void intel_pmu_lbr_init_skl(void);

void intel_pmu_lbr_init_knl(void);

void intel_pmu_lbr_init(void);

void intel_pmu_arch_lbr_init(void);

void intel_pmu_pebs_data_source_nhm(void);

void intel_pmu_pebs_data_source_skl(bool pmem);

void intel_pmu_pebs_data_source_adl(void);

void intel_pmu_pebs_data_source_grt(void);

void intel_pmu_pebs_data_source_mtl(void);

void intel_pmu_pebs_data_source_cmt(void);

int intel_pmu_setup_lbr_filter(struct perf_event *event);

void intel_pt_interrupt(void);

int intel_bts_interrupt(void);

void intel_bts_enable_local(void);

void intel_bts_disable_local(void);

int p4_pmu_init(void);

int p6_pmu_init(void);

int knc_pmu_init(void);

static inline int is_ht_workaround_enabled(void)
{
	return !!(x86_pmu.flags & PMU_FL_EXCL_ENABLED);
}

#else /* CONFIG_CPU_SUP_INTEL */

static inline void reserve_ds_buffers(void)
{
}

static inline void release_ds_buffers(void)
{
}

static inline void release_lbr_buffers(void)
{
}

static inline void reserve_lbr_buffers(void)
{
}

static inline int intel_pmu_init(void)
{
	return 0;
}

static inline int intel_cpuc_prepare(struct cpu_hw_events *cpuc, int cpu)
{
	return 0;
}

static inline void intel_cpuc_finish(struct cpu_hw_events *cpuc)
{
}

static inline int is_ht_workaround_enabled(void)
{
	return 0;
}
#endif /* CONFIG_CPU_SUP_INTEL */

#if ((defined CONFIG_CPU_SUP_CENTAUR) || (defined CONFIG_CPU_SUP_ZHAOXIN))
int zhaoxin_pmu_init(void);
#else
static inline int zhaoxin_pmu_init(void)
{
	return 0;
}
#endif /*CONFIG_CPU_SUP_CENTAUR or CONFIG_CPU_SUP_ZHAOXIN*/
