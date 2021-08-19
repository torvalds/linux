// SPDX-License-Identifier: GPL-2.0-only
#include <linux/perf_event.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <asm/apicdef.h>
#include <asm/nmi.h>

#include "../perf_event.h"

static DEFINE_PER_CPU(unsigned long, perf_nmi_tstamp);
static unsigned long perf_nmi_window;

/* AMD Event 0xFFF: Merge.  Used with Large Increment per Cycle events */
#define AMD_MERGE_EVENT ((0xFULL << 32) | 0xFFULL)
#define AMD_MERGE_EVENT_ENABLE (AMD_MERGE_EVENT | ARCH_PERFMON_EVENTSEL_ENABLE)

static __initconst const u64 amd_hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX] =
{
 [ C(L1D) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0040, /* Data Cache Accesses        */
		[ C(RESULT_MISS)   ] = 0x0141, /* Data Cache Misses          */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0,
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
		[ C(RESULT_MISS)   ] = 0x0746, /* L1_DTLB_AND_L2_DLTB_MISS.ALL */
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
		[ C(RESULT_MISS)   ] = 0x0385, /* L1_ITLB_AND_L2_ITLB_MISS.ALL */
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
 [ C(NODE) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0xb8e9, /* CPU Request to Memory, l+r */
		[ C(RESULT_MISS)   ] = 0x98e9, /* CPU Request to Memory, r   */
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

static __initconst const u64 amd_hw_cache_event_ids_f17h
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
[C(L1D)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = 0x0040, /* Data Cache Accesses */
		[C(RESULT_MISS)]   = 0xc860, /* L2$ access from DC Miss */
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = 0,
		[C(RESULT_MISS)]   = 0,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = 0xff5a, /* h/w prefetch DC Fills */
		[C(RESULT_MISS)]   = 0,
	},
},
[C(L1I)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = 0x0080, /* Instruction cache fetches  */
		[C(RESULT_MISS)]   = 0x0081, /* Instruction cache misses   */
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = -1,
		[C(RESULT_MISS)]   = -1,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = 0,
		[C(RESULT_MISS)]   = 0,
	},
},
[C(LL)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = 0,
		[C(RESULT_MISS)]   = 0,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = 0,
		[C(RESULT_MISS)]   = 0,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = 0,
		[C(RESULT_MISS)]   = 0,
	},
},
[C(DTLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = 0xff45, /* All L2 DTLB accesses */
		[C(RESULT_MISS)]   = 0xf045, /* L2 DTLB misses (PT walks) */
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = 0,
		[C(RESULT_MISS)]   = 0,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = 0,
		[C(RESULT_MISS)]   = 0,
	},
},
[C(ITLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = 0x0084, /* L1 ITLB misses, L2 ITLB hits */
		[C(RESULT_MISS)]   = 0xff85, /* L1 ITLB misses, L2 misses */
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = -1,
		[C(RESULT_MISS)]   = -1,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = -1,
		[C(RESULT_MISS)]   = -1,
	},
},
[C(BPU)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = 0x00c2, /* Retired Branch Instr.      */
		[C(RESULT_MISS)]   = 0x00c3, /* Retired Mispredicted BI    */
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = -1,
		[C(RESULT_MISS)]   = -1,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = -1,
		[C(RESULT_MISS)]   = -1,
	},
},
[C(NODE)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = 0,
		[C(RESULT_MISS)]   = 0,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = -1,
		[C(RESULT_MISS)]   = -1,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = -1,
		[C(RESULT_MISS)]   = -1,
	},
},
};

/*
 * AMD Performance Monitor K7 and later, up to and including Family 16h:
 */
static const u64 amd_perfmon_event_map[PERF_COUNT_HW_MAX] =
{
	[PERF_COUNT_HW_CPU_CYCLES]		= 0x0076,
	[PERF_COUNT_HW_INSTRUCTIONS]		= 0x00c0,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= 0x077d,
	[PERF_COUNT_HW_CACHE_MISSES]		= 0x077e,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x00c2,
	[PERF_COUNT_HW_BRANCH_MISSES]		= 0x00c3,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= 0x00d0, /* "Decoder empty" event */
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= 0x00d1, /* "Dispatch stalls" event */
};

/*
 * AMD Performance Monitor Family 17h and later:
 */
static const u64 amd_f17h_perfmon_event_map[PERF_COUNT_HW_MAX] =
{
	[PERF_COUNT_HW_CPU_CYCLES]		= 0x0076,
	[PERF_COUNT_HW_INSTRUCTIONS]		= 0x00c0,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= 0xff60,
	[PERF_COUNT_HW_CACHE_MISSES]		= 0x0964,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x00c2,
	[PERF_COUNT_HW_BRANCH_MISSES]		= 0x00c3,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= 0x0287,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= 0x0187,
};

static u64 amd_pmu_event_map(int hw_event)
{
	if (boot_cpu_data.x86 >= 0x17)
		return amd_f17h_perfmon_event_map[hw_event];

	return amd_perfmon_event_map[hw_event];
}

/*
 * Previously calculated offsets
 */
static unsigned int event_offsets[X86_PMC_IDX_MAX] __read_mostly;
static unsigned int count_offsets[X86_PMC_IDX_MAX] __read_mostly;

/*
 * Legacy CPUs:
 *   4 counters starting at 0xc0010000 each offset by 1
 *
 * CPUs with core performance counter extensions:
 *   6 counters starting at 0xc0010200 each offset by 2
 */
static inline int amd_pmu_addr_offset(int index, bool eventsel)
{
	int offset;

	if (!index)
		return index;

	if (eventsel)
		offset = event_offsets[index];
	else
		offset = count_offsets[index];

	if (offset)
		return offset;

	if (!boot_cpu_has(X86_FEATURE_PERFCTR_CORE))
		offset = index;
	else
		offset = index << 1;

	if (eventsel)
		event_offsets[index] = offset;
	else
		count_offsets[index] = offset;

	return offset;
}

/*
 * AMD64 events are detected based on their event codes.
 */
static inline unsigned int amd_get_event_code(struct hw_perf_event *hwc)
{
	return ((hwc->config >> 24) & 0x0f00) | (hwc->config & 0x00ff);
}

static inline bool amd_is_pair_event_code(struct hw_perf_event *hwc)
{
	if (!(x86_pmu.flags & PMU_FL_PAIR))
		return false;

	switch (amd_get_event_code(hwc)) {
	case 0x003:	return true;	/* Retired SSE/AVX FLOPs */
	default:	return false;
	}
}

static int amd_core_hw_config(struct perf_event *event)
{
	if (event->attr.exclude_host && event->attr.exclude_guest)
		/*
		 * When HO == GO == 1 the hardware treats that as GO == HO == 0
		 * and will count in both modes. We don't want to count in that
		 * case so we emulate no-counting by setting US = OS = 0.
		 */
		event->hw.config &= ~(ARCH_PERFMON_EVENTSEL_USR |
				      ARCH_PERFMON_EVENTSEL_OS);
	else if (event->attr.exclude_host)
		event->hw.config |= AMD64_EVENTSEL_GUESTONLY;
	else if (event->attr.exclude_guest)
		event->hw.config |= AMD64_EVENTSEL_HOSTONLY;

	if ((x86_pmu.flags & PMU_FL_PAIR) && amd_is_pair_event_code(&event->hw))
		event->hw.flags |= PERF_X86_EVENT_PAIR;

	return 0;
}

static inline int amd_is_nb_event(struct hw_perf_event *hwc)
{
	return (hwc->config & 0xe0) == 0xe0;
}

static inline int amd_has_nb(struct cpu_hw_events *cpuc)
{
	struct amd_nb *nb = cpuc->amd_nb;

	return nb && nb->nb_id != -1;
}

static int amd_pmu_hw_config(struct perf_event *event)
{
	int ret;

	/* pass precise event sampling to ibs: */
	if (event->attr.precise_ip && get_ibs_caps())
		return -ENOENT;

	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	ret = x86_pmu_hw_config(event);
	if (ret)
		return ret;

	if (event->attr.type == PERF_TYPE_RAW)
		event->hw.config |= event->attr.config & AMD64_RAW_EVENT_MASK;

	return amd_core_hw_config(event);
}

static void __amd_put_nb_event_constraints(struct cpu_hw_events *cpuc,
					   struct perf_event *event)
{
	struct amd_nb *nb = cpuc->amd_nb;
	int i;

	/*
	 * need to scan whole list because event may not have
	 * been assigned during scheduling
	 *
	 * no race condition possible because event can only
	 * be removed on one CPU at a time AND PMU is disabled
	 * when we come here
	 */
	for (i = 0; i < x86_pmu.num_counters; i++) {
		if (cmpxchg(nb->owners + i, event, NULL) == event)
			break;
	}
}

 /*
  * AMD64 NorthBridge events need special treatment because
  * counter access needs to be synchronized across all cores
  * of a package. Refer to BKDG section 3.12
  *
  * NB events are events measuring L3 cache, Hypertransport
  * traffic. They are identified by an event code >= 0xe00.
  * They measure events on the NorthBride which is shared
  * by all cores on a package. NB events are counted on a
  * shared set of counters. When a NB event is programmed
  * in a counter, the data actually comes from a shared
  * counter. Thus, access to those counters needs to be
  * synchronized.
  *
  * We implement the synchronization such that no two cores
  * can be measuring NB events using the same counters. Thus,
  * we maintain a per-NB allocation table. The available slot
  * is propagated using the event_constraint structure.
  *
  * We provide only one choice for each NB event based on
  * the fact that only NB events have restrictions. Consequently,
  * if a counter is available, there is a guarantee the NB event
  * will be assigned to it. If no slot is available, an empty
  * constraint is returned and scheduling will eventually fail
  * for this event.
  *
  * Note that all cores attached the same NB compete for the same
  * counters to host NB events, this is why we use atomic ops. Some
  * multi-chip CPUs may have more than one NB.
  *
  * Given that resources are allocated (cmpxchg), they must be
  * eventually freed for others to use. This is accomplished by
  * calling __amd_put_nb_event_constraints()
  *
  * Non NB events are not impacted by this restriction.
  */
static struct event_constraint *
__amd_get_nb_event_constraints(struct cpu_hw_events *cpuc, struct perf_event *event,
			       struct event_constraint *c)
{
	struct hw_perf_event *hwc = &event->hw;
	struct amd_nb *nb = cpuc->amd_nb;
	struct perf_event *old;
	int idx, new = -1;

	if (!c)
		c = &unconstrained;

	if (cpuc->is_fake)
		return c;

	/*
	 * detect if already present, if so reuse
	 *
	 * cannot merge with actual allocation
	 * because of possible holes
	 *
	 * event can already be present yet not assigned (in hwc->idx)
	 * because of successive calls to x86_schedule_events() from
	 * hw_perf_group_sched_in() without hw_perf_enable()
	 */
	for_each_set_bit(idx, c->idxmsk, x86_pmu.num_counters) {
		if (new == -1 || hwc->idx == idx)
			/* assign free slot, prefer hwc->idx */
			old = cmpxchg(nb->owners + idx, NULL, event);
		else if (nb->owners[idx] == event)
			/* event already present */
			old = event;
		else
			continue;

		if (old && old != event)
			continue;

		/* reassign to this slot */
		if (new != -1)
			cmpxchg(nb->owners + new, event, NULL);
		new = idx;

		/* already present, reuse */
		if (old == event)
			break;
	}

	if (new == -1)
		return &emptyconstraint;

	return &nb->event_constraints[new];
}

static struct amd_nb *amd_alloc_nb(int cpu)
{
	struct amd_nb *nb;
	int i;

	nb = kzalloc_node(sizeof(struct amd_nb), GFP_KERNEL, cpu_to_node(cpu));
	if (!nb)
		return NULL;

	nb->nb_id = -1;

	/*
	 * initialize all possible NB constraints
	 */
	for (i = 0; i < x86_pmu.num_counters; i++) {
		__set_bit(i, nb->event_constraints[i].idxmsk);
		nb->event_constraints[i].weight = 1;
	}
	return nb;
}

static int amd_pmu_cpu_prepare(int cpu)
{
	struct cpu_hw_events *cpuc = &per_cpu(cpu_hw_events, cpu);

	WARN_ON_ONCE(cpuc->amd_nb);

	if (!x86_pmu.amd_nb_constraints)
		return 0;

	cpuc->amd_nb = amd_alloc_nb(cpu);
	if (!cpuc->amd_nb)
		return -ENOMEM;

	return 0;
}

static void amd_pmu_cpu_starting(int cpu)
{
	struct cpu_hw_events *cpuc = &per_cpu(cpu_hw_events, cpu);
	void **onln = &cpuc->kfree_on_online[X86_PERF_KFREE_SHARED];
	struct amd_nb *nb;
	int i, nb_id;

	cpuc->perf_ctr_virt_mask = AMD64_EVENTSEL_HOSTONLY;

	if (!x86_pmu.amd_nb_constraints)
		return;

	nb_id = topology_die_id(cpu);
	WARN_ON_ONCE(nb_id == BAD_APICID);

	for_each_online_cpu(i) {
		nb = per_cpu(cpu_hw_events, i).amd_nb;
		if (WARN_ON_ONCE(!nb))
			continue;

		if (nb->nb_id == nb_id) {
			*onln = cpuc->amd_nb;
			cpuc->amd_nb = nb;
			break;
		}
	}

	cpuc->amd_nb->nb_id = nb_id;
	cpuc->amd_nb->refcnt++;
}

static void amd_pmu_cpu_dead(int cpu)
{
	struct cpu_hw_events *cpuhw;

	if (!x86_pmu.amd_nb_constraints)
		return;

	cpuhw = &per_cpu(cpu_hw_events, cpu);

	if (cpuhw->amd_nb) {
		struct amd_nb *nb = cpuhw->amd_nb;

		if (nb->nb_id == -1 || --nb->refcnt == 0)
			kfree(nb);

		cpuhw->amd_nb = NULL;
	}
}

/*
 * When a PMC counter overflows, an NMI is used to process the event and
 * reset the counter. NMI latency can result in the counter being updated
 * before the NMI can run, which can result in what appear to be spurious
 * NMIs. This function is intended to wait for the NMI to run and reset
 * the counter to avoid possible unhandled NMI messages.
 */
#define OVERFLOW_WAIT_COUNT	50

static void amd_pmu_wait_on_overflow(int idx)
{
	unsigned int i;
	u64 counter;

	/*
	 * Wait for the counter to be reset if it has overflowed. This loop
	 * should exit very, very quickly, but just in case, don't wait
	 * forever...
	 */
	for (i = 0; i < OVERFLOW_WAIT_COUNT; i++) {
		rdmsrl(x86_pmu_event_addr(idx), counter);
		if (counter & (1ULL << (x86_pmu.cntval_bits - 1)))
			break;

		/* Might be in IRQ context, so can't sleep */
		udelay(1);
	}
}

static void amd_pmu_disable_all(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int idx;

	x86_pmu_disable_all();

	/*
	 * This shouldn't be called from NMI context, but add a safeguard here
	 * to return, since if we're in NMI context we can't wait for an NMI
	 * to reset an overflowed counter value.
	 */
	if (in_nmi())
		return;

	/*
	 * Check each counter for overflow and wait for it to be reset by the
	 * NMI if it has overflowed. This relies on the fact that all active
	 * counters are always enabled when this function is called and
	 * ARCH_PERFMON_EVENTSEL_INT is always set.
	 */
	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		if (!test_bit(idx, cpuc->active_mask))
			continue;

		amd_pmu_wait_on_overflow(idx);
	}
}

static void amd_pmu_disable_event(struct perf_event *event)
{
	x86_pmu_disable_event(event);

	/*
	 * This can be called from NMI context (via x86_pmu_stop). The counter
	 * may have overflowed, but either way, we'll never see it get reset
	 * by the NMI if we're already in the NMI. And the NMI latency support
	 * below will take care of any pending NMI that might have been
	 * generated by the overflow.
	 */
	if (in_nmi())
		return;

	amd_pmu_wait_on_overflow(event->hw.idx);
}

/*
 * Because of NMI latency, if multiple PMC counters are active or other sources
 * of NMIs are received, the perf NMI handler can handle one or more overflowed
 * PMC counters outside of the NMI associated with the PMC overflow. If the NMI
 * doesn't arrive at the LAPIC in time to become a pending NMI, then the kernel
 * back-to-back NMI support won't be active. This PMC handler needs to take into
 * account that this can occur, otherwise this could result in unknown NMI
 * messages being issued. Examples of this is PMC overflow while in the NMI
 * handler when multiple PMCs are active or PMC overflow while handling some
 * other source of an NMI.
 *
 * Attempt to mitigate this by creating an NMI window in which un-handled NMIs
 * received during this window will be claimed. This prevents extending the
 * window past when it is possible that latent NMIs should be received. The
 * per-CPU perf_nmi_tstamp will be set to the window end time whenever perf has
 * handled a counter. When an un-handled NMI is received, it will be claimed
 * only if arriving within that window.
 */
static int amd_pmu_handle_irq(struct pt_regs *regs)
{
	int handled;

	/* Process any counter overflows */
	handled = x86_pmu_handle_irq(regs);

	/*
	 * If a counter was handled, record a timestamp such that un-handled
	 * NMIs will be claimed if arriving within that window.
	 */
	if (handled) {
		this_cpu_write(perf_nmi_tstamp, jiffies + perf_nmi_window);

		return handled;
	}

	if (time_after(jiffies, this_cpu_read(perf_nmi_tstamp)))
		return NMI_DONE;

	return NMI_HANDLED;
}

static struct event_constraint *
amd_get_event_constraints(struct cpu_hw_events *cpuc, int idx,
			  struct perf_event *event)
{
	/*
	 * if not NB event or no NB, then no constraints
	 */
	if (!(amd_has_nb(cpuc) && amd_is_nb_event(&event->hw)))
		return &unconstrained;

	return __amd_get_nb_event_constraints(cpuc, event, NULL);
}

static void amd_put_event_constraints(struct cpu_hw_events *cpuc,
				      struct perf_event *event)
{
	if (amd_has_nb(cpuc) && amd_is_nb_event(&event->hw))
		__amd_put_nb_event_constraints(cpuc, event);
}

PMU_FORMAT_ATTR(event,	"config:0-7,32-35");
PMU_FORMAT_ATTR(umask,	"config:8-15"	);
PMU_FORMAT_ATTR(edge,	"config:18"	);
PMU_FORMAT_ATTR(inv,	"config:23"	);
PMU_FORMAT_ATTR(cmask,	"config:24-31"	);

static struct attribute *amd_format_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_cmask.attr,
	NULL,
};

/* AMD Family 15h */

#define AMD_EVENT_TYPE_MASK	0x000000F0ULL

#define AMD_EVENT_FP		0x00000000ULL ... 0x00000010ULL
#define AMD_EVENT_LS		0x00000020ULL ... 0x00000030ULL
#define AMD_EVENT_DC		0x00000040ULL ... 0x00000050ULL
#define AMD_EVENT_CU		0x00000060ULL ... 0x00000070ULL
#define AMD_EVENT_IC_DE		0x00000080ULL ... 0x00000090ULL
#define AMD_EVENT_EX_LS		0x000000C0ULL
#define AMD_EVENT_DE		0x000000D0ULL
#define AMD_EVENT_NB		0x000000E0ULL ... 0x000000F0ULL

/*
 * AMD family 15h event code/PMC mappings:
 *
 * type = event_code & 0x0F0:
 *
 * 0x000	FP	PERF_CTL[5:3]
 * 0x010	FP	PERF_CTL[5:3]
 * 0x020	LS	PERF_CTL[5:0]
 * 0x030	LS	PERF_CTL[5:0]
 * 0x040	DC	PERF_CTL[5:0]
 * 0x050	DC	PERF_CTL[5:0]
 * 0x060	CU	PERF_CTL[2:0]
 * 0x070	CU	PERF_CTL[2:0]
 * 0x080	IC/DE	PERF_CTL[2:0]
 * 0x090	IC/DE	PERF_CTL[2:0]
 * 0x0A0	---
 * 0x0B0	---
 * 0x0C0	EX/LS	PERF_CTL[5:0]
 * 0x0D0	DE	PERF_CTL[2:0]
 * 0x0E0	NB	NB_PERF_CTL[3:0]
 * 0x0F0	NB	NB_PERF_CTL[3:0]
 *
 * Exceptions:
 *
 * 0x000	FP	PERF_CTL[3], PERF_CTL[5:3] (*)
 * 0x003	FP	PERF_CTL[3]
 * 0x004	FP	PERF_CTL[3], PERF_CTL[5:3] (*)
 * 0x00B	FP	PERF_CTL[3]
 * 0x00D	FP	PERF_CTL[3]
 * 0x023	DE	PERF_CTL[2:0]
 * 0x02D	LS	PERF_CTL[3]
 * 0x02E	LS	PERF_CTL[3,0]
 * 0x031	LS	PERF_CTL[2:0] (**)
 * 0x043	CU	PERF_CTL[2:0]
 * 0x045	CU	PERF_CTL[2:0]
 * 0x046	CU	PERF_CTL[2:0]
 * 0x054	CU	PERF_CTL[2:0]
 * 0x055	CU	PERF_CTL[2:0]
 * 0x08F	IC	PERF_CTL[0]
 * 0x187	DE	PERF_CTL[0]
 * 0x188	DE	PERF_CTL[0]
 * 0x0DB	EX	PERF_CTL[5:0]
 * 0x0DC	LS	PERF_CTL[5:0]
 * 0x0DD	LS	PERF_CTL[5:0]
 * 0x0DE	LS	PERF_CTL[5:0]
 * 0x0DF	LS	PERF_CTL[5:0]
 * 0x1C0	EX	PERF_CTL[5:3]
 * 0x1D6	EX	PERF_CTL[5:0]
 * 0x1D8	EX	PERF_CTL[5:0]
 *
 * (*)  depending on the umask all FPU counters may be used
 * (**) only one unitmask enabled at a time
 */

static struct event_constraint amd_f15_PMC0  = EVENT_CONSTRAINT(0, 0x01, 0);
static struct event_constraint amd_f15_PMC20 = EVENT_CONSTRAINT(0, 0x07, 0);
static struct event_constraint amd_f15_PMC3  = EVENT_CONSTRAINT(0, 0x08, 0);
static struct event_constraint amd_f15_PMC30 = EVENT_CONSTRAINT_OVERLAP(0, 0x09, 0);
static struct event_constraint amd_f15_PMC50 = EVENT_CONSTRAINT(0, 0x3F, 0);
static struct event_constraint amd_f15_PMC53 = EVENT_CONSTRAINT(0, 0x38, 0);

static struct event_constraint *
amd_get_event_constraints_f15h(struct cpu_hw_events *cpuc, int idx,
			       struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	unsigned int event_code = amd_get_event_code(hwc);

	switch (event_code & AMD_EVENT_TYPE_MASK) {
	case AMD_EVENT_FP:
		switch (event_code) {
		case 0x000:
			if (!(hwc->config & 0x0000F000ULL))
				break;
			if (!(hwc->config & 0x00000F00ULL))
				break;
			return &amd_f15_PMC3;
		case 0x004:
			if (hweight_long(hwc->config & ARCH_PERFMON_EVENTSEL_UMASK) <= 1)
				break;
			return &amd_f15_PMC3;
		case 0x003:
		case 0x00B:
		case 0x00D:
			return &amd_f15_PMC3;
		}
		return &amd_f15_PMC53;
	case AMD_EVENT_LS:
	case AMD_EVENT_DC:
	case AMD_EVENT_EX_LS:
		switch (event_code) {
		case 0x023:
		case 0x043:
		case 0x045:
		case 0x046:
		case 0x054:
		case 0x055:
			return &amd_f15_PMC20;
		case 0x02D:
			return &amd_f15_PMC3;
		case 0x02E:
			return &amd_f15_PMC30;
		case 0x031:
			if (hweight_long(hwc->config & ARCH_PERFMON_EVENTSEL_UMASK) <= 1)
				return &amd_f15_PMC20;
			return &emptyconstraint;
		case 0x1C0:
			return &amd_f15_PMC53;
		default:
			return &amd_f15_PMC50;
		}
	case AMD_EVENT_CU:
	case AMD_EVENT_IC_DE:
	case AMD_EVENT_DE:
		switch (event_code) {
		case 0x08F:
		case 0x187:
		case 0x188:
			return &amd_f15_PMC0;
		case 0x0DB ... 0x0DF:
		case 0x1D6:
		case 0x1D8:
			return &amd_f15_PMC50;
		default:
			return &amd_f15_PMC20;
		}
	case AMD_EVENT_NB:
		/* moved to uncore.c */
		return &emptyconstraint;
	default:
		return &emptyconstraint;
	}
}

static struct event_constraint pair_constraint;

static struct event_constraint *
amd_get_event_constraints_f17h(struct cpu_hw_events *cpuc, int idx,
			       struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (amd_is_pair_event_code(hwc))
		return &pair_constraint;

	return &unconstrained;
}

static void amd_put_event_constraints_f17h(struct cpu_hw_events *cpuc,
					   struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (is_counter_pair(hwc))
		--cpuc->n_pair;
}

static ssize_t amd_event_sysfs_show(char *page, u64 config)
{
	u64 event = (config & ARCH_PERFMON_EVENTSEL_EVENT) |
		    (config & AMD64_EVENTSEL_EVENT) >> 24;

	return x86_event_sysfs_show(page, config, event);
}

static __initconst const struct x86_pmu amd_pmu = {
	.name			= "AMD",
	.handle_irq		= amd_pmu_handle_irq,
	.disable_all		= amd_pmu_disable_all,
	.enable_all		= x86_pmu_enable_all,
	.enable			= x86_pmu_enable_event,
	.disable		= amd_pmu_disable_event,
	.hw_config		= amd_pmu_hw_config,
	.schedule_events	= x86_schedule_events,
	.eventsel		= MSR_K7_EVNTSEL0,
	.perfctr		= MSR_K7_PERFCTR0,
	.addr_offset            = amd_pmu_addr_offset,
	.event_map		= amd_pmu_event_map,
	.max_events		= ARRAY_SIZE(amd_perfmon_event_map),
	.num_counters		= AMD64_NUM_COUNTERS,
	.cntval_bits		= 48,
	.cntval_mask		= (1ULL << 48) - 1,
	.apic			= 1,
	/* use highest bit to detect overflow */
	.max_period		= (1ULL << 47) - 1,
	.get_event_constraints	= amd_get_event_constraints,
	.put_event_constraints	= amd_put_event_constraints,

	.format_attrs		= amd_format_attr,
	.events_sysfs_show	= amd_event_sysfs_show,

	.cpu_prepare		= amd_pmu_cpu_prepare,
	.cpu_starting		= amd_pmu_cpu_starting,
	.cpu_dead		= amd_pmu_cpu_dead,

	.amd_nb_constraints	= 1,
};

static int __init amd_core_pmu_init(void)
{
	u64 even_ctr_mask = 0ULL;
	int i;

	if (!boot_cpu_has(X86_FEATURE_PERFCTR_CORE))
		return 0;

	/* Avoid calculating the value each time in the NMI handler */
	perf_nmi_window = msecs_to_jiffies(100);

	/*
	 * If core performance counter extensions exists, we must use
	 * MSR_F15H_PERF_CTL/MSR_F15H_PERF_CTR msrs. See also
	 * amd_pmu_addr_offset().
	 */
	x86_pmu.eventsel	= MSR_F15H_PERF_CTL;
	x86_pmu.perfctr		= MSR_F15H_PERF_CTR;
	x86_pmu.num_counters	= AMD64_NUM_COUNTERS_CORE;
	/*
	 * AMD Core perfctr has separate MSRs for the NB events, see
	 * the amd/uncore.c driver.
	 */
	x86_pmu.amd_nb_constraints = 0;

	if (boot_cpu_data.x86 == 0x15) {
		pr_cont("Fam15h ");
		x86_pmu.get_event_constraints = amd_get_event_constraints_f15h;
	}
	if (boot_cpu_data.x86 >= 0x17) {
		pr_cont("Fam17h+ ");
		/*
		 * Family 17h and compatibles have constraints for Large
		 * Increment per Cycle events: they may only be assigned an
		 * even numbered counter that has a consecutive adjacent odd
		 * numbered counter following it.
		 */
		for (i = 0; i < x86_pmu.num_counters - 1; i += 2)
			even_ctr_mask |= 1 << i;

		pair_constraint = (struct event_constraint)
				    __EVENT_CONSTRAINT(0, even_ctr_mask, 0,
				    x86_pmu.num_counters / 2, 0,
				    PERF_X86_EVENT_PAIR);

		x86_pmu.get_event_constraints = amd_get_event_constraints_f17h;
		x86_pmu.put_event_constraints = amd_put_event_constraints_f17h;
		x86_pmu.perf_ctr_pair_en = AMD_MERGE_EVENT_ENABLE;
		x86_pmu.flags |= PMU_FL_PAIR;
	}

	pr_cont("core perfctr, ");
	return 0;
}

__init int amd_pmu_init(void)
{
	int ret;

	/* Performance-monitoring supported from K7 and later: */
	if (boot_cpu_data.x86 < 6)
		return -ENODEV;

	x86_pmu = amd_pmu;

	ret = amd_core_pmu_init();
	if (ret)
		return ret;

	if (num_possible_cpus() == 1) {
		/*
		 * No point in allocating data structures to serialize
		 * against other CPUs, when there is only the one CPU.
		 */
		x86_pmu.amd_nb_constraints = 0;
	}

	if (boot_cpu_data.x86 >= 0x17)
		memcpy(hw_cache_event_ids, amd_hw_cache_event_ids_f17h, sizeof(hw_cache_event_ids));
	else
		memcpy(hw_cache_event_ids, amd_hw_cache_event_ids, sizeof(hw_cache_event_ids));

	return 0;
}

void amd_pmu_enable_virt(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	cpuc->perf_ctr_virt_mask = 0;

	/* Reload all events */
	amd_pmu_disable_all();
	x86_pmu_enable_all(0);
}
EXPORT_SYMBOL_GPL(amd_pmu_enable_virt);

void amd_pmu_disable_virt(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	/*
	 * We only mask out the Host-only bit so that host-only counting works
	 * when SVM is disabled. If someone sets up a guest-only counter when
	 * SVM is disabled the Guest-only bits still gets set and the counter
	 * will not count anything.
	 */
	cpuc->perf_ctr_virt_mask = AMD64_EVENTSEL_HOSTONLY;

	/* Reload all events */
	amd_pmu_disable_all();
	x86_pmu_enable_all(0);
}
EXPORT_SYMBOL_GPL(amd_pmu_disable_virt);
