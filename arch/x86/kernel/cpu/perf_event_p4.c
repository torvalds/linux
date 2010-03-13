/*
 * Netburst Perfomance Events (P4, old Xeon)
 *
 *  Copyright (C) 2010 Parallels, Inc., Cyrill Gorcunov <gorcunov@openvz.org>
 *  Copyright (C) 2010 Intel Corporation, Lin Ming <ming.m.lin@intel.com>
 *
 *  For licencing details see kernel-base/COPYING
 */

#ifdef CONFIG_CPU_SUP_INTEL

#include <asm/perf_event_p4.h>

/*
 * array indices: 0,1 - HT threads, used with HT enabled cpu
 */
struct p4_event_template {
	u32 opcode;			/* ESCR event + CCCR selector */
	u64 config;			/* packed predefined bits */
	int dep;			/* upstream dependency event index */
	unsigned int emask;		/* ESCR EventMask */
	unsigned int escr_msr[2];	/* ESCR MSR for this event */
	unsigned int cntr[2];		/* counter index (offset) */
};

struct p4_pmu_res {
	/* maps hw_conf::idx into template for ESCR sake */
	struct p4_event_template *tpl[ARCH_P4_MAX_CCCR];
};

static DEFINE_PER_CPU(struct p4_pmu_res, p4_pmu_config);

/*
 * WARN: CCCR1 doesn't have a working enable bit so try to not
 * use it if possible
 *
 * Also as only we start to support raw events we will need to
 * append _all_ P4_EVENT_PACK'ed events here
 */
struct p4_event_template p4_templates[] = {
	[0] = {
		.opcode	= P4_UOP_TYPE,
		.config	= 0,
		.dep	= -1,
		.emask	=
			P4_EVENT_ATTR(P4_UOP_TYPE, TAGLOADS)	|
			P4_EVENT_ATTR(P4_UOP_TYPE, TAGSTORES),
		.escr_msr	= { MSR_P4_RAT_ESCR0, MSR_P4_RAT_ESCR1 },
		.cntr		= { 16, 17 },
	},
	[1] = {
		.opcode	= P4_GLOBAL_POWER_EVENTS,
		.config	= 0,
		.dep	= -1,
		.emask	=
			P4_EVENT_ATTR(P4_GLOBAL_POWER_EVENTS, RUNNING),
		.escr_msr	= { MSR_P4_FSB_ESCR0, MSR_P4_FSB_ESCR1 },
		.cntr		= { 0, 2 },
	},
	[2] = {
		.opcode	= P4_INSTR_RETIRED,
		.config	= 0,
		.dep	= 0, /* needs front-end tagging */
		.emask	=
			P4_EVENT_ATTR(P4_INSTR_RETIRED, NBOGUSNTAG)	|
			P4_EVENT_ATTR(P4_INSTR_RETIRED, NBOGUSTAG)	|
			P4_EVENT_ATTR(P4_INSTR_RETIRED, BOGUSNTAG)	|
			P4_EVENT_ATTR(P4_INSTR_RETIRED, BOGUSTAG),
		.escr_msr	= { MSR_P4_CRU_ESCR2, MSR_P4_CRU_ESCR3 },
		.cntr		= { 12, 14 },
	},
	[3] = {
		.opcode	= P4_BSQ_CACHE_REFERENCE,
		.config	= 0,
		.dep	= -1,
		.emask	=
			P4_EVENT_ATTR(P4_BSQ_CACHE_REFERENCE, RD_2ndL_HITS)	|
			P4_EVENT_ATTR(P4_BSQ_CACHE_REFERENCE, RD_2ndL_HITE)	|
			P4_EVENT_ATTR(P4_BSQ_CACHE_REFERENCE, RD_2ndL_HITM)	|
			P4_EVENT_ATTR(P4_BSQ_CACHE_REFERENCE, RD_3rdL_HITS)	|
			P4_EVENT_ATTR(P4_BSQ_CACHE_REFERENCE, RD_3rdL_HITE)	|
			P4_EVENT_ATTR(P4_BSQ_CACHE_REFERENCE, RD_3rdL_HITM),
		.escr_msr	= { MSR_P4_BSU_ESCR0, MSR_P4_BSU_ESCR1 },
		.cntr		= { 0, 2 },
	},
	[4] = {
		.opcode	= P4_BSQ_CACHE_REFERENCE,
		.config	= 0,
		.dep	= -1,
		.emask	=
			P4_EVENT_ATTR(P4_BSQ_CACHE_REFERENCE, RD_2ndL_MISS)	|
			P4_EVENT_ATTR(P4_BSQ_CACHE_REFERENCE, RD_3rdL_MISS)	|
			P4_EVENT_ATTR(P4_BSQ_CACHE_REFERENCE, WR_2ndL_MISS),
		.escr_msr	= { MSR_P4_BSU_ESCR0, MSR_P4_BSU_ESCR1 },
		.cntr		= { 0, 3 },
	},
	[5] = {
		.opcode	= P4_RETIRED_BRANCH_TYPE,
		.config	= 0,
		.dep	= -1,
		.emask	=
			P4_EVENT_ATTR(P4_RETIRED_BRANCH_TYPE, CONDITIONAL)	|
			P4_EVENT_ATTR(P4_RETIRED_BRANCH_TYPE, CALL)		|
			P4_EVENT_ATTR(P4_RETIRED_BRANCH_TYPE, RETURN)		|
			P4_EVENT_ATTR(P4_RETIRED_BRANCH_TYPE, INDIRECT),
		.escr_msr	= { MSR_P4_TBPU_ESCR0, MSR_P4_TBPU_ESCR1 },
		.cntr		= { 4, 6 },
	},
	[6] = {
		.opcode	= P4_MISPRED_BRANCH_RETIRED,
		.config	= 0,
		.dep	= -1,
		.emask	=
			P4_EVENT_ATTR(P4_MISPRED_BRANCH_RETIRED, NBOGUS),
		.escr_msr	= { MSR_P4_CRU_ESCR0, MSR_P4_CRU_ESCR1 },
		.cntr		= { 12, 14 },
	},
	[7] = {
		.opcode	= P4_FSB_DATA_ACTIVITY,
		.config	= p4_config_pack_cccr(P4_CCCR_EDGE | P4_CCCR_COMPARE),
		.dep	= -1,
		.emask	=
			P4_EVENT_ATTR(P4_FSB_DATA_ACTIVITY, DRDY_DRV)	|
			P4_EVENT_ATTR(P4_FSB_DATA_ACTIVITY, DRDY_OWN),
		.escr_msr	= { MSR_P4_FSB_ESCR0, MSR_P4_FSB_ESCR1 },
		.cntr		= { 0, 2 },
	},
};

static struct p4_event_template *p4_event_map[PERF_COUNT_HW_MAX] = {
	/* non-halted CPU clocks */
	[PERF_COUNT_HW_CPU_CYCLES]		= &p4_templates[1],

	/* retired instructions: dep on tagging the FSB */
	[PERF_COUNT_HW_INSTRUCTIONS]		= &p4_templates[2],

	/* cache hits */
	[PERF_COUNT_HW_CACHE_REFERENCES]	= &p4_templates[3],

	/* cache misses */
	[PERF_COUNT_HW_CACHE_MISSES]		= &p4_templates[4],

	/* branch instructions retired */
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= &p4_templates[5],

	/* mispredicted branches retired */
	[PERF_COUNT_HW_BRANCH_MISSES]		= &p4_templates[6],

	/* bus ready clocks (cpu is driving #DRDY_DRV\#DRDY_OWN):  */
	[PERF_COUNT_HW_BUS_CYCLES]		= &p4_templates[7],
};

static u64 p4_pmu_event_map(int hw_event)
{
	struct p4_event_template *tpl;
	u64 config;

	if (hw_event > ARRAY_SIZE(p4_event_map)) {
		printk_once(KERN_ERR "PMU: Incorrect event index\n");
		return 0;
	}
	tpl = p4_event_map[hw_event];

	/*
	 * fill config up according to
	 * a predefined event template
	 */
	config  = tpl->config;
	config |= p4_config_pack_escr(P4_EVENT_UNPACK_EVENT(tpl->opcode) << P4_EVNTSEL_EVENT_SHIFT);
	config |= p4_config_pack_escr(tpl->emask << P4_EVNTSEL_EVENTMASK_SHIFT);
	config |= p4_config_pack_cccr(P4_EVENT_UNPACK_SELECTOR(tpl->opcode) << P4_CCCR_ESCR_SELECT_SHIFT);

	/* on HT machine we need a special bit */
	if (p4_ht_active() && p4_ht_thread(raw_smp_processor_id()))
		config = p4_set_ht_bit(config);

	return config;
}

/*
 * Note that we still have 5 events (from global events SDM list)
 * intersected in opcode+emask bits so we will need another
 * scheme there do distinguish templates.
 */
static inline int p4_pmu_emask_match(unsigned int dst, unsigned int src)
{
	return dst & src;
}

static struct p4_event_template *p4_pmu_template_lookup(u64 config)
{
	u32 opcode = p4_config_unpack_opcode(config);
	unsigned int emask = p4_config_unpack_emask(config);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(p4_templates); i++) {
		if (opcode == p4_templates[i].opcode &&
			p4_pmu_emask_match(emask, p4_templates[i].emask))
			return &p4_templates[i];
	}

	return NULL;
}

/*
 * We don't control raw events so it's up to the caller
 * to pass sane values (and we don't count the thread number
 * on HT machine but allow HT-compatible specifics to be
 * passed on)
 */
static u64 p4_pmu_raw_event(u64 hw_event)
{
	return hw_event &
		(p4_config_pack_escr(P4_EVNTSEL_MASK_HT) |
		 p4_config_pack_cccr(P4_CCCR_MASK_HT));
}

static int p4_hw_config(struct perf_event_attr *attr, struct hw_perf_event *hwc)
{
	int cpu = raw_smp_processor_id();

	/*
	 * the reason we use cpu that early is that: if we get scheduled
	 * first time on the same cpu -- we will not need swap thread
	 * specific flags in config (and will save some cpu cycles)
	 */

	/* CCCR by default */
	hwc->config = p4_config_pack_cccr(p4_default_cccr_conf(cpu));

	/* Count user and OS events unless not requested to */
	hwc->config |= p4_config_pack_escr(p4_default_escr_conf(cpu, attr->exclude_kernel,
								attr->exclude_user));
	return 0;
}

static inline void p4_pmu_clear_cccr_ovf(struct hw_perf_event *hwc)
{
	unsigned long dummy;

	rdmsrl(hwc->config_base + hwc->idx, dummy);
	if (dummy & P4_CCCR_OVF) {
		(void)checking_wrmsrl(hwc->config_base + hwc->idx,
			((u64)dummy) & ~P4_CCCR_OVF);
	}
}

static inline void p4_pmu_disable_event(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	/*
	 * If event gets disabled while counter is in overflowed
	 * state we need to clear P4_CCCR_OVF, otherwise interrupt get
	 * asserted again and again
	 */
	(void)checking_wrmsrl(hwc->config_base + hwc->idx,
		(u64)(p4_config_unpack_cccr(hwc->config)) &
			~P4_CCCR_ENABLE & ~P4_CCCR_OVF);
}

static void p4_pmu_disable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	int idx;

	for (idx = 0; idx < x86_pmu.num_events; idx++) {
		struct perf_event *event = cpuc->events[idx];
		if (!test_bit(idx, cpuc->active_mask))
			continue;
		p4_pmu_disable_event(event);
	}
}

static void p4_pmu_enable_event(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int thread = p4_ht_config_thread(hwc->config);
	u64 escr_conf = p4_config_unpack_escr(p4_clear_ht_bit(hwc->config));
	u64 escr_base;
	struct p4_event_template *tpl;
	struct p4_pmu_res *c;

	/*
	 * some preparation work from per-cpu private fields
	 * since we need to find out which ESCR to use
	 */
	c = &__get_cpu_var(p4_pmu_config);
	tpl = c->tpl[hwc->idx];
	if (!tpl) {
		pr_crit("%s: Wrong index: %d\n", __func__, hwc->idx);
		return;
	}
	escr_base = (u64)tpl->escr_msr[thread];

	/*
	 * - we dont support cascaded counters yet
	 * - and counter 1 is broken (erratum)
	 */
	WARN_ON_ONCE(p4_is_event_cascaded(hwc->config));
	WARN_ON_ONCE(hwc->idx == 1);

	(void)checking_wrmsrl(escr_base, escr_conf);
	(void)checking_wrmsrl(hwc->config_base + hwc->idx,
		(u64)(p4_config_unpack_cccr(hwc->config)) | P4_CCCR_ENABLE);
}

static void p4_pmu_enable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	int idx;

	for (idx = 0; idx < x86_pmu.num_events; idx++) {
		struct perf_event *event = cpuc->events[idx];
		if (!test_bit(idx, cpuc->active_mask))
			continue;
		p4_pmu_enable_event(event);
	}
}

static int p4_pmu_handle_irq(struct pt_regs *regs)
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

		WARN_ON_ONCE(hwc->idx != idx);

		/*
		 * FIXME: Redundant call, actually not needed
		 * but just to check if we're screwed
		 */
		p4_pmu_clear_cccr_ovf(hwc);

		val = x86_perf_event_update(event);
		if (val & (1ULL << (x86_pmu.event_bits - 1)))
			continue;

		/*
		 * event overflow
		 */
		handled		= 1;
		data.period	= event->hw.last_period;

		if (!x86_perf_event_set_period(event))
			continue;
		if (perf_event_overflow(event, 1, &data, regs))
			p4_pmu_disable_event(event);
	}

	if (handled) {
#ifdef CONFIG_X86_LOCAL_APIC
		/* p4 quirk: unmask it again */
		apic_write(APIC_LVTPC, apic_read(APIC_LVTPC) & ~APIC_LVT_MASKED);
#endif
		inc_irq_stat(apic_perf_irqs);
	}

	return handled;
}

/*
 * swap thread specific fields according to a thread
 * we are going to run on
 */
static void p4_pmu_swap_config_ts(struct hw_perf_event *hwc, int cpu)
{
	u32 escr, cccr;

	/*
	 * we either lucky and continue on same cpu or no HT support
	 */
	if (!p4_should_swap_ts(hwc->config, cpu))
		return;

	/*
	 * the event is migrated from an another logical
	 * cpu, so we need to swap thread specific flags
	 */

	escr = p4_config_unpack_escr(hwc->config);
	cccr = p4_config_unpack_cccr(hwc->config);

	if (p4_ht_thread(cpu)) {
		cccr &= ~P4_CCCR_OVF_PMI_T0;
		cccr |= P4_CCCR_OVF_PMI_T1;
		if (escr & P4_EVNTSEL_T0_OS) {
			escr &= ~P4_EVNTSEL_T0_OS;
			escr |= P4_EVNTSEL_T1_OS;
		}
		if (escr & P4_EVNTSEL_T0_USR) {
			escr &= ~P4_EVNTSEL_T0_USR;
			escr |= P4_EVNTSEL_T1_USR;
		}
		hwc->config  = p4_config_pack_escr(escr);
		hwc->config |= p4_config_pack_cccr(cccr);
		hwc->config |= P4_CONFIG_HT;
	} else {
		cccr &= ~P4_CCCR_OVF_PMI_T1;
		cccr |= P4_CCCR_OVF_PMI_T0;
		if (escr & P4_EVNTSEL_T1_OS) {
			escr &= ~P4_EVNTSEL_T1_OS;
			escr |= P4_EVNTSEL_T0_OS;
		}
		if (escr & P4_EVNTSEL_T1_USR) {
			escr &= ~P4_EVNTSEL_T1_USR;
			escr |= P4_EVNTSEL_T0_USR;
		}
		hwc->config  = p4_config_pack_escr(escr);
		hwc->config |= p4_config_pack_cccr(cccr);
		hwc->config &= ~P4_CONFIG_HT;
	}
}

/* ESCRs are not sequential in memory so we need a map */
static unsigned int p4_escr_map[ARCH_P4_TOTAL_ESCR] = {
	MSR_P4_ALF_ESCR0,	/*  0 */
	MSR_P4_ALF_ESCR1,	/*  1 */
	MSR_P4_BPU_ESCR0,	/*  2 */
	MSR_P4_BPU_ESCR1,	/*  3 */
	MSR_P4_BSU_ESCR0,	/*  4 */
	MSR_P4_BSU_ESCR1,	/*  5 */
	MSR_P4_CRU_ESCR0,	/*  6 */
	MSR_P4_CRU_ESCR1,	/*  7 */
	MSR_P4_CRU_ESCR2,	/*  8 */
	MSR_P4_CRU_ESCR3,	/*  9 */
	MSR_P4_CRU_ESCR4,	/* 10 */
	MSR_P4_CRU_ESCR5,	/* 11 */
	MSR_P4_DAC_ESCR0,	/* 12 */
	MSR_P4_DAC_ESCR1,	/* 13 */
	MSR_P4_FIRM_ESCR0,	/* 14 */
	MSR_P4_FIRM_ESCR1,	/* 15 */
	MSR_P4_FLAME_ESCR0,	/* 16 */
	MSR_P4_FLAME_ESCR1,	/* 17 */
	MSR_P4_FSB_ESCR0,	/* 18 */
	MSR_P4_FSB_ESCR1,	/* 19 */
	MSR_P4_IQ_ESCR0,	/* 20 */
	MSR_P4_IQ_ESCR1,	/* 21 */
	MSR_P4_IS_ESCR0,	/* 22 */
	MSR_P4_IS_ESCR1,	/* 23 */
	MSR_P4_ITLB_ESCR0,	/* 24 */
	MSR_P4_ITLB_ESCR1,	/* 25 */
	MSR_P4_IX_ESCR0,	/* 26 */
	MSR_P4_IX_ESCR1,	/* 27 */
	MSR_P4_MOB_ESCR0,	/* 28 */
	MSR_P4_MOB_ESCR1,	/* 29 */
	MSR_P4_MS_ESCR0,	/* 30 */
	MSR_P4_MS_ESCR1,	/* 31 */
	MSR_P4_PMH_ESCR0,	/* 32 */
	MSR_P4_PMH_ESCR1,	/* 33 */
	MSR_P4_RAT_ESCR0,	/* 34 */
	MSR_P4_RAT_ESCR1,	/* 35 */
	MSR_P4_SAAT_ESCR0,	/* 36 */
	MSR_P4_SAAT_ESCR1,	/* 37 */
	MSR_P4_SSU_ESCR0,	/* 38 */
	MSR_P4_SSU_ESCR1,	/* 39 */
	MSR_P4_TBPU_ESCR0,	/* 40 */
	MSR_P4_TBPU_ESCR1,	/* 41 */
	MSR_P4_TC_ESCR0,	/* 42 */
	MSR_P4_TC_ESCR1,	/* 43 */
	MSR_P4_U2L_ESCR0,	/* 44 */
	MSR_P4_U2L_ESCR1,	/* 45 */
};

static int p4_get_escr_idx(unsigned int addr)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(p4_escr_map); i++) {
		if (addr == p4_escr_map[i])
			return i;
	}

	return -1;
}

static int p4_pmu_schedule_events(struct cpu_hw_events *cpuc, int n, int *assign)
{
	unsigned long used_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	unsigned long escr_mask[BITS_TO_LONGS(ARCH_P4_TOTAL_ESCR)];

	struct hw_perf_event *hwc;
	struct p4_event_template *tpl;
	struct p4_pmu_res *c;
	int cpu = raw_smp_processor_id();
	int escr_idx, thread, i, num;

	bitmap_zero(used_mask, X86_PMC_IDX_MAX);
	bitmap_zero(escr_mask, ARCH_P4_TOTAL_ESCR);

	c = &__get_cpu_var(p4_pmu_config);
	/*
	 * Firstly find out which resource events are going
	 * to use, if ESCR+CCCR tuple is already borrowed
	 * then get out of here
	 */
	for (i = 0, num = n; i < n; i++, num--) {
		hwc = &cpuc->event_list[i]->hw;
		tpl = p4_pmu_template_lookup(hwc->config);
		if (!tpl)
			goto done;
		thread = p4_ht_thread(cpu);
		escr_idx = p4_get_escr_idx(tpl->escr_msr[thread]);
		if (escr_idx == -1)
			goto done;

		/* already allocated and remains on the same cpu */
		if (hwc->idx != -1 && !p4_should_swap_ts(hwc->config, cpu)) {
			if (assign)
				assign[i] = hwc->idx;
			/* upstream dependent event */
			if (unlikely(tpl->dep != -1))
				printk_once(KERN_WARNING "PMU: Dep events are "
					"not implemented yet\n");
			goto reserve;
		}

		/* it may be already borrowed */
		if (test_bit(tpl->cntr[thread], used_mask) ||
			test_bit(escr_idx, escr_mask))
			goto done;

		/*
		 * ESCR+CCCR+COUNTERs are available to use lets swap
		 * thread specific bits, push assigned bits
		 * back and save template into per-cpu
		 * area (which will allow us to find out the ESCR
		 * to be used at moment of "enable event via real MSR")
		 */
		p4_pmu_swap_config_ts(hwc, cpu);
		if (assign) {
			assign[i] = tpl->cntr[thread];
			c->tpl[assign[i]] = tpl;
		}
reserve:
		set_bit(tpl->cntr[thread], used_mask);
		set_bit(escr_idx, escr_mask);
	}

done:
	return num ? -ENOSPC : 0;
}

static __initconst struct x86_pmu p4_pmu = {
	.name			= "Netburst P4/Xeon",
	.handle_irq		= p4_pmu_handle_irq,
	.disable_all		= p4_pmu_disable_all,
	.enable_all		= p4_pmu_enable_all,
	.enable			= p4_pmu_enable_event,
	.disable		= p4_pmu_disable_event,
	.eventsel		= MSR_P4_BPU_CCCR0,
	.perfctr		= MSR_P4_BPU_PERFCTR0,
	.event_map		= p4_pmu_event_map,
	.raw_event		= p4_pmu_raw_event,
	.max_events		= ARRAY_SIZE(p4_event_map),
	.get_event_constraints	= x86_get_event_constraints,
	/*
	 * IF HT disabled we may need to use all
	 * ARCH_P4_MAX_CCCR counters simulaneously
	 * though leave it restricted at moment assuming
	 * HT is on
	 */
	.num_events		= ARCH_P4_MAX_CCCR,
	.apic			= 1,
	.event_bits		= 40,
	.event_mask		= (1ULL << 40) - 1,
	.max_period		= (1ULL << 39) - 1,
	.hw_config		= p4_hw_config,
	.schedule_events	= p4_pmu_schedule_events,
};

static __init int p4_pmu_init(void)
{
	unsigned int low, high;

	/* If we get stripped -- indexig fails */
	BUILD_BUG_ON(ARCH_P4_MAX_CCCR > X86_PMC_MAX_GENERIC);

	rdmsr(MSR_IA32_MISC_ENABLE, low, high);
	if (!(low & (1 << 7))) {
		pr_cont("unsupported Netburst CPU model %d ",
			boot_cpu_data.x86_model);
		return -ENODEV;
	}

	pr_cont("Netburst events, ");

	x86_pmu = p4_pmu;

	return 0;
}

#endif /* CONFIG_CPU_SUP_INTEL */
