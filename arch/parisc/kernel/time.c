/*
 *  linux/arch/parisc/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Modifications for ARM (C) 1994, 1995, 1996,1997 Russell King
 *  Copyright (C) 1999 SuSE GmbH, (Philipp Rumpf, prumpf@tux.org)
 *
 * 1994-07-02  Alan Modra
 *             fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1998-12-20  Updated NTP code according to technical memorandum Jan '96
 *             "A Kernel Model for Precision Timekeeping" by Dave Mills
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/profile.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/param.h>
#include <asm/pdc.h>
#include <asm/led.h>

#include <linux/timex.h>

static unsigned long clocktick __read_mostly;	/* timer cycles per tick */

/*
 * We keep time on PA-RISC Linux by using the Interval Timer which is
 * a pair of registers; one is read-only and one is write-only; both
 * accessed through CR16.  The read-only register is 32 or 64 bits wide,
 * and increments by 1 every CPU clock tick.  The architecture only
 * guarantees us a rate between 0.5 and 2, but all implementations use a
 * rate of 1.  The write-only register is 32-bits wide.  When the lowest
 * 32 bits of the read-only register compare equal to the write-only
 * register, it raises a maskable external interrupt.  Each processor has
 * an Interval Timer of its own and they are not synchronised.  
 *
 * We want to generate an interrupt every 1/HZ seconds.  So we program
 * CR16 to interrupt every @clocktick cycles.  The it_value in cpu_data
 * is programmed with the intended time of the next tick.  We can be
 * held off for an arbitrarily long period of time by interrupts being
 * disabled, so we may miss one or more ticks.
 */
irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	unsigned long now;
	unsigned long next_tick;
	unsigned long cycles_elapsed, ticks_elapsed;
	unsigned long cycles_remainder;
	unsigned int cpu = smp_processor_id();
	struct cpuinfo_parisc *cpuinfo = &cpu_data[cpu];

	/* gcc can optimize for "read-only" case with a local clocktick */
	unsigned long cpt = clocktick;

	profile_tick(CPU_PROFILING);

	/* Initialize next_tick to the expected tick time. */
	next_tick = cpuinfo->it_value;

	/* Get current interval timer.
	 * CR16 reads as 64 bits in CPU wide mode.
	 * CR16 reads as 32 bits in CPU narrow mode.
	 */
	now = mfctl(16);

	cycles_elapsed = now - next_tick;

	if ((cycles_elapsed >> 5) < cpt) {
		/* use "cheap" math (add/subtract) instead
		 * of the more expensive div/mul method
		 */
		cycles_remainder = cycles_elapsed;
		ticks_elapsed = 1;
		while (cycles_remainder > cpt) {
			cycles_remainder -= cpt;
			ticks_elapsed++;
		}
	} else {
		cycles_remainder = cycles_elapsed % cpt;
		ticks_elapsed = 1 + cycles_elapsed / cpt;
	}

	/* Can we differentiate between "early CR16" (aka Scenario 1) and
	 * "long delay" (aka Scenario 3)? I don't think so.
	 *
	 * We expected timer_interrupt to be delivered at least a few hundred
	 * cycles after the IT fires. But it's arbitrary how much time passes
	 * before we call it "late". I've picked one second.
	 */
	if (ticks_elapsed > HZ) {
		/* Scenario 3: very long delay?  bad in any case */
		printk (KERN_CRIT "timer_interrupt(CPU %d): delayed!"
			" cycles %lX rem %lX "
			" next/now %lX/%lX\n",
			cpu,
			cycles_elapsed, cycles_remainder,
			next_tick, now );
	}

	/* convert from "division remainder" to "remainder of clock tick" */
	cycles_remainder = cpt - cycles_remainder;

	/* Determine when (in CR16 cycles) next IT interrupt will fire.
	 * We want IT to fire modulo clocktick even if we miss/skip some.
	 * But those interrupts don't in fact get delivered that regularly.
	 */
	next_tick = now + cycles_remainder;

	cpuinfo->it_value = next_tick;

	/* Skip one clocktick on purpose if we are likely to miss next_tick.
	 * We want to avoid the new next_tick being less than CR16.
	 * If that happened, itimer wouldn't fire until CR16 wrapped.
	 * We'll catch the tick we missed on the tick after that.
	 */
	if (!(cycles_remainder >> 13))
		next_tick += cpt;

	/* Program the IT when to deliver the next interrupt. */
	/* Only bottom 32-bits of next_tick are written to cr16.  */
	mtctl(next_tick, 16);


	/* Done mucking with unreliable delivery of interrupts.
	 * Go do system house keeping.
	 */

	if (!--cpuinfo->prof_counter) {
		cpuinfo->prof_counter = cpuinfo->prof_multiplier;
		update_process_times(user_mode(get_irq_regs()));
	}

	if (cpu == 0) {
		write_seqlock(&xtime_lock);
		do_timer(ticks_elapsed);
		write_sequnlock(&xtime_lock);
	}

	/* check soft power switch status */
	if (cpu == 0 && !atomic_read(&power_tasklet.count))
		tasklet_schedule(&power_tasklet);

	return IRQ_HANDLED;
}


unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	if (regs->gr[0] & PSW_N)
		pc -= 4;

#ifdef CONFIG_SMP
	if (in_lock_functions(pc))
		pc = regs->gr[2];
#endif

	return pc;
}
EXPORT_SYMBOL(profile_pc);


/*
 * Return the number of micro-seconds that elapsed since the last
 * update to wall time (aka xtime).  The xtime_lock
 * must be at least read-locked when calling this routine.
 */
static inline unsigned long gettimeoffset (void)
{
#ifndef CONFIG_SMP
	/*
	 * FIXME: This won't work on smp because jiffies are updated by cpu 0.
	 *    Once parisc-linux learns the cr16 difference between processors,
	 *    this could be made to work.
	 */
	unsigned long now;
	unsigned long prev_tick;
	unsigned long next_tick;
	unsigned long elapsed_cycles;
	unsigned long usec;
	unsigned long cpuid = smp_processor_id();
	unsigned long cpt = clocktick;

	next_tick = cpu_data[cpuid].it_value;
	now = mfctl(16);	/* Read the hardware interval timer.  */

	prev_tick = next_tick - cpt;

	/* Assume Scenario 1: "now" is later than prev_tick.  */
	elapsed_cycles = now - prev_tick;

/* aproximate HZ with shifts. Intended math is "(elapsed/clocktick) > HZ" */
#if HZ == 1000
	if (elapsed_cycles > (cpt << 10) )
#elif HZ == 250
	if (elapsed_cycles > (cpt << 8) )
#elif HZ == 100
	if (elapsed_cycles > (cpt << 7) )
#else
#warn WTF is HZ set to anyway?
	if (elapsed_cycles > (HZ * cpt) )
#endif
	{
		/* Scenario 3: clock ticks are missing. */
		printk (KERN_CRIT "gettimeoffset(CPU %ld): missing %ld ticks!"
			" cycles %lX prev/now/next %lX/%lX/%lX  clock %lX\n",
			cpuid, elapsed_cycles / cpt,
			elapsed_cycles, prev_tick, now, next_tick, cpt);
	}

	/* FIXME: Can we improve the precision? Not with PAGE0. */
	usec = (elapsed_cycles * 10000) / PAGE0->mem_10msec;
	return usec;
#else
	return 0;
#endif
}

void
do_gettimeofday (struct timeval *tv)
{
	unsigned long flags, seq, usec, sec;

	/* Hold xtime_lock and adjust timeval.  */
	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);
		usec = gettimeoffset();
		sec = xtime.tv_sec;
		usec += (xtime.tv_nsec / 1000);
	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	/* Move adjusted usec's into sec's.  */
	while (usec >= USEC_PER_SEC) {
		usec -= USEC_PER_SEC;
		++sec;
	}

	/* Return adjusted result.  */
	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);

int
do_settimeofday (struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);
	{
		/*
		 * This is revolting. We need to set "xtime"
		 * correctly. However, the value in this location is
		 * the value at the most recent update of wall time.
		 * Discover what correction gettimeofday would have
		 * done, and then undo it!
		 */
		nsec -= gettimeoffset() * 1000;

		wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
		wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

		set_normalized_timespec(&xtime, sec, nsec);
		set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

		ntp_clear();
	}
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();
	return 0;
}
EXPORT_SYMBOL(do_settimeofday);

/*
 * XXX: We can do better than this.
 * Returns nanoseconds
 */

unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies * (1000000000 / HZ);
}


void __init start_cpu_itimer(void)
{
	unsigned int cpu = smp_processor_id();
	unsigned long next_tick = mfctl(16) + clocktick;

	mtctl(next_tick, 16);		/* kick off Interval Timer (CR16) */

	cpu_data[cpu].it_value = next_tick;
}

void __init time_init(void)
{
	static struct pdc_tod tod_data;

	clocktick = (100 * PAGE0->mem_10msec) / HZ;

	start_cpu_itimer();	/* get CPU 0 started */

	if (pdc_tod_read(&tod_data) == 0) {
		unsigned long flags;

		write_seqlock_irqsave(&xtime_lock, flags);
		xtime.tv_sec = tod_data.tod_sec;
		xtime.tv_nsec = tod_data.tod_usec * 1000;
		set_normalized_timespec(&wall_to_monotonic,
		                        -xtime.tv_sec, -xtime.tv_nsec);
		write_sequnlock_irqrestore(&xtime_lock, flags);
	} else {
		printk(KERN_ERR "Error reading tod clock\n");
	        xtime.tv_sec = 0;
		xtime.tv_nsec = 0;
	}
}

