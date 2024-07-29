// SPDX-License-Identifier: GPL-2.0
/*
 * Count register synchronisation.
 *
 * Derived from arch/x86/kernel/tsc_sync.c
 * Copyright (C) 2006, Red Hat, Inc., Ingo Molnar
 */

#include <linux/kernel.h>
#include <linux/irqflags.h>
#include <linux/cpumask.h>
#include <linux/atomic.h>
#include <linux/nmi.h>
#include <linux/smp.h>
#include <linux/spinlock.h>

#include <asm/r4k-timer.h>
#include <asm/mipsregs.h>
#include <asm/time.h>

#define COUNTON		100
#define NR_LOOPS	3
#define LOOP_TIMEOUT	20

/*
 * Entry/exit counters that make sure that both CPUs
 * run the measurement code at once:
 */
static atomic_t start_count;
static atomic_t stop_count;
static atomic_t test_runs;

/*
 * We use a raw spinlock in this exceptional case, because
 * we want to have the fastest, inlined, non-debug version
 * of a critical section, to be able to prove counter time-warps:
 */
static arch_spinlock_t sync_lock = __ARCH_SPIN_LOCK_UNLOCKED;

static uint32_t last_counter;
static uint32_t max_warp;
static int nr_warps;
static int random_warps;

/*
 * Counter warp measurement loop running on both CPUs.
 */
static uint32_t check_counter_warp(void)
{
	uint32_t start, now, prev, end, cur_max_warp = 0;
	int i, cur_warps = 0;

	start = read_c0_count();
	end = start + (uint32_t) mips_hpt_frequency / 1000 * LOOP_TIMEOUT;

	for (i = 0; ; i++) {
		/*
		 * We take the global lock, measure counter, save the
		 * previous counter that was measured (possibly on
		 * another CPU) and update the previous counter timestamp.
		 */
		arch_spin_lock(&sync_lock);
		prev = last_counter;
		now = read_c0_count();
		last_counter = now;
		arch_spin_unlock(&sync_lock);

		/*
		 * Be nice every now and then (and also check whether
		 * measurement is done [we also insert a 10 million
		 * loops safety exit, so we dont lock up in case the
		 * counter is totally broken]):
		 */
		if (unlikely(!(i & 7))) {
			if (now > end || i > 10000000)
				break;
			cpu_relax();
			touch_nmi_watchdog();
		}
		/*
		 * Outside the critical section we can now see whether
		 * we saw a time-warp of the counter going backwards:
		 */
		if (unlikely(prev > now)) {
			arch_spin_lock(&sync_lock);
			max_warp = max(max_warp, prev - now);
			cur_max_warp = max_warp;
			/*
			 * Check whether this bounces back and forth. Only
			 * one CPU should observe time going backwards.
			 */
			if (cur_warps != nr_warps)
				random_warps++;
			nr_warps++;
			cur_warps = nr_warps;
			arch_spin_unlock(&sync_lock);
		}
	}
	WARN(!(now-start),
		"Warning: zero counter calibration delta: %d [max: %d]\n",
			now-start, end-start);
	return cur_max_warp;
}

/*
 * The freshly booted CPU initiates this via an async SMP function call.
 */
static void check_counter_sync_source(void *__cpu)
{
	unsigned int cpu = (unsigned long)__cpu;
	int cpus = 2;

	atomic_set(&test_runs, NR_LOOPS);
retry:
	/* Wait for the target to start. */
	while (atomic_read(&start_count) != cpus - 1)
		cpu_relax();

	/*
	 * Trigger the target to continue into the measurement too:
	 */
	atomic_inc(&start_count);

	check_counter_warp();

	while (atomic_read(&stop_count) != cpus-1)
		cpu_relax();

	/*
	 * If the test was successful set the number of runs to zero and
	 * stop. If not, decrement the number of runs an check if we can
	 * retry. In case of random warps no retry is attempted.
	 */
	if (!nr_warps) {
		atomic_set(&test_runs, 0);

		pr_info("Counter synchronization [CPU#%d -> CPU#%u]: passed\n",
			smp_processor_id(), cpu);
	} else if (atomic_dec_and_test(&test_runs) || random_warps) {
		/* Force it to 0 if random warps brought us here */
		atomic_set(&test_runs, 0);

		pr_info("Counter synchronization [CPU#%d -> CPU#%u]:\n",
			smp_processor_id(), cpu);
		pr_info("Measured %d cycles counter warp between CPUs", max_warp);
		if (random_warps)
			pr_warn("Counter warped randomly between CPUs\n");
	}

	/*
	 * Reset it - just in case we boot another CPU later:
	 */
	atomic_set(&start_count, 0);
	random_warps = 0;
	nr_warps = 0;
	max_warp = 0;
	last_counter = 0;

	/*
	 * Let the target continue with the bootup:
	 */
	atomic_inc(&stop_count);

	/*
	 * Retry, if there is a chance to do so.
	 */
	if (atomic_read(&test_runs) > 0)
		goto retry;
}

/*
 * Freshly booted CPUs call into this:
 */
void synchronise_count_slave(int cpu)
{
	uint32_t cur_max_warp, gbl_max_warp, count;
	int cpus = 2;

	if (!cpu_has_counter || !mips_hpt_frequency)
		return;

	/* Kick the control CPU into the counter synchronization function */
	smp_call_function_single(cpumask_first(cpu_online_mask),
				 check_counter_sync_source,
				 (unsigned long *)(unsigned long)cpu, 0);
retry:
	/*
	 * Register this CPU's participation and wait for the
	 * source CPU to start the measurement:
	 */
	atomic_inc(&start_count);
	while (atomic_read(&start_count) != cpus)
		cpu_relax();

	cur_max_warp = check_counter_warp();

	/*
	 * Store the maximum observed warp value for a potential retry:
	 */
	gbl_max_warp = max_warp;

	/*
	 * Ok, we are done:
	 */
	atomic_inc(&stop_count);

	/*
	 * Wait for the source CPU to print stuff:
	 */
	while (atomic_read(&stop_count) != cpus)
		cpu_relax();

	/*
	 * Reset it for the next sync test:
	 */
	atomic_set(&stop_count, 0);

	/*
	 * Check the number of remaining test runs. If not zero, the test
	 * failed and a retry with adjusted counter is possible. If zero the
	 * test was either successful or failed terminally.
	 */
	if (!atomic_read(&test_runs)) {
		/* Arrange for an interrupt in a short while */
		write_c0_compare(read_c0_count() + COUNTON);
		return;
	}

	/*
	 * If the warp value of this CPU is 0, then the other CPU
	 * observed time going backwards so this counter was ahead and
	 * needs to move backwards.
	 */
	if (!cur_max_warp)
		cur_max_warp = -gbl_max_warp;

	count = read_c0_count();
	count += cur_max_warp;
	write_c0_count(count);

	pr_debug("Counter compensate: CPU%u observed %d warp\n", cpu, cur_max_warp);

	goto retry;

}
