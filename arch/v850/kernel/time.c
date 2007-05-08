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
