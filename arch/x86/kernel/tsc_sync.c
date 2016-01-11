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
#include <linux/smp.h>
#include <linux/nmi.h>
#include <asm/tsc.h>

/*
 * Entry/exit counters that make sure that both CPUs
 * run the measurement code at once:
 */
static atomic_t start_count;
static atomic_t stop_count;

/*
 * We use a raw spinlock in this exceptional case, because
 * we want to have the fastest, inlined, non-debug version
 * of a critical section, to be able to prove TSC time-warps:
 */
static arch_spinlock_t sync_lock = __ARCH_SPIN_LOCK_UNLOCKED;

static cycles_t last_tsc;
static cycles_t max_warp;
static int nr_warps;

/*
 * TSC-warp measurement loop running on both CPUs.  This is not called
 * if there is no TSC.
 */
static void check_tsc_warp(unsigned int timeout)
{
	cycles_t start, now, prev, end;
	int i;

	start = rdtsc_ordered();
	/*
	 * The measurement runs for 'timeout' msecs:
	 */
	end = start + (cycles_t) tsc_khz * timeout;
	now = start;

	for (i = 0; ; i++) {
		/*
		 * We take the global lock, measure TSC, save the
		 * previous TSC that was measured (possibly on
		 * another CPU) and update the previous TSC timestamp.
		 */
		arch_spin_lock(&sync_lock);
		prev = last_tsc;
		now = rdtsc_ordered();
		last_tsc = now;
		arch_spin_unlock(&sync_lock);

		/*
		 * Be nice every now and then (and also check whether
		 * measurement is done [we also insert a 10 million
		 * loops safety exit, so we dont lock up in case the
		 * TSC readout is totally broken]):
		 */
		if (unlikely(!(i & 7))) {
			if (now > end || i > 10000000)
				break;
			cpu_relax();
			touch_nmi_watchdog();
		}
		/*
		 * Outside the critical section we can now see whether
		 * we saw a time-warp of the TSC going backwards:
		 */
		if (unlikely(prev > now)) {
			arch_spin_lock(&sync_lock);
			max_warp = max(max_warp, prev - now);
			nr_warps++;
			arch_spin_unlock(&sync_lock);
		}
	}
	WARN(!(now-start),
		"Warning: zero tsc calibration delta: %Ld [max: %Ld]\n",
			now-start, end-start);
}

/*
 * If the target CPU coming online doesn't have any of its core-siblings
 * online, a timeout of 20msec will be used for the TSC-warp measurement
 * loop. Otherwise a smaller timeout of 2msec will be used, as we have some
 * information about this socket already (and this information grows as we
 * have more and more logical-siblings in that socket).
 *
 * Ideally we should be able to skip the TSC sync check on the other
 * core-siblings, if the first logical CPU in a socket passed the sync test.
 * But as the TSC is per-logical CPU and can potentially be modified wrongly
 * by the bios, TSC sync test for smaller duration should be able
 * to catch such errors. Also this will catch the condition where all the
 * cores in the socket doesn't get reset at the same time.
 */
static inline unsigned int loop_timeout(int cpu)
{
	return (cpumask_weight(topology_core_cpumask(cpu)) > 1) ? 2 : 20;
}

/*
 * Source CPU calls into this - it waits for the freshly booted
 * target CPU to arrive and then starts the measurement:
 */
void check_tsc_sync_source(int cpu)
{
	int cpus = 2;

	/*
	 * No need to check if we already know that the TSC is not
	 * synchronized or if we have no TSC.
	 */
	if (unsynchronized_tsc())
		return;

	if (tsc_clocksource_reliable) {
		if (cpu == (nr_cpu_ids-1) || system_state != SYSTEM_BOOTING)
			pr_info(
			"Skipped synchronization checks as TSC is reliable.\n");
		return;
	}

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

	check_tsc_warp(loop_timeout(cpu));

	while (atomic_read(&stop_count) != cpus-1)
		cpu_relax();

	if (nr_warps) {
		pr_warning("TSC synchronization [CPU#%d -> CPU#%d]:\n",
			smp_processor_id(), cpu);
		pr_warning("Measured %Ld cycles TSC warp between CPUs, "
			   "turning off TSC clock.\n", max_warp);
		mark_tsc_unstable("check_tsc_sync_source failed");
	} else {
		pr_debug("TSC synchronization [CPU#%d -> CPU#%d]: passed\n",
			smp_processor_id(), cpu);
	}

	/*
	 * Reset it - just in case we boot another CPU later:
	 */
	atomic_set(&start_count, 0);
	nr_warps = 0;
	max_warp = 0;
	last_tsc = 0;

	/*
	 * Let the target continue with the bootup:
	 */
	atomic_inc(&stop_count);
}

/*
 * Freshly booted CPUs call into this:
 */
void check_tsc_sync_target(void)
{
	int cpus = 2;

	/* Also aborts if there is no TSC. */
	if (unsynchronized_tsc() || tsc_clocksource_reliable)
		return;

	/*
	 * Register this CPU's participation and wait for the
	 * source CPU to start the measurement:
	 */
	atomic_inc(&start_count);
	while (atomic_read(&start_count) != cpus)
		cpu_relax();

	check_tsc_warp(loop_timeout(smp_processor_id()));

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
