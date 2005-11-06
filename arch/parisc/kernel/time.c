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
#include <linux/config.h>
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

/* xtime and wall_jiffies keep wall-clock time */
extern unsigned long wall_jiffies;

static long clocktick;	/* timer cycles per tick */
static long halftick;

#ifdef CONFIG_SMP
extern void smp_do_timer(struct pt_regs *regs);
#endif

irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	long now;
	long next_tick;
	int nticks;
	int cpu = smp_processor_id();

	profile_tick(CPU_PROFILING, regs);

	now = mfctl(16);
	/* initialize next_tick to time at last clocktick */
	next_tick = cpu_data[cpu].it_value;

	/* since time passes between the interrupt and the mfctl()
	 * above, it is never true that last_tick + clocktick == now.  If we
	 * never miss a clocktick, we could set next_tick = last_tick + clocktick
	 * but maybe we'll miss ticks, hence the loop.
	 *
	 * Variables are *signed*.
	 */

	nticks = 0;
	while((next_tick - now) < halftick) {
		next_tick += clocktick;
		nticks++;
	}
	mtctl(next_tick, 16);
	cpu_data[cpu].it_value = next_tick;

	while (nticks--) {
#ifdef CONFIG_SMP
		smp_do_timer(regs);
#else
		update_process_times(user_mode(regs));
#endif
		if (cpu == 0) {
			write_seqlock(&xtime_lock);
			do_timer(regs);
			write_sequnlock(&xtime_lock);
		}
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


/*** converted from ia64 ***/
/*
 * Return the number of micro-seconds that elapsed since the last
 * update to wall time (aka xtime aka wall_jiffies).  The xtime_lock
 * must be at least read-locked when calling this routine.
 */
static inline unsigned long
gettimeoffset (void)
{
#ifndef CONFIG_SMP
	/*
	 * FIXME: This won't work on smp because jiffies are updated by cpu 0.
	 *    Once parisc-linux learns the cr16 difference between processors,
	 *    this could be made to work.
	 */
	long last_tick;
	long elapsed_cycles;

	/* it_value is the intended time of the next tick */
	last_tick = cpu_data[smp_processor_id()].it_value;

	/* Subtract one tick and account for possible difference between
	 * when we expected the tick and when it actually arrived.
	 * (aka wall vs real)
	 */
	last_tick -= clocktick * (jiffies - wall_jiffies + 1);
	elapsed_cycles = mfctl(16) - last_tick;

	/* the precision of this math could be improved */
	return elapsed_cycles / (PAGE0->mem_10msec / 10000);
#else
	return 0;
#endif
}

void
do_gettimeofday (struct timeval *tv)
{
	unsigned long flags, seq, usec, sec;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);
		usec = gettimeoffset();
		sec = xtime.tv_sec;
		usec += (xtime.tv_nsec / 1000);
	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	while (usec >= 1000000) {
		usec -= 1000000;
		++sec;
	}

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


void __init time_init(void)
{
	unsigned long next_tick;
	static struct pdc_tod tod_data;

	clocktick = (100 * PAGE0->mem_10msec) / HZ;
	halftick = clocktick / 2;

	/* Setup clock interrupt timing */

	next_tick = mfctl(16);
	next_tick += clocktick;
	cpu_data[smp_processor_id()].it_value = next_tick;

	/* kick off Itimer (CR16) */
	mtctl(next_tick, 16);

	if(pdc_tod_read(&tod_data) == 0) {
		write_seqlock_irq(&xtime_lock);
		xtime.tv_sec = tod_data.tod_sec;
		xtime.tv_nsec = tod_data.tod_usec * 1000;
		set_normalized_timespec(&wall_to_monotonic,
		                        -xtime.tv_sec, -xtime.tv_nsec);
		write_sequnlock_irq(&xtime_lock);
	} else {
		printk(KERN_ERR "Error reading tod clock\n");
	        xtime.tv_sec = 0;
		xtime.tv_nsec = 0;
	}
}

