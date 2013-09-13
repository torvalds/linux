#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/slab.h>

#include <asm/perf_event.h>
#include <asm/insn.h>

#include "perf_event.h"

/* The size of a BTS record in bytes: */
#define BTS_RECORD_SIZE		24

#define BTS_BUFFER_SIZE		(PAGE_SIZE << 4)
#define PEBS_BUFFER_SIZE	PAGE_SIZE

/*
 * pebs_record_32 for p4 and core not supported

struct pebs_record_32 {
	u32 flags, ip;
	u32 ax, bc, cx, dx;
	u32 si, di, bp, sp;
};

 */

union intel_x86_pebs_dse {
	u64 val;
	struct {
		unsigned int ld_dse:4;
		unsigned int ld_stlb_miss:1;
		unsigned int ld_locked:1;
		unsigned int ld_reserved:26;
	};
	struct {
		unsigned int st_l1d_hit:1;
		unsigned int st_reserved1:3;
		unsigned int st_stlb_miss:1;
		unsigned int st_locked:1;
		unsigned int st_reserved2:26;
	};
};


/*
 * Map PEBS Load Latency Data Source encodings to generic
 * memory data source information
 */
#define P(a, b) PERF_MEM_S(a, b)
#define OP_LH (P(OP, LOAD) | P(LVL, HIT))
#define SNOOP_NONE_MISS (P(SNOOP, NONE) | P(SNOOP, MISS))

static const u64 pebs_data_source[] = {
	P(OP, LOAD) | P(LVL, MISS) | P(LVL, L3) | P(SNOOP, NA),/* 0x00:ukn L3 */
	OP_LH | P(LVL, L1)  | P(SNOOP, NONE),	/* 0x01: L1 local */
	OP_LH | P(LVL, LFB) | P(SNOOP, NONE),	/* 0x02: LFB hit */
	OP_LH | P(LVL, L2)  | P(SNOOP, NONE),	/* 0x03: L2 hit */
	OP_LH | P(LVL, L3)  | P(SNOOP, NONE),	/* 0x04: L3 hit */
	OP_LH | P(LVL, L3)  | P(SNOOP, MISS),	/* 0x05: L3 hit, snoop miss */
	OP_LH | P(LVL, L3)  | P(SNOOP, HIT),	/* 0x06: L3 hit, snoop hit */
	OP_LH | P(LVL, L3)  | P(SNOOP, HITM),	/* 0x07: L3 hit, snoop hitm */
	OP_LH | P(LVL, REM_CCE1) | P(SNOOP, HIT),  /* 0x08: L3 miss snoop hit */
	OP_LH | P(LVL, REM_CCE1) | P(SNOOP, HITM), /* 0x09: L3 miss snoop hitm*/
	OP_LH | P(LVL, LOC_RAM)  | P(SNOOP, HIT),  /* 0x0a: L3 miss, shared */
	OP_LH | P(LVL, REM_RAM1) | P(SNOOP, HIT),  /* 0x0b: L3 miss, shared */
	OP_LH | P(LVL, LOC_RAM)  | SNOOP_NONE_MISS,/* 0x0c: L3 miss, excl */
	OP_LH | P(LVL, REM_RAM1) | SNOOP_NONE_MISS,/* 0x0d: L3 miss, excl */
	OP_LH | P(LVL, IO)  | P(SNOOP, NONE), /* 0x0e: I/O */
	OP_LH | P(LVL, UNC) | P(SNOOP, NONE), /* 0x0f: uncached */
};

static u64 precise_store_data(u64 status)
{
	union intel_x86_pebs_dse dse;
	u64 val = P(OP, STORE) | P(SNOOP, NA) | P(LVL, L1) | P(TLB, L2);

	dse.val = status;

	/*
	 * bit 4: TLB access
	 * 1 = stored missed 2nd level TLB
	 *
	 * so it either hit the walker or the OS
	 * otherwise hit 2nd level TLB
	 */
	if (dse.st_stlb_miss)
		val |= P(TLB, MISS);
	else
		val |= P(TLB, HIT);

	/*
	 * bit 0: hit L1 data cache
	 * if not set, then all we know is that
	 * it missed L1D
	 */
	if (dse.st_l1d_hit)
		val |= P(LVL, HIT);
	else
		val |= P(LVL, MISS);

	/*
	 * bit 5: Locked prefix
	 */
	if (dse.st_locked)
		val |= P(LOCK, LOCKED);

	return val;
}

static u64 precise_store_data_hsw(u64 status)
{
	union perf_mem_data_src dse;

	dse.val = 0;
	dse.mem_op = PERF_MEM_OP_STORE;
	dse.mem_lvl = PERF_MEM_LVL_NA;
	if (status & 1)
		dse.mem_lvl = PERF_MEM_LVL_L1;
	/* Nothing else supported. Sorry. */
	return dse.val;
}

static u64 load_latency_data(u64 status)
{
	union intel_x86_pebs_dse dse;
	u64 val;
	int model = boot_cpu_data.x86_model;
	int fam = boot_cpu_data.x86;

	dse.val = status;

	/*
	 * use the mapping table for bit 0-3
	 */
	val = pebs_data_source[dse.ld_dse];

	/*
	 * Nehalem models do not support TLB, Lock infos
	 */
	if (fam == 0x6 && (model == 26 || model == 30
	    || model == 31 || model == 46)) {
		val |= P(TLB, NA) | P(LOCK, NA);
		return val;
	}
	/*
	 * bit 4: TLB access
	 * 0 = did not miss 2nd level TLB
	 * 1 = missed 2nd level TLB
	 */
	if (dse.ld_stlb_miss)
		val |= P(TLB, MISS) | P(TLB, L2);
	else
		val |= P(TLB, HIT) | P(TLB, L1) | P(TLB, L2);

	/*
	 * bit 5: locked prefix
	 */
	if (dse.ld_locked)
		val |= P(LOCK, LOCKED);

	return val;
}

struct pebs_record_core {
	u64 flags, ip;
	u64 ax, bx, cx, dx;
	u64 si, di, bp, sp;
	u64 r8,  r9,  r10, r11;
	u64 r12, r13, r14, r15;
};

struct pebs_record_nhm {
	u64 flags, ip;
	u64 ax, bx, cx, dx;
	u64 si, di, bp, sp;
	u64 r8,  r9,  r10, r11;
	u64 r12, r13, r14, r15;
	u64 status, dla, dse, lat;
};

/*
 * Same as pebs_record_nhm, with two additional fields.
 */
struct pebs_record_hsw {
	struct pebs_record_nhm nhm;
	/*
	 * Real IP of the event. In the Intel documentation this
	 * is called eventingrip.
	 */
	u64 real_ip;
	/*
	 * TSX tuning information field: abort cycles and abort flags.
	 */
	u64 tsx_tuning;
};

void init_debug_store_on_cpu(int cpu)
{
	struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;

	if (!ds)
		return;

	wrmsr_on_cpu(cpu, MSR_IA32_DS_AREA,
		     (u32)((u64)(unsigned long)ds),
		     (u32)((u64)(unsigned long)ds >> 32));
}

void fini_debug_store_on_cpu(int cpu)
{
	if (!per_cpu(cpu_hw_events, cpu).ds)
		return;

	wrmsr_on_cpu(cpu, MSR_IA32_DS_AREA, 0, 0);
}

static int alloc_pebs_buffer(int cpu)
{
	struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;
	int node = cpu_to_node(cpu);
	int max, thresh = 1; /* always use a single PEBS record */
	void *buffer;

	if (!x86_pmu.pebs)
		return 0;

	buffer = kmalloc_node(PEBS_BUFFER_SIZE, GFP_KERNEL | __GFP_ZERO, node);
	if (unlikely(!buffer))
		return -ENOMEM;

	max = PEBS_BUFFER_SIZE / x86_pmu.pebs_record_size;

	ds->pebs_buffer_base = (u64)(unsigned long)buffer;
	ds->pebs_index = ds->pebs_buffer_base;
	ds->pebs_absolute_maximum = ds->pebs_buffer_base +
		max * x86_pmu.pebs_record_size;

	ds->pebs_interrupt_threshold = ds->pebs_buffer_base +
		thresh * x86_pmu.pebs_record_size;

	return 0;
}

static void release_pebs_buffer(int cpu)
{
	struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;

	if (!ds || !x86_pmu.pebs)
		return;

	kfree((void *)(unsigned long)ds->pebs_buffer_base);
	ds->pebs_buffer_base = 0;
}

static int alloc_bts_buffer(int cpu)
{
	struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;
	int node = cpu_to_node(cpu);
	int max, thresh;
	void *buffer;

	if (!x86_pmu.bts)
		return 0;

	buffer = kmalloc_node(BTS_BUFFER_SIZE, GFP_KERNEL | __GFP_ZERO, node);
	if (unlikely(!buffer))
		return -ENOMEM;

	max = BTS_BUFFER_SIZE / BTS_RECORD_SIZE;
	thresh = max / 16;

	ds->bts_buffer_base = (u64)(unsigned long)buffer;
	ds->bts_index = ds->bts_buffer_base;
	ds->bts_absolute_maximum = ds->bts_buffer_base +
		max * BTS_RECORD_SIZE;
	ds->bts_interrupt_threshold = ds->bts_absolute_maximum -
		thresh * BTS_RECORD_SIZE;

	return 0;
}

static void release_bts_buffer(int cpu)
{
	struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;

	if (!ds || !x86_pmu.bts)
		return;

	kfree((void *)(unsigned long)ds->bts_buffer_base);
	ds->bts_buffer_base = 0;
}

static int alloc_ds_buffer(int cpu)
{
	int node = cpu_to_node(cpu);
	struct debug_store *ds;

	ds = kmalloc_node(sizeof(*ds), GFP_KERNEL | __GFP_ZERO, node);
	if (unlikely(!ds))
		return -ENOMEM;

	per_cpu(cpu_hw_events, cpu).ds = ds;

	return 0;
}

static void release_ds_buffer(int cpu)
{
	struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;

	if (!ds)
		return;

	per_cpu(cpu_hw_events, cpu).ds = NULL;
	kfree(ds);
}

void release_ds_buffers(void)
{
	int cpu;

	if (!x86_pmu.bts && !x86_pmu.pebs)
		return;

	get_online_cpus();
	for_each_online_cpu(cpu)
		fini_debug_store_on_cpu(cpu);

	for_each_possible_cpu(cpu) {
		release_pebs_buffer(cpu);
		release_bts_buffer(cpu);
		release_ds_buffer(cpu);
	}
	put_online_cpus();
}

void reserve_ds_buffers(void)
{
	int bts_err = 0, pebs_err = 0;
	int cpu;

	x86_pmu.bts_active = 0;
	x86_pmu.pebs_active = 0;

	if (!x86_pmu.bts && !x86_pmu.pebs)
		return;

	if (!x86_pmu.bts)
		bts_err = 1;

	if (!x86_pmu.pebs)
		pebs_err = 1;

	get_online_cpus();

	for_each_possible_cpu(cpu) {
		if (alloc_ds_buffer(cpu)) {
			bts_err = 1;
			pebs_err = 1;
		}

		if (!bts_err && alloc_bts_buffer(cpu))
			bts_err = 1;

		if (!pebs_err && alloc_pebs_buffer(cpu))
			pebs_err = 1;

		if (bts_err && pebs_err)
			break;
	}

	if (bts_err) {
		for_each_possible_cpu(cpu)
			release_bts_buffer(cpu);
	}

	if (pebs_err) {
		for_each_possible_cpu(cpu)
			release_pebs_buffer(cpu);
	}

	if (bts_err && pebs_err) {
		for_each_possible_cpu(cpu)
			release_ds_buffer(cpu);
	} else {
		if (x86_pmu.bts && !bts_err)
			x86_pmu.bts_active = 1;

		if (x86_pmu.pebs && !pebs_err)
			x86_pmu.pebs_active = 1;

		for_each_online_cpu(cpu)
			init_debug_store_on_cpu(cpu);
	}

	put_online_cpus();
}

/*
 * BTS
 */

struct event_constraint bts_constraint =
	EVENT_CONSTRAINT(0, 1ULL << INTEL_PMC_IDX_FIXED_BTS, 0);

void intel_pmu_enable_bts(u64 config)
{
	unsigned long debugctlmsr;

	debugctlmsr = get_debugctlmsr();

	debugctlmsr |= DEBUGCTLMSR_TR;
	debugctlmsr |= DEBUGCTLMSR_BTS;
	debugctlmsr |= DEBUGCTLMSR_BTINT;

	if (!(config & ARCH_PERFMON_EVENTSEL_OS))
		debugctlmsr |= DEBUGCTLMSR_BTS_OFF_OS;

	if (!(config & ARCH_PERFMON_EVENTSEL_USR))
		debugctlmsr |= DEBUGCTLMSR_BTS_OFF_USR;

	update_debugctlmsr(debugctlmsr);
}

void intel_pmu_disable_bts(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	unsigned long debugctlmsr;

	if (!cpuc->ds)
		return;

	debugctlmsr = get_debugctlmsr();

	debugctlmsr &=
		~(DEBUGCTLMSR_TR | DEBUGCTLMSR_BTS | DEBUGCTLMSR_BTINT |
		  DEBUGCTLMSR_BTS_OFF_OS | DEBUGCTLMSR_BTS_OFF_USR);

	update_debugctlmsr(debugctlmsr);
}

int intel_pmu_drain_bts_buffer(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct debug_store *ds = cpuc->ds;
	struct bts_record {
		u64	from;
		u64	to;
		u64	flags;
	};
	struct perf_event *event = cpuc->events[INTEL_PMC_IDX_FIXED_BTS];
	struct bts_record *at, *top;
	struct perf_output_handle handle;
	struct perf_event_header header;
	struct perf_sample_data data;
	struct pt_regs regs;

	if (!event)
		return 0;

	if (!x86_pmu.bts_active)
		return 0;

	at  = (struct bts_record *)(unsigned long)ds->bts_buffer_base;
	top = (struct bts_record *)(unsigned long)ds->bts_index;

	if (top <= at)
		return 0;

	memset(&regs, 0, sizeof(regs));

	ds->bts_index = ds->bts_buffer_base;

	perf_sample_data_init(&data, 0, event->hw.last_period);

	/*
	 * Prepare a generic sample, i.e. fill in the invariant fields.
	 * We will overwrite the from and to address before we output
	 * the sample.
	 */
	perf_prepare_sample(&header, &data, event, &regs);

	if (perf_output_begin(&handle, event, header.size * (top - at)))
		return 1;

	for (; at < top; at++) {
		data.ip		= at->from;
		data.addr	= at->to;

		perf_output_sample(&handle, &header, &data, event);
	}

	perf_output_end(&handle);

	/* There's new data available. */
	event->hw.interrupts++;
	event->pending_kill = POLL_IN;
	return 1;
}

/*
 * PEBS
 */
struct event_constraint intel_core2_pebs_event_constraints[] = {
	INTEL_UEVENT_CONSTRAINT(0x00c0, 0x1), /* INST_RETIRED.ANY */
	INTEL_UEVENT_CONSTRAINT(0xfec1, 0x1), /* X87_OPS_RETIRED.ANY */
	INTEL_UEVENT_CONSTRAINT(0x00c5, 0x1), /* BR_INST_RETIRED.MISPRED */
	INTEL_UEVENT_CONSTRAINT(0x1fc7, 0x1), /* SIMD_INST_RETURED.ANY */
	INTEL_EVENT_CONSTRAINT(0xcb, 0x1),    /* MEM_LOAD_RETIRED.* */
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_atom_pebs_event_constraints[] = {
	INTEL_UEVENT_CONSTRAINT(0x00c0, 0x1), /* INST_RETIRED.ANY */
	INTEL_UEVENT_CONSTRAINT(0x00c5, 0x1), /* MISPREDICTED_BRANCH_RETIRED */
	INTEL_EVENT_CONSTRAINT(0xcb, 0x1),    /* MEM_LOAD_RETIRED.* */
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_nehalem_pebs_event_constraints[] = {
	INTEL_PLD_CONSTRAINT(0x100b, 0xf),      /* MEM_INST_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0x0f, 0xf),    /* MEM_UNCORE_RETIRED.* */
	INTEL_UEVENT_CONSTRAINT(0x010c, 0xf), /* MEM_STORE_RETIRED.DTLB_MISS */
	INTEL_EVENT_CONSTRAINT(0xc0, 0xf),    /* INST_RETIRED.ANY */
	INTEL_EVENT_CONSTRAINT(0xc2, 0xf),    /* UOPS_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xc4, 0xf),    /* BR_INST_RETIRED.* */
	INTEL_UEVENT_CONSTRAINT(0x02c5, 0xf), /* BR_MISP_RETIRED.NEAR_CALL */
	INTEL_EVENT_CONSTRAINT(0xc7, 0xf),    /* SSEX_UOPS_RETIRED.* */
	INTEL_UEVENT_CONSTRAINT(0x20c8, 0xf), /* ITLB_MISS_RETIRED */
	INTEL_EVENT_CONSTRAINT(0xcb, 0xf),    /* MEM_LOAD_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xf7, 0xf),    /* FP_ASSIST.* */
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_westmere_pebs_event_constraints[] = {
	INTEL_PLD_CONSTRAINT(0x100b, 0xf),      /* MEM_INST_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0x0f, 0xf),    /* MEM_UNCORE_RETIRED.* */
	INTEL_UEVENT_CONSTRAINT(0x010c, 0xf), /* MEM_STORE_RETIRED.DTLB_MISS */
	INTEL_EVENT_CONSTRAINT(0xc0, 0xf),    /* INSTR_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xc2, 0xf),    /* UOPS_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xc4, 0xf),    /* BR_INST_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xc5, 0xf),    /* BR_MISP_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xc7, 0xf),    /* SSEX_UOPS_RETIRED.* */
	INTEL_UEVENT_CONSTRAINT(0x20c8, 0xf), /* ITLB_MISS_RETIRED */
	INTEL_EVENT_CONSTRAINT(0xcb, 0xf),    /* MEM_LOAD_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xf7, 0xf),    /* FP_ASSIST.* */
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_snb_pebs_event_constraints[] = {
	INTEL_UEVENT_CONSTRAINT(0x01c0, 0x2), /* INST_RETIRED.PRECDIST */
	INTEL_UEVENT_CONSTRAINT(0x01c2, 0xf), /* UOPS_RETIRED.ALL */
	INTEL_UEVENT_CONSTRAINT(0x02c2, 0xf), /* UOPS_RETIRED.RETIRE_SLOTS */
	INTEL_EVENT_CONSTRAINT(0xc4, 0xf),    /* BR_INST_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xc5, 0xf),    /* BR_MISP_RETIRED.* */
	INTEL_PLD_CONSTRAINT(0x01cd, 0x8),    /* MEM_TRANS_RETIRED.LAT_ABOVE_THR */
	INTEL_PST_CONSTRAINT(0x02cd, 0x8),    /* MEM_TRANS_RETIRED.PRECISE_STORES */
	INTEL_EVENT_CONSTRAINT(0xd0, 0xf),    /* MEM_UOP_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xd1, 0xf),    /* MEM_LOAD_UOPS_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xd2, 0xf),    /* MEM_LOAD_UOPS_LLC_HIT_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xd3, 0xf),    /* MEM_LOAD_UOPS_LLC_MISS_RETIRED.* */
	INTEL_UEVENT_CONSTRAINT(0x02d4, 0xf), /* MEM_LOAD_UOPS_MISC_RETIRED.LLC_MISS */
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_ivb_pebs_event_constraints[] = {
        INTEL_UEVENT_CONSTRAINT(0x01c0, 0x2), /* INST_RETIRED.PRECDIST */
        INTEL_UEVENT_CONSTRAINT(0x01c2, 0xf), /* UOPS_RETIRED.ALL */
        INTEL_UEVENT_CONSTRAINT(0x02c2, 0xf), /* UOPS_RETIRED.RETIRE_SLOTS */
        INTEL_EVENT_CONSTRAINT(0xc4, 0xf),    /* BR_INST_RETIRED.* */
        INTEL_EVENT_CONSTRAINT(0xc5, 0xf),    /* BR_MISP_RETIRED.* */
        INTEL_PLD_CONSTRAINT(0x01cd, 0x8),    /* MEM_TRANS_RETIRED.LAT_ABOVE_THR */
	INTEL_PST_CONSTRAINT(0x02cd, 0x8),    /* MEM_TRANS_RETIRED.PRECISE_STORES */
        INTEL_EVENT_CONSTRAINT(0xd0, 0xf),    /* MEM_UOP_RETIRED.* */
        INTEL_EVENT_CONSTRAINT(0xd1, 0xf),    /* MEM_LOAD_UOPS_RETIRED.* */
        INTEL_EVENT_CONSTRAINT(0xd2, 0xf),    /* MEM_LOAD_UOPS_LLC_HIT_RETIRED.* */
        INTEL_EVENT_CONSTRAINT(0xd3, 0xf),    /* MEM_LOAD_UOPS_LLC_MISS_RETIRED.* */
        EVENT_CONSTRAINT_END
};

struct event_constraint intel_hsw_pebs_event_constraints[] = {
	INTEL_UEVENT_CONSTRAINT(0x01c0, 0x2), /* INST_RETIRED.PRECDIST */
	INTEL_PST_HSW_CONSTRAINT(0x01c2, 0xf), /* UOPS_RETIRED.ALL */
	INTEL_UEVENT_CONSTRAINT(0x02c2, 0xf), /* UOPS_RETIRED.RETIRE_SLOTS */
	INTEL_EVENT_CONSTRAINT(0xc4, 0xf),    /* BR_INST_RETIRED.* */
	INTEL_UEVENT_CONSTRAINT(0x01c5, 0xf), /* BR_MISP_RETIRED.CONDITIONAL */
	INTEL_UEVENT_CONSTRAINT(0x04c5, 0xf), /* BR_MISP_RETIRED.ALL_BRANCHES */
	INTEL_UEVENT_CONSTRAINT(0x20c5, 0xf), /* BR_MISP_RETIRED.NEAR_TAKEN */
	INTEL_PLD_CONSTRAINT(0x01cd, 0x8),    /* MEM_TRANS_RETIRED.* */
	/* MEM_UOPS_RETIRED.STLB_MISS_LOADS */
	INTEL_UEVENT_CONSTRAINT(0x11d0, 0xf),
	/* MEM_UOPS_RETIRED.STLB_MISS_STORES */
	INTEL_UEVENT_CONSTRAINT(0x12d0, 0xf),
	INTEL_UEVENT_CONSTRAINT(0x21d0, 0xf), /* MEM_UOPS_RETIRED.LOCK_LOADS */
	INTEL_UEVENT_CONSTRAINT(0x41d0, 0xf), /* MEM_UOPS_RETIRED.SPLIT_LOADS */
	/* MEM_UOPS_RETIRED.SPLIT_STORES */
	INTEL_UEVENT_CONSTRAINT(0x42d0, 0xf),
	INTEL_UEVENT_CONSTRAINT(0x81d0, 0xf), /* MEM_UOPS_RETIRED.ALL_LOADS */
	INTEL_PST_HSW_CONSTRAINT(0x82d0, 0xf), /* MEM_UOPS_RETIRED.ALL_STORES */
	INTEL_UEVENT_CONSTRAINT(0x01d1, 0xf), /* MEM_LOAD_UOPS_RETIRED.L1_HIT */
	INTEL_UEVENT_CONSTRAINT(0x02d1, 0xf), /* MEM_LOAD_UOPS_RETIRED.L2_HIT */
	INTEL_UEVENT_CONSTRAINT(0x04d1, 0xf), /* MEM_LOAD_UOPS_RETIRED.L3_HIT */
	/* MEM_LOAD_UOPS_RETIRED.HIT_LFB */
	INTEL_UEVENT_CONSTRAINT(0x40d1, 0xf),
	/* MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_MISS */
	INTEL_UEVENT_CONSTRAINT(0x01d2, 0xf),
	/* MEM_LOAD_UOPS_LLC_HIT_RETIRED.XSNP_HIT */
	INTEL_UEVENT_CONSTRAINT(0x02d2, 0xf),
	/* MEM_LOAD_UOPS_LLC_MISS_RETIRED.LOCAL_DRAM */
	INTEL_UEVENT_CONSTRAINT(0x01d3, 0xf),
	INTEL_UEVENT_CONSTRAINT(0x04c8, 0xf), /* HLE_RETIRED.Abort */
	INTEL_UEVENT_CONSTRAINT(0x04c9, 0xf), /* RTM_RETIRED.Abort */

	EVENT_CONSTRAINT_END
};

struct event_constraint *intel_pebs_constraints(struct perf_event *event)
{
	struct event_constraint *c;

	if (!event->attr.precise_ip)
		return NULL;

	if (x86_pmu.pebs_constraints) {
		for_each_event_constraint(c, x86_pmu.pebs_constraints) {
			if ((event->hw.config & c->cmask) == c->code) {
				event->hw.flags |= c->flags;
				return c;
			}
		}
	}

	return &emptyconstraint;
}

void intel_pmu_pebs_enable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	hwc->config &= ~ARCH_PERFMON_EVENTSEL_INT;

	cpuc->pebs_enabled |= 1ULL << hwc->idx;

	if (event->hw.flags & PERF_X86_EVENT_PEBS_LDLAT)
		cpuc->pebs_enabled |= 1ULL << (hwc->idx + 32);
	else if (event->hw.flags & PERF_X86_EVENT_PEBS_ST)
		cpuc->pebs_enabled |= 1ULL << 63;
}

void intel_pmu_pebs_disable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	cpuc->pebs_enabled &= ~(1ULL << hwc->idx);

	if (event->hw.constraint->flags & PERF_X86_EVENT_PEBS_LDLAT)
		cpuc->pebs_enabled &= ~(1ULL << (hwc->idx + 32));
	else if (event->hw.constraint->flags & PERF_X86_EVENT_PEBS_ST)
		cpuc->pebs_enabled &= ~(1ULL << 63);

	if (cpuc->enabled)
		wrmsrl(MSR_IA32_PEBS_ENABLE, cpuc->pebs_enabled);

	hwc->config |= ARCH_PERFMON_EVENTSEL_INT;
}

void intel_pmu_pebs_enable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->pebs_enabled)
		wrmsrl(MSR_IA32_PEBS_ENABLE, cpuc->pebs_enabled);
}

void intel_pmu_pebs_disable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->pebs_enabled)
		wrmsrl(MSR_IA32_PEBS_ENABLE, 0);
}

static int intel_pmu_pebs_fixup_ip(struct pt_regs *regs)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	unsigned long from = cpuc->lbr_entries[0].from;
	unsigned long old_to, to = cpuc->lbr_entries[0].to;
	unsigned long ip = regs->ip;
	int is_64bit = 0;

	/*
	 * We don't need to fixup if the PEBS assist is fault like
	 */
	if (!x86_pmu.intel_cap.pebs_trap)
		return 1;

	/*
	 * No LBR entry, no basic block, no rewinding
	 */
	if (!cpuc->lbr_stack.nr || !from || !to)
		return 0;

	/*
	 * Basic blocks should never cross user/kernel boundaries
	 */
	if (kernel_ip(ip) != kernel_ip(to))
		return 0;

	/*
	 * unsigned math, either ip is before the start (impossible) or
	 * the basic block is larger than 1 page (sanity)
	 */
	if ((ip - to) > PAGE_SIZE)
		return 0;

	/*
	 * We sampled a branch insn, rewind using the LBR stack
	 */
	if (ip == to) {
		set_linear_ip(regs, from);
		return 1;
	}

	do {
		struct insn insn;
		u8 buf[MAX_INSN_SIZE];
		void *kaddr;

		old_to = to;
		if (!kernel_ip(ip)) {
			int bytes, size = MAX_INSN_SIZE;

			bytes = copy_from_user_nmi(buf, (void __user *)to, size);
			if (bytes != size)
				return 0;

			kaddr = buf;
		} else
			kaddr = (void *)to;

#ifdef CONFIG_X86_64
		is_64bit = kernel_ip(to) || !test_thread_flag(TIF_IA32);
#endif
		insn_init(&insn, kaddr, is_64bit);
		insn_get_length(&insn);
		to += insn.length;
	} while (to < ip);

	if (to == ip) {
		set_linear_ip(regs, old_to);
		return 1;
	}

	/*
	 * Even though we decoded the basic block, the instruction stream
	 * never matched the given IP, either the TO or the IP got corrupted.
	 */
	return 0;
}

static void __intel_pmu_pebs_event(struct perf_event *event,
				   struct pt_regs *iregs, void *__pebs)
{
	/*
	 * We cast to pebs_record_nhm to get the load latency data
	 * if extra_reg MSR_PEBS_LD_LAT_THRESHOLD used
	 */
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct pebs_record_nhm *pebs = __pebs;
	struct pebs_record_hsw *pebs_hsw = __pebs;
	struct perf_sample_data data;
	struct pt_regs regs;
	u64 sample_type;
	int fll, fst;

	if (!intel_pmu_save_and_restart(event))
		return;

	fll = event->hw.flags & PERF_X86_EVENT_PEBS_LDLAT;
	fst = event->hw.flags & (PERF_X86_EVENT_PEBS_ST |
				 PERF_X86_EVENT_PEBS_ST_HSW);

	perf_sample_data_init(&data, 0, event->hw.last_period);

	data.period = event->hw.last_period;
	sample_type = event->attr.sample_type;

	/*
	 * if PEBS-LL or PreciseStore
	 */
	if (fll || fst) {
		/*
		 * Use latency for weight (only avail with PEBS-LL)
		 */
		if (fll && (sample_type & PERF_SAMPLE_WEIGHT))
			data.weight = pebs->lat;

		/*
		 * data.data_src encodes the data source
		 */
		if (sample_type & PERF_SAMPLE_DATA_SRC) {
			if (fll)
				data.data_src.val = load_latency_data(pebs->dse);
			else if (event->hw.flags & PERF_X86_EVENT_PEBS_ST_HSW)
				data.data_src.val =
					precise_store_data_hsw(pebs->dse);
			else
				data.data_src.val = precise_store_data(pebs->dse);
		}
	}

	/*
	 * We use the interrupt regs as a base because the PEBS record
	 * does not contain a full regs set, specifically it seems to
	 * lack segment descriptors, which get used by things like
	 * user_mode().
	 *
	 * In the simple case fix up only the IP and BP,SP regs, for
	 * PERF_SAMPLE_IP and PERF_SAMPLE_CALLCHAIN to function properly.
	 * A possible PERF_SAMPLE_REGS will have to transfer all regs.
	 */
	regs = *iregs;
	regs.flags = pebs->flags;
	set_linear_ip(&regs, pebs->ip);
	regs.bp = pebs->bp;
	regs.sp = pebs->sp;

	if (event->attr.precise_ip > 1 && x86_pmu.intel_cap.pebs_format >= 2) {
		regs.ip = pebs_hsw->real_ip;
		regs.flags |= PERF_EFLAGS_EXACT;
	} else if (event->attr.precise_ip > 1 && intel_pmu_pebs_fixup_ip(&regs))
		regs.flags |= PERF_EFLAGS_EXACT;
	else
		regs.flags &= ~PERF_EFLAGS_EXACT;

	if ((event->attr.sample_type & PERF_SAMPLE_ADDR) &&
		x86_pmu.intel_cap.pebs_format >= 1)
		data.addr = pebs->dla;

	if (has_branch_stack(event))
		data.br_stack = &cpuc->lbr_stack;

	if (perf_event_overflow(event, &data, &regs))
		x86_pmu_stop(event, 0);
}

static void intel_pmu_drain_pebs_core(struct pt_regs *iregs)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct debug_store *ds = cpuc->ds;
	struct perf_event *event = cpuc->events[0]; /* PMC0 only */
	struct pebs_record_core *at, *top;
	int n;

	if (!x86_pmu.pebs_active)
		return;

	at  = (struct pebs_record_core *)(unsigned long)ds->pebs_buffer_base;
	top = (struct pebs_record_core *)(unsigned long)ds->pebs_index;

	/*
	 * Whatever else happens, drain the thing
	 */
	ds->pebs_index = ds->pebs_buffer_base;

	if (!test_bit(0, cpuc->active_mask))
		return;

	WARN_ON_ONCE(!event);

	if (!event->attr.precise_ip)
		return;

	n = top - at;
	if (n <= 0)
		return;

	/*
	 * Should not happen, we program the threshold at 1 and do not
	 * set a reset value.
	 */
	WARN_ONCE(n > 1, "bad leftover pebs %d\n", n);
	at += n - 1;

	__intel_pmu_pebs_event(event, iregs, at);
}

static void __intel_pmu_drain_pebs_nhm(struct pt_regs *iregs, void *at,
					void *top)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct debug_store *ds = cpuc->ds;
	struct perf_event *event = NULL;
	u64 status = 0;
	int bit;

	ds->pebs_index = ds->pebs_buffer_base;

	for (; at < top; at += x86_pmu.pebs_record_size) {
		struct pebs_record_nhm *p = at;

		for_each_set_bit(bit, (unsigned long *)&p->status,
				 x86_pmu.max_pebs_events) {
			event = cpuc->events[bit];
			if (!test_bit(bit, cpuc->active_mask))
				continue;

			WARN_ON_ONCE(!event);

			if (!event->attr.precise_ip)
				continue;

			if (__test_and_set_bit(bit, (unsigned long *)&status))
				continue;

			break;
		}

		if (!event || bit >= x86_pmu.max_pebs_events)
			continue;

		__intel_pmu_pebs_event(event, iregs, at);
	}
}

static void intel_pmu_drain_pebs_nhm(struct pt_regs *iregs)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct debug_store *ds = cpuc->ds;
	struct pebs_record_nhm *at, *top;
	int n;

	if (!x86_pmu.pebs_active)
		return;

	at  = (struct pebs_record_nhm *)(unsigned long)ds->pebs_buffer_base;
	top = (struct pebs_record_nhm *)(unsigned long)ds->pebs_index;

	ds->pebs_index = ds->pebs_buffer_base;

	n = top - at;
	if (n <= 0)
		return;

	/*
	 * Should not happen, we program the threshold at 1 and do not
	 * set a reset value.
	 */
	WARN_ONCE(n > x86_pmu.max_pebs_events,
		  "Unexpected number of pebs records %d\n", n);

	return __intel_pmu_drain_pebs_nhm(iregs, at, top);
}

static void intel_pmu_drain_pebs_hsw(struct pt_regs *iregs)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct debug_store *ds = cpuc->ds;
	struct pebs_record_hsw *at, *top;
	int n;

	if (!x86_pmu.pebs_active)
		return;

	at  = (struct pebs_record_hsw *)(unsigned long)ds->pebs_buffer_base;
	top = (struct pebs_record_hsw *)(unsigned long)ds->pebs_index;

	n = top - at;
	if (n <= 0)
		return;
	/*
	 * Should not happen, we program the threshold at 1 and do not
	 * set a reset value.
	 */
	WARN_ONCE(n > x86_pmu.max_pebs_events,
		  "Unexpected number of pebs records %d\n", n);

	return __intel_pmu_drain_pebs_nhm(iregs, at, top);
}

/*
 * BTS, PEBS probe and setup
 */

void intel_ds_init(void)
{
	/*
	 * No support for 32bit formats
	 */
	if (!boot_cpu_has(X86_FEATURE_DTES64))
		return;

	x86_pmu.bts  = boot_cpu_has(X86_FEATURE_BTS);
	x86_pmu.pebs = boot_cpu_has(X86_FEATURE_PEBS);
	if (x86_pmu.pebs) {
		char pebs_type = x86_pmu.intel_cap.pebs_trap ?  '+' : '-';
		int format = x86_pmu.intel_cap.pebs_format;

		switch (format) {
		case 0:
			printk(KERN_CONT "PEBS fmt0%c, ", pebs_type);
			x86_pmu.pebs_record_size = sizeof(struct pebs_record_core);
			x86_pmu.drain_pebs = intel_pmu_drain_pebs_core;
			break;

		case 1:
			printk(KERN_CONT "PEBS fmt1%c, ", pebs_type);
			x86_pmu.pebs_record_size = sizeof(struct pebs_record_nhm);
			x86_pmu.drain_pebs = intel_pmu_drain_pebs_nhm;
			break;

		case 2:
			pr_cont("PEBS fmt2%c, ", pebs_type);
			x86_pmu.pebs_record_size = sizeof(struct pebs_record_hsw);
			x86_pmu.drain_pebs = intel_pmu_drain_pebs_hsw;
			break;

		default:
			printk(KERN_CONT "no PEBS fmt%d%c, ", format, pebs_type);
			x86_pmu.pebs = 0;
		}
	}
}

void perf_restore_debug_store(void)
{
	struct debug_store *ds = __this_cpu_read(cpu_hw_events.ds);

	if (!x86_pmu.bts && !x86_pmu.pebs)
		return;

	wrmsrl(MSR_IA32_DS_AREA, (unsigned long)ds);
}
