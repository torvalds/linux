/*
 * check TSC synchronization.
 *
 * Copyright (C) 2006, Red Hat, Inc., Ingo Molnar
 *
 * We check whether all boot CPUs have their TSC's synchronized,
 * print a warning if not and turn off the TSC clock-source.
 *
 * The warp-check is point-to-point between two CPUs, the CPU
 * initiating the bootup is the 'source CPU', the freshly booting
 * CPU is the 'target CPU'.
 *
 * Only two CPUs may participate - they can enter in any order.
 * ( The serial nature of the boot logic and the CPU hotplug lock
 *   protects against more than 2 CPUs entering this code. )
 */
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/nmi.h>
#include <asm/tsc.h>

/*
 * Entry/exit counters that make sure that both CPUs
 * run the measurement code at once:
 */
static __cpuinitdata atomic_t start_count;
static __cpuinitdata atomic_t stop_count;

/*
 * We use a raw spinlock in this exceptional case, because
 * we want to have the fastest, inlined, non-debug version
 * of a critical section, to be able to prove TSC time-warps:
 */
static __cpuinitdata raw_spinlock_t sync_lock = __RAW_SPIN_LOCK_UNLOCKED;
static __cpuinitdata cycles_t last_tsc;
static __cpuinitdata cycles_t max_warp;
static __cpuinitdata int nr_warps;

/*
 * TSC-warp measurement loop running on both CPUs:
 */
static __cpuinit void check_tsc_warp(void)
{
	cycles_t start, now, prev, end;
	int i;

	start = get_cycles_sync();
	/*
	 * The measurement runs for 20 msecs:
	 */
	end = start + tsc_khz * 20ULL;
	now = start;

	for (i = 0; ; i++) {
		/*
		 * We take the global lock, measure TSC, save the
		 * previous TSC that was measured (possibly on
		 * another CPU) and update the previous TSC timestamp.
		 */
		__raw_spin_lock(&sync_lock);
		prev = last_tsc;
		now = get_cycles_sync();
		last_tsc = now;
		__raw_spin_unlock(&sync_lock);

		/*
		 * Be nice every now and then (and also check whether
		 * measurement is done [we also insert a 100 million
		 * loops safety exit, so we dont lock up in case the
		 * TSC readout is totally broken]):
		 */
		if (unlikely(!(i & 7))) {
			if (now > end || i > 100000000)
				break;
			cpu_relax();
			touch_nmi_watchdog();
		}
		/*
		 * Outside the critical section we can now see whether
		 * we saw a time-warp of the TSC going backwards:
		 */
		if (unlikely(prev > now)) {
			__raw_spin_lock(&sync_lock);
			max_warp = max(max_warp, prev - now);
			nr_warps++;
			__raw_spin_unlock(&sync_lock);
		}

	}
}

/*
 * Source CPU calls into this - it waits for the freshly booted
 * target CPU to arrive and then starts the measurement:
 */
void __cpuinit check_tsc_sync_source(int cpu)
{
	int cpus = 2;

	/*
	 * No need to check if we already know that the TSC is not
	 * synchronized:
	 */
	if (unsynchronized_tsc())
		return;

	printk(KERN_INFO "checking TSC synchronization [CPU#%d -> CPU#%d]:",
			  smp_processor_id(), cpu);

	/*
	 * Reset it - in case this is a second bootup:
	 */
	atomic_set(&stop_count, 0);

	/*
	 * Wait for the target to arrive:
	 */
	while (atomic_read(&start_count) != cpus-1)
		cpu_relax();
	/*
	 * Trigger the target to continue into the measurement too:
	 */
	atomic_inc(&start_count);

	check_tsc_warp();

	while (atomic_read(&stop_count) != cpus-1)
		cpu_relax();

	/*
	 * Reset it - just in case we boot another CPU later:
	 */
	atomic_set(&start_count, 0);

	if (nr_warps) {
		printk("\n");
		printk(KERN_WARNING "Measured %Ld cycles TSC warp between CPUs,"
				    " turning off TSC clock.\n", max_warp);
		mark_tsc_unstable("check_tsc_sync_source failed");
		nr_warps = 0;
		max_warp = 0;
		last_tsc = 0;
	} else {
		printk(" passed.\n");
	}

	/*
	 * Let the target continue with the bootup:
	 */
	atomic_inc(&stop_count);
}

/*
 * Freshly booted CPUs call into this:
 */
void __cpuinit check_tsc_sync_target(void)
{
	int cpus = 2;

	if (unsynchronized_tsc())
		return;

	/*
	 * Register this CPU's participation and wait for the
	 * source CPU to start the measurement:
	 */
	atomic_inc(&start_count);
	while (atomic_read(&start_count) != cpus)
		cpu_relax();

	check_tsc_warp();

	/*
	 * Ok, we are done:
	 */
	atomic_inc(&stop_count);

	/*
	 * Wait for the source CPU to print stuff:
	 */
	while (atomic_read(&stop_count) != cpus)
		cpu_relax();
}
#undef NR_LOOPS

