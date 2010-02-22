/*
 *  HW NMI watchdog support
 *
 *  started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 *  Arch specific calls to support NMI watchdog
 *
 *  Bits copied from original nmi.c file
 *
 */

#include <asm/apic.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/kernel_stat.h>
#include <asm/mce.h>

#include <linux/nmi.h>
#include <linux/module.h>

/* For reliability, we're prepared to waste bits here. */
static DECLARE_BITMAP(backtrace_mask, NR_CPUS) __read_mostly;

static DEFINE_PER_CPU(unsigned, last_irq_sum);

/*
 * Take the local apic timer and PIT/HPET into account. We don't
 * know which one is active, when we have highres/dyntick on
 */
static inline unsigned int get_timer_irqs(int cpu)
{
	unsigned int irqs = per_cpu(irq_stat, cpu).irq0_irqs;

#if defined(CONFIG_X86_LOCAL_APIC)
	irqs += per_cpu(irq_stat, cpu).apic_timer_irqs;
#endif

	return irqs;
}

static inline int mce_in_progress(void)
{
#if defined(CONFIG_X86_MCE)
	return atomic_read(&mce_entry) > 0;
#endif
	return 0;
}

int hw_nmi_is_cpu_stuck(struct pt_regs *regs)
{
	unsigned int sum;
	int cpu = smp_processor_id();

	/* FIXME: cheap hack for this check, probably should get its own
	 * die_notifier handler
	 */
	if (cpumask_test_cpu(cpu, to_cpumask(backtrace_mask))) {
		static DEFINE_SPINLOCK(lock);	/* Serialise the printks */

		spin_lock(&lock);
		printk(KERN_WARNING "NMI backtrace for cpu %d\n", cpu);
		show_regs(regs);
		dump_stack();
		spin_unlock(&lock);
		cpumask_clear_cpu(cpu, to_cpumask(backtrace_mask));
	}

	/* if we are doing an mce, just assume the cpu is not stuck */
	/* Could check oops_in_progress here too, but it's safer not to */
	if (mce_in_progress())
		return 0;

	/* We determine if the cpu is stuck by checking whether any
	 * interrupts have happened since we last checked.  Of course
	 * an nmi storm could create false positives, but the higher
	 * level logic should account for that
	 */
	sum = get_timer_irqs(cpu);
	if (__get_cpu_var(last_irq_sum) == sum) {
		return 1;
	} else {
		__get_cpu_var(last_irq_sum) = sum;
		return 0;
	}
}

u64 hw_nmi_get_sample_period(void)
{
	return cpu_khz * 1000;
}

#ifdef ARCH_HAS_NMI_WATCHDOG
void arch_trigger_all_cpu_backtrace(void)
{
	int i;

	cpumask_copy(to_cpumask(backtrace_mask), cpu_online_mask);

	printk(KERN_INFO "sending NMI to all CPUs:\n");
	apic->send_IPI_all(NMI_VECTOR);

	/* Wait for up to 10 seconds for all CPUs to do the backtrace */
	for (i = 0; i < 10 * 1000; i++) {
		if (cpumask_empty(to_cpumask(backtrace_mask)))
			break;
		mdelay(1);
	}
}
#endif

/* STUB calls to mimic old nmi_watchdog behaviour */
#if defined(CONFIG_X86_LOCAL_APIC)
unsigned int nmi_watchdog = NMI_NONE;
EXPORT_SYMBOL(nmi_watchdog);
void acpi_nmi_enable(void) { return; }
void acpi_nmi_disable(void) { return; }
#endif
atomic_t nmi_active = ATOMIC_INIT(0);           /* oprofile uses this */
EXPORT_SYMBOL(nmi_active);
int unknown_nmi_panic;
void cpu_nmi_set_wd_enabled(void) { return; }
void stop_apic_nmi_watchdog(void *unused) { return; }
void setup_apic_nmi_watchdog(void *unused) { return; }
int __init check_nmi_watchdog(void) { return 0; }
