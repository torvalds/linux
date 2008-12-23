/*
 * Performance counter x86 architecture code
 *
 *  Copyright(C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright(C) 2008 Red Hat, Inc., Ingo Molnar
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

#include <asm/intel_arch_perfmon.h>
#include <asm/apic.h>

static bool perf_counters_initialized __read_mostly;

/*
 * Number of (generic) HW counters:
 */
static int nr_hw_counters __read_mostly;
static u32 perf_counter_mask __read_mostly;

/* No support for fixed function counters yet */

#define MAX_HW_COUNTERS		8

struct cpu_hw_counters {
	struct perf_counter	*counters[MAX_HW_COUNTERS];
	unsigned long		used[BITS_TO_LONGS(MAX_HW_COUNTERS)];
};

/*
 * Intel PerfMon v3. Used on Core2 and later.
 */
static DEFINE_PER_CPU(struct cpu_hw_counters, cpu_hw_counters);

static const int intel_perfmon_event_map[] =
{
  [PERF_COUNT_CYCLES]			= 0x003c,
  [PERF_COUNT_INSTRUCTIONS]		= 0x00c0,
  [PERF_COUNT_CACHE_REFERENCES]		= 0x4f2e,
  [PERF_COUNT_CACHE_MISSES]		= 0x412e,
  [PERF_COUNT_BRANCH_INSTRUCTIONS]	= 0x00c4,
  [PERF_COUNT_BRANCH_MISSES]		= 0x00c5,
};

static const int max_intel_perfmon_events = ARRAY_SIZE(intel_perfmon_event_map);

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

/*
 * Setup the hardware configuration for a given hw_event_type
 */
static int __hw_perf_counter_init(struct perf_counter *counter)
{
	struct perf_counter_hw_event *hw_event = &counter->hw_event;
	struct hw_perf_counter *hwc = &counter->hw;

	if (unlikely(!perf_counters_initialized))
		return -EINVAL;

	/*
	 * Count user events, and generate PMC IRQs:
	 * (keep 'enabled' bit clear for now)
	 */
	hwc->config = ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_INT;

	/*
	 * If privileged enough, count OS events too, and allow
	 * NMI events as well:
	 */
	hwc->nmi = 0;
	if (capable(CAP_SYS_ADMIN)) {
		hwc->config |= ARCH_PERFMON_EVENTSEL_OS;
		if (hw_event->nmi)
			hwc->nmi = 1;
	}

	hwc->config_base	= MSR_ARCH_PERFMON_EVENTSEL0;
	hwc->counter_base	= MSR_ARCH_PERFMON_PERFCTR0;

	hwc->irq_period		= hw_event->irq_period;
	/*
	 * Intel PMCs cannot be accessed sanely above 32 bit width,
	 * so we install an artificial 1<<31 period regardless of
	 * the generic counter period:
	 */
	if ((s64)hwc->irq_period <= 0 || hwc->irq_period > 0x7FFFFFFF)
		hwc->irq_period = 0x7FFFFFFF;

	atomic64_set(&hwc->period_left, hwc->irq_period);

	/*
	 * Raw event type provide the config in the event structure
	 */
	if (hw_event->raw) {
		hwc->config |= hw_event->type;
	} else {
		if (hw_event->type >= max_intel_perfmon_events)
			return -EINVAL;
		/*
		 * The generic map:
		 */
		hwc->config |= intel_perfmon_event_map[hw_event->type];
	}
	counter->wakeup_pending = 0;

	return 0;
}

void hw_perf_enable_all(void)
{
	if (unlikely(!perf_counters_initialized))
		return;

	wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, perf_counter_mask, 0);
}

u64 hw_perf_save_disable(void)
{
	u64 ctrl;

	if (unlikely(!perf_counters_initialized))
		return 0;

	rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, ctrl);
	wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0, 0);

	return ctrl;
}
EXPORT_SYMBOL_GPL(hw_perf_save_disable);

void hw_perf_restore(u64 ctrl)
{
	if (unlikely(!perf_counters_initialized))
		return;

	wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, ctrl, 0);
}
EXPORT_SYMBOL_GPL(hw_perf_restore);

static inline void
__x86_perf_counter_disable(struct perf_counter *counter,
			   struct hw_perf_counter *hwc, unsigned int idx)
{
	int err;

	err = wrmsr_safe(hwc->config_base + idx, hwc->config, 0);
}

static DEFINE_PER_CPU(u64, prev_left[MAX_HW_COUNTERS]);

/*
 * Set the next IRQ period, based on the hwc->period_left value.
 * To be called with the counter disabled in hw:
 */
static void
__hw_perf_counter_set_period(struct perf_counter *counter,
			     struct hw_perf_counter *hwc, int idx)
{
	s32 left = atomic64_read(&hwc->period_left);
	s32 period = hwc->irq_period;

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
	atomic64_set(&hwc->prev_count, (u64)(s64)-left);

	wrmsr(hwc->counter_base + idx, -left, 0);
}

static void
__x86_perf_counter_enable(struct perf_counter *counter,
			  struct hw_perf_counter *hwc, int idx)
{
	wrmsr(hwc->config_base + idx,
	      hwc->config | ARCH_PERFMON_EVENTSEL0_ENABLE, 0);
}

/*
 * Find a PMC slot for the freshly enabled / scheduled in counter:
 */
static void x86_perf_counter_enable(struct perf_counter *counter)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	struct hw_perf_counter *hwc = &counter->hw;
	int idx = hwc->idx;

	/* Try to get the previous counter again */
	if (test_and_set_bit(idx, cpuc->used)) {
		idx = find_first_zero_bit(cpuc->used, nr_hw_counters);
		set_bit(idx, cpuc->used);
		hwc->idx = idx;
	}

	perf_counters_lapic_init(hwc->nmi);

	__x86_perf_counter_disable(counter, hwc, idx);

	cpuc->counters[idx] = counter;

	__hw_perf_counter_set_period(counter, hwc, idx);
	__x86_perf_counter_enable(counter, hwc, idx);
}

void perf_counter_print_debug(void)
{
	u64 ctrl, status, overflow, pmc_ctrl, pmc_count, prev_left;
	int cpu, idx;

	if (!nr_hw_counters)
		return;

	local_irq_disable();

	cpu = smp_processor_id();

	rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, ctrl);
	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);
	rdmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, overflow);

	printk(KERN_INFO "\n");
	printk(KERN_INFO "CPU#%d: ctrl:       %016llx\n", cpu, ctrl);
	printk(KERN_INFO "CPU#%d: status:     %016llx\n", cpu, status);
	printk(KERN_INFO "CPU#%d: overflow:   %016llx\n", cpu, overflow);

	for (idx = 0; idx < nr_hw_counters; idx++) {
		rdmsrl(MSR_ARCH_PERFMON_EVENTSEL0 + idx, pmc_ctrl);
		rdmsrl(MSR_ARCH_PERFMON_PERFCTR0  + idx, pmc_count);

		prev_left = per_cpu(prev_left[idx], cpu);

		printk(KERN_INFO "CPU#%d: PMC%d ctrl:  %016llx\n",
			cpu, idx, pmc_ctrl);
		printk(KERN_INFO "CPU#%d: PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
		printk(KERN_INFO "CPU#%d: PMC%d left:  %016llx\n",
			cpu, idx, prev_left);
	}
	local_irq_enable();
}

static void x86_perf_counter_disable(struct perf_counter *counter)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	struct hw_perf_counter *hwc = &counter->hw;
	unsigned int idx = hwc->idx;

	__x86_perf_counter_disable(counter, hwc, idx);

	clear_bit(idx, cpuc->used);
	cpuc->counters[idx] = NULL;

	/*
	 * Drain the remaining delta count out of a counter
	 * that we are disabling:
	 */
	x86_perf_counter_update(counter, hwc, idx);
}

static void perf_store_irq_data(struct perf_counter *counter, u64 data)
{
	struct perf_data *irqdata = counter->irqdata;

	if (irqdata->len > PERF_DATA_BUFLEN - sizeof(u64)) {
		irqdata->overrun++;
	} else {
		u64 *p = (u64 *) &irqdata->data[irqdata->len];

		*p = data;
		irqdata->len += sizeof(u64);
	}
}

/*
 * Save and restart an expired counter. Called by NMI contexts,
 * so it has to be careful about preempting normal counter ops:
 */
static void perf_save_and_restart(struct perf_counter *counter)
{
	struct hw_perf_counter *hwc = &counter->hw;
	int idx = hwc->idx;
	u64 pmc_ctrl;

	rdmsrl(MSR_ARCH_PERFMON_EVENTSEL0 + idx, pmc_ctrl);

	x86_perf_counter_update(counter, hwc, idx);
	__hw_perf_counter_set_period(counter, hwc, idx);

	if (pmc_ctrl & ARCH_PERFMON_EVENTSEL0_ENABLE)
		__x86_perf_counter_enable(counter, hwc, idx);
}

static void
perf_handle_group(struct perf_counter *sibling, u64 *status, u64 *overflown)
{
	struct perf_counter *counter, *group_leader = sibling->group_leader;

	/*
	 * Store sibling timestamps (if any):
	 */
	list_for_each_entry(counter, &group_leader->sibling_list, list_entry) {
		x86_perf_counter_update(counter, &counter->hw, counter->hw.idx);
		perf_store_irq_data(sibling, counter->hw_event.type);
		perf_store_irq_data(sibling, atomic64_read(&counter->count));
	}
}

/*
 * This handler is triggered by the local APIC, so the APIC IRQ handling
 * rules apply:
 */
static void __smp_perf_counter_interrupt(struct pt_regs *regs, int nmi)
{
	int bit, cpu = smp_processor_id();
	u64 ack, status, saved_global;
	struct cpu_hw_counters *cpuc;

	rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, saved_global);

	/* Disable counters globally */
	wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0, 0);
	ack_APIC_irq();

	cpuc = &per_cpu(cpu_hw_counters, cpu);

	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);
	if (!status)
		goto out;

again:
	ack = status;
	for_each_bit(bit, (unsigned long *) &status, nr_hw_counters) {
		struct perf_counter *counter = cpuc->counters[bit];

		clear_bit(bit, (unsigned long *) &status);
		if (!counter)
			continue;

		perf_save_and_restart(counter);

		switch (counter->hw_event.record_type) {
		case PERF_RECORD_SIMPLE:
			continue;
		case PERF_RECORD_IRQ:
			perf_store_irq_data(counter, instruction_pointer(regs));
			break;
		case PERF_RECORD_GROUP:
			perf_handle_group(counter, &status, &ack);
			break;
		}
		/*
		 * From NMI context we cannot call into the scheduler to
		 * do a task wakeup - but we mark these counters as
		 * wakeup_pending and initate a wakeup callback:
		 */
		if (nmi) {
			counter->wakeup_pending = 1;
			set_tsk_thread_flag(current, TIF_PERF_COUNTERS);
		} else {
			wake_up(&counter->waitq);
		}
	}

	wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, ack, 0);

	/*
	 * Repeat if there is more work to be done:
	 */
	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);
	if (status)
		goto again;
out:
	/*
	 * Restore - do not reenable when global enable is off:
	 */
	wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, saved_global, 0);
}

void smp_perf_counter_interrupt(struct pt_regs *regs)
{
	irq_enter();
	inc_irq_stat(apic_perf_irqs);
	apic_write(APIC_LVTPC, LOCAL_PERF_VECTOR);
	__smp_perf_counter_interrupt(regs, 0);

	irq_exit();
}

/*
 * This handler is triggered by NMI contexts:
 */
void perf_counter_notify(struct pt_regs *regs)
{
	struct cpu_hw_counters *cpuc;
	unsigned long flags;
	int bit, cpu;

	local_irq_save(flags);
	cpu = smp_processor_id();
	cpuc = &per_cpu(cpu_hw_counters, cpu);

	for_each_bit(bit, cpuc->used, nr_hw_counters) {
		struct perf_counter *counter = cpuc->counters[bit];

		if (!counter)
			continue;

		if (counter->wakeup_pending) {
			counter->wakeup_pending = 0;
			wake_up(&counter->waitq);
		}
	}

	local_irq_restore(flags);
}

void __cpuinit perf_counters_lapic_init(int nmi)
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

	if (likely(cmd != DIE_NMI_IPI))
		return NOTIFY_DONE;

	regs = args->regs;

	apic_write(APIC_LVTPC, APIC_DM_NMI);
	__smp_perf_counter_interrupt(regs, 1);

	return NOTIFY_STOP;
}

static __read_mostly struct notifier_block perf_counter_nmi_notifier = {
	.notifier_call		= perf_counter_nmi_handler
};

void __init init_hw_perf_counters(void)
{
	union cpuid10_eax eax;
	unsigned int unused;
	unsigned int ebx;

	if (!cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON))
		return;

	/*
	 * Check whether the Architectural PerfMon supports
	 * Branch Misses Retired Event or not.
	 */
	cpuid(10, &(eax.full), &ebx, &unused, &unused);
	if (eax.split.mask_length <= ARCH_PERFMON_BRANCH_MISSES_RETIRED)
		return;

	printk(KERN_INFO "Intel Performance Monitoring support detected.\n");

	printk(KERN_INFO "... version:      %d\n", eax.split.version_id);
	printk(KERN_INFO "... num_counters: %d\n", eax.split.num_counters);
	nr_hw_counters = eax.split.num_counters;
	if (nr_hw_counters > MAX_HW_COUNTERS) {
		nr_hw_counters = MAX_HW_COUNTERS;
		WARN(1, KERN_ERR "hw perf counters %d > max(%d), clipping!",
			nr_hw_counters, MAX_HW_COUNTERS);
	}
	perf_counter_mask = (1 << nr_hw_counters) - 1;
	perf_max_counters = nr_hw_counters;

	printk(KERN_INFO "... bit_width:    %d\n", eax.split.bit_width);
	printk(KERN_INFO "... mask_length:  %d\n", eax.split.mask_length);

	perf_counters_initialized = true;

	perf_counters_lapic_init(0);
	register_die_notifier(&perf_counter_nmi_notifier);
}

static void x86_perf_counter_read(struct perf_counter *counter)
{
	x86_perf_counter_update(counter, &counter->hw, counter->hw.idx);
}

static const struct hw_perf_counter_ops x86_perf_counter_ops = {
	.hw_perf_counter_enable		= x86_perf_counter_enable,
	.hw_perf_counter_disable	= x86_perf_counter_disable,
	.hw_perf_counter_read		= x86_perf_counter_read,
};

const struct hw_perf_counter_ops *
hw_perf_counter_init(struct perf_counter *counter)
{
	int err;

	err = __hw_perf_counter_init(counter);
	if (err)
		return NULL;

	return &x86_perf_counter_ops;
}
