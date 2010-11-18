/*
 *  linux/arch/m68knommu/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * This file contains the m68k-specific time handling details.
 * Most of the stuff is located in the machine specific files.
 *
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/profile.h>
#include <linux/time.h>
#include <linux/timex.h>

#include <asm/machdep.h>
#include <asm/irq_regs.h>

#define	TICK_SIZE (tick_nsec / 1000)

static inline int set_rtc_mmss(unsigned long nowtime)
{
	if (mach_set_clock_mmss)
		return mach_set_clock_mmss (nowtime);
	return -1;
}

#ifndef CONFIG_GENERIC_CLOCKEVENTS
/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
irqreturn_t arch_timer_interrupt(int irq, void *dummy)
{

	if (current->pid)
		profile_tick(CPU_PROFILING);

	write_seqlock(&xtime_lock);

	do_timer(1);

	write_sequnlock(&xtime_lock);

	update_process_times(user_mode(get_irq_regs()));

	return(IRQ_HANDLED);
}
#endif

static unsigned long read_rtc_mmss(void)
{
	unsigned int year, mon, day, hour, min, sec;

	if (mach_gettod) {
		mach_gettod(&year, &mon, &day, &hour, &min, &sec);
		if ((year += 1900) < 1970)
			year += 100;
	} else {
		year = 1970;
		mon = day = 1;
		hour = min = sec = 0;
	}


	return  mktime(year, mon, day, hour, min, sec);
}

void read_persistent_clock(struct timespec *ts)
{
	ts->tv_sec = read_rtc_mmss();
	ts->tv_nsec = 0;
}

int update_persistent_clock(struct timespec now)
{
	return set_rtc_mmss(now.tv_sec);
}

void time_init(void)
{
	hw_timer_init();
}
