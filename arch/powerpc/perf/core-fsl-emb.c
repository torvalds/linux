/*
 * Performance event support - Freescale Embedded Performance Monitor
 *
 * Copyright 2008-2009 Paul Mackerras, IBM Corporation.
 * Copyright 2010 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/perf_event.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <asm/reg_fsl_emb.h>
#include <asm/pmc.h>
#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/ptrace.h>

struct cpu_hw_events {
	int n_events;
	int disabled;
	u8  pmcs_enabled;
	struct perf_event *event[MAX_HWEVENTS];
};
static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

static struct fsl_emb_pmu *ppmu;

/* Number of perf_events counting hardware events */
static atomic_t num_events;
/* Used to avoid races in calling reserve/release_pmc_hardware */
static DEFINE_MUTEX(pmc_reserve_mutex);

/*
 * If interrupts were soft-disabled when a PMU interrupt occurs, treat
 * it as an NMI.
 */
static inline int perf_intr_is_nmi(struct pt_regs *regs)
{
#ifdef __powerpc64__
	return !regs->softe;
#else
	return 0;
#endif
}

static void perf_event_interrupt(struct pt_regs *regs);

/*
 * Read one performance monitor counter (PMC).
 */
static unsigned long read_pmc(int idx)
{
	unsigned long val;

	switch (idx) {
	case 0:
		val = mfpmr(PMRN_PMC0);
		break;
	case 1:
		val = mfpmr(PMRN_PMC1);
		break;
	case 2:
		val = mfpmr(PMRN_PMC2);
		break;
	case 3:
		val = mfpmr(PMRN_PMC3);
		break;
	default:
		printk(KERN_ERR "oops trying to read PMC%d\n", idx);
		val = 0;
	}
	return val;
}

/*
 * Write one PMC.
 */
static void write_pmc(int idx, unsigned long val)
{
	switch (idx) {
	case 0:
		mtpmr(PMRN_PMC0, val);
		break;
	case 1:
		mtpmr(PMRN_PMC1, val);
		break;
	case 2:
		mtpmr(PMRN_PMC2, val);
		break;
	case 3:
		mtpmr(PMRN_PMC3, val);
		break;
	default:
		printk(KERN_ERR "oops trying to write PMC%d\n", idx);
	}

	isync();
}

/*
 * Write one local control A register
 */
static void write_pmlca(int idx, unsigned long val)
{
	switch (idx) {
	case 0:
		mtpmr(PMRN_PMLCA0, val);
		break;
	case 1:
		mtpmr(PMRN_PMLCA1, val);
		break;
	case 2:
		mtpmr(PMRN_PMLCA2, val);
		break;
	case 3:
		mtpmr(PMRN_PMLCA3, val);
		break;
	default:
		printk(KERN_ERR "oops trying to write PMLCA%d\n", idx);
	}

	isync();
}

/*
 * Write one local control B register
 */
static void write_pmlcb(int idx, unsigned long val)
{
	switch (idx) {
	case 0:
		mtpmr(PMRN_PMLCB0, val);
		break;
	case 1:
		mtpmr(PMRN_PMLCB1, val);
		break;
	case 2:
		mtpmr(PMRN_PMLCB2, val);
		break;
	case 3:
		mtpmr(PMRN_PMLCB3, val);
		break;
	default:
		printk(KERN_ERR "oops trying to write PMLCB%d\n", idx);
	}

	isync();
}

static void fsl_emb_pmu_read(struct perf_event *event)
{
	s64 val, delta, prev;

	if (event->hw.state & PERF_HES_STOPPED)
		return;

	/*
	 * Performance monitor interrupts come even when interrupts
	 * are soft-disabled, as long as interrupts are hard-enabled.
	 * Therefore we treat them like NMIs.
	 */
	do {
		prev = local64_read(&event->hw.prev_count);
		barrier();
		val = read_pmc(event->hw.idx);
	} while (local64_cmpxchg(&event->hw.prev_count, prev, val) != prev);

	/* The counters are only 32 bits wide */
	delta = (val - prev) & 0xfffffffful;
	local64_add(delta, &event->count);
	local64_sub(delta, &event->hw.period_left);
}

/*
 * Disable all events to prevent PMU interrupts and to allow
 * events to be added or removed.
 */
static void fsl_emb_pmu_disable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuhw;
	unsigned long flags;

	local_irq_save(flags);
	cpuhw = &__get_cpu_var(cpu_hw_events);

	if (!cpuhw->disabled) {
		cpuhw->disabled = 1;

		/*
		 * Check if we ever enabled the PMU on this cpu.
		 */
		if (!cpuhw->pmcs_enabled) {
			ppc_enable_pmcs();
			cpuhw->pmcs_enabled = 1;
		}

		if (atomic_read(&num_events)) {
			/*
			 * Set the 'freeze all counters' bit, and disable
			 * interrupts.  The barrier is to make sure the
			 * mtpmr has been executed and the PMU has frozen
			 * the events before we return.
			 */

			mtpmr(PMRN_PMGC0, PMGC0_FAC);
			isync();
		}
	}
	local_irq_restore(flags);
}

/*
 * Re-enable all events if disable == 0.
 * If we were previously disabled and events were added, then
 * put the new config on the PMU.
 */
static void fsl_emb_pmu_enable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuhw;
	unsigned long flags;

	local_irq_save(flags);
	cpuhw = &__get_cpu_var(cpu_hw_events);
	if (!cpuhw->disabled)
		goto out;

	cpuhw->disabled = 0;
	ppc_set_pmu_inuse(cpuhw->n_events != 0);

	if (cpuhw->n_events > 0) {
		mtpmr(PMRN_PMGC0, PMGC0_PMIE | PMGC0_FCECE);
		isync();
	}

 out:
	local_irq_restore(flags);
}

static int collect_events(struct perf_event *group, int max_count,
			  struct perf_event *ctrs[])
{
	int n = 0;
	struct perf_event *event;

	if (!is_software_event(group)) {
		if (n >= max_count)
			return -1;
		ctrs[n] = group;
		n++;
	}
	list_for_each_entry(event, &group->sibling_list, group_entry) {
		if (!is_software_event(event) &&
		    event->state != PERF_EVENT_STATE_OFF) {
			if (n >= max_count)
				return -1;
			ctrs[n] = event;
			n++;
		}
	}
	return n;
}

/* context locked on entry */
static int fsl_emb_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuhw;
	int ret = -EAGAIN;
	int num_counters = ppmu->n_counter;
	u64 val;
	int i;

	perf_pmu_disable(event->pmu);
	cpuhw = &get_cpu_var(cpu_hw_events);

	if (event->hw.config & FSL_EMB_EVENT_RESTRICTED)
		num_counters = ppmu->n_restricted;

	/*
	 * Allocate counters from top-down, so that restricted-capable
	 * counters are kept free as long as possible.
	 */
	for (i = num_counters - 1; i >= 0; i--) {
		if (cpuhw->event[i])
			continue;

		break;
	}

	if (i < 0)
		goto out;

	event->hw.idx = i;
	cpuhw->event[i] = event;
	++cpuhw->n_events;

	val = 0;
	if (event->hw.sample_period) {
		s64 left = local64_read(&event->hw.period_left);
		if (left < 0x80000000L)
			val = 0x80000000L - left;
	}
	local64_set(&event->hw.prev_count, val);

	if (!(flags & PERF_EF_START)) {
		event->hw.state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
		val = 0;
	}

	write_pmc(i, val);
	perf_event_update_userpage(event);

	write_pmlcb(i, event->hw.config >> 32);
	write_pmlca(i, event->hw.config_base);

	ret = 0;
 out:
	put_cpu_var(cpu_hw_events);
	perf_pmu_enable(event->pmu);
	return ret;
}

/* context locked on entry */
static void fsl_emb_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuhw;
	int i = event->hw.idx;

	perf_pmu_disable(event->pmu);
	if (i < 0)
		goto out;

	fsl_emb_pmu_read(event);

	cpuhw = &get_cpu_var(cpu_hw_events);

	WARN_ON(event != cpuhw->event[event->hw.idx]);

	write_pmlca(i, 0);
	write_pmlcb(i, 0);
	write_pmc(i, 0);

	cpuhw->event[i] = NULL;
	event->hw.idx = -1;

	/*
	 * TODO: if at least one restricted event exists, and we
	 * just freed up a non-restricted-capable counter, and
	 * there is a restricted-capable counter occupied by
	 * a non-restricted event, migrate that event to the
	 * vacated counter.
	 */

	cpuhw->n_events--;

 out:
	perf_pmu_enable(event->pmu);
	put_cpu_var(cpu_hw_events);
}

static void fsl_emb_pmu_start(struct perf_event *event, int ef_flags)
{
	unsigned long flags;
	s64 left;

	if (event->hw.idx < 0 || !event->hw.sample_period)
		return;

	if (!(event->hw.state & PERF_HES_STOPPED))
		return;

	if (ef_flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	local_irq_save(flags);
	perf_pmu_disable(event->pmu);

	event->hw.state = 0;
	left = local64_read(&event->hw.period_left);
	write_pmc(event->hw.idx, left);

	perf_event_update_userpage(event);
	perf_pmu_enable(event->pmu);
	local_irq_restore(flags);
}

static void fsl_emb_pmu_stop(struct perf_event *event, int ef_flags)
{
	unsigned long flags;

	if (event->hw.idx < 0 || !event->hw.sample_period)
		return;

	if (event->hw.state & PERF_HES_STOPPED)
		return;

	local_irq_save(flags);
	perf_pmu_disable(event->pmu);

	fsl_emb_pmu_read(event);
	event->hw.state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
	write_pmc(event->hw.idx, 0);

	perf_event_update_userpage(event);
	perf_pmu_enable(event->pmu);
	local_irq_restore(flags);
}

/*
 * Release the PMU if this is the last perf_event.
 */
static void hw_perf_event_destroy(struct perf_event *event)
{
	if (!atomic_add_unless(&num_events, -1, 1)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_dec_return(&num_events) == 0)
			release_pmc_hardware();
		mutex_unlock(&pmc_reserve_mutex);
	}
}

/*
 * Translate a generic cache event_id config to a raw event_id code.
 */
static int hw_perf_cache_event(u64 config, u64 *eventp)
{
	unsigned long type, op, result;
	int ev;

	if (!ppmu->cache_events)
		return -EINVAL;

	/* unpack config */
	type = config & 0xff;
	op = (config >> 8) & 0xff;
	result = (config >> 16) & 0xff;

	if (type >= PERF_COUNT_HW_CACHE_MAX ||
	    op >= PERF_COUNT_HW_CACHE_OP_MAX ||
	    result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	ev = (*ppmu->cache_events)[type][op][result];
	if (ev == 0)
		return -EOPNOTSUPP;
	if (ev == -1)
		return -EINVAL;
	*eventp = ev;
	return 0;
}

static int fsl_emb_pmu_event_init(struct perf_event *event)
{
	u64 ev;
	struct perf_event *events[MAX_HWEVENTS];
	int n;
	int err;
	int num_restricted;
	int i;

	if (ppmu->n_counter > MAX_HWEVENTS) {
		WARN(1, "No. of perf counters (%d) is higher than max array size(%d)\n",
			ppmu->n_counter, MAX_HWEVENTS);
		ppmu->n_counter = MAX_HWEVENTS;
	}

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		ev = event->attr.config;
		if (ev >= ppmu->n_generic || ppmu->generic_events[ev] == 0)
			return -EOPNOTSUPP;
		ev = ppmu->generic_events[ev];
		break;

	case PERF_TYPE_HW_CACHE:
		err = hw_perf_cache_event(event->attr.config, &ev);
		if (err)
			return err;
		break;

	case PERF_TYPE_RAW:
		ev = event->attr.config;
		break;

	default:
		return -ENOENT;
	}

	event->hw.config = ppmu->xlate_event(ev);
	if (!(event->hw.config & FSL_EMB_EVENT_VALID))
		return -EINVAL;

	/*
	 * If this is in a group, check if it can go on with all the
	 * other hardware events in the group.  We assume the event
	 * hasn't been linked into its leader's sibling list at this point.
	 */
	n = 0;
	if (event->group_leader != event) {
		n = collect_events(event->group_leader,
		                   ppmu->n_counter - 1, events);
		if (n < 0)
			return -EINVAL;
	}

	if (event->hw.config & FSL_EMB_EVENT_RESTRICTED) {
		num_restricted = 0;
		for (i = 0; i < n; i++) {
			if (events[i]->hw.config & FSL_EMB_EVENT_RESTRICTED)
				num_restricted++;
		}

		if (num_restricted >= ppmu->n_restricted)
			return -EINVAL;
	}

	event->hw.idx = -1;

	event->hw.config_base = PMLCA_CE | PMLCA_FCM1 |
	                        (u32)((ev << 16) & PMLCA_EVENT_MASK);

	if (event->attr.exclude_user)
		event->hw.config_base |= PMLCA_FCU;
	if (event->attr.exclude_kernel)
		event->hw.config_base |= PMLCA_FCS;
	if (event->attr.exclude_idle)
		return -ENOTSUPP;

	event->hw.last_period = event->hw.sample_period;
	local64_set(&event->hw.period_left, event->hw.last_period);

	/*
	 * See if we need to reserve the PMU.
	 * If no events are currently in use, then we have to take a
	 * mutex to ensure that we don't race with another task doing
	 * reserve_pmc_hardware or release_pmc_hardware.
	 */
	err = 0;
	if (!atomic_inc_not_zero(&num_events)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&num_events) == 0 &&
		    reserve_pmc_hardware(perf_event_interrupt))
			err = -EBUSY;
		else
			atomic_inc(&num_events);
		mutex_unlock(&pmc_reserve_mutex);

		mtpmr(PMRN_PMGC0, PMGC0_FAC);
		isync();
	}
	event->destroy = hw_perf_event_destroy;

	return err;
}

static struct pmu fsl_emb_pmu = {
	.pmu_enable	= fsl_emb_pmu_enable,
	.pmu_disable	= fsl_emb_pmu_disable,
	.event_init	= fsl_emb_pmu_event_init,
	.add		= fsl_emb_pmu_add,
	.del		= fsl_emb_pmu_del,
	.start		= fsl_emb_pmu_start,
	.stop		= fsl_emb_pmu_stop,
	.read		= fsl_emb_pmu_read,
};

/*
 * A counter has overflowed; update its count and record
 * things if requested.  Note that interrupts are hard-disabled
 * here so there is no possibility of being interrupted.
 */
static void record_and_restart(struct perf_event *event, unsigned long val,
			       struct pt_regs *regs)
{
	u64 period = event->hw.sample_period;
	s64 prev, delta, left;
	int record = 0;

	if (event->hw.state & PERF_HES_STOPPED) {
		write_pmc(event->hw.idx, 0);
		return;
	}

	/* we don't have to worry about interrupts here */
	prev = local64_read(&event->hw.prev_count);
	delta = (val - prev) & 0xfffffffful;
	local64_add(delta, &event->count);

	/*
	 * See if the total period for this event has expired,
	 * and update for the next period.
	 */
	val = 0;
	left = local64_read(&event->hw.period_left) - delta;
	if (period) {
		if (left <= 0) {
			left += period;
			if (left <= 0)
				left = period;
			record = 1;
			event->hw.last_period = event->hw.sample_period;
		}
		if (left < 0x80000000LL)
			val = 0x80000000LL - left;
	}

	write_pmc(event->hw.idx, val);
	local64_set(&event->hw.prev_count, val);
	local64_set(&event->hw.period_left, left);
	perf_event_update_userpage(event);

	/*
	 * Finally record data if requested.
	 */
	if (record) {
		struct perf_sample_data data;

		perf_sample_data_init(&data, 0, event->hw.last_period);

		if (perf_event_overflow(event, &data, regs))
			fsl_emb_pmu_stop(event, 0);
	}
}

static void perf_event_interrupt(struct pt_regs *regs)
{
	int i;
	struct cpu_hw_events *cpuhw = &__get_cpu_var(cpu_hw_events);
	struct perf_event *event;
	unsigned long val;
	int found = 0;
	int nmi;

	nmi = perf_intr_is_nmi(regs);
	if (nmi)
		nmi_enter();
	else
		irq_enter();

	for (i = 0; i < ppmu->n_counter; ++i) {
		event = cpuhw->event[i];

		val = read_pmc(i);
		if ((int)val < 0) {
			if (event) {
				/* event has overflowed */
				found = 1;
				record_and_restart(event, val, regs);
			} else {
				/*
				 * Disabled counter is negative,
				 * reset it just in case.
				 */
				write_pmc(i, 0);
			}
		}
	}

	/* PMM will keep counters frozen until we return from the interrupt. */
	mtmsr(mfmsr() | MSR_PMM);
	mtpmr(PMRN_PMGC0, PMGC0_PMIE | PMGC0_FCECE);
	isync();

	if (nmi)
		nmi_exit();
	else
		irq_exit();
}

void hw_perf_event_setup(int cpu)
{
	struct cpu_hw_events *cpuhw = &per_cpu(cpu_hw_events, cpu);

	memset(cpuhw, 0, sizeof(*cpuhw));
}

int register_fsl_emb_pmu(struct fsl_emb_pmu *pmu)
{
	if (ppmu)
		return -EBUSY;		/* something's already registered */

	ppmu = pmu;
	pr_info("%s performance monitor hardware support registered\n",
		pmu->name);

	perf_pmu_register(&fsl_emb_pmu, "cpu", PERF_TYPE_RAW);

	return 0;
}
