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
	int			enable_all;
};

/*
 * Intel PerfMon v3. Used on Core2 and later.
 */
static DEFINE_PER_CPU(struct cpu_hw_counters, cpu_hw_counters);

const int intel_perfmon_event_map[] =
{
  [PERF_COUNT_CYCLES]			= 0x003c,
  [PERF_COUNT_INSTRUCTIONS]		= 0x00c0,
  [PERF_COUNT_CACHE_REFERENCES]		= 0x4f2e,
  [PERF_COUNT_CACHE_MISSES]		= 0x412e,
  [PERF_COUNT_BRANCH_INSTRUCTIONS]	= 0x00c4,
  [PERF_COUNT_BRANCH_MISSES]		= 0x00c5,
};

const int max_intel_perfmon_events = ARRAY_SIZE(intel_perfmon_event_map);

/*
 * Setup the hardware configuration for a given hw_event_type
 */
int hw_perf_counter_init(struct perf_counter *counter, s32 hw_event_type)
{
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
		if (hw_event_type & PERF_COUNT_NMI)
			hwc->nmi = 1;
	}

	hwc->config_base = MSR_ARCH_PERFMON_EVENTSEL0;
	hwc->counter_base = MSR_ARCH_PERFMON_PERFCTR0;

	hwc->irq_period = counter->__irq_period;
	/*
	 * Intel PMCs cannot be accessed sanely above 32 bit width,
	 * so we install an artificial 1<<31 period regardless of
	 * the generic counter period:
	 */
	if (!hwc->irq_period)
		hwc->irq_period = 0x7FFFFFFF;

	hwc->next_count = -((s32) hwc->irq_period);

	/*
	 * Negative event types mean raw encoded event+umask values:
	 */
	if (hw_event_type < 0) {
		counter->hw_event_type = -hw_event_type;
		counter->hw_event_type &= ~PERF_COUNT_NMI;
	} else {
		hw_event_type &= ~PERF_COUNT_NMI;
		if (hw_event_type >= max_intel_perfmon_events)
			return -EINVAL;
		/*
		 * The generic map:
		 */
		counter->hw_event_type = intel_perfmon_event_map[hw_event_type];
	}
	hwc->config |= counter->hw_event_type;
	counter->wakeup_pending = 0;

	return 0;
}

static void __hw_perf_enable_all(void)
{
	wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, perf_counter_mask, 0);
}

void hw_perf_enable_all(void)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);

	cpuc->enable_all = 1;
	__hw_perf_enable_all();
}

void hw_perf_disable_all(void)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);

	cpuc->enable_all = 0;
	wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0, 0);
}

static DEFINE_PER_CPU(u64, prev_next_count[MAX_HW_COUNTERS]);

static void __hw_perf_counter_enable(struct hw_perf_counter *hwc, int idx)
{
	per_cpu(prev_next_count[idx], smp_processor_id()) = hwc->next_count;

	wrmsr(hwc->counter_base + idx, hwc->next_count, 0);
	wrmsr(hwc->config_base + idx, hwc->config, 0);
}

void hw_perf_counter_enable(struct perf_counter *counter)
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

	wrmsr(hwc->config_base + idx,
	      hwc->config & ~ARCH_PERFMON_EVENTSEL0_ENABLE, 0);

	cpuc->counters[idx] = counter;
	counter->hw.config |= ARCH_PERFMON_EVENTSEL0_ENABLE;
	__hw_perf_counter_enable(hwc, idx);
}

#ifdef CONFIG_X86_64
static inline void atomic64_counter_set(struct perf_counter *counter, u64 val)
{
	atomic64_set(&counter->count, val);
}

static inline u64 atomic64_counter_read(struct perf_counter *counter)
{
	return atomic64_read(&counter->count);
}
#else
/*
 * Todo: add proper atomic64_t support to 32-bit x86:
 */
static inline void atomic64_counter_set(struct perf_counter *counter, u64 val64)
{
	u32 *val32 = (void *)&val64;

	atomic_set(counter->count32 + 0, *(val32 + 0));
	atomic_set(counter->count32 + 1, *(val32 + 1));
}

static inline u64 atomic64_counter_read(struct perf_counter *counter)
{
	return atomic_read(counter->count32 + 0) |
		(u64) atomic_read(counter->count32 + 1) << 32;
}
#endif

static void __hw_perf_save_counter(struct perf_counter *counter,
				   struct hw_perf_counter *hwc, int idx)
{
	s64 raw = -1;
	s64 delta;
	int err;

	/*
	 * Get the raw hw counter value:
	 */
	err = rdmsrl_safe(hwc->counter_base + idx, &raw);
	WARN_ON_ONCE(err);

	/*
	 * Rebase it to zero (it started counting at -irq_period),
	 * to see the delta since ->prev_count:
	 */
	delta = (s64)hwc->irq_period + (s64)(s32)raw;

	atomic64_counter_set(counter, hwc->prev_count + delta);

	/*
	 * Adjust the ->prev_count offset - if we went beyond
	 * irq_period of units, then we got an IRQ and the counter
	 * was set back to -irq_period:
	 */
	while (delta >= (s64)hwc->irq_period) {
		hwc->prev_count += hwc->irq_period;
		delta -= (s64)hwc->irq_period;
	}

	/*
	 * Calculate the next raw counter value we'll write into
	 * the counter at the next sched-in time:
	 */
	delta -= (s64)hwc->irq_period;

	hwc->next_count = (s32)delta;
}

void perf_counter_print_debug(void)
{
	u64 ctrl, status, overflow, pmc_ctrl, pmc_count, next_count;
	int cpu, err, idx;

	local_irq_disable();

	cpu = smp_processor_id();

	err = rdmsrl_safe(MSR_CORE_PERF_GLOBAL_CTRL, &ctrl);
	WARN_ON_ONCE(err);

	err = rdmsrl_safe(MSR_CORE_PERF_GLOBAL_STATUS, &status);
	WARN_ON_ONCE(err);

	err = rdmsrl_safe(MSR_CORE_PERF_GLOBAL_OVF_CTRL, &overflow);
	WARN_ON_ONCE(err);

	printk(KERN_INFO "\n");
	printk(KERN_INFO "CPU#%d: ctrl:       %016llx\n", cpu, ctrl);
	printk(KERN_INFO "CPU#%d: status:     %016llx\n", cpu, status);
	printk(KERN_INFO "CPU#%d: overflow:   %016llx\n", cpu, overflow);

	for (idx = 0; idx < nr_hw_counters; idx++) {
		err = rdmsrl_safe(MSR_ARCH_PERFMON_EVENTSEL0 + idx, &pmc_ctrl);
		WARN_ON_ONCE(err);

		err = rdmsrl_safe(MSR_ARCH_PERFMON_PERFCTR0 + idx, &pmc_count);
		WARN_ON_ONCE(err);

		next_count = per_cpu(prev_next_count[idx], cpu);

		printk(KERN_INFO "CPU#%d: PMC%d ctrl:  %016llx\n",
			cpu, idx, pmc_ctrl);
		printk(KERN_INFO "CPU#%d: PMC%d count: %016llx\n",
			cpu, idx, pmc_count);
		printk(KERN_INFO "CPU#%d: PMC%d next:  %016llx\n",
			cpu, idx, next_count);
	}
	local_irq_enable();
}

void hw_perf_counter_disable(struct perf_counter *counter)
{
	struct cpu_hw_counters *cpuc = &__get_cpu_var(cpu_hw_counters);
	struct hw_perf_counter *hwc = &counter->hw;
	unsigned int idx = hwc->idx;

	counter->hw.config &= ~ARCH_PERFMON_EVENTSEL0_ENABLE;
	wrmsr(hwc->config_base + idx, hwc->config, 0);

	clear_bit(idx, cpuc->used);
	cpuc->counters[idx] = NULL;
	__hw_perf_save_counter(counter, hwc, idx);
}

void hw_perf_counter_read(struct perf_counter *counter)
{
	struct hw_perf_counter *hwc = &counter->hw;
	unsigned long addr = hwc->counter_base + hwc->idx;
	s64 offs, val = -1LL;
	s32 val32;
	int err;

	/* Careful: NMI might modify the counter offset */
	do {
		offs = hwc->prev_count;
		err = rdmsrl_safe(addr, &val);
		WARN_ON_ONCE(err);
	} while (offs != hwc->prev_count);

	val32 = (s32) val;
	val =  (s64)hwc->irq_period + (s64)val32;
	atomic64_counter_set(counter, hwc->prev_count + val);
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

static void perf_save_and_restart(struct perf_counter *counter)
{
	struct hw_perf_counter *hwc = &counter->hw;
	int idx = hwc->idx;

	wrmsr(hwc->config_base + idx,
	      hwc->config & ~ARCH_PERFMON_EVENTSEL0_ENABLE, 0);

	if (hwc->config & ARCH_PERFMON_EVENTSEL0_ENABLE) {
		__hw_perf_save_counter(counter, hwc, idx);
		__hw_perf_counter_enable(hwc, idx);
	}
}

static void
perf_handle_group(struct perf_counter *leader, u64 *status, u64 *overflown)
{
	struct perf_counter_context *ctx = leader->ctx;
	struct perf_counter *counter;
	int bit;

	list_for_each_entry(counter, &ctx->counters, list) {
		if (counter->record_type != PERF_RECORD_SIMPLE ||
		    counter == leader)
			continue;

		if (counter->active) {
			/*
			 * When counter was not in the overflow mask, we have to
			 * read it from hardware. We read it as well, when it
			 * has not been read yet and clear the bit in the
			 * status mask.
			 */
			bit = counter->hw.idx;
			if (!test_bit(bit, (unsigned long *) overflown) ||
			    test_bit(bit, (unsigned long *) status)) {
				clear_bit(bit, (unsigned long *) status);
				perf_save_and_restart(counter);
			}
		}
		perf_store_irq_data(leader, counter->hw_event_type);
		perf_store_irq_data(leader, atomic64_counter_read(counter));
	}
}

/*
 * This handler is triggered by the local APIC, so the APIC IRQ handling
 * rules apply:
 */
static void __smp_perf_counter_interrupt(struct pt_regs *regs, int nmi)
{
	int bit, cpu = smp_processor_id();
	struct cpu_hw_counters *cpuc;
	u64 ack, status;

	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, status);
	if (!status) {
		ack_APIC_irq();
		return;
	}

	/* Disable counters globally */
	wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0, 0);
	ack_APIC_irq();

	cpuc = &per_cpu(cpu_hw_counters, cpu);

again:
	ack = status;
	for_each_bit(bit, (unsigned long *) &status, nr_hw_counters) {
		struct perf_counter *counter = cpuc->counters[bit];

		clear_bit(bit, (unsigned long *) &status);
		if (!counter)
			continue;

		perf_save_and_restart(counter);

		switch (counter->record_type) {
		case PERF_RECORD_SIMPLE:
			continue;
		case PERF_RECORD_IRQ:
			perf_store_irq_data(counter, instruction_pointer(regs));
			break;
		case PERF_RECORD_GROUP:
			perf_store_irq_data(counter, counter->hw_event_type);
			perf_store_irq_data(counter,
					    atomic64_counter_read(counter));
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

	/*
	 * Do not reenable when global enable is off:
	 */
	if (cpuc->enable_all)
		__hw_perf_enable_all();
}

void smp_perf_counter_interrupt(struct pt_regs *regs)
{
	irq_enter();
#ifdef CONFIG_X86_64
	add_pda(apic_perf_irqs, 1);
#else
	per_cpu(irq_stat, smp_processor_id()).apic_perf_irqs++;
#endif
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

	perf_counters_lapic_init(0);
	register_die_notifier(&perf_counter_nmi_notifier);

	perf_counters_initialized = true;
}
