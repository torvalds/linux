/*
 * Count register synchronisation.
 *
 * All CPUs will have their count registers synchronised to the CPU0 next time
 * value. This can cause a small timewarp for CPU0. All other CPU's should
 * not have done anything significant (but they may have had interrupts
 * enabled briefly - prom_smp_finish() should not be responsible for enabling
 * interrupts...)
 */

#include <linux/kernel.h>
#include <linux/irqflags.h>
#include <linux/cpumask.h>

#include <asm/r4k-timer.h>
#include <linux/atomic.h>
#include <asm/barrier.h>
#include <asm/mipsregs.h>

static unsigned int initcount = 0;
static atomic_t count_count_start = ATOMIC_INIT(0);
static atomic_t count_count_stop = ATOMIC_INIT(0);

#define COUNTON 100
#define NR_LOOPS 3

void synchronise_count_master(int cpu)
{
	int i;
	unsigned long flags;

	pr_info("Synchronize counters for CPU %u: ", cpu);

	local_irq_save(flags);

	/*
	 * We loop a few times to get a primed instruction cache,
	 * then the last pass is more or less synchronised and
	 * the master and slaves each set their cycle counters to a known
	 * value all at once. This reduces the chance of having random offsets
	 * between the processors, and guarantees that the maximum
	 * delay between the cycle counters is never bigger than
	 * the latency of information-passing (cachelines) between
	 * two CPUs.
	 */

	for (i = 0; i < NR_LOOPS; i++) {
		/* slaves loop on '!= 2' */
		while (atomic_read(&count_count_start) != 1)
			mb();
		atomic_set(&count_count_stop, 0);
		smp_wmb();

		/* Let the slave writes its count register */
		atomic_inc(&count_count_start);

		/* Count will be initialised to current timer */
		if (i == 1)
			initcount = read_c0_count();

		/*
		 * Everyone initialises count in the last loop:
		 */
		if (i == NR_LOOPS-1)
			write_c0_count(initcount);

		/*
		 * Wait for slave to leave the synchronization point:
		 */
		while (atomic_read(&count_count_stop) != 1)
			mb();
		atomic_set(&count_count_start, 0);
		smp_wmb();
		atomic_inc(&count_count_stop);
	}
	/* Arrange for an interrupt in a short while */
	write_c0_compare(read_c0_count() + COUNTON);

	local_irq_restore(flags);

	/*
	 * i386 code reported the skew here, but the
	 * count registers were almost certainly out of sync
	 * so no point in alarming people
	 */
	pr_cont("done.\n");
}

void synchronise_count_slave(int cpu)
{
	int i;

	/*
	 * Not every cpu is online at the time this gets called,
	 * so we first wait for the master to say everyone is ready
	 */

	for (i = 0; i < NR_LOOPS; i++) {
		atomic_inc(&count_count_start);
		while (atomic_read(&count_count_start) != 2)
			mb();

		/*
		 * Everyone initialises count in the last loop:
		 */
		if (i == NR_LOOPS-1)
			write_c0_count(initcount);

		atomic_inc(&count_count_stop);
		while (atomic_read(&count_count_stop) != 2)
			mb();
	}
	/* Arrange for an interrupt in a short while */
	write_c0_compare(read_c0_count() + COUNTON);
}
#undef NR_LOOPS
