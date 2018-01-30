// SPDX-License-Identifier: GPL-2.0
/*
 * Watchdog support on powerpc systems.
 *
 * Copyright 2017, IBM Corporation.
 *
 * This uses code from arch/sparc/kernel/nmi.c and kernel/watchdog.c
 */
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/nmi.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/kprobes.h>
#include <linux/hardirq.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/kdebug.h>
#include <linux/sched/debug.h>
#include <linux/delay.h>
#include <linux/smp.h>

#include <asm/paca.h>

/*
 * The watchdog has a simple timer that runs on each CPU, once per timer
 * period. This is the heartbeat.
 *
 * Then there are checks to see if the heartbeat has not triggered on a CPU
 * for the panic timeout period. Currently the watchdog only supports an
 * SMP check, so the heartbeat only turns on when we have 2 or more CPUs.
 *
 * This is not an NMI watchdog, but Linux uses that name for a generic
 * watchdog in some cases, so NMI gets used in some places.
 */

static cpumask_t wd_cpus_enabled __read_mostly;

static u64 wd_panic_timeout_tb __read_mostly; /* timebase ticks until panic */
static u64 wd_smp_panic_timeout_tb __read_mostly; /* panic other CPUs */

static u64 wd_timer_period_ms __read_mostly;  /* interval between heartbeat */

static DEFINE_PER_CPU(struct timer_list, wd_timer);
static DEFINE_PER_CPU(u64, wd_timer_tb);

/*
 * These are for the SMP checker. CPUs clear their pending bit in their
 * heartbeat. If the bitmask becomes empty, the time is noted and the
 * bitmask is refilled.
 *
 * All CPUs clear their bit in the pending mask every timer period.
 * Once all have cleared, the time is noted and the bits are reset.
 * If the time since all clear was greater than the panic timeout,
 * we can panic with the list of stuck CPUs.
 *
 * This will work best with NMI IPIs for crash code so the stuck CPUs
 * can be pulled out to get their backtraces.
 */
static unsigned long __wd_smp_lock;
static cpumask_t wd_smp_cpus_pending;
static cpumask_t wd_smp_cpus_stuck;
static u64 wd_smp_last_reset_tb;

static inline void wd_smp_lock(unsigned long *flags)
{
	/*
	 * Avoid locking layers if possible.
	 * This may be called from low level interrupt handlers at some
	 * point in future.
	 */
	raw_local_irq_save(*flags);
	hard_irq_disable(); /* Make it soft-NMI safe */
	while (unlikely(test_and_set_bit_lock(0, &__wd_smp_lock))) {
		raw_local_irq_restore(*flags);
		spin_until_cond(!test_bit(0, &__wd_smp_lock));
		raw_local_irq_save(*flags);
		hard_irq_disable();
	}
}

static inline void wd_smp_unlock(unsigned long *flags)
{
	clear_bit_unlock(0, &__wd_smp_lock);
	raw_local_irq_restore(*flags);
}

static void wd_lockup_ipi(struct pt_regs *regs)
{
	pr_emerg("Watchdog CPU:%d Hard LOCKUP\n", raw_smp_processor_id());
	print_modules();
	print_irqtrace_events(current);
	if (regs)
		show_regs(regs);
	else
		dump_stack();

	/* Do not panic from here because that can recurse into NMI IPI layer */
}

static void set_cpumask_stuck(const struct cpumask *cpumask, u64 tb)
{
	cpumask_or(&wd_smp_cpus_stuck, &wd_smp_cpus_stuck, cpumask);
	cpumask_andnot(&wd_smp_cpus_pending, &wd_smp_cpus_pending, cpumask);
	if (cpumask_empty(&wd_smp_cpus_pending)) {
		wd_smp_last_reset_tb = tb;
		cpumask_andnot(&wd_smp_cpus_pending,
				&wd_cpus_enabled,
				&wd_smp_cpus_stuck);
	}
}
static void set_cpu_stuck(int cpu, u64 tb)
{
	set_cpumask_stuck(cpumask_of(cpu), tb);
}

static void watchdog_smp_panic(int cpu, u64 tb)
{
	unsigned long flags;
	int c;

	wd_smp_lock(&flags);
	/* Double check some things under lock */
	if ((s64)(tb - wd_smp_last_reset_tb) < (s64)wd_smp_panic_timeout_tb)
		goto out;
	if (cpumask_test_cpu(cpu, &wd_smp_cpus_pending))
		goto out;
	if (cpumask_weight(&wd_smp_cpus_pending) == 0)
		goto out;

	pr_emerg("Watchdog CPU:%d detected Hard LOCKUP other CPUS:%*pbl\n",
			cpu, cpumask_pr_args(&wd_smp_cpus_pending));

	if (!sysctl_hardlockup_all_cpu_backtrace) {
		/*
		 * Try to trigger the stuck CPUs, unless we are going to
		 * get a backtrace on all of them anyway.
		 */
		for_each_cpu(c, &wd_smp_cpus_pending) {
			if (c == cpu)
				continue;
			smp_send_nmi_ipi(c, wd_lockup_ipi, 1000000);
		}
		smp_flush_nmi_ipi(1000000);
	}

	/* Take the stuck CPUs out of the watch group */
	set_cpumask_stuck(&wd_smp_cpus_pending, tb);

	wd_smp_unlock(&flags);

	printk_safe_flush();
	/*
	 * printk_safe_flush() seems to require another print
	 * before anything actually goes out to console.
	 */
	if (sysctl_hardlockup_all_cpu_backtrace)
		trigger_allbutself_cpu_backtrace();

	if (hardlockup_panic)
		nmi_panic(NULL, "Hard LOCKUP");

	return;

out:
	wd_smp_unlock(&flags);
}

static void wd_smp_clear_cpu_pending(int cpu, u64 tb)
{
	if (!cpumask_test_cpu(cpu, &wd_smp_cpus_pending)) {
		if (unlikely(cpumask_test_cpu(cpu, &wd_smp_cpus_stuck))) {
			unsigned long flags;

			pr_emerg("Watchdog CPU:%d became unstuck\n", cpu);
			wd_smp_lock(&flags);
			cpumask_clear_cpu(cpu, &wd_smp_cpus_stuck);
			wd_smp_unlock(&flags);
		}
		return;
	}
	cpumask_clear_cpu(cpu, &wd_smp_cpus_pending);
	if (cpumask_empty(&wd_smp_cpus_pending)) {
		unsigned long flags;

		wd_smp_lock(&flags);
		if (cpumask_empty(&wd_smp_cpus_pending)) {
			wd_smp_last_reset_tb = tb;
			cpumask_andnot(&wd_smp_cpus_pending,
					&wd_cpus_enabled,
					&wd_smp_cpus_stuck);
		}
		wd_smp_unlock(&flags);
	}
}

static void watchdog_timer_interrupt(int cpu)
{
	u64 tb = get_tb();

	per_cpu(wd_timer_tb, cpu) = tb;

	wd_smp_clear_cpu_pending(cpu, tb);

	if ((s64)(tb - wd_smp_last_reset_tb) >= (s64)wd_smp_panic_timeout_tb)
		watchdog_smp_panic(cpu, tb);
}

void soft_nmi_interrupt(struct pt_regs *regs)
{
	unsigned long flags;
	int cpu = raw_smp_processor_id();
	u64 tb;

	if (!cpumask_test_cpu(cpu, &wd_cpus_enabled))
		return;

	nmi_enter();

	__this_cpu_inc(irq_stat.soft_nmi_irqs);

	tb = get_tb();
	if (tb - per_cpu(wd_timer_tb, cpu) >= wd_panic_timeout_tb) {
		per_cpu(wd_timer_tb, cpu) = tb;

		wd_smp_lock(&flags);
		if (cpumask_test_cpu(cpu, &wd_smp_cpus_stuck)) {
			wd_smp_unlock(&flags);
			goto out;
		}
		set_cpu_stuck(cpu, tb);

		pr_emerg("Watchdog CPU:%d Hard LOCKUP\n", cpu);
		print_modules();
		print_irqtrace_events(current);
		if (regs)
			show_regs(regs);
		else
			dump_stack();

		wd_smp_unlock(&flags);

		if (sysctl_hardlockup_all_cpu_backtrace)
			trigger_allbutself_cpu_backtrace();

		if (hardlockup_panic)
			nmi_panic(regs, "Hard LOCKUP");
	}
	if (wd_panic_timeout_tb < 0x7fffffff)
		mtspr(SPRN_DEC, wd_panic_timeout_tb);

out:
	nmi_exit();
}

static void wd_timer_reset(unsigned int cpu, struct timer_list *t)
{
	t->expires = jiffies + msecs_to_jiffies(wd_timer_period_ms);
	if (wd_timer_period_ms > 1000)
		t->expires = __round_jiffies_up(t->expires, cpu);
	add_timer_on(t, cpu);
}

static void wd_timer_fn(struct timer_list *t)
{
	int cpu = smp_processor_id();

	watchdog_timer_interrupt(cpu);

	wd_timer_reset(cpu, t);
}

void arch_touch_nmi_watchdog(void)
{
	unsigned long ticks = tb_ticks_per_usec * wd_timer_period_ms * 1000;
	int cpu = smp_processor_id();
	u64 tb = get_tb();

	if (tb - per_cpu(wd_timer_tb, cpu) >= ticks) {
		per_cpu(wd_timer_tb, cpu) = tb;
		wd_smp_clear_cpu_pending(cpu, tb);
	}
}
EXPORT_SYMBOL(arch_touch_nmi_watchdog);

static void start_watchdog_timer_on(unsigned int cpu)
{
	struct timer_list *t = per_cpu_ptr(&wd_timer, cpu);

	per_cpu(wd_timer_tb, cpu) = get_tb();

	timer_setup(t, wd_timer_fn, TIMER_PINNED);
	wd_timer_reset(cpu, t);
}

static void stop_watchdog_timer_on(unsigned int cpu)
{
	struct timer_list *t = per_cpu_ptr(&wd_timer, cpu);

	del_timer_sync(t);
}

static int start_wd_on_cpu(unsigned int cpu)
{
	unsigned long flags;

	if (cpumask_test_cpu(cpu, &wd_cpus_enabled)) {
		WARN_ON(1);
		return 0;
	}

	if (!(watchdog_enabled & NMI_WATCHDOG_ENABLED))
		return 0;

	if (!cpumask_test_cpu(cpu, &watchdog_cpumask))
		return 0;

	wd_smp_lock(&flags);
	cpumask_set_cpu(cpu, &wd_cpus_enabled);
	if (cpumask_weight(&wd_cpus_enabled) == 1) {
		cpumask_set_cpu(cpu, &wd_smp_cpus_pending);
		wd_smp_last_reset_tb = get_tb();
	}
	wd_smp_unlock(&flags);

	start_watchdog_timer_on(cpu);

	return 0;
}

static int stop_wd_on_cpu(unsigned int cpu)
{
	unsigned long flags;

	if (!cpumask_test_cpu(cpu, &wd_cpus_enabled))
		return 0; /* Can happen in CPU unplug case */

	stop_watchdog_timer_on(cpu);

	wd_smp_lock(&flags);
	cpumask_clear_cpu(cpu, &wd_cpus_enabled);
	wd_smp_unlock(&flags);

	wd_smp_clear_cpu_pending(cpu, get_tb());

	return 0;
}

static void watchdog_calc_timeouts(void)
{
	wd_panic_timeout_tb = watchdog_thresh * ppc_tb_freq;

	/* Have the SMP detector trigger a bit later */
	wd_smp_panic_timeout_tb = wd_panic_timeout_tb * 3 / 2;

	/* 2/5 is the factor that the perf based detector uses */
	wd_timer_period_ms = watchdog_thresh * 1000 * 2 / 5;
}

void watchdog_nmi_stop(void)
{
	int cpu;

	for_each_cpu(cpu, &wd_cpus_enabled)
		stop_wd_on_cpu(cpu);
}

void watchdog_nmi_start(void)
{
	int cpu;

	watchdog_calc_timeouts();
	for_each_cpu_and(cpu, cpu_online_mask, &watchdog_cpumask)
		start_wd_on_cpu(cpu);
}

/*
 * Invoked from core watchdog init.
 */
int __init watchdog_nmi_probe(void)
{
	int err;

	err = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"powerpc/watchdog:online",
					start_wd_on_cpu, stop_wd_on_cpu);
	if (err < 0) {
		pr_warn("Watchdog could not be initialized");
		return err;
	}
	return 0;
}

static void handle_backtrace_ipi(struct pt_regs *regs)
{
	nmi_cpu_backtrace(regs);
}

static void raise_backtrace_ipi(cpumask_t *mask)
{
	unsigned int cpu;

	for_each_cpu(cpu, mask) {
		if (cpu == smp_processor_id())
			handle_backtrace_ipi(NULL);
		else
			smp_send_nmi_ipi(cpu, handle_backtrace_ipi, 1000000);
	}
}

void arch_trigger_cpumask_backtrace(const cpumask_t *mask, bool exclude_self)
{
	nmi_trigger_cpumask_backtrace(mask, exclude_self, raise_backtrace_ipi);
}
