/*
 * linux/arch/v850/kernel/time.c -- Arch-dependent timer functions
 *
 *  Copyright (C) 1991, 1992, 1995, 2001, 2002  Linus Torvalds
 *
 * This file contains the v850-specific time handling details.
 * Most of the stuff is located in the machine specific files.
 *
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/profile.h>

#include <asm/io.h>

#include "mach.h"

#define TICK_SIZE	(tick_nsec / 1000)

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies * (1000000000 / HZ);
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static irqreturn_t timer_interrupt (int irq, void *dummy, struct pt_regs *regs)
{
#if 0
	/* last time the cmos clock got updated */
	static long last_rtc_update=0;
#endif

	/* may need to kick the hardware timer */
	if (mach_tick)
	  mach_tick ();

	do_timer (1);
#ifndef CONFIG_SMP
	update_process_times(user_mode(regs));
#endif
	profile_tick(CPU_PROFILING, regs);
#if 0
	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (ntp_synced() &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2) {
	  if (set_rtc_mmss (xtime.tv_sec) == 0)
	    last_rtc_update = xtime.tv_sec;
	  else
	    last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}
#ifdef CONFIG_HEARTBEAT
	/* use power LED as a heartbeat instead -- much more useful
	   for debugging -- based on the version for PReP by Cort */
	/* acts like an actual heart beat -- ie thump-thump-pause... */
	if (mach_heartbeat) {
	    static unsigned cnt = 0, period = 0, dist = 0;

	    if (cnt == 0 || cnt == dist)
		mach_heartbeat ( 1 );
	    else if (cnt == 7 || cnt == dist+7)
		mach_heartbeat ( 0 );

	    if (++cnt > period) {
		cnt = 0;
		/* The hyperbolic function below modifies the heartbeat period
		 * length in dependency of the current (5min) load. It goes
		 * through the points f(0)=126, f(1)=86, f(5)=51,
		 * f(inf)->30. */
		period = ((672<<FSHIFT)/(5*avenrun[0]+(7<<FSHIFT))) + 30;
		dist = period / 4;
	    }
	}
#endif /* CONFIG_HEARTBEAT */
#endif /* 0 */

	return IRQ_HANDLED;
}

/*
 * This version of gettimeofday has near microsecond resolution.
 */
void do_gettimeofday (struct timeval *tv)
{
#if 0 /* DAVIDM later if possible */
	extern volatile unsigned long lost_ticks;
	unsigned long lost;
#endif
	unsigned long flags;
	unsigned long usec, sec;
	unsigned long seq;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);

#if 0
		usec = mach_gettimeoffset ? mach_gettimeoffset () : 0;
#else
		usec = 0;
#endif
#if 0 /* DAVIDM later if possible */
		lost = lost_ticks;
		if (lost)
			usec += lost * (1000000/HZ);
#endif
		sec = xtime.tv_sec;
		usec += xtime.tv_nsec / 1000;
	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);

int do_settimeofday(struct timespec *tv)
{
	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq (&xtime_lock);

	/* This is revolting. We need to set the xtime.tv_nsec
	 * correctly. However, the value in this location is
	 * is value at the last tick.
	 * Discover what correction gettimeofday
	 * would have done, and then undo it!
	 */
#if 0
	tv->tv_nsec -= mach_gettimeoffset() * 1000;
#endif

	while (tv->tv_nsec < 0) {
		tv->tv_nsec += NSEC_PER_SEC;
		tv->tv_sec--;
	}

	xtime.tv_sec = tv->tv_sec;
	xtime.tv_nsec = tv->tv_nsec;

	ntp_clear();

	write_sequnlock_irq (&xtime_lock);
	clock_was_set();
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

static int timer_dev_id;
static struct irqaction timer_irqaction = {
	timer_interrupt,
	IRQF_DISABLED,
	CPU_MASK_NONE,
	"timer",
	&timer_dev_id,
	NULL
};

void time_init (void)
{
	mach_gettimeofday (&xtime);
	mach_sched_init (&timer_irqaction);
}
