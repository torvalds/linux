#include <linux/perf_event.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/apicdef.h>

#include "perf_event.h"

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

/*
 * AMD Performance Monitor K7 and later.
 */
static const u64 amd_perfmon_event_map[] =
{
  [PERF_COUNT_HW_CPU_CYCLES]			= 0x0076,
  [PERF_COUNT_HW_INSTRUCTIONS]			= 0x00c0,
  [PERF_COUNT_HW_CACHE_REFERENCES]		= 0x0080,
  [PERF_COUNT_HW_CACHE_MISSES]			= 0x0081,
  [PERF_COUNT_HW_BRANCH_INSTRUCTIONS]		= 0x00c2,
  [PERF_COUNT_HW_BRANCH_MISSES]			= 0x00c3,
  [PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= 0x00d0, /* "Decoder empty" event */
  [PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= 0x00d1, /* "Dispatch stalls" event */
};

static u64 amd_pmu_event_map(int hw_event)
{
	return amd_perfmon_event_map[hw_event];
}

static int amd_pmu_hw_config(struct perf_event *event)
{
	int ret = x86_pmu_hw_config(event);

	if (ret)
		return ret;

	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	if (event->attr.exclude_host && event->attr.exclude_guest)
		/*
		 * When HO == GO == 1 the hardware treats that as GO == HO == 0
		 * and will count in both modes. We don't want to count in that
		 * case so we emulate no-counting by setting US = OS = 0.
		 */
		event->hw.config &= ~(ARCH_PERFMON_EVENTSEL_USR |
				      ARCH_PERFMON_EVENTSEL_OS);
	else if (event->attr.exclude_host)
		event->hw.config |= AMD_PERFMON_EVENTSEL_GUESTONLY;
	else if (event->attr.exclude_guest)
		event->hw.config |= AMD_PERFMON_EVENTSEL_HOSTONLY;

	if (event->attr.type != PERF_TYPE_RAW)
		return 0;

	event->hw.config |= event->attr.config & AMD64_RAW_EVENT_MASK;

	return 0;
}

/*
 * AMD64 events are detected based on their event codes.
 */
static inline unsigned int amd_get_event_code(struct hw_perf_event *hwc)
{
	return ((hwc->config >> 24) & 0x0f00) | (hwc->config & 0x00ff);
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

static void amd_put_event_constraints(struct cpu_hw_events *cpuc,
				      struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct amd_nb *nb = cpuc->amd_nb;
	int i;

	/*
	 * only care about NB events
	 */
	if (!(amd_has_nb(cpuc) && amd_is_nb_event(hwc)))
		return;

	/*
	 * need to scan whole list because event may not have
	 * been assigned during scheduling
	 *
	 * no race condition possible because event can only
	 * be removed on one CPU at a time AND PMU is disabled
	 * when we come here
	 */
	for (i = 0; i < x86_pmu.num_counters; i++) {
		if (nb->owners[i] == event) {
			cmpxchg(nb->owners+i, event, NULL);
			break;
		}
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
  * calling amd_put_event_constraints().
  *
  * Non NB events are not impacted by this restriction.
  */
static struct event_constraint *
amd_get_event_constraints(struct cpu_hw_events *cpuc, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct amd_nb *nb = cpuc->amd_nb;
	struct perf_event *old = NULL;
	int max = x86_pmu.num_counters;
	int i, j, k = -1;

	/*
	 * if not NB event or no NB, then no constraints
	 */
	if (!(amd_has_nb(cpuc) && amd_is_nb_event(hwc)))
		return &unconstrained;

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
	for (i = 0; i < max; i++) {
		/*
		 * keep track of first free slot
		 */
		if (k == -1 && !nb->owners[i])
			k = i;

		/* already present, reuse */
		if (nb->owners[i] == event)
			goto done;
	}
	/*
	 * not present, so grab a new slot
	 * starting either at:
	 */
	if (hwc->idx != -1) {
		/* previous assignment */
		i = hwc->idx;
	} else if (k != -1) {
		/* start from free slot found */
		i = k;
	} else {
		/*
		 * event not found, no slot found in
		 * first pass, try again from the
		 * beginning
		 */
		i = 0;
	}
	j = i;
	do {
		old = cmpxchg(nb->owners+i, NULL, event);
		if (!old)
			break;
		if (++i == max)
			i = 0;
	} while (i != j);
done:
	if (!old)
		return &nb->event_constraints[i];

	return &emptyconstraint;
}

static struct amd_nb *amd_alloc_nb(int cpu)
{
	struct amd_nb *nb;
	int i;

	nb = kmalloc_node(sizeof(struct amd_nb), GFP_KERNEL | __GFP_ZERO,
			  cpu_to_node(cpu));
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

	if (boot_cpu_data.x86_max_cores < 2)
		return NOTIFY_OK;

	cpuc->amd_nb = amd_alloc_nb(cpu);
	if (!cpuc->amd_nb)
		return NOTIFY_BAD;

	return NOTIFY_OK;
}

static void amd_pmu_cpu_starting(int cpu)
{
	struct cpu_hw_events *cpuc = &per_cpu(cpu_hw_events, cpu);
	struct amd_nb *nb;
	int i, nb_id;

	cpuc->perf_ctr_virt_mask = AMD_PERFMON_EVENTSEL_HOSTONLY;

	if (boot_cpu_data.x86_max_cores < 2 || boot_cpu_data.x86 == 0x15)
		return;

	nb_id = amd_get_nb_id(cpu);
	WARN_ON_ONCE(nb_id == BAD_APICID);

	for_each_online_cpu(i) {
		nb = per_cpu(cpu_hw_events, i).amd_nb;
		if (WARN_ON_ONCE(!nb))
			continue;

		if (nb->nb_id == nb_id) {
			cpuc->kfree_on_online = cpuc->amd_nb;
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

	if (boot_cpu_data.x86_max_cores < 2)
		return;

	cpuhw = &per_cpu(cpu_hw_events, cpu);

	if (cpuhw->amd_nb) {
		struct amd_nb *nb = cpuhw->amd_nb;

		if (nb->nb_id == -1 || --nb->refcnt == 0)
			kfree(nb);

		cpuhw->amd_nb = NULL;
	}
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

static __initconst const struct x86_pmu amd_pmu = {
	.name			= "AMD",
	.handle_irq		= x86_pmu_handle_irq,
	.disable_all		= x86_pmu_disable_all,
	.enable_all		= x86_pmu_enable_all,
	.enable			= x86_pmu_enable_event,
	.disable		= x86_pmu_disable_event,
	.hw_config		= amd_pmu_hw_config,
	.schedule_events	= x86_schedule_events,
	.eventsel		= MSR_K7_EVNTSEL0,
	.perfctr		= MSR_K7_PERFCTR0,
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

	.cpu_prepare		= amd_pmu_cpu_prepare,
	.cpu_starting		= amd_pmu_cpu_starting,
	.cpu_dead		= amd_pmu_cpu_dead,
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
amd_get_event_constraints_f15h(struct cpu_hw_events *cpuc, struct perf_event *event)
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
		/* not yet implemented */
		return &emptyconstraint;
	default:
		return &emptyconstraint;
	}
}

static __initconst const struct x86_pmu amd_pmu_f15h = {
	.name			= "AMD Family 15h",
	.handle_irq		= x86_pmu_handle_irq,
	.disable_all		= x86_pmu_disable_all,
	.enable_all		= x86_pmu_enable_all,
	.enable			= x86_pmu_enable_event,
	.disable		= x86_pmu_disable_event,
	.hw_config		= amd_pmu_hw_config,
	.schedule_events	= x86_schedule_events,
	.eventsel		= MSR_F15H_PERF_CTL,
	.perfctr		= MSR_F15H_PERF_CTR,
	.event_map		= amd_pmu_event_map,
	.max_events		= ARRAY_SIZE(amd_perfmon_event_map),
	.num_counters		= AMD64_NUM_COUNTERS_F15H,
	.cntval_bits		= 48,
	.cntval_mask		= (1ULL << 48) - 1,
	.apic			= 1,
	/* use highest bit to detect overflow */
	.max_period		= (1ULL << 47) - 1,
	.get_event_constraints	= amd_get_event_constraints_f15h,
	/* nortbridge counters not yet implemented: */
#if 0
	.put_event_constraints	= amd_put_event_constraints,

	.cpu_prepare		= amd_pmu_cpu_prepare,
	.cpu_dead		= amd_pmu_cpu_dead,
#endif
	.cpu_starting		= amd_pmu_cpu_starting,
	.format_attrs		= amd_format_attr,
};

__init int amd_pmu_init(void)
{
	/* Performance-monitoring supported from K7 and later: */
	if (boot_cpu_data.x86 < 6)
		return -ENODEV;

	/*
	 * If core performance counter extensions exists, it must be
	 * family 15h, otherwise fail. See x86_pmu_addr_offset().
	 */
	switch (boot_cpu_data.x86) {
	case 0x15:
		if (!cpu_has_perfctr_core)
			return -ENODEV;
		x86_pmu = amd_pmu_f15h;
		break;
	default:
		if (cpu_has_perfctr_core)
			return -ENODEV;
		x86_pmu = amd_pmu;
		break;
	}

	/* Events are common for all AMDs */
	memcpy(hw_cache_event_ids, amd_hw_cache_event_ids,
	       sizeof(hw_cache_event_ids));

	return 0;
}

void amd_pmu_enable_virt(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	cpuc->perf_ctr_virt_mask = 0;

	/* Reload all events */
	x86_pmu_disable_all();
	x86_pmu_enable_all(0);
}
EXPORT_SYMBOL_GPL(amd_pmu_enable_virt);

void amd_pmu_disable_virt(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	/*
	 * We only mask out the Host-only bit so that host-only counting works
	 * when SVM is disabled. If someone sets up a guest-only counter when
	 * SVM is disabled the Guest-only bits still gets set and the counter
	 * will not count anything.
	 */
	cpuc->perf_ctr_virt_mask = AMD_PERFMON_EVENTSEL_HOSTONLY;

	/* Reload all events */
	x86_pmu_disable_all();
	x86_pmu_enable_all(0);
}
EXPORT_SYMBOL_GPL(amd_pmu_disable_virt);
