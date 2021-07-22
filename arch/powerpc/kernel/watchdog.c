// SPDX-License-Identifier: GPL-2.0
/*
 * Watchdog support on powerpc systems.
 *
 * Copyright 2017, IBM Corporation.
 *
 * This uses code from arch/sparc/kernel/nmi.c and kernel/watchdog.c
 */

#define pr_fmt(fmt) "watchdog: " fmt

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
#include <linux/processor.h>
#include <linux/smp.h>

#include <asm/interrupt.h>
#include <asm/paca.h>
#include <asm/nmi.h>

/*
 * The powerpc watchdog ensures that each CPU is able to service timers.
 * The watchdog sets up a simple timer on each CPU to run once per timer
 * period, and updates a per-cpu timestamp and a "pending" cpumask. This is
 * the heartbeat.
 *
 * Then there are two systems to check that the heartbeat is still running.
 * The local soft-NMI, and the SMP checker.
 *
 * The soft-NMI checker can detect lockups on the local CPU. When interrupts
 * are disabled with local_irq_disable(), platforms that use soft-masking
 * can leave hardware interrupts enabled and handle them with a masked
 * interrupt handler. The masked handler can send the timer interrupt to the
 * watchdog's soft_nmi_interrupt(), which appears to Linux as an NMI
 * interrupt, and can be used to detect CPUs stuck with IRQs disabled.
 *
 * The soft-NMI checker will compare the heartbeat timestamp for this CPU
 * with the current time, and take action if the difference exceeds the
 * watchdog threshold.
 *
 * The limitation of the soft-NMI watchdog is that it does not work when
 * interrupts are hard disabled or otherwise not being serviced. This is
 * solved by also having a SMP watchdog where all CPUs check all other
 * CPUs heartbeat.
 *
 * The SMP checker can detect lockups on other CPUs. A gobal "pending"
 * cpumask is kept, containing all CPUs which enable the watchdog. Each
 * CPU clears their pending bit in their heartbeat timer. When the bitmask
 * becomes empty, the last CPU to clear its pending bit updates a global
 * timestamp and refills the pending bitmask.
 *
 * In the heartbeat timer, if any CPU notices that the global timestamp has
 * not been updated for a period exceeding the watchdog threshold, then it
 * means the CPU(s) with their bit still set in the pending mask have had
 * their heartbeat stop, and action is taken.
 *
 * Some platforms implement true NMI IPIs, which can be used by the SMP
 * watchdog to detect an unresponsive CPU and pull it out of its stuck
 * state with the NMI IPI, to get crash/debug data from it. This way the
 * SMP watchdog can detect hardware interrupts off lockups.
 */

static cpumask_t wd_cpus_enabled __read_mostly;

static u64 wd_panic_timeout_tb __read_mostly; /* timebase ticks until panic */
static u64 wd_smp_panic_timeout_tb __read_mostly; /* panic other CPUs */

static u64 wd_timer_period_ms __read_mostly;  /* interval between heartbeat */

static DEFINE_PER_CPU(struct hrtimer, wd_hrtimer);
static DEFINE_PER_CPU(u64, wd_timer_tb);

/* SMP checker bits */
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
	int cpu = raw_smp_processor_id();
	u64 tb = get_tb();

	pr_emerg("CPU %d Hard LOCKUP\n", cpu);
	pr_emerg("CPU %d TB:%lld, last heartbeat TB:%lld (%lldms ago)\n",
		 cpu, tb, per_cpu(wd_timer_tb, cpu),
		 tb_to_ns(tb - per_cpu(wd_timer_tb, cpu)) / 1000000);
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

	pr_emerg("CPU %d detected hard LOCKUP on other CPUs %*pbl\n",
		 cpu, cpumask_pr_args(&wd_smp_cpus_pending));
	pr_emerg("CPU %d TB:%lld, last SMP heartbeat TB:%lld (%lldms ago)\n",
		 cpu, tb, wd_smp_last_reset_tb,
		 tb_to_ns(tb - wd_smp_last_reset_tb) / 1000000);

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
			struct pt_regs *regs = get_irq_regs();
			unsigned long flags;

			wd_smp_lock(&flags);

			pr_emerg("CPU %d became unstuck TB:%lld\n",
				 cpu, tb);
			print_irqtrace_events(current);
			if (regs)
				show_regs(regs);
			else
				dump_stack();

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

DEFINE_INTERRUPT_HANDLER_NMI(soft_nmi_interrupt)
{
	unsigned long flags;
	int cpu = raw_smp_processor_id();
	u64 tb;

	/* should only arrive from kernel, with irqs disabled */
	WARN_ON_ONCE(!arch_irq_disabled_regs(regs));

	if (!cpumask_test_cpu(cpu, &wd_cpus_enabled))
		return 0;

	__this_cpu_inc(irq_stat.soft_nmi_irqs);

	tb = get_tb();
	if (tb - per_cpu(wd_timer_tb, cpu) >= wd_panic_timeout_tb) {
		wd_smp_lock(&flags);
		if (cpumask_test_cpu(cpu, &wd_smp_cpus_stuck)) {
			wd_smp_unlock(&flags);
			return 0;
		}
		set_cpu_stuck(cpu, tb);

		pr_emerg("CPU %d self-detected hard LOCKUP @ %pS\n",
			 cpu, (void *)regs->nip);
		pr_emerg("CPU %d TB:%lld, last heartbeat TB:%lld (%lldms ago)\n",
			 cpu, tb, per_cpu(wd_timer_tb, cpu),
			 tb_to_ns(tb - per_cpu(wd_timer_tb, cpu)) / 1000000);
		print_modules();
		print_irqtrace_events(current);
		show_regs(regs);

		wd_smp_unlock(&flags);

		if (sysctl_hardlockup_all_cpu_backtrace)
			trigger_allbutself_cpu_backtrace();

		if (hardlockup_panic)
			nmi_panic(regs, "Hard LOCKUP");
	}
	if (wd_panic_timeout_tb < 0x7fffffff)
		mtspr(SPRN_DEC, wd_panic_timeout_tb);

	return 0;
}

static enum hrtimer_restart watchdog_timer_fn(struct hrtimer *hrtimer)
{
	int cpu = smp_processor_id();

	if (!(watchdog_enabled & NMI_WATCHDOG_ENABLED))
		return HRTIMER_NORESTART;

	if (!cpumask_test_cpu(cpu, &watchdog_cpumask))
		return HRTIMER_NORESTART;

	watchdog_timer_interrupt(cpu);

	hrtimer_forward_now(hrtimer, ms_to_ktime(wd_timer_period_ms));

	return HRTIMER_RESTART;
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

static void start_watchdog(void *arg)
{
	struct hrtimer *hrtimer = this_cpu_ptr(&wd_hrtimer);
	int cpu = smp_processor_id();
	unsigned long flags;

	if (cpumask_test_cpu(cpu, &wd_cpus_enabled)) {
		WARN_ON(1);
		return;
	}

	if (!(watchdog_enabled & NMI_WATCHDOG_ENABLED))
		return;

	if (!cpumask_test_cpu(cpu, &watchdog_cpumask))
		return;

	wd_smp_lock(&flags);
	cpumask_set_cpu(cpu, &wd_cpus_enabled);
	if (cpumask_weight(&wd_cpus_enabled) == 1) {
		cpumask_set_cpu(cpu, &wd_smp_cpus_pending);
		wd_smp_last_reset_tb = get_tb();
	}
	wd_smp_unlock(&flags);

	*this_cpu_ptr(&wd_timer_tb) = get_tb();

	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = watchdog_timer_fn;
	hrtimer_start(hrtimer, ms_to_ktime(wd_timer_period_ms),
		      HRTIMER_MODE_REL_PINNED);
}

static int start_watchdog_on_cpu(unsigned int cpu)
{
	return smp_call_function_single(cpu, start_watchdog, NULL, true);
}

static void stop_watchdog(void *arg)
{
	struct hrtimer *hrtimer = this_cpu_ptr(&wd_hrtimer);
	int cpu = smp_processor_id();
	unsigned long flags;

	if (!cpumask_test_cpu(cpu, &wd_cpus_enabled))
		return; /* Can happen in CPU unplug case */

	hrtimer_cancel(hrtimer);

	wd_smp_lock(&flags);
	cpumask_clear_cpu(cpu, &wd_cpus_enabled);
	wd_smp_unlock(&flags);

	wd_smp_clear_cpu_pending(cpu, get_tb());
}

static int stop_watchdog_on_cpu(unsigned int cpu)
{
	return smp_call_function_single(cpu, stop_watchdog, NULL, true);
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
		stop_watchdog_on_cpu(cpu);
}

void watchdog_nmi_start(void)
{
	int cpu;

	watchdog_calc_timeouts();
	for_each_cpu_and(cpu, cpu_online_mask, &watchdog_cpumask)
		start_watchdog_on_cpu(cpu);
}

/*
 * Invoked from core watchdog init.
 */
int __init watchdog_nmi_probe(void)
{
	int err;

	err = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"powerpc/watchdog:online",
					start_watchdog_on_cpu,
					stop_watchdog_on_cpu);
	if (err < 0) {
		pr_warn("could not be initialized");
		return err;
	}
	return 0;
}
