/*
 *  linux/arch/h8300/kernel/time.c
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Copied/hacked from:
 *
 *  linux/arch/m68k/kernel/time.c
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
#include <linux/timex.h>
#include <linux/profile.h>

#include <asm/io.h>
#include <asm/irq_regs.h>
#include <asm/timer.h>

#define	TICK_SIZE (tick_nsec / 1000)

void h8300_timer_tick(void)
{
	if (current->pid)
		profile_tick(CPU_PROFILING);
	xtime_update(1);
	update_process_times(user_mode(get_irq_regs()));
}

void read_persistent_clock(struct timespec *ts)
{
	unsigned int year, mon, day, hour, min, sec;

	/* FIX by dqg : Set to zero for platforms that don't have tod */
	/* without this time is undefined and can overflow time_t, causing  */
	/* very strange errors */
	year = 1980;
	mon = day = 1;
	hour = min = sec = 0;
#ifdef CONFIG_H8300_GETTOD
	h8300_gettod (&year, &mon, &day, &hour, &min, &sec);
#endif
	if ((year += 1900) < 1970)
		year += 100;
	ts->tv_sec = mktime(year, mon, day, hour, min, sec);
	ts->tv_nsec = 0;
}

void __init time_init(void)
{

	h8300_timer_setup();
}
