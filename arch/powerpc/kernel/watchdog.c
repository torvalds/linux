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
 * The SMP checker can detect lockups on other CPUs. A global "pending"
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
static unsigned long __wd_reporting;
static unsigned long __wd_nmi_output;
static cpumask_t wd_smp_cpus_pending;
static cpumask_t wd_smp_cpus_stuck;
static u64 wd_smp_last_reset_tb;

#ifdef CONFIG_PPC_PSERIES
static u64 wd_timeout_pct;
#endif

/*
 * Try to take the exclusive watchdog action / NMI IPI / printing lock.
 * wd_smp_lock must be held. If this fails, we should return and wait
 * for the watchdog to kick in again (or another CPU to trigger it).
 *
 * Importantly, if hardlockup_panic is set, wd_try_report failure should
 * not delay the panic, because whichever other CPU is reporting will
 * call panic.
 */
static bool wd_try_report(void)
{
	if (__wd_reporting)
		return false;
	__wd_reporting = 1;
	return true;
}

/* End printing after successful wd_try_report. wd_smp_lock not required. */
static void wd_end_reporting(void)
{
	smp_mb(); /* End printing "critical section" */
	WARN_ON_ONCE(__wd_reporting == 0);
	WRITE_ONCE(__wd_reporting, 0);
}

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

	/*
	 * __wd_nmi_output must be set after we printk from NMI context.
	 *
	 * printk from NMI context defers printing to the console to irq_work.
	 * If that NMI was taken in some code that is hard-locked, then irqs
	 * are disabled so irq_work will never fire. That can result in the
	 * hard lockup messages being delayed (indefinitely, until something
	 * else kicks the console drivers).
	 *
	 * Setting __wd_nmi_output will cause another CPU to notice and kick
	 * the console drivers for us.
	 *
	 * xchg is not needed here (it could be a smp_mb and store), but xchg
	 * gives the memory ordering and atomicity required.
	 */
	xchg(&__wd_nmi_output, 1);

	/* Do not panic from here because that can recurse into NMI IPI layer */
}

static bool set_cpu_stuck(int cpu)
{
	cpumask_set_cpu(cpu, &wd_smp_cpus_stuck);
	cpumask_clear_cpu(cpu, &wd_smp_cpus_pending);
	/*
	 * See wd_smp_clear_cpu_pending()
	 */
	smp_mb();
	if (cpumask_empty(&wd_smp_cpus_pending)) {
		wd_smp_last_reset_tb = get_tb();
		cpumask_andnot(&wd_smp_cpus_pending,
				&wd_cpus_enabled,
				&wd_smp_cpus_stuck);
		return true;
	}
	return false;
}

static void watchdog_smp_panic(int cpu)
{
	static cpumask_t wd_smp_cpus_ipi; // protected by reporting
	unsigned long flags;
	u64 tb, last_reset;
	int c;

	wd_smp_lock(&flags);
	/* Double check some things under lock */
	tb = get_tb();
	last_reset = wd_smp_last_reset_tb;
	if ((s64)(tb - last_reset) < (s64)wd_smp_panic_timeout_tb)
		goto out;
	if (cpumask_test_cpu(cpu, &wd_smp_cpus_pending))
		goto out;
	if (!wd_try_report())
		goto out;
	for_each_online_cpu(c) {
		if (!cpumask_test_cpu(c, &wd_smp_cpus_pending))
			continue;
		if (c == cpu)
			continue; // should not happen

		__cpumask_set_cpu(c, &wd_smp_cpus_ipi);
		if (set_cpu_stuck(c))
			break;
	}
	if (cpumask_empty(&wd_smp_cpus_ipi)) {
		wd_end_reporting();
		goto out;
	}
	wd_smp_unlock(&flags);

	pr_emerg("CPU %d detected hard LOCKUP on other CPUs %*pbl\n",
		 cpu, cpumask_pr_args(&wd_smp_cpus_ipi));
	pr_emerg("CPU %d TB:%lld, last SMP heartbeat TB:%lld (%lldms ago)\n",
		 cpu, tb, last_reset, tb_to_ns(tb - last_reset) / 1000000);

	if (!sysctl_hardlockup_all_cpu_backtrace) {
		/*
		 * Try to trigger the stuck CPUs, unless we are going to
		 * get a backtrace on all of them anyway.
		 */
		for_each_cpu(c, &wd_smp_cpus_ipi) {
			smp_send_nmi_ipi(c, wd_lockup_ipi, 1000000);
			__cpumask_clear_cpu(c, &wd_smp_cpus_ipi);
		}
	} else {
		trigger_allbutcpu_cpu_backtrace(cpu);
		cpumask_clear(&wd_smp_cpus_ipi);
	}

	if (hardlockup_panic)
		nmi_panic(NULL, "Hard LOCKUP");

	wd_end_reporting();

	return;

out:
	wd_smp_unlock(&flags);
}

static void wd_smp_clear_cpu_pending(int cpu)
{
	if (!cpumask_test_cpu(cpu, &wd_smp_cpus_pending)) {
		if (unlikely(cpumask_test_cpu(cpu, &wd_smp_cpus_stuck))) {
			struct pt_regs *regs = get_irq_regs();
			unsigned long flags;

			pr_emerg("CPU %d became unstuck TB:%lld\n",
				 cpu, get_tb());
			print_irqtrace_events(current);
			if (regs)
				show_regs(regs);
			else
				dump_stack();

			wd_smp_lock(&flags);
			cpumask_clear_cpu(cpu, &wd_smp_cpus_stuck);
			wd_smp_unlock(&flags);
		} else {
			/*
			 * The last CPU to clear pending should have reset the
			 * watchdog so we generally should not find it empty
			 * here if our CPU was clear. However it could happen
			 * due to a rare race with another CPU taking the
			 * last CPU out of the mask concurrently.
			 *
			 * We can't add a warning for it. But just in case
			 * there is a problem with the watchdog that is causing
			 * the mask to not be reset, try to kick it along here.
			 */
			if (unlikely(cpumask_empty(&wd_smp_cpus_pending)))
				goto none_pending;
		}
		return;
	}

	/*
	 * All other updates to wd_smp_cpus_pending are performed under
	 * wd_smp_lock. All of them are atomic except the case where the
	 * mask becomes empty and is reset. This will not happen here because
	 * cpu was tested to be in the bitmap (above), and a CPU only clears
	 * its own bit. _Except_ in the case where another CPU has detected a
	 * hard lockup on our CPU and takes us out of the pending mask. So in
	 * normal operation there will be no race here, no problem.
	 *
	 * In the lockup case, this atomic clear-bit vs a store that refills
	 * other bits in the accessed word wll not be a problem. The bit clear
	 * is atomic so it will not cause the store to get lost, and the store
	 * will never set this bit so it will not overwrite the bit clear. The
	 * only way for a stuck CPU to return to the pending bitmap is to
	 * become unstuck itself.
	 */
	cpumask_clear_cpu(cpu, &wd_smp_cpus_pending);

	/*
	 * Order the store to clear pending with the load(s) to check all
	 * words in the pending mask to check they are all empty. This orders
	 * with the same barrier on another CPU. This prevents two CPUs
	 * clearing the last 2 pending bits, but neither seeing the other's
	 * store when checking if the mask is empty, and missing an empty
	 * mask, which ends with a false positive.
	 */
	smp_mb();
	if (cpumask_empty(&wd_smp_cpus_pending)) {
		unsigned long flags;

none_pending:
		/*
		 * Double check under lock because more than one CPU could see
		 * a clear mask with the lockless check after clearing their
		 * pending bits.
		 */
		wd_smp_lock(&flags);
		if (cpumask_empty(&wd_smp_cpus_pending)) {
			wd_smp_last_reset_tb = get_tb();
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

	wd_smp_clear_cpu_pending(cpu);

	if ((s64)(tb - wd_smp_last_reset_tb) >= (s64)wd_smp_panic_timeout_tb)
		watchdog_smp_panic(cpu);

	if (__wd_nmi_output && xchg(&__wd_nmi_output, 0)) {
		/*
		 * Something has called printk from NMI context. It might be
		 * stuck, so this triggers a flush that will get that
		 * printk output to the console.
		 *
		 * See wd_lockup_ipi.
		 */
		printk_trigger_flush();
	}
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
		/*
		 * Taking wd_smp_lock here means it is a soft-NMI lock, which
		 * means we can't take any regular or irqsafe spin locks while
		 * holding this lock. This is why timers can't printk while
		 * holding the lock.
		 */
		wd_smp_lock(&flags);
		if (cpumask_test_cpu(cpu, &wd_smp_cpus_stuck)) {
			wd_smp_unlock(&flags);
			return 0;
		}
		if (!wd_try_report()) {
			wd_smp_unlock(&flags);
			/* Couldn't report, try again in 100ms */
			mtspr(SPRN_DEC, 100 * tb_ticks_per_usec * 1000);
			return 0;
		}

		set_cpu_stuck(cpu);

		wd_smp_unlock(&flags);

		pr_emerg("CPU %d self-detected hard LOCKUP @ %pS\n",
			 cpu, (void *)regs->nip);
		pr_emerg("CPU %d TB:%lld, last heartbeat TB:%lld (%lldms ago)\n",
			 cpu, tb, per_cpu(wd_timer_tb, cpu),
			 tb_to_ns(tb - per_cpu(wd_timer_tb, cpu)) / 1000000);
		print_modules();
		print_irqtrace_events(current);
		show_regs(regs);

		xchg(&__wd_nmi_output, 1); // see wd_lockup_ipi

		if (sysctl_hardlockup_all_cpu_backtrace)
			trigger_allbutcpu_cpu_backtrace(cpu);

		if (hardlockup_panic)
			nmi_panic(regs, "Hard LOCKUP");

		wd_end_reporting();
	}
	/*
	 * We are okay to change DEC in soft_nmi_interrupt because the masked
	 * handler has marked a DEC as pending, so the timer interrupt will be
	 * replayed as soon as local irqs are enabled again.
	 */
	if (wd_panic_timeout_tb < 0x7fffffff)
		mtspr(SPRN_DEC, wd_panic_timeout_tb);

	return 0;
}

static enum hrtimer_restart watchdog_timer_fn(struct hrtimer *hrtimer)
{
	int cpu = smp_processor_id();

	if (!(watchdog_enabled & WATCHDOG_HARDLOCKUP_ENABLED))
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
	u64 tb;

	if (!cpumask_test_cpu(cpu, &watchdog_cpumask))
		return;

	tb = get_tb();
	if (tb - per_cpu(wd_timer_tb, cpu) >= ticks) {
		per_cpu(wd_timer_tb, cpu) = tb;
		wd_smp_clear_cpu_pending(cpu);
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

	if (!(watchdog_enabled & WATCHDOG_HARDLOCKUP_ENABLED))
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

	hrtimer_setup(hrtimer, watchdog_timer_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
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

	wd_smp_clear_cpu_pending(cpu);
}

static int stop_watchdog_on_cpu(unsigned int cpu)
{
	return smp_call_function_single(cpu, stop_watchdog, NULL, true);
}

static void watchdog_calc_timeouts(void)
{
	u64 threshold = watchdog_thresh;

#ifdef CONFIG_PPC_PSERIES
	threshold += (READ_ONCE(wd_timeout_pct) * threshold) / 100;
#endif

	wd_panic_timeout_tb = threshold * ppc_tb_freq;

	/* Have the SMP detector trigger a bit later */
	wd_smp_panic_timeout_tb = wd_panic_timeout_tb * 3 / 2;

	/* 2/5 is the factor that the perf based detector uses */
	wd_timer_period_ms = watchdog_thresh * 1000 * 2 / 5;
}

void watchdog_hardlockup_stop(void)
{
	int cpu;

	for_each_cpu(cpu, &wd_cpus_enabled)
		stop_watchdog_on_cpu(cpu);
}

void watchdog_hardlockup_start(void)
{
	int cpu;

	watchdog_calc_timeouts();
	for_each_cpu_and(cpu, cpu_online_mask, &watchdog_cpumask)
		start_watchdog_on_cpu(cpu);
}

/*
 * Invoked from core watchdog init.
 */
int __init watchdog_hardlockup_probe(void)
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

#ifdef CONFIG_PPC_PSERIES
void watchdog_hardlockup_set_timeout_pct(u64 pct)
{
	pr_info("Set the NMI watchdog timeout factor to %llu%%\n", pct);
	WRITE_ONCE(wd_timeout_pct, pct);
	lockup_detector_reconfigure();
}
#endif
