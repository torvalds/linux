/*
 *  linux/arch/cris/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Copyright (C) 1999, 2000, 2001 Axis Communications AB
 *
 * 1994-07-02    Alan Modra
 *	fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1995-03-26    Markus Kuhn
 *      fixed 500 ms bug at call to set_rtc_mmss, fixed DS12887
 *      precision CMOS clock update
 * 1996-05-03    Ingo Molnar
 *      fixed time warps in do_[slow|fast]_gettimeoffset()
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 *
 * Linux/CRIS specific code:
 *
 * Authors:    Bjorn Wesen
 *             Johan Adolfsson
 *
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/bcd.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/profile.h>
#include <linux/sched/clock.h>


#define D(x)

#define TICK_SIZE tick

extern unsigned long loops_per_jiffy; /* init/main.c */
unsigned long loops_per_usec;

extern void cris_profile_sample(struct pt_regs* regs);

void
cris_do_profile(struct pt_regs* regs)
{

#ifdef CONFIG_SYSTEM_PROFILER
        cris_profile_sample(regs);
#endif

#ifdef CONFIG_PROFILING
	profile_tick(CPU_PROFILING);
#endif
}

#ifndef CONFIG_GENERIC_SCHED_CLOCK
unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies * (NSEC_PER_SEC / HZ) +
		get_ns_in_jiffie();
}
#endif

static int
__init init_udelay(void)
{
	loops_per_usec = (loops_per_jiffy * HZ) / 1000000;
	return 0;
}

__initcall(init_udelay);
