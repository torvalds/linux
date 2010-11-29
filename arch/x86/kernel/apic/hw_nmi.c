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

#include <linux/cpumask.h>
#include <linux/kdebug.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/nmi.h>
#include <linux/module.h>

#ifdef CONFIG_HARDLOCKUP_DETECTOR
u64 hw_nmi_get_sample_period(void)
{
	return (u64)(cpu_khz) * 1000 * 60;
}
#endif

#ifndef CONFIG_HARDLOCKUP_DETECTOR
void touch_nmi_watchdog(void)
{
	touch_softlockup_watchdog();
}
EXPORT_SYMBOL(touch_nmi_watchdog);
#endif
#ifdef arch_trigger_all_cpu_backtrace
/* For reliability, we're prepared to waste bits here. */
static DECLARE_BITMAP(backtrace_mask, NR_CPUS) __read_mostly;

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

static int __kprobes
arch_trigger_all_cpu_backtrace_handler(struct notifier_block *self,
			 unsigned long cmd, void *__args)
{
	struct die_args *args = __args;
	struct pt_regs *regs;
	int cpu = smp_processor_id();

	switch (cmd) {
	case DIE_NMI:
	case DIE_NMI_IPI:
		break;

	default:
		return NOTIFY_DONE;
	}

	regs = args->regs;

	if (cpumask_test_cpu(cpu, to_cpumask(backtrace_mask))) {
		static arch_spinlock_t lock = __ARCH_SPIN_LOCK_UNLOCKED;

		arch_spin_lock(&lock);
		printk(KERN_WARNING "NMI backtrace for cpu %d\n", cpu);
		show_regs(regs);
		dump_stack();
		arch_spin_unlock(&lock);
		cpumask_clear_cpu(cpu, to_cpumask(backtrace_mask));
		return NOTIFY_STOP;
	}

	return NOTIFY_DONE;
}

static __read_mostly struct notifier_block backtrace_notifier = {
	.notifier_call          = arch_trigger_all_cpu_backtrace_handler,
	.next                   = NULL,
	.priority               = 1
};

static int __init register_trigger_all_cpu_backtrace(void)
{
	register_die_notifier(&backtrace_notifier);
	return 0;
}
early_initcall(register_trigger_all_cpu_backtrace);
#endif

/* STUB calls to mimic old nmi_watchdog behaviour */
int unknown_nmi_panic;
