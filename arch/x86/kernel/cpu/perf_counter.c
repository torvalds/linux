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

#include <asm/perf_counter.h>
#include <asm/apic.h>

static bool perf_counters_initialized __read_mostly;

/*
 * Number of (generic) HW counters:
 */
static int nr_counters_generic __read_mostly;
static u64 perf_counter_mask __read_mostly;
static u64 counter_value_mask __read_mostly;

static int nr_counters_fixed __read_mostly;

struct cpu_hw_counters {
	struct perf_counter	*counters[X86_PMC_IDX_MAX];
	unsigned long		used[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	unsigned long		interrupts;
	u64			global_enable;
};

/*
 * struct pmc_x86_ops - performance counter x86 ops
 */
struct pmc_x86_ops {
	u64 (*save_disable_all)		(void);
	void (*restore_all)		(u64 ctrl);
	unsigned eventsel;
	unsigned perfctr;
	int (*event_map)		(int event);
	int max_events;
};

static struct pmc_x86_ops *pmc_ops;

static DEFINE_PER_CPU(struct cpu_hw_counters, cpu_hw_counters);

/*
 * Intel PerfMon v3. Used on Core2 and later.
 */
static const int intel_perfmon_event_map[] =
{
  [PERF_COUNT_CPU_CYCLES]		= 0x003c,
  [PERF_COUNT_INSTRUCTIONS]		= 0x00c0,
  [PERF_COUNT_CACHE_REFERENCES]		= 0x4f2e,
  [PERF_COUNT_CACHE_MISSES]		= 0x412e,
  [PERF_COUNT_BRANCH_INSTRUCTIONS]	= 0x00c4,
  [PERF_COUNT_BRANCH_MISSES]		= 0x00c5,
  [PERF_COUNT_BUS_CYCLES]		= 0x013c,
};

static int pmc_intel_event_map(int event)
{
	return intel_perfmon_event_map[event];
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
	if ((s64)hwc->irq_period <= 0 || hwc->irq_period > 0x7FFFFFFF)
		hwc->irq_period = 0x7FFFFFFF;

	atomic64_set(&hwc->period_left, hwc->irq_period);

	/*
	 * Raw event type provide the config in the event structure
	 */
	if (hw_event->raw) {
		hwc->config |= hw_event->type;
	} else {
		if (hw_event->type >= pmc_ops->max_events)
			return -EINVAL;
		/*
		 * The generic map:
		 */
		hwc->config |= pmc_ops->event_map(hw_event->type);
	}
	counter->wakeup_pending = 0;

	return 0;
}

static u64 pmc_intel_save_disable_all(void)
{
	u64 ctrl;

	rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, ctrl);
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0);

	return ctrl;
}

u64 hw_perf_save_disable(void)
{
	if (unlikely(!perf_counters_initialized))
		return 0;

	return pmc_ops->save_disable_all();
}
EXPORT_SYMBOL_GPL(hw_perf_save_disable);

static void pmc_intel_restore_all(u64 ctrl)
{
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, ctrl);
}

void hw_perf_restore(u64 ctrl)
{
	if (unlikely(!perf_counters_initialized))
		return;

	pmc_ops->restore_all(ctrl);
}
EXPORT_SYMBOL_GPL(hw_perf_restore);

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
		wrmsr_safe(hwc->config_base + idx, hwc->config, 0);
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
	s32 period = hwc->irq_period;
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
		wrmsr(hwc->config_base + idx,
		      hwc->config | ARCH_PERFMON_EVENTSEL0_ENABLE, 0);
}

static int
fixed_mode_idx(struct perf_counter *counter, struct hw_perf_counter *hwc)
{
	unsigned int event;

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

	rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, ctrl);
	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);
	rdmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, overflow);
	rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR_CTRL, fixed);

	printk(KERN_INFO "\n");
	printk(KERN_INFO "CPU#%d: ctrl:       %016llx\n", cpu, ctrl);
	printk(KERN_INFO "CPU#%d: status:     %016llx\n", cpu, status);
	printk(KERN_INFO "CPU#%d: overflow:   %016llx\n", cpu, overflow);
	printk(KERN_INFO "CPU#%d: fixed:      %016llx\n", cpu, fixed);
	printk(KERN_INFO "CPU#%d: used:       %016llx\n", cpu, *(u64 *)cpuc->used);

	for (idx = 0; idx < nr_counters_generic; idx++) {
		rdmsrl(pmc_ops->eventsel + idx, pmc_ctrl);
		rdmsrl(pmc_ops->perfctr  + idx, pmc_count);

		prev_left = per_cpu(prev_left[idx], cpu);

		printk(KERN_INFO "CPU#%d:   gen-PMC%d ctrl:  %016llx\n",
			cpu, idx, pmc_ctrl);
		printk(KERN_INFO "CPU#%d:   gen-PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
		printk(KERN_INFO "CPU#%d:   gen-PMC%d left:  %016llx\n",
			cpu, idx, prev_left);
	}
	for (idx = 0; idx < nr_counters_fixed; idx++) {
		rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR0 + idx, pmc_count);

		printk(KERN_INFO "CPU#%d: fixed-PMC%d count: %016llx\n",
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

	x86_perf_counter_update(counter, hwc, idx);
	__hw_perf_counter_set_period(counter, hwc, idx);

	if (counter->state == PERF_COUNTER_STATE_ACTIVE)
		__pmc_generic_enable(counter, hwc, idx);
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
 * Maximum interrupt frequency of 100KHz per CPU
 */
#define PERFMON_MAX_INTERRUPTS 100000/HZ

/*
 * This handler is triggered by the local APIC, so the APIC IRQ handling
 * rules apply:
 */
static void __smp_perf_counter_interrupt(struct pt_regs *regs, int nmi)
{
	int bit, cpu = smp_processor_id();
	u64 ack, status;
	struct cpu_hw_counters *cpuc = &per_cpu(cpu_hw_counters, cpu);

	rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, cpuc->global_enable);

	/* Disable counters globally */
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0);
	ack_APIC_irq();

	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);
	if (!status)
		goto out;

again:
	inc_irq_stat(apic_perf_irqs);
	ack = status;
	for_each_bit(bit, (unsigned long *)&status, X86_PMC_IDX_MAX) {
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
		 * do a task wakeup - but we mark these generic as
		 * wakeup_pending and initate a wakeup callback:
		 */
		if (nmi) {
			counter->wakeup_pending = 1;
			set_tsk_thread_flag(current, TIF_PERF_COUNTERS);
		} else {
			wake_up(&counter->waitq);
		}
	}

	wrmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, ack);

	/*
	 * Repeat if there is more work to be done:
	 */
	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);
	if (status)
		goto again;
out:
	/*
	 * Restore - do not reenable when global enable is off or throttled:
	 */
	if (++cpuc->interrupts < PERFMON_MAX_INTERRUPTS)
		wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, cpuc->global_enable);
}

void perf_counter_unthrottle(void)
{
	struct cpu_hw_counters *cpuc;
	u64 global_enable;

	if (!cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON))
		return;

	if (unlikely(!perf_counters_initialized))
		return;

	cpuc = &per_cpu(cpu_hw_counters, smp_processor_id());
	if (cpuc->interrupts >= PERFMON_MAX_INTERRUPTS) {
		if (printk_ratelimit())
			printk(KERN_WARNING "PERFMON: max interrupts exceeded!\n");
		wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, cpuc->global_enable);
	}
	rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, global_enable);
	if (unlikely(cpuc->global_enable && !global_enable))
		wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, cpuc->global_enable);
	cpuc->interrupts = 0;
}

void smp_perf_counter_interrupt(struct pt_regs *regs)
{
	irq_enter();
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

	for_each_bit(bit, cpuc->used, X86_PMC_IDX_MAX) {
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

	if (likely(cmd != DIE_NMI_IPI))
		return NOTIFY_DONE;

	regs = args->regs;

	apic_write(APIC_LVTPC, APIC_DM_NMI);
	__smp_perf_counter_interrupt(regs, 1);

	return NOTIFY_STOP;
}

static __read_mostly struct notifier_block perf_counter_nmi_notifier = {
	.notifier_call		= perf_counter_nmi_handler,
	.next			= NULL,
	.priority		= 1
};

static struct pmc_x86_ops pmc_intel_ops = {
	.save_disable_all	= pmc_intel_save_disable_all,
	.restore_all		= pmc_intel_restore_all,
	.eventsel		= MSR_ARCH_PERFMON_EVENTSEL0,
	.perfctr		= MSR_ARCH_PERFMON_PERFCTR0,
	.event_map		= pmc_intel_event_map,
	.max_events		= ARRAY_SIZE(intel_perfmon_event_map),
};

static struct pmc_x86_ops *pmc_intel_init(void)
{
	union cpuid10_eax eax;
	unsigned int ebx;
	unsigned int unused;
	union cpuid10_edx edx;

	/*
	 * Check whether the Architectural PerfMon supports
	 * Branch Misses Retired Event or not.
	 */
	cpuid(10, &eax.full, &ebx, &unused, &edx.full);
	if (eax.split.mask_length <= ARCH_PERFMON_BRANCH_MISSES_RETIRED)
		return NULL;

	printk(KERN_INFO "Intel Performance Monitoring support detected.\n");
	printk(KERN_INFO "... version:         %d\n", eax.split.version_id);
	printk(KERN_INFO "... bit width:       %d\n", eax.split.bit_width);
	printk(KERN_INFO "... mask length:     %d\n", eax.split.mask_length);

	nr_counters_generic = eax.split.num_counters;
	nr_counters_fixed = edx.split.num_counters_fixed;
	counter_value_mask = (1ULL << eax.split.bit_width) - 1;

	return &pmc_intel_ops;
}

void __init init_hw_perf_counters(void)
{
	if (!cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON))
		return;

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		pmc_ops = pmc_intel_init();
		break;
	}
	if (!pmc_ops)
		return;

	printk(KERN_INFO "... num counters:    %d\n", nr_counters_generic);
	if (nr_counters_generic > X86_PMC_MAX_GENERIC) {
		nr_counters_generic = X86_PMC_MAX_GENERIC;
		WARN(1, KERN_ERR "hw perf counters %d > max(%d), clipping!",
			nr_counters_generic, X86_PMC_MAX_GENERIC);
	}
	perf_counter_mask = (1 << nr_counters_generic) - 1;
	perf_max_counters = nr_counters_generic;

	printk(KERN_INFO "... value mask:      %016Lx\n", counter_value_mask);

	if (nr_counters_fixed > X86_PMC_MAX_FIXED) {
		nr_counters_fixed = X86_PMC_MAX_FIXED;
		WARN(1, KERN_ERR "hw perf counters fixed %d > max(%d), clipping!",
			nr_counters_fixed, X86_PMC_MAX_FIXED);
	}
	printk(KERN_INFO "... fixed counters:  %d\n", nr_counters_fixed);

	perf_counter_mask |= ((1LL << nr_counters_fixed)-1) << X86_PMC_IDX_FIXED;

	printk(KERN_INFO "... counter mask:    %016Lx\n", perf_counter_mask);
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
		return NULL;

	return &x86_perf_counter_ops;
}
