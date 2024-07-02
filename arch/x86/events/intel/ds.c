// SPDX-License-Identifier: GPL-2.0
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sched/clock.h>

#include <asm/cpu_entry_area.h>
#include <asm/debugreg.h>
#include <asm/perf_event.h>
#include <asm/tlbflush.h>
#include <asm/insn.h>
#include <asm/io.h>
#include <asm/timer.h>

#include "../perf_event.h"

/* Waste a full page so it can be mapped into the cpu_entry_area */
DEFINE_PER_CPU_PAGE_ALIGNED(struct debug_store, cpu_debug_store);

/* The size of a BTS record in bytes: */
#define BTS_RECORD_SIZE		24

#define PEBS_FIXUP_SIZE		PAGE_SIZE

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
		unsigned int ld_data_blk:1;
		unsigned int ld_addr_blk:1;
		unsigned int ld_reserved:24;
	};
	struct {
		unsigned int st_l1d_hit:1;
		unsigned int st_reserved1:3;
		unsigned int st_stlb_miss:1;
		unsigned int st_locked:1;
		unsigned int st_reserved2:26;
	};
	struct {
		unsigned int st_lat_dse:4;
		unsigned int st_lat_stlb_miss:1;
		unsigned int st_lat_locked:1;
		unsigned int ld_reserved3:26;
	};
	struct {
		unsigned int mtl_dse:5;
		unsigned int mtl_locked:1;
		unsigned int mtl_stlb_miss:1;
		unsigned int mtl_fwd_blk:1;
		unsigned int ld_reserved4:24;
	};
};


/*
 * Map PEBS Load Latency Data Source encodings to generic
 * memory data source information
 */
#define P(a, b) PERF_MEM_S(a, b)
#define OP_LH (P(OP, LOAD) | P(LVL, HIT))
#define LEVEL(x) P(LVLNUM, x)
#define REM P(REMOTE, REMOTE)
#define SNOOP_NONE_MISS (P(SNOOP, NONE) | P(SNOOP, MISS))

/* Version for Sandy Bridge and later */
static u64 pebs_data_source[] = {
	P(OP, LOAD) | P(LVL, MISS) | LEVEL(L3) | P(SNOOP, NA),/* 0x00:ukn L3 */
	OP_LH | P(LVL, L1)  | LEVEL(L1) | P(SNOOP, NONE),  /* 0x01: L1 local */
	OP_LH | P(LVL, LFB) | LEVEL(LFB) | P(SNOOP, NONE), /* 0x02: LFB hit */
	OP_LH | P(LVL, L2)  | LEVEL(L2) | P(SNOOP, NONE),  /* 0x03: L2 hit */
	OP_LH | P(LVL, L3)  | LEVEL(L3) | P(SNOOP, NONE),  /* 0x04: L3 hit */
	OP_LH | P(LVL, L3)  | LEVEL(L3) | P(SNOOP, MISS),  /* 0x05: L3 hit, snoop miss */
	OP_LH | P(LVL, L3)  | LEVEL(L3) | P(SNOOP, HIT),   /* 0x06: L3 hit, snoop hit */
	OP_LH | P(LVL, L3)  | LEVEL(L3) | P(SNOOP, HITM),  /* 0x07: L3 hit, snoop hitm */
	OP_LH | P(LVL, REM_CCE1) | REM | LEVEL(L3) | P(SNOOP, HIT),  /* 0x08: L3 miss snoop hit */
	OP_LH | P(LVL, REM_CCE1) | REM | LEVEL(L3) | P(SNOOP, HITM), /* 0x09: L3 miss snoop hitm*/
	OP_LH | P(LVL, LOC_RAM)  | LEVEL(RAM) | P(SNOOP, HIT),       /* 0x0a: L3 miss, shared */
	OP_LH | P(LVL, REM_RAM1) | REM | LEVEL(L3) | P(SNOOP, HIT),  /* 0x0b: L3 miss, shared */
	OP_LH | P(LVL, LOC_RAM)  | LEVEL(RAM) | SNOOP_NONE_MISS,     /* 0x0c: L3 miss, excl */
	OP_LH | P(LVL, REM_RAM1) | LEVEL(RAM) | REM | SNOOP_NONE_MISS, /* 0x0d: L3 miss, excl */
	OP_LH | P(LVL, IO)  | LEVEL(NA) | P(SNOOP, NONE), /* 0x0e: I/O */
	OP_LH | P(LVL, UNC) | LEVEL(NA) | P(SNOOP, NONE), /* 0x0f: uncached */
};

/* Patch up minor differences in the bits */
void __init intel_pmu_pebs_data_source_nhm(void)
{
	pebs_data_source[0x05] = OP_LH | P(LVL, L3) | LEVEL(L3) | P(SNOOP, HIT);
	pebs_data_source[0x06] = OP_LH | P(LVL, L3) | LEVEL(L3) | P(SNOOP, HITM);
	pebs_data_source[0x07] = OP_LH | P(LVL, L3) | LEVEL(L3) | P(SNOOP, HITM);
}

static void __init __intel_pmu_pebs_data_source_skl(bool pmem, u64 *data_source)
{
	u64 pmem_or_l4 = pmem ? LEVEL(PMEM) : LEVEL(L4);

	data_source[0x08] = OP_LH | pmem_or_l4 | P(SNOOP, HIT);
	data_source[0x09] = OP_LH | pmem_or_l4 | REM | P(SNOOP, HIT);
	data_source[0x0b] = OP_LH | LEVEL(RAM) | REM | P(SNOOP, NONE);
	data_source[0x0c] = OP_LH | LEVEL(ANY_CACHE) | REM | P(SNOOPX, FWD);
	data_source[0x0d] = OP_LH | LEVEL(ANY_CACHE) | REM | P(SNOOP, HITM);
}

void __init intel_pmu_pebs_data_source_skl(bool pmem)
{
	__intel_pmu_pebs_data_source_skl(pmem, pebs_data_source);
}

static void __init __intel_pmu_pebs_data_source_grt(u64 *data_source)
{
	data_source[0x05] = OP_LH | P(LVL, L3) | LEVEL(L3) | P(SNOOP, HIT);
	data_source[0x06] = OP_LH | P(LVL, L3) | LEVEL(L3) | P(SNOOP, HITM);
	data_source[0x08] = OP_LH | P(LVL, L3) | LEVEL(L3) | P(SNOOPX, FWD);
}

void __init intel_pmu_pebs_data_source_grt(void)
{
	__intel_pmu_pebs_data_source_grt(pebs_data_source);
}

void __init intel_pmu_pebs_data_source_adl(void)
{
	u64 *data_source;

	data_source = x86_pmu.hybrid_pmu[X86_HYBRID_PMU_CORE_IDX].pebs_data_source;
	memcpy(data_source, pebs_data_source, sizeof(pebs_data_source));
	__intel_pmu_pebs_data_source_skl(false, data_source);

	data_source = x86_pmu.hybrid_pmu[X86_HYBRID_PMU_ATOM_IDX].pebs_data_source;
	memcpy(data_source, pebs_data_source, sizeof(pebs_data_source));
	__intel_pmu_pebs_data_source_grt(data_source);
}

static void __init __intel_pmu_pebs_data_source_cmt(u64 *data_source)
{
	data_source[0x07] = OP_LH | P(LVL, L3) | LEVEL(L3) | P(SNOOPX, FWD);
	data_source[0x08] = OP_LH | P(LVL, L3) | LEVEL(L3) | P(SNOOP, HITM);
	data_source[0x0a] = OP_LH | P(LVL, LOC_RAM)  | LEVEL(RAM) | P(SNOOP, NONE);
	data_source[0x0b] = OP_LH | LEVEL(RAM) | REM | P(SNOOP, NONE);
	data_source[0x0c] = OP_LH | LEVEL(RAM) | REM | P(SNOOPX, FWD);
	data_source[0x0d] = OP_LH | LEVEL(RAM) | REM | P(SNOOP, HITM);
}

void __init intel_pmu_pebs_data_source_mtl(void)
{
	u64 *data_source;

	data_source = x86_pmu.hybrid_pmu[X86_HYBRID_PMU_CORE_IDX].pebs_data_source;
	memcpy(data_source, pebs_data_source, sizeof(pebs_data_source));
	__intel_pmu_pebs_data_source_skl(false, data_source);

	data_source = x86_pmu.hybrid_pmu[X86_HYBRID_PMU_ATOM_IDX].pebs_data_source;
	memcpy(data_source, pebs_data_source, sizeof(pebs_data_source));
	__intel_pmu_pebs_data_source_cmt(data_source);
}

void __init intel_pmu_pebs_data_source_cmt(void)
{
	__intel_pmu_pebs_data_source_cmt(pebs_data_source);
}

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

static u64 precise_datala_hsw(struct perf_event *event, u64 status)
{
	union perf_mem_data_src dse;

	dse.val = PERF_MEM_NA;

	if (event->hw.flags & PERF_X86_EVENT_PEBS_ST_HSW)
		dse.mem_op = PERF_MEM_OP_STORE;
	else if (event->hw.flags & PERF_X86_EVENT_PEBS_LD_HSW)
		dse.mem_op = PERF_MEM_OP_LOAD;

	/*
	 * L1 info only valid for following events:
	 *
	 * MEM_UOPS_RETIRED.STLB_MISS_STORES
	 * MEM_UOPS_RETIRED.LOCK_STORES
	 * MEM_UOPS_RETIRED.SPLIT_STORES
	 * MEM_UOPS_RETIRED.ALL_STORES
	 */
	if (event->hw.flags & PERF_X86_EVENT_PEBS_ST_HSW) {
		if (status & 1)
			dse.mem_lvl = PERF_MEM_LVL_L1 | PERF_MEM_LVL_HIT;
		else
			dse.mem_lvl = PERF_MEM_LVL_L1 | PERF_MEM_LVL_MISS;
	}
	return dse.val;
}

static inline void pebs_set_tlb_lock(u64 *val, bool tlb, bool lock)
{
	/*
	 * TLB access
	 * 0 = did not miss 2nd level TLB
	 * 1 = missed 2nd level TLB
	 */
	if (tlb)
		*val |= P(TLB, MISS) | P(TLB, L2);
	else
		*val |= P(TLB, HIT) | P(TLB, L1) | P(TLB, L2);

	/* locked prefix */
	if (lock)
		*val |= P(LOCK, LOCKED);
}

/* Retrieve the latency data for e-core of ADL */
static u64 __adl_latency_data_small(struct perf_event *event, u64 status,
				     u8 dse, bool tlb, bool lock, bool blk)
{
	u64 val;

	WARN_ON_ONCE(hybrid_pmu(event->pmu)->pmu_type == hybrid_big);

	dse &= PERF_PEBS_DATA_SOURCE_MASK;
	val = hybrid_var(event->pmu, pebs_data_source)[dse];

	pebs_set_tlb_lock(&val, tlb, lock);

	if (blk)
		val |= P(BLK, DATA);
	else
		val |= P(BLK, NA);

	return val;
}

u64 adl_latency_data_small(struct perf_event *event, u64 status)
{
	union intel_x86_pebs_dse dse;

	dse.val = status;

	return __adl_latency_data_small(event, status, dse.ld_dse,
					dse.ld_locked, dse.ld_stlb_miss,
					dse.ld_data_blk);
}

/* Retrieve the latency data for e-core of MTL */
u64 mtl_latency_data_small(struct perf_event *event, u64 status)
{
	union intel_x86_pebs_dse dse;

	dse.val = status;

	return __adl_latency_data_small(event, status, dse.mtl_dse,
					dse.mtl_stlb_miss, dse.mtl_locked,
					dse.mtl_fwd_blk);
}

static u64 load_latency_data(struct perf_event *event, u64 status)
{
	union intel_x86_pebs_dse dse;
	u64 val;

	dse.val = status;

	/*
	 * use the mapping table for bit 0-3
	 */
	val = hybrid_var(event->pmu, pebs_data_source)[dse.ld_dse];

	/*
	 * Nehalem models do not support TLB, Lock infos
	 */
	if (x86_pmu.pebs_no_tlb) {
		val |= P(TLB, NA) | P(LOCK, NA);
		return val;
	}

	pebs_set_tlb_lock(&val, dse.ld_stlb_miss, dse.ld_locked);

	/*
	 * Ice Lake and earlier models do not support block infos.
	 */
	if (!x86_pmu.pebs_block) {
		val |= P(BLK, NA);
		return val;
	}
	/*
	 * bit 6: load was blocked since its data could not be forwarded
	 *        from a preceding store
	 */
	if (dse.ld_data_blk)
		val |= P(BLK, DATA);

	/*
	 * bit 7: load was blocked due to potential address conflict with
	 *        a preceding store
	 */
	if (dse.ld_addr_blk)
		val |= P(BLK, ADDR);

	if (!dse.ld_data_blk && !dse.ld_addr_blk)
		val |= P(BLK, NA);

	return val;
}

static u64 store_latency_data(struct perf_event *event, u64 status)
{
	union intel_x86_pebs_dse dse;
	union perf_mem_data_src src;
	u64 val;

	dse.val = status;

	/*
	 * use the mapping table for bit 0-3
	 */
	val = hybrid_var(event->pmu, pebs_data_source)[dse.st_lat_dse];

	pebs_set_tlb_lock(&val, dse.st_lat_stlb_miss, dse.st_lat_locked);

	val |= P(BLK, NA);

	/*
	 * the pebs_data_source table is only for loads
	 * so override the mem_op to say STORE instead
	 */
	src.val = val;
	src.mem_op = P(OP,STORE);

	return src.val;
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
	u64 flags, ip;
	u64 ax, bx, cx, dx;
	u64 si, di, bp, sp;
	u64 r8,  r9,  r10, r11;
	u64 r12, r13, r14, r15;
	u64 status, dla, dse, lat;
	u64 real_ip, tsx_tuning;
};

union hsw_tsx_tuning {
	struct {
		u32 cycles_last_block     : 32,
		    hle_abort		  : 1,
		    rtm_abort		  : 1,
		    instruction_abort     : 1,
		    non_instruction_abort : 1,
		    retry		  : 1,
		    data_conflict	  : 1,
		    capacity_writes	  : 1,
		    capacity_reads	  : 1;
	};
	u64	    value;
};

#define PEBS_HSW_TSX_FLAGS	0xff00000000ULL

/* Same as HSW, plus TSC */

struct pebs_record_skl {
	u64 flags, ip;
	u64 ax, bx, cx, dx;
	u64 si, di, bp, sp;
	u64 r8,  r9,  r10, r11;
	u64 r12, r13, r14, r15;
	u64 status, dla, dse, lat;
	u64 real_ip, tsx_tuning;
	u64 tsc;
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

static DEFINE_PER_CPU(void *, insn_buffer);

static void ds_update_cea(void *cea, void *addr, size_t size, pgprot_t prot)
{
	unsigned long start = (unsigned long)cea;
	phys_addr_t pa;
	size_t msz = 0;

	pa = virt_to_phys(addr);

	preempt_disable();
	for (; msz < size; msz += PAGE_SIZE, pa += PAGE_SIZE, cea += PAGE_SIZE)
		cea_set_pte(cea, pa, prot);

	/*
	 * This is a cross-CPU update of the cpu_entry_area, we must shoot down
	 * all TLB entries for it.
	 */
	flush_tlb_kernel_range(start, start + size);
	preempt_enable();
}

static void ds_clear_cea(void *cea, size_t size)
{
	unsigned long start = (unsigned long)cea;
	size_t msz = 0;

	preempt_disable();
	for (; msz < size; msz += PAGE_SIZE, cea += PAGE_SIZE)
		cea_set_pte(cea, 0, PAGE_NONE);

	flush_tlb_kernel_range(start, start + size);
	preempt_enable();
}

static void *dsalloc_pages(size_t size, gfp_t flags, int cpu)
{
	unsigned int order = get_order(size);
	int node = cpu_to_node(cpu);
	struct page *page;

	page = __alloc_pages_node(node, flags | __GFP_ZERO, order);
	return page ? page_address(page) : NULL;
}

static void dsfree_pages(const void *buffer, size_t size)
{
	if (buffer)
		free_pages((unsigned long)buffer, get_order(size));
}

static int alloc_pebs_buffer(int cpu)
{
	struct cpu_hw_events *hwev = per_cpu_ptr(&cpu_hw_events, cpu);
	struct debug_store *ds = hwev->ds;
	size_t bsiz = x86_pmu.pebs_buffer_size;
	int max, node = cpu_to_node(cpu);
	void *buffer, *insn_buff, *cea;

	if (!x86_pmu.pebs)
		return 0;

	buffer = dsalloc_pages(bsiz, GFP_KERNEL, cpu);
	if (unlikely(!buffer))
		return -ENOMEM;

	/*
	 * HSW+ already provides us the eventing ip; no need to allocate this
	 * buffer then.
	 */
	if (x86_pmu.intel_cap.pebs_format < 2) {
		insn_buff = kzalloc_node(PEBS_FIXUP_SIZE, GFP_KERNEL, node);
		if (!insn_buff) {
			dsfree_pages(buffer, bsiz);
			return -ENOMEM;
		}
		per_cpu(insn_buffer, cpu) = insn_buff;
	}
	hwev->ds_pebs_vaddr = buffer;
	/* Update the cpu entry area mapping */
	cea = &get_cpu_entry_area(cpu)->cpu_debug_buffers.pebs_buffer;
	ds->pebs_buffer_base = (unsigned long) cea;
	ds_update_cea(cea, buffer, bsiz, PAGE_KERNEL);
	ds->pebs_index = ds->pebs_buffer_base;
	max = x86_pmu.pebs_record_size * (bsiz / x86_pmu.pebs_record_size);
	ds->pebs_absolute_maximum = ds->pebs_buffer_base + max;
	return 0;
}

static void release_pebs_buffer(int cpu)
{
	struct cpu_hw_events *hwev = per_cpu_ptr(&cpu_hw_events, cpu);
	void *cea;

	if (!x86_pmu.pebs)
		return;

	kfree(per_cpu(insn_buffer, cpu));
	per_cpu(insn_buffer, cpu) = NULL;

	/* Clear the fixmap */
	cea = &get_cpu_entry_area(cpu)->cpu_debug_buffers.pebs_buffer;
	ds_clear_cea(cea, x86_pmu.pebs_buffer_size);
	dsfree_pages(hwev->ds_pebs_vaddr, x86_pmu.pebs_buffer_size);
	hwev->ds_pebs_vaddr = NULL;
}

static int alloc_bts_buffer(int cpu)
{
	struct cpu_hw_events *hwev = per_cpu_ptr(&cpu_hw_events, cpu);
	struct debug_store *ds = hwev->ds;
	void *buffer, *cea;
	int max;

	if (!x86_pmu.bts)
		return 0;

	buffer = dsalloc_pages(BTS_BUFFER_SIZE, GFP_KERNEL | __GFP_NOWARN, cpu);
	if (unlikely(!buffer)) {
		WARN_ONCE(1, "%s: BTS buffer allocation failure\n", __func__);
		return -ENOMEM;
	}
	hwev->ds_bts_vaddr = buffer;
	/* Update the fixmap */
	cea = &get_cpu_entry_area(cpu)->cpu_debug_buffers.bts_buffer;
	ds->bts_buffer_base = (unsigned long) cea;
	ds_update_cea(cea, buffer, BTS_BUFFER_SIZE, PAGE_KERNEL);
	ds->bts_index = ds->bts_buffer_base;
	max = BTS_BUFFER_SIZE / BTS_RECORD_SIZE;
	ds->bts_absolute_maximum = ds->bts_buffer_base +
					max * BTS_RECORD_SIZE;
	ds->bts_interrupt_threshold = ds->bts_absolute_maximum -
					(max / 16) * BTS_RECORD_SIZE;
	return 0;
}

static void release_bts_buffer(int cpu)
{
	struct cpu_hw_events *hwev = per_cpu_ptr(&cpu_hw_events, cpu);
	void *cea;

	if (!x86_pmu.bts)
		return;

	/* Clear the fixmap */
	cea = &get_cpu_entry_area(cpu)->cpu_debug_buffers.bts_buffer;
	ds_clear_cea(cea, BTS_BUFFER_SIZE);
	dsfree_pages(hwev->ds_bts_vaddr, BTS_BUFFER_SIZE);
	hwev->ds_bts_vaddr = NULL;
}

static int alloc_ds_buffer(int cpu)
{
	struct debug_store *ds = &get_cpu_entry_area(cpu)->cpu_debug_store;

	memset(ds, 0, sizeof(*ds));
	per_cpu(cpu_hw_events, cpu).ds = ds;
	return 0;
}

static void release_ds_buffer(int cpu)
{
	per_cpu(cpu_hw_events, cpu).ds = NULL;
}

void release_ds_buffers(void)
{
	int cpu;

	if (!x86_pmu.bts && !x86_pmu.pebs)
		return;

	for_each_possible_cpu(cpu)
		release_ds_buffer(cpu);

	for_each_possible_cpu(cpu) {
		/*
		 * Again, ignore errors from offline CPUs, they will no longer
		 * observe cpu_hw_events.ds and not program the DS_AREA when
		 * they come up.
		 */
		fini_debug_store_on_cpu(cpu);
	}

	for_each_possible_cpu(cpu) {
		release_pebs_buffer(cpu);
		release_bts_buffer(cpu);
	}
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

		for_each_possible_cpu(cpu) {
			/*
			 * Ignores wrmsr_on_cpu() errors for offline CPUs they
			 * will get this call through intel_pmu_cpu_starting().
			 */
			init_debug_store_on_cpu(cpu);
		}
	}
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
	if (config & ARCH_PERFMON_EVENTSEL_INT)
		debugctlmsr |= DEBUGCTLMSR_BTINT;

	if (!(config & ARCH_PERFMON_EVENTSEL_OS))
		debugctlmsr |= DEBUGCTLMSR_BTS_OFF_OS;

	if (!(config & ARCH_PERFMON_EVENTSEL_USR))
		debugctlmsr |= DEBUGCTLMSR_BTS_OFF_USR;

	update_debugctlmsr(debugctlmsr);
}

void intel_pmu_disable_bts(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
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
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct debug_store *ds = cpuc->ds;
	struct bts_record {
		u64	from;
		u64	to;
		u64	flags;
	};
	struct perf_event *event = cpuc->events[INTEL_PMC_IDX_FIXED_BTS];
	struct bts_record *at, *base, *top;
	struct perf_output_handle handle;
	struct perf_event_header header;
	struct perf_sample_data data;
	unsigned long skip = 0;
	struct pt_regs regs;

	if (!event)
		return 0;

	if (!x86_pmu.bts_active)
		return 0;

	base = (struct bts_record *)(unsigned long)ds->bts_buffer_base;
	top  = (struct bts_record *)(unsigned long)ds->bts_index;

	if (top <= base)
		return 0;

	memset(&regs, 0, sizeof(regs));

	ds->bts_index = ds->bts_buffer_base;

	perf_sample_data_init(&data, 0, event->hw.last_period);

	/*
	 * BTS leaks kernel addresses in branches across the cpl boundary,
	 * such as traps or system calls, so unless the user is asking for
	 * kernel tracing (and right now it's not possible), we'd need to
	 * filter them out. But first we need to count how many of those we
	 * have in the current batch. This is an extra O(n) pass, however,
	 * it's much faster than the other one especially considering that
	 * n <= 2560 (BTS_BUFFER_SIZE / BTS_RECORD_SIZE * 15/16; see the
	 * alloc_bts_buffer()).
	 */
	for (at = base; at < top; at++) {
		/*
		 * Note that right now *this* BTS code only works if
		 * attr::exclude_kernel is set, but let's keep this extra
		 * check here in case that changes.
		 */
		if (event->attr.exclude_kernel &&
		    (kernel_ip(at->from) || kernel_ip(at->to)))
			skip++;
	}

	/*
	 * Prepare a generic sample, i.e. fill in the invariant fields.
	 * We will overwrite the from and to address before we output
	 * the sample.
	 */
	rcu_read_lock();
	perf_prepare_sample(&data, event, &regs);
	perf_prepare_header(&header, &data, event, &regs);

	if (perf_output_begin(&handle, &data, event,
			      header.size * (top - base - skip)))
		goto unlock;

	for (at = base; at < top; at++) {
		/* Filter out any records that contain kernel addresses. */
		if (event->attr.exclude_kernel &&
		    (kernel_ip(at->from) || kernel_ip(at->to)))
			continue;

		data.ip		= at->from;
		data.addr	= at->to;

		perf_output_sample(&handle, &header, &data, event);
	}

	perf_output_end(&handle);

	/* There's new data available. */
	event->hw.interrupts++;
	event->pending_kill = POLL_IN;
unlock:
	rcu_read_unlock();
	return 1;
}

static inline void intel_pmu_drain_pebs_buffer(void)
{
	struct perf_sample_data data;

	x86_pmu.drain_pebs(NULL, &data);
}

/*
 * PEBS
 */
struct event_constraint intel_core2_pebs_event_constraints[] = {
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x00c0, 0x1), /* INST_RETIRED.ANY */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0xfec1, 0x1), /* X87_OPS_RETIRED.ANY */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x00c5, 0x1), /* BR_INST_RETIRED.MISPRED */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x1fc7, 0x1), /* SIMD_INST_RETURED.ANY */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xcb, 0x1),    /* MEM_LOAD_RETIRED.* */
	/* INST_RETIRED.ANY_P, inv=1, cmask=16 (cycles:p). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108000c0, 0x01),
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_atom_pebs_event_constraints[] = {
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x00c0, 0x1), /* INST_RETIRED.ANY */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x00c5, 0x1), /* MISPREDICTED_BRANCH_RETIRED */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xcb, 0x1),    /* MEM_LOAD_RETIRED.* */
	/* INST_RETIRED.ANY_P, inv=1, cmask=16 (cycles:p). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108000c0, 0x01),
	/* Allow all events as PEBS with no flags */
	INTEL_ALL_EVENT_CONSTRAINT(0, 0x1),
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_slm_pebs_event_constraints[] = {
	/* INST_RETIRED.ANY_P, inv=1, cmask=16 (cycles:p). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108000c0, 0x1),
	/* Allow all events as PEBS with no flags */
	INTEL_ALL_EVENT_CONSTRAINT(0, 0x1),
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_glm_pebs_event_constraints[] = {
	/* Allow all events as PEBS with no flags */
	INTEL_ALL_EVENT_CONSTRAINT(0, 0x1),
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_grt_pebs_event_constraints[] = {
	/* Allow all events as PEBS with no flags */
	INTEL_HYBRID_LAT_CONSTRAINT(0x5d0, 0x3),
	INTEL_HYBRID_LAT_CONSTRAINT(0x6d0, 0xf),
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_nehalem_pebs_event_constraints[] = {
	INTEL_PLD_CONSTRAINT(0x100b, 0xf),      /* MEM_INST_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT(0x0f, 0xf),    /* MEM_UNCORE_RETIRED.* */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x010c, 0xf), /* MEM_STORE_RETIRED.DTLB_MISS */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xc0, 0xf),    /* INST_RETIRED.ANY */
	INTEL_EVENT_CONSTRAINT(0xc2, 0xf),    /* UOPS_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xc4, 0xf),    /* BR_INST_RETIRED.* */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x02c5, 0xf), /* BR_MISP_RETIRED.NEAR_CALL */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xc7, 0xf),    /* SSEX_UOPS_RETIRED.* */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x20c8, 0xf), /* ITLB_MISS_RETIRED */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xcb, 0xf),    /* MEM_LOAD_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xf7, 0xf),    /* FP_ASSIST.* */
	/* INST_RETIRED.ANY_P, inv=1, cmask=16 (cycles:p). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108000c0, 0x0f),
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_westmere_pebs_event_constraints[] = {
	INTEL_PLD_CONSTRAINT(0x100b, 0xf),      /* MEM_INST_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT(0x0f, 0xf),    /* MEM_UNCORE_RETIRED.* */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x010c, 0xf), /* MEM_STORE_RETIRED.DTLB_MISS */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xc0, 0xf),    /* INSTR_RETIRED.* */
	INTEL_EVENT_CONSTRAINT(0xc2, 0xf),    /* UOPS_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xc4, 0xf),    /* BR_INST_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xc5, 0xf),    /* BR_MISP_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xc7, 0xf),    /* SSEX_UOPS_RETIRED.* */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x20c8, 0xf), /* ITLB_MISS_RETIRED */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xcb, 0xf),    /* MEM_LOAD_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT(0xf7, 0xf),    /* FP_ASSIST.* */
	/* INST_RETIRED.ANY_P, inv=1, cmask=16 (cycles:p). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108000c0, 0x0f),
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_snb_pebs_event_constraints[] = {
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x01c0, 0x2), /* INST_RETIRED.PRECDIST */
	INTEL_PLD_CONSTRAINT(0x01cd, 0x8),    /* MEM_TRANS_RETIRED.LAT_ABOVE_THR */
	INTEL_PST_CONSTRAINT(0x02cd, 0x8),    /* MEM_TRANS_RETIRED.PRECISE_STORES */
	/* UOPS_RETIRED.ALL, inv=1, cmask=16 (cycles:p). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108001c2, 0xf),
        INTEL_EXCLEVT_CONSTRAINT(0xd0, 0xf),    /* MEM_UOP_RETIRED.* */
        INTEL_EXCLEVT_CONSTRAINT(0xd1, 0xf),    /* MEM_LOAD_UOPS_RETIRED.* */
        INTEL_EXCLEVT_CONSTRAINT(0xd2, 0xf),    /* MEM_LOAD_UOPS_LLC_HIT_RETIRED.* */
        INTEL_EXCLEVT_CONSTRAINT(0xd3, 0xf),    /* MEM_LOAD_UOPS_LLC_MISS_RETIRED.* */
	/* Allow all events as PEBS with no flags */
	INTEL_ALL_EVENT_CONSTRAINT(0, 0xf),
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_ivb_pebs_event_constraints[] = {
        INTEL_FLAGS_UEVENT_CONSTRAINT(0x01c0, 0x2), /* INST_RETIRED.PRECDIST */
        INTEL_PLD_CONSTRAINT(0x01cd, 0x8),    /* MEM_TRANS_RETIRED.LAT_ABOVE_THR */
	INTEL_PST_CONSTRAINT(0x02cd, 0x8),    /* MEM_TRANS_RETIRED.PRECISE_STORES */
	/* UOPS_RETIRED.ALL, inv=1, cmask=16 (cycles:p). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108001c2, 0xf),
	/* INST_RETIRED.PREC_DIST, inv=1, cmask=16 (cycles:ppp). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108001c0, 0x2),
	INTEL_EXCLEVT_CONSTRAINT(0xd0, 0xf),    /* MEM_UOP_RETIRED.* */
	INTEL_EXCLEVT_CONSTRAINT(0xd1, 0xf),    /* MEM_LOAD_UOPS_RETIRED.* */
	INTEL_EXCLEVT_CONSTRAINT(0xd2, 0xf),    /* MEM_LOAD_UOPS_LLC_HIT_RETIRED.* */
	INTEL_EXCLEVT_CONSTRAINT(0xd3, 0xf),    /* MEM_LOAD_UOPS_LLC_MISS_RETIRED.* */
	/* Allow all events as PEBS with no flags */
	INTEL_ALL_EVENT_CONSTRAINT(0, 0xf),
        EVENT_CONSTRAINT_END
};

struct event_constraint intel_hsw_pebs_event_constraints[] = {
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x01c0, 0x2), /* INST_RETIRED.PRECDIST */
	INTEL_PLD_CONSTRAINT(0x01cd, 0xf),    /* MEM_TRANS_RETIRED.* */
	/* UOPS_RETIRED.ALL, inv=1, cmask=16 (cycles:p). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108001c2, 0xf),
	/* INST_RETIRED.PREC_DIST, inv=1, cmask=16 (cycles:ppp). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108001c0, 0x2),
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_NA(0x01c2, 0xf), /* UOPS_RETIRED.ALL */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_XLD(0x11d0, 0xf), /* MEM_UOPS_RETIRED.STLB_MISS_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_XLD(0x21d0, 0xf), /* MEM_UOPS_RETIRED.LOCK_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_XLD(0x41d0, 0xf), /* MEM_UOPS_RETIRED.SPLIT_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_XLD(0x81d0, 0xf), /* MEM_UOPS_RETIRED.ALL_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_XST(0x12d0, 0xf), /* MEM_UOPS_RETIRED.STLB_MISS_STORES */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_XST(0x42d0, 0xf), /* MEM_UOPS_RETIRED.SPLIT_STORES */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_XST(0x82d0, 0xf), /* MEM_UOPS_RETIRED.ALL_STORES */
	INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_XLD(0xd1, 0xf),    /* MEM_LOAD_UOPS_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_XLD(0xd2, 0xf),    /* MEM_LOAD_UOPS_L3_HIT_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_XLD(0xd3, 0xf),    /* MEM_LOAD_UOPS_L3_MISS_RETIRED.* */
	/* Allow all events as PEBS with no flags */
	INTEL_ALL_EVENT_CONSTRAINT(0, 0xf),
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_bdw_pebs_event_constraints[] = {
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x01c0, 0x2), /* INST_RETIRED.PRECDIST */
	INTEL_PLD_CONSTRAINT(0x01cd, 0xf),    /* MEM_TRANS_RETIRED.* */
	/* UOPS_RETIRED.ALL, inv=1, cmask=16 (cycles:p). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108001c2, 0xf),
	/* INST_RETIRED.PREC_DIST, inv=1, cmask=16 (cycles:ppp). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108001c0, 0x2),
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_NA(0x01c2, 0xf), /* UOPS_RETIRED.ALL */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x11d0, 0xf), /* MEM_UOPS_RETIRED.STLB_MISS_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x21d0, 0xf), /* MEM_UOPS_RETIRED.LOCK_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x41d0, 0xf), /* MEM_UOPS_RETIRED.SPLIT_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x81d0, 0xf), /* MEM_UOPS_RETIRED.ALL_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x12d0, 0xf), /* MEM_UOPS_RETIRED.STLB_MISS_STORES */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x42d0, 0xf), /* MEM_UOPS_RETIRED.SPLIT_STORES */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x82d0, 0xf), /* MEM_UOPS_RETIRED.ALL_STORES */
	INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_LD(0xd1, 0xf),    /* MEM_LOAD_UOPS_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_LD(0xd2, 0xf),    /* MEM_LOAD_UOPS_L3_HIT_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_LD(0xd3, 0xf),    /* MEM_LOAD_UOPS_L3_MISS_RETIRED.* */
	/* Allow all events as PEBS with no flags */
	INTEL_ALL_EVENT_CONSTRAINT(0, 0xf),
	EVENT_CONSTRAINT_END
};


struct event_constraint intel_skl_pebs_event_constraints[] = {
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x1c0, 0x2),	/* INST_RETIRED.PREC_DIST */
	/* INST_RETIRED.PREC_DIST, inv=1, cmask=16 (cycles:ppp). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108001c0, 0x2),
	/* INST_RETIRED.TOTAL_CYCLES_PS (inv=1, cmask=16) (cycles:p). */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x108000c0, 0x0f),
	INTEL_PLD_CONSTRAINT(0x1cd, 0xf),		      /* MEM_TRANS_RETIRED.* */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x11d0, 0xf), /* MEM_INST_RETIRED.STLB_MISS_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x12d0, 0xf), /* MEM_INST_RETIRED.STLB_MISS_STORES */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x21d0, 0xf), /* MEM_INST_RETIRED.LOCK_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x22d0, 0xf), /* MEM_INST_RETIRED.LOCK_STORES */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x41d0, 0xf), /* MEM_INST_RETIRED.SPLIT_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x42d0, 0xf), /* MEM_INST_RETIRED.SPLIT_STORES */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x81d0, 0xf), /* MEM_INST_RETIRED.ALL_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x82d0, 0xf), /* MEM_INST_RETIRED.ALL_STORES */
	INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_LD(0xd1, 0xf),    /* MEM_LOAD_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_LD(0xd2, 0xf),    /* MEM_LOAD_L3_HIT_RETIRED.* */
	INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_LD(0xd3, 0xf),    /* MEM_LOAD_L3_MISS_RETIRED.* */
	/* Allow all events as PEBS with no flags */
	INTEL_ALL_EVENT_CONSTRAINT(0, 0xf),
	EVENT_CONSTRAINT_END
};

struct event_constraint intel_icl_pebs_event_constraints[] = {
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x01c0, 0x100000000ULL),	/* old INST_RETIRED.PREC_DIST */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x0100, 0x100000000ULL),	/* INST_RETIRED.PREC_DIST */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x0400, 0x800000000ULL),	/* SLOTS */

	INTEL_PLD_CONSTRAINT(0x1cd, 0xff),			/* MEM_TRANS_RETIRED.LOAD_LATENCY */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x11d0, 0xf),	/* MEM_INST_RETIRED.STLB_MISS_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x12d0, 0xf),	/* MEM_INST_RETIRED.STLB_MISS_STORES */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x21d0, 0xf),	/* MEM_INST_RETIRED.LOCK_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x41d0, 0xf),	/* MEM_INST_RETIRED.SPLIT_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x42d0, 0xf),	/* MEM_INST_RETIRED.SPLIT_STORES */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x81d0, 0xf),	/* MEM_INST_RETIRED.ALL_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x82d0, 0xf),	/* MEM_INST_RETIRED.ALL_STORES */

	INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_LD_RANGE(0xd1, 0xd4, 0xf), /* MEM_LOAD_*_RETIRED.* */

	INTEL_FLAGS_EVENT_CONSTRAINT(0xd0, 0xf),		/* MEM_INST_RETIRED.* */

	/*
	 * Everything else is handled by PMU_FL_PEBS_ALL, because we
	 * need the full constraints from the main table.
	 */

	EVENT_CONSTRAINT_END
};

struct event_constraint intel_glc_pebs_event_constraints[] = {
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x100, 0x100000000ULL),	/* INST_RETIRED.PREC_DIST */
	INTEL_FLAGS_UEVENT_CONSTRAINT(0x0400, 0x800000000ULL),

	INTEL_FLAGS_EVENT_CONSTRAINT(0xc0, 0xfe),
	INTEL_PLD_CONSTRAINT(0x1cd, 0xfe),
	INTEL_PSD_CONSTRAINT(0x2cd, 0x1),
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x11d0, 0xf),	/* MEM_INST_RETIRED.STLB_MISS_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x12d0, 0xf),	/* MEM_INST_RETIRED.STLB_MISS_STORES */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x21d0, 0xf),	/* MEM_INST_RETIRED.LOCK_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x41d0, 0xf),	/* MEM_INST_RETIRED.SPLIT_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x42d0, 0xf),	/* MEM_INST_RETIRED.SPLIT_STORES */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_LD(0x81d0, 0xf),	/* MEM_INST_RETIRED.ALL_LOADS */
	INTEL_FLAGS_UEVENT_CONSTRAINT_DATALA_ST(0x82d0, 0xf),	/* MEM_INST_RETIRED.ALL_STORES */

	INTEL_FLAGS_EVENT_CONSTRAINT_DATALA_LD_RANGE(0xd1, 0xd4, 0xf),

	INTEL_FLAGS_EVENT_CONSTRAINT(0xd0, 0xf),

	/*
	 * Everything else is handled by PMU_FL_PEBS_ALL, because we
	 * need the full constraints from the main table.
	 */

	EVENT_CONSTRAINT_END
};

struct event_constraint *intel_pebs_constraints(struct perf_event *event)
{
	struct event_constraint *pebs_constraints = hybrid(event->pmu, pebs_constraints);
	struct event_constraint *c;

	if (!event->attr.precise_ip)
		return NULL;

	if (pebs_constraints) {
		for_each_event_constraint(c, pebs_constraints) {
			if (constraint_match(c, event->hw.config)) {
				event->hw.flags |= c->flags;
				return c;
			}
		}
	}

	/*
	 * Extended PEBS support
	 * Makes the PEBS code search the normal constraints.
	 */
	if (x86_pmu.flags & PMU_FL_PEBS_ALL)
		return NULL;

	return &emptyconstraint;
}

/*
 * We need the sched_task callback even for per-cpu events when we use
 * the large interrupt threshold, such that we can provide PID and TID
 * to PEBS samples.
 */
static inline bool pebs_needs_sched_cb(struct cpu_hw_events *cpuc)
{
	if (cpuc->n_pebs == cpuc->n_pebs_via_pt)
		return false;

	return cpuc->n_pebs && (cpuc->n_pebs == cpuc->n_large_pebs);
}

void intel_pmu_pebs_sched_task(struct perf_event_pmu_context *pmu_ctx, bool sched_in)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (!sched_in && pebs_needs_sched_cb(cpuc))
		intel_pmu_drain_pebs_buffer();
}

static inline void pebs_update_threshold(struct cpu_hw_events *cpuc)
{
	struct debug_store *ds = cpuc->ds;
	int max_pebs_events = hybrid(cpuc->pmu, max_pebs_events);
	int num_counters_fixed = hybrid(cpuc->pmu, num_counters_fixed);
	u64 threshold;
	int reserved;

	if (cpuc->n_pebs_via_pt)
		return;

	if (x86_pmu.flags & PMU_FL_PEBS_ALL)
		reserved = max_pebs_events + num_counters_fixed;
	else
		reserved = max_pebs_events;

	if (cpuc->n_pebs == cpuc->n_large_pebs) {
		threshold = ds->pebs_absolute_maximum -
			reserved * cpuc->pebs_record_size;
	} else {
		threshold = ds->pebs_buffer_base + cpuc->pebs_record_size;
	}

	ds->pebs_interrupt_threshold = threshold;
}

static void adaptive_pebs_record_size_update(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	u64 pebs_data_cfg = cpuc->pebs_data_cfg;
	int sz = sizeof(struct pebs_basic);

	if (pebs_data_cfg & PEBS_DATACFG_MEMINFO)
		sz += sizeof(struct pebs_meminfo);
	if (pebs_data_cfg & PEBS_DATACFG_GP)
		sz += sizeof(struct pebs_gprs);
	if (pebs_data_cfg & PEBS_DATACFG_XMMS)
		sz += sizeof(struct pebs_xmm);
	if (pebs_data_cfg & PEBS_DATACFG_LBRS)
		sz += x86_pmu.lbr_nr * sizeof(struct lbr_entry);

	cpuc->pebs_record_size = sz;
}

#define PERF_PEBS_MEMINFO_TYPE	(PERF_SAMPLE_ADDR | PERF_SAMPLE_DATA_SRC |   \
				PERF_SAMPLE_PHYS_ADDR |			     \
				PERF_SAMPLE_WEIGHT_TYPE |		     \
				PERF_SAMPLE_TRANSACTION |		     \
				PERF_SAMPLE_DATA_PAGE_SIZE)

static u64 pebs_update_adaptive_cfg(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	u64 sample_type = attr->sample_type;
	u64 pebs_data_cfg = 0;
	bool gprs, tsx_weight;

	if (!(sample_type & ~(PERF_SAMPLE_IP|PERF_SAMPLE_TIME)) &&
	    attr->precise_ip > 1)
		return pebs_data_cfg;

	if (sample_type & PERF_PEBS_MEMINFO_TYPE)
		pebs_data_cfg |= PEBS_DATACFG_MEMINFO;

	/*
	 * We need GPRs when:
	 * + user requested them
	 * + precise_ip < 2 for the non event IP
	 * + For RTM TSX weight we need GPRs for the abort code.
	 */
	gprs = (sample_type & PERF_SAMPLE_REGS_INTR) &&
	       (attr->sample_regs_intr & PEBS_GP_REGS);

	tsx_weight = (sample_type & PERF_SAMPLE_WEIGHT_TYPE) &&
		     ((attr->config & INTEL_ARCH_EVENT_MASK) ==
		      x86_pmu.rtm_abort_event);

	if (gprs || (attr->precise_ip < 2) || tsx_weight)
		pebs_data_cfg |= PEBS_DATACFG_GP;

	if ((sample_type & PERF_SAMPLE_REGS_INTR) &&
	    (attr->sample_regs_intr & PERF_REG_EXTENDED_MASK))
		pebs_data_cfg |= PEBS_DATACFG_XMMS;

	if (sample_type & PERF_SAMPLE_BRANCH_STACK) {
		/*
		 * For now always log all LBRs. Could configure this
		 * later.
		 */
		pebs_data_cfg |= PEBS_DATACFG_LBRS |
			((x86_pmu.lbr_nr-1) << PEBS_DATACFG_LBR_SHIFT);
	}

	return pebs_data_cfg;
}

static void
pebs_update_state(bool needed_cb, struct cpu_hw_events *cpuc,
		  struct perf_event *event, bool add)
{
	struct pmu *pmu = event->pmu;

	/*
	 * Make sure we get updated with the first PEBS event.
	 * During removal, ->pebs_data_cfg is still valid for
	 * the last PEBS event. Don't clear it.
	 */
	if ((cpuc->n_pebs == 1) && add)
		cpuc->pebs_data_cfg = PEBS_UPDATE_DS_SW;

	if (needed_cb != pebs_needs_sched_cb(cpuc)) {
		if (!needed_cb)
			perf_sched_cb_inc(pmu);
		else
			perf_sched_cb_dec(pmu);

		cpuc->pebs_data_cfg |= PEBS_UPDATE_DS_SW;
	}

	/*
	 * The PEBS record doesn't shrink on pmu::del(). Doing so would require
	 * iterating all remaining PEBS events to reconstruct the config.
	 */
	if (x86_pmu.intel_cap.pebs_baseline && add) {
		u64 pebs_data_cfg;

		pebs_data_cfg = pebs_update_adaptive_cfg(event);
		/*
		 * Be sure to update the thresholds when we change the record.
		 */
		if (pebs_data_cfg & ~cpuc->pebs_data_cfg)
			cpuc->pebs_data_cfg |= pebs_data_cfg | PEBS_UPDATE_DS_SW;
	}
}

void intel_pmu_pebs_add(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	bool needed_cb = pebs_needs_sched_cb(cpuc);

	cpuc->n_pebs++;
	if (hwc->flags & PERF_X86_EVENT_LARGE_PEBS)
		cpuc->n_large_pebs++;
	if (hwc->flags & PERF_X86_EVENT_PEBS_VIA_PT)
		cpuc->n_pebs_via_pt++;

	pebs_update_state(needed_cb, cpuc, event, true);
}

static void intel_pmu_pebs_via_pt_disable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (!is_pebs_pt(event))
		return;

	if (!(cpuc->pebs_enabled & ~PEBS_VIA_PT_MASK))
		cpuc->pebs_enabled &= ~PEBS_VIA_PT_MASK;
}

static void intel_pmu_pebs_via_pt_enable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	struct debug_store *ds = cpuc->ds;
	u64 value = ds->pebs_event_reset[hwc->idx];
	u32 base = MSR_RELOAD_PMC0;
	unsigned int idx = hwc->idx;

	if (!is_pebs_pt(event))
		return;

	if (!(event->hw.flags & PERF_X86_EVENT_LARGE_PEBS))
		cpuc->pebs_enabled |= PEBS_PMI_AFTER_EACH_RECORD;

	cpuc->pebs_enabled |= PEBS_OUTPUT_PT;

	if (hwc->idx >= INTEL_PMC_IDX_FIXED) {
		base = MSR_RELOAD_FIXED_CTR0;
		idx = hwc->idx - INTEL_PMC_IDX_FIXED;
		if (x86_pmu.intel_cap.pebs_format < 5)
			value = ds->pebs_event_reset[MAX_PEBS_EVENTS_FMT4 + idx];
		else
			value = ds->pebs_event_reset[MAX_PEBS_EVENTS + idx];
	}
	wrmsrl(base + idx, value);
}

static inline void intel_pmu_drain_large_pebs(struct cpu_hw_events *cpuc)
{
	if (cpuc->n_pebs == cpuc->n_large_pebs &&
	    cpuc->n_pebs != cpuc->n_pebs_via_pt)
		intel_pmu_drain_pebs_buffer();
}

void intel_pmu_pebs_enable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	u64 pebs_data_cfg = cpuc->pebs_data_cfg & ~PEBS_UPDATE_DS_SW;
	struct hw_perf_event *hwc = &event->hw;
	struct debug_store *ds = cpuc->ds;
	unsigned int idx = hwc->idx;

	hwc->config &= ~ARCH_PERFMON_EVENTSEL_INT;

	cpuc->pebs_enabled |= 1ULL << hwc->idx;

	if ((event->hw.flags & PERF_X86_EVENT_PEBS_LDLAT) && (x86_pmu.version < 5))
		cpuc->pebs_enabled |= 1ULL << (hwc->idx + 32);
	else if (event->hw.flags & PERF_X86_EVENT_PEBS_ST)
		cpuc->pebs_enabled |= 1ULL << 63;

	if (x86_pmu.intel_cap.pebs_baseline) {
		hwc->config |= ICL_EVENTSEL_ADAPTIVE;
		if (pebs_data_cfg != cpuc->active_pebs_data_cfg) {
			/*
			 * drain_pebs() assumes uniform record size;
			 * hence we need to drain when changing said
			 * size.
			 */
			intel_pmu_drain_large_pebs(cpuc);
			adaptive_pebs_record_size_update();
			wrmsrl(MSR_PEBS_DATA_CFG, pebs_data_cfg);
			cpuc->active_pebs_data_cfg = pebs_data_cfg;
		}
	}
	if (cpuc->pebs_data_cfg & PEBS_UPDATE_DS_SW) {
		cpuc->pebs_data_cfg = pebs_data_cfg;
		pebs_update_threshold(cpuc);
	}

	if (idx >= INTEL_PMC_IDX_FIXED) {
		if (x86_pmu.intel_cap.pebs_format < 5)
			idx = MAX_PEBS_EVENTS_FMT4 + (idx - INTEL_PMC_IDX_FIXED);
		else
			idx = MAX_PEBS_EVENTS + (idx - INTEL_PMC_IDX_FIXED);
	}

	/*
	 * Use auto-reload if possible to save a MSR write in the PMI.
	 * This must be done in pmu::start(), because PERF_EVENT_IOC_PERIOD.
	 */
	if (hwc->flags & PERF_X86_EVENT_AUTO_RELOAD) {
		ds->pebs_event_reset[idx] =
			(u64)(-hwc->sample_period) & x86_pmu.cntval_mask;
	} else {
		ds->pebs_event_reset[idx] = 0;
	}

	intel_pmu_pebs_via_pt_enable(event);
}

void intel_pmu_pebs_del(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	bool needed_cb = pebs_needs_sched_cb(cpuc);

	cpuc->n_pebs--;
	if (hwc->flags & PERF_X86_EVENT_LARGE_PEBS)
		cpuc->n_large_pebs--;
	if (hwc->flags & PERF_X86_EVENT_PEBS_VIA_PT)
		cpuc->n_pebs_via_pt--;

	pebs_update_state(needed_cb, cpuc, event, false);
}

void intel_pmu_pebs_disable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	intel_pmu_drain_large_pebs(cpuc);

	cpuc->pebs_enabled &= ~(1ULL << hwc->idx);

	if ((event->hw.flags & PERF_X86_EVENT_PEBS_LDLAT) &&
	    (x86_pmu.version < 5))
		cpuc->pebs_enabled &= ~(1ULL << (hwc->idx + 32));
	else if (event->hw.flags & PERF_X86_EVENT_PEBS_ST)
		cpuc->pebs_enabled &= ~(1ULL << 63);

	intel_pmu_pebs_via_pt_disable(event);

	if (cpuc->enabled)
		wrmsrl(MSR_IA32_PEBS_ENABLE, cpuc->pebs_enabled);

	hwc->config |= ARCH_PERFMON_EVENTSEL_INT;
}

void intel_pmu_pebs_enable_all(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (cpuc->pebs_enabled)
		wrmsrl(MSR_IA32_PEBS_ENABLE, cpuc->pebs_enabled);
}

void intel_pmu_pebs_disable_all(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (cpuc->pebs_enabled)
		__intel_pmu_pebs_disable_all();
}

static int intel_pmu_pebs_fixup_ip(struct pt_regs *regs)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	unsigned long from = cpuc->lbr_entries[0].from;
	unsigned long old_to, to = cpuc->lbr_entries[0].to;
	unsigned long ip = regs->ip;
	int is_64bit = 0;
	void *kaddr;
	int size;

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
	if ((ip - to) > PEBS_FIXUP_SIZE)
		return 0;

	/*
	 * We sampled a branch insn, rewind using the LBR stack
	 */
	if (ip == to) {
		set_linear_ip(regs, from);
		return 1;
	}

	size = ip - to;
	if (!kernel_ip(ip)) {
		int bytes;
		u8 *buf = this_cpu_read(insn_buffer);

		/* 'size' must fit our buffer, see above */
		bytes = copy_from_user_nmi(buf, (void __user *)to, size);
		if (bytes != 0)
			return 0;

		kaddr = buf;
	} else {
		kaddr = (void *)to;
	}

	do {
		struct insn insn;

		old_to = to;

#ifdef CONFIG_X86_64
		is_64bit = kernel_ip(to) || any_64bit_mode(regs);
#endif
		insn_init(&insn, kaddr, size, is_64bit);

		/*
		 * Make sure there was not a problem decoding the instruction.
		 * This is doubly important because we have an infinite loop if
		 * insn.length=0.
		 */
		if (insn_get_length(&insn))
			break;

		to += insn.length;
		kaddr += insn.length;
		size -= insn.length;
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

static inline u64 intel_get_tsx_weight(u64 tsx_tuning)
{
	if (tsx_tuning) {
		union hsw_tsx_tuning tsx = { .value = tsx_tuning };
		return tsx.cycles_last_block;
	}
	return 0;
}

static inline u64 intel_get_tsx_transaction(u64 tsx_tuning, u64 ax)
{
	u64 txn = (tsx_tuning & PEBS_HSW_TSX_FLAGS) >> 32;

	/* For RTM XABORTs also log the abort code from AX */
	if ((txn & PERF_TXN_TRANSACTION) && (ax & 1))
		txn |= ((ax >> 24) & 0xff) << PERF_TXN_ABORT_SHIFT;
	return txn;
}

static inline u64 get_pebs_status(void *n)
{
	if (x86_pmu.intel_cap.pebs_format < 4)
		return ((struct pebs_record_nhm *)n)->status;
	return ((struct pebs_basic *)n)->applicable_counters;
}

#define PERF_X86_EVENT_PEBS_HSW_PREC \
		(PERF_X86_EVENT_PEBS_ST_HSW | \
		 PERF_X86_EVENT_PEBS_LD_HSW | \
		 PERF_X86_EVENT_PEBS_NA_HSW)

static u64 get_data_src(struct perf_event *event, u64 aux)
{
	u64 val = PERF_MEM_NA;
	int fl = event->hw.flags;
	bool fst = fl & (PERF_X86_EVENT_PEBS_ST | PERF_X86_EVENT_PEBS_HSW_PREC);

	if (fl & PERF_X86_EVENT_PEBS_LDLAT)
		val = load_latency_data(event, aux);
	else if (fl & PERF_X86_EVENT_PEBS_STLAT)
		val = store_latency_data(event, aux);
	else if (fl & PERF_X86_EVENT_PEBS_LAT_HYBRID)
		val = x86_pmu.pebs_latency_data(event, aux);
	else if (fst && (fl & PERF_X86_EVENT_PEBS_HSW_PREC))
		val = precise_datala_hsw(event, aux);
	else if (fst)
		val = precise_store_data(aux);
	return val;
}

static void setup_pebs_time(struct perf_event *event,
			    struct perf_sample_data *data,
			    u64 tsc)
{
	/* Converting to a user-defined clock is not supported yet. */
	if (event->attr.use_clockid != 0)
		return;

	/*
	 * Doesn't support the conversion when the TSC is unstable.
	 * The TSC unstable case is a corner case and very unlikely to
	 * happen. If it happens, the TSC in a PEBS record will be
	 * dropped and fall back to perf_event_clock().
	 */
	if (!using_native_sched_clock() || !sched_clock_stable())
		return;

	data->time = native_sched_clock_from_tsc(tsc) + __sched_clock_offset;
	data->sample_flags |= PERF_SAMPLE_TIME;
}

#define PERF_SAMPLE_ADDR_TYPE	(PERF_SAMPLE_ADDR |		\
				 PERF_SAMPLE_PHYS_ADDR |	\
				 PERF_SAMPLE_DATA_PAGE_SIZE)

static void setup_pebs_fixed_sample_data(struct perf_event *event,
				   struct pt_regs *iregs, void *__pebs,
				   struct perf_sample_data *data,
				   struct pt_regs *regs)
{
	/*
	 * We cast to the biggest pebs_record but are careful not to
	 * unconditionally access the 'extra' entries.
	 */
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct pebs_record_skl *pebs = __pebs;
	u64 sample_type;
	int fll;

	if (pebs == NULL)
		return;

	sample_type = event->attr.sample_type;
	fll = event->hw.flags & PERF_X86_EVENT_PEBS_LDLAT;

	perf_sample_data_init(data, 0, event->hw.last_period);

	data->period = event->hw.last_period;

	/*
	 * Use latency for weight (only avail with PEBS-LL)
	 */
	if (fll && (sample_type & PERF_SAMPLE_WEIGHT_TYPE)) {
		data->weight.full = pebs->lat;
		data->sample_flags |= PERF_SAMPLE_WEIGHT_TYPE;
	}

	/*
	 * data.data_src encodes the data source
	 */
	if (sample_type & PERF_SAMPLE_DATA_SRC) {
		data->data_src.val = get_data_src(event, pebs->dse);
		data->sample_flags |= PERF_SAMPLE_DATA_SRC;
	}

	/*
	 * We must however always use iregs for the unwinder to stay sane; the
	 * record BP,SP,IP can point into thin air when the record is from a
	 * previous PMI context or an (I)RET happened between the record and
	 * PMI.
	 */
	if (sample_type & PERF_SAMPLE_CALLCHAIN)
		perf_sample_save_callchain(data, event, iregs);

	/*
	 * We use the interrupt regs as a base because the PEBS record does not
	 * contain a full regs set, specifically it seems to lack segment
	 * descriptors, which get used by things like user_mode().
	 *
	 * In the simple case fix up only the IP for PERF_SAMPLE_IP.
	 */
	*regs = *iregs;

	/*
	 * Initialize regs_>flags from PEBS,
	 * Clear exact bit (which uses x86 EFLAGS Reserved bit 3),
	 * i.e., do not rely on it being zero:
	 */
	regs->flags = pebs->flags & ~PERF_EFLAGS_EXACT;

	if (sample_type & PERF_SAMPLE_REGS_INTR) {
		regs->ax = pebs->ax;
		regs->bx = pebs->bx;
		regs->cx = pebs->cx;
		regs->dx = pebs->dx;
		regs->si = pebs->si;
		regs->di = pebs->di;

		regs->bp = pebs->bp;
		regs->sp = pebs->sp;

#ifndef CONFIG_X86_32
		regs->r8 = pebs->r8;
		regs->r9 = pebs->r9;
		regs->r10 = pebs->r10;
		regs->r11 = pebs->r11;
		regs->r12 = pebs->r12;
		regs->r13 = pebs->r13;
		regs->r14 = pebs->r14;
		regs->r15 = pebs->r15;
#endif
	}

	if (event->attr.precise_ip > 1) {
		/*
		 * Haswell and later processors have an 'eventing IP'
		 * (real IP) which fixes the off-by-1 skid in hardware.
		 * Use it when precise_ip >= 2 :
		 */
		if (x86_pmu.intel_cap.pebs_format >= 2) {
			set_linear_ip(regs, pebs->real_ip);
			regs->flags |= PERF_EFLAGS_EXACT;
		} else {
			/* Otherwise, use PEBS off-by-1 IP: */
			set_linear_ip(regs, pebs->ip);

			/*
			 * With precise_ip >= 2, try to fix up the off-by-1 IP
			 * using the LBR. If successful, the fixup function
			 * corrects regs->ip and calls set_linear_ip() on regs:
			 */
			if (intel_pmu_pebs_fixup_ip(regs))
				regs->flags |= PERF_EFLAGS_EXACT;
		}
	} else {
		/*
		 * When precise_ip == 1, return the PEBS off-by-1 IP,
		 * no fixup attempted:
		 */
		set_linear_ip(regs, pebs->ip);
	}


	if ((sample_type & PERF_SAMPLE_ADDR_TYPE) &&
	    x86_pmu.intel_cap.pebs_format >= 1) {
		data->addr = pebs->dla;
		data->sample_flags |= PERF_SAMPLE_ADDR;
	}

	if (x86_pmu.intel_cap.pebs_format >= 2) {
		/* Only set the TSX weight when no memory weight. */
		if ((sample_type & PERF_SAMPLE_WEIGHT_TYPE) && !fll) {
			data->weight.full = intel_get_tsx_weight(pebs->tsx_tuning);
			data->sample_flags |= PERF_SAMPLE_WEIGHT_TYPE;
		}
		if (sample_type & PERF_SAMPLE_TRANSACTION) {
			data->txn = intel_get_tsx_transaction(pebs->tsx_tuning,
							      pebs->ax);
			data->sample_flags |= PERF_SAMPLE_TRANSACTION;
		}
	}

	/*
	 * v3 supplies an accurate time stamp, so we use that
	 * for the time stamp.
	 *
	 * We can only do this for the default trace clock.
	 */
	if (x86_pmu.intel_cap.pebs_format >= 3)
		setup_pebs_time(event, data, pebs->tsc);

	if (has_branch_stack(event))
		perf_sample_save_brstack(data, event, &cpuc->lbr_stack, NULL);
}

static void adaptive_pebs_save_regs(struct pt_regs *regs,
				    struct pebs_gprs *gprs)
{
	regs->ax = gprs->ax;
	regs->bx = gprs->bx;
	regs->cx = gprs->cx;
	regs->dx = gprs->dx;
	regs->si = gprs->si;
	regs->di = gprs->di;
	regs->bp = gprs->bp;
	regs->sp = gprs->sp;
#ifndef CONFIG_X86_32
	regs->r8 = gprs->r8;
	regs->r9 = gprs->r9;
	regs->r10 = gprs->r10;
	regs->r11 = gprs->r11;
	regs->r12 = gprs->r12;
	regs->r13 = gprs->r13;
	regs->r14 = gprs->r14;
	regs->r15 = gprs->r15;
#endif
}

#define PEBS_LATENCY_MASK			0xffff
#define PEBS_CACHE_LATENCY_OFFSET		32
#define PEBS_RETIRE_LATENCY_OFFSET		32

/*
 * With adaptive PEBS the layout depends on what fields are configured.
 */

static void setup_pebs_adaptive_sample_data(struct perf_event *event,
					    struct pt_regs *iregs, void *__pebs,
					    struct perf_sample_data *data,
					    struct pt_regs *regs)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct pebs_basic *basic = __pebs;
	void *next_record = basic + 1;
	u64 sample_type;
	u64 format_size;
	struct pebs_meminfo *meminfo = NULL;
	struct pebs_gprs *gprs = NULL;
	struct x86_perf_regs *perf_regs;

	if (basic == NULL)
		return;

	perf_regs = container_of(regs, struct x86_perf_regs, regs);
	perf_regs->xmm_regs = NULL;

	sample_type = event->attr.sample_type;
	format_size = basic->format_size;
	perf_sample_data_init(data, 0, event->hw.last_period);
	data->period = event->hw.last_period;

	setup_pebs_time(event, data, basic->tsc);

	/*
	 * We must however always use iregs for the unwinder to stay sane; the
	 * record BP,SP,IP can point into thin air when the record is from a
	 * previous PMI context or an (I)RET happened between the record and
	 * PMI.
	 */
	if (sample_type & PERF_SAMPLE_CALLCHAIN)
		perf_sample_save_callchain(data, event, iregs);

	*regs = *iregs;
	/* The ip in basic is EventingIP */
	set_linear_ip(regs, basic->ip);
	regs->flags = PERF_EFLAGS_EXACT;

	if ((sample_type & PERF_SAMPLE_WEIGHT_STRUCT) && (x86_pmu.flags & PMU_FL_RETIRE_LATENCY))
		data->weight.var3_w = format_size >> PEBS_RETIRE_LATENCY_OFFSET & PEBS_LATENCY_MASK;

	/*
	 * The record for MEMINFO is in front of GP
	 * But PERF_SAMPLE_TRANSACTION needs gprs->ax.
	 * Save the pointer here but process later.
	 */
	if (format_size & PEBS_DATACFG_MEMINFO) {
		meminfo = next_record;
		next_record = meminfo + 1;
	}

	if (format_size & PEBS_DATACFG_GP) {
		gprs = next_record;
		next_record = gprs + 1;

		if (event->attr.precise_ip < 2) {
			set_linear_ip(regs, gprs->ip);
			regs->flags &= ~PERF_EFLAGS_EXACT;
		}

		if (sample_type & PERF_SAMPLE_REGS_INTR)
			adaptive_pebs_save_regs(regs, gprs);
	}

	if (format_size & PEBS_DATACFG_MEMINFO) {
		if (sample_type & PERF_SAMPLE_WEIGHT_TYPE) {
			u64 weight = meminfo->latency;

			if (x86_pmu.flags & PMU_FL_INSTR_LATENCY) {
				data->weight.var2_w = weight & PEBS_LATENCY_MASK;
				weight >>= PEBS_CACHE_LATENCY_OFFSET;
			}

			/*
			 * Although meminfo::latency is defined as a u64,
			 * only the lower 32 bits include the valid data
			 * in practice on Ice Lake and earlier platforms.
			 */
			if (sample_type & PERF_SAMPLE_WEIGHT) {
				data->weight.full = weight ?:
					intel_get_tsx_weight(meminfo->tsx_tuning);
			} else {
				data->weight.var1_dw = (u32)(weight & PEBS_LATENCY_MASK) ?:
					intel_get_tsx_weight(meminfo->tsx_tuning);
			}
			data->sample_flags |= PERF_SAMPLE_WEIGHT_TYPE;
		}

		if (sample_type & PERF_SAMPLE_DATA_SRC) {
			data->data_src.val = get_data_src(event, meminfo->aux);
			data->sample_flags |= PERF_SAMPLE_DATA_SRC;
		}

		if (sample_type & PERF_SAMPLE_ADDR_TYPE) {
			data->addr = meminfo->address;
			data->sample_flags |= PERF_SAMPLE_ADDR;
		}

		if (sample_type & PERF_SAMPLE_TRANSACTION) {
			data->txn = intel_get_tsx_transaction(meminfo->tsx_tuning,
							  gprs ? gprs->ax : 0);
			data->sample_flags |= PERF_SAMPLE_TRANSACTION;
		}
	}

	if (format_size & PEBS_DATACFG_XMMS) {
		struct pebs_xmm *xmm = next_record;

		next_record = xmm + 1;
		perf_regs->xmm_regs = xmm->xmm;
	}

	if (format_size & PEBS_DATACFG_LBRS) {
		struct lbr_entry *lbr = next_record;
		int num_lbr = ((format_size >> PEBS_DATACFG_LBR_SHIFT)
					& 0xff) + 1;
		next_record = next_record + num_lbr * sizeof(struct lbr_entry);

		if (has_branch_stack(event)) {
			intel_pmu_store_pebs_lbrs(lbr);
			intel_pmu_lbr_save_brstack(data, cpuc, event);
		}
	}

	WARN_ONCE(next_record != __pebs + (format_size >> 48),
			"PEBS record size %llu, expected %llu, config %llx\n",
			format_size >> 48,
			(u64)(next_record - __pebs),
			basic->format_size);
}

static inline void *
get_next_pebs_record_by_bit(void *base, void *top, int bit)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	void *at;
	u64 pebs_status;

	/*
	 * fmt0 does not have a status bitfield (does not use
	 * perf_record_nhm format)
	 */
	if (x86_pmu.intel_cap.pebs_format < 1)
		return base;

	if (base == NULL)
		return NULL;

	for (at = base; at < top; at += cpuc->pebs_record_size) {
		unsigned long status = get_pebs_status(at);

		if (test_bit(bit, (unsigned long *)&status)) {
			/* PEBS v3 has accurate status bits */
			if (x86_pmu.intel_cap.pebs_format >= 3)
				return at;

			if (status == (1 << bit))
				return at;

			/* clear non-PEBS bit and re-check */
			pebs_status = status & cpuc->pebs_enabled;
			pebs_status &= PEBS_COUNTER_MASK;
			if (pebs_status == (1 << bit))
				return at;
		}
	}
	return NULL;
}

void intel_pmu_auto_reload_read(struct perf_event *event)
{
	WARN_ON(!(event->hw.flags & PERF_X86_EVENT_AUTO_RELOAD));

	perf_pmu_disable(event->pmu);
	intel_pmu_drain_pebs_buffer();
	perf_pmu_enable(event->pmu);
}

/*
 * Special variant of intel_pmu_save_and_restart() for auto-reload.
 */
static int
intel_pmu_save_and_restart_reload(struct perf_event *event, int count)
{
	struct hw_perf_event *hwc = &event->hw;
	int shift = 64 - x86_pmu.cntval_bits;
	u64 period = hwc->sample_period;
	u64 prev_raw_count, new_raw_count;
	s64 new, old;

	WARN_ON(!period);

	/*
	 * drain_pebs() only happens when the PMU is disabled.
	 */
	WARN_ON(this_cpu_read(cpu_hw_events.enabled));

	prev_raw_count = local64_read(&hwc->prev_count);
	rdpmcl(hwc->event_base_rdpmc, new_raw_count);
	local64_set(&hwc->prev_count, new_raw_count);

	/*
	 * Since the counter increments a negative counter value and
	 * overflows on the sign switch, giving the interval:
	 *
	 *   [-period, 0]
	 *
	 * the difference between two consecutive reads is:
	 *
	 *   A) value2 - value1;
	 *      when no overflows have happened in between,
	 *
	 *   B) (0 - value1) + (value2 - (-period));
	 *      when one overflow happened in between,
	 *
	 *   C) (0 - value1) + (n - 1) * (period) + (value2 - (-period));
	 *      when @n overflows happened in between.
	 *
	 * Here A) is the obvious difference, B) is the extension to the
	 * discrete interval, where the first term is to the top of the
	 * interval and the second term is from the bottom of the next
	 * interval and C) the extension to multiple intervals, where the
	 * middle term is the whole intervals covered.
	 *
	 * An equivalent of C, by reduction, is:
	 *
	 *   value2 - value1 + n * period
	 */
	new = ((s64)(new_raw_count << shift) >> shift);
	old = ((s64)(prev_raw_count << shift) >> shift);
	local64_add(new - old + count * period, &event->count);

	local64_set(&hwc->period_left, -new);

	perf_event_update_userpage(event);

	return 0;
}

static __always_inline void
__intel_pmu_pebs_event(struct perf_event *event,
		       struct pt_regs *iregs,
		       struct perf_sample_data *data,
		       void *base, void *top,
		       int bit, int count,
		       void (*setup_sample)(struct perf_event *,
					    struct pt_regs *,
					    void *,
					    struct perf_sample_data *,
					    struct pt_regs *))
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	struct x86_perf_regs perf_regs;
	struct pt_regs *regs = &perf_regs.regs;
	void *at = get_next_pebs_record_by_bit(base, top, bit);
	static struct pt_regs dummy_iregs;

	if (hwc->flags & PERF_X86_EVENT_AUTO_RELOAD) {
		/*
		 * Now, auto-reload is only enabled in fixed period mode.
		 * The reload value is always hwc->sample_period.
		 * May need to change it, if auto-reload is enabled in
		 * freq mode later.
		 */
		intel_pmu_save_and_restart_reload(event, count);
	} else if (!intel_pmu_save_and_restart(event))
		return;

	if (!iregs)
		iregs = &dummy_iregs;

	while (count > 1) {
		setup_sample(event, iregs, at, data, regs);
		perf_event_output(event, data, regs);
		at += cpuc->pebs_record_size;
		at = get_next_pebs_record_by_bit(at, top, bit);
		count--;
	}

	setup_sample(event, iregs, at, data, regs);
	if (iregs == &dummy_iregs) {
		/*
		 * The PEBS records may be drained in the non-overflow context,
		 * e.g., large PEBS + context switch. Perf should treat the
		 * last record the same as other PEBS records, and doesn't
		 * invoke the generic overflow handler.
		 */
		perf_event_output(event, data, regs);
	} else {
		/*
		 * All but the last records are processed.
		 * The last one is left to be able to call the overflow handler.
		 */
		if (perf_event_overflow(event, data, regs))
			x86_pmu_stop(event, 0);
	}
}

static void intel_pmu_drain_pebs_core(struct pt_regs *iregs, struct perf_sample_data *data)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
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
	if (n <= 0) {
		if (event->hw.flags & PERF_X86_EVENT_AUTO_RELOAD)
			intel_pmu_save_and_restart_reload(event, 0);
		return;
	}

	__intel_pmu_pebs_event(event, iregs, data, at, top, 0, n,
			       setup_pebs_fixed_sample_data);
}

static void intel_pmu_pebs_event_update_no_drain(struct cpu_hw_events *cpuc, int size)
{
	struct perf_event *event;
	int bit;

	/*
	 * The drain_pebs() could be called twice in a short period
	 * for auto-reload event in pmu::read(). There are no
	 * overflows have happened in between.
	 * It needs to call intel_pmu_save_and_restart_reload() to
	 * update the event->count for this case.
	 */
	for_each_set_bit(bit, (unsigned long *)&cpuc->pebs_enabled, size) {
		event = cpuc->events[bit];
		if (event->hw.flags & PERF_X86_EVENT_AUTO_RELOAD)
			intel_pmu_save_and_restart_reload(event, 0);
	}
}

static void intel_pmu_drain_pebs_nhm(struct pt_regs *iregs, struct perf_sample_data *data)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct debug_store *ds = cpuc->ds;
	struct perf_event *event;
	void *base, *at, *top;
	short counts[INTEL_PMC_IDX_FIXED + MAX_FIXED_PEBS_EVENTS] = {};
	short error[INTEL_PMC_IDX_FIXED + MAX_FIXED_PEBS_EVENTS] = {};
	int bit, i, size;
	u64 mask;

	if (!x86_pmu.pebs_active)
		return;

	base = (struct pebs_record_nhm *)(unsigned long)ds->pebs_buffer_base;
	top = (struct pebs_record_nhm *)(unsigned long)ds->pebs_index;

	ds->pebs_index = ds->pebs_buffer_base;

	mask = (1ULL << x86_pmu.max_pebs_events) - 1;
	size = x86_pmu.max_pebs_events;
	if (x86_pmu.flags & PMU_FL_PEBS_ALL) {
		mask |= ((1ULL << x86_pmu.num_counters_fixed) - 1) << INTEL_PMC_IDX_FIXED;
		size = INTEL_PMC_IDX_FIXED + x86_pmu.num_counters_fixed;
	}

	if (unlikely(base >= top)) {
		intel_pmu_pebs_event_update_no_drain(cpuc, size);
		return;
	}

	for (at = base; at < top; at += x86_pmu.pebs_record_size) {
		struct pebs_record_nhm *p = at;
		u64 pebs_status;

		pebs_status = p->status & cpuc->pebs_enabled;
		pebs_status &= mask;

		/* PEBS v3 has more accurate status bits */
		if (x86_pmu.intel_cap.pebs_format >= 3) {
			for_each_set_bit(bit, (unsigned long *)&pebs_status, size)
				counts[bit]++;

			continue;
		}

		/*
		 * On some CPUs the PEBS status can be zero when PEBS is
		 * racing with clearing of GLOBAL_STATUS.
		 *
		 * Normally we would drop that record, but in the
		 * case when there is only a single active PEBS event
		 * we can assume it's for that event.
		 */
		if (!pebs_status && cpuc->pebs_enabled &&
			!(cpuc->pebs_enabled & (cpuc->pebs_enabled-1)))
			pebs_status = p->status = cpuc->pebs_enabled;

		bit = find_first_bit((unsigned long *)&pebs_status,
					x86_pmu.max_pebs_events);
		if (bit >= x86_pmu.max_pebs_events)
			continue;

		/*
		 * The PEBS hardware does not deal well with the situation
		 * when events happen near to each other and multiple bits
		 * are set. But it should happen rarely.
		 *
		 * If these events include one PEBS and multiple non-PEBS
		 * events, it doesn't impact PEBS record. The record will
		 * be handled normally. (slow path)
		 *
		 * If these events include two or more PEBS events, the
		 * records for the events can be collapsed into a single
		 * one, and it's not possible to reconstruct all events
		 * that caused the PEBS record. It's called collision.
		 * If collision happened, the record will be dropped.
		 */
		if (pebs_status != (1ULL << bit)) {
			for_each_set_bit(i, (unsigned long *)&pebs_status, size)
				error[i]++;
			continue;
		}

		counts[bit]++;
	}

	for_each_set_bit(bit, (unsigned long *)&mask, size) {
		if ((counts[bit] == 0) && (error[bit] == 0))
			continue;

		event = cpuc->events[bit];
		if (WARN_ON_ONCE(!event))
			continue;

		if (WARN_ON_ONCE(!event->attr.precise_ip))
			continue;

		/* log dropped samples number */
		if (error[bit]) {
			perf_log_lost_samples(event, error[bit]);

			if (iregs && perf_event_account_interrupt(event))
				x86_pmu_stop(event, 0);
		}

		if (counts[bit]) {
			__intel_pmu_pebs_event(event, iregs, data, base,
					       top, bit, counts[bit],
					       setup_pebs_fixed_sample_data);
		}
	}
}

static void intel_pmu_drain_pebs_icl(struct pt_regs *iregs, struct perf_sample_data *data)
{
	short counts[INTEL_PMC_IDX_FIXED + MAX_FIXED_PEBS_EVENTS] = {};
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int max_pebs_events = hybrid(cpuc->pmu, max_pebs_events);
	int num_counters_fixed = hybrid(cpuc->pmu, num_counters_fixed);
	struct debug_store *ds = cpuc->ds;
	struct perf_event *event;
	void *base, *at, *top;
	int bit, size;
	u64 mask;

	if (!x86_pmu.pebs_active)
		return;

	base = (struct pebs_basic *)(unsigned long)ds->pebs_buffer_base;
	top = (struct pebs_basic *)(unsigned long)ds->pebs_index;

	ds->pebs_index = ds->pebs_buffer_base;

	mask = ((1ULL << max_pebs_events) - 1) |
	       (((1ULL << num_counters_fixed) - 1) << INTEL_PMC_IDX_FIXED);
	size = INTEL_PMC_IDX_FIXED + num_counters_fixed;

	if (unlikely(base >= top)) {
		intel_pmu_pebs_event_update_no_drain(cpuc, size);
		return;
	}

	for (at = base; at < top; at += cpuc->pebs_record_size) {
		u64 pebs_status;

		pebs_status = get_pebs_status(at) & cpuc->pebs_enabled;
		pebs_status &= mask;

		for_each_set_bit(bit, (unsigned long *)&pebs_status, size)
			counts[bit]++;
	}

	for_each_set_bit(bit, (unsigned long *)&mask, size) {
		if (counts[bit] == 0)
			continue;

		event = cpuc->events[bit];
		if (WARN_ON_ONCE(!event))
			continue;

		if (WARN_ON_ONCE(!event->attr.precise_ip))
			continue;

		__intel_pmu_pebs_event(event, iregs, data, base,
				       top, bit, counts[bit],
				       setup_pebs_adaptive_sample_data);
	}
}

/*
 * BTS, PEBS probe and setup
 */

void __init intel_ds_init(void)
{
	/*
	 * No support for 32bit formats
	 */
	if (!boot_cpu_has(X86_FEATURE_DTES64))
		return;

	x86_pmu.bts  = boot_cpu_has(X86_FEATURE_BTS);
	x86_pmu.pebs = boot_cpu_has(X86_FEATURE_PEBS);
	x86_pmu.pebs_buffer_size = PEBS_BUFFER_SIZE;
	if (x86_pmu.version <= 4)
		x86_pmu.pebs_no_isolation = 1;

	if (x86_pmu.pebs) {
		char pebs_type = x86_pmu.intel_cap.pebs_trap ?  '+' : '-';
		char *pebs_qual = "";
		int format = x86_pmu.intel_cap.pebs_format;

		if (format < 4)
			x86_pmu.intel_cap.pebs_baseline = 0;

		switch (format) {
		case 0:
			pr_cont("PEBS fmt0%c, ", pebs_type);
			x86_pmu.pebs_record_size = sizeof(struct pebs_record_core);
			/*
			 * Using >PAGE_SIZE buffers makes the WRMSR to
			 * PERF_GLOBAL_CTRL in intel_pmu_enable_all()
			 * mysteriously hang on Core2.
			 *
			 * As a workaround, we don't do this.
			 */
			x86_pmu.pebs_buffer_size = PAGE_SIZE;
			x86_pmu.drain_pebs = intel_pmu_drain_pebs_core;
			break;

		case 1:
			pr_cont("PEBS fmt1%c, ", pebs_type);
			x86_pmu.pebs_record_size = sizeof(struct pebs_record_nhm);
			x86_pmu.drain_pebs = intel_pmu_drain_pebs_nhm;
			break;

		case 2:
			pr_cont("PEBS fmt2%c, ", pebs_type);
			x86_pmu.pebs_record_size = sizeof(struct pebs_record_hsw);
			x86_pmu.drain_pebs = intel_pmu_drain_pebs_nhm;
			break;

		case 3:
			pr_cont("PEBS fmt3%c, ", pebs_type);
			x86_pmu.pebs_record_size =
						sizeof(struct pebs_record_skl);
			x86_pmu.drain_pebs = intel_pmu_drain_pebs_nhm;
			x86_pmu.large_pebs_flags |= PERF_SAMPLE_TIME;
			break;

		case 5:
			x86_pmu.pebs_ept = 1;
			fallthrough;
		case 4:
			x86_pmu.drain_pebs = intel_pmu_drain_pebs_icl;
			x86_pmu.pebs_record_size = sizeof(struct pebs_basic);
			if (x86_pmu.intel_cap.pebs_baseline) {
				x86_pmu.large_pebs_flags |=
					PERF_SAMPLE_BRANCH_STACK |
					PERF_SAMPLE_TIME;
				x86_pmu.flags |= PMU_FL_PEBS_ALL;
				x86_pmu.pebs_capable = ~0ULL;
				pebs_qual = "-baseline";
				x86_get_pmu(smp_processor_id())->capabilities |= PERF_PMU_CAP_EXTENDED_REGS;
			} else {
				/* Only basic record supported */
				x86_pmu.large_pebs_flags &=
					~(PERF_SAMPLE_ADDR |
					  PERF_SAMPLE_TIME |
					  PERF_SAMPLE_DATA_SRC |
					  PERF_SAMPLE_TRANSACTION |
					  PERF_SAMPLE_REGS_USER |
					  PERF_SAMPLE_REGS_INTR);
			}
			pr_cont("PEBS fmt4%c%s, ", pebs_type, pebs_qual);

			if (!is_hybrid() && x86_pmu.intel_cap.pebs_output_pt_available) {
				pr_cont("PEBS-via-PT, ");
				x86_get_pmu(smp_processor_id())->capabilities |= PERF_PMU_CAP_AUX_OUTPUT;
			}

			break;

		default:
			pr_cont("no PEBS fmt%d%c, ", format, pebs_type);
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
