/*
 *  NMI watchdog support on APIC systems
 *
 *  Started by Ingo Molnar <mingo@redhat.com>
 *
 *  Fixes:
 *  Mikael Pettersson	: AMD K7 support for local APIC NMI watchdog.
 *  Mikael Pettersson	: Power Management for local APIC NMI watchdog.
 *  Mikael Pettersson	: Pentium 4 support for local APIC NMI watchdog.
 *  Pavel Machek and
 *  Mikael Pettersson	: PM converted to driver model. Disable/enable API.
 */

#include <asm/apic.h>

#include <linux/nmi.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/sysdev.h>
#include <linux/sysctl.h>
#include <linux/percpu.h>
#include <linux/kprobes.h>
#include <linux/cpumask.h>
#include <linux/kernel_stat.h>
#include <linux/kdebug.h>
#include <linux/smp.h>

#include <asm/i8259.h>
#include <asm/io_apic.h>
#include <asm/proto.h>
#include <asm/timer.h>

#include <asm/mce.h>

#include <asm/mach_traps.h>

int unknown_nmi_panic;
int nmi_watchdog_enabled;

static cpumask_var_t backtrace_mask;

/* nmi_active:
 * >0: the lapic NMI watchdog is active, but can be disabled
 * <0: the lapic NMI watchdog has not been set up, and cannot
 *     be enabled
 *  0: the lapic NMI watchdog is disabled, but can be enabled
 */
atomic_t nmi_active = ATOMIC_INIT(0);		/* oprofile uses this */
EXPORT_SYMBOL(nmi_active);

unsigned int nmi_watchdog = NMI_NONE;
EXPORT_SYMBOL(nmi_watchdog);

static int panic_on_timeout;

static unsigned int nmi_hz = HZ;
static DEFINE_PER_CPU(short, wd_enabled);
static int endflag __initdata;

static inline unsigned int get_nmi_count(int cpu)
{
	return per_cpu(irq_stat, cpu).__nmi_count;
}

static inline int mce_in_progress(void)
{
#if defined(CONFIG_X86_NEW_MCE)
	return atomic_read(&mce_entry) > 0;
#endif
	return 0;
}

/*
 * Take the local apic timer and PIT/HPET into account. We don't
 * know which one is active, when we have highres/dyntick on
 */
static inline unsigned int get_timer_irqs(int cpu)
{
	return per_cpu(irq_stat, cpu).apic_timer_irqs +
		per_cpu(irq_stat, cpu).irq0_irqs;
}

#ifdef CONFIG_SMP
/*
 * The performance counters used by NMI_LOCAL_APIC don't trigger when
 * the CPU is idle. To make sure the NMI watchdog really ticks on all
 * CPUs during the test make them busy.
 */
static __init void nmi_cpu_busy(void *data)
{
	local_irq_enable_in_hardirq();
	/*
	 * Intentionally don't use cpu_relax here. This is
	 * to make sure that the performance counter really ticks,
	 * even if there is a simulator or similar that catches the
	 * pause instruction. On a real HT machine this is fine because
	 * all other CPUs are busy with "useless" delay loops and don't
	 * care if they get somewhat less cycles.
	 */
	while (endflag == 0)
		mb();
}
#endif

static void report_broken_nmi(int cpu, unsigned int *prev_nmi_count)
{
	printk(KERN_CONT "\n");

	printk(KERN_WARNING
		"WARNING: CPU#%d: NMI appears to be stuck (%d->%d)!\n",
			cpu, prev_nmi_count[cpu], get_nmi_count(cpu));

	printk(KERN_WARNING
		"Please report this to bugzilla.kernel.org,\n");
	printk(KERN_WARNING
		"and attach the output of the 'dmesg' command.\n");

	per_cpu(wd_enabled, cpu) = 0;
	atomic_dec(&nmi_active);
}

static void __acpi_nmi_disable(void *__unused)
{
	apic_write(APIC_LVT0, APIC_DM_NMI | APIC_LVT_MASKED);
}

int __init check_nmi_watchdog(void)
{
	unsigned int *prev_nmi_count;
	int cpu;

	if (!nmi_watchdog_active() || !atomic_read(&nmi_active))
		return 0;

	prev_nmi_count = kmalloc(nr_cpu_ids * sizeof(int), GFP_KERNEL);
	if (!prev_nmi_count)
		goto error;

	alloc_cpumask_var(&backtrace_mask, GFP_KERNEL|__GFP_ZERO);
	printk(KERN_INFO "Testing NMI watchdog ... ");

#ifdef CONFIG_SMP
	if (nmi_watchdog == NMI_LOCAL_APIC)
		smp_call_function(nmi_cpu_busy, (void *)&endflag, 0);
#endif

	for_each_possible_cpu(cpu)
		prev_nmi_count[cpu] = get_nmi_count(cpu);
	local_irq_enable();
	mdelay((20 * 1000) / nmi_hz); /* wait 20 ticks */

	for_each_online_cpu(cpu) {
		if (!per_cpu(wd_enabled, cpu))
			continue;
		if (get_nmi_count(cpu) - prev_nmi_count[cpu] <= 5)
			report_broken_nmi(cpu, prev_nmi_count);
	}
	endflag = 1;
	if (!atomic_read(&nmi_active)) {
		kfree(prev_nmi_count);
		atomic_set(&nmi_active, -1);
		goto error;
	}
	printk("OK.\n");

	/*
	 * now that we know it works we can reduce NMI frequency to
	 * something more reasonable; makes a difference in some configs
	 */
	if (nmi_watchdog == NMI_LOCAL_APIC)
		nmi_hz = lapic_adjust_nmi_hz(1);

	kfree(prev_nmi_count);
	return 0;
error:
	if (nmi_watchdog == NMI_IO_APIC) {
		if (!timer_through_8259)
			disable_8259A_irq(0);
		on_each_cpu(__acpi_nmi_disable, NULL, 1);
	}

#ifdef CONFIG_X86_32
	timer_ack = 0;
#endif
	return -1;
}

static int __init setup_nmi_watchdog(char *str)
{
	unsigned int nmi;

	if (!strncmp(str, "panic", 5)) {
		panic_on_timeout = 1;
		str = strchr(str, ',');
		if (!str)
			return 1;
		++str;
	}

	if (!strncmp(str, "lapic", 5))
		nmi_watchdog = NMI_LOCAL_APIC;
	else if (!strncmp(str, "ioapic", 6))
		nmi_watchdog = NMI_IO_APIC;
	else {
		get_option(&str, &nmi);
		if (nmi >= NMI_INVALID)
			return 0;
		nmi_watchdog = nmi;
	}

	return 1;
}
__setup("nmi_watchdog=", setup_nmi_watchdog);

/*
 * Suspend/resume support
 */
#ifdef CONFIG_PM

static int nmi_pm_active; /* nmi_active before suspend */

static int lapic_nmi_suspend(struct sys_device *dev, pm_message_t state)
{
	/* only CPU0 goes here, other CPUs should be offline */
	nmi_pm_active = atomic_read(&nmi_active);
	stop_apic_nmi_watchdog(NULL);
	BUG_ON(atomic_read(&nmi_active) != 0);
	return 0;
}

static int lapic_nmi_resume(struct sys_device *dev)
{
	/* only CPU0 goes here, other CPUs should be offline */
	if (nmi_pm_active > 0) {
		setup_apic_nmi_watchdog(NULL);
		touch_nmi_watchdog();
	}
	return 0;
}

static struct sysdev_class nmi_sysclass = {
	.name		= "lapic_nmi",
	.resume		= lapic_nmi_resume,
	.suspend	= lapic_nmi_suspend,
};

static struct sys_device device_lapic_nmi = {
	.id	= 0,
	.cls	= &nmi_sysclass,
};

static int __init init_lapic_nmi_sysfs(void)
{
	int error;

	/*
	 * should really be a BUG_ON but b/c this is an
	 * init call, it just doesn't work.  -dcz
	 */
	if (nmi_watchdog != NMI_LOCAL_APIC)
		return 0;

	if (atomic_read(&nmi_active) < 0)
		return 0;

	error = sysdev_class_register(&nmi_sysclass);
	if (!error)
		error = sysdev_register(&device_lapic_nmi);
	return error;
}

/* must come after the local APIC's device_initcall() */
late_initcall(init_lapic_nmi_sysfs);

#endif	/* CONFIG_PM */

static void __acpi_nmi_enable(void *__unused)
{
	apic_write(APIC_LVT0, APIC_DM_NMI);
}

/*
 * Enable timer based NMIs on all CPUs:
 */
void acpi_nmi_enable(void)
{
	if (atomic_read(&nmi_active) && nmi_watchdog == NMI_IO_APIC)
		on_each_cpu(__acpi_nmi_enable, NULL, 1);
}

/*
 * Disable timer based NMIs on all CPUs:
 */
void acpi_nmi_disable(void)
{
	if (atomic_read(&nmi_active) && nmi_watchdog == NMI_IO_APIC)
		on_each_cpu(__acpi_nmi_disable, NULL, 1);
}

/*
 * This function is called as soon the LAPIC NMI watchdog driver has everything
 * in place and it's ready to check if the NMIs belong to the NMI watchdog
 */
void cpu_nmi_set_wd_enabled(void)
{
	__get_cpu_var(wd_enabled) = 1;
}

void setup_apic_nmi_watchdog(void *unused)
{
	if (__get_cpu_var(wd_enabled))
		return;

	/* cheap hack to support suspend/resume */
	/* if cpu0 is not active neither should the other cpus */
	if (smp_processor_id() != 0 && atomic_read(&nmi_active) <= 0)
		return;

	switch (nmi_watchdog) {
	case NMI_LOCAL_APIC:
		if (lapic_watchdog_init(nmi_hz) < 0) {
			__get_cpu_var(wd_enabled) = 0;
			return;
		}
		/* FALL THROUGH */
	case NMI_IO_APIC:
		__get_cpu_var(wd_enabled) = 1;
		atomic_inc(&nmi_active);
	}
}

void stop_apic_nmi_watchdog(void *unused)
{
	/* only support LOCAL and IO APICs for now */
	if (!nmi_watchdog_active())
		return;
	if (__get_cpu_var(wd_enabled) == 0)
		return;
	if (nmi_watchdog == NMI_LOCAL_APIC)
		lapic_watchdog_stop();
	else
		__acpi_nmi_disable(NULL);
	__get_cpu_var(wd_enabled) = 0;
	atomic_dec(&nmi_active);
}

/*
 * the best way to detect whether a CPU has a 'hard lockup' problem
 * is to check it's local APIC timer IRQ counts. If they are not
 * changing then that CPU has some problem.
 *
 * as these watchdog NMI IRQs are generated on every CPU, we only
 * have to check the current processor.
 *
 * since NMIs don't listen to _any_ locks, we have to be extremely
 * careful not to rely on unsafe variables. The printk might lock
 * up though, so we have to break up any console locks first ...
 * [when there will be more tty-related locks, break them up here too!]
 */

static DEFINE_PER_CPU(unsigned, last_irq_sum);
static DEFINE_PER_CPU(local_t, alert_counter);
static DEFINE_PER_CPU(int, nmi_touch);

void touch_nmi_watchdog(void)
{
	if (nmi_watchdog_active()) {
		unsigned cpu;

		/*
		 * Tell other CPUs to reset their alert counters. We cannot
		 * do it ourselves because the alert count increase is not
		 * atomic.
		 */
		for_each_present_cpu(cpu) {
			if (per_cpu(nmi_touch, cpu) != 1)
				per_cpu(nmi_touch, cpu) = 1;
		}
	}

	/*
	 * Tickle the softlockup detector too:
	 */
	touch_softlockup_watchdog();
}
EXPORT_SYMBOL(touch_nmi_watchdog);

notrace __kprobes int
nmi_watchdog_tick(struct pt_regs *regs, unsigned reason)
{
	/*
	 * Since current_thread_info()-> is always on the stack, and we
	 * always switch the stack NMI-atomically, it's safe to use
	 * smp_processor_id().
	 */
	unsigned int sum;
	int touched = 0;
	int cpu = smp_processor_id();
	int rc = 0;

	/* check for other users first */
	if (notify_die(DIE_NMI, "nmi", regs, reason, 2, SIGINT)
			== NOTIFY_STOP) {
		rc = 1;
		touched = 1;
	}

	sum = get_timer_irqs(cpu);

	if (__get_cpu_var(nmi_touch)) {
		__get_cpu_var(nmi_touch) = 0;
		touched = 1;
	}

	/* We can be called before check_nmi_watchdog, hence NULL check. */
	if (backtrace_mask != NULL && cpumask_test_cpu(cpu, backtrace_mask)) {
		static DEFINE_SPINLOCK(lock);	/* Serialise the printks */

		spin_lock(&lock);
		printk(KERN_WARNING "NMI backtrace for cpu %d\n", cpu);
		dump_stack();
		spin_unlock(&lock);
		cpumask_clear_cpu(cpu, backtrace_mask);
	}

	/* Could check oops_in_progress here too, but it's safer not to */
	if (mce_in_progress())
		touched = 1;

	/* if the none of the timers isn't firing, this cpu isn't doing much */
	if (!touched && __get_cpu_var(last_irq_sum) == sum) {
		/*
		 * Ayiee, looks like this CPU is stuck ...
		 * wait a few IRQs (5 seconds) before doing the oops ...
		 */
		local_inc(&__get_cpu_var(alert_counter));
		if (local_read(&__get_cpu_var(alert_counter)) == 5 * nmi_hz)
			/*
			 * die_nmi will return ONLY if NOTIFY_STOP happens..
			 */
			die_nmi("BUG: NMI Watchdog detected LOCKUP",
				regs, panic_on_timeout);
	} else {
		__get_cpu_var(last_irq_sum) = sum;
		local_set(&__get_cpu_var(alert_counter), 0);
	}

	/* see if the nmi watchdog went off */
	if (!__get_cpu_var(wd_enabled))
		return rc;
	switch (nmi_watchdog) {
	case NMI_LOCAL_APIC:
		rc |= lapic_wd_event(nmi_hz);
		break;
	case NMI_IO_APIC:
		/*
		 * don't know how to accurately check for this.
		 * just assume it was a watchdog timer interrupt
		 * This matches the old behaviour.
		 */
		rc = 1;
		break;
	}
	return rc;
}

#ifdef CONFIG_SYSCTL

static void enable_ioapic_nmi_watchdog_single(void *unused)
{
	__get_cpu_var(wd_enabled) = 1;
	atomic_inc(&nmi_active);
	__acpi_nmi_enable(NULL);
}

static void enable_ioapic_nmi_watchdog(void)
{
	on_each_cpu(enable_ioapic_nmi_watchdog_single, NULL, 1);
	touch_nmi_watchdog();
}

static void disable_ioapic_nmi_watchdog(void)
{
	on_each_cpu(stop_apic_nmi_watchdog, NULL, 1);
}

static int __init setup_unknown_nmi_panic(char *str)
{
	unknown_nmi_panic = 1;
	return 1;
}
__setup("unknown_nmi_panic", setup_unknown_nmi_panic);

static int unknown_nmi_panic_callback(struct pt_regs *regs, int cpu)
{
	unsigned char reason = get_nmi_reason();
	char buf[64];

	sprintf(buf, "NMI received for unknown reason %02x\n", reason);
	die_nmi(buf, regs, 1); /* Always panic here */
	return 0;
}

/*
 * proc handler for /proc/sys/kernel/nmi
 */
int proc_nmi_enabled(struct ctl_table *table, int write, struct file *file,
			void __user *buffer, size_t *length, loff_t *ppos)
{
	int old_state;

	nmi_watchdog_enabled = (atomic_read(&nmi_active) > 0) ? 1 : 0;
	old_state = nmi_watchdog_enabled;
	proc_dointvec(table, write, file, buffer, length, ppos);
	if (!!old_state == !!nmi_watchdog_enabled)
		return 0;

	if (atomic_read(&nmi_active) < 0 || !nmi_watchdog_active()) {
		printk(KERN_WARNING
			"NMI watchdog is permanently disabled\n");
		return -EIO;
	}

	if (nmi_watchdog == NMI_LOCAL_APIC) {
		if (nmi_watchdog_enabled)
			enable_lapic_nmi_watchdog();
		else
			disable_lapic_nmi_watchdog();
	} else if (nmi_watchdog == NMI_IO_APIC) {
		if (nmi_watchdog_enabled)
			enable_ioapic_nmi_watchdog();
		else
			disable_ioapic_nmi_watchdog();
	} else {
		printk(KERN_WARNING
			"NMI watchdog doesn't know what hardware to touch\n");
		return -EIO;
	}
	return 0;
}

#endif /* CONFIG_SYSCTL */

int do_nmi_callback(struct pt_regs *regs, int cpu)
{
#ifdef CONFIG_SYSCTL
	if (unknown_nmi_panic)
		return unknown_nmi_panic_callback(regs, cpu);
#endif
	return 0;
}

void __trigger_all_cpu_backtrace(void)
{
	int i;

	cpumask_copy(backtrace_mask, cpu_online_mask);
	/* Wait for up to 10 seconds for all CPUs to do the backtrace */
	for (i = 0; i < 10 * 1000; i++) {
		if (cpumask_empty(backtrace_mask))
			break;
		mdelay(1);
	}
}
