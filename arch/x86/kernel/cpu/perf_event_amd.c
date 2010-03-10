#ifdef CONFIG_CPU_SUP_AMD

static DEFINE_RAW_SPINLOCK(amd_nb_lock);

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
#define K7_EVNTSEL_EVENT_MASK	0xF000000FFULL
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
 * AMD64 events are detected based on their event codes.
 */
static inline int amd_is_nb_event(struct hw_perf_event *hwc)
{
	return (hwc->config & 0xe0) == 0xe0;
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
	if (!(nb && amd_is_nb_event(hwc)))
		return;

	/*
	 * need to scan whole list because event may not have
	 * been assigned during scheduling
	 *
	 * no race condition possible because event can only
	 * be removed on one CPU at a time AND PMU is disabled
	 * when we come here
	 */
	for (i = 0; i < x86_pmu.num_events; i++) {
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
	int max = x86_pmu.num_events;
	int i, j, k = -1;

	/*
	 * if not NB event or no NB, then no constraints
	 */
	if (!(nb && amd_is_nb_event(hwc)))
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

static __initconst struct x86_pmu amd_pmu = {
	.name			= "AMD",
	.handle_irq		= x86_pmu_handle_irq,
	.disable_all		= x86_pmu_disable_all,
	.enable_all		= x86_pmu_enable_all,
	.enable			= x86_pmu_enable_event,
	.disable		= x86_pmu_disable_event,
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
	.get_event_constraints	= amd_get_event_constraints,
	.put_event_constraints	= amd_put_event_constraints
};

static struct amd_nb *amd_alloc_nb(int cpu, int nb_id)
{
	struct amd_nb *nb;
	int i;

	nb = kmalloc(sizeof(struct amd_nb), GFP_KERNEL);
	if (!nb)
		return NULL;

	memset(nb, 0, sizeof(*nb));
	nb->nb_id = nb_id;

	/*
	 * initialize all possible NB constraints
	 */
	for (i = 0; i < x86_pmu.num_events; i++) {
		set_bit(i, nb->event_constraints[i].idxmsk);
		nb->event_constraints[i].weight = 1;
	}
	return nb;
}

static void amd_pmu_cpu_online(int cpu)
{
	struct cpu_hw_events *cpu1, *cpu2;
	struct amd_nb *nb = NULL;
	int i, nb_id;

	if (boot_cpu_data.x86_max_cores < 2)
		return;

	/*
	 * function may be called too early in the
	 * boot process, in which case nb_id is bogus
	 */
	nb_id = amd_get_nb_id(cpu);
	if (nb_id == BAD_APICID)
		return;

	cpu1 = &per_cpu(cpu_hw_events, cpu);
	cpu1->amd_nb = NULL;

	raw_spin_lock(&amd_nb_lock);

	for_each_online_cpu(i) {
		cpu2 = &per_cpu(cpu_hw_events, i);
		nb = cpu2->amd_nb;
		if (!nb)
			continue;
		if (nb->nb_id == nb_id)
			goto found;
	}

	nb = amd_alloc_nb(cpu, nb_id);
	if (!nb) {
		pr_err("perf_events: failed NB allocation for CPU%d\n", cpu);
		raw_spin_unlock(&amd_nb_lock);
		return;
	}
found:
	nb->refcnt++;
	cpu1->amd_nb = nb;

	raw_spin_unlock(&amd_nb_lock);
}

static void amd_pmu_cpu_offline(int cpu)
{
	struct cpu_hw_events *cpuhw;

	if (boot_cpu_data.x86_max_cores < 2)
		return;

	cpuhw = &per_cpu(cpu_hw_events, cpu);

	raw_spin_lock(&amd_nb_lock);

	if (--cpuhw->amd_nb->refcnt == 0)
		kfree(cpuhw->amd_nb);

	cpuhw->amd_nb = NULL;

	raw_spin_unlock(&amd_nb_lock);
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

	/*
	 * explicitly initialize the boot cpu, other cpus will get
	 * the cpu hotplug callbacks from smp_init()
	 */
	amd_pmu_cpu_online(smp_processor_id());
	return 0;
}

#else /* CONFIG_CPU_SUP_AMD */

static int amd_pmu_init(void)
{
	return 0;
}

static void amd_pmu_cpu_online(int cpu)
{
}

static void amd_pmu_cpu_offline(int cpu)
{
}

#endif
