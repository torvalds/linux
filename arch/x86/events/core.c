/*
 * Performance events x86 architecture code
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2009 Jaswinder Singh Rajput
 *  Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 *  Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra
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
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kdebug.h>
#include <linux/sched/mm.h>
#include <linux/sched/clock.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/nospec.h>
#include <linux/static_call.h>

#include <asm/apic.h>
#include <asm/stacktrace.h>
#include <asm/nmi.h>
#include <asm/smp.h>
#include <asm/alternative.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/timer.h>
#include <asm/desc.h>
#include <asm/ldt.h>
#include <asm/unwind.h>

#include "perf_event.h"

struct x86_pmu x86_pmu __read_mostly;
static struct pmu pmu;

DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events) = {
	.enabled = 1,
	.pmu = &pmu,
};

DEFINE_STATIC_KEY_FALSE(rdpmc_never_available_key);
DEFINE_STATIC_KEY_FALSE(rdpmc_always_available_key);
DEFINE_STATIC_KEY_FALSE(perf_is_hybrid);

/*
 * This here uses DEFINE_STATIC_CALL_NULL() to get a static_call defined
 * from just a typename, as opposed to an actual function.
 */
DEFINE_STATIC_CALL_NULL(x86_pmu_handle_irq,  *x86_pmu.handle_irq);
DEFINE_STATIC_CALL_NULL(x86_pmu_disable_all, *x86_pmu.disable_all);
DEFINE_STATIC_CALL_NULL(x86_pmu_enable_all,  *x86_pmu.enable_all);
DEFINE_STATIC_CALL_NULL(x86_pmu_enable,	     *x86_pmu.enable);
DEFINE_STATIC_CALL_NULL(x86_pmu_disable,     *x86_pmu.disable);

DEFINE_STATIC_CALL_NULL(x86_pmu_add,  *x86_pmu.add);
DEFINE_STATIC_CALL_NULL(x86_pmu_del,  *x86_pmu.del);
DEFINE_STATIC_CALL_NULL(x86_pmu_read, *x86_pmu.read);

DEFINE_STATIC_CALL_NULL(x86_pmu_schedule_events,       *x86_pmu.schedule_events);
DEFINE_STATIC_CALL_NULL(x86_pmu_get_event_constraints, *x86_pmu.get_event_constraints);
DEFINE_STATIC_CALL_NULL(x86_pmu_put_event_constraints, *x86_pmu.put_event_constraints);

DEFINE_STATIC_CALL_NULL(x86_pmu_start_scheduling,  *x86_pmu.start_scheduling);
DEFINE_STATIC_CALL_NULL(x86_pmu_commit_scheduling, *x86_pmu.commit_scheduling);
DEFINE_STATIC_CALL_NULL(x86_pmu_stop_scheduling,   *x86_pmu.stop_scheduling);

DEFINE_STATIC_CALL_NULL(x86_pmu_sched_task,    *x86_pmu.sched_task);
DEFINE_STATIC_CALL_NULL(x86_pmu_swap_task_ctx, *x86_pmu.swap_task_ctx);

DEFINE_STATIC_CALL_NULL(x86_pmu_drain_pebs,   *x86_pmu.drain_pebs);
DEFINE_STATIC_CALL_NULL(x86_pmu_pebs_aliases, *x86_pmu.pebs_aliases);

/*
 * This one is magic, it will get called even when PMU init fails (because
 * there is no PMU), in which case it should simply return NULL.
 */
DEFINE_STATIC_CALL_RET0(x86_pmu_guest_get_msrs, *x86_pmu.guest_get_msrs);

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
	u64 delta;

	if (unlikely(!hwc->event_base))
		return 0;

	if (unlikely(is_topdown_count(event)) && x86_pmu.update_topdown_event)
		return x86_pmu.update_topdown_event(event);

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
	struct extra_reg *extra_regs = hybrid(event->pmu, extra_regs);
	struct hw_perf_event_extra *reg;
	struct extra_reg *er;

	reg = &event->hw.extra_reg;

	if (!extra_regs)
		return 0;

	for (er = extra_regs; er->msr; er++) {
		if (er->event != (config & er->config_mask))
			continue;
		if (event->attr.config1 & ~er->valid_mask)
			return -EINVAL;
		/* Check if the extra msrs can be safely accessed*/
		if (!er->extra_msr_access)
			return -ENXIO;

		reg->idx = er->idx;
		reg->config = event->attr.config1;
		reg->reg = er->msr;
		break;
	}
	return 0;
}

static atomic_t active_events;
static atomic_t pmc_refcount;
static DEFINE_MUTEX(pmc_reserve_mutex);

#ifdef CONFIG_X86_LOCAL_APIC

static inline int get_possible_num_counters(void)
{
	int i, num_counters = x86_pmu.num_counters;

	if (!is_hybrid())
		return num_counters;

	for (i = 0; i < x86_pmu.num_hybrid_pmus; i++)
		num_counters = max_t(int, num_counters, x86_pmu.hybrid_pmu[i].num_counters);

	return num_counters;
}

static bool reserve_pmc_hardware(void)
{
	int i, num_counters = get_possible_num_counters();

	for (i = 0; i < num_counters; i++) {
		if (!reserve_perfctr_nmi(x86_pmu_event_addr(i)))
			goto perfctr_fail;
	}

	for (i = 0; i < num_counters; i++) {
		if (!reserve_evntsel_nmi(x86_pmu_config_addr(i)))
			goto eventsel_fail;
	}

	return true;

eventsel_fail:
	for (i--; i >= 0; i--)
		release_evntsel_nmi(x86_pmu_config_addr(i));

	i = num_counters;

perfctr_fail:
	for (i--; i >= 0; i--)
		release_perfctr_nmi(x86_pmu_event_addr(i));

	return false;
}

static void release_pmc_hardware(void)
{
	int i, num_counters = get_possible_num_counters();

	for (i = 0; i < num_counters; i++) {
		release_perfctr_nmi(x86_pmu_event_addr(i));
		release_evntsel_nmi(x86_pmu_config_addr(i));
	}
}

#else

static bool reserve_pmc_hardware(void) { return true; }
static void release_pmc_hardware(void) {}

#endif

bool check_hw_exists(struct pmu *pmu, int num_counters, int num_counters_fixed)
{
	u64 val, val_fail = -1, val_new= ~0;
	int i, reg, reg_fail = -1, ret = 0;
	int bios_fail = 0;
	int reg_safe = -1;

	/*
	 * Check to see if the BIOS enabled any of the counters, if so
	 * complain and bail.
	 */
	for (i = 0; i < num_counters; i++) {
		reg = x86_pmu_config_addr(i);
		ret = rdmsrl_safe(reg, &val);
		if (ret)
			goto msr_fail;
		if (val & ARCH_PERFMON_EVENTSEL_ENABLE) {
			bios_fail = 1;
			val_fail = val;
			reg_fail = reg;
		} else {
			reg_safe = i;
		}
	}

	if (num_counters_fixed) {
		reg = MSR_ARCH_PERFMON_FIXED_CTR_CTRL;
		ret = rdmsrl_safe(reg, &val);
		if (ret)
			goto msr_fail;
		for (i = 0; i < num_counters_fixed; i++) {
			if (fixed_counter_disabled(i, pmu))
				continue;
			if (val & (0x03ULL << i*4)) {
				bios_fail = 1;
				val_fail = val;
				reg_fail = reg;
			}
		}
	}

	/*
	 * If all the counters are enabled, the below test will always
	 * fail.  The tools will also become useless in this scenario.
	 * Just fail and disable the hardware counters.
	 */

	if (reg_safe == -1) {
		reg = reg_safe;
		goto msr_fail;
	}

	/*
	 * Read the current value, change it and read it back to see if it
	 * matches, this is needed to detect certain hardware emulators
	 * (qemu/kvm) that don't trap on the MSR access and always return 0s.
	 */
	reg = x86_pmu_event_addr(reg_safe);
	if (rdmsrl_safe(reg, &val))
		goto msr_fail;
	val ^= 0xffffUL;
	ret = wrmsrl_safe(reg, val);
	ret |= rdmsrl_safe(reg, &val_new);
	if (ret || val != val_new)
		goto msr_fail;

	/*
	 * We still allow the PMU driver to operate:
	 */
	if (bios_fail) {
		pr_cont("Broken BIOS detected, complain to your hardware vendor.\n");
		pr_err(FW_BUG "the BIOS has corrupted hw-PMU resources (MSR %x is %Lx)\n",
			      reg_fail, val_fail);
	}

	return true;

msr_fail:
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		pr_cont("PMU not available due to virtualization, using software events only.\n");
	} else {
		pr_cont("Broken PMU hardware detected, using software events only.\n");
		pr_err("Failed to access perfctr msr (MSR %x is %Lx)\n",
		       reg, val_new);
	}

	return false;
}

static void hw_perf_event_destroy(struct perf_event *event)
{
	x86_release_hardware();
	atomic_dec(&active_events);
}

void hw_perf_lbr_event_destroy(struct perf_event *event)
{
	hw_perf_event_destroy(event);

	/* undo the lbr/bts event accounting */
	x86_del_exclusive(x86_lbr_exclusive_lbr);
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

	cache_type = (config >> 0) & 0xff;
	if (cache_type >= PERF_COUNT_HW_CACHE_MAX)
		return -EINVAL;
	cache_type = array_index_nospec(cache_type, PERF_COUNT_HW_CACHE_MAX);

	cache_op = (config >>  8) & 0xff;
	if (cache_op >= PERF_COUNT_HW_CACHE_OP_MAX)
		return -EINVAL;
	cache_op = array_index_nospec(cache_op, PERF_COUNT_HW_CACHE_OP_MAX);

	cache_result = (config >> 16) & 0xff;
	if (cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;
	cache_result = array_index_nospec(cache_result, PERF_COUNT_HW_CACHE_RESULT_MAX);

	val = hybrid_var(event->pmu, hw_cache_event_ids)[cache_type][cache_op][cache_result];
	if (val == 0)
		return -ENOENT;

	if (val == -1)
		return -EINVAL;

	hwc->config |= val;
	attr->config1 = hybrid_var(event->pmu, hw_cache_extra_regs)[cache_type][cache_op][cache_result];
	return x86_pmu_extra_regs(val, event);
}

int x86_reserve_hardware(void)
{
	int err = 0;

	if (!atomic_inc_not_zero(&pmc_refcount)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&pmc_refcount) == 0) {
			if (!reserve_pmc_hardware()) {
				err = -EBUSY;
			} else {
				reserve_ds_buffers();
				reserve_lbr_buffers();
			}
		}
		if (!err)
			atomic_inc(&pmc_refcount);
		mutex_unlock(&pmc_reserve_mutex);
	}

	return err;
}

void x86_release_hardware(void)
{
	if (atomic_dec_and_mutex_lock(&pmc_refcount, &pmc_reserve_mutex)) {
		release_pmc_hardware();
		release_ds_buffers();
		release_lbr_buffers();
		mutex_unlock(&pmc_reserve_mutex);
	}
}

/*
 * Check if we can create event of a certain type (that no conflicting events
 * are present).
 */
int x86_add_exclusive(unsigned int what)
{
	int i;

	/*
	 * When lbr_pt_coexist we allow PT to coexist with either LBR or BTS.
	 * LBR and BTS are still mutually exclusive.
	 */
	if (x86_pmu.lbr_pt_coexist && what == x86_lbr_exclusive_pt)
		goto out;

	if (!atomic_inc_not_zero(&x86_pmu.lbr_exclusive[what])) {
		mutex_lock(&pmc_reserve_mutex);
		for (i = 0; i < ARRAY_SIZE(x86_pmu.lbr_exclusive); i++) {
			if (i != what && atomic_read(&x86_pmu.lbr_exclusive[i]))
				goto fail_unlock;
		}
		atomic_inc(&x86_pmu.lbr_exclusive[what]);
		mutex_unlock(&pmc_reserve_mutex);
	}

out:
	atomic_inc(&active_events);
	return 0;

fail_unlock:
	mutex_unlock(&pmc_reserve_mutex);
	return -EBUSY;
}

void x86_del_exclusive(unsigned int what)
{
	atomic_dec(&active_events);

	/*
	 * See the comment in x86_add_exclusive().
	 */
	if (x86_pmu.lbr_pt_coexist && what == x86_lbr_exclusive_pt)
		return;

	atomic_dec(&x86_pmu.lbr_exclusive[what]);
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
	}

	if (attr->type == event->pmu->type)
		return x86_pmu_extra_regs(event->attr.config, event);

	if (attr->type == PERF_TYPE_HW_CACHE)
		return set_ext_hw_attr(hwc, event);

	if (attr->config >= x86_pmu.max_events)
		return -EINVAL;

	attr->config = array_index_nospec((unsigned long)attr->config, x86_pmu.max_events);

	/*
	 * The generic map:
	 */
	config = x86_pmu.event_map(attr->config);

	if (config == 0)
		return -ENOENT;

	if (config == -1LL)
		return -EINVAL;

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

int x86_pmu_max_precise(void)
{
	int precise = 0;

	/* Support for constant skid */
	if (x86_pmu.pebs_active && !x86_pmu.pebs_broken) {
		precise++;

		/* Support for IP fixup */
		if (x86_pmu.lbr_nr || x86_pmu.intel_cap.pebs_format >= 2)
			precise++;

		if (x86_pmu.pebs_prec_dist)
			precise++;
	}
	return precise;
}

int x86_pmu_hw_config(struct perf_event *event)
{
	if (event->attr.precise_ip) {
		int precise = x86_pmu_max_precise();

		if (event->attr.precise_ip > precise)
			return -EOPNOTSUPP;

		/* There's no sense in having PEBS for non sampling events: */
		if (!is_sampling_event(event))
			return -EINVAL;
	}
	/*
	 * check that PEBS LBR correction does not conflict with
	 * whatever the user is asking with attr->branch_sample_type
	 */
	if (event->attr.precise_ip > 1 && x86_pmu.intel_cap.pebs_format < 2) {
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

	if (event->attr.branch_sample_type & PERF_SAMPLE_BRANCH_CALL_STACK)
		event->attach_state |= PERF_ATTACH_TASK_DATA;

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

	if (event->attr.type == event->pmu->type)
		event->hw.config |= event->attr.config & X86_RAW_EVENT_MASK;

	if (event->attr.sample_period && x86_pmu.limit_period) {
		if (x86_pmu.limit_period(event, event->attr.sample_period) >
				event->attr.sample_period)
			return -EINVAL;
	}

	/* sample_regs_user never support XMM registers */
	if (unlikely(event->attr.sample_regs_user & PERF_REG_EXTENDED_MASK))
		return -EINVAL;
	/*
	 * Besides the general purpose registers, XMM registers may
	 * be collected in PEBS on some platforms, e.g. Icelake
	 */
	if (unlikely(event->attr.sample_regs_intr & PERF_REG_EXTENDED_MASK)) {
		if (!(event->pmu->capabilities & PERF_PMU_CAP_EXTENDED_REGS))
			return -EINVAL;

		if (!event->attr.precise_ip)
			return -EINVAL;
	}

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

	err = x86_reserve_hardware();
	if (err)
		return err;

	atomic_inc(&active_events);
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
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int idx;

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		struct hw_perf_event *hwc = &cpuc->events[idx]->hw;
		u64 val;

		if (!test_bit(idx, cpuc->active_mask))
			continue;
		rdmsrl(x86_pmu_config_addr(idx), val);
		if (!(val & ARCH_PERFMON_EVENTSEL_ENABLE))
			continue;
		val &= ~ARCH_PERFMON_EVENTSEL_ENABLE;
		wrmsrl(x86_pmu_config_addr(idx), val);
		if (is_counter_pair(hwc))
			wrmsrl(x86_pmu_config_addr(idx + 1), 0);
	}
}

struct perf_guest_switch_msr *perf_guest_get_msrs(int *nr)
{
	return static_call(x86_pmu_guest_get_msrs)(nr);
}
EXPORT_SYMBOL_GPL(perf_guest_get_msrs);

/*
 * There may be PMI landing after enabled=0. The PMI hitting could be before or
 * after disable_all.
 *
 * If PMI hits before disable_all, the PMU will be disabled in the NMI handler.
 * It will not be re-enabled in the NMI handler again, because enabled=0. After
 * handling the NMI, disable_all will be called, which will not change the
 * state either. If PMI hits after disable_all, the PMU is already disabled
 * before entering NMI handler. The NMI handler will not change the state
 * either.
 *
 * So either situation is harmless.
 */
static void x86_pmu_disable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (!x86_pmu_initialized())
		return;

	if (!cpuc->enabled)
		return;

	cpuc->n_added = 0;
	cpuc->enabled = 0;
	barrier();

	static_call(x86_pmu_disable_all)();
}

void x86_pmu_enable_all(int added)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int idx;

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		struct hw_perf_event *hwc = &cpuc->events[idx]->hw;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		__x86_pmu_enable_event(hwc, ARCH_PERFMON_EVENTSEL_ENABLE);
	}
}

static inline int is_x86_event(struct perf_event *event)
{
	int i;

	if (!is_hybrid())
		return event->pmu == &pmu;

	for (i = 0; i < x86_pmu.num_hybrid_pmus; i++) {
		if (event->pmu == &x86_pmu.hybrid_pmu[i].pmu)
			return true;
	}

	return false;
}

struct pmu *x86_get_pmu(unsigned int cpu)
{
	struct cpu_hw_events *cpuc = &per_cpu(cpu_hw_events, cpu);

	/*
	 * All CPUs of the hybrid type have been offline.
	 * The x86_get_pmu() should not be invoked.
	 */
	if (WARN_ON_ONCE(!cpuc->pmu))
		return &pmu;

	return cpuc->pmu;
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
	int	nr_gp;		/* number of GP counters used */
	u64	used;
};

/* Total max is X86_PMC_IDX_MAX, but we are O(n!) limited */
#define	SCHED_STATES_MAX	2

struct perf_sched {
	int			max_weight;
	int			max_events;
	int			max_gp;
	int			saved_states;
	struct event_constraint	**constraints;
	struct sched_state	state;
	struct sched_state	saved[SCHED_STATES_MAX];
};

/*
 * Initialize iterator that runs through all events and counters.
 */
static void perf_sched_init(struct perf_sched *sched, struct event_constraint **constraints,
			    int num, int wmin, int wmax, int gpmax)
{
	int idx;

	memset(sched, 0, sizeof(*sched));
	sched->max_events	= num;
	sched->max_weight	= wmax;
	sched->max_gp		= gpmax;
	sched->constraints	= constraints;

	for (idx = 0; idx < num; idx++) {
		if (constraints[idx]->weight == wmin)
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

	/* this assignment didn't work out */
	/* XXX broken vs EVENT_PAIR */
	sched->state.used &= ~BIT_ULL(sched->state.counter);

	/* try the next one */
	sched->state.counter++;

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
	if (c->idxmsk64 & (~0ULL << INTEL_PMC_IDX_FIXED)) {
		idx = INTEL_PMC_IDX_FIXED;
		for_each_set_bit_from(idx, c->idxmsk, X86_PMC_IDX_MAX) {
			u64 mask = BIT_ULL(idx);

			if (sched->state.used & mask)
				continue;

			sched->state.used |= mask;
			goto done;
		}
	}

	/* Grab the first unused counter starting with idx */
	idx = sched->state.counter;
	for_each_set_bit_from(idx, c->idxmsk, INTEL_PMC_IDX_FIXED) {
		u64 mask = BIT_ULL(idx);

		if (c->flags & PERF_X86_EVENT_PAIR)
			mask |= mask << 1;

		if (sched->state.used & mask)
			continue;

		if (sched->state.nr_gp++ >= sched->max_gp)
			return false;

		sched->state.used |= mask;
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
			int wmin, int wmax, int gpmax, int *assign)
{
	struct perf_sched sched;

	perf_sched_init(&sched, constraints, n, wmin, wmax, gpmax);

	do {
		if (!perf_sched_find_counter(&sched))
			break;	/* failed */
		if (assign)
			assign[sched.state.event] = sched.state.counter;
	} while (perf_sched_next_event(&sched));

	return sched.state.unassigned;
}
EXPORT_SYMBOL_GPL(perf_assign_events);

int x86_schedule_events(struct cpu_hw_events *cpuc, int n, int *assign)
{
	int num_counters = hybrid(cpuc->pmu, num_counters);
	struct event_constraint *c;
	struct perf_event *e;
	int n0, i, wmin, wmax, unsched = 0;
	struct hw_perf_event *hwc;
	u64 used_mask = 0;

	/*
	 * Compute the number of events already present; see x86_pmu_add(),
	 * validate_group() and x86_pmu_commit_txn(). For the former two
	 * cpuc->n_events hasn't been updated yet, while for the latter
	 * cpuc->n_txn contains the number of events added in the current
	 * transaction.
	 */
	n0 = cpuc->n_events;
	if (cpuc->txn_flags & PERF_PMU_TXN_ADD)
		n0 -= cpuc->n_txn;

	static_call_cond(x86_pmu_start_scheduling)(cpuc);

	for (i = 0, wmin = X86_PMC_IDX_MAX, wmax = 0; i < n; i++) {
		c = cpuc->event_constraint[i];

		/*
		 * Previously scheduled events should have a cached constraint,
		 * while new events should not have one.
		 */
		WARN_ON_ONCE((c && i >= n0) || (!c && i < n0));

		/*
		 * Request constraints for new events; or for those events that
		 * have a dynamic constraint -- for those the constraint can
		 * change due to external factors (sibling state, allow_tfa).
		 */
		if (!c || (c->flags & PERF_X86_EVENT_DYNAMIC)) {
			c = static_call(x86_pmu_get_event_constraints)(cpuc, i, cpuc->event_list[i]);
			cpuc->event_constraint[i] = c;
		}

		wmin = min(wmin, c->weight);
		wmax = max(wmax, c->weight);
	}

	/*
	 * fastpath, try to reuse previous register
	 */
	for (i = 0; i < n; i++) {
		u64 mask;

		hwc = &cpuc->event_list[i]->hw;
		c = cpuc->event_constraint[i];

		/* never assigned */
		if (hwc->idx == -1)
			break;

		/* constraint still honored */
		if (!test_bit(hwc->idx, c->idxmsk))
			break;

		mask = BIT_ULL(hwc->idx);
		if (is_counter_pair(hwc))
			mask |= mask << 1;

		/* not already used */
		if (used_mask & mask)
			break;

		used_mask |= mask;

		if (assign)
			assign[i] = hwc->idx;
	}

	/* slow path */
	if (i != n) {
		int gpmax = num_counters;

		/*
		 * Do not allow scheduling of more than half the available
		 * generic counters.
		 *
		 * This helps avoid counter starvation of sibling thread by
		 * ensuring at most half the counters cannot be in exclusive
		 * mode. There is no designated counters for the limits. Any
		 * N/2 counters can be used. This helps with events with
		 * specific counter constraints.
		 */
		if (is_ht_workaround_enabled() && !cpuc->is_fake &&
		    READ_ONCE(cpuc->excl_cntrs->exclusive_present))
			gpmax /= 2;

		/*
		 * Reduce the amount of available counters to allow fitting
		 * the extra Merge events needed by large increment events.
		 */
		if (x86_pmu.flags & PMU_FL_PAIR) {
			gpmax = num_counters - cpuc->n_pair;
			WARN_ON(gpmax <= 0);
		}

		unsched = perf_assign_events(cpuc->event_constraint, n, wmin,
					     wmax, gpmax, assign);
	}

	/*
	 * In case of success (unsched = 0), mark events as committed,
	 * so we do not put_constraint() in case new events are added
	 * and fail to be scheduled
	 *
	 * We invoke the lower level commit callback to lock the resource
	 *
	 * We do not need to do all of this in case we are called to
	 * validate an event group (assign == NULL)
	 */
	if (!unsched && assign) {
		for (i = 0; i < n; i++) {
			e = cpuc->event_list[i];
			static_call_cond(x86_pmu_commit_scheduling)(cpuc, i, assign[i]);
		}
	} else {
		for (i = n0; i < n; i++) {
			e = cpuc->event_list[i];

			/*
			 * release events that failed scheduling
			 */
			static_call_cond(x86_pmu_put_event_constraints)(cpuc, e);

			cpuc->event_constraint[i] = NULL;
		}
	}

	static_call_cond(x86_pmu_stop_scheduling)(cpuc);

	return unsched ? -EINVAL : 0;
}

static int add_nr_metric_event(struct cpu_hw_events *cpuc,
			       struct perf_event *event)
{
	if (is_metric_event(event)) {
		if (cpuc->n_metric == INTEL_TD_METRIC_NUM)
			return -EINVAL;
		cpuc->n_metric++;
		cpuc->n_txn_metric++;
	}

	return 0;
}

static void del_nr_metric_event(struct cpu_hw_events *cpuc,
				struct perf_event *event)
{
	if (is_metric_event(event))
		cpuc->n_metric--;
}

static int collect_event(struct cpu_hw_events *cpuc, struct perf_event *event,
			 int max_count, int n)
{
	union perf_capabilities intel_cap = hybrid(cpuc->pmu, intel_cap);

	if (intel_cap.perf_metrics && add_nr_metric_event(cpuc, event))
		return -EINVAL;

	if (n >= max_count + cpuc->n_metric)
		return -EINVAL;

	cpuc->event_list[n] = event;
	if (is_counter_pair(&event->hw)) {
		cpuc->n_pair++;
		cpuc->n_txn_pair++;
	}

	return 0;
}

/*
 * dogrp: true if must collect siblings events (group)
 * returns total number of events and error code
 */
static int collect_events(struct cpu_hw_events *cpuc, struct perf_event *leader, bool dogrp)
{
	int num_counters = hybrid(cpuc->pmu, num_counters);
	int num_counters_fixed = hybrid(cpuc->pmu, num_counters_fixed);
	struct perf_event *event;
	int n, max_count;

	max_count = num_counters + num_counters_fixed;

	/* current number of events already accepted */
	n = cpuc->n_events;
	if (!cpuc->n_events)
		cpuc->pebs_output = 0;

	if (!cpuc->is_fake && leader->attr.precise_ip) {
		/*
		 * For PEBS->PT, if !aux_event, the group leader (PT) went
		 * away, the group was broken down and this singleton event
		 * can't schedule any more.
		 */
		if (is_pebs_pt(leader) && !leader->aux_event)
			return -EINVAL;

		/*
		 * pebs_output: 0: no PEBS so far, 1: PT, 2: DS
		 */
		if (cpuc->pebs_output &&
		    cpuc->pebs_output != is_pebs_pt(leader) + 1)
			return -EINVAL;

		cpuc->pebs_output = is_pebs_pt(leader) + 1;
	}

	if (is_x86_event(leader)) {
		if (collect_event(cpuc, leader, max_count, n))
			return -EINVAL;
		n++;
	}

	if (!dogrp)
		return n;

	for_each_sibling_event(event, leader) {
		if (!is_x86_event(event) || event->state <= PERF_EVENT_STATE_OFF)
			continue;

		if (collect_event(cpuc, event, max_count, n))
			return -EINVAL;

		n++;
	}
	return n;
}

static inline void x86_assign_hw_event(struct perf_event *event,
				struct cpu_hw_events *cpuc, int i)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	idx = hwc->idx = cpuc->assign[i];
	hwc->last_cpu = smp_processor_id();
	hwc->last_tag = ++cpuc->tags[i];

	switch (hwc->idx) {
	case INTEL_PMC_IDX_FIXED_BTS:
	case INTEL_PMC_IDX_FIXED_VLBR:
		hwc->config_base = 0;
		hwc->event_base	= 0;
		break;

	case INTEL_PMC_IDX_METRIC_BASE ... INTEL_PMC_IDX_METRIC_END:
		/* All the metric events are mapped onto the fixed counter 3. */
		idx = INTEL_PMC_IDX_FIXED_SLOTS;
		fallthrough;
	case INTEL_PMC_IDX_FIXED ... INTEL_PMC_IDX_FIXED_BTS-1:
		hwc->config_base = MSR_ARCH_PERFMON_FIXED_CTR_CTRL;
		hwc->event_base = MSR_ARCH_PERFMON_FIXED_CTR0 +
				(idx - INTEL_PMC_IDX_FIXED);
		hwc->event_base_rdpmc = (idx - INTEL_PMC_IDX_FIXED) |
					INTEL_PMC_FIXED_RDPMC_BASE;
		break;

	default:
		hwc->config_base = x86_pmu_config_addr(hwc->idx);
		hwc->event_base  = x86_pmu_event_addr(hwc->idx);
		hwc->event_base_rdpmc = x86_pmu_rdpmc_index(hwc->idx);
		break;
	}
}

/**
 * x86_perf_rdpmc_index - Return PMC counter used for event
 * @event: the perf_event to which the PMC counter was assigned
 *
 * The counter assigned to this performance event may change if interrupts
 * are enabled. This counter should thus never be used while interrupts are
 * enabled. Before this function is used to obtain the assigned counter the
 * event should be checked for validity using, for example,
 * perf_event_read_local(), within the same interrupt disabled section in
 * which this counter is planned to be used.
 *
 * Return: The index of the performance monitoring counter assigned to
 * @perf_event.
 */
int x86_perf_rdpmc_index(struct perf_event *event)
{
	lockdep_assert_irqs_disabled();

	return event->hw.event_base_rdpmc;
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
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
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

		/*
		 * step2: reprogram moved events into new counters
		 */
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

	static_call(x86_pmu_enable_all)(added);
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

	if (unlikely(!hwc->event_base))
		return 0;

	if (unlikely(is_topdown_count(event)) &&
	    x86_pmu.set_topdown_event_period)
		return x86_pmu.set_topdown_event_period(event);

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

	if (x86_pmu.limit_period)
		left = x86_pmu.limit_period(event, left);

	per_cpu(pmc_prev_left[idx], smp_processor_id()) = left;

	/*
	 * The hw event starts counting from this event offset,
	 * mark it to be able to extra future deltas:
	 */
	local64_set(&hwc->prev_count, (u64)-left);

	wrmsrl(hwc->event_base, (u64)(-left) & x86_pmu.cntval_mask);

	/*
	 * Sign extend the Merge event counter's upper 16 bits since
	 * we currently declare a 48-bit counter width
	 */
	if (is_counter_pair(hwc))
		wrmsrl(x86_pmu_event_addr(idx + 1), 0xffff);

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
 * but only if it can be scheduled with existing events.
 */
static int x86_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc;
	int assign[X86_PMC_IDX_MAX];
	int n, n0, ret;

	hwc = &event->hw;

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
	 * at commit time (->commit_txn) as a whole.
	 *
	 * If commit fails, we'll call ->del() on all events
	 * for which ->add() was called.
	 */
	if (cpuc->txn_flags & PERF_PMU_TXN_ADD)
		goto done_collect;

	ret = static_call(x86_pmu_schedule_events)(cpuc, n, assign);
	if (ret)
		goto out;
	/*
	 * copy new assignment, now we know it is possible
	 * will be used by hw_perf_enable()
	 */
	memcpy(cpuc->assign, assign, n*sizeof(int));

done_collect:
	/*
	 * Commit the collect_events() state. See x86_pmu_del() and
	 * x86_pmu_*_txn().
	 */
	cpuc->n_events = n;
	cpuc->n_added += n - n0;
	cpuc->n_txn += n - n0;

	/*
	 * This is before x86_pmu_enable() will call x86_pmu_start(),
	 * so we enable LBRs before an event needs them etc..
	 */
	static_call_cond(x86_pmu_add)(event);

	ret = 0;
out:
	return ret;
}

static void x86_pmu_start(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
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
	static_call(x86_pmu_enable)(event);
	perf_event_update_userpage(event);
}

void perf_event_print_debug(void)
{
	u64 ctrl, status, overflow, pmc_ctrl, pmc_count, prev_left, fixed;
	u64 pebs, debugctl;
	int cpu = smp_processor_id();
	struct cpu_hw_events *cpuc = &per_cpu(cpu_hw_events, cpu);
	int num_counters = hybrid(cpuc->pmu, num_counters);
	int num_counters_fixed = hybrid(cpuc->pmu, num_counters_fixed);
	struct event_constraint *pebs_constraints = hybrid(cpuc->pmu, pebs_constraints);
	unsigned long flags;
	int idx;

	if (!num_counters)
		return;

	local_irq_save(flags);

	if (x86_pmu.version >= 2) {
		rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, ctrl);
		rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);
		rdmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, overflow);
		rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR_CTRL, fixed);

		pr_info("\n");
		pr_info("CPU#%d: ctrl:       %016llx\n", cpu, ctrl);
		pr_info("CPU#%d: status:     %016llx\n", cpu, status);
		pr_info("CPU#%d: overflow:   %016llx\n", cpu, overflow);
		pr_info("CPU#%d: fixed:      %016llx\n", cpu, fixed);
		if (pebs_constraints) {
			rdmsrl(MSR_IA32_PEBS_ENABLE, pebs);
			pr_info("CPU#%d: pebs:       %016llx\n", cpu, pebs);
		}
		if (x86_pmu.lbr_nr) {
			rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
			pr_info("CPU#%d: debugctl:   %016llx\n", cpu, debugctl);
		}
	}
	pr_info("CPU#%d: active:     %016llx\n", cpu, *(u64 *)cpuc->active_mask);

	for (idx = 0; idx < num_counters; idx++) {
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
	for (idx = 0; idx < num_counters_fixed; idx++) {
		if (fixed_counter_disabled(idx, cpuc->pmu))
			continue;
		rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR0 + idx, pmc_count);

		pr_info("CPU#%d: fixed-PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
	}
	local_irq_restore(flags);
}

void x86_pmu_stop(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	if (test_bit(hwc->idx, cpuc->active_mask)) {
		static_call(x86_pmu_disable)(event);
		__clear_bit(hwc->idx, cpuc->active_mask);
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
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	union perf_capabilities intel_cap = hybrid(cpuc->pmu, intel_cap);
	int i;

	/*
	 * If we're called during a txn, we only need to undo x86_pmu.add.
	 * The events never got scheduled and ->cancel_txn will truncate
	 * the event_list.
	 *
	 * XXX assumes any ->del() called during a TXN will only be on
	 * an event added during that same TXN.
	 */
	if (cpuc->txn_flags & PERF_PMU_TXN_ADD)
		goto do_del;

	__set_bit(event->hw.idx, cpuc->dirty);

	/*
	 * Not a TXN, therefore cleanup properly.
	 */
	x86_pmu_stop(event, PERF_EF_UPDATE);

	for (i = 0; i < cpuc->n_events; i++) {
		if (event == cpuc->event_list[i])
			break;
	}

	if (WARN_ON_ONCE(i == cpuc->n_events)) /* called ->del() without ->add() ? */
		return;

	/* If we have a newly added event; make sure to decrease n_added. */
	if (i >= cpuc->n_events - cpuc->n_added)
		--cpuc->n_added;

	static_call_cond(x86_pmu_put_event_constraints)(cpuc, event);

	/* Delete the array entry. */
	while (++i < cpuc->n_events) {
		cpuc->event_list[i-1] = cpuc->event_list[i];
		cpuc->event_constraint[i-1] = cpuc->event_constraint[i];
	}
	cpuc->event_constraint[i-1] = NULL;
	--cpuc->n_events;
	if (intel_cap.perf_metrics)
		del_nr_metric_event(cpuc, event);

	perf_event_update_userpage(event);

do_del:

	/*
	 * This is after x86_pmu_stop(); so we disable LBRs after any
	 * event can need them etc..
	 */
	static_call_cond(x86_pmu_del)(event);
}

int x86_pmu_handle_irq(struct pt_regs *regs)
{
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct perf_event *event;
	int idx, handled = 0;
	u64 val;

	cpuc = this_cpu_ptr(&cpu_hw_events);

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
		if (!test_bit(idx, cpuc->active_mask))
			continue;

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

static int
perf_event_nmi_handler(unsigned int cmd, struct pt_regs *regs)
{
	u64 start_clock;
	u64 finish_clock;
	int ret;

	/*
	 * All PMUs/events that share this PMI handler should make sure to
	 * increment active_events for their events.
	 */
	if (!atomic_read(&active_events))
		return NMI_DONE;

	start_clock = sched_clock();
	ret = static_call(x86_pmu_handle_irq)(regs);
	finish_clock = sched_clock();

	perf_sample_event_took(finish_clock - start_clock);

	return ret;
}
NOKPROBE_SYMBOL(perf_event_nmi_handler);

struct event_constraint emptyconstraint;
struct event_constraint unconstrained;

static int x86_pmu_prepare_cpu(unsigned int cpu)
{
	struct cpu_hw_events *cpuc = &per_cpu(cpu_hw_events, cpu);
	int i;

	for (i = 0 ; i < X86_PERF_KFREE_MAX; i++)
		cpuc->kfree_on_online[i] = NULL;
	if (x86_pmu.cpu_prepare)
		return x86_pmu.cpu_prepare(cpu);
	return 0;
}

static int x86_pmu_dead_cpu(unsigned int cpu)
{
	if (x86_pmu.cpu_dead)
		x86_pmu.cpu_dead(cpu);
	return 0;
}

static int x86_pmu_online_cpu(unsigned int cpu)
{
	struct cpu_hw_events *cpuc = &per_cpu(cpu_hw_events, cpu);
	int i;

	for (i = 0 ; i < X86_PERF_KFREE_MAX; i++) {
		kfree(cpuc->kfree_on_online[i]);
		cpuc->kfree_on_online[i] = NULL;
	}
	return 0;
}

static int x86_pmu_starting_cpu(unsigned int cpu)
{
	if (x86_pmu.cpu_starting)
		x86_pmu.cpu_starting(cpu);
	return 0;
}

static int x86_pmu_dying_cpu(unsigned int cpu)
{
	if (x86_pmu.cpu_dying)
		x86_pmu.cpu_dying(cpu);
	return 0;
}

static void __init pmu_check_apic(void)
{
	if (boot_cpu_has(X86_FEATURE_APIC))
		return;

	x86_pmu.apic = 0;
	pr_info("no APIC, boot with the \"lapic\" boot parameter to force-enable it.\n");
	pr_info("no hardware sampling interrupt available.\n");

	/*
	 * If we have a PMU initialized but no APIC
	 * interrupts, we cannot sample hardware
	 * events (user-space has to fall back and
	 * sample via a hrtimer based software event):
	 */
	pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;

}

static struct attribute_group x86_pmu_format_group __ro_after_init = {
	.name = "format",
	.attrs = NULL,
};

ssize_t events_sysfs_show(struct device *dev, struct device_attribute *attr, char *page)
{
	struct perf_pmu_events_attr *pmu_attr =
		container_of(attr, struct perf_pmu_events_attr, attr);
	u64 config = 0;

	if (pmu_attr->id < x86_pmu.max_events)
		config = x86_pmu.event_map(pmu_attr->id);

	/* string trumps id */
	if (pmu_attr->event_str)
		return sprintf(page, "%s", pmu_attr->event_str);

	return x86_pmu.events_sysfs_show(page, config);
}
EXPORT_SYMBOL_GPL(events_sysfs_show);

ssize_t events_ht_sysfs_show(struct device *dev, struct device_attribute *attr,
			  char *page)
{
	struct perf_pmu_events_ht_attr *pmu_attr =
		container_of(attr, struct perf_pmu_events_ht_attr, attr);

	/*
	 * Report conditional events depending on Hyper-Threading.
	 *
	 * This is overly conservative as usually the HT special
	 * handling is not needed if the other CPU thread is idle.
	 *
	 * Note this does not (and cannot) handle the case when thread
	 * siblings are invisible, for example with virtualization
	 * if they are owned by some other guest.  The user tool
	 * has to re-read when a thread sibling gets onlined later.
	 */
	return sprintf(page, "%s",
			topology_max_smt_threads() > 1 ?
			pmu_attr->event_str_ht :
			pmu_attr->event_str_noht);
}

ssize_t events_hybrid_sysfs_show(struct device *dev,
				 struct device_attribute *attr,
				 char *page)
{
	struct perf_pmu_events_hybrid_attr *pmu_attr =
		container_of(attr, struct perf_pmu_events_hybrid_attr, attr);
	struct x86_hybrid_pmu *pmu;
	const char *str, *next_str;
	int i;

	if (hweight64(pmu_attr->pmu_type) == 1)
		return sprintf(page, "%s", pmu_attr->event_str);

	/*
	 * Hybrid PMUs may support the same event name, but with different
	 * event encoding, e.g., the mem-loads event on an Atom PMU has
	 * different event encoding from a Core PMU.
	 *
	 * The event_str includes all event encodings. Each event encoding
	 * is divided by ";". The order of the event encodings must follow
	 * the order of the hybrid PMU index.
	 */
	pmu = container_of(dev_get_drvdata(dev), struct x86_hybrid_pmu, pmu);

	str = pmu_attr->event_str;
	for (i = 0; i < x86_pmu.num_hybrid_pmus; i++) {
		if (!(x86_pmu.hybrid_pmu[i].cpu_type & pmu_attr->pmu_type))
			continue;
		if (x86_pmu.hybrid_pmu[i].cpu_type & pmu->cpu_type) {
			next_str = strchr(str, ';');
			if (next_str)
				return snprintf(page, next_str - str + 1, "%s", str);
			else
				return sprintf(page, "%s", str);
		}
		str = strchr(str, ';');
		str++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(events_hybrid_sysfs_show);

EVENT_ATTR(cpu-cycles,			CPU_CYCLES		);
EVENT_ATTR(instructions,		INSTRUCTIONS		);
EVENT_ATTR(cache-references,		CACHE_REFERENCES	);
EVENT_ATTR(cache-misses, 		CACHE_MISSES		);
EVENT_ATTR(branch-instructions,		BRANCH_INSTRUCTIONS	);
EVENT_ATTR(branch-misses,		BRANCH_MISSES		);
EVENT_ATTR(bus-cycles,			BUS_CYCLES		);
EVENT_ATTR(stalled-cycles-frontend,	STALLED_CYCLES_FRONTEND	);
EVENT_ATTR(stalled-cycles-backend,	STALLED_CYCLES_BACKEND	);
EVENT_ATTR(ref-cycles,			REF_CPU_CYCLES		);

static struct attribute *empty_attrs;

static struct attribute *events_attr[] = {
	EVENT_PTR(CPU_CYCLES),
	EVENT_PTR(INSTRUCTIONS),
	EVENT_PTR(CACHE_REFERENCES),
	EVENT_PTR(CACHE_MISSES),
	EVENT_PTR(BRANCH_INSTRUCTIONS),
	EVENT_PTR(BRANCH_MISSES),
	EVENT_PTR(BUS_CYCLES),
	EVENT_PTR(STALLED_CYCLES_FRONTEND),
	EVENT_PTR(STALLED_CYCLES_BACKEND),
	EVENT_PTR(REF_CPU_CYCLES),
	NULL,
};

/*
 * Remove all undefined events (x86_pmu.event_map(id) == 0)
 * out of events_attr attributes.
 */
static umode_t
is_visible(struct kobject *kobj, struct attribute *attr, int idx)
{
	struct perf_pmu_events_attr *pmu_attr;

	if (idx >= x86_pmu.max_events)
		return 0;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr.attr);
	/* str trumps id */
	return pmu_attr->event_str || x86_pmu.event_map(idx) ? attr->mode : 0;
}

static struct attribute_group x86_pmu_events_group __ro_after_init = {
	.name = "events",
	.attrs = events_attr,
	.is_visible = is_visible,
};

ssize_t x86_event_sysfs_show(char *page, u64 config, u64 event)
{
	u64 umask  = (config & ARCH_PERFMON_EVENTSEL_UMASK) >> 8;
	u64 cmask  = (config & ARCH_PERFMON_EVENTSEL_CMASK) >> 24;
	bool edge  = (config & ARCH_PERFMON_EVENTSEL_EDGE);
	bool pc    = (config & ARCH_PERFMON_EVENTSEL_PIN_CONTROL);
	bool any   = (config & ARCH_PERFMON_EVENTSEL_ANY);
	bool inv   = (config & ARCH_PERFMON_EVENTSEL_INV);
	ssize_t ret;

	/*
	* We have whole page size to spend and just little data
	* to write, so we can safely use sprintf.
	*/
	ret = sprintf(page, "event=0x%02llx", event);

	if (umask)
		ret += sprintf(page + ret, ",umask=0x%02llx", umask);

	if (edge)
		ret += sprintf(page + ret, ",edge");

	if (pc)
		ret += sprintf(page + ret, ",pc");

	if (any)
		ret += sprintf(page + ret, ",any");

	if (inv)
		ret += sprintf(page + ret, ",inv");

	if (cmask)
		ret += sprintf(page + ret, ",cmask=0x%02llx", cmask);

	ret += sprintf(page + ret, "\n");

	return ret;
}

static struct attribute_group x86_pmu_attr_group;
static struct attribute_group x86_pmu_caps_group;

static void x86_pmu_static_call_update(void)
{
	static_call_update(x86_pmu_handle_irq, x86_pmu.handle_irq);
	static_call_update(x86_pmu_disable_all, x86_pmu.disable_all);
	static_call_update(x86_pmu_enable_all, x86_pmu.enable_all);
	static_call_update(x86_pmu_enable, x86_pmu.enable);
	static_call_update(x86_pmu_disable, x86_pmu.disable);

	static_call_update(x86_pmu_add, x86_pmu.add);
	static_call_update(x86_pmu_del, x86_pmu.del);
	static_call_update(x86_pmu_read, x86_pmu.read);

	static_call_update(x86_pmu_schedule_events, x86_pmu.schedule_events);
	static_call_update(x86_pmu_get_event_constraints, x86_pmu.get_event_constraints);
	static_call_update(x86_pmu_put_event_constraints, x86_pmu.put_event_constraints);

	static_call_update(x86_pmu_start_scheduling, x86_pmu.start_scheduling);
	static_call_update(x86_pmu_commit_scheduling, x86_pmu.commit_scheduling);
	static_call_update(x86_pmu_stop_scheduling, x86_pmu.stop_scheduling);

	static_call_update(x86_pmu_sched_task, x86_pmu.sched_task);
	static_call_update(x86_pmu_swap_task_ctx, x86_pmu.swap_task_ctx);

	static_call_update(x86_pmu_drain_pebs, x86_pmu.drain_pebs);
	static_call_update(x86_pmu_pebs_aliases, x86_pmu.pebs_aliases);

	static_call_update(x86_pmu_guest_get_msrs, x86_pmu.guest_get_msrs);
}

static void _x86_pmu_read(struct perf_event *event)
{
	x86_perf_event_update(event);
}

void x86_pmu_show_pmu_cap(int num_counters, int num_counters_fixed,
			  u64 intel_ctrl)
{
	pr_info("... version:                %d\n",     x86_pmu.version);
	pr_info("... bit width:              %d\n",     x86_pmu.cntval_bits);
	pr_info("... generic registers:      %d\n",     num_counters);
	pr_info("... value mask:             %016Lx\n", x86_pmu.cntval_mask);
	pr_info("... max period:             %016Lx\n", x86_pmu.max_period);
	pr_info("... fixed-purpose events:   %lu\n",
			hweight64((((1ULL << num_counters_fixed) - 1)
					<< INTEL_PMC_IDX_FIXED) & intel_ctrl));
	pr_info("... event mask:             %016Lx\n", intel_ctrl);
}

/*
 * The generic code is not hybrid friendly. The hybrid_pmu->pmu
 * of the first registered PMU is unconditionally assigned to
 * each possible cpuctx->ctx.pmu.
 * Update the correct hybrid PMU to the cpuctx->ctx.pmu.
 */
void x86_pmu_update_cpu_context(struct pmu *pmu, int cpu)
{
	struct perf_cpu_context *cpuctx;

	if (!pmu->pmu_cpu_context)
		return;

	cpuctx = per_cpu_ptr(pmu->pmu_cpu_context, cpu);
	cpuctx->ctx.pmu = pmu;
}

static int __init init_hw_perf_events(void)
{
	struct x86_pmu_quirk *quirk;
	int err;

	pr_info("Performance Events: ");

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		err = intel_pmu_init();
		break;
	case X86_VENDOR_AMD:
		err = amd_pmu_init();
		break;
	case X86_VENDOR_HYGON:
		err = amd_pmu_init();
		x86_pmu.name = "HYGON";
		break;
	case X86_VENDOR_ZHAOXIN:
	case X86_VENDOR_CENTAUR:
		err = zhaoxin_pmu_init();
		break;
	default:
		err = -ENOTSUPP;
	}
	if (err != 0) {
		pr_cont("no PMU driver, software events only.\n");
		return 0;
	}

	pmu_check_apic();

	/* sanity check that the hardware exists or is emulated */
	if (!check_hw_exists(&pmu, x86_pmu.num_counters, x86_pmu.num_counters_fixed))
		return 0;

	pr_cont("%s PMU driver.\n", x86_pmu.name);

	x86_pmu.attr_rdpmc = 1; /* enable userspace RDPMC usage by default */

	for (quirk = x86_pmu.quirks; quirk; quirk = quirk->next)
		quirk->func();

	if (!x86_pmu.intel_ctrl)
		x86_pmu.intel_ctrl = (1 << x86_pmu.num_counters) - 1;

	perf_events_lapic_init();
	register_nmi_handler(NMI_LOCAL, perf_event_nmi_handler, 0, "PMI");

	unconstrained = (struct event_constraint)
		__EVENT_CONSTRAINT(0, (1ULL << x86_pmu.num_counters) - 1,
				   0, x86_pmu.num_counters, 0, 0);

	x86_pmu_format_group.attrs = x86_pmu.format_attrs;

	if (!x86_pmu.events_sysfs_show)
		x86_pmu_events_group.attrs = &empty_attrs;

	pmu.attr_update = x86_pmu.attr_update;

	if (!is_hybrid()) {
		x86_pmu_show_pmu_cap(x86_pmu.num_counters,
				     x86_pmu.num_counters_fixed,
				     x86_pmu.intel_ctrl);
	}

	if (!x86_pmu.read)
		x86_pmu.read = _x86_pmu_read;

	if (!x86_pmu.guest_get_msrs)
		x86_pmu.guest_get_msrs = (void *)&__static_call_return0;

	x86_pmu_static_call_update();

	/*
	 * Install callbacks. Core will call them for each online
	 * cpu.
	 */
	err = cpuhp_setup_state(CPUHP_PERF_X86_PREPARE, "perf/x86:prepare",
				x86_pmu_prepare_cpu, x86_pmu_dead_cpu);
	if (err)
		return err;

	err = cpuhp_setup_state(CPUHP_AP_PERF_X86_STARTING,
				"perf/x86:starting", x86_pmu_starting_cpu,
				x86_pmu_dying_cpu);
	if (err)
		goto out;

	err = cpuhp_setup_state(CPUHP_AP_PERF_X86_ONLINE, "perf/x86:online",
				x86_pmu_online_cpu, NULL);
	if (err)
		goto out1;

	if (!is_hybrid()) {
		err = perf_pmu_register(&pmu, "cpu", PERF_TYPE_RAW);
		if (err)
			goto out2;
	} else {
		u8 cpu_type = get_this_hybrid_cpu_type();
		struct x86_hybrid_pmu *hybrid_pmu;
		int i, j;

		if (!cpu_type && x86_pmu.get_hybrid_cpu_type)
			cpu_type = x86_pmu.get_hybrid_cpu_type();

		for (i = 0; i < x86_pmu.num_hybrid_pmus; i++) {
			hybrid_pmu = &x86_pmu.hybrid_pmu[i];

			hybrid_pmu->pmu = pmu;
			hybrid_pmu->pmu.type = -1;
			hybrid_pmu->pmu.attr_update = x86_pmu.attr_update;
			hybrid_pmu->pmu.capabilities |= PERF_PMU_CAP_HETEROGENEOUS_CPUS;
			hybrid_pmu->pmu.capabilities |= PERF_PMU_CAP_EXTENDED_HW_TYPE;

			err = perf_pmu_register(&hybrid_pmu->pmu, hybrid_pmu->name,
						(hybrid_pmu->cpu_type == hybrid_big) ? PERF_TYPE_RAW : -1);
			if (err)
				break;

			if (cpu_type == hybrid_pmu->cpu_type)
				x86_pmu_update_cpu_context(&hybrid_pmu->pmu, raw_smp_processor_id());
		}

		if (i < x86_pmu.num_hybrid_pmus) {
			for (j = 0; j < i; j++)
				perf_pmu_unregister(&x86_pmu.hybrid_pmu[j].pmu);
			pr_warn("Failed to register hybrid PMUs\n");
			kfree(x86_pmu.hybrid_pmu);
			x86_pmu.hybrid_pmu = NULL;
			x86_pmu.num_hybrid_pmus = 0;
			goto out2;
		}
	}

	return 0;

out2:
	cpuhp_remove_state(CPUHP_AP_PERF_X86_ONLINE);
out1:
	cpuhp_remove_state(CPUHP_AP_PERF_X86_STARTING);
out:
	cpuhp_remove_state(CPUHP_PERF_X86_PREPARE);
	return err;
}
early_initcall(init_hw_perf_events);

static void x86_pmu_read(struct perf_event *event)
{
	static_call(x86_pmu_read)(event);
}

/*
 * Start group events scheduling transaction
 * Set the flag to make pmu::enable() not perform the
 * schedulability test, it will be performed at commit time
 *
 * We only support PERF_PMU_TXN_ADD transactions. Save the
 * transaction flags but otherwise ignore non-PERF_PMU_TXN_ADD
 * transactions.
 */
static void x86_pmu_start_txn(struct pmu *pmu, unsigned int txn_flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	WARN_ON_ONCE(cpuc->txn_flags);		/* txn already in flight */

	cpuc->txn_flags = txn_flags;
	if (txn_flags & ~PERF_PMU_TXN_ADD)
		return;

	perf_pmu_disable(pmu);
	__this_cpu_write(cpu_hw_events.n_txn, 0);
	__this_cpu_write(cpu_hw_events.n_txn_pair, 0);
	__this_cpu_write(cpu_hw_events.n_txn_metric, 0);
}

/*
 * Stop group events scheduling transaction
 * Clear the flag and pmu::enable() will perform the
 * schedulability test.
 */
static void x86_pmu_cancel_txn(struct pmu *pmu)
{
	unsigned int txn_flags;
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	WARN_ON_ONCE(!cpuc->txn_flags);	/* no txn in flight */

	txn_flags = cpuc->txn_flags;
	cpuc->txn_flags = 0;
	if (txn_flags & ~PERF_PMU_TXN_ADD)
		return;

	/*
	 * Truncate collected array by the number of events added in this
	 * transaction. See x86_pmu_add() and x86_pmu_*_txn().
	 */
	__this_cpu_sub(cpu_hw_events.n_added, __this_cpu_read(cpu_hw_events.n_txn));
	__this_cpu_sub(cpu_hw_events.n_events, __this_cpu_read(cpu_hw_events.n_txn));
	__this_cpu_sub(cpu_hw_events.n_pair, __this_cpu_read(cpu_hw_events.n_txn_pair));
	__this_cpu_sub(cpu_hw_events.n_metric, __this_cpu_read(cpu_hw_events.n_txn_metric));
	perf_pmu_enable(pmu);
}

/*
 * Commit group events scheduling transaction
 * Perform the group schedulability test as a whole
 * Return 0 if success
 *
 * Does not cancel the transaction on failure; expects the caller to do this.
 */
static int x86_pmu_commit_txn(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int assign[X86_PMC_IDX_MAX];
	int n, ret;

	WARN_ON_ONCE(!cpuc->txn_flags);	/* no txn in flight */

	if (cpuc->txn_flags & ~PERF_PMU_TXN_ADD) {
		cpuc->txn_flags = 0;
		return 0;
	}

	n = cpuc->n_events;

	if (!x86_pmu_initialized())
		return -EAGAIN;

	ret = static_call(x86_pmu_schedule_events)(cpuc, n, assign);
	if (ret)
		return ret;

	/*
	 * copy new assignment, now we know it is possible
	 * will be used by hw_perf_enable()
	 */
	memcpy(cpuc->assign, assign, n*sizeof(int));

	cpuc->txn_flags = 0;
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
	intel_cpuc_finish(cpuc);
	kfree(cpuc);
}

static struct cpu_hw_events *allocate_fake_cpuc(struct pmu *event_pmu)
{
	struct cpu_hw_events *cpuc;
	int cpu;

	cpuc = kzalloc(sizeof(*cpuc), GFP_KERNEL);
	if (!cpuc)
		return ERR_PTR(-ENOMEM);
	cpuc->is_fake = 1;

	if (is_hybrid()) {
		struct x86_hybrid_pmu *h_pmu;

		h_pmu = hybrid_pmu(event_pmu);
		if (cpumask_empty(&h_pmu->supported_cpus))
			goto error;
		cpu = cpumask_first(&h_pmu->supported_cpus);
	} else
		cpu = raw_smp_processor_id();
	cpuc->pmu = event_pmu;

	if (intel_cpuc_prepare(cpuc, cpu))
		goto error;

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

	fake_cpuc = allocate_fake_cpuc(event->pmu);
	if (IS_ERR(fake_cpuc))
		return PTR_ERR(fake_cpuc);

	c = x86_pmu.get_event_constraints(fake_cpuc, 0, event);

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

	/*
	 * Reject events from different hybrid PMUs.
	 */
	if (is_hybrid()) {
		struct perf_event *sibling;
		struct pmu *pmu = NULL;

		if (is_x86_event(leader))
			pmu = leader->pmu;

		for_each_sibling_event(sibling, leader) {
			if (!is_x86_event(sibling))
				continue;
			if (!pmu)
				pmu = sibling->pmu;
			else if (pmu != sibling->pmu)
				return ret;
		}
	}

	fake_cpuc = allocate_fake_cpuc(event->pmu);
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

	fake_cpuc->n_events = 0;
	ret = x86_pmu.schedule_events(fake_cpuc, n, NULL);

out:
	free_fake_cpuc(fake_cpuc);
	return ret;
}

static int x86_pmu_event_init(struct perf_event *event)
{
	struct x86_hybrid_pmu *pmu = NULL;
	int err;

	if ((event->attr.type != event->pmu->type) &&
	    (event->attr.type != PERF_TYPE_HARDWARE) &&
	    (event->attr.type != PERF_TYPE_HW_CACHE))
		return -ENOENT;

	if (is_hybrid() && (event->cpu != -1)) {
		pmu = hybrid_pmu(event->pmu);
		if (!cpumask_test_cpu(event->cpu, &pmu->supported_cpus))
			return -ENOENT;
	}

	err = __x86_pmu_event_init(event);
	if (!err) {
		if (event->group_leader != event)
			err = validate_group(event);
		else
			err = validate_event(event);
	}
	if (err) {
		if (event->destroy)
			event->destroy(event);
	}

	if (READ_ONCE(x86_pmu.attr_rdpmc) &&
	    !(event->hw.flags & PERF_X86_EVENT_LARGE_PEBS))
		event->hw.flags |= PERF_X86_EVENT_RDPMC_ALLOWED;

	return err;
}

void perf_clear_dirty_counters(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int i;

	 /* Don't need to clear the assigned counter. */
	for (i = 0; i < cpuc->n_events; i++)
		__clear_bit(cpuc->assign[i], cpuc->dirty);

	if (bitmap_empty(cpuc->dirty, X86_PMC_IDX_MAX))
		return;

	for_each_set_bit(i, cpuc->dirty, X86_PMC_IDX_MAX) {
		if (i >= INTEL_PMC_IDX_FIXED) {
			/* Metrics and fake events don't have corresponding HW counters. */
			if ((i - INTEL_PMC_IDX_FIXED) >= hybrid(cpuc->pmu, num_counters_fixed))
				continue;

			wrmsrl(MSR_ARCH_PERFMON_FIXED_CTR0 + (i - INTEL_PMC_IDX_FIXED), 0);
		} else {
			wrmsrl(x86_pmu_event_addr(i), 0);
		}
	}

	bitmap_zero(cpuc->dirty, X86_PMC_IDX_MAX);
}

static void x86_pmu_event_mapped(struct perf_event *event, struct mm_struct *mm)
{
	if (!(event->hw.flags & PERF_X86_EVENT_RDPMC_ALLOWED))
		return;

	/*
	 * This function relies on not being called concurrently in two
	 * tasks in the same mm.  Otherwise one task could observe
	 * perf_rdpmc_allowed > 1 and return all the way back to
	 * userspace with CR4.PCE clear while another task is still
	 * doing on_each_cpu_mask() to propagate CR4.PCE.
	 *
	 * For now, this can't happen because all callers hold mmap_lock
	 * for write.  If this changes, we'll need a different solution.
	 */
	mmap_assert_write_locked(mm);

	if (atomic_inc_return(&mm->context.perf_rdpmc_allowed) == 1)
		on_each_cpu_mask(mm_cpumask(mm), cr4_update_pce, NULL, 1);
}

static void x86_pmu_event_unmapped(struct perf_event *event, struct mm_struct *mm)
{
	if (!(event->hw.flags & PERF_X86_EVENT_RDPMC_ALLOWED))
		return;

	if (atomic_dec_and_test(&mm->context.perf_rdpmc_allowed))
		on_each_cpu_mask(mm_cpumask(mm), cr4_update_pce, NULL, 1);
}

static int x86_pmu_event_idx(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (!(hwc->flags & PERF_X86_EVENT_RDPMC_ALLOWED))
		return 0;

	if (is_metric_idx(hwc->idx))
		return INTEL_PMC_FIXED_RDPMC_METRICS + 1;
	else
		return hwc->event_base_rdpmc + 1;
}

static ssize_t get_attr_rdpmc(struct device *cdev,
			      struct device_attribute *attr,
			      char *buf)
{
	return snprintf(buf, 40, "%d\n", x86_pmu.attr_rdpmc);
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

	if (val > 2)
		return -EINVAL;

	if (x86_pmu.attr_rdpmc_broken)
		return -ENOTSUPP;

	if (val != x86_pmu.attr_rdpmc) {
		/*
		 * Changing into or out of never available or always available,
		 * aka perf-event-bypassing mode. This path is extremely slow,
		 * but only root can trigger it, so it's okay.
		 */
		if (val == 0)
			static_branch_inc(&rdpmc_never_available_key);
		else if (x86_pmu.attr_rdpmc == 0)
			static_branch_dec(&rdpmc_never_available_key);

		if (val == 2)
			static_branch_inc(&rdpmc_always_available_key);
		else if (x86_pmu.attr_rdpmc == 2)
			static_branch_dec(&rdpmc_always_available_key);

		on_each_cpu(cr4_update_pce, NULL, 1);
		x86_pmu.attr_rdpmc = val;
	}

	return count;
}

static DEVICE_ATTR(rdpmc, S_IRUSR | S_IWUSR, get_attr_rdpmc, set_attr_rdpmc);

static struct attribute *x86_pmu_attrs[] = {
	&dev_attr_rdpmc.attr,
	NULL,
};

static struct attribute_group x86_pmu_attr_group __ro_after_init = {
	.attrs = x86_pmu_attrs,
};

static ssize_t max_precise_show(struct device *cdev,
				  struct device_attribute *attr,
				  char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", x86_pmu_max_precise());
}

static DEVICE_ATTR_RO(max_precise);

static struct attribute *x86_pmu_caps_attrs[] = {
	&dev_attr_max_precise.attr,
	NULL
};

static struct attribute_group x86_pmu_caps_group __ro_after_init = {
	.name = "caps",
	.attrs = x86_pmu_caps_attrs,
};

static const struct attribute_group *x86_pmu_attr_groups[] = {
	&x86_pmu_attr_group,
	&x86_pmu_format_group,
	&x86_pmu_events_group,
	&x86_pmu_caps_group,
	NULL,
};

static void x86_pmu_sched_task(struct perf_event_context *ctx, bool sched_in)
{
	static_call_cond(x86_pmu_sched_task)(ctx, sched_in);
}

static void x86_pmu_swap_task_ctx(struct perf_event_context *prev,
				  struct perf_event_context *next)
{
	static_call_cond(x86_pmu_swap_task_ctx)(prev, next);
}

void perf_check_microcode(void)
{
	if (x86_pmu.check_microcode)
		x86_pmu.check_microcode();
}

static int x86_pmu_check_period(struct perf_event *event, u64 value)
{
	if (x86_pmu.check_period && x86_pmu.check_period(event, value))
		return -EINVAL;

	if (value && x86_pmu.limit_period) {
		if (x86_pmu.limit_period(event, value) > value)
			return -EINVAL;
	}

	return 0;
}

static int x86_pmu_aux_output_match(struct perf_event *event)
{
	if (!(pmu.capabilities & PERF_PMU_CAP_AUX_OUTPUT))
		return 0;

	if (x86_pmu.aux_output_match)
		return x86_pmu.aux_output_match(event);

	return 0;
}

static int x86_pmu_filter_match(struct perf_event *event)
{
	if (x86_pmu.filter_match)
		return x86_pmu.filter_match(event);

	return 1;
}

static struct pmu pmu = {
	.pmu_enable		= x86_pmu_enable,
	.pmu_disable		= x86_pmu_disable,

	.attr_groups		= x86_pmu_attr_groups,

	.event_init		= x86_pmu_event_init,

	.event_mapped		= x86_pmu_event_mapped,
	.event_unmapped		= x86_pmu_event_unmapped,

	.add			= x86_pmu_add,
	.del			= x86_pmu_del,
	.start			= x86_pmu_start,
	.stop			= x86_pmu_stop,
	.read			= x86_pmu_read,

	.start_txn		= x86_pmu_start_txn,
	.cancel_txn		= x86_pmu_cancel_txn,
	.commit_txn		= x86_pmu_commit_txn,

	.event_idx		= x86_pmu_event_idx,
	.sched_task		= x86_pmu_sched_task,
	.swap_task_ctx		= x86_pmu_swap_task_ctx,
	.check_period		= x86_pmu_check_period,

	.aux_output_match	= x86_pmu_aux_output_match,

	.filter_match		= x86_pmu_filter_match,
};

void arch_perf_update_userpage(struct perf_event *event,
			       struct perf_event_mmap_page *userpg, u64 now)
{
	struct cyc2ns_data data;
	u64 offset;

	userpg->cap_user_time = 0;
	userpg->cap_user_time_zero = 0;
	userpg->cap_user_rdpmc =
		!!(event->hw.flags & PERF_X86_EVENT_RDPMC_ALLOWED);
	userpg->pmc_width = x86_pmu.cntval_bits;

	if (!using_native_sched_clock() || !sched_clock_stable())
		return;

	cyc2ns_read_begin(&data);

	offset = data.cyc2ns_offset + __sched_clock_offset;

	/*
	 * Internal timekeeping for enabled/running/stopped times
	 * is always in the local_clock domain.
	 */
	userpg->cap_user_time = 1;
	userpg->time_mult = data.cyc2ns_mul;
	userpg->time_shift = data.cyc2ns_shift;
	userpg->time_offset = offset - now;

	/*
	 * cap_user_time_zero doesn't make sense when we're using a different
	 * time base for the records.
	 */
	if (!event->attr.use_clockid) {
		userpg->cap_user_time_zero = 1;
		userpg->time_zero = offset;
	}

	cyc2ns_read_end();
}

/*
 * Determine whether the regs were taken from an irq/exception handler rather
 * than from perf_arch_fetch_caller_regs().
 */
static bool perf_hw_regs(struct pt_regs *regs)
{
	return regs->flags & X86_EFLAGS_FIXED;
}

void
perf_callchain_kernel(struct perf_callchain_entry_ctx *entry, struct pt_regs *regs)
{
	struct unwind_state state;
	unsigned long addr;

	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		/* TODO: We don't support guest os callchain now */
		return;
	}

	if (perf_callchain_store(entry, regs->ip))
		return;

	if (perf_hw_regs(regs))
		unwind_start(&state, current, regs, NULL);
	else
		unwind_start(&state, current, NULL, (void *)regs->sp);

	for (; !unwind_done(&state); unwind_next_frame(&state)) {
		addr = unwind_get_return_address(&state);
		if (!addr || perf_callchain_store(entry, addr))
			return;
	}
}

static inline int
valid_user_frame(const void __user *fp, unsigned long size)
{
	return (__range_not_ok(fp, size, TASK_SIZE) == 0);
}

static unsigned long get_segment_base(unsigned int segment)
{
	struct desc_struct *desc;
	unsigned int idx = segment >> 3;

	if ((segment & SEGMENT_TI_MASK) == SEGMENT_LDT) {
#ifdef CONFIG_MODIFY_LDT_SYSCALL
		struct ldt_struct *ldt;

		/* IRQs are off, so this synchronizes with smp_store_release */
		ldt = READ_ONCE(current->active_mm->context.ldt);
		if (!ldt || idx >= ldt->nr_entries)
			return 0;

		desc = &ldt->entries[idx];
#else
		return 0;
#endif
	} else {
		if (idx >= GDT_ENTRIES)
			return 0;

		desc = raw_cpu_ptr(gdt_page.gdt) + idx;
	}

	return get_desc_base(desc);
}

#ifdef CONFIG_IA32_EMULATION

#include <linux/compat.h>

static inline int
perf_callchain_user32(struct pt_regs *regs, struct perf_callchain_entry_ctx *entry)
{
	/* 32-bit process in 64-bit kernel. */
	unsigned long ss_base, cs_base;
	struct stack_frame_ia32 frame;
	const struct stack_frame_ia32 __user *fp;

	if (user_64bit_mode(regs))
		return 0;

	cs_base = get_segment_base(regs->cs);
	ss_base = get_segment_base(regs->ss);

	fp = compat_ptr(ss_base + regs->bp);
	pagefault_disable();
	while (entry->nr < entry->max_stack) {
		if (!valid_user_frame(fp, sizeof(frame)))
			break;

		if (__get_user(frame.next_frame, &fp->next_frame))
			break;
		if (__get_user(frame.return_address, &fp->return_address))
			break;

		perf_callchain_store(entry, cs_base + frame.return_address);
		fp = compat_ptr(ss_base + frame.next_frame);
	}
	pagefault_enable();
	return 1;
}
#else
static inline int
perf_callchain_user32(struct pt_regs *regs, struct perf_callchain_entry_ctx *entry)
{
    return 0;
}
#endif

void
perf_callchain_user(struct perf_callchain_entry_ctx *entry, struct pt_regs *regs)
{
	struct stack_frame frame;
	const struct stack_frame __user *fp;

	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		/* TODO: We don't support guest os callchain now */
		return;
	}

	/*
	 * We don't know what to do with VM86 stacks.. ignore them for now.
	 */
	if (regs->flags & (X86_VM_MASK | PERF_EFLAGS_VM))
		return;

	fp = (void __user *)regs->bp;

	perf_callchain_store(entry, regs->ip);

	if (!nmi_uaccess_okay())
		return;

	if (perf_callchain_user32(regs, entry))
		return;

	pagefault_disable();
	while (entry->nr < entry->max_stack) {
		if (!valid_user_frame(fp, sizeof(frame)))
			break;

		if (__get_user(frame.next_frame, &fp->next_frame))
			break;
		if (__get_user(frame.return_address, &fp->return_address))
			break;

		perf_callchain_store(entry, frame.return_address);
		fp = (void __user *)frame.next_frame;
	}
	pagefault_enable();
}

/*
 * Deal with code segment offsets for the various execution modes:
 *
 *   VM86 - the good olde 16 bit days, where the linear address is
 *          20 bits and we use regs->ip + 0x10 * regs->cs.
 *
 *   IA32 - Where we need to look at GDT/LDT segment descriptor tables
 *          to figure out what the 32bit base address is.
 *
 *    X32 - has TIF_X32 set, but is running in x86_64
 *
 * X86_64 - CS,DS,SS,ES are all zero based.
 */
static unsigned long code_segment_base(struct pt_regs *regs)
{
	/*
	 * For IA32 we look at the GDT/LDT segment base to convert the
	 * effective IP to a linear address.
	 */

#ifdef CONFIG_X86_32
	/*
	 * If we are in VM86 mode, add the segment offset to convert to a
	 * linear address.
	 */
	if (regs->flags & X86_VM_MASK)
		return 0x10 * regs->cs;

	if (user_mode(regs) && regs->cs != __USER_CS)
		return get_segment_base(regs->cs);
#else
	if (user_mode(regs) && !user_64bit_mode(regs) &&
	    regs->cs != __USER32_CS)
		return get_segment_base(regs->cs);
#endif
	return 0;
}

unsigned long perf_instruction_pointer(struct pt_regs *regs)
{
	if (perf_guest_cbs && perf_guest_cbs->is_in_guest())
		return perf_guest_cbs->get_guest_ip();

	return regs->ip + code_segment_base(regs);
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

void perf_get_x86_pmu_capability(struct x86_pmu_capability *cap)
{
	cap->version		= x86_pmu.version;
	/*
	 * KVM doesn't support the hybrid PMU yet.
	 * Return the common value in global x86_pmu,
	 * which available for all cores.
	 */
	cap->num_counters_gp	= x86_pmu.num_counters;
	cap->num_counters_fixed	= x86_pmu.num_counters_fixed;
	cap->bit_width_gp	= x86_pmu.cntval_bits;
	cap->bit_width_fixed	= x86_pmu.cntval_bits;
	cap->events_mask	= (unsigned int)x86_pmu.events_maskl;
	cap->events_mask_len	= x86_pmu.events_mask_len;
}
EXPORT_SYMBOL_GPL(perf_get_x86_pmu_capability);
