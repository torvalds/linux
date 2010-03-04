/*
 * Performance event support - powerpc architecture code
 *
 * Copyright 2008-2009 Paul Mackerras, IBM Corporation.
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
#include <asm/reg.h>
#include <asm/pmc.h>
#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/ptrace.h>

struct cpu_hw_events {
	int n_events;
	int n_percpu;
	int disabled;
	int n_added;
	int n_limited;
	u8  pmcs_enabled;
	struct perf_event *event[MAX_HWEVENTS];
	u64 events[MAX_HWEVENTS];
	unsigned int flags[MAX_HWEVENTS];
	unsigned long mmcr[3];
	struct perf_event *limited_counter[MAX_LIMITED_HWCOUNTERS];
	u8  limited_hwidx[MAX_LIMITED_HWCOUNTERS];
	u64 alternatives[MAX_HWEVENTS][MAX_EVENT_ALTERNATIVES];
	unsigned long amasks[MAX_HWEVENTS][MAX_EVENT_ALTERNATIVES];
	unsigned long avalues[MAX_HWEVENTS][MAX_EVENT_ALTERNATIVES];
};
DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

struct power_pmu *ppmu;

/*
 * Normally, to ignore kernel events we set the FCS (freeze counters
 * in supervisor mode) bit in MMCR0, but if the kernel runs with the
 * hypervisor bit set in the MSR, or if we are running on a processor
 * where the hypervisor bit is forced to 1 (as on Apple G5 processors),
 * then we need to use the FCHV bit to ignore kernel events.
 */
static unsigned int freeze_events_kernel = MMCR0_FCS;

/*
 * 32-bit doesn't have MMCRA but does have an MMCR2,
 * and a few other names are different.
 */
#ifdef CONFIG_PPC32

#define MMCR0_FCHV		0
#define MMCR0_PMCjCE		MMCR0_PMCnCE

#define SPRN_MMCRA		SPRN_MMCR2
#define MMCRA_SAMPLE_ENABLE	0

static inline unsigned long perf_ip_adjust(struct pt_regs *regs)
{
	return 0;
}
static inline void perf_get_data_addr(struct pt_regs *regs, u64 *addrp) { }
static inline u32 perf_get_misc_flags(struct pt_regs *regs)
{
	return 0;
}
static inline void perf_read_regs(struct pt_regs *regs) { }
static inline int perf_intr_is_nmi(struct pt_regs *regs)
{
	return 0;
}

#endif /* CONFIG_PPC32 */

/*
 * Things that are specific to 64-bit implementations.
 */
#ifdef CONFIG_PPC64

static inline unsigned long perf_ip_adjust(struct pt_regs *regs)
{
	unsigned long mmcra = regs->dsisr;

	if ((mmcra & MMCRA_SAMPLE_ENABLE) && !(ppmu->flags & PPMU_ALT_SIPR)) {
		unsigned long slot = (mmcra & MMCRA_SLOT) >> MMCRA_SLOT_SHIFT;
		if (slot > 1)
			return 4 * (slot - 1);
	}
	return 0;
}

/*
 * The user wants a data address recorded.
 * If we're not doing instruction sampling, give them the SDAR
 * (sampled data address).  If we are doing instruction sampling, then
 * only give them the SDAR if it corresponds to the instruction
 * pointed to by SIAR; this is indicated by the [POWER6_]MMCRA_SDSYNC
 * bit in MMCRA.
 */
static inline void perf_get_data_addr(struct pt_regs *regs, u64 *addrp)
{
	unsigned long mmcra = regs->dsisr;
	unsigned long sdsync = (ppmu->flags & PPMU_ALT_SIPR) ?
		POWER6_MMCRA_SDSYNC : MMCRA_SDSYNC;

	if (!(mmcra & MMCRA_SAMPLE_ENABLE) || (mmcra & sdsync))
		*addrp = mfspr(SPRN_SDAR);
}

static inline u32 perf_get_misc_flags(struct pt_regs *regs)
{
	unsigned long mmcra = regs->dsisr;
	unsigned long sihv = MMCRA_SIHV;
	unsigned long sipr = MMCRA_SIPR;

	if (TRAP(regs) != 0xf00)
		return 0;	/* not a PMU interrupt */

	if (ppmu->flags & PPMU_ALT_SIPR) {
		sihv = POWER6_MMCRA_SIHV;
		sipr = POWER6_MMCRA_SIPR;
	}

	/* PR has priority over HV, so order below is important */
	if (mmcra & sipr)
		return PERF_RECORD_MISC_USER;
	if ((mmcra & sihv) && (freeze_events_kernel != MMCR0_FCHV))
		return PERF_RECORD_MISC_HYPERVISOR;
	return PERF_RECORD_MISC_KERNEL;
}

/*
 * Overload regs->dsisr to store MMCRA so we only need to read it once
 * on each interrupt.
 */
static inline void perf_read_regs(struct pt_regs *regs)
{
	regs->dsisr = mfspr(SPRN_MMCRA);
}

/*
 * If interrupts were soft-disabled when a PMU interrupt occurs, treat
 * it as an NMI.
 */
static inline int perf_intr_is_nmi(struct pt_regs *regs)
{
	return !regs->softe;
}

#endif /* CONFIG_PPC64 */

static void perf_event_interrupt(struct pt_regs *regs);

void perf_event_print_debug(void)
{
}

/*
 * Read one performance monitor counter (PMC).
 */
static unsigned long read_pmc(int idx)
{
	unsigned long val;

	switch (idx) {
	case 1:
		val = mfspr(SPRN_PMC1);
		break;
	case 2:
		val = mfspr(SPRN_PMC2);
		break;
	case 3:
		val = mfspr(SPRN_PMC3);
		break;
	case 4:
		val = mfspr(SPRN_PMC4);
		break;
	case 5:
		val = mfspr(SPRN_PMC5);
		break;
	case 6:
		val = mfspr(SPRN_PMC6);
		break;
#ifdef CONFIG_PPC64
	case 7:
		val = mfspr(SPRN_PMC7);
		break;
	case 8:
		val = mfspr(SPRN_PMC8);
		break;
#endif /* CONFIG_PPC64 */
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
	case 1:
		mtspr(SPRN_PMC1, val);
		break;
	case 2:
		mtspr(SPRN_PMC2, val);
		break;
	case 3:
		mtspr(SPRN_PMC3, val);
		break;
	case 4:
		mtspr(SPRN_PMC4, val);
		break;
	case 5:
		mtspr(SPRN_PMC5, val);
		break;
	case 6:
		mtspr(SPRN_PMC6, val);
		break;
#ifdef CONFIG_PPC64
	case 7:
		mtspr(SPRN_PMC7, val);
		break;
	case 8:
		mtspr(SPRN_PMC8, val);
		break;
#endif /* CONFIG_PPC64 */
	default:
		printk(KERN_ERR "oops trying to write PMC%d\n", idx);
	}
}

/*
 * Check if a set of events can all go on the PMU at once.
 * If they can't, this will look at alternative codes for the events
 * and see if any combination of alternative codes is feasible.
 * The feasible set is returned in event_id[].
 */
static int power_check_constraints(struct cpu_hw_events *cpuhw,
				   u64 event_id[], unsigned int cflags[],
				   int n_ev)
{
	unsigned long mask, value, nv;
	unsigned long smasks[MAX_HWEVENTS], svalues[MAX_HWEVENTS];
	int n_alt[MAX_HWEVENTS], choice[MAX_HWEVENTS];
	int i, j;
	unsigned long addf = ppmu->add_fields;
	unsigned long tadd = ppmu->test_adder;

	if (n_ev > ppmu->n_counter)
		return -1;

	/* First see if the events will go on as-is */
	for (i = 0; i < n_ev; ++i) {
		if ((cflags[i] & PPMU_LIMITED_PMC_REQD)
		    && !ppmu->limited_pmc_event(event_id[i])) {
			ppmu->get_alternatives(event_id[i], cflags[i],
					       cpuhw->alternatives[i]);
			event_id[i] = cpuhw->alternatives[i][0];
		}
		if (ppmu->get_constraint(event_id[i], &cpuhw->amasks[i][0],
					 &cpuhw->avalues[i][0]))
			return -1;
	}
	value = mask = 0;
	for (i = 0; i < n_ev; ++i) {
		nv = (value | cpuhw->avalues[i][0]) +
			(value & cpuhw->avalues[i][0] & addf);
		if ((((nv + tadd) ^ value) & mask) != 0 ||
		    (((nv + tadd) ^ cpuhw->avalues[i][0]) &
		     cpuhw->amasks[i][0]) != 0)
			break;
		value = nv;
		mask |= cpuhw->amasks[i][0];
	}
	if (i == n_ev)
		return 0;	/* all OK */

	/* doesn't work, gather alternatives... */
	if (!ppmu->get_alternatives)
		return -1;
	for (i = 0; i < n_ev; ++i) {
		choice[i] = 0;
		n_alt[i] = ppmu->get_alternatives(event_id[i], cflags[i],
						  cpuhw->alternatives[i]);
		for (j = 1; j < n_alt[i]; ++j)
			ppmu->get_constraint(cpuhw->alternatives[i][j],
					     &cpuhw->amasks[i][j],
					     &cpuhw->avalues[i][j]);
	}

	/* enumerate all possibilities and see if any will work */
	i = 0;
	j = -1;
	value = mask = nv = 0;
	while (i < n_ev) {
		if (j >= 0) {
			/* we're backtracking, restore context */
			value = svalues[i];
			mask = smasks[i];
			j = choice[i];
		}
		/*
		 * See if any alternative k for event_id i,
		 * where k > j, will satisfy the constraints.
		 */
		while (++j < n_alt[i]) {
			nv = (value | cpuhw->avalues[i][j]) +
				(value & cpuhw->avalues[i][j] & addf);
			if ((((nv + tadd) ^ value) & mask) == 0 &&
			    (((nv + tadd) ^ cpuhw->avalues[i][j])
			     & cpuhw->amasks[i][j]) == 0)
				break;
		}
		if (j >= n_alt[i]) {
			/*
			 * No feasible alternative, backtrack
			 * to event_id i-1 and continue enumerating its
			 * alternatives from where we got up to.
			 */
			if (--i < 0)
				return -1;
		} else {
			/*
			 * Found a feasible alternative for event_id i,
			 * remember where we got up to with this event_id,
			 * go on to the next event_id, and start with
			 * the first alternative for it.
			 */
			choice[i] = j;
			svalues[i] = value;
			smasks[i] = mask;
			value = nv;
			mask |= cpuhw->amasks[i][j];
			++i;
			j = -1;
		}
	}

	/* OK, we have a feasible combination, tell the caller the solution */
	for (i = 0; i < n_ev; ++i)
		event_id[i] = cpuhw->alternatives[i][choice[i]];
	return 0;
}

/*
 * Check if newly-added events have consistent settings for
 * exclude_{user,kernel,hv} with each other and any previously
 * added events.
 */
static int check_excludes(struct perf_event **ctrs, unsigned int cflags[],
			  int n_prev, int n_new)
{
	int eu = 0, ek = 0, eh = 0;
	int i, n, first;
	struct perf_event *event;

	n = n_prev + n_new;
	if (n <= 1)
		return 0;

	first = 1;
	for (i = 0; i < n; ++i) {
		if (cflags[i] & PPMU_LIMITED_PMC_OK) {
			cflags[i] &= ~PPMU_LIMITED_PMC_REQD;
			continue;
		}
		event = ctrs[i];
		if (first) {
			eu = event->attr.exclude_user;
			ek = event->attr.exclude_kernel;
			eh = event->attr.exclude_hv;
			first = 0;
		} else if (event->attr.exclude_user != eu ||
			   event->attr.exclude_kernel != ek ||
			   event->attr.exclude_hv != eh) {
			return -EAGAIN;
		}
	}

	if (eu || ek || eh)
		for (i = 0; i < n; ++i)
			if (cflags[i] & PPMU_LIMITED_PMC_OK)
				cflags[i] |= PPMU_LIMITED_PMC_REQD;

	return 0;
}

static void power_pmu_read(struct perf_event *event)
{
	s64 val, delta, prev;

	if (!event->hw.idx)
		return;
	/*
	 * Performance monitor interrupts come even when interrupts
	 * are soft-disabled, as long as interrupts are hard-enabled.
	 * Therefore we treat them like NMIs.
	 */
	do {
		prev = atomic64_read(&event->hw.prev_count);
		barrier();
		val = read_pmc(event->hw.idx);
	} while (atomic64_cmpxchg(&event->hw.prev_count, prev, val) != prev);

	/* The counters are only 32 bits wide */
	delta = (val - prev) & 0xfffffffful;
	atomic64_add(delta, &event->count);
	atomic64_sub(delta, &event->hw.period_left);
}

/*
 * On some machines, PMC5 and PMC6 can't be written, don't respect
 * the freeze conditions, and don't generate interrupts.  This tells
 * us if `event' is using such a PMC.
 */
static int is_limited_pmc(int pmcnum)
{
	return (ppmu->flags & PPMU_LIMITED_PMC5_6)
		&& (pmcnum == 5 || pmcnum == 6);
}

static void freeze_limited_counters(struct cpu_hw_events *cpuhw,
				    unsigned long pmc5, unsigned long pmc6)
{
	struct perf_event *event;
	u64 val, prev, delta;
	int i;

	for (i = 0; i < cpuhw->n_limited; ++i) {
		event = cpuhw->limited_counter[i];
		if (!event->hw.idx)
			continue;
		val = (event->hw.idx == 5) ? pmc5 : pmc6;
		prev = atomic64_read(&event->hw.prev_count);
		event->hw.idx = 0;
		delta = (val - prev) & 0xfffffffful;
		atomic64_add(delta, &event->count);
	}
}

static void thaw_limited_counters(struct cpu_hw_events *cpuhw,
				  unsigned long pmc5, unsigned long pmc6)
{
	struct perf_event *event;
	u64 val;
	int i;

	for (i = 0; i < cpuhw->n_limited; ++i) {
		event = cpuhw->limited_counter[i];
		event->hw.idx = cpuhw->limited_hwidx[i];
		val = (event->hw.idx == 5) ? pmc5 : pmc6;
		atomic64_set(&event->hw.prev_count, val);
		perf_event_update_userpage(event);
	}
}

/*
 * Since limited events don't respect the freeze conditions, we
 * have to read them immediately after freezing or unfreezing the
 * other events.  We try to keep the values from the limited
 * events as consistent as possible by keeping the delay (in
 * cycles and instructions) between freezing/unfreezing and reading
 * the limited events as small and consistent as possible.
 * Therefore, if any limited events are in use, we read them
 * both, and always in the same order, to minimize variability,
 * and do it inside the same asm that writes MMCR0.
 */
static void write_mmcr0(struct cpu_hw_events *cpuhw, unsigned long mmcr0)
{
	unsigned long pmc5, pmc6;

	if (!cpuhw->n_limited) {
		mtspr(SPRN_MMCR0, mmcr0);
		return;
	}

	/*
	 * Write MMCR0, then read PMC5 and PMC6 immediately.
	 * To ensure we don't get a performance monitor interrupt
	 * between writing MMCR0 and freezing/thawing the limited
	 * events, we first write MMCR0 with the event overflow
	 * interrupt enable bits turned off.
	 */
	asm volatile("mtspr %3,%2; mfspr %0,%4; mfspr %1,%5"
		     : "=&r" (pmc5), "=&r" (pmc6)
		     : "r" (mmcr0 & ~(MMCR0_PMC1CE | MMCR0_PMCjCE)),
		       "i" (SPRN_MMCR0),
		       "i" (SPRN_PMC5), "i" (SPRN_PMC6));

	if (mmcr0 & MMCR0_FC)
		freeze_limited_counters(cpuhw, pmc5, pmc6);
	else
		thaw_limited_counters(cpuhw, pmc5, pmc6);

	/*
	 * Write the full MMCR0 including the event overflow interrupt
	 * enable bits, if necessary.
	 */
	if (mmcr0 & (MMCR0_PMC1CE | MMCR0_PMCjCE))
		mtspr(SPRN_MMCR0, mmcr0);
}

/*
 * Disable all events to prevent PMU interrupts and to allow
 * events to be added or removed.
 */
void hw_perf_disable(void)
{
	struct cpu_hw_events *cpuhw;
	unsigned long flags;

	if (!ppmu)
		return;
	local_irq_save(flags);
	cpuhw = &__get_cpu_var(cpu_hw_events);

	if (!cpuhw->disabled) {
		cpuhw->disabled = 1;
		cpuhw->n_added = 0;

		/*
		 * Check if we ever enabled the PMU on this cpu.
		 */
		if (!cpuhw->pmcs_enabled) {
			ppc_enable_pmcs();
			cpuhw->pmcs_enabled = 1;
		}

		/*
		 * Disable instruction sampling if it was enabled
		 */
		if (cpuhw->mmcr[2] & MMCRA_SAMPLE_ENABLE) {
			mtspr(SPRN_MMCRA,
			      cpuhw->mmcr[2] & ~MMCRA_SAMPLE_ENABLE);
			mb();
		}

		/*
		 * Set the 'freeze counters' bit.
		 * The barrier is to make sure the mtspr has been
		 * executed and the PMU has frozen the events
		 * before we return.
		 */
		write_mmcr0(cpuhw, mfspr(SPRN_MMCR0) | MMCR0_FC);
		mb();
	}
	local_irq_restore(flags);
}

/*
 * Re-enable all events if disable == 0.
 * If we were previously disabled and events were added, then
 * put the new config on the PMU.
 */
void hw_perf_enable(void)
{
	struct perf_event *event;
	struct cpu_hw_events *cpuhw;
	unsigned long flags;
	long i;
	unsigned long val;
	s64 left;
	unsigned int hwc_index[MAX_HWEVENTS];
	int n_lim;
	int idx;

	if (!ppmu)
		return;
	local_irq_save(flags);
	cpuhw = &__get_cpu_var(cpu_hw_events);
	if (!cpuhw->disabled) {
		local_irq_restore(flags);
		return;
	}
	cpuhw->disabled = 0;

	/*
	 * If we didn't change anything, or only removed events,
	 * no need to recalculate MMCR* settings and reset the PMCs.
	 * Just reenable the PMU with the current MMCR* settings
	 * (possibly updated for removal of events).
	 */
	if (!cpuhw->n_added) {
		mtspr(SPRN_MMCRA, cpuhw->mmcr[2] & ~MMCRA_SAMPLE_ENABLE);
		mtspr(SPRN_MMCR1, cpuhw->mmcr[1]);
		if (cpuhw->n_events == 0)
			ppc_set_pmu_inuse(0);
		goto out_enable;
	}

	/*
	 * Compute MMCR* values for the new set of events
	 */
	if (ppmu->compute_mmcr(cpuhw->events, cpuhw->n_events, hwc_index,
			       cpuhw->mmcr)) {
		/* shouldn't ever get here */
		printk(KERN_ERR "oops compute_mmcr failed\n");
		goto out;
	}

	/*
	 * Add in MMCR0 freeze bits corresponding to the
	 * attr.exclude_* bits for the first event.
	 * We have already checked that all events have the
	 * same values for these bits as the first event.
	 */
	event = cpuhw->event[0];
	if (event->attr.exclude_user)
		cpuhw->mmcr[0] |= MMCR0_FCP;
	if (event->attr.exclude_kernel)
		cpuhw->mmcr[0] |= freeze_events_kernel;
	if (event->attr.exclude_hv)
		cpuhw->mmcr[0] |= MMCR0_FCHV;

	/*
	 * Write the new configuration to MMCR* with the freeze
	 * bit set and set the hardware events to their initial values.
	 * Then unfreeze the events.
	 */
	ppc_set_pmu_inuse(1);
	mtspr(SPRN_MMCRA, cpuhw->mmcr[2] & ~MMCRA_SAMPLE_ENABLE);
	mtspr(SPRN_MMCR1, cpuhw->mmcr[1]);
	mtspr(SPRN_MMCR0, (cpuhw->mmcr[0] & ~(MMCR0_PMC1CE | MMCR0_PMCjCE))
				| MMCR0_FC);

	/*
	 * Read off any pre-existing events that need to move
	 * to another PMC.
	 */
	for (i = 0; i < cpuhw->n_events; ++i) {
		event = cpuhw->event[i];
		if (event->hw.idx && event->hw.idx != hwc_index[i] + 1) {
			power_pmu_read(event);
			write_pmc(event->hw.idx, 0);
			event->hw.idx = 0;
		}
	}

	/*
	 * Initialize the PMCs for all the new and moved events.
	 */
	cpuhw->n_limited = n_lim = 0;
	for (i = 0; i < cpuhw->n_events; ++i) {
		event = cpuhw->event[i];
		if (event->hw.idx)
			continue;
		idx = hwc_index[i] + 1;
		if (is_limited_pmc(idx)) {
			cpuhw->limited_counter[n_lim] = event;
			cpuhw->limited_hwidx[n_lim] = idx;
			++n_lim;
			continue;
		}
		val = 0;
		if (event->hw.sample_period) {
			left = atomic64_read(&event->hw.period_left);
			if (left < 0x80000000L)
				val = 0x80000000L - left;
		}
		atomic64_set(&event->hw.prev_count, val);
		event->hw.idx = idx;
		write_pmc(idx, val);
		perf_event_update_userpage(event);
	}
	cpuhw->n_limited = n_lim;
	cpuhw->mmcr[0] |= MMCR0_PMXE | MMCR0_FCECE;

 out_enable:
	mb();
	write_mmcr0(cpuhw, cpuhw->mmcr[0]);

	/*
	 * Enable instruction sampling if necessary
	 */
	if (cpuhw->mmcr[2] & MMCRA_SAMPLE_ENABLE) {
		mb();
		mtspr(SPRN_MMCRA, cpuhw->mmcr[2]);
	}

 out:
	local_irq_restore(flags);
}

static int collect_events(struct perf_event *group, int max_count,
			  struct perf_event *ctrs[], u64 *events,
			  unsigned int *flags)
{
	int n = 0;
	struct perf_event *event;

	if (!is_software_event(group)) {
		if (n >= max_count)
			return -1;
		ctrs[n] = group;
		flags[n] = group->hw.event_base;
		events[n++] = group->hw.config;
	}
	list_for_each_entry(event, &group->sibling_list, group_entry) {
		if (!is_software_event(event) &&
		    event->state != PERF_EVENT_STATE_OFF) {
			if (n >= max_count)
				return -1;
			ctrs[n] = event;
			flags[n] = event->hw.event_base;
			events[n++] = event->hw.config;
		}
	}
	return n;
}

static void event_sched_in(struct perf_event *event)
{
	event->state = PERF_EVENT_STATE_ACTIVE;
	event->oncpu = smp_processor_id();
	event->tstamp_running += event->ctx->time - event->tstamp_stopped;
	if (is_software_event(event))
		event->pmu->enable(event);
}

/*
 * Called to enable a whole group of events.
 * Returns 1 if the group was enabled, or -EAGAIN if it could not be.
 * Assumes the caller has disabled interrupts and has
 * frozen the PMU with hw_perf_save_disable.
 */
int hw_perf_group_sched_in(struct perf_event *group_leader,
	       struct perf_cpu_context *cpuctx,
	       struct perf_event_context *ctx)
{
	struct cpu_hw_events *cpuhw;
	long i, n, n0;
	struct perf_event *sub;

	if (!ppmu)
		return 0;
	cpuhw = &__get_cpu_var(cpu_hw_events);
	n0 = cpuhw->n_events;
	n = collect_events(group_leader, ppmu->n_counter - n0,
			   &cpuhw->event[n0], &cpuhw->events[n0],
			   &cpuhw->flags[n0]);
	if (n < 0)
		return -EAGAIN;
	if (check_excludes(cpuhw->event, cpuhw->flags, n0, n))
		return -EAGAIN;
	i = power_check_constraints(cpuhw, cpuhw->events, cpuhw->flags, n + n0);
	if (i < 0)
		return -EAGAIN;
	cpuhw->n_events = n0 + n;
	cpuhw->n_added += n;

	/*
	 * OK, this group can go on; update event states etc.,
	 * and enable any software events
	 */
	for (i = n0; i < n0 + n; ++i)
		cpuhw->event[i]->hw.config = cpuhw->events[i];
	cpuctx->active_oncpu += n;
	n = 1;
	event_sched_in(group_leader);
	list_for_each_entry(sub, &group_leader->sibling_list, group_entry) {
		if (sub->state != PERF_EVENT_STATE_OFF) {
			event_sched_in(sub);
			++n;
		}
	}
	ctx->nr_active += n;

	return 1;
}

/*
 * Add a event to the PMU.
 * If all events are not already frozen, then we disable and
 * re-enable the PMU in order to get hw_perf_enable to do the
 * actual work of reconfiguring the PMU.
 */
static int power_pmu_enable(struct perf_event *event)
{
	struct cpu_hw_events *cpuhw;
	unsigned long flags;
	int n0;
	int ret = -EAGAIN;

	local_irq_save(flags);
	perf_disable();

	/*
	 * Add the event to the list (if there is room)
	 * and check whether the total set is still feasible.
	 */
	cpuhw = &__get_cpu_var(cpu_hw_events);
	n0 = cpuhw->n_events;
	if (n0 >= ppmu->n_counter)
		goto out;
	cpuhw->event[n0] = event;
	cpuhw->events[n0] = event->hw.config;
	cpuhw->flags[n0] = event->hw.event_base;
	if (check_excludes(cpuhw->event, cpuhw->flags, n0, 1))
		goto out;
	if (power_check_constraints(cpuhw, cpuhw->events, cpuhw->flags, n0 + 1))
		goto out;

	event->hw.config = cpuhw->events[n0];
	++cpuhw->n_events;
	++cpuhw->n_added;

	ret = 0;
 out:
	perf_enable();
	local_irq_restore(flags);
	return ret;
}

/*
 * Remove a event from the PMU.
 */
static void power_pmu_disable(struct perf_event *event)
{
	struct cpu_hw_events *cpuhw;
	long i;
	unsigned long flags;

	local_irq_save(flags);
	perf_disable();

	power_pmu_read(event);

	cpuhw = &__get_cpu_var(cpu_hw_events);
	for (i = 0; i < cpuhw->n_events; ++i) {
		if (event == cpuhw->event[i]) {
			while (++i < cpuhw->n_events)
				cpuhw->event[i-1] = cpuhw->event[i];
			--cpuhw->n_events;
			ppmu->disable_pmc(event->hw.idx - 1, cpuhw->mmcr);
			if (event->hw.idx) {
				write_pmc(event->hw.idx, 0);
				event->hw.idx = 0;
			}
			perf_event_update_userpage(event);
			break;
		}
	}
	for (i = 0; i < cpuhw->n_limited; ++i)
		if (event == cpuhw->limited_counter[i])
			break;
	if (i < cpuhw->n_limited) {
		while (++i < cpuhw->n_limited) {
			cpuhw->limited_counter[i-1] = cpuhw->limited_counter[i];
			cpuhw->limited_hwidx[i-1] = cpuhw->limited_hwidx[i];
		}
		--cpuhw->n_limited;
	}
	if (cpuhw->n_events == 0) {
		/* disable exceptions if no events are running */
		cpuhw->mmcr[0] &= ~(MMCR0_PMXE | MMCR0_FCECE);
	}

	perf_enable();
	local_irq_restore(flags);
}

/*
 * Re-enable interrupts on a event after they were throttled
 * because they were coming too fast.
 */
static void power_pmu_unthrottle(struct perf_event *event)
{
	s64 val, left;
	unsigned long flags;

	if (!event->hw.idx || !event->hw.sample_period)
		return;
	local_irq_save(flags);
	perf_disable();
	power_pmu_read(event);
	left = event->hw.sample_period;
	event->hw.last_period = left;
	val = 0;
	if (left < 0x80000000L)
		val = 0x80000000L - left;
	write_pmc(event->hw.idx, val);
	atomic64_set(&event->hw.prev_count, val);
	atomic64_set(&event->hw.period_left, left);
	perf_event_update_userpage(event);
	perf_enable();
	local_irq_restore(flags);
}

struct pmu power_pmu = {
	.enable		= power_pmu_enable,
	.disable	= power_pmu_disable,
	.read		= power_pmu_read,
	.unthrottle	= power_pmu_unthrottle,
};

/*
 * Return 1 if we might be able to put event on a limited PMC,
 * or 0 if not.
 * A event can only go on a limited PMC if it counts something
 * that a limited PMC can count, doesn't require interrupts, and
 * doesn't exclude any processor mode.
 */
static int can_go_on_limited_pmc(struct perf_event *event, u64 ev,
				 unsigned int flags)
{
	int n;
	u64 alt[MAX_EVENT_ALTERNATIVES];

	if (event->attr.exclude_user
	    || event->attr.exclude_kernel
	    || event->attr.exclude_hv
	    || event->attr.sample_period)
		return 0;

	if (ppmu->limited_pmc_event(ev))
		return 1;

	/*
	 * The requested event_id isn't on a limited PMC already;
	 * see if any alternative code goes on a limited PMC.
	 */
	if (!ppmu->get_alternatives)
		return 0;

	flags |= PPMU_LIMITED_PMC_OK | PPMU_LIMITED_PMC_REQD;
	n = ppmu->get_alternatives(ev, flags, alt);

	return n > 0;
}

/*
 * Find an alternative event_id that goes on a normal PMC, if possible,
 * and return the event_id code, or 0 if there is no such alternative.
 * (Note: event_id code 0 is "don't count" on all machines.)
 */
static u64 normal_pmc_alternative(u64 ev, unsigned long flags)
{
	u64 alt[MAX_EVENT_ALTERNATIVES];
	int n;

	flags &= ~(PPMU_LIMITED_PMC_OK | PPMU_LIMITED_PMC_REQD);
	n = ppmu->get_alternatives(ev, flags, alt);
	if (!n)
		return 0;
	return alt[0];
}

/* Number of perf_events counting hardware events */
static atomic_t num_events;
/* Used to avoid races in calling reserve/release_pmc_hardware */
static DEFINE_MUTEX(pmc_reserve_mutex);

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

const struct pmu *hw_perf_event_init(struct perf_event *event)
{
	u64 ev;
	unsigned long flags;
	struct perf_event *ctrs[MAX_HWEVENTS];
	u64 events[MAX_HWEVENTS];
	unsigned int cflags[MAX_HWEVENTS];
	int n;
	int err;
	struct cpu_hw_events *cpuhw;

	if (!ppmu)
		return ERR_PTR(-ENXIO);
	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		ev = event->attr.config;
		if (ev >= ppmu->n_generic || ppmu->generic_events[ev] == 0)
			return ERR_PTR(-EOPNOTSUPP);
		ev = ppmu->generic_events[ev];
		break;
	case PERF_TYPE_HW_CACHE:
		err = hw_perf_cache_event(event->attr.config, &ev);
		if (err)
			return ERR_PTR(err);
		break;
	case PERF_TYPE_RAW:
		ev = event->attr.config;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}
	event->hw.config_base = ev;
	event->hw.idx = 0;

	/*
	 * If we are not running on a hypervisor, force the
	 * exclude_hv bit to 0 so that we don't care what
	 * the user set it to.
	 */
	if (!firmware_has_feature(FW_FEATURE_LPAR))
		event->attr.exclude_hv = 0;

	/*
	 * If this is a per-task event, then we can use
	 * PM_RUN_* events interchangeably with their non RUN_*
	 * equivalents, e.g. PM_RUN_CYC instead of PM_CYC.
	 * XXX we should check if the task is an idle task.
	 */
	flags = 0;
	if (event->ctx->task)
		flags |= PPMU_ONLY_COUNT_RUN;

	/*
	 * If this machine has limited events, check whether this
	 * event_id could go on a limited event.
	 */
	if (ppmu->flags & PPMU_LIMITED_PMC5_6) {
		if (can_go_on_limited_pmc(event, ev, flags)) {
			flags |= PPMU_LIMITED_PMC_OK;
		} else if (ppmu->limited_pmc_event(ev)) {
			/*
			 * The requested event_id is on a limited PMC,
			 * but we can't use a limited PMC; see if any
			 * alternative goes on a normal PMC.
			 */
			ev = normal_pmc_alternative(ev, flags);
			if (!ev)
				return ERR_PTR(-EINVAL);
		}
	}

	/*
	 * If this is in a group, check if it can go on with all the
	 * other hardware events in the group.  We assume the event
	 * hasn't been linked into its leader's sibling list at this point.
	 */
	n = 0;
	if (event->group_leader != event) {
		n = collect_events(event->group_leader, ppmu->n_counter - 1,
				   ctrs, events, cflags);
		if (n < 0)
			return ERR_PTR(-EINVAL);
	}
	events[n] = ev;
	ctrs[n] = event;
	cflags[n] = flags;
	if (check_excludes(ctrs, cflags, n, 1))
		return ERR_PTR(-EINVAL);

	cpuhw = &get_cpu_var(cpu_hw_events);
	err = power_check_constraints(cpuhw, events, cflags, n + 1);
	put_cpu_var(cpu_hw_events);
	if (err)
		return ERR_PTR(-EINVAL);

	event->hw.config = events[n];
	event->hw.event_base = cflags[n];
	event->hw.last_period = event->hw.sample_period;
	atomic64_set(&event->hw.period_left, event->hw.last_period);

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
	}
	event->destroy = hw_perf_event_destroy;

	if (err)
		return ERR_PTR(err);
	return &power_pmu;
}

/*
 * A counter has overflowed; update its count and record
 * things if requested.  Note that interrupts are hard-disabled
 * here so there is no possibility of being interrupted.
 */
static void record_and_restart(struct perf_event *event, unsigned long val,
			       struct pt_regs *regs, int nmi)
{
	u64 period = event->hw.sample_period;
	s64 prev, delta, left;
	int record = 0;

	/* we don't have to worry about interrupts here */
	prev = atomic64_read(&event->hw.prev_count);
	delta = (val - prev) & 0xfffffffful;
	atomic64_add(delta, &event->count);

	/*
	 * See if the total period for this event has expired,
	 * and update for the next period.
	 */
	val = 0;
	left = atomic64_read(&event->hw.period_left) - delta;
	if (period) {
		if (left <= 0) {
			left += period;
			if (left <= 0)
				left = period;
			record = 1;
		}
		if (left < 0x80000000LL)
			val = 0x80000000LL - left;
	}

	/*
	 * Finally record data if requested.
	 */
	if (record) {
		struct perf_sample_data data = {
			.addr	= ~0ULL,
			.period	= event->hw.last_period,
		};

		if (event->attr.sample_type & PERF_SAMPLE_ADDR)
			perf_get_data_addr(regs, &data.addr);

		if (perf_event_overflow(event, nmi, &data, regs)) {
			/*
			 * Interrupts are coming too fast - throttle them
			 * by setting the event to 0, so it will be
			 * at least 2^30 cycles until the next interrupt
			 * (assuming each event counts at most 2 counts
			 * per cycle).
			 */
			val = 0;
			left = ~0ULL >> 1;
		}
	}

	write_pmc(event->hw.idx, val);
	atomic64_set(&event->hw.prev_count, val);
	atomic64_set(&event->hw.period_left, left);
	perf_event_update_userpage(event);
}

/*
 * Called from generic code to get the misc flags (i.e. processor mode)
 * for an event_id.
 */
unsigned long perf_misc_flags(struct pt_regs *regs)
{
	u32 flags = perf_get_misc_flags(regs);

	if (flags)
		return flags;
	return user_mode(regs) ? PERF_RECORD_MISC_USER :
		PERF_RECORD_MISC_KERNEL;
}

/*
 * Called from generic code to get the instruction pointer
 * for an event_id.
 */
unsigned long perf_instruction_pointer(struct pt_regs *regs)
{
	unsigned long ip;

	if (TRAP(regs) != 0xf00)
		return regs->nip;	/* not a PMU interrupt */

	ip = mfspr(SPRN_SIAR) + perf_ip_adjust(regs);
	return ip;
}

/*
 * Performance monitor interrupt stuff
 */
static void perf_event_interrupt(struct pt_regs *regs)
{
	int i;
	struct cpu_hw_events *cpuhw = &__get_cpu_var(cpu_hw_events);
	struct perf_event *event;
	unsigned long val;
	int found = 0;
	int nmi;

	if (cpuhw->n_limited)
		freeze_limited_counters(cpuhw, mfspr(SPRN_PMC5),
					mfspr(SPRN_PMC6));

	perf_read_regs(regs);

	nmi = perf_intr_is_nmi(regs);
	if (nmi)
		nmi_enter();
	else
		irq_enter();

	for (i = 0; i < cpuhw->n_events; ++i) {
		event = cpuhw->event[i];
		if (!event->hw.idx || is_limited_pmc(event->hw.idx))
			continue;
		val = read_pmc(event->hw.idx);
		if ((int)val < 0) {
			/* event has overflowed */
			found = 1;
			record_and_restart(event, val, regs, nmi);
		}
	}

	/*
	 * In case we didn't find and reset the event that caused
	 * the interrupt, scan all events and reset any that are
	 * negative, to avoid getting continual interrupts.
	 * Any that we processed in the previous loop will not be negative.
	 */
	if (!found) {
		for (i = 0; i < ppmu->n_counter; ++i) {
			if (is_limited_pmc(i + 1))
				continue;
			val = read_pmc(i + 1);
			if ((int)val < 0)
				write_pmc(i + 1, 0);
		}
	}

	/*
	 * Reset MMCR0 to its normal value.  This will set PMXE and
	 * clear FC (freeze counters) and PMAO (perf mon alert occurred)
	 * and thus allow interrupts to occur again.
	 * XXX might want to use MSR.PM to keep the events frozen until
	 * we get back out of this interrupt.
	 */
	write_mmcr0(cpuhw, cpuhw->mmcr[0]);

	if (nmi)
		nmi_exit();
	else
		irq_exit();
}

void hw_perf_event_setup(int cpu)
{
	struct cpu_hw_events *cpuhw = &per_cpu(cpu_hw_events, cpu);

	if (!ppmu)
		return;
	memset(cpuhw, 0, sizeof(*cpuhw));
	cpuhw->mmcr[0] = MMCR0_FC;
}

int register_power_pmu(struct power_pmu *pmu)
{
	if (ppmu)
		return -EBUSY;		/* something's already registered */

	ppmu = pmu;
	pr_info("%s performance monitor hardware support registered\n",
		pmu->name);

#ifdef MSR_HV
	/*
	 * Use FCHV to ignore kernel events if MSR.HV is set.
	 */
	if (mfmsr() & MSR_HV)
		freeze_events_kernel = MMCR0_FCHV;
#endif /* CONFIG_PPC64 */

	return 0;
}
