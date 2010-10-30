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
#include <linux/highmem.h>
#include <linux/cpu.h>
#include <linux/bitops.h>

#include <asm/apic.h>
#include <asm/stacktrace.h>
#include <asm/nmi.h>
#include <asm/compat.h>

#if 0
#undef wrmsrl
#define wrmsrl(msr, val) 					\
do {								\
	trace_printk("wrmsrl(%lx, %lx)\n", (unsigned long)(msr),\
			(unsigned long)(val));			\
	native_write_msr((msr), (u32)((u64)(val)), 		\
			(u32)((u64)(val) >> 32));		\
} while (0)
#endif

/*
 * best effort, GUP based copy_from_user() that assumes IRQ or NMI context
 */
static unsigned long
copy_from_user_nmi(void *to, const void __user *from, unsigned long n)
{
	unsigned long offset, addr = (unsigned long)from;
	int type = in_nmi() ? KM_NMI : KM_IRQ0;
	unsigned long size, len = 0;
	struct page *page;
	void *map;
	int ret;

	do {
		ret = __get_user_pages_fast(addr, 1, 0, &page);
		if (!ret)
			break;

		offset = addr & (PAGE_SIZE - 1);
		size = min(PAGE_SIZE - offset, n - len);

		map = kmap_atomic(page, type);
		memcpy(to, map+offset, size);
		kunmap_atomic(map, type);
		put_page(page);

		len  += size;
		to   += size;
		addr += size;

	} while (len < n);

	return len;
}

struct event_constraint {
	union {
		unsigned long	idxmsk[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
		u64		idxmsk64;
	};
	u64	code;
	u64	cmask;
	int	weight;
};

struct amd_nb {
	int nb_id;  /* NorthBridge id */
	int refcnt; /* reference count */
	struct perf_event *owners[X86_PMC_IDX_MAX];
	struct event_constraint event_constraints[X86_PMC_IDX_MAX];
};

#define MAX_LBR_ENTRIES		16

struct cpu_hw_events {
	/*
	 * Generic x86 PMC bits
	 */
	struct perf_event	*events[X86_PMC_IDX_MAX]; /* in counter order */
	unsigned long		active_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	unsigned long		running[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	int			enabled;

	int			n_events;
	int			n_added;
	int			n_txn;
	int			assign[X86_PMC_IDX_MAX]; /* event to counter assignment */
	u64			tags[X86_PMC_IDX_MAX];
	struct perf_event	*event_list[X86_PMC_IDX_MAX]; /* in enabled order */

	unsigned int		group_flag;

	/*
	 * Intel DebugStore bits
	 */
	struct debug_store	*ds;
	u64			pebs_enabled;

	/*
	 * Intel LBR bits
	 */
	int				lbr_users;
	void				*lbr_context;
	struct perf_branch_stack	lbr_stack;
	struct perf_branch_entry	lbr_entries[MAX_LBR_ENTRIES];

	/*
	 * AMD specific bits
	 */
	struct amd_nb		*amd_nb;
};

#define __EVENT_CONSTRAINT(c, n, m, w) {\
	{ .idxmsk64 = (n) },		\
	.code = (c),			\
	.cmask = (m),			\
	.weight = (w),			\
}

#define EVENT_CONSTRAINT(c, n, m)	\
	__EVENT_CONSTRAINT(c, n, m, HWEIGHT(n))

/*
 * Constraint on the Event code.
 */
#define INTEL_EVENT_CONSTRAINT(c, n)	\
	EVENT_CONSTRAINT(c, n, ARCH_PERFMON_EVENTSEL_EVENT)

/*
 * Constraint on the Event code + UMask + fixed-mask
 *
 * filter mask to validate fixed counter events.
 * the following filters disqualify for fixed counters:
 *  - inv
 *  - edge
 *  - cnt-mask
 *  The other filters are supported by fixed counters.
 *  The any-thread option is supported starting with v3.
 */
#define FIXED_EVENT_CONSTRAINT(c, n)	\
	EVENT_CONSTRAINT(c, (1ULL << (32+n)), X86_RAW_EVENT_MASK)

/*
 * Constraint on the Event code + UMask
 */
#define PEBS_EVENT_CONSTRAINT(c, n)	\
	EVENT_CONSTRAINT(c, n, INTEL_ARCH_EVENT_MASK)

#define EVENT_CONSTRAINT_END		\
	EVENT_CONSTRAINT(0, 0, 0)

#define for_each_event_constraint(e, c)	\
	for ((e) = (c); (e)->weight; (e)++)

union perf_capabilities {
	struct {
		u64	lbr_format    : 6;
		u64	pebs_trap     : 1;
		u64	pebs_arch_reg : 1;
		u64	pebs_format   : 4;
		u64	smm_freeze    : 1;
	};
	u64	capabilities;
};

/*
 * struct x86_pmu - generic x86 pmu
 */
struct x86_pmu {
	/*
	 * Generic x86 PMC bits
	 */
	const char	*name;
	int		version;
	int		(*handle_irq)(struct pt_regs *);
	void		(*disable_all)(void);
	void		(*enable_all)(int added);
	void		(*enable)(struct perf_event *);
	void		(*disable)(struct perf_event *);
	int		(*hw_config)(struct perf_event *event);
	int		(*schedule_events)(struct cpu_hw_events *cpuc, int n, int *assign);
	unsigned	eventsel;
	unsigned	perfctr;
	u64		(*event_map)(int);
	int		max_events;
	int		num_counters;
	int		num_counters_fixed;
	int		cntval_bits;
	u64		cntval_mask;
	int		apic;
	u64		max_period;
	struct event_constraint *
			(*get_event_constraints)(struct cpu_hw_events *cpuc,
						 struct perf_event *event);

	void		(*put_event_constraints)(struct cpu_hw_events *cpuc,
						 struct perf_event *event);
	struct event_constraint *event_constraints;
	void		(*quirks)(void);
	int		perfctr_second_write;

	int		(*cpu_prepare)(int cpu);
	void		(*cpu_starting)(int cpu);
	void		(*cpu_dying)(int cpu);
	void		(*cpu_dead)(int cpu);

	/*
	 * Intel Arch Perfmon v2+
	 */
	u64			intel_ctrl;
	union perf_capabilities intel_cap;

	/*
	 * Intel DebugStore bits
	 */
	int		bts, pebs;
	int		pebs_record_size;
	void		(*drain_pebs)(struct pt_regs *regs);
	struct event_constraint *pebs_constraints;

	/*
	 * Intel LBR
	 */
	unsigned long	lbr_tos, lbr_from, lbr_to; /* MSR base regs       */
	int		lbr_nr;			   /* hardware stack size */
};

static struct x86_pmu x86_pmu __read_mostly;

static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events) = {
	.enabled = 1,
};

static int x86_perf_event_set_period(struct perf_event *event);

/*
 * Generalized hw caching related hw_event table, filled
 * in on a per model basis. A value of 0 means
 * 'not supported', -1 means 'hw_event makes no sense on
 * this CPU', any other value means the raw hw_event
 * ID.
 */

#define C(x) PERF_COUNT_HW_CACHE_##x

static u64 __read_mostly hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX];

/*
 * Propagate event elapsed time into the generic event.
 * Can only be executed on the CPU where the event is active.
 * Returns the delta events processed.
 */
static u64
x86_perf_event_update(struct perf_event *event)
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
	rdmsrl(hwc->event_base + idx, new_raw_count);

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

static atomic_t active_events;
static DEFINE_MUTEX(pmc_reserve_mutex);

#ifdef CONFIG_X86_LOCAL_APIC

static bool reserve_pmc_hardware(void)
{
	int i;

	if (nmi_watchdog == NMI_LOCAL_APIC)
		disable_lapic_nmi_watchdog();

	for (i = 0; i < x86_pmu.num_counters; i++) {
		if (!reserve_perfctr_nmi(x86_pmu.perfctr + i))
			goto perfctr_fail;
	}

	for (i = 0; i < x86_pmu.num_counters; i++) {
		if (!reserve_evntsel_nmi(x86_pmu.eventsel + i))
			goto eventsel_fail;
	}

	return true;

eventsel_fail:
	for (i--; i >= 0; i--)
		release_evntsel_nmi(x86_pmu.eventsel + i);

	i = x86_pmu.num_counters;

perfctr_fail:
	for (i--; i >= 0; i--)
		release_perfctr_nmi(x86_pmu.perfctr + i);

	if (nmi_watchdog == NMI_LOCAL_APIC)
		enable_lapic_nmi_watchdog();

	return false;
}

static void release_pmc_hardware(void)
{
	int i;

	for (i = 0; i < x86_pmu.num_counters; i++) {
		release_perfctr_nmi(x86_pmu.perfctr + i);
		release_evntsel_nmi(x86_pmu.eventsel + i);
	}

	if (nmi_watchdog == NMI_LOCAL_APIC)
		enable_lapic_nmi_watchdog();
}

#else

static bool reserve_pmc_hardware(void) { return true; }
static void release_pmc_hardware(void) {}

#endif

static int reserve_ds_buffers(void);
static void release_ds_buffers(void);

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
set_ext_hw_attr(struct hw_perf_event *hwc, struct perf_event_attr *attr)
{
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

	return 0;
}

static int x86_setup_perfctr(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	u64 config;

	if (!hwc->sample_period) {
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
		return 0;

	if (attr->type == PERF_TYPE_HW_CACHE)
		return set_ext_hw_attr(hwc, attr);

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
	if ((attr->config == PERF_COUNT_HW_BRANCH_INSTRUCTIONS) &&
	    (hwc->sample_period == 1)) {
		/* BTS is not supported by this architecture. */
		if (!x86_pmu.bts)
			return -EOPNOTSUPP;

		/* BTS is currently only allowed for user-mode. */
		if (!attr->exclude_kernel)
			return -EOPNOTSUPP;
	}

	hwc->config |= config;

	return 0;
}

static int x86_pmu_hw_config(struct perf_event *event)
{
	if (event->attr.precise_ip) {
		int precise = 0;

		/* Support for constant skid */
		if (x86_pmu.pebs)
			precise++;

		/* Support for IP fixup */
		if (x86_pmu.lbr_nr)
			precise++;

		if (event->attr.precise_ip > precise)
			return -EOPNOTSUPP;
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
			else {
				err = reserve_ds_buffers();
				if (err)
					release_pmc_hardware();
			}
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

	return x86_pmu.hw_config(event);
}

static void x86_pmu_disable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	int idx;

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		u64 val;

		if (!test_bit(idx, cpuc->active_mask))
			continue;
		rdmsrl(x86_pmu.eventsel + idx, val);
		if (!(val & ARCH_PERFMON_EVENTSEL_ENABLE))
			continue;
		val &= ~ARCH_PERFMON_EVENTSEL_ENABLE;
		wrmsrl(x86_pmu.eventsel + idx, val);
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

static void x86_pmu_enable_all(int added)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	int idx;

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		struct perf_event *event = cpuc->events[idx];
		u64 val;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		val = event->hw.config;
		val |= ARCH_PERFMON_EVENTSEL_ENABLE;
		wrmsrl(x86_pmu.eventsel + idx, val);
	}
}

static struct pmu pmu;

static inline int is_x86_event(struct perf_event *event)
{
	return event->pmu == &pmu;
}

static int x86_schedule_events(struct cpu_hw_events *cpuc, int n, int *assign)
{
	struct event_constraint *c, *constraints[X86_PMC_IDX_MAX];
	unsigned long used_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	int i, j, w, wmax, num = 0;
	struct hw_perf_event *hwc;

	bitmap_zero(used_mask, X86_PMC_IDX_MAX);

	for (i = 0; i < n; i++) {
		c = x86_pmu.get_event_constraints(cpuc, cpuc->event_list[i]);
		constraints[i] = c;
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
	if (i == n)
		goto done;

	/*
	 * begin slow path
	 */

	bitmap_zero(used_mask, X86_PMC_IDX_MAX);

	/*
	 * weight = number of possible counters
	 *
	 * 1    = most constrained, only works on one counter
	 * wmax = least constrained, works on any counter
	 *
	 * assign events to counters starting with most
	 * constrained events.
	 */
	wmax = x86_pmu.num_counters;

	/*
	 * when fixed event counters are present,
	 * wmax is incremented by 1 to account
	 * for one more choice
	 */
	if (x86_pmu.num_counters_fixed)
		wmax++;

	for (w = 1, num = n; num && w <= wmax; w++) {
		/* for each event */
		for (i = 0; num && i < n; i++) {
			c = constraints[i];
			hwc = &cpuc->event_list[i]->hw;

			if (c->weight != w)
				continue;

			for_each_set_bit(j, c->idxmsk, X86_PMC_IDX_MAX) {
				if (!test_bit(j, used_mask))
					break;
			}

			if (j == X86_PMC_IDX_MAX)
				break;

			__set_bit(j, used_mask);

			if (assign)
				assign[i] = j;
			num--;
		}
	}
done:
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
	return num ? -ENOSPC : 0;
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
			return -ENOSPC;
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
			return -ENOSPC;

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
		/*
		 * We set it so that event_base + idx in wrmsr/rdmsr maps to
		 * MSR_ARCH_PERFMON_FIXED_CTR0 ... CTR2:
		 */
		hwc->event_base =
			MSR_ARCH_PERFMON_FIXED_CTR0 - X86_PMC_IDX_FIXED;
	} else {
		hwc->config_base = x86_pmu.eventsel;
		hwc->event_base  = x86_pmu.perfctr;
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
static void x86_pmu_stop(struct perf_event *event, int flags);

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

static inline void __x86_pmu_enable_event(struct hw_perf_event *hwc,
					  u64 enable_mask)
{
	wrmsrl(hwc->config_base + hwc->idx, hwc->config | enable_mask);
}

static inline void x86_pmu_disable_event(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base + hwc->idx, hwc->config);
}

static DEFINE_PER_CPU(u64 [X86_PMC_IDX_MAX], pmc_prev_left);

/*
 * Set the next IRQ period, based on the hwc->period_left value.
 * To be called with the event disabled in hw:
 */
static int
x86_perf_event_set_period(struct perf_event *event)
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

	wrmsrl(hwc->event_base + idx, (u64)(-left) & x86_pmu.cntval_mask);

	/*
	 * Due to erratum on certan cpu we need
	 * a second write to be sure the register
	 * is updated properly
	 */
	if (x86_pmu.perfctr_second_write) {
		wrmsrl(hwc->event_base + idx,
			(u64)(-left) & x86_pmu.cntval_mask);
	}

	perf_event_update_userpage(event);

	return ret;
}

static void x86_pmu_enable_event(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	if (cpuc->enabled)
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
	 * skip the schedulability test here, it will be peformed
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
		rdmsrl(x86_pmu.eventsel + idx, pmc_ctrl);
		rdmsrl(x86_pmu.perfctr  + idx, pmc_count);

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

static void x86_pmu_stop(struct perf_event *event, int flags)
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

static int x86_pmu_handle_irq(struct pt_regs *regs)
{
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct perf_event *event;
	int idx, handled = 0;
	u64 val;

	perf_sample_data_init(&data, 0);

	cpuc = &__get_cpu_var(cpu_hw_events);

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
		data.period	= event->hw.last_period;

		if (!x86_perf_event_set_period(event))
			continue;

		if (perf_event_overflow(event, 1, &data, regs))
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

struct pmu_nmi_state {
	unsigned int	marked;
	int		handled;
};

static DEFINE_PER_CPU(struct pmu_nmi_state, pmu_nmi);

static int __kprobes
perf_event_nmi_handler(struct notifier_block *self,
			 unsigned long cmd, void *__args)
{
	struct die_args *args = __args;
	unsigned int this_nmi;
	int handled;

	if (!atomic_read(&active_events))
		return NOTIFY_DONE;

	switch (cmd) {
	case DIE_NMI:
	case DIE_NMI_IPI:
		break;
	case DIE_NMIUNKNOWN:
		this_nmi = percpu_read(irq_stat.__nmi_count);
		if (this_nmi != __get_cpu_var(pmu_nmi).marked)
			/* let the kernel handle the unknown nmi */
			return NOTIFY_DONE;
		/*
		 * This one is a PMU back-to-back nmi. Two events
		 * trigger 'simultaneously' raising two back-to-back
		 * NMIs. If the first NMI handles both, the latter
		 * will be empty and daze the CPU. So, we drop it to
		 * avoid false-positive 'unknown nmi' messages.
		 */
		return NOTIFY_STOP;
	default:
		return NOTIFY_DONE;
	}

	apic_write(APIC_LVTPC, APIC_DM_NMI);

	handled = x86_pmu.handle_irq(args->regs);
	if (!handled)
		return NOTIFY_DONE;

	this_nmi = percpu_read(irq_stat.__nmi_count);
	if ((handled > 1) ||
		/* the next nmi could be a back-to-back nmi */
	    ((__get_cpu_var(pmu_nmi).marked == this_nmi) &&
	     (__get_cpu_var(pmu_nmi).handled > 1))) {
		/*
		 * We could have two subsequent back-to-back nmis: The
		 * first handles more than one counter, the 2nd
		 * handles only one counter and the 3rd handles no
		 * counter.
		 *
		 * This is the 2nd nmi because the previous was
		 * handling more than one counter. We will mark the
		 * next (3rd) and then drop it if unhandled.
		 */
		__get_cpu_var(pmu_nmi).marked	= this_nmi + 1;
		__get_cpu_var(pmu_nmi).handled	= handled;
	}

	return NOTIFY_STOP;
}

static __read_mostly struct notifier_block perf_event_nmi_notifier = {
	.notifier_call		= perf_event_nmi_handler,
	.next			= NULL,
	.priority		= 1
};

static struct event_constraint unconstrained;
static struct event_constraint emptyconstraint;

static struct event_constraint *
x86_get_event_constraints(struct cpu_hw_events *cpuc, struct perf_event *event)
{
	struct event_constraint *c;

	if (x86_pmu.event_constraints) {
		for_each_event_constraint(c, x86_pmu.event_constraints) {
			if ((event->hw.config & c->cmask) == c->code)
				return c;
		}
	}

	return &unconstrained;
}

#include "perf_event_amd.c"
#include "perf_event_p6.c"
#include "perf_event_p4.c"
#include "perf_event_intel_lbr.c"
#include "perf_event_intel_ds.c"
#include "perf_event_intel.c"

static int __cpuinit
x86_pmu_notifier(struct notifier_block *self, unsigned long action, void *hcpu)
{
	unsigned int cpu = (long)hcpu;
	int ret = NOTIFY_OK;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		if (x86_pmu.cpu_prepare)
			ret = x86_pmu.cpu_prepare(cpu);
		break;

	case CPU_STARTING:
		if (x86_pmu.cpu_starting)
			x86_pmu.cpu_starting(cpu);
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

void __init init_hw_perf_events(void)
{
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
		return;
	}
	if (err != 0) {
		pr_cont("no PMU driver, software events only.\n");
		return;
	}

	pmu_check_apic();

	pr_cont("%s PMU driver.\n", x86_pmu.name);

	if (x86_pmu.quirks)
		x86_pmu.quirks();

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
	register_die_notifier(&perf_event_nmi_notifier);

	unconstrained = (struct event_constraint)
		__EVENT_CONSTRAINT(0, (1ULL << x86_pmu.num_counters) - 1,
				   0, x86_pmu.num_counters);

	if (x86_pmu.event_constraints) {
		for_each_event_constraint(c, x86_pmu.event_constraints) {
			if (c->cmask != X86_RAW_EVENT_MASK)
				continue;

			c->idxmsk64 |= (1ULL << x86_pmu.num_counters) - 1;
			c->weight += x86_pmu.num_counters;
		}
	}

	pr_info("... version:                %d\n",     x86_pmu.version);
	pr_info("... bit width:              %d\n",     x86_pmu.cntval_bits);
	pr_info("... generic registers:      %d\n",     x86_pmu.num_counters);
	pr_info("... value mask:             %016Lx\n", x86_pmu.cntval_mask);
	pr_info("... max period:             %016Lx\n", x86_pmu.max_period);
	pr_info("... fixed-purpose events:   %d\n",     x86_pmu.num_counters_fixed);
	pr_info("... event mask:             %016Lx\n", x86_pmu.intel_ctrl);

	perf_pmu_register(&pmu);
	perf_cpu_notifier(x86_pmu_notifier);
}

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
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	perf_pmu_disable(pmu);
	cpuc->group_flag |= PERF_EVENT_TXN;
	cpuc->n_txn = 0;
}

/*
 * Stop group events scheduling transaction
 * Clear the flag and pmu::enable() will perform the
 * schedulability test.
 */
static void x86_pmu_cancel_txn(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	cpuc->group_flag &= ~PERF_EVENT_TXN;
	/*
	 * Truncate the collected events.
	 */
	cpuc->n_added -= cpuc->n_txn;
	cpuc->n_events -= cpuc->n_txn;
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
 * validate that we can schedule this event
 */
static int validate_event(struct perf_event *event)
{
	struct cpu_hw_events *fake_cpuc;
	struct event_constraint *c;
	int ret = 0;

	fake_cpuc = kmalloc(sizeof(*fake_cpuc), GFP_KERNEL | __GFP_ZERO);
	if (!fake_cpuc)
		return -ENOMEM;

	c = x86_pmu.get_event_constraints(fake_cpuc, event);

	if (!c || !c->weight)
		ret = -ENOSPC;

	if (x86_pmu.put_event_constraints)
		x86_pmu.put_event_constraints(fake_cpuc, event);

	kfree(fake_cpuc);

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
	int ret, n;

	ret = -ENOMEM;
	fake_cpuc = kmalloc(sizeof(*fake_cpuc), GFP_KERNEL | __GFP_ZERO);
	if (!fake_cpuc)
		goto out;

	/*
	 * the event is not yet connected with its
	 * siblings therefore we must first collect
	 * existing siblings, then add the new event
	 * before we can simulate the scheduling
	 */
	ret = -ENOSPC;
	n = collect_events(fake_cpuc, leader, true);
	if (n < 0)
		goto out_free;

	fake_cpuc->n_events = n;
	n = collect_events(fake_cpuc, event, false);
	if (n < 0)
		goto out_free;

	fake_cpuc->n_events = n;

	ret = x86_pmu.schedule_events(fake_cpuc, n, NULL);

out_free:
	kfree(fake_cpuc);
out:
	return ret;
}

int x86_pmu_event_init(struct perf_event *event)
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

static struct pmu pmu = {
	.pmu_enable	= x86_pmu_enable,
	.pmu_disable	= x86_pmu_disable,

	.event_init	= x86_pmu_event_init,

	.add		= x86_pmu_add,
	.del		= x86_pmu_del,
	.start		= x86_pmu_start,
	.stop		= x86_pmu_stop,
	.read		= x86_pmu_read,

	.start_txn	= x86_pmu_start_txn,
	.cancel_txn	= x86_pmu_cancel_txn,
	.commit_txn	= x86_pmu_commit_txn,
};

/*
 * callchain support
 */

static void
backtrace_warning_symbol(void *data, char *msg, unsigned long symbol)
{
	/* Ignore warnings */
}

static void backtrace_warning(void *data, char *msg)
{
	/* Ignore warnings */
}

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
	.warning		= backtrace_warning,
	.warning_symbol		= backtrace_warning_symbol,
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

	dump_trace(NULL, regs, NULL, regs->bp, &backtrace_ops, entry);
}

#ifdef CONFIG_COMPAT
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

		if (fp < compat_ptr(regs->sp))
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

	if (perf_callchain_user32(regs, entry))
		return;

	while (entry->nr < PERF_MAX_STACK_DEPTH) {
		unsigned long bytes;
		frame.next_frame	     = NULL;
		frame.return_address = 0;

		bytes = copy_from_user_nmi(&frame, fp, sizeof(frame));
		if (bytes != sizeof(frame))
			break;

		if ((unsigned long)fp < regs->sp)
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
		if (user_mode(regs))
			misc |= PERF_RECORD_MISC_USER;
		else
			misc |= PERF_RECORD_MISC_KERNEL;
	}

	if (regs->flags & PERF_EFLAGS_EXACT)
		misc |= PERF_RECORD_MISC_EXACT_IP;

	return misc;
}
