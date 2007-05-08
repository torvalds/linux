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
#include <asm/target_time.h>

#define	TICK_SIZE (tick_nsec / 1000)

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static void timer_interrupt(int irq, void *dummy, struct pt_regs * regs)
{
	/* may need to kick the hardware timer */
	platform_timer_eoi();

	do_timer(1);
#ifndef CONFIG_SMP
	update_process_times(user_mode(regs));
#endif
	profile_tick(CPU_PROFILING);
}

void time_init(void)
{
	unsigned int year, mon, day, hour, min, sec;

	/* FIX by dqg : Set to zero for platforms that don't have tod */
	/* without this time is undefined and can overflow time_t, causing  */
	/* very stange errors */
	year = 1980;
	mon = day = 1;
	hour = min = sec = 0;
	platform_gettod (&year, &mon, &day, &hour, &min, &sec);

	if ((year += 1900) < 1970)
		year += 100;
	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_nsec = 0;

	platform_timer_setup(timer_interrupt);
}
