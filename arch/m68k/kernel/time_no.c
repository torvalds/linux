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
#include <linux/rtc.h>

#include <asm/machdep.h>
#include <asm/irq_regs.h>

static inline int set_rtc_mmss(unsigned long nowtime)
{
	if (mach_set_clock_mmss)
		return mach_set_clock_mmss (nowtime);
	return -1;
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "xtime_update()" routine every clocktick
 */
static irqreturn_t timer_interrupt(int irq, void *dummy)
{

	if (current->pid)
		profile_tick(CPU_PROFILING);

	xtime_update(1);

	update_process_times(user_mode(get_irq_regs()));

	return(IRQ_HANDLED);
}

void read_persistent_clock(struct timespec *ts)
{
	struct rtc_time time;
	ts->tv_sec = 0;
	ts->tv_nsec = 0;

	if (mach_hwclk) {
		mach_hwclk(0, &time);

		if ((time.tm_year += 1900) < 1970)
			time.tm_year += 100;
		ts->tv_sec = mktime(time.tm_year, time.tm_mon, time.tm_mday,
			time.tm_hour, time.tm_min, time.tm_sec);
	}
}

int update_persistent_clock(struct timespec now)
{
	return set_rtc_mmss(now.tv_sec);
}

void __init time_init(void)
{
	mach_sched_init(timer_interrupt);
}
