/*
 * Performance events x86 architecture code
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2009 Jaswinder Singh Rajput
 *  Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 *  Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *  Copyright (C) 2009 Intel Corporation, <markus.t.metzger@intel.com>
 *  Copyright (C) 2009 Google, Inc., Stephane Eranian
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/perf_event.h>
#include <linux/capability.h>
#include <linux/notifier.h>
#include <linux/hardirq.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/kdebug.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/bitops.h>
#include <linux/device.h>

#include <asm/apic.h>
#include <asm/stacktrace.h>
#include <asm/nmi.h>
#include <asm/smp.h>
#include <asm/alternative.h>
#include <asm/timer.h>

#include "perf_event.h"

struct x86_pmu x86_pmu __read_mostly;

DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events) = {
	.enabled = 1,
};

u64 __read_mostly hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX];
u64 __read_mostly hw_cache_extra_regs
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX];

/*
 * Propagate event elapsed time into the generic event.
 * Can only be executed on the CPU where the event is active.
 * Returns the delta events processed.
 */
u64 x86_perf_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int shift = 64 - x86_pmu.cntval_bits;
	u64 prev_raw_count, new_raw_count;
	int idx = hwc->idx;
	s64 delta;

	if (idx == X86_PMC_IDX_FIXED_BTS)
		return 0;

	/*
	 * Careful: an NMI might modify the previous event value.
	 *
	 * Our tactic to handle this is to first atomically read and
	 * exchange a new raw count - then add that new-prev delta
	 * count to the generic event atomically:
	 */
again:
	prev_raw_count = local64_read(&hwc->prev_count);
	rdpmcl(hwc->event_base_rdpmc, new_raw_count);

	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
					new_raw_count) != prev_raw_count)
		goto again;

	/*
	 * Now we have the new raw value and have updated the prev
	 * timestamp already. We can now calculate the elapsed delta
	 * (event-)time and add that to the generic event.
	 *
	 * Careful, not all hw sign-extends above the physical width
	 * of the count.
	 */
	delta = (new_raw_count << shift) - (prev_raw_count << shift);
	delta >>= shift;

	local64_add(delta, &event->count);
	local64_sub(delta, &hwc->period_left);

	return new_raw_count;
}

/*
 * Find and validate any extra registers to set up.
 */
static int x86_pmu_extra_regs(u64 config, struct perf_event *event)
{
	struct hw_perf_event_extra *reg;
	struct extra_reg *er;

	reg = &event->hw.extra_reg;

	if (!x86_pmu.extra_regs)
		return 0;

	for (er = x86_pmu.extra_regs; er->msr; er++) {
		if (er->event != (config & er->config_mask))
			continue;
		if (event->attr.config1 & ~er->valid_mask)
			return -EINVAL;

		reg->idx = er->idx;
		reg->config = event->attr.config1;
		reg->reg = er->msr;
		break;
	}
	return 0;
}

static atomic_t active_events;
static DEFINE_MUTEX(pmc_reserve_mutex);

#ifdef CONFIG_X86_LOCAL_APIC

static bool reserve_pmc_hardware(void)
{
	int i;

	for (i = 0; i < x86_pmu.num_counters; i++) {
		if (!reserve_perfctr_nmi(x86_pmu_event_addr(i)))
			goto perfctr_fail;
	}

	for (i = 0; i < x86_pmu.num_counters; i++) {
		if (!reserve_evntsel_nmi(x86_pmu_config_addr(i)))
			goto eventsel_fail;
	}

	return true;

eventsel_fail:
	for (i--; i >= 0; i--)
		release_evntsel_nmi(x86_pmu_config_addr(i));

	i = x86_pmu.num_counters;

perfctr_fail:
	for (i--; i >= 0; i--)
		release_perfctr_nmi(x86_pmu_event_addr(i));

	return false;
}

static void release_pmc_hardware(void)
{
	int i;

	for (i = 0; i < x86_pmu.num_counters; i++) {
		release_perfctr_nmi(x86_pmu_event_addr(i));
		release_evntsel_nmi(x86_pmu_config_addr(i));
	}
}

#else

static bool reserve_pmc_hardware(void) { return true; }
static void release_pmc_hardware(void) {}

#endif

static bool check_hw_exists(void)
{
	u64 val, val_new = 0;
	int i, reg, ret = 0;

	/*
	 * Check to see if the BIOS enabled any of the counters, if so
	 * complain and bail.
	 */
	for (i = 0; i < x86_pmu.num_counters; i++) {
		reg = x86_pmu_config_addr(i);
		ret = rdmsrl_safe(reg, &val);
		if (ret)
			goto msr_fail;
		if (val & ARCH_PERFMON_EVENTSEL_ENABLE)
			goto bios_fail;
	}

	if (x86_pmu.num_counters_fixed) {
		reg = MSR_ARCH_PERFMON_FIXED_CTR_CTRL;
		ret = rdmsrl_safe(reg, &val);
		if (ret)
			goto msr_fail;
		for (i = 0; i < x86_pmu.num_counters_fixed; i++) {
			if (val & (0x03 << i*4))
				goto bios_fail;
		}
	}

	/*
	 * Now write a value and read it back to see if it matches,
	 * this is needed to detect certain hardware emulators (qemu/kvm)
	 * that don't trap on the MSR access and always return 0s.
	 */
	val = 0xabcdUL;
	ret = wrmsrl_safe(x86_pmu_event_addr(0), val);
	ret |= rdmsrl_safe(x86_pmu_event_addr(0), &val_new);
	if (ret || val != val_new)
		goto msr_fail;

	return true;

bios_fail:
	/*
	 * We still allow the PMU driver to operate:
	 */
	printk(KERN_CONT "Broken BIOS detected, complain to your hardware vendor.\n");
	printk(KERN_ERR FW_BUG "the BIOS has corrupted hw-PMU resources (MSR %x is %Lx)\n", reg, val);

	return true;

msr_fail:
	printk(KERN_CONT "Broken PMU hardware detected, using software events only.\n");

	return false;
}

static void hw_perf_event_destroy(struct perf_event *event)
{
	if (atomic_dec_and_mutex_lock(&active_events, &pmc_reserve_mutex)) {
		release_pmc_hardware();
		release_ds_buffers();
		mutex_unlock(&pmc_reserve_mutex);
	}
}

static inline int x86_pmu_initialized(void)
{
	return x86_pmu.handle_irq != NULL;
}

static inline int
set_ext_hw_attr(struct hw_perf_event *hwc, struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	unsigned int cache_type, cache_op, cache_result;
	u64 config, val;

	config = attr->config;

	cache_type = (config >>  0) & 0xff;
	if (cache_type >= PERF_COUNT_HW_CACHE_MAX)
		return -EINVAL;

	cache_op = (config >>  8) & 0xff;
	if (cache_op >= PERF_COUNT_HW_CACHE_OP_MAX)
		return -EINVAL;

	cache_result = (config >> 16) & 0xff;
	if (cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	val = hw_cache_event_ids[cache_type][cache_op][cache_result];

	if (val == 0)
		return -ENOENT;

	if (val == -1)
		return -EINVAL;

	hwc->config |= val;
	attr->config1 = hw_cache_extra_regs[cache_type][cache_op][cache_result];
	return x86_pmu_extra_regs(val, event);
}

int x86_setup_perfctr(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	u64 config;

	if (!is_sampling_event(event)) {
		hwc->sample_period = x86_pmu.max_period;
		hwc->last_period = hwc->sample_period;
		local64_set(&hwc->period_left, hwc->sample_period);
	} else {
		/*
		 * If we have a PMU initialized but no APIC
		 * interrupts, we cannot sample hardware
		 * events (user-space has to fall back and
		 * sample via a hrtimer based software event):
		 */
		if (!x86_pmu.apic)
			return -EOPNOTSUPP;
	}

	if (attr->type == PERF_TYPE_RAW)
		return x86_pmu_extra_regs(event->attr.config, event);

	if (attr->type == PERF_TYPE_HW_CACHE)
		return set_ext_hw_attr(hwc, event);

	if (attr->config >= x86_pmu.max_events)
		return -EINVAL;

	/*
	 * The generic map:
	 */
	config = x86_pmu.event_map(attr->config);

	if (config == 0)
		return -ENOENT;

	if (config == -1LL)
		return -EINVAL;

	/*
	 * Branch tracing:
	 */
	if (attr->config == PERF_COUNT_HW_BRANCH_INSTRUCTIONS &&
	    !attr->freq && hwc->sample_period == 1) {
		/* BTS is not supported by this architecture. */
		if (!x86_pmu.bts_active)
			return -EOPNOTSUPP;

		/* BTS is currently only allowed for user-mode. */
		if (!attr->exclude_kernel)
			return -EOPNOTSUPP;
	}

	hwc->config |= config;

	return 0;
}

/*
 * check that branch_sample_type is compatible with
 * settings needed for precise_ip > 1 which implies
 * using the LBR to capture ALL taken branches at the
 * priv levels of the measurement
 */
static inline int precise_br_compat(struct perf_event *event)
{
	u64 m = event->attr.branch_sample_type;
	u64 b = 0;

	/* must capture all branches */
	if (!(m & PERF_SAMPLE_BRANCH_ANY))
		return 0;

	m &= PERF_SAMPLE_BRANCH_KERNEL | PERF_SAMPLE_BRANCH_USER;

	if (!event->attr.exclude_user)
		b |= PERF_SAMPLE_BRANCH_USER;

	if (!event->attr.exclude_kernel)
		b |= PERF_SAMPLE_BRANCH_KERNEL;

	/*
	 * ignore PERF_SAMPLE_BRANCH_HV, not supported on x86
	 */

	return m == b;
}

int x86_pmu_hw_config(struct perf_event *event)
{
	if (event->attr.precise_ip) {
		int precise = 0;

		/* Support for constant skid */
		if (x86_pmu.pebs_active) {
			precise++;

			/* Support for IP fixup */
			if (x86_pmu.lbr_nr)
				precise++;
		}

		if (event->attr.precise_ip > precise)
			return -EOPNOTSUPP;
		/*
		 * check that PEBS LBR correction does not conflict with
		 * whatever the user is asking with attr->branch_sample_type
		 */
		if (event->attr.precise_ip > 1) {
			u64 *br_type = &event->attr.branch_sample_type;

			if (has_branch_stack(event)) {
				if (!precise_br_compat(event))
					return -EOPNOTSUPP;

				/* branch_sample_type is compatible */

			} else {
				/*
				 * user did not specify  branch_sample_type
				 *
				 * For PEBS fixups, we capture all
				 * the branches at the priv level of the
				 * event.
				 */
				*br_type = PERF_SAMPLE_BRANCH_ANY;

				if (!event->attr.exclude_user)
					*br_type |= PERF_SAMPLE_BRANCH_USER;

				if (!event->attr.exclude_kernel)
					*br_type |= PERF_SAMPLE_BRANCH_KERNEL;
			}
		}
	}

	/*
	 * Generate PMC IRQs:
	 * (keep 'enabled' bit clear for now)
	 */
	event->hw.config = ARCH_PERFMON_EVENTSEL_INT;

	/*
	 * Count user and OS events unless requested not to
	 */
	if (!event->attr.exclude_user)
		event->hw.config |= ARCH_PERFMON_EVENTSEL_USR;
	if (!event->attr.exclude_kernel)
		event->hw.config |= ARCH_PERFMON_EVENTSEL_OS;

	if (event->attr.type == PERF_TYPE_RAW)
		event->hw.config |= event->attr.config & X86_RAW_EVENT_MASK;

	return x86_setup_perfctr(event);
}

/*
 * Setup the hardware configuration for a given attr_type
 */
static int __x86_pmu_event_init(struct perf_event *event)
{
	int err;

	if (!x86_pmu_initialized())
		return -ENODEV;

	err = 0;
	if (!atomic_inc_not_zero(&active_events)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&active_events) == 0) {
			if (!reserve_pmc_hardware())
				err = -EBUSY;
			else
				reserve_ds_buffers();
		}
		if (!err)
			atomic_inc(&active_events);
		mutex_unlock(&pmc_reserve_mutex);
	}
	if (err)
		return err;

	event->destroy = hw_perf_event_destroy;

	event->hw.idx = -1;
	event->hw.last_cpu = -1;
	event->hw.last_tag = ~0ULL;

	/* mark unused */
	event->hw.extra_reg.idx = EXTRA_REG_NONE;
	event->hw.branch_reg.idx = EXTRA_REG_NONE;

	return x86_pmu.hw_config(event);
}

void x86_pmu_disable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	int idx;

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		u64 val;

		if (!test_bit(idx, cpuc->active_mask))
			continue;
		rdmsrl(x86_pmu_config_addr(idx), val);
		if (!(val & ARCH_PERFMON_EVENTSEL_ENABLE))
			continue;
		val &= ~ARCH_PERFMON_EVENTSEL_ENABLE;
		wrmsrl(x86_pmu_config_addr(idx), val);
	}
}

static void x86_pmu_disable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!x86_pmu_initialized())
		return;

	if (!cpuc->enabled)
		return;

	cpuc->n_added = 0;
	cpuc->enabled = 0;
	barrier();

	x86_pmu.disable_all();
}

void x86_pmu_enable_all(int added)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	int idx;

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		struct hw_perf_event *hwc = &cpuc->events[idx]->hw;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		__x86_pmu_enable_event(hwc, ARCH_PERFMON_EVENTSEL_ENABLE);
	}
}

static struct pmu pmu;

static inline int is_x86_event(struct perf_event *event)
{
	return event->pmu == &pmu;
}

/*
 * Event scheduler state:
 *
 * Assign events iterating over all events and counters, beginning
 * with events with least weights first. Keep the current iterator
 * state in struct sched_state.
 */
struct sched_state {
	int	weight;
	int	event;		/* event index */
	int	counter;	/* counter index */
	int	unassigned;	/* number of events to be assigned left */
	unsigned long used[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
};

/* Total max is X86_PMC_IDX_MAX, but we are O(n!) limited */
#define	SCHED_STATES_MAX	2

struct perf_sched {
	int			max_weight;
	int			max_events;
	struct event_constraint	**constraints;
	struct sched_state	state;
	int			saved_states;
	struct sched_state	saved[SCHED_STATES_MAX];
};

/*
 * Initialize interator that runs through all events and counters.
 */
static void perf_sched_init(struct perf_sched *sched, struct event_constraint **c,
			    int num, int wmin, int wmax)
{
	int idx;

	memset(sched, 0, sizeof(*sched));
	sched->max_events	= num;
	sched->max_weight	= wmax;
	sched->constraints	= c;

	for (idx = 0; idx < num; idx++) {
		if (c[idx]->weight == wmin)
			break;
	}

	sched->state.event	= idx;		/* start with min weight */
	sched->state.weight	= wmin;
	sched->state.unassigned	= num;
}

static void perf_sched_save_state(struct perf_sched *sched)
{
	if (WARN_ON_ONCE(sched->saved_states >= SCHED_STATES_MAX))
		return;

	sched->saved[sched->saved_states] = sched->state;
	sched->saved_states++;
}

static bool perf_sched_restore_state(struct perf_sched *sched)
{
	if (!sched->saved_states)
		return false;

	sched->saved_states--;
	sched->state = sched->saved[sched->saved_states];

	/* continue with next counter: */
	clear_bit(sched->state.counter++, sched->state.used);

	return true;
}

/*
 * Select a counter for the current event to schedule. Return true on
 * success.
 */
static bool __perf_sched_find_counter(struct perf_sched *sched)
{
	struct event_constraint *c;
	int idx;

	if (!sched->state.unassigned)
		return false;

	if (sched->state.event >= sched->max_events)
		return false;

	c = sched->constraints[sched->state.event];

	/* Prefer fixed purpose counters */
	if (c->idxmsk64 & (~0ULL << X86_PMC_IDX_FIXED)) {
		idx = X86_PMC_IDX_FIXED;
		for_each_set_bit_from(idx, c->idxmsk, X86_PMC_IDX_MAX) {
			if (!__test_and_set_bit(idx, sched->state.used))
				goto done;
		}
	}
	/* Grab the first unused counter starting with idx */
	idx = sched->state.counter;
	for_each_set_bit_from(idx, c->idxmsk, X86_PMC_IDX_FIXED) {
		if (!__test_and_set_bit(idx, sched->state.used))
			goto done;
	}

	return false;

done:
	sched->state.counter = idx;

	if (c->overlap)
		perf_sched_save_state(sched);

	return true;
}

static bool perf_sched_find_counter(struct perf_sched *sched)
{
	while (!__perf_sched_find_counter(sched)) {
		if (!perf_sched_restore_state(sched))
			return false;
	}

	return true;
}

/*
 * Go through all unassigned events and find the next one to schedule.
 * Take events with the least weight first. Return true on success.
 */
static bool perf_sched_next_event(struct perf_sched *sched)
{
	struct event_constraint *c;

	if (!sched->state.unassigned || !--sched->state.unassigned)
		return false;

	do {
		/* next event */
		sched->state.event++;
		if (sched->state.event >= sched->max_events) {
			/* next weight */
			sched->state.event = 0;
			sched->state.weight++;
			if (sched->state.weight > sched->max_weight)
				return false;
		}
		c = sched->constraints[sched->state.event];
	} while (c->weight != sched->state.weight);

	sched->state.counter = 0;	/* start with first counter */

	return true;
}

/*
 * Assign a counter for each event.
 */
int perf_assign_events(struct event_constraint **constraints, int n,
			int wmin, int wmax, int *assign)
{
	struct perf_sched sched;

	perf_sched_init(&sched, constraints, n, wmin, wmax);

	do {
		if (!perf_sched_find_counter(&sched))
			break;	/* failed */
		if (assign)
			assign[sched.state.event] = sched.state.counter;
	} while (perf_sched_next_event(&sched));

	return sched.state.unassigned;
}

int x86_schedule_events(struct cpu_hw_events *cpuc, int n, int *assign)
{
	struct event_constraint *c, *constraints[X86_PMC_IDX_MAX];
	unsigned long used_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	int i, wmin, wmax, num = 0;
	struct hw_perf_event *hwc;

	bitmap_zero(used_mask, X86_PMC_IDX_MAX);

	for (i = 0, wmin = X86_PMC_IDX_MAX, wmax = 0; i < n; i++) {
		c = x86_pmu.get_event_constraints(cpuc, cpuc->event_list[i]);
		constraints[i] = c;
		wmin = min(wmin, c->weight);
		wmax = max(wmax, c->weight);
	}

	/*
	 * fastpath, try to reuse previous register
	 */
	for (i = 0; i < n; i++) {
		hwc = &cpuc->event_list[i]->hw;
		c = constraints[i];

		/* never assigned */
		if (hwc->idx == -1)
			break;

		/* constraint still honored */
		if (!test_bit(hwc->idx, c->idxmsk))
			break;

		/* not already used */
		if (test_bit(hwc->idx, used_mask))
			break;

		__set_bit(hwc->idx, used_mask);
		if (assign)
			assign[i] = hwc->idx;
	}

	/* slow path */
	if (i != n)
		num = perf_assign_events(constraints, n, wmin, wmax, assign);

	/*
	 * scheduling failed or is just a simulation,
	 * free resources if necessary
	 */
	if (!assign || num) {
		for (i = 0; i < n; i++) {
			if (x86_pmu.put_event_constraints)
				x86_pmu.put_event_constraints(cpuc, cpuc->event_list[i]);
		}
	}
	return num ? -EINVAL : 0;
}

/*
 * dogrp: true if must collect siblings events (group)
 * returns total number of events and error code
 */
static int collect_events(struct cpu_hw_events *cpuc, struct perf_event *leader, bool dogrp)
{
	struct perf_event *event;
	int n, max_count;

	max_count = x86_pmu.num_counters + x86_pmu.num_counters_fixed;

	/* current number of events already accepted */
	n = cpuc->n_events;

	if (is_x86_event(leader)) {
		if (n >= max_count)
			return -EINVAL;
		cpuc->event_list[n] = leader;
		n++;
	}
	if (!dogrp)
		return n;

	list_for_each_entry(event, &leader->sibling_list, group_entry) {
		if (!is_x86_event(event) ||
		    event->state <= PERF_EVENT_STATE_OFF)
			continue;

		if (n >= max_count)
			return -EINVAL;

		cpuc->event_list[n] = event;
		n++;
	}
	return n;
}

static inline void x86_assign_hw_event(struct perf_event *event,
				struct cpu_hw_events *cpuc, int i)
{
	struct hw_perf_event *hwc = &event->hw;

	hwc->idx = cpuc->assign[i];
	hwc->last_cpu = smp_processor_id();
	hwc->last_tag = ++cpuc->tags[i];

	if (hwc->idx == X86_PMC_IDX_FIXED_BTS) {
		hwc->config_base = 0;
		hwc->event_base	= 0;
	} else if (hwc->idx >= X86_PMC_IDX_FIXED) {
		hwc->config_base = MSR_ARCH_PERFMON_FIXED_CTR_CTRL;
		hwc->event_base = MSR_ARCH_PERFMON_FIXED_CTR0 + (hwc->idx - X86_PMC_IDX_FIXED);
		hwc->event_base_rdpmc = (hwc->idx - X86_PMC_IDX_FIXED) | 1<<30;
	} else {
		hwc->config_base = x86_pmu_config_addr(hwc->idx);
		hwc->event_base  = x86_pmu_event_addr(hwc->idx);
		hwc->event_base_rdpmc = hwc->idx;
	}
}

static inline int match_prev_assignment(struct hw_perf_event *hwc,
					struct cpu_hw_events *cpuc,
					int i)
{
	return hwc->idx == cpuc->assign[i] &&
		hwc->last_cpu == smp_processor_id() &&
		hwc->last_tag == cpuc->tags[i];
}

static void x86_pmu_start(struct perf_event *event, int flags);

static void x86_pmu_enable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct perf_event *event;
	struct hw_perf_event *hwc;
	int i, added = cpuc->n_added;

	if (!x86_pmu_initialized())
		return;

	if (cpuc->enabled)
		return;

	if (cpuc->n_added) {
		int n_running = cpuc->n_events - cpuc->n_added;
		/*
		 * apply assignment obtained either from
		 * hw_perf_group_sched_in() or x86_pmu_enable()
		 *
		 * step1: save events moving to new counters
		 * step2: reprogram moved events into new counters
		 */
		for (i = 0; i < n_running; i++) {
			event = cpuc->event_list[i];
			hwc = &event->hw;

			/*
			 * we can avoid reprogramming counter if:
			 * - assigned same counter as last time
			 * - running on same CPU as last time
			 * - no other event has used the counter since
			 */
			if (hwc->idx == -1 ||
			    match_prev_assignment(hwc, cpuc, i))
				continue;

			/*
			 * Ensure we don't accidentally enable a stopped
			 * counter simply because we rescheduled.
			 */
			if (hwc->state & PERF_HES_STOPPED)
				hwc->state |= PERF_HES_ARCH;

			x86_pmu_stop(event, PERF_EF_UPDATE);
		}

		for (i = 0; i < cpuc->n_events; i++) {
			event = cpuc->event_list[i];
			hwc = &event->hw;

			if (!match_prev_assignment(hwc, cpuc, i))
				x86_assign_hw_event(event, cpuc, i);
			else if (i < n_running)
				continue;

			if (hwc->state & PERF_HES_ARCH)
				continue;

			x86_pmu_start(event, PERF_EF_RELOAD);
		}
		cpuc->n_added = 0;
		perf_events_lapic_init();
	}

	cpuc->enabled = 1;
	barrier();

	x86_pmu.enable_all(added);
}

static DEFINE_PER_CPU(u64 [X86_PMC_IDX_MAX], pmc_prev_left);

/*
 * Set the next IRQ period, based on the hwc->period_left value.
 * To be called with the event disabled in hw:
 */
int x86_perf_event_set_period(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	s64 left = local64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	int ret = 0, idx = hwc->idx;

	if (idx == X86_PMC_IDX_FIXED_BTS)
		return 0;

	/*
	 * If we are way outside a reasonable range then just skip forward:
	 */
	if (unlikely(left <= -period)) {
		left = period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (unlikely(left <= 0)) {
		left += period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}
	/*
	 * Quirk: certain CPUs dont like it if just 1 hw_event is left:
	 */
	if (unlikely(left < 2))
		left = 2;

	if (left > x86_pmu.max_period)
		left = x86_pmu.max_period;

	per_cpu(pmc_prev_left[idx], smp_processor_id()) = left;

	/*
	 * The hw event starts counting from this event offset,
	 * mark it to be able to extra future deltas:
	 */
	local64_set(&hwc->prev_count, (u64)-left);

	wrmsrl(hwc->event_base, (u64)(-left) & x86_pmu.cntval_mask);

	/*
	 * Due to erratum on certan cpu we need
	 * a second write to be sure the register
	 * is updated properly
	 */
	if (x86_pmu.perfctr_second_write) {
		wrmsrl(hwc->event_base,
			(u64)(-left) & x86_pmu.cntval_mask);
	}

	perf_event_update_userpage(event);

	return ret;
}

void x86_pmu_enable_event(struct perf_event *event)
{
	if (__this_cpu_read(cpu_hw_events.enabled))
		__x86_pmu_enable_event(&event->hw,
				       ARCH_PERFMON_EVENTSEL_ENABLE);
}

/*
 * Add a single event to the PMU.
 *
 * The event is added to the group of enabled events
 * but only if it can be scehduled with existing events.
 */
static int x86_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc;
	int assign[X86_PMC_IDX_MAX];
	int n, n0, ret;

	hwc = &event->hw;

	perf_pmu_disable(event->pmu);
	n0 = cpuc->n_events;
	ret = n = collect_events(cpuc, event, false);
	if (ret < 0)
		goto out;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;
	if (!(flags & PERF_EF_START))
		hwc->state |= PERF_HES_ARCH;

	/*
	 * If group events scheduling transaction was started,
	 * skip the schedulability test here, it will be performed
	 * at commit time (->commit_txn) as a whole
	 */
	if (cpuc->group_flag & PERF_EVENT_TXN)
		goto done_collect;

	ret = x86_pmu.schedule_events(cpuc, n, assign);
	if (ret)
		goto out;
	/*
	 * copy new assignment, now we know it is possible
	 * will be used by hw_perf_enable()
	 */
	memcpy(cpuc->assign, assign, n*sizeof(int));

done_collect:
	cpuc->n_events = n;
	cpuc->n_added += n - n0;
	cpuc->n_txn += n - n0;

	ret = 0;
out:
	perf_pmu_enable(event->pmu);
	return ret;
}

static void x86_pmu_start(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	int idx = event->hw.idx;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (WARN_ON_ONCE(idx == -1))
		return;

	if (flags & PERF_EF_RELOAD) {
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));
		x86_perf_event_set_period(event);
	}

	event->hw.state = 0;

	cpuc->events[idx] = event;
	__set_bit(idx, cpuc->active_mask);
	__set_bit(idx, cpuc->running);
	x86_pmu.enable(event);
	perf_event_update_userpage(event);
}

void perf_event_print_debug(void)
{
	u64 ctrl, status, overflow, pmc_ctrl, pmc_count, prev_left, fixed;
	u64 pebs;
	struct cpu_hw_events *cpuc;
	unsigned long flags;
	int cpu, idx;

	if (!x86_pmu.num_counters)
		return;

	local_irq_save(flags);

	cpu = smp_processor_id();
	cpuc = &per_cpu(cpu_hw_events, cpu);

	if (x86_pmu.version >= 2) {
		rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, ctrl);
		rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);
		rdmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, overflow);
		rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR_CTRL, fixed);
		rdmsrl(MSR_IA32_PEBS_ENABLE, pebs);

		pr_info("\n");
		pr_info("CPU#%d: ctrl:       %016llx\n", cpu, ctrl);
		pr_info("CPU#%d: status:     %016llx\n", cpu, status);
		pr_info("CPU#%d: overflow:   %016llx\n", cpu, overflow);
		pr_info("CPU#%d: fixed:      %016llx\n", cpu, fixed);
		pr_info("CPU#%d: pebs:       %016llx\n", cpu, pebs);
	}
	pr_info("CPU#%d: active:     %016llx\n", cpu, *(u64 *)cpuc->active_mask);

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		rdmsrl(x86_pmu_config_addr(idx), pmc_ctrl);
		rdmsrl(x86_pmu_event_addr(idx), pmc_count);

		prev_left = per_cpu(pmc_prev_left[idx], cpu);

		pr_info("CPU#%d:   gen-PMC%d ctrl:  %016llx\n",
			cpu, idx, pmc_ctrl);
		pr_info("CPU#%d:   gen-PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
		pr_info("CPU#%d:   gen-PMC%d left:  %016llx\n",
			cpu, idx, prev_left);
	}
	for (idx = 0; idx < x86_pmu.num_counters_fixed; idx++) {
		rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR0 + idx, pmc_count);

		pr_info("CPU#%d: fixed-PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
	}
	local_irq_restore(flags);
}

void x86_pmu_stop(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	if (__test_and_clear_bit(hwc->idx, cpuc->active_mask)) {
		x86_pmu.disable(event);
		cpuc->events[hwc->idx] = NULL;
		WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
		hwc->state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		/*
		 * Drain the remaining delta count out of a event
		 * that we are disabling:
		 */
		x86_perf_event_update(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

static void x86_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	int i;

	/*
	 * If we're called during a txn, we don't need to do anything.
	 * The events never got scheduled and ->cancel_txn will truncate
	 * the event_list.
	 */
	if (cpuc->group_flag & PERF_EVENT_TXN)
		return;

	x86_pmu_stop(event, PERF_EF_UPDATE);

	for (i = 0; i < cpuc->n_events; i++) {
		if (event == cpuc->event_list[i]) {

			if (x86_pmu.put_event_constraints)
				x86_pmu.put_event_constraints(cpuc, event);

			while (++i < cpuc->n_events)
				cpuc->event_list[i-1] = cpuc->event_list[i];

			--cpuc->n_events;
			break;
		}
	}
	perf_event_update_userpage(event);
}

int x86_pmu_handle_irq(struct pt_regs *regs)
{
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct perf_event *event;
	int idx, handled = 0;
	u64 val;

	cpuc = &__get_cpu_var(cpu_hw_events);

	/*
	 * Some chipsets need to unmask the LVTPC in a particular spot
	 * inside the nmi handler.  As a result, the unmasking was pushed
	 * into all the nmi handlers.
	 *
	 * This generic handler doesn't seem to have any issues where the
	 * unmasking occurs so it was left at the top.
	 */
	apic_write(APIC_LVTPC, APIC_DM_NMI);

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		if (!test_bit(idx, cpuc->active_mask)) {
			/*
			 * Though we deactivated the counter some cpus
			 * might still deliver spurious interrupts still
			 * in flight. Catch them:
			 */
			if (__test_and_clear_bit(idx, cpuc->running))
				handled++;
			continue;
		}

		event = cpuc->events[idx];

		val = x86_perf_event_update(event);
		if (val & (1ULL << (x86_pmu.cntval_bits - 1)))
			continue;

		/*
		 * event overflow
		 */
		handled++;
		perf_sample_data_init(&data, 0, event->hw.last_period);

		if (!x86_perf_event_set_period(event))
			continue;

		if (perf_event_overflow(event, &data, regs))
			x86_pmu_stop(event, 0);
	}

	if (handled)
		inc_irq_stat(apic_perf_irqs);

	return handled;
}

void perf_events_lapic_init(void)
{
	if (!x86_pmu.apic || !x86_pmu_initialized())
		return;

	/*
	 * Always use NMI for PMU
	 */
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}

static int __kprobes
perf_event_nmi_handler(unsigned int cmd, struct pt_regs *regs)
{
	if (!atomic_read(&active_events))
		return NMI_DONE;

	return x86_pmu.handle_irq(regs);
}

struct event_constraint emptyconstraint;
struct event_constraint unconstrained;

static int __cpuinit
x86_pmu_notifier(struct notifier_block *self, unsigned long action, void *hcpu)
{
	unsigned int cpu = (long)hcpu;
	struct cpu_hw_events *cpuc = &per_cpu(cpu_hw_events, cpu);
	int ret = NOTIFY_OK;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		cpuc->kfree_on_online = NULL;
		if (x86_pmu.cpu_prepare)
			ret = x86_pmu.cpu_prepare(cpu);
		break;

	case CPU_STARTING:
		if (x86_pmu.attr_rdpmc)
			set_in_cr4(X86_CR4_PCE);
		if (x86_pmu.cpu_starting)
			x86_pmu.cpu_starting(cpu);
		break;

	case CPU_ONLINE:
		kfree(cpuc->kfree_on_online);
		break;

	case CPU_DYING:
		if (x86_pmu.cpu_dying)
			x86_pmu.cpu_dying(cpu);
		break;

	case CPU_UP_CANCELED:
	case CPU_DEAD:
		if (x86_pmu.cpu_dead)
			x86_pmu.cpu_dead(cpu);
		break;

	default:
		break;
	}

	return ret;
}

static void __init pmu_check_apic(void)
{
	if (cpu_has_apic)
		return;

	x86_pmu.apic = 0;
	pr_info("no APIC, boot with the \"lapic\" boot parameter to force-enable it.\n");
	pr_info("no hardware sampling interrupt available.\n");
}

static struct attribute_group x86_pmu_format_group = {
	.name = "format",
	.attrs = NULL,
};

static int __init init_hw_perf_events(void)
{
	struct x86_pmu_quirk *quirk;
	struct event_constraint *c;
	int err;

	pr_info("Performance Events: ");

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		err = intel_pmu_init();
		break;
	case X86_VENDOR_AMD:
		err = amd_pmu_init();
		break;
	default:
		return 0;
	}
	if (err != 0) {
		pr_cont("no PMU driver, software events only.\n");
		return 0;
	}

	pmu_check_apic();

	/* sanity check that the hardware exists or is emulated */
	if (!check_hw_exists())
		return 0;

	pr_cont("%s PMU driver.\n", x86_pmu.name);

	for (quirk = x86_pmu.quirks; quirk; quirk = quirk->next)
		quirk->func();

	if (x86_pmu.num_counters > X86_PMC_MAX_GENERIC) {
		WARN(1, KERN_ERR "hw perf events %d > max(%d), clipping!",
		     x86_pmu.num_counters, X86_PMC_MAX_GENERIC);
		x86_pmu.num_counters = X86_PMC_MAX_GENERIC;
	}
	x86_pmu.intel_ctrl = (1 << x86_pmu.num_counters) - 1;

	if (x86_pmu.num_counters_fixed > X86_PMC_MAX_FIXED) {
		WARN(1, KERN_ERR "hw perf events fixed %d > max(%d), clipping!",
		     x86_pmu.num_counters_fixed, X86_PMC_MAX_FIXED);
		x86_pmu.num_counters_fixed = X86_PMC_MAX_FIXED;
	}

	x86_pmu.intel_ctrl |=
		((1LL << x86_pmu.num_counters_fixed)-1) << X86_PMC_IDX_FIXED;

	perf_events_lapic_init();
	register_nmi_handler(NMI_LOCAL, perf_event_nmi_handler, 0, "PMI");

	unconstrained = (struct event_constraint)
		__EVENT_CONSTRAINT(0, (1ULL << x86_pmu.num_counters) - 1,
				   0, x86_pmu.num_counters, 0);

	if (x86_pmu.event_constraints) {
		/*
		 * event on fixed counter2 (REF_CYCLES) only works on this
		 * counter, so do not extend mask to generic counters
		 */
		for_each_event_constraint(c, x86_pmu.event_constraints) {
			if (c->cmask != X86_RAW_EVENT_MASK
			    || c->idxmsk64 == X86_PMC_MSK_FIXED_REF_CYCLES) {
				continue;
			}

			c->idxmsk64 |= (1ULL << x86_pmu.num_counters) - 1;
			c->weight += x86_pmu.num_counters;
		}
	}

	x86_pmu.attr_rdpmc = 1; /* enable userspace RDPMC usage by default */
	x86_pmu_format_group.attrs = x86_pmu.format_attrs;

	pr_info("... version:                %d\n",     x86_pmu.version);
	pr_info("... bit width:              %d\n",     x86_pmu.cntval_bits);
	pr_info("... generic registers:      %d\n",     x86_pmu.num_counters);
	pr_info("... value mask:             %016Lx\n", x86_pmu.cntval_mask);
	pr_info("... max period:             %016Lx\n", x86_pmu.max_period);
	pr_info("... fixed-purpose events:   %d\n",     x86_pmu.num_counters_fixed);
	pr_info("... event mask:             %016Lx\n", x86_pmu.intel_ctrl);

	perf_pmu_register(&pmu, "cpu", PERF_TYPE_RAW);
	perf_cpu_notifier(x86_pmu_notifier);

	return 0;
}
early_initcall(init_hw_perf_events);

static inline void x86_pmu_read(struct perf_event *event)
{
	x86_perf_event_update(event);
}

/*
 * Start group events scheduling transaction
 * Set the flag to make pmu::enable() not perform the
 * schedulability test, it will be performed at commit time
 */
static void x86_pmu_start_txn(struct pmu *pmu)
{
	perf_pmu_disable(pmu);
	__this_cpu_or(cpu_hw_events.group_flag, PERF_EVENT_TXN);
	__this_cpu_write(cpu_hw_events.n_txn, 0);
}

/*
 * Stop group events scheduling transaction
 * Clear the flag and pmu::enable() will perform the
 * schedulability test.
 */
static void x86_pmu_cancel_txn(struct pmu *pmu)
{
	__this_cpu_and(cpu_hw_events.group_flag, ~PERF_EVENT_TXN);
	/*
	 * Truncate the collected events.
	 */
	__this_cpu_sub(cpu_hw_events.n_added, __this_cpu_read(cpu_hw_events.n_txn));
	__this_cpu_sub(cpu_hw_events.n_events, __this_cpu_read(cpu_hw_events.n_txn));
	perf_pmu_enable(pmu);
}

/*
 * Commit group events scheduling transaction
 * Perform the group schedulability test as a whole
 * Return 0 if success
 */
static int x86_pmu_commit_txn(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	int assign[X86_PMC_IDX_MAX];
	int n, ret;

	n = cpuc->n_events;

	if (!x86_pmu_initialized())
		return -EAGAIN;

	ret = x86_pmu.schedule_events(cpuc, n, assign);
	if (ret)
		return ret;

	/*
	 * copy new assignment, now we know it is possible
	 * will be used by hw_perf_enable()
	 */
	memcpy(cpuc->assign, assign, n*sizeof(int));

	cpuc->group_flag &= ~PERF_EVENT_TXN;
	perf_pmu_enable(pmu);
	return 0;
}
/*
 * a fake_cpuc is used to validate event groups. Due to
 * the extra reg logic, we need to also allocate a fake
 * per_core and per_cpu structure. Otherwise, group events
 * using extra reg may conflict without the kernel being
 * able to catch this when the last event gets added to
 * the group.
 */
static void free_fake_cpuc(struct cpu_hw_events *cpuc)
{
	kfree(cpuc->shared_regs);
	kfree(cpuc);
}

static struct cpu_hw_events *allocate_fake_cpuc(void)
{
	struct cpu_hw_events *cpuc;
	int cpu = raw_smp_processor_id();

	cpuc = kzalloc(sizeof(*cpuc), GFP_KERNEL);
	if (!cpuc)
		return ERR_PTR(-ENOMEM);

	/* only needed, if we have extra_regs */
	if (x86_pmu.extra_regs) {
		cpuc->shared_regs = allocate_shared_regs(cpu);
		if (!cpuc->shared_regs)
			goto error;
	}
	cpuc->is_fake = 1;
	return cpuc;
error:
	free_fake_cpuc(cpuc);
	return ERR_PTR(-ENOMEM);
}

/*
 * validate that we can schedule this event
 */
static int validate_event(struct perf_event *event)
{
	struct cpu_hw_events *fake_cpuc;
	struct event_constraint *c;
	int ret = 0;

	fake_cpuc = allocate_fake_cpuc();
	if (IS_ERR(fake_cpuc))
		return PTR_ERR(fake_cpuc);

	c = x86_pmu.get_event_constraints(fake_cpuc, event);

	if (!c || !c->weight)
		ret = -EINVAL;

	if (x86_pmu.put_event_constraints)
		x86_pmu.put_event_constraints(fake_cpuc, event);

	free_fake_cpuc(fake_cpuc);

	return ret;
}

/*
 * validate a single event group
 *
 * validation include:
 *	- check events are compatible which each other
 *	- events do not compete for the same counter
 *	- number of events <= number of counters
 *
 * validation ensures the group can be loaded onto the
 * PMU if it was the only group available.
 */
static int validate_group(struct perf_event *event)
{
	struct perf_event *leader = event->group_leader;
	struct cpu_hw_events *fake_cpuc;
	int ret = -EINVAL, n;

	fake_cpuc = allocate_fake_cpuc();
	if (IS_ERR(fake_cpuc))
		return PTR_ERR(fake_cpuc);
	/*
	 * the event is not yet connected with its
	 * siblings therefore we must first collect
	 * existing siblings, then add the new event
	 * before we can simulate the scheduling
	 */
	n = collect_events(fake_cpuc, leader, true);
	if (n < 0)
		goto out;

	fake_cpuc->n_events = n;
	n = collect_events(fake_cpuc, event, false);
	if (n < 0)
		goto out;

	fake_cpuc->n_events = n;

	ret = x86_pmu.schedule_events(fake_cpuc, n, NULL);

out:
	free_fake_cpuc(fake_cpuc);
	return ret;
}

static int x86_pmu_event_init(struct perf_event *event)
{
	struct pmu *tmp;
	int err;

	switch (event->attr.type) {
	case PERF_TYPE_RAW:
	case PERF_TYPE_HARDWARE:
	case PERF_TYPE_HW_CACHE:
		break;

	default:
		return -ENOENT;
	}

	err = __x86_pmu_event_init(event);
	if (!err) {
		/*
		 * we temporarily connect event to its pmu
		 * such that validate_group() can classify
		 * it as an x86 event using is_x86_event()
		 */
		tmp = event->pmu;
		event->pmu = &pmu;

		if (event->group_leader != event)
			err = validate_group(event);
		else
			err = validate_event(event);

		event->pmu = tmp;
	}
	if (err) {
		if (event->destroy)
			event->destroy(event);
	}

	return err;
}

static int x86_pmu_event_idx(struct perf_event *event)
{
	int idx = event->hw.idx;

	if (!x86_pmu.attr_rdpmc)
		return 0;

	if (x86_pmu.num_counters_fixed && idx >= X86_PMC_IDX_FIXED) {
		idx -= X86_PMC_IDX_FIXED;
		idx |= 1 << 30;
	}

	return idx + 1;
}

static ssize_t get_attr_rdpmc(struct device *cdev,
			      struct device_attribute *attr,
			      char *buf)
{
	return snprintf(buf, 40, "%d\n", x86_pmu.attr_rdpmc);
}

static void change_rdpmc(void *info)
{
	bool enable = !!(unsigned long)info;

	if (enable)
		set_in_cr4(X86_CR4_PCE);
	else
		clear_in_cr4(X86_CR4_PCE);
}

static ssize_t set_attr_rdpmc(struct device *cdev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	unsigned long val;
	ssize_t ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (!!val != !!x86_pmu.attr_rdpmc) {
		x86_pmu.attr_rdpmc = !!val;
		smp_call_function(change_rdpmc, (void *)val, 1);
	}

	return count;
}

static DEVICE_ATTR(rdpmc, S_IRUSR | S_IWUSR, get_attr_rdpmc, set_attr_rdpmc);

static struct attribute *x86_pmu_attrs[] = {
	&dev_attr_rdpmc.attr,
	NULL,
};

static struct attribute_group x86_pmu_attr_group = {
	.attrs = x86_pmu_attrs,
};

static const struct attribute_group *x86_pmu_attr_groups[] = {
	&x86_pmu_attr_group,
	&x86_pmu_format_group,
	NULL,
};

static void x86_pmu_flush_branch_stack(void)
{
	if (x86_pmu.flush_branch_stack)
		x86_pmu.flush_branch_stack();
}

static struct pmu pmu = {
	.pmu_enable		= x86_pmu_enable,
	.pmu_disable		= x86_pmu_disable,

	.attr_groups	= x86_pmu_attr_groups,

	.event_init	= x86_pmu_event_init,

	.add			= x86_pmu_add,
	.del			= x86_pmu_del,
	.start			= x86_pmu_start,
	.stop			= x86_pmu_stop,
	.read			= x86_pmu_read,

	.start_txn	= x86_pmu_start_txn,
	.cancel_txn	= x86_pmu_cancel_txn,
	.commit_txn	= x86_pmu_commit_txn,

	.event_idx	= x86_pmu_event_idx,
	.flush_branch_stack	= x86_pmu_flush_branch_stack,
};

void arch_perf_update_userpage(struct perf_event_mmap_page *userpg, u64 now)
{
	userpg->cap_usr_time = 0;
	userpg->cap_usr_rdpmc = x86_pmu.attr_rdpmc;
	userpg->pmc_width = x86_pmu.cntval_bits;

	if (!boot_cpu_has(X86_FEATURE_CONSTANT_TSC))
		return;

	if (!boot_cpu_has(X86_FEATURE_NONSTOP_TSC))
		return;

	userpg->cap_usr_time = 1;
	userpg->time_mult = this_cpu_read(cyc2ns);
	userpg->time_shift = CYC2NS_SCALE_FACTOR;
	userpg->time_offset = this_cpu_read(cyc2ns_offset) - now;
}

/*
 * callchain support
 */

static int backtrace_stack(void *data, char *name)
{
	return 0;
}

static void backtrace_address(void *data, unsigned long addr, int reliable)
{
	struct perf_callchain_entry *entry = data;

	perf_callchain_store(entry, addr);
}

static const struct stacktrace_ops backtrace_ops = {
	.stack			= backtrace_stack,
	.address		= backtrace_address,
	.walk_stack		= print_context_stack_bp,
};

void
perf_callchain_kernel(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		/* TODO: We don't support guest os callchain now */
		return;
	}

	perf_callchain_store(entry, regs->ip);

	dump_trace(NULL, regs, NULL, 0, &backtrace_ops, entry);
}

static inline int
valid_user_frame(const void __user *fp, unsigned long size)
{
	return (__range_not_ok(fp, size, TASK_SIZE) == 0);
}

#ifdef CONFIG_COMPAT

#include <asm/compat.h>

static inline int
perf_callchain_user32(struct pt_regs *regs, struct perf_callchain_entry *entry)
{
	/* 32-bit process in 64-bit kernel. */
	struct stack_frame_ia32 frame;
	const void __user *fp;

	if (!test_thread_flag(TIF_IA32))
		return 0;

	fp = compat_ptr(regs->bp);
	while (entry->nr < PERF_MAX_STACK_DEPTH) {
		unsigned long bytes;
		frame.next_frame     = 0;
		frame.return_address = 0;

		bytes = copy_from_user_nmi(&frame, fp, sizeof(frame));
		if (bytes != sizeof(frame))
			break;

		if (!valid_user_frame(fp, sizeof(frame)))
			break;

		perf_callchain_store(entry, frame.return_address);
		fp = compat_ptr(frame.next_frame);
	}
	return 1;
}
#else
static inline int
perf_callchain_user32(struct pt_regs *regs, struct perf_callchain_entry *entry)
{
    return 0;
}
#endif

void
perf_callchain_user(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	struct stack_frame frame;
	const void __user *fp;

	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		/* TODO: We don't support guest os callchain now */
		return;
	}

	fp = (void __user *)regs->bp;

	perf_callchain_store(entry, regs->ip);

	if (!current->mm)
		return;

	if (perf_callchain_user32(regs, entry))
		return;

	while (entry->nr < PERF_MAX_STACK_DEPTH) {
		unsigned long bytes;
		frame.next_frame	     = NULL;
		frame.return_address = 0;

		bytes = copy_from_user_nmi(&frame, fp, sizeof(frame));
		if (bytes != sizeof(frame))
			break;

		if (!valid_user_frame(fp, sizeof(frame)))
			break;

		perf_callchain_store(entry, frame.return_address);
		fp = frame.next_frame;
	}
}

unsigned long perf_instruction_pointer(struct pt_regs *regs)
{
	unsigned long ip;

	if (perf_guest_cbs && perf_guest_cbs->is_in_guest())
		ip = perf_guest_cbs->get_guest_ip();
	else
		ip = instruction_pointer(regs);

	return ip;
}

unsigned long perf_misc_flags(struct pt_regs *regs)
{
	int misc = 0;

	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		if (perf_guest_cbs->is_user_mode())
			misc |= PERF_RECORD_MISC_GUEST_USER;
		else
			misc |= PERF_RECORD_MISC_GUEST_KERNEL;
	} else {
		if (!kernel_ip(regs->ip))
			misc |= PERF_RECORD_MISC_USER;
		else
			misc |= PERF_RECORD_MISC_KERNEL;
	}

	if (regs->flags & PERF_EFLAGS_EXACT)
		misc |= PERF_RECORD_MISC_EXACT_IP;

	return misc;
}

void perf_get_x86_pmu_capability(struct x86_pmu_capability *cap)
{
	cap->version		= x86_pmu.version;
	cap->num_counters_gp	= x86_pmu.num_counters;
	cap->num_counters_fixed	= x86_pmu.num_counters_fixed;
	cap->bit_width_gp	= x86_pmu.cntval_bits;
	cap->bit_width_fixed	= x86_pmu.cntval_bits;
	cap->events_mask	= (unsigned int)x86_pmu.events_maskl;
	cap->events_mask_len	= x86_pmu.events_mask_len;
}
EXPORT_SYMBOL_GPL(perf_get_x86_pmu_capability);
