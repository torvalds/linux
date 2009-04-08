/*
 * Performance counter x86 architecture code
 *
 *  Copyright(C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright(C) 2008 Red Hat, Inc., Ingo Molnar
 *  Copyright(C) 2009 Jaswinder Singh Rajput
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/perf_counter.h>
#include <linux/capability.h>
#include <linux/notifier.h>
#include <linux/hardirq.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/kdebug.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include <asm/apic.h>
#include <asm/stacktrace.h>
#include <asm/nmi.h>

static bool perf_counters_initialized __read_mostly;

/*
 * Number of (generic) HW counters:
 */
static int nr_counters_generic __read_mostly;
static u64 perf_counter_mask __read_mostly;
static u64 counter_value_mask __read_mostly;
static int counter_value_bits __read_mostly;

static int nr_counters_fixed __read_mostly;

struct cpu_hw_counters {
	struct perf_counter	*counters[X86_PMC_IDX_MAX];
	unsigned long		used[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	unsigned long		interrupts;
	u64			throttle_ctrl;
	unsigned long		active_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	int			enabled;
};

/*
 * struct pmc_x86_ops - performance counter x86 ops
 */
struct pmc_x86_ops {
	u64		(*save_disable_all)(void);
	void		(*restore_all)(u64);
	u64		(*get_status)(u64);
	void		(*ack_status)(u64);
	void		(*enable)(int, u64);
	void		(*disable)(int, u64);
	unsigned	eventsel;
	unsigned	perfctr;
	u64		(*event_map)(int);
	u64		(*raw_event)(u64);
	int		max_events;
};

static struct pmc_x86_ops *pmc_ops __read_mostly;

static DEFINE_PER_CPU(struct cpu_hw_counters, cpu_hw_counters) = {
	.enabled = 1,
};

static __read_mostly int intel_perfmon_version;

/*
 * Intel PerfMon v3. Used on Core2 and later.
 */
static const u64 intel_perfmon_event_map[] =
{
  [PERF_COUNT_CPU_CYCLES]		= 0x003c,
  [PERF_COUNT_INSTRUCTIONS]		= 0x00c0,
  [PERF_COUNT_CACHE_REFERENCES]		= 0x4f2e,
  [PERF_COUNT_CACHE_MISSES]		= 0x412e,
  [PERF_COUNT_BRANCH_INSTRUCTIONS]	= 0x00c4,
  [PERF_COUNT_BRANCH_MISSES]		= 0x00c5,
  [PERF_COUNT_BUS_CYCLES]		= 0x013c,
};

static u64 pmc_intel_event_map(int event)
{
	return intel_perfmon_event_map[event];
}

static u64 pmc_intel_raw_event(u64 event)
{
#define CORE_EVNTSEL_EVENT_MASK		0x000000FFULL
#define CORE_EVNTSEL_UNIT_MASK		0x0000FF00ULL
#define CORE_EVNTSEL_COUNTER_MASK	0xFF000000ULL

#define CORE_EVNTSEL_MASK 		\
	(CORE_EVNTSEL_EVENT_MASK |	\
	 CORE_EVNTSEL_UNIT_MASK  |	\
	 CORE_EVNTSEL_COUNTER_MASK)

	return event & CORE_EVNTSEL_MASK;
}

/*
 * AMD Performance Monitor K7 and later.
 */
static const u64 amd_perfmon_event_map[] =
{
  [PERF_COUNT_CPU_CYCLES]		= 0x0076,
  [PERF_COUNT_INSTRUCTIONS]		= 0x00c0,
  [PERF_COUNT_CACHE_REFERENCES]		= 0x0080,
  [PERF_COUNT_CACHE_MISSES]		= 0x0081,
  [PERF_COUNT_BRANCH_INSTRUCTIONS]	= 0x00c4,
  [PERF_COUNT_BRANCH_MISSES]		= 0x00c5,
};

static u64 pmc_amd_event_map(int event)
{
	return amd_perfmon_event_map[event];
}

static u64 pmc_amd_raw_event(u64 event)
{
#define K7_EVNTSEL_EVENT_MASK	0x7000000FFULL
#define K7_EVNTSEL_UNIT_MASK	0x00000FF00ULL
#define K7_EVNTSEL_COUNTER_MASK	0x0FF000000ULL

#define K7_EVNTSEL_MASK			\
	(K7_EVNTSEL_EVENT_MASK |	\
	 K7_EVNTSEL_UNIT_MASK  |	\
	 K7_EVNTSEL_COUNTER_MASK)

	return event & K7_EVNTSEL_MASK;
}

/*
 * Propagate counter elapsed time into the generic counter.
 * Can only be executed on the CPU where the counter is active.
 * Returns the delta events processed.
 */
static void
x86_perf_counter_update(struct perf_counter *counter,
			struct hw_perf_counter *hwc, int idx)
{
	u64 prev_raw_count, new_raw_count, delta;

	/*
	 * Careful: an NMI might modify the previous counter value.
	 *
	 * Our tactic to handle this is to first atomically read and
	 * exchange a new raw count - then add that new-prev delta
	 * count to the generic counter atomically:
	 */
again:
	prev_raw_count = atomic64_read(&hwc->prev_count);
	rdmsrl(hwc->counter_base + idx, new_raw_count);

	if (atomic64_cmpxchg(&hwc->prev_count, prev_raw_count,
					new_raw_count) != prev_raw_count)
		goto again;

	/*
	 * Now we have the new raw value and have updated the prev
	 * timestamp already. We can now calculate the elapsed delta
	 * (counter-)time and add that to the generic counter.
	 *
	 * Careful, not all hw sign-extends above the physical width
	 * of the count, so we do that by clipping the delta to 32 bits:
	 */
	delta = (u64)(u32)((s32)new_raw_count - (s32)prev_raw_count);

	atomic64_add(delta, &counter->count);
	atomic64_sub(delta, &hwc->period_left);
}

static atomic_t num_counters;
static DEFINE_MUTEX(pmc_reserve_mutex);

static bool reserve_pmc_hardware(void)
{
	int i;

	if (nmi_watchdog == NMI_LOCAL_APIC)
		disable_lapic_nmi_watchdog();

	for (i = 0; i < nr_counters_generic; i++) {
		if (!reserve_perfctr_nmi(pmc_ops->perfctr + i))
			goto perfctr_fail;
	}

	for (i = 0; i < nr_counters_generic; i++) {
		if (!reserve_evntsel_nmi(pmc_ops->eventsel + i))
			goto eventsel_fail;
	}

	return true;

eventsel_fail:
	for (i--; i >= 0; i--)
		release_evntsel_nmi(pmc_ops->eventsel + i);

	i = nr_counters_generic;

perfctr_fail:
	for (i--; i >= 0; i--)
		release_perfctr_nmi(pmc_ops->perfctr + i);

	if (nmi_watchdog == NMI_LOCAL_APIC)
		enable_lapic_nmi_watchdog();

	return false;
}

static void release_pmc_hardware(void)
{
	int i;

	for (i = 0; i < nr_counters_generic; i++) {
		release_perfctr_nmi(pmc_ops->perfctr + i);
		release_evntsel_nmi(pmc_ops->eventsel + i);
	}

	if (nmi_watchdog == NMI_LOCAL_APIC)
		enable_lapic_nmi_watchdog();
}

static void hw_perf_counter_destroy(struct perf_counter *counter)
{
	if (atomic_dec_and_mutex_lock(&num_counters, &pmc_reserve_mutex)) {
		release_pmc_hardware();
		mutex_unlock(&pmc_reserve_mutex);
	}
}

/*
 * Setup the hardware configuration for a given hw_event_type
 */
static int __hw_perf_counter_init(struct perf_counter *counter)
{
	struct perf_counter_hw_event *hw_event = &counter->hw_event;
	struct hw_perf_counter *hwc = &counter->hw;
	int err;

	if (unlikely(!perf_counters_initialized))
		return -EINVAL;

	err = 0;
	if (atomic_inc_not_zero(&num_counters)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&num_counters) == 0 && !reserve_pmc_hardware())
			err = -EBUSY;
		else
			atomic_inc(&num_counters);
		mutex_unlock(&pmc_reserve_mutex);
	}
	if (err)
		return err;

	/*
	 * Generate PMC IRQs:
	 * (keep 'enabled' bit clear for now)
	 */
	hwc->config = ARCH_PERFMON_EVENTSEL_INT;

	/*
	 * Count user and OS events unless requested not to.
	 */
	if (!hw_event->exclude_user)
		hwc->config |= ARCH_PERFMON_EVENTSEL_USR;
	if (!hw_event->exclude_kernel)
		hwc->config |= ARCH_PERFMON_EVENTSEL_OS;

	/*
	 * If privileged enough, allow NMI events:
	 */
	hwc->nmi = 0;
	if (capable(CAP_SYS_ADMIN) && hw_event->nmi)
		hwc->nmi = 1;

	hwc->irq_period		= hw_event->irq_period;
	/*
	 * Intel PMCs cannot be accessed sanely above 32 bit width,
	 * so we install an artificial 1<<31 period regardless of
	 * the generic counter period:
	 */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		if ((s64)hwc->irq_period <= 0 || hwc->irq_period > 0x7FFFFFFF)
			hwc->irq_period = 0x7FFFFFFF;

	atomic64_set(&hwc->period_left, hwc->irq_period);

	/*
	 * Raw event type provide the config in the event structure
	 */
	if (perf_event_raw(hw_event)) {
		hwc->config |= pmc_ops->raw_event(perf_event_config(hw_event));
	} else {
		if (perf_event_id(hw_event) >= pmc_ops->max_events)
			return -EINVAL;
		/*
		 * The generic map:
		 */
		hwc->config |= pmc_ops->event_map(perf_event_id(hw_event));
	}

	counter->destroy = hw_perf_counter_destroy;

	return 0;
}

static u64 pmc_intel_save_disable_all(void)
{
	u64 ctrl;

	rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, ctrl);
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0);

	return ctrl;
}

static u64 pmc_amd_save_disable_all(void)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	int enabled, idx;

	enabled = cpuc->enabled;
	cpuc->enabled = 0;
	/*
	 * ensure we write the disable before we start disabling the
	 * counters proper, so that pcm_amd_enable() does the right thing.
	 */
	barrier();

	for (idx = 0; idx < nr_counters_generic; idx++) {
		u64 val;

		rdmsrl(MSR_K7_EVNTSEL0 + idx, val);
		if (val & ARCH_PERFMON_EVENTSEL0_ENABLE) {
			val &= ~ARCH_PERFMON_EVENTSEL0_ENABLE;
			wrmsrl(MSR_K7_EVNTSEL0 + idx, val);
		}
	}

	return enabled;
}

u64 hw_perf_save_disable(void)
{
	if (unlikely(!perf_counters_initialized))
		return 0;

	return pmc_ops->save_disable_all();
}
/*
 * Exported because of ACPI idle
 */
EXPORT_SYMBOL_GPL(hw_perf_save_disable);

static void pmc_intel_restore_all(u64 ctrl)
{
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, ctrl);
}

static void pmc_amd_restore_all(u64 ctrl)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	int idx;

	cpuc->enabled = ctrl;
	barrier();
	if (!ctrl)
		return;

	for (idx = 0; idx < nr_counters_generic; idx++) {
		if (test_bit(idx, cpuc->active_mask)) {
			u64 val;

			rdmsrl(MSR_K7_EVNTSEL0 + idx, val);
			val |= ARCH_PERFMON_EVENTSEL0_ENABLE;
			wrmsrl(MSR_K7_EVNTSEL0 + idx, val);
		}
	}
}

void hw_perf_restore(u64 ctrl)
{
	if (unlikely(!perf_counters_initialized))
		return;

	pmc_ops->restore_all(ctrl);
}
/*
 * Exported because of ACPI idle
 */
EXPORT_SYMBOL_GPL(hw_perf_restore);

static u64 pmc_intel_get_status(u64 mask)
{
	u64 status;

	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);

	return status;
}

static u64 pmc_amd_get_status(u64 mask)
{
	u64 status = 0;
	int idx;

	for (idx = 0; idx < nr_counters_generic; idx++) {
		s64 val;

		if (!(mask & (1 << idx)))
			continue;

		rdmsrl(MSR_K7_PERFCTR0 + idx, val);
		val <<= (64 - counter_value_bits);
		if (val >= 0)
			status |= (1 << idx);
	}

	return status;
}

static u64 hw_perf_get_status(u64 mask)
{
	if (unlikely(!perf_counters_initialized))
		return 0;

	return pmc_ops->get_status(mask);
}

static void pmc_intel_ack_status(u64 ack)
{
	wrmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, ack);
}

static void pmc_amd_ack_status(u64 ack)
{
}

static void hw_perf_ack_status(u64 ack)
{
	if (unlikely(!perf_counters_initialized))
		return;

	pmc_ops->ack_status(ack);
}

static void pmc_intel_enable(int idx, u64 config)
{
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL0 + idx,
			config | ARCH_PERFMON_EVENTSEL0_ENABLE);
}

static void pmc_amd_enable(int idx, u64 config)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);

	set_bit(idx, cpuc->active_mask);
	if (cpuc->enabled)
		config |= ARCH_PERFMON_EVENTSEL0_ENABLE;

	wrmsrl(MSR_K7_EVNTSEL0 + idx, config);
}

static void hw_perf_enable(int idx, u64 config)
{
	if (unlikely(!perf_counters_initialized))
		return;

	pmc_ops->enable(idx, config);
}

static void pmc_intel_disable(int idx, u64 config)
{
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL0 + idx, config);
}

static void pmc_amd_disable(int idx, u64 config)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);

	clear_bit(idx, cpuc->active_mask);
	wrmsrl(MSR_K7_EVNTSEL0 + idx, config);

}

static void hw_perf_disable(int idx, u64 config)
{
	if (unlikely(!perf_counters_initialized))
		return;

	pmc_ops->disable(idx, config);
}

static inline void
__pmc_fixed_disable(struct perf_counter *counter,
		    struct hw_perf_counter *hwc, unsigned int __idx)
{
	int idx = __idx - X86_PMC_IDX_FIXED;
	u64 ctrl_val, mask;
	int err;

	mask = 0xfULL << (idx * 4);

	rdmsrl(hwc->config_base, ctrl_val);
	ctrl_val &= ~mask;
	err = checking_wrmsrl(hwc->config_base, ctrl_val);
}

static inline void
__pmc_generic_disable(struct perf_counter *counter,
			   struct hw_perf_counter *hwc, unsigned int idx)
{
	if (unlikely(hwc->config_base == MSR_ARCH_PERFMON_FIXED_CTR_CTRL))
		__pmc_fixed_disable(counter, hwc, idx);
	else
		hw_perf_disable(idx, hwc->config);
}

static DEFINE_PER_CPU(u64, prev_left[X86_PMC_IDX_MAX]);

/*
 * Set the next IRQ period, based on the hwc->period_left value.
 * To be called with the counter disabled in hw:
 */
static void
__hw_perf_counter_set_period(struct perf_counter *counter,
			     struct hw_perf_counter *hwc, int idx)
{
	s64 left = atomic64_read(&hwc->period_left);
	s64 period = hwc->irq_period;
	int err;

	/*
	 * If we are way outside a reasoable range then just skip forward:
	 */
	if (unlikely(left <= -period)) {
		left = period;
		atomic64_set(&hwc->period_left, left);
	}

	if (unlikely(left <= 0)) {
		left += period;
		atomic64_set(&hwc->period_left, left);
	}

	per_cpu(prev_left[idx], smp_processor_id()) = left;

	/*
	 * The hw counter starts counting from this counter offset,
	 * mark it to be able to extra future deltas:
	 */
	atomic64_set(&hwc->prev_count, (u64)-left);

	err = checking_wrmsrl(hwc->counter_base + idx,
			     (u64)(-left) & counter_value_mask);
}

static inline void
__pmc_fixed_enable(struct perf_counter *counter,
		   struct hw_perf_counter *hwc, unsigned int __idx)
{
	int idx = __idx - X86_PMC_IDX_FIXED;
	u64 ctrl_val, bits, mask;
	int err;

	/*
	 * Enable IRQ generation (0x8),
	 * and enable ring-3 counting (0x2) and ring-0 counting (0x1)
	 * if requested:
	 */
	bits = 0x8ULL;
	if (hwc->config & ARCH_PERFMON_EVENTSEL_USR)
		bits |= 0x2;
	if (hwc->config & ARCH_PERFMON_EVENTSEL_OS)
		bits |= 0x1;
	bits <<= (idx * 4);
	mask = 0xfULL << (idx * 4);

	rdmsrl(hwc->config_base, ctrl_val);
	ctrl_val &= ~mask;
	ctrl_val |= bits;
	err = checking_wrmsrl(hwc->config_base, ctrl_val);
}

static void
__pmc_generic_enable(struct perf_counter *counter,
			  struct hw_perf_counter *hwc, int idx)
{
	if (unlikely(hwc->config_base == MSR_ARCH_PERFMON_FIXED_CTR_CTRL))
		__pmc_fixed_enable(counter, hwc, idx);
	else
		hw_perf_enable(idx, hwc->config);
}

static int
fixed_mode_idx(struct perf_counter *counter, struct hw_perf_counter *hwc)
{
	unsigned int event;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		return -1;

	if (unlikely(hwc->nmi))
		return -1;

	event = hwc->config & ARCH_PERFMON_EVENT_MASK;

	if (unlikely(event == pmc_ops->event_map(PERF_COUNT_INSTRUCTIONS)))
		return X86_PMC_IDX_FIXED_INSTRUCTIONS;
	if (unlikely(event == pmc_ops->event_map(PERF_COUNT_CPU_CYCLES)))
		return X86_PMC_IDX_FIXED_CPU_CYCLES;
	if (unlikely(event == pmc_ops->event_map(PERF_COUNT_BUS_CYCLES)))
		return X86_PMC_IDX_FIXED_BUS_CYCLES;

	return -1;
}

/*
 * Find a PMC slot for the freshly enabled / scheduled in counter:
 */
static int pmc_generic_enable(struct perf_counter *counter)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	struct hw_perf_counter *hwc = &counter->hw;
	int idx;

	idx = fixed_mode_idx(counter, hwc);
	if (idx >= 0) {
		/*
		 * Try to get the fixed counter, if that is already taken
		 * then try to get a generic counter:
		 */
		if (test_and_set_bit(idx, cpuc->used))
			goto try_generic;

		hwc->config_base = MSR_ARCH_PERFMON_FIXED_CTR_CTRL;
		/*
		 * We set it so that counter_base + idx in wrmsr/rdmsr maps to
		 * MSR_ARCH_PERFMON_FIXED_CTR0 ... CTR2:
		 */
		hwc->counter_base =
			MSR_ARCH_PERFMON_FIXED_CTR0 - X86_PMC_IDX_FIXED;
		hwc->idx = idx;
	} else {
		idx = hwc->idx;
		/* Try to get the previous generic counter again */
		if (test_and_set_bit(idx, cpuc->used)) {
try_generic:
			idx = find_first_zero_bit(cpuc->used, nr_counters_generic);
			if (idx == nr_counters_generic)
				return -EAGAIN;

			set_bit(idx, cpuc->used);
			hwc->idx = idx;
		}
		hwc->config_base  = pmc_ops->eventsel;
		hwc->counter_base = pmc_ops->perfctr;
	}

	perf_counters_lapic_init(hwc->nmi);

	__pmc_generic_disable(counter, hwc, idx);

	cpuc->counters[idx] = counter;
	/*
	 * Make it visible before enabling the hw:
	 */
	smp_wmb();

	__hw_perf_counter_set_period(counter, hwc, idx);
	__pmc_generic_enable(counter, hwc, idx);

	return 0;
}

void perf_counter_print_debug(void)
{
	u64 ctrl, status, overflow, pmc_ctrl, pmc_count, prev_left, fixed;
	struct cpu_hw_counters *cpuc;
	int cpu, idx;

	if (!nr_counters_generic)
		return;

	local_irq_disable();

	cpu = smp_processor_id();
	cpuc = &per_cpu(cpu_hw_counters, cpu);

	if (intel_perfmon_version >= 2) {
		rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, ctrl);
		rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);
		rdmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, overflow);
		rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR_CTRL, fixed);

		pr_info("\n");
		pr_info("CPU#%d: ctrl:       %016llx\n", cpu, ctrl);
		pr_info("CPU#%d: status:     %016llx\n", cpu, status);
		pr_info("CPU#%d: overflow:   %016llx\n", cpu, overflow);
		pr_info("CPU#%d: fixed:      %016llx\n", cpu, fixed);
	}
	pr_info("CPU#%d: used:       %016llx\n", cpu, *(u64 *)cpuc->used);

	for (idx = 0; idx < nr_counters_generic; idx++) {
		rdmsrl(pmc_ops->eventsel + idx, pmc_ctrl);
		rdmsrl(pmc_ops->perfctr  + idx, pmc_count);

		prev_left = per_cpu(prev_left[idx], cpu);

		pr_info("CPU#%d:   gen-PMC%d ctrl:  %016llx\n",
			cpu, idx, pmc_ctrl);
		pr_info("CPU#%d:   gen-PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
		pr_info("CPU#%d:   gen-PMC%d left:  %016llx\n",
			cpu, idx, prev_left);
	}
	for (idx = 0; idx < nr_counters_fixed; idx++) {
		rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR0 + idx, pmc_count);

		pr_info("CPU#%d: fixed-PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
	}
	local_irq_enable();
}

static void pmc_generic_disable(struct perf_counter *counter)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	struct hw_perf_counter *hwc = &counter->hw;
	unsigned int idx = hwc->idx;

	__pmc_generic_disable(counter, hwc, idx);

	clear_bit(idx, cpuc->used);
	cpuc->counters[idx] = NULL;
	/*
	 * Make sure the cleared pointer becomes visible before we
	 * (potentially) free the counter:
	 */
	smp_wmb();

	/*
	 * Drain the remaining delta count out of a counter
	 * that we are disabling:
	 */
	x86_perf_counter_update(counter, hwc, idx);
}

/*
 * Save and restart an expired counter. Called by NMI contexts,
 * so it has to be careful about preempting normal counter ops:
 */
static void perf_save_and_restart(struct perf_counter *counter)
{
	struct hw_perf_counter *hwc = &counter->hw;
	int idx = hwc->idx;

	x86_perf_counter_update(counter, hwc, idx);
	__hw_perf_counter_set_period(counter, hwc, idx);

	if (counter->state == PERF_COUNTER_STATE_ACTIVE)
		__pmc_generic_enable(counter, hwc, idx);
}

/*
 * Maximum interrupt frequency of 100KHz per CPU
 */
#define PERFMON_MAX_INTERRUPTS (100000/HZ)

/*
 * This handler is triggered by the local APIC, so the APIC IRQ handling
 * rules apply:
 */
static int __smp_perf_counter_interrupt(struct pt_regs *regs, int nmi)
{
	int bit, cpu = smp_processor_id();
	u64 ack, status;
	struct cpu_hw_counters *cpuc = &per_cpu(cpu_hw_counters, cpu);
	int ret = 0;

	cpuc->throttle_ctrl = hw_perf_save_disable();

	status = hw_perf_get_status(cpuc->throttle_ctrl);
	if (!status)
		goto out;

	ret = 1;
again:
	inc_irq_stat(apic_perf_irqs);
	ack = status;
	for_each_bit(bit, (unsigned long *)&status, X86_PMC_IDX_MAX) {
		struct perf_counter *counter = cpuc->counters[bit];

		clear_bit(bit, (unsigned long *) &status);
		if (!counter)
			continue;

		perf_save_and_restart(counter);
		if (perf_counter_overflow(counter, nmi, regs, 0))
			__pmc_generic_disable(counter, &counter->hw, bit);
	}

	hw_perf_ack_status(ack);

	/*
	 * Repeat if there is more work to be done:
	 */
	status = hw_perf_get_status(cpuc->throttle_ctrl);
	if (status)
		goto again;
out:
	/*
	 * Restore - do not reenable when global enable is off or throttled:
	 */
	if (++cpuc->interrupts < PERFMON_MAX_INTERRUPTS)
		hw_perf_restore(cpuc->throttle_ctrl);

	return ret;
}

void perf_counter_unthrottle(void)
{
	struct cpu_hw_counters *cpuc;

	if (!cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON))
		return;

	if (unlikely(!perf_counters_initialized))
		return;

	cpuc = &__get_cpu_var(cpu_hw_counters);
	if (cpuc->interrupts >= PERFMON_MAX_INTERRUPTS) {
		if (printk_ratelimit())
			printk(KERN_WARNING "PERFMON: max interrupts exceeded!\n");
		hw_perf_restore(cpuc->throttle_ctrl);
	}
	cpuc->interrupts = 0;
}

void smp_perf_counter_interrupt(struct pt_regs *regs)
{
	irq_enter();
	apic_write(APIC_LVTPC, LOCAL_PERF_VECTOR);
	ack_APIC_irq();
	__smp_perf_counter_interrupt(regs, 0);
	irq_exit();
}

void smp_perf_pending_interrupt(struct pt_regs *regs)
{
	irq_enter();
	ack_APIC_irq();
	inc_irq_stat(apic_pending_irqs);
	perf_counter_do_pending();
	irq_exit();
}

void set_perf_counter_pending(void)
{
	apic->send_IPI_self(LOCAL_PENDING_VECTOR);
}

void perf_counters_lapic_init(int nmi)
{
	u32 apic_val;

	if (!perf_counters_initialized)
		return;
	/*
	 * Enable the performance counter vector in the APIC LVT:
	 */
	apic_val = apic_read(APIC_LVTERR);

	apic_write(APIC_LVTERR, apic_val | APIC_LVT_MASKED);
	if (nmi)
		apic_write(APIC_LVTPC, APIC_DM_NMI);
	else
		apic_write(APIC_LVTPC, LOCAL_PERF_VECTOR);
	apic_write(APIC_LVTERR, apic_val);
}

static int __kprobes
perf_counter_nmi_handler(struct notifier_block *self,
			 unsigned long cmd, void *__args)
{
	struct die_args *args = __args;
	struct pt_regs *regs;
	int ret;

	switch (cmd) {
	case DIE_NMI:
	case DIE_NMI_IPI:
		break;

	default:
		return NOTIFY_DONE;
	}

	regs = args->regs;

	apic_write(APIC_LVTPC, APIC_DM_NMI);
	ret = __smp_perf_counter_interrupt(regs, 1);

	return ret ? NOTIFY_STOP : NOTIFY_OK;
}

static __read_mostly struct notifier_block perf_counter_nmi_notifier = {
	.notifier_call		= perf_counter_nmi_handler,
	.next			= NULL,
	.priority		= 1
};

static struct pmc_x86_ops pmc_intel_ops = {
	.save_disable_all	= pmc_intel_save_disable_all,
	.restore_all		= pmc_intel_restore_all,
	.get_status		= pmc_intel_get_status,
	.ack_status		= pmc_intel_ack_status,
	.enable			= pmc_intel_enable,
	.disable		= pmc_intel_disable,
	.eventsel		= MSR_ARCH_PERFMON_EVENTSEL0,
	.perfctr		= MSR_ARCH_PERFMON_PERFCTR0,
	.event_map		= pmc_intel_event_map,
	.raw_event		= pmc_intel_raw_event,
	.max_events		= ARRAY_SIZE(intel_perfmon_event_map),
};

static struct pmc_x86_ops pmc_amd_ops = {
	.save_disable_all	= pmc_amd_save_disable_all,
	.restore_all		= pmc_amd_restore_all,
	.get_status		= pmc_amd_get_status,
	.ack_status		= pmc_amd_ack_status,
	.enable			= pmc_amd_enable,
	.disable		= pmc_amd_disable,
	.eventsel		= MSR_K7_EVNTSEL0,
	.perfctr		= MSR_K7_PERFCTR0,
	.event_map		= pmc_amd_event_map,
	.raw_event		= pmc_amd_raw_event,
	.max_events		= ARRAY_SIZE(amd_perfmon_event_map),
};

static struct pmc_x86_ops *pmc_intel_init(void)
{
	union cpuid10_edx edx;
	union cpuid10_eax eax;
	unsigned int unused;
	unsigned int ebx;

	/*
	 * Check whether the Architectural PerfMon supports
	 * Branch Misses Retired Event or not.
	 */
	cpuid(10, &eax.full, &ebx, &unused, &edx.full);
	if (eax.split.mask_length <= ARCH_PERFMON_BRANCH_MISSES_RETIRED)
		return NULL;

	intel_perfmon_version = eax.split.version_id;
	if (intel_perfmon_version < 2)
		return NULL;

	pr_info("Intel Performance Monitoring support detected.\n");
	pr_info("... version:         %d\n", intel_perfmon_version);
	pr_info("... bit width:       %d\n", eax.split.bit_width);
	pr_info("... mask length:     %d\n", eax.split.mask_length);

	nr_counters_generic = eax.split.num_counters;
	nr_counters_fixed = edx.split.num_counters_fixed;
	counter_value_mask = (1ULL << eax.split.bit_width) - 1;

	return &pmc_intel_ops;
}

static struct pmc_x86_ops *pmc_amd_init(void)
{
	nr_counters_generic = 4;
	nr_counters_fixed = 0;
	counter_value_mask = 0x0000FFFFFFFFFFFFULL;
	counter_value_bits = 48;

	pr_info("AMD Performance Monitoring support detected.\n");

	return &pmc_amd_ops;
}

void __init init_hw_perf_counters(void)
{
	if (!cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON))
		return;

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		pmc_ops = pmc_intel_init();
		break;
	case X86_VENDOR_AMD:
		pmc_ops = pmc_amd_init();
		break;
	}
	if (!pmc_ops)
		return;

	pr_info("... num counters:    %d\n", nr_counters_generic);
	if (nr_counters_generic > X86_PMC_MAX_GENERIC) {
		nr_counters_generic = X86_PMC_MAX_GENERIC;
		WARN(1, KERN_ERR "hw perf counters %d > max(%d), clipping!",
			nr_counters_generic, X86_PMC_MAX_GENERIC);
	}
	perf_counter_mask = (1 << nr_counters_generic) - 1;
	perf_max_counters = nr_counters_generic;

	pr_info("... value mask:      %016Lx\n", counter_value_mask);

	if (nr_counters_fixed > X86_PMC_MAX_FIXED) {
		nr_counters_fixed = X86_PMC_MAX_FIXED;
		WARN(1, KERN_ERR "hw perf counters fixed %d > max(%d), clipping!",
			nr_counters_fixed, X86_PMC_MAX_FIXED);
	}
	pr_info("... fixed counters:  %d\n", nr_counters_fixed);

	perf_counter_mask |= ((1LL << nr_counters_fixed)-1) << X86_PMC_IDX_FIXED;

	pr_info("... counter mask:    %016Lx\n", perf_counter_mask);
	perf_counters_initialized = true;

	perf_counters_lapic_init(0);
	register_die_notifier(&perf_counter_nmi_notifier);
}

static void pmc_generic_read(struct perf_counter *counter)
{
	x86_perf_counter_update(counter, &counter->hw, counter->hw.idx);
}

static const struct hw_perf_counter_ops x86_perf_counter_ops = {
	.enable		= pmc_generic_enable,
	.disable	= pmc_generic_disable,
	.read		= pmc_generic_read,
};

const struct hw_perf_counter_ops *
hw_perf_counter_init(struct perf_counter *counter)
{
	int err;

	err = __hw_perf_counter_init(counter);
	if (err)
		return ERR_PTR(err);

	return &x86_perf_counter_ops;
}

/*
 * callchain support
 */

static inline
void callchain_store(struct perf_callchain_entry *entry, unsigned long ip)
{
	if (entry->nr < MAX_STACK_DEPTH)
		entry->ip[entry->nr++] = ip;
}

static DEFINE_PER_CPU(struct perf_callchain_entry, irq_entry);
static DEFINE_PER_CPU(struct perf_callchain_entry, nmi_entry);


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
	/* Don't bother with IRQ stacks for now */
	return -1;
}

static void backtrace_address(void *data, unsigned long addr, int reliable)
{
	struct perf_callchain_entry *entry = data;

	if (reliable)
		callchain_store(entry, addr);
}

static const struct stacktrace_ops backtrace_ops = {
	.warning		= backtrace_warning,
	.warning_symbol		= backtrace_warning_symbol,
	.stack			= backtrace_stack,
	.address		= backtrace_address,
};

static void
perf_callchain_kernel(struct pt_regs *regs, struct perf_callchain_entry *entry)
{
	unsigned long bp;
	char *stack;
	int nr = entry->nr;

	callchain_store(entry, instruction_pointer(regs));

	stack = ((char *)regs + sizeof(struct pt_regs));
#ifdef CONFIG_FRAME_POINTER
	bp = frame_pointer(regs);
#else
	bp = 0;
#endif

	dump_trace(NULL, regs, (void *)stack, bp, &backtrace_ops, entry);

	entry->kernel = entry->nr - nr;
}


struct stack_frame {
	const void __user	*next_fp;
	unsigned long		return_address;
};

static int copy_stack_frame(const void __user *fp, struct stack_frame *frame)
{
	int ret;

	if (!access_ok(VERIFY_READ, fp, sizeof(*frame)))
		return 0;

	ret = 1;
	pagefault_disable();
	if (__copy_from_user_inatomic(frame, fp, sizeof(*frame)))
		ret = 0;
	pagefault_enable();

	return ret;
}

static void
perf_callchain_user(struct pt_regs *regs, struct perf_callchain_entry *entry)
{
	struct stack_frame frame;
	const void __user *fp;
	int nr = entry->nr;

	regs = (struct pt_regs *)current->thread.sp0 - 1;
	fp   = (void __user *)regs->bp;

	callchain_store(entry, regs->ip);

	while (entry->nr < MAX_STACK_DEPTH) {
		frame.next_fp	     = NULL;
		frame.return_address = 0;

		if (!copy_stack_frame(fp, &frame))
			break;

		if ((unsigned long)fp < user_stack_pointer(regs))
			break;

		callchain_store(entry, frame.return_address);
		fp = frame.next_fp;
	}

	entry->user = entry->nr - nr;
}

static void
perf_do_callchain(struct pt_regs *regs, struct perf_callchain_entry *entry)
{
	int is_user;

	if (!regs)
		return;

	is_user = user_mode(regs);

	if (!current || current->pid == 0)
		return;

	if (is_user && current->state != TASK_RUNNING)
		return;

	if (!is_user)
		perf_callchain_kernel(regs, entry);

	if (current->mm)
		perf_callchain_user(regs, entry);
}

struct perf_callchain_entry *perf_callchain(struct pt_regs *regs)
{
	struct perf_callchain_entry *entry;

	if (in_nmi())
		entry = &__get_cpu_var(nmi_entry);
	else
		entry = &__get_cpu_var(irq_entry);

	entry->nr = 0;
	entry->hv = 0;
	entry->kernel = 0;
	entry->user = 0;

	perf_do_callchain(regs, entry);

	return entry;
}
