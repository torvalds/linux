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
#include <linux/export.h>
#include <linux/kprobes.h>
#include <linux/kernel_stat.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/kdebug.h>
#include <linux/delay.h>
#include <linux/smp.h>

#include <asm/perf_event.h>
#include <asm/ptrace.h>
#include <asm/pcr.h>

#include "kstack.h"

/* We don't have a real NMI on sparc64, but we can fake one
 * up using profiling counter overflow interrupts and interrupt
 * levels.
 *
 * The profile overflow interrupts at level 15, so we use
 * level 14 as our IRQ off level.
 */

static int panic_on_timeout;

/* nmi_active:
 * >0: the NMI watchdog is active, but can be disabled
 * <0: the NMI watchdog has not been set up, and cannot be enabled
 *  0: the NMI watchdog is disabled, but can be enabled
 */
atomic_t nmi_active = ATOMIC_INIT(0);		/* oprofile uses this */
EXPORT_SYMBOL(nmi_active);

static unsigned int nmi_hz = HZ;
static DEFINE_PER_CPU(short, wd_enabled);
static int endflag __initdata;

static DEFINE_PER_CPU(unsigned int, last_irq_sum);
static DEFINE_PER_CPU(long, alert_counter);
static DEFINE_PER_CPU(int, nmi_touch);

void touch_nmi_watchdog(void)
{
	if (atomic_read(&nmi_active)) {
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
	int this_cpu = smp_processor_id();

	if (notify_die(DIE_NMIWATCHDOG, str, regs, 0,
		       pt_regs_trap_type(regs), SIGINT) == NOTIFY_STOP)
		return;

	if (do_panic || panic_on_oops)
		panic("Watchdog detected hard LOCKUP on cpu %d", this_cpu);
	else
		WARN(1, "Watchdog detected hard LOCKUP on cpu %d", this_cpu);
}

notrace __kprobes void perfctr_irq(int irq, struct pt_regs *regs)
{
	unsigned int sum, touched = 0;
	void *orig_sp;

	clear_softint(1 << irq);

	local_cpu_data().__nmi_count++;

	nmi_enter();

	orig_sp = set_hardirq_stack();

	if (notify_die(DIE_NMI, "nmi", regs, 0,
		       pt_regs_trap_type(regs), SIGINT) == NOTIFY_STOP)
		touched = 1;
	else
		pcr_ops->write_pcr(0, pcr_ops->pcr_nmi_disable);

	sum = local_cpu_data().irq0_irqs;
	if (__this_cpu_read(nmi_touch)) {
		__this_cpu_write(nmi_touch, 0);
		touched = 1;
	}
	if (!touched && __this_cpu_read(last_irq_sum) == sum) {
		__this_cpu_inc(alert_counter);
		if (__this_cpu_read(alert_counter) == 30 * nmi_hz)
			die_nmi("BUG: NMI Watchdog detected LOCKUP",
				regs, panic_on_timeout);
	} else {
		__this_cpu_write(last_irq_sum, sum);
		__this_cpu_write(alert_counter, 0);
	}
	if (__this_cpu_read(wd_enabled)) {
		pcr_ops->write_pic(0, pcr_ops->nmi_picl_value(nmi_hz));
		pcr_ops->write_pcr(0, pcr_ops->pcr_nmi_enable);
	}

	restore_hardirq_stack(orig_sp);

	nmi_exit();
}

static inline unsigned int get_nmi_count(int cpu)
{
	return cpu_data(cpu).__nmi_count;
}

static __init void nmi_cpu_busy(void *data)
{
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

	per_cpu(wd_enabled, cpu) = 0;
	atomic_dec(&nmi_active);
}

void stop_nmi_watchdog(void *unused)
{
	pcr_ops->write_pcr(0, pcr_ops->pcr_nmi_disable);
	__this_cpu_write(wd_enabled, 0);
	atomic_dec(&nmi_active);
}

static int __init check_nmi_watchdog(void)
{
	unsigned int *prev_nmi_count;
	int cpu, err;

	if (!atomic_read(&nmi_active))
		return 0;

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
		if (!per_cpu(wd_enabled, cpu))
			continue;
		if (get_nmi_count(cpu) - prev_nmi_count[cpu] <= 5)
			report_broken_nmi(cpu, prev_nmi_count);
	}
	endflag = 1;
	if (!atomic_read(&nmi_active)) {
		kfree(prev_nmi_count);
		atomic_set(&nmi_active, -1);
		err = -ENODEV;
		goto error;
	}
	printk("OK.\n");

	nmi_hz = 1;

	kfree(prev_nmi_count);
	return 0;
error:
	on_each_cpu(stop_nmi_watchdog, NULL, 1);
	return err;
}

void start_nmi_watchdog(void *unused)
{
	__this_cpu_write(wd_enabled, 1);
	atomic_inc(&nmi_active);

	pcr_ops->write_pcr(0, pcr_ops->pcr_nmi_disable);
	pcr_ops->write_pic(0, pcr_ops->nmi_picl_value(nmi_hz));

	pcr_ops->write_pcr(0, pcr_ops->pcr_nmi_enable);
}

static void nmi_adjust_hz_one(void *unused)
{
	if (!__this_cpu_read(wd_enabled))
		return;

	pcr_ops->write_pcr(0, pcr_ops->pcr_nmi_disable);
	pcr_ops->write_pic(0, pcr_ops->nmi_picl_value(nmi_hz));

	pcr_ops->write_pcr(0, pcr_ops->pcr_nmi_enable);
}

void nmi_adjust_hz(unsigned int new_hz)
{
	nmi_hz = new_hz;
	on_each_cpu(nmi_adjust_hz_one, NULL, 1);
}
EXPORT_SYMBOL_GPL(nmi_adjust_hz);

static int nmi_shutdown(struct notifier_block *nb, unsigned long cmd, void *p)
{
	on_each_cpu(stop_nmi_watchdog, NULL, 1);
	return 0;
}

static struct notifier_block nmi_reboot_notifier = {
	.notifier_call = nmi_shutdown,
};

int __init nmi_init(void)
{
	int err;

	on_each_cpu(start_nmi_watchdog, NULL, 1);

	err = check_nmi_watchdog();
	if (!err) {
		err = register_reboot_notifier(&nmi_reboot_notifier);
		if (err) {
			on_each_cpu(stop_nmi_watchdog, NULL, 1);
			atomic_set(&nmi_active, -1);
		}
	}

	return err;
}

static int __init setup_nmi_watchdog(char *str)
{
	if (!strncmp(str, "panic", 5))
		panic_on_timeout = 1;

	return 0;
}
__setup("nmi_watchdog=", setup_nmi_watchdog);
