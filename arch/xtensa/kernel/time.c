/*
 * arch/xtensa/kernel/time.c
 *
 * Timer and clock support.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/profile.h>
#include <linux/delay.h>

#include <asm/timex.h>
#include <asm/platform.h>


extern volatile unsigned long wall_jiffies;

spinlock_t rtc_lock = SPIN_LOCK_UNLOCKED;
EXPORT_SYMBOL(rtc_lock);


#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
unsigned long ccount_per_jiffy;		/* per 1/HZ */
unsigned long ccount_nsec;		/* nsec per ccount increment */
#endif

unsigned int last_ccount_stamp;
static long last_rtc_update = 0;

/*
 * Scheduler clock - returns current tim in nanosec units.
 */

unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies * (1000000000 / HZ);
}

static irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static struct irqaction timer_irqaction = {
	.handler =	timer_interrupt,
	.flags =	SA_INTERRUPT,
	.name =		"timer",
};

void __init time_init(void)
{
	time_t sec_o, sec_n = 0;

	/* The platform must provide a function to calibrate the processor
	 * speed for the CALIBRATE.
	 */

#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
	printk("Calibrating CPU frequency ");
	platform_calibrate_ccount();
	printk("%d.%02d MHz\n", (int)ccount_per_jiffy/(1000000/HZ),
			(int)(ccount_per_jiffy/(10000/HZ))%100);
#endif

	/* Set time from RTC (if provided) */

	if (platform_get_rtc_time(&sec_o) == 0)
		while (platform_get_rtc_time(&sec_n))
			if (sec_o != sec_n)
				break;

	xtime.tv_nsec = 0;
	last_rtc_update = xtime.tv_sec = sec_n;
	last_ccount_stamp = get_ccount();

	set_normalized_timespec(&wall_to_monotonic,
		-xtime.tv_sec, -xtime.tv_nsec);

	/* Initialize the linux timer interrupt. */

	setup_irq(LINUX_TIMER_INT, &timer_irqaction);
	set_linux_timer(get_ccount() + CCOUNT_PER_JIFFY);
}


int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;
	unsigned long ccount;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);

	/* This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */
	ccount = get_ccount();
	nsec -= (ccount - last_ccount_stamp) * CCOUNT_NSEC;
	nsec -= (jiffies - wall_jiffies) * CCOUNT_PER_JIFFY * CCOUNT_NSEC;

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	ntp_clear();
	write_sequnlock_irq(&xtime_lock);
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);


void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long sec, usec, delta, lost, seq;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);

		delta = get_ccount() - last_ccount_stamp;
		sec = xtime.tv_sec;
		usec = (xtime.tv_nsec / NSEC_PER_USEC);

		lost = jiffies - wall_jiffies;

	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	usec += lost * (1000000UL/HZ) + (delta * CCOUNT_NSEC) / NSEC_PER_USEC;
	for (; usec >= 1000000; sec++, usec -= 1000000)
		;

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);

/*
 * The timer interrupt is called HZ times per second.
 */

irqreturn_t timer_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{

	unsigned long next;

	next = get_linux_timer();

again:
	while ((signed long)(get_ccount() - next) > 0) {

		profile_tick(CPU_PROFILING, regs);
#ifndef CONFIG_SMP
		update_process_times(user_mode(regs));
#endif

		write_seqlock(&xtime_lock);

		last_ccount_stamp = next;
		next += CCOUNT_PER_JIFFY;
		do_timer (regs); /* Linux handler in kernel/timer.c */

		if (ntp_synced() &&
		    xtime.tv_sec - last_rtc_update >= 659 &&
		    abs((xtime.tv_nsec/1000)-(1000000-1000000/HZ))<5000000/HZ &&
		    jiffies - wall_jiffies == 1) {

			if (platform_set_rtc_time(xtime.tv_sec+1) == 0)
				last_rtc_update = xtime.tv_sec+1;
			else
				/* Do it again in 60 s */
				last_rtc_update += 60;
		}
		write_sequnlock(&xtime_lock);
	}

	/* NOTE: writing CCOMPAREn clears the interrupt.  */

	set_linux_timer (next);

	/* Make sure we didn't miss any tick... */

	if ((signed long)(get_ccount() - next) > 0)
		goto again;

	/* Allow platform to do something usefull (Wdog). */

	platform_heartbeat();

	return IRQ_HANDLED;
}

#ifndef CONFIG_GENERIC_CALIBRATE_DELAY
void __devinit calibrate_delay(void)
{
	loops_per_jiffy = CCOUNT_PER_JIFFY;
	printk("Calibrating delay loop (skipped)... "
	       "%lu.%02lu BogoMIPS preset\n",
	       loops_per_jiffy/(1000000/HZ),
	       (loops_per_jiffy/(10000/HZ)) % 100);
}
#endif

