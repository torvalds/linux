/* Pseudo NMI support on sparc64 systems.
 *
 * Copyright (C) 2009 David S. Miller <davem@davemloft.net>
 *
 * The NMI watchdog support and infrastructure is based almost
 * entirely upon the x86 NMI support code.
 */
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/nmi.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/kernel_stat.h>
#include <linux/slab.h>
#include <linux/kdebug.h>
#include <linux/delay.h>
#include <linux/smp.h>

#include <asm/ptrace.h>
#include <asm/local.h>
#include <asm/pcr.h>

/* We don't have a real NMI on sparc64, but we can fake one
 * up using profiling counter overflow interrupts and interrupt
 * levels.
 *
 * The profile overflow interrupts at level 15, so we use
 * level 14 as our IRQ off level.
 */

static int nmi_watchdog_active;
static int panic_on_timeout;

int nmi_usable;
EXPORT_SYMBOL_GPL(nmi_usable);

static unsigned int nmi_hz = HZ;

static DEFINE_PER_CPU(unsigned int, last_irq_sum);
static DEFINE_PER_CPU(local_t, alert_counter);
static DEFINE_PER_CPU(int, nmi_touch);

void touch_nmi_watchdog(void)
{
	if (nmi_watchdog_active) {
		int cpu;

		for_each_present_cpu(cpu) {
			if (per_cpu(nmi_touch, cpu) != 1)
				per_cpu(nmi_touch, cpu) = 1;
		}
	}

	touch_softlockup_watchdog();
}
EXPORT_SYMBOL(touch_nmi_watchdog);

static void die_nmi(const char *str, struct pt_regs *regs, int do_panic)
{
	if (notify_die(DIE_NMIWATCHDOG, str, regs, 0,
		       pt_regs_trap_type(regs), SIGINT) == NOTIFY_STOP)
		return;

	console_verbose();
	bust_spinlocks(1);

	printk(KERN_EMERG "%s", str);
	printk(" on CPU%d, ip %08lx, registers:\n",
	       smp_processor_id(), regs->tpc);
	show_regs(regs);
	dump_stack();

	bust_spinlocks(0);

	if (do_panic || panic_on_oops)
		panic("Non maskable interrupt");

	local_irq_enable();
	do_exit(SIGBUS);
}

notrace __kprobes void perfctr_irq(int irq, struct pt_regs *regs)
{
	unsigned int sum, touched = 0;
	int cpu = smp_processor_id();

	clear_softint(1 << irq);
	pcr_ops->write(PCR_PIC_PRIV);

	local_cpu_data().__nmi_count++;

	if (notify_die(DIE_NMI, "nmi", regs, 0,
		       pt_regs_trap_type(regs), SIGINT) == NOTIFY_STOP)
		touched = 1;

	sum = kstat_irqs_cpu(0, cpu);
	if (__get_cpu_var(nmi_touch)) {
		__get_cpu_var(nmi_touch) = 0;
		touched = 1;
	}
	if (!touched && __get_cpu_var(last_irq_sum) == sum) {
		local_inc(&__get_cpu_var(alert_counter));
		if (local_read(&__get_cpu_var(alert_counter)) == 5 * nmi_hz)
			die_nmi("BUG: NMI Watchdog detected LOCKUP",
				regs, panic_on_timeout);
	} else {
		__get_cpu_var(last_irq_sum) = sum;
		local_set(&__get_cpu_var(alert_counter), 0);
	}
	if (nmi_usable) {
		write_pic(picl_value(nmi_hz));
		pcr_ops->write(pcr_enable);
	}
}

static inline unsigned int get_nmi_count(int cpu)
{
	return cpu_data(cpu).__nmi_count;
}

static int endflag __initdata;

static __init void nmi_cpu_busy(void *data)
{
	local_irq_enable_in_hardirq();
	while (endflag == 0)
		mb();
}

static void report_broken_nmi(int cpu, int *prev_nmi_count)
{
	printk(KERN_CONT "\n");

	printk(KERN_WARNING
		"WARNING: CPU#%d: NMI appears to be stuck (%d->%d)!\n",
			cpu, prev_nmi_count[cpu], get_nmi_count(cpu));

	printk(KERN_WARNING
		"Please report this to bugzilla.kernel.org,\n");
	printk(KERN_WARNING
		"and attach the output of the 'dmesg' command.\n");

	nmi_usable = 0;
}

static void stop_watchdog(void *unused)
{
	pcr_ops->write(PCR_PIC_PRIV);
}

static int __init check_nmi_watchdog(void)
{
	unsigned int *prev_nmi_count;
	int cpu, err;

	prev_nmi_count = kmalloc(nr_cpu_ids * sizeof(unsigned int), GFP_KERNEL);
	if (!prev_nmi_count) {
		err = -ENOMEM;
		goto error;
	}

	printk(KERN_INFO "Testing NMI watchdog ... ");

	smp_call_function(nmi_cpu_busy, (void *)&endflag, 0);

	for_each_possible_cpu(cpu)
		prev_nmi_count[cpu] = get_nmi_count(cpu);
	local_irq_enable();
	mdelay((20 * 1000) / nmi_hz); /* wait 20 ticks */

	for_each_online_cpu(cpu) {
		if (get_nmi_count(cpu) - prev_nmi_count[cpu] <= 5)
			report_broken_nmi(cpu, prev_nmi_count);
	}
	endflag = 1;
	if (!nmi_usable) {
		kfree(prev_nmi_count);
		err = -ENODEV;
		goto error;
	}
	printk("OK.\n");

	nmi_hz = 1;

	kfree(prev_nmi_count);
	return 0;
error:
	on_each_cpu(stop_watchdog, NULL, 1);
	return err;
}

static void start_watchdog(void *unused)
{
	pcr_ops->write(PCR_PIC_PRIV);
	write_pic(picl_value(nmi_hz));

	pcr_ops->write(pcr_enable);
}

void nmi_adjust_hz(unsigned int new_hz)
{
	nmi_hz = new_hz;
	on_each_cpu(start_watchdog, NULL, 1);
}
EXPORT_SYMBOL_GPL(nmi_adjust_hz);

int __init nmi_init(void)
{
	nmi_usable = 1;

	on_each_cpu(start_watchdog, NULL, 1);

	return check_nmi_watchdog();
}

static int __init setup_nmi_watchdog(char *str)
{
	if (!strncmp(str, "panic", 5))
		panic_on_timeout = 1;

	return 0;
}
__setup("nmi_watchdog=", setup_nmi_watchdog);
