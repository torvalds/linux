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

#include <asm/intel_ds.h>

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
	EXTRA_REG_NONE  = -1,	/* not used */

	EXTRA_REG_RSP_0 = 0,	/* offcore_response_0 */
	EXTRA_REG_RSP_1 = 1,	/* offcore_response_1 */
	EXTRA_REG_LBR   = 2,	/* lbr_select */
	EXTRA_REG_LDLAT = 3,	/* ld_lat_threshold */
	EXTRA_REG_FE    = 4,    /* fe_* */

	EXTRA_REG_MAX		/* number of entries needed */
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

/*
 * struct hw_perf_event.flags flags
 */
#define PERF_X86_EVENT_PEBS_LDLAT	0x0001 /* ld+ldlat data address sampling */
#define PERF_X86_EVENT_PEBS_ST		0x0002 /* st data address sampling */
#define PERF_X86_EVENT_PEBS_ST_HSW	0x0004 /* haswell style datala, store */
#define PERF_X86_EVENT_PEBS_LD_HSW	0x0008 /* haswell style datala, load */
#define PERF_X86_EVENT_PEBS_NA_HSW	0x0010 /* haswell style datala, unknown */
#define PERF_X86_EVENT_EXCL		0x0020 /* HT exclusivity on counter */
#define PERF_X86_EVENT_DYNAMIC		0x0040 /* dynamic alloc'd constraint */
#define PERF_X86_EVENT_RDPMC_ALLOWED	0x0080 /* grant rdpmc permission */
#define PERF_X86_EVENT_EXCL_ACCT	0x0100 /* accounted EXCL event */
#define PERF_X86_EVENT_AUTO_RELOAD	0x0200 /* use PEBS auto-reload */
#define PERF_X86_EVENT_LARGE_PEBS	0x0400 /* use large PEBS */
#define PERF_X86_EVENT_PEBS_VIA_PT	0x0800 /* use PT buffer for PEBS */

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
	PERF_SAMPLE_PERIOD)

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
	unsigned long		running[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	int			enabled;

	int			n_events; /* the # of events in the below arrays */
	int			n_added;  /* the # last events in the below arrays;
					     they've never been enabled yet */
	int			n_txn;    /* the # last events in the below arrays;
					     added in the current transaction */
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

	/*
	 * Intel LBR bits
	 */
	int				lbr_users;
	int				lbr_pebs_users;
	struct perf_branch_stack	lbr_stack;
	struct perf_branch_entry	lbr_entries[MAX_LBR_ENTRIES];
	struct er_account		*lbr_sel;
	u64				br_sel;
	struct x86_perf_task_context	*last_task_ctx;
	int				last_log_id;

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
	 * AMD specific bits
	 */
	struct amd_nb			*amd_nb;
	/* Inverted mask of bits to clear in the perf_ctr ctrl registers */
	u64				perf_ctr_virt_mask;

	void				*kfree_on_online[X86_PERF_KFREE_MAX];
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

#define INTEL_PST_CONSTRAINT(c, n)	\
	__EVENT_CONSTRAINT(c, n, INTEL_ARCH_EVENT_MASK|X86_ALL_EVENT_FLAGS, \
			  HWEIGHT(n), 0, PERF_X86_EVENT_PEBS_ST)

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
		u64	pebs_metrics_available:1;
		u64	pebs_output_pt_available:1;
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
	void		(*add)(struct perf_event *);
	void		(*del)(struct perf_event *);
	void		(*read)(struct perf_event *event);
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
	int		perfctr_second_write;
	u64		(*limit_period)(struct perf_event *event, u64 l);

	/* PMI handler bits */
	unsigned int	late_ack		:1,
			counter_freezing	:1;
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
	void		(*sched_task)(struct perf_event_context *ctx,
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
			pebs_no_isolation	:1;
	int		pebs_record_size;
	int		pebs_buffer_size;
	int		max_pebs_events;
	void		(*drain_pebs)(struct pt_regs *regs);
	struct event_constraint *pebs_constraints;
	void		(*pebs_aliases)(struct perf_event *event);
	unsigned long	large_pebs_flags;
	u64		rtm_abort_event;

	/*
	 * Intel LBR
	 */
	unsigned long	lbr_tos, lbr_from, lbr_to; /* MSR base regs       */
	int		lbr_nr;			   /* hardware stack size */
	u64		lbr_sel_mask;		   /* LBR_SELECT valid bits */
	const int	*lbr_sel_map;		   /* lbr_select mappings */
	bool		lbr_double_abort;	   /* duplicated lbr aborts */
	bool		lbr_pt_coexist;		   /* (LBR|BTS) may coexist with PT */

	/*
	 * Intel PT/LBR/BTS are exclusive
	 */
	atomic_t	lbr_exclusive[x86_lbr_exclusive_max];

	/*
	 * AMD bits
	 */
	unsigned int	amd_nb_constraints : 1;

	/*
	 * Extra registers for events
	 */
	struct extra_reg *extra_regs;
	unsigned int flags;

	/*
	 * Intel host/guest support (KVM)
	 */
	struct perf_guest_switch_msr *(*guest_get_msrs)(int *nr);

	/*
	 * Check period value for PERF_EVENT_IOC_PERIOD ioctl.
	 */
	int (*check_period) (struct perf_event *event, u64 period);

	int (*aux_output_match) (struct perf_event *event);
};

struct x86_perf_task_context {
	u64 lbr_from[MAX_LBR_ENTRIES];
	u64 lbr_to[MAX_LBR_ENTRIES];
	u64 lbr_info[MAX_LBR_ENTRIES];
	int tos;
	int valid_lbrs;
	int lbr_callstack_users;
	int lbr_stack_state;
	int log_id;
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

struct pmu *x86_get_pmu(void);
extern struct x86_pmu x86_pmu __read_mostly;

static inline bool x86_pmu_has_lbr_callstack(void)
{
	return  x86_pmu.lbr_sel_map &&
		x86_pmu.lbr_sel_map[PERF_SAMPLE_BRANCH_CALL_STACK_SHIFT] > 0;
}

DECLARE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

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

int x86_add_exclusive(unsigned int what);

void x86_del_exclusive(unsigned int what);

int x86_reserve_hardware(void);

void x86_release_hardware(void);

int x86_pmu_max_precise(void);

void hw_perf_lbr_event_destroy(struct perf_event *event);

int x86_setup_perfctr(struct perf_event *event);

int x86_pmu_hw_config(struct perf_event *event);

void x86_pmu_disable_all(void);

static inline void __x86_pmu_enable_event(struct hw_perf_event *hwc,
					  u64 enable_mask)
{
	u64 disable_mask = __this_cpu_read(cpu_hw_events.perf_ctr_virt_mask);

	if (hwc->extra_reg.reg)
		wrmsrl(hwc->extra_reg.reg, hwc->extra_reg.config);
	wrmsrl(hwc->config_base, (hwc->config | enable_mask) & ~disable_mask);
}

void x86_pmu_enable_all(int added);

int perf_assign_events(struct event_constraint **constraints, int n,
			int wmin, int wmax, int gpmax, int *assign);
int x86_schedule_events(struct cpu_hw_events *cpuc, int n, int *assign);

void x86_pmu_stop(struct perf_event *event, int flags);

static inline void x86_pmu_disable_event(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config);
}

void x86_pmu_enable_event(struct perf_event *event);

int x86_pmu_handle_irq(struct pt_regs *regs);

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

ssize_t x86_event_sysfs_show(char *page, u64 config, u64 event);
ssize_t intel_event_sysfs_show(char *page, u64 config);

ssize_t events_sysfs_show(struct device *dev, struct device_attribute *attr,
			  char *page);
ssize_t events_ht_sysfs_show(struct device *dev, struct device_attribute *attr,
			  char *page);

#ifdef CONFIG_CPU_SUP_AMD

int amd_pmu_init(void);

#else /* CONFIG_CPU_SUP_AMD */

static inline int amd_pmu_init(void)
{
	return 0;
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

extern struct event_constraint bts_constraint;

void intel_pmu_enable_bts(u64 config);

void intel_pmu_disable_bts(void);

int intel_pmu_drain_bts_buffer(void);

extern struct event_constraint intel_core2_pebs_event_constraints[];

extern struct event_constraint intel_atom_pebs_event_constraints[];

extern struct event_constraint intel_slm_pebs_event_constraints[];

extern struct event_constraint intel_glm_pebs_event_constraints[];

extern struct event_constraint intel_glp_pebs_event_constraints[];

extern struct event_constraint intel_nehalem_pebs_event_constraints[];

extern struct event_constraint intel_westmere_pebs_event_constraints[];

extern struct event_constraint intel_snb_pebs_event_constraints[];

extern struct event_constraint intel_ivb_pebs_event_constraints[];

extern struct event_constraint intel_hsw_pebs_event_constraints[];

extern struct event_constraint intel_bdw_pebs_event_constraints[];

extern struct event_constraint intel_skl_pebs_event_constraints[];

extern struct event_constraint intel_icl_pebs_event_constraints[];

struct event_constraint *intel_pebs_constraints(struct perf_event *event);

void intel_pmu_pebs_add(struct perf_event *event);

void intel_pmu_pebs_del(struct perf_event *event);

void intel_pmu_pebs_enable(struct perf_event *event);

void intel_pmu_pebs_disable(struct perf_event *event);

void intel_pmu_pebs_enable_all(void);

void intel_pmu_pebs_disable_all(void);

void intel_pmu_pebs_sched_task(struct perf_event_context *ctx, bool sched_in);

void intel_pmu_auto_reload_read(struct perf_event *event);

void intel_pmu_store_pebs_lbrs(struct pebs_lbr *lbr);

void intel_ds_init(void);

void intel_pmu_lbr_sched_task(struct perf_event_context *ctx, bool sched_in);

u64 lbr_from_signext_quirk_wr(u64 val);

void intel_pmu_lbr_reset(void);

void intel_pmu_lbr_add(struct perf_event *event);

void intel_pmu_lbr_del(struct perf_event *event);

void intel_pmu_lbr_enable_all(bool pmi);

void intel_pmu_lbr_disable_all(void);

void intel_pmu_lbr_read(void);

void intel_pmu_lbr_init_core(void);

void intel_pmu_lbr_init_nhm(void);

void intel_pmu_lbr_init_atom(void);

void intel_pmu_lbr_init_slm(void);

void intel_pmu_lbr_init_snb(void);

void intel_pmu_lbr_init_hsw(void);

void intel_pmu_lbr_init_skl(void);

void intel_pmu_lbr_init_knl(void);

void intel_pmu_pebs_data_source_nhm(void);

void intel_pmu_pebs_data_source_skl(bool pmem);

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
