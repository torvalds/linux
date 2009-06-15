/*
 * Performance counter support - powerpc architecture code
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
#include <linux/perf_counter.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <asm/reg.h>
#include <asm/pmc.h>
#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/ptrace.h>

struct cpu_hw_counters {
	int n_counters;
	int n_percpu;
	int disabled;
	int n_added;
	int n_limited;
	u8  pmcs_enabled;
	struct perf_counter *counter[MAX_HWCOUNTERS];
	u64 events[MAX_HWCOUNTERS];
	unsigned int flags[MAX_HWCOUNTERS];
	u64 mmcr[3];
	struct perf_counter *limited_counter[MAX_LIMITED_HWCOUNTERS];
	u8  limited_hwidx[MAX_LIMITED_HWCOUNTERS];
};
DEFINE_PER_CPU(struct cpu_hw_counters, cpu_hw_counters);

struct power_pmu *ppmu;

/*
 * Normally, to ignore kernel events we set the FCS (freeze counters
 * in supervisor mode) bit in MMCR0, but if the kernel runs with the
 * hypervisor bit set in the MSR, or if we are running on a processor
 * where the hypervisor bit is forced to 1 (as on Apple G5 processors),
 * then we need to use the FCHV bit to ignore kernel events.
 */
static unsigned int freeze_counters_kernel = MMCR0_FCS;

static void perf_counter_interrupt(struct pt_regs *regs);

void perf_counter_print_debug(void)
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
	case 7:
		val = mfspr(SPRN_PMC7);
		break;
	case 8:
		val = mfspr(SPRN_PMC8);
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
	case 7:
		mtspr(SPRN_PMC7, val);
		break;
	case 8:
		mtspr(SPRN_PMC8, val);
		break;
	default:
		printk(KERN_ERR "oops trying to write PMC%d\n", idx);
	}
}

/*
 * Check if a set of events can all go on the PMU at once.
 * If they can't, this will look at alternative codes for the events
 * and see if any combination of alternative codes is feasible.
 * The feasible set is returned in event[].
 */
static int power_check_constraints(u64 event[], unsigned int cflags[],
				   int n_ev)
{
	u64 mask, value, nv;
	u64 alternatives[MAX_HWCOUNTERS][MAX_EVENT_ALTERNATIVES];
	u64 amasks[MAX_HWCOUNTERS][MAX_EVENT_ALTERNATIVES];
	u64 avalues[MAX_HWCOUNTERS][MAX_EVENT_ALTERNATIVES];
	u64 smasks[MAX_HWCOUNTERS], svalues[MAX_HWCOUNTERS];
	int n_alt[MAX_HWCOUNTERS], choice[MAX_HWCOUNTERS];
	int i, j;
	u64 addf = ppmu->add_fields;
	u64 tadd = ppmu->test_adder;

	if (n_ev > ppmu->n_counter)
		return -1;

	/* First see if the events will go on as-is */
	for (i = 0; i < n_ev; ++i) {
		if ((cflags[i] & PPMU_LIMITED_PMC_REQD)
		    && !ppmu->limited_pmc_event(event[i])) {
			ppmu->get_alternatives(event[i], cflags[i],
					       alternatives[i]);
			event[i] = alternatives[i][0];
		}
		if (ppmu->get_constraint(event[i], &amasks[i][0],
					 &avalues[i][0]))
			return -1;
	}
	value = mask = 0;
	for (i = 0; i < n_ev; ++i) {
		nv = (value | avalues[i][0]) + (value & avalues[i][0] & addf);
		if ((((nv + tadd) ^ value) & mask) != 0 ||
		    (((nv + tadd) ^ avalues[i][0]) & amasks[i][0]) != 0)
			break;
		value = nv;
		mask |= amasks[i][0];
	}
	if (i == n_ev)
		return 0;	/* all OK */

	/* doesn't work, gather alternatives... */
	if (!ppmu->get_alternatives)
		return -1;
	for (i = 0; i < n_ev; ++i) {
		choice[i] = 0;
		n_alt[i] = ppmu->get_alternatives(event[i], cflags[i],
						  alternatives[i]);
		for (j = 1; j < n_alt[i]; ++j)
			ppmu->get_constraint(alternatives[i][j],
					     &amasks[i][j], &avalues[i][j]);
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
		 * See if any alternative k for event i,
		 * where k > j, will satisfy the constraints.
		 */
		while (++j < n_alt[i]) {
			nv = (value | avalues[i][j]) +
				(value & avalues[i][j] & addf);
			if ((((nv + tadd) ^ value) & mask) == 0 &&
			    (((nv + tadd) ^ avalues[i][j])
			     & amasks[i][j]) == 0)
				break;
		}
		if (j >= n_alt[i]) {
			/*
			 * No feasible alternative, backtrack
			 * to event i-1 and continue enumerating its
			 * alternatives from where we got up to.
			 */
			if (--i < 0)
				return -1;
		} else {
			/*
			 * Found a feasible alternative for event i,
			 * remember where we got up to with this event,
			 * go on to the next event, and start with
			 * the first alternative for it.
			 */
			choice[i] = j;
			svalues[i] = value;
			smasks[i] = mask;
			value = nv;
			mask |= amasks[i][j];
			++i;
			j = -1;
		}
	}

	/* OK, we have a feasible combination, tell the caller the solution */
	for (i = 0; i < n_ev; ++i)
		event[i] = alternatives[i][choice[i]];
	return 0;
}

/*
 * Check if newly-added counters have consistent settings for
 * exclude_{user,kernel,hv} with each other and any previously
 * added counters.
 */
static int check_excludes(struct perf_counter **ctrs, unsigned int cflags[],
			  int n_prev, int n_new)
{
	int eu = 0, ek = 0, eh = 0;
	int i, n, first;
	struct perf_counter *counter;

	n = n_prev + n_new;
	if (n <= 1)
		return 0;

	first = 1;
	for (i = 0; i < n; ++i) {
		if (cflags[i] & PPMU_LIMITED_PMC_OK) {
			cflags[i] &= ~PPMU_LIMITED_PMC_REQD;
			continue;
		}
		counter = ctrs[i];
		if (first) {
			eu = counter->attr.exclude_user;
			ek = counter->attr.exclude_kernel;
			eh = counter->attr.exclude_hv;
			first = 0;
		} else if (counter->attr.exclude_user != eu ||
			   counter->attr.exclude_kernel != ek ||
			   counter->attr.exclude_hv != eh) {
			return -EAGAIN;
		}
	}

	if (eu || ek || eh)
		for (i = 0; i < n; ++i)
			if (cflags[i] & PPMU_LIMITED_PMC_OK)
				cflags[i] |= PPMU_LIMITED_PMC_REQD;

	return 0;
}

static void power_pmu_read(struct perf_counter *counter)
{
	long val, delta, prev;

	if (!counter->hw.idx)
		return;
	/*
	 * Performance monitor interrupts come even when interrupts
	 * are soft-disabled, as long as interrupts are hard-enabled.
	 * Therefore we treat them like NMIs.
	 */
	do {
		prev = atomic64_read(&counter->hw.prev_count);
		barrier();
		val = read_pmc(counter->hw.idx);
	} while (atomic64_cmpxchg(&counter->hw.prev_count, prev, val) != prev);

	/* The counters are only 32 bits wide */
	delta = (val - prev) & 0xfffffffful;
	atomic64_add(delta, &counter->count);
	atomic64_sub(delta, &counter->hw.period_left);
}

/*
 * On some machines, PMC5 and PMC6 can't be written, don't respect
 * the freeze conditions, and don't generate interrupts.  This tells
 * us if `counter' is using such a PMC.
 */
static int is_limited_pmc(int pmcnum)
{
	return (ppmu->flags & PPMU_LIMITED_PMC5_6)
		&& (pmcnum == 5 || pmcnum == 6);
}

static void freeze_limited_counters(struct cpu_hw_counters *cpuhw,
				    unsigned long pmc5, unsigned long pmc6)
{
	struct perf_counter *counter;
	u64 val, prev, delta;
	int i;

	for (i = 0; i < cpuhw->n_limited; ++i) {
		counter = cpuhw->limited_counter[i];
		if (!counter->hw.idx)
			continue;
		val = (counter->hw.idx == 5) ? pmc5 : pmc6;
		prev = atomic64_read(&counter->hw.prev_count);
		counter->hw.idx = 0;
		delta = (val - prev) & 0xfffffffful;
		atomic64_add(delta, &counter->count);
	}
}

static void thaw_limited_counters(struct cpu_hw_counters *cpuhw,
				  unsigned long pmc5, unsigned long pmc6)
{
	struct perf_counter *counter;
	u64 val;
	int i;

	for (i = 0; i < cpuhw->n_limited; ++i) {
		counter = cpuhw->limited_counter[i];
		counter->hw.idx = cpuhw->limited_hwidx[i];
		val = (counter->hw.idx == 5) ? pmc5 : pmc6;
		atomic64_set(&counter->hw.prev_count, val);
		perf_counter_update_userpage(counter);
	}
}

/*
 * Since limited counters don't respect the freeze conditions, we
 * have to read them immediately after freezing or unfreezing the
 * other counters.  We try to keep the values from the limited
 * counters as consistent as possible by keeping the delay (in
 * cycles and instructions) between freezing/unfreezing and reading
 * the limited counters as small and consistent as possible.
 * Therefore, if any limited counters are in use, we read them
 * both, and always in the same order, to minimize variability,
 * and do it inside the same asm that writes MMCR0.
 */
static void write_mmcr0(struct cpu_hw_counters *cpuhw, unsigned long mmcr0)
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
	 * counters, we first write MMCR0 with the counter overflow
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
	 * Write the full MMCR0 including the counter overflow interrupt
	 * enable bits, if necessary.
	 */
	if (mmcr0 & (MMCR0_PMC1CE | MMCR0_PMCjCE))
		mtspr(SPRN_MMCR0, mmcr0);
}

/*
 * Disable all counters to prevent PMU interrupts and to allow
 * counters to be added or removed.
 */
void hw_perf_disable(void)
{
	struct cpu_hw_counters *cpuhw;
	unsigned long ret;
	unsigned long flags;

	local_irq_save(flags);
	cpuhw = &__get_cpu_var(cpu_hw_counters);

	ret = cpuhw->disabled;
	if (!ret) {
		cpuhw->disabled = 1;
		cpuhw->n_added = 0;

		/*
		 * Check if we ever enabled the PMU on this cpu.
		 */
		if (!cpuhw->pmcs_enabled) {
			if (ppc_md.enable_pmcs)
				ppc_md.enable_pmcs();
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
		 * executed and the PMU has frozen the counters
		 * before we return.
		 */
		write_mmcr0(cpuhw, mfspr(SPRN_MMCR0) | MMCR0_FC);
		mb();
	}
	local_irq_restore(flags);
}

/*
 * Re-enable all counters if disable == 0.
 * If we were previously disabled and counters were added, then
 * put the new config on the PMU.
 */
void hw_perf_enable(void)
{
	struct perf_counter *counter;
	struct cpu_hw_counters *cpuhw;
	unsigned long flags;
	long i;
	unsigned long val;
	s64 left;
	unsigned int hwc_index[MAX_HWCOUNTERS];
	int n_lim;
	int idx;

	local_irq_save(flags);
	cpuhw = &__get_cpu_var(cpu_hw_counters);
	if (!cpuhw->disabled) {
		local_irq_restore(flags);
		return;
	}
	cpuhw->disabled = 0;

	/*
	 * If we didn't change anything, or only removed counters,
	 * no need to recalculate MMCR* settings and reset the PMCs.
	 * Just reenable the PMU with the current MMCR* settings
	 * (possibly updated for removal of counters).
	 */
	if (!cpuhw->n_added) {
		mtspr(SPRN_MMCRA, cpuhw->mmcr[2] & ~MMCRA_SAMPLE_ENABLE);
		mtspr(SPRN_MMCR1, cpuhw->mmcr[1]);
		if (cpuhw->n_counters == 0)
			get_lppaca()->pmcregs_in_use = 0;
		goto out_enable;
	}

	/*
	 * Compute MMCR* values for the new set of counters
	 */
	if (ppmu->compute_mmcr(cpuhw->events, cpuhw->n_counters, hwc_index,
			       cpuhw->mmcr)) {
		/* shouldn't ever get here */
		printk(KERN_ERR "oops compute_mmcr failed\n");
		goto out;
	}

	/*
	 * Add in MMCR0 freeze bits corresponding to the
	 * attr.exclude_* bits for the first counter.
	 * We have already checked that all counters have the
	 * same values for these bits as the first counter.
	 */
	counter = cpuhw->counter[0];
	if (counter->attr.exclude_user)
		cpuhw->mmcr[0] |= MMCR0_FCP;
	if (counter->attr.exclude_kernel)
		cpuhw->mmcr[0] |= freeze_counters_kernel;
	if (counter->attr.exclude_hv)
		cpuhw->mmcr[0] |= MMCR0_FCHV;

	/*
	 * Write the new configuration to MMCR* with the freeze
	 * bit set and set the hardware counters to their initial values.
	 * Then unfreeze the counters.
	 */
	get_lppaca()->pmcregs_in_use = 1;
	mtspr(SPRN_MMCRA, cpuhw->mmcr[2] & ~MMCRA_SAMPLE_ENABLE);
	mtspr(SPRN_MMCR1, cpuhw->mmcr[1]);
	mtspr(SPRN_MMCR0, (cpuhw->mmcr[0] & ~(MMCR0_PMC1CE | MMCR0_PMCjCE))
				| MMCR0_FC);

	/*
	 * Read off any pre-existing counters that need to move
	 * to another PMC.
	 */
	for (i = 0; i < cpuhw->n_counters; ++i) {
		counter = cpuhw->counter[i];
		if (counter->hw.idx && counter->hw.idx != hwc_index[i] + 1) {
			power_pmu_read(counter);
			write_pmc(counter->hw.idx, 0);
			counter->hw.idx = 0;
		}
	}

	/*
	 * Initialize the PMCs for all the new and moved counters.
	 */
	cpuhw->n_limited = n_lim = 0;
	for (i = 0; i < cpuhw->n_counters; ++i) {
		counter = cpuhw->counter[i];
		if (counter->hw.idx)
			continue;
		idx = hwc_index[i] + 1;
		if (is_limited_pmc(idx)) {
			cpuhw->limited_counter[n_lim] = counter;
			cpuhw->limited_hwidx[n_lim] = idx;
			++n_lim;
			continue;
		}
		val = 0;
		if (counter->hw.sample_period) {
			left = atomic64_read(&counter->hw.period_left);
			if (left < 0x80000000L)
				val = 0x80000000L - left;
		}
		atomic64_set(&counter->hw.prev_count, val);
		counter->hw.idx = idx;
		write_pmc(idx, val);
		perf_counter_update_userpage(counter);
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

static int collect_events(struct perf_counter *group, int max_count,
			  struct perf_counter *ctrs[], u64 *events,
			  unsigned int *flags)
{
	int n = 0;
	struct perf_counter *counter;

	if (!is_software_counter(group)) {
		if (n >= max_count)
			return -1;
		ctrs[n] = group;
		flags[n] = group->hw.counter_base;
		events[n++] = group->hw.config;
	}
	list_for_each_entry(counter, &group->sibling_list, list_entry) {
		if (!is_software_counter(counter) &&
		    counter->state != PERF_COUNTER_STATE_OFF) {
			if (n >= max_count)
				return -1;
			ctrs[n] = counter;
			flags[n] = counter->hw.counter_base;
			events[n++] = counter->hw.config;
		}
	}
	return n;
}

static void counter_sched_in(struct perf_counter *counter, int cpu)
{
	counter->state = PERF_COUNTER_STATE_ACTIVE;
	counter->oncpu = cpu;
	counter->tstamp_running += counter->ctx->time - counter->tstamp_stopped;
	if (is_software_counter(counter))
		counter->pmu->enable(counter);
}

/*
 * Called to enable a whole group of counters.
 * Returns 1 if the group was enabled, or -EAGAIN if it could not be.
 * Assumes the caller has disabled interrupts and has
 * frozen the PMU with hw_perf_save_disable.
 */
int hw_perf_group_sched_in(struct perf_counter *group_leader,
	       struct perf_cpu_context *cpuctx,
	       struct perf_counter_context *ctx, int cpu)
{
	struct cpu_hw_counters *cpuhw;
	long i, n, n0;
	struct perf_counter *sub;

	cpuhw = &__get_cpu_var(cpu_hw_counters);
	n0 = cpuhw->n_counters;
	n = collect_events(group_leader, ppmu->n_counter - n0,
			   &cpuhw->counter[n0], &cpuhw->events[n0],
			   &cpuhw->flags[n0]);
	if (n < 0)
		return -EAGAIN;
	if (check_excludes(cpuhw->counter, cpuhw->flags, n0, n))
		return -EAGAIN;
	i = power_check_constraints(cpuhw->events, cpuhw->flags, n + n0);
	if (i < 0)
		return -EAGAIN;
	cpuhw->n_counters = n0 + n;
	cpuhw->n_added += n;

	/*
	 * OK, this group can go on; update counter states etc.,
	 * and enable any software counters
	 */
	for (i = n0; i < n0 + n; ++i)
		cpuhw->counter[i]->hw.config = cpuhw->events[i];
	cpuctx->active_oncpu += n;
	n = 1;
	counter_sched_in(group_leader, cpu);
	list_for_each_entry(sub, &group_leader->sibling_list, list_entry) {
		if (sub->state != PERF_COUNTER_STATE_OFF) {
			counter_sched_in(sub, cpu);
			++n;
		}
	}
	ctx->nr_active += n;

	return 1;
}

/*
 * Add a counter to the PMU.
 * If all counters are not already frozen, then we disable and
 * re-enable the PMU in order to get hw_perf_enable to do the
 * actual work of reconfiguring the PMU.
 */
static int power_pmu_enable(struct perf_counter *counter)
{
	struct cpu_hw_counters *cpuhw;
	unsigned long flags;
	int n0;
	int ret = -EAGAIN;

	local_irq_save(flags);
	perf_disable();

	/*
	 * Add the counter to the list (if there is room)
	 * and check whether the total set is still feasible.
	 */
	cpuhw = &__get_cpu_var(cpu_hw_counters);
	n0 = cpuhw->n_counters;
	if (n0 >= ppmu->n_counter)
		goto out;
	cpuhw->counter[n0] = counter;
	cpuhw->events[n0] = counter->hw.config;
	cpuhw->flags[n0] = counter->hw.counter_base;
	if (check_excludes(cpuhw->counter, cpuhw->flags, n0, 1))
		goto out;
	if (power_check_constraints(cpuhw->events, cpuhw->flags, n0 + 1))
		goto out;

	counter->hw.config = cpuhw->events[n0];
	++cpuhw->n_counters;
	++cpuhw->n_added;

	ret = 0;
 out:
	perf_enable();
	local_irq_restore(flags);
	return ret;
}

/*
 * Remove a counter from the PMU.
 */
static void power_pmu_disable(struct perf_counter *counter)
{
	struct cpu_hw_counters *cpuhw;
	long i;
	unsigned long flags;

	local_irq_save(flags);
	perf_disable();

	power_pmu_read(counter);

	cpuhw = &__get_cpu_var(cpu_hw_counters);
	for (i = 0; i < cpuhw->n_counters; ++i) {
		if (counter == cpuhw->counter[i]) {
			while (++i < cpuhw->n_counters)
				cpuhw->counter[i-1] = cpuhw->counter[i];
			--cpuhw->n_counters;
			ppmu->disable_pmc(counter->hw.idx - 1, cpuhw->mmcr);
			if (counter->hw.idx) {
				write_pmc(counter->hw.idx, 0);
				counter->hw.idx = 0;
			}
			perf_counter_update_userpage(counter);
			break;
		}
	}
	for (i = 0; i < cpuhw->n_limited; ++i)
		if (counter == cpuhw->limited_counter[i])
			break;
	if (i < cpuhw->n_limited) {
		while (++i < cpuhw->n_limited) {
			cpuhw->limited_counter[i-1] = cpuhw->limited_counter[i];
			cpuhw->limited_hwidx[i-1] = cpuhw->limited_hwidx[i];
		}
		--cpuhw->n_limited;
	}
	if (cpuhw->n_counters == 0) {
		/* disable exceptions if no counters are running */
		cpuhw->mmcr[0] &= ~(MMCR0_PMXE | MMCR0_FCECE);
	}

	perf_enable();
	local_irq_restore(flags);
}

/*
 * Re-enable interrupts on a counter after they were throttled
 * because they were coming too fast.
 */
static void power_pmu_unthrottle(struct perf_counter *counter)
{
	s64 val, left;
	unsigned long flags;

	if (!counter->hw.idx || !counter->hw.sample_period)
		return;
	local_irq_save(flags);
	perf_disable();
	power_pmu_read(counter);
	left = counter->hw.sample_period;
	counter->hw.last_period = left;
	val = 0;
	if (left < 0x80000000L)
		val = 0x80000000L - left;
	write_pmc(counter->hw.idx, val);
	atomic64_set(&counter->hw.prev_count, val);
	atomic64_set(&counter->hw.period_left, left);
	perf_counter_update_userpage(counter);
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
 * Return 1 if we might be able to put counter on a limited PMC,
 * or 0 if not.
 * A counter can only go on a limited PMC if it counts something
 * that a limited PMC can count, doesn't require interrupts, and
 * doesn't exclude any processor mode.
 */
static int can_go_on_limited_pmc(struct perf_counter *counter, u64 ev,
				 unsigned int flags)
{
	int n;
	u64 alt[MAX_EVENT_ALTERNATIVES];

	if (counter->attr.exclude_user
	    || counter->attr.exclude_kernel
	    || counter->attr.exclude_hv
	    || counter->attr.sample_period)
		return 0;

	if (ppmu->limited_pmc_event(ev))
		return 1;

	/*
	 * The requested event isn't on a limited PMC already;
	 * see if any alternative code goes on a limited PMC.
	 */
	if (!ppmu->get_alternatives)
		return 0;

	flags |= PPMU_LIMITED_PMC_OK | PPMU_LIMITED_PMC_REQD;
	n = ppmu->get_alternatives(ev, flags, alt);

	return n > 0;
}

/*
 * Find an alternative event that goes on a normal PMC, if possible,
 * and return the event code, or 0 if there is no such alternative.
 * (Note: event code 0 is "don't count" on all machines.)
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

/* Number of perf_counters counting hardware events */
static atomic_t num_counters;
/* Used to avoid races in calling reserve/release_pmc_hardware */
static DEFINE_MUTEX(pmc_reserve_mutex);

/*
 * Release the PMU if this is the last perf_counter.
 */
static void hw_perf_counter_destroy(struct perf_counter *counter)
{
	if (!atomic_add_unless(&num_counters, -1, 1)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_dec_return(&num_counters) == 0)
			release_pmc_hardware();
		mutex_unlock(&pmc_reserve_mutex);
	}
}

/*
 * Translate a generic cache event config to a raw event code.
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

const struct pmu *hw_perf_counter_init(struct perf_counter *counter)
{
	u64 ev;
	unsigned long flags;
	struct perf_counter *ctrs[MAX_HWCOUNTERS];
	u64 events[MAX_HWCOUNTERS];
	unsigned int cflags[MAX_HWCOUNTERS];
	int n;
	int err;

	if (!ppmu)
		return ERR_PTR(-ENXIO);
	switch (counter->attr.type) {
	case PERF_TYPE_HARDWARE:
		ev = counter->attr.config;
		if (ev >= ppmu->n_generic || ppmu->generic_events[ev] == 0)
			return ERR_PTR(-EOPNOTSUPP);
		ev = ppmu->generic_events[ev];
		break;
	case PERF_TYPE_HW_CACHE:
		err = hw_perf_cache_event(counter->attr.config, &ev);
		if (err)
			return ERR_PTR(err);
		break;
	case PERF_TYPE_RAW:
		ev = counter->attr.config;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}
	counter->hw.config_base = ev;
	counter->hw.idx = 0;

	/*
	 * If we are not running on a hypervisor, force the
	 * exclude_hv bit to 0 so that we don't care what
	 * the user set it to.
	 */
	if (!firmware_has_feature(FW_FEATURE_LPAR))
		counter->attr.exclude_hv = 0;

	/*
	 * If this is a per-task counter, then we can use
	 * PM_RUN_* events interchangeably with their non RUN_*
	 * equivalents, e.g. PM_RUN_CYC instead of PM_CYC.
	 * XXX we should check if the task is an idle task.
	 */
	flags = 0;
	if (counter->ctx->task)
		flags |= PPMU_ONLY_COUNT_RUN;

	/*
	 * If this machine has limited counters, check whether this
	 * event could go on a limited counter.
	 */
	if (ppmu->flags & PPMU_LIMITED_PMC5_6) {
		if (can_go_on_limited_pmc(counter, ev, flags)) {
			flags |= PPMU_LIMITED_PMC_OK;
		} else if (ppmu->limited_pmc_event(ev)) {
			/*
			 * The requested event is on a limited PMC,
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
	 * other hardware counters in the group.  We assume the counter
	 * hasn't been linked into its leader's sibling list at this point.
	 */
	n = 0;
	if (counter->group_leader != counter) {
		n = collect_events(counter->group_leader, ppmu->n_counter - 1,
				   ctrs, events, cflags);
		if (n < 0)
			return ERR_PTR(-EINVAL);
	}
	events[n] = ev;
	ctrs[n] = counter;
	cflags[n] = flags;
	if (check_excludes(ctrs, cflags, n, 1))
		return ERR_PTR(-EINVAL);
	if (power_check_constraints(events, cflags, n + 1))
		return ERR_PTR(-EINVAL);

	counter->hw.config = events[n];
	counter->hw.counter_base = cflags[n];
	counter->hw.last_period = counter->hw.sample_period;
	atomic64_set(&counter->hw.period_left, counter->hw.last_period);

	/*
	 * See if we need to reserve the PMU.
	 * If no counters are currently in use, then we have to take a
	 * mutex to ensure that we don't race with another task doing
	 * reserve_pmc_hardware or release_pmc_hardware.
	 */
	err = 0;
	if (!atomic_inc_not_zero(&num_counters)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&num_counters) == 0 &&
		    reserve_pmc_hardware(perf_counter_interrupt))
			err = -EBUSY;
		else
			atomic_inc(&num_counters);
		mutex_unlock(&pmc_reserve_mutex);
	}
	counter->destroy = hw_perf_counter_destroy;

	if (err)
		return ERR_PTR(err);
	return &power_pmu;
}

/*
 * A counter has overflowed; update its count and record
 * things if requested.  Note that interrupts are hard-disabled
 * here so there is no possibility of being interrupted.
 */
static void record_and_restart(struct perf_counter *counter, long val,
			       struct pt_regs *regs, int nmi)
{
	u64 period = counter->hw.sample_period;
	s64 prev, delta, left;
	int record = 0;
	u64 mmcra, sdsync;

	/* we don't have to worry about interrupts here */
	prev = atomic64_read(&counter->hw.prev_count);
	delta = (val - prev) & 0xfffffffful;
	atomic64_add(delta, &counter->count);

	/*
	 * See if the total period for this counter has expired,
	 * and update for the next period.
	 */
	val = 0;
	left = atomic64_read(&counter->hw.period_left) - delta;
	if (period) {
		if (left <= 0) {
			left += period;
			if (left <= 0)
				left = period;
			record = 1;
		}
		if (left < 0x80000000L)
			val = 0x80000000L - left;
	}

	/*
	 * Finally record data if requested.
	 */
	if (record) {
		struct perf_sample_data data = {
			.regs	= regs,
			.addr	= 0,
			.period	= counter->hw.last_period,
		};

		if (counter->attr.sample_type & PERF_SAMPLE_ADDR) {
			/*
			 * The user wants a data address recorded.
			 * If we're not doing instruction sampling,
			 * give them the SDAR (sampled data address).
			 * If we are doing instruction sampling, then only
			 * give them the SDAR if it corresponds to the
			 * instruction pointed to by SIAR; this is indicated
			 * by the [POWER6_]MMCRA_SDSYNC bit in MMCRA.
			 */
			mmcra = regs->dsisr;
			sdsync = (ppmu->flags & PPMU_ALT_SIPR) ?
				POWER6_MMCRA_SDSYNC : MMCRA_SDSYNC;
			if (!(mmcra & MMCRA_SAMPLE_ENABLE) || (mmcra & sdsync))
				data.addr = mfspr(SPRN_SDAR);
		}
		if (perf_counter_overflow(counter, nmi, &data)) {
			/*
			 * Interrupts are coming too fast - throttle them
			 * by setting the counter to 0, so it will be
			 * at least 2^30 cycles until the next interrupt
			 * (assuming each counter counts at most 2 counts
			 * per cycle).
			 */
			val = 0;
			left = ~0ULL >> 1;
		}
	}

	write_pmc(counter->hw.idx, val);
	atomic64_set(&counter->hw.prev_count, val);
	atomic64_set(&counter->hw.period_left, left);
	perf_counter_update_userpage(counter);
}

/*
 * Called from generic code to get the misc flags (i.e. processor mode)
 * for an event.
 */
unsigned long perf_misc_flags(struct pt_regs *regs)
{
	unsigned long mmcra;

	if (TRAP(regs) != 0xf00) {
		/* not a PMU interrupt */
		return user_mode(regs) ? PERF_EVENT_MISC_USER :
			PERF_EVENT_MISC_KERNEL;
	}

	mmcra = regs->dsisr;
	if (ppmu->flags & PPMU_ALT_SIPR) {
		if (mmcra & POWER6_MMCRA_SIHV)
			return PERF_EVENT_MISC_HYPERVISOR;
		return (mmcra & POWER6_MMCRA_SIPR) ? PERF_EVENT_MISC_USER :
			PERF_EVENT_MISC_KERNEL;
	}
	if (mmcra & MMCRA_SIHV)
		return PERF_EVENT_MISC_HYPERVISOR;
	return (mmcra & MMCRA_SIPR) ? PERF_EVENT_MISC_USER :
			PERF_EVENT_MISC_KERNEL;
}

/*
 * Called from generic code to get the instruction pointer
 * for an event.
 */
unsigned long perf_instruction_pointer(struct pt_regs *regs)
{
	unsigned long mmcra;
	unsigned long ip;
	unsigned long slot;

	if (TRAP(regs) != 0xf00)
		return regs->nip;	/* not a PMU interrupt */

	ip = mfspr(SPRN_SIAR);
	mmcra = regs->dsisr;
	if ((mmcra & MMCRA_SAMPLE_ENABLE) && !(ppmu->flags & PPMU_ALT_SIPR)) {
		slot = (mmcra & MMCRA_SLOT) >> MMCRA_SLOT_SHIFT;
		if (slot > 1)
			ip += 4 * (slot - 1);
	}
	return ip;
}

/*
 * Performance monitor interrupt stuff
 */
static void perf_counter_interrupt(struct pt_regs *regs)
{
	int i;
	struct cpu_hw_counters *cpuhw = &__get_cpu_var(cpu_hw_counters);
	struct perf_counter *counter;
	long val;
	int found = 0;
	int nmi;

	if (cpuhw->n_limited)
		freeze_limited_counters(cpuhw, mfspr(SPRN_PMC5),
					mfspr(SPRN_PMC6));

	/*
	 * Overload regs->dsisr to store MMCRA so we only need to read it once.
	 */
	regs->dsisr = mfspr(SPRN_MMCRA);

	/*
	 * If interrupts were soft-disabled when this PMU interrupt
	 * occurred, treat it as an NMI.
	 */
	nmi = !regs->softe;
	if (nmi)
		nmi_enter();
	else
		irq_enter();

	for (i = 0; i < cpuhw->n_counters; ++i) {
		counter = cpuhw->counter[i];
		if (!counter->hw.idx || is_limited_pmc(counter->hw.idx))
			continue;
		val = read_pmc(counter->hw.idx);
		if ((int)val < 0) {
			/* counter has overflowed */
			found = 1;
			record_and_restart(counter, val, regs, nmi);
		}
	}

	/*
	 * In case we didn't find and reset the counter that caused
	 * the interrupt, scan all counters and reset any that are
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
	 * XXX might want to use MSR.PM to keep the counters frozen until
	 * we get back out of this interrupt.
	 */
	write_mmcr0(cpuhw, cpuhw->mmcr[0]);

	if (nmi)
		nmi_exit();
	else
		irq_exit();
}

void hw_perf_counter_setup(int cpu)
{
	struct cpu_hw_counters *cpuhw = &per_cpu(cpu_hw_counters, cpu);

	memset(cpuhw, 0, sizeof(*cpuhw));
	cpuhw->mmcr[0] = MMCR0_FC;
}

extern struct power_pmu power4_pmu;
extern struct power_pmu ppc970_pmu;
extern struct power_pmu power5_pmu;
extern struct power_pmu power5p_pmu;
extern struct power_pmu power6_pmu;
extern struct power_pmu power7_pmu;

static int init_perf_counters(void)
{
	unsigned long pvr;

	/* XXX should get this from cputable */
	pvr = mfspr(SPRN_PVR);
	switch (PVR_VER(pvr)) {
	case PV_POWER4:
	case PV_POWER4p:
		ppmu = &power4_pmu;
		break;
	case PV_970:
	case PV_970FX:
	case PV_970MP:
		ppmu = &ppc970_pmu;
		break;
	case PV_POWER5:
		ppmu = &power5_pmu;
		break;
	case PV_POWER5p:
		ppmu = &power5p_pmu;
		break;
	case 0x3e:
		ppmu = &power6_pmu;
		break;
	case 0x3f:
		ppmu = &power7_pmu;
		break;
	}

	/*
	 * Use FCHV to ignore kernel events if MSR.HV is set.
	 */
	if (mfmsr() & MSR_HV)
		freeze_counters_kernel = MMCR0_FCHV;

	return 0;
}

arch_initcall(init_perf_counters);
