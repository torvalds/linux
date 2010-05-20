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

#include <asm/rtc.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/bcd.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/profile.h>
#include <linux/sched.h>	/* just for sched_clock() - funny that */

int have_rtc;  /* used to remember if we have an RTC or not */;

#define TICK_SIZE tick

extern unsigned long loops_per_jiffy; /* init/main.c */
unsigned long loops_per_usec;

extern unsigned long do_slow_gettimeoffset(void);
static unsigned long (*do_gettimeoffset)(void) = do_slow_gettimeoffset;

u32 arch_gettimeoffset(void)
{
	return do_gettimeoffset() * 1000;
}

/*
 * BUG: This routine does not handle hour overflow properly; it just
 *      sets the minutes. Usually you'll only notice that after reboot!
 */

int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;

	printk(KERN_DEBUG "set_rtc_mmss(%lu)\n", nowtime);

	if(!have_rtc)
		return 0;

	cmos_minutes = CMOS_READ(RTC_MINUTES);
	cmos_minutes = bcd2bin(cmos_minutes);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
		real_minutes += 30;		/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		real_seconds = bin2bcd(real_seconds);
		real_minutes = bin2bcd(real_minutes);
		CMOS_WRITE(real_seconds,RTC_SECONDS);
		CMOS_WRITE(real_minutes,RTC_MINUTES);
	} else {
		printk(KERN_WARNING
		       "set_rtc_mmss: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
		retval = -1;
	}

	return retval;
}

/* grab the time from the RTC chip */

unsigned long
get_cmos_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	if(!have_rtc)
		return 0;

	sec = CMOS_READ(RTC_SECONDS);
	min = CMOS_READ(RTC_MINUTES);
	hour = CMOS_READ(RTC_HOURS);
	day = CMOS_READ(RTC_DAY_OF_MONTH);
	mon = CMOS_READ(RTC_MONTH);
	year = CMOS_READ(RTC_YEAR);

	sec = bcd2bin(sec);
	min = bcd2bin(min);
	hour = bcd2bin(hour);
	day = bcd2bin(day);
	mon = bcd2bin(mon);
	year = bcd2bin(year);

	if ((year += 1900) < 1970)
		year += 100;

	return mktime(year, mon, day, hour, min, sec);
}


int update_persistent_clock(struct timespec now)
{
	return set_rtc_mmss(now.tv_sec);
}

void read_persistent_clock(struct timespec *ts)
{
	ts->tv_sec = get_cmos_time();
	ts->tv_nsec = 0;
}


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

unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies * (1000000000 / HZ) +
		get_ns_in_jiffie();
}

static int
__init init_udelay(void)
{
	loops_per_usec = (loops_per_jiffy * HZ) / 1000000;
	return 0;
}

__initcall(init_udelay);
