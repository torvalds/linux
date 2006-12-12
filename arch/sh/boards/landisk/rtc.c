/*
 * arch/sh/boards/landisk/rtc.c --  RTC support
 *
 *  Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 */
/*
 * modifed by kogiidena
 * 2005.09.16
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/bcd.h>
#include <asm/rtc.h>

extern spinlock_t rtc_lock;

extern void
rs5c313_set_cmos_time(unsigned int BCD_yr, unsigned int BCD_mon,
		      unsigned int BCD_day, unsigned int BCD_hr,
		      unsigned int BCD_min, unsigned int BCD_sec);

extern unsigned long
rs5c313_get_cmos_time(unsigned int *BCD_yr, unsigned int *BCD_mon,
		      unsigned int *BCD_day, unsigned int *BCD_hr,
		      unsigned int *BCD_min, unsigned int *BCD_sec);

void landisk_rtc_gettimeofday(struct timespec *tv)
{
	unsigned int BCD_yr, BCD_mon, BCD_day, BCD_hr, BCD_min, BCD_sec;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	tv->tv_sec = rs5c313_get_cmos_time
	    (&BCD_yr, &BCD_mon, &BCD_day, &BCD_hr, &BCD_min, &BCD_sec);
	tv->tv_nsec = 0;
	spin_unlock_irqrestore(&rtc_lock, flags);
}

int landisk_rtc_settimeofday(const time_t secs)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	unsigned long flags;
	unsigned long nowtime = secs;
	unsigned int BCD_yr, BCD_mon, BCD_day, BCD_hr, BCD_min, BCD_sec;

	spin_lock_irqsave(&rtc_lock, flags);

	rs5c313_get_cmos_time
	  (&BCD_yr, &BCD_mon, &BCD_day, &BCD_hr, &BCD_min, &BCD_sec);
	cmos_minutes = BCD_min;
	BCD_TO_BIN(cmos_minutes);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15) / 30) & 1)
		real_minutes += 30;	/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		BIN_TO_BCD(real_seconds);
		BIN_TO_BCD(real_minutes);
		rs5c313_set_cmos_time(BCD_yr, BCD_mon, BCD_day, BCD_hr,
				      real_minutes, real_seconds);
	} else {
		printk(KERN_WARNING
		       "set_rtc_time: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
		retval = -1;
	}

	spin_unlock_irqrestore(&rtc_lock, flags);
	return retval;
}

void landisk_time_init(void)
{
	rtc_sh_get_time = landisk_rtc_gettimeofday;
	rtc_sh_set_time = landisk_rtc_settimeofday;
}
