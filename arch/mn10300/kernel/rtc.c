/* MN10300 RTC management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>
#include <linux/bcd.h>
#include <linux/timex.h>
#include <asm/rtc-regs.h>
#include <asm/rtc.h>

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

/*
 * Read the current RTC time
 */
void read_persistent_clock(struct timespec *ts)
{
	struct rtc_time tm;

	get_rtc_time(&tm);

	ts->tv_nsec = 0;
	ts->tv_sec = mktime(tm.tm_year, tm.tm_mon, tm.tm_mday,
			    tm.tm_hour, tm.tm_min, tm.tm_sec);

	/* if rtc is way off in the past, set something reasonable */
	if (ts->tv_sec < 0)
		ts->tv_sec = mktime(2009, 1, 1, 12, 0, 0);
}

/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be called 500
 * ms after the second nowtime has started, because when nowtime is written
 * into the registers of the CMOS clock, it will jump to the next second
 * precisely 500 ms later.  Check the Motorola MC146818A or Dallas DS12887 data
 * sheet for details.
 *
 * BUG: This routine does not handle hour overflow properly; it just
 *      sets the minutes. Usually you'll only notice that after reboot!
 */
static int set_rtc_mmss(unsigned long nowtime)
{
	unsigned char save_control, save_freq_select;
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;

	/* gets recalled with irq locally disabled */
	spin_lock(&rtc_lock);
	save_control = CMOS_READ(RTC_CONTROL); /* tell the clock it's being
						* set */
	CMOS_WRITE(save_control | RTC_SET, RTC_CONTROL);

	save_freq_select = CMOS_READ(RTC_FREQ_SELECT); /* stop and reset
							* prescaler */
	CMOS_WRITE(save_freq_select | RTC_DIV_RESET2, RTC_FREQ_SELECT);

	cmos_minutes = CMOS_READ(RTC_MINUTES);
	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		cmos_minutes = bcd2bin(cmos_minutes);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15) / 30) & 1)
		/* correct for half hour time zone */
		real_minutes += 30;
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
			real_seconds = bin2bcd(real_seconds);
			real_minutes = bin2bcd(real_minutes);
		}
		CMOS_WRITE(real_seconds, RTC_SECONDS);
		CMOS_WRITE(real_minutes, RTC_MINUTES);
	} else {
		printk_once(KERN_NOTICE
		       "set_rtc_mmss: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
		retval = -1;
	}

	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	CMOS_WRITE(save_control, RTC_CONTROL);
	CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);
	spin_unlock(&rtc_lock);

	return retval;
}

int update_persistent_clock(struct timespec now)
{
	return set_rtc_mmss(now.tv_sec);
}

/*
 * calibrate the TSC clock against the RTC
 */
void __init calibrate_clock(void)
{
	unsigned char status;

	/* make sure the RTC is running and is set to operate in 24hr mode */
	status = RTSRC;
	RTCRB |= RTCRB_SET;
	RTCRB |= RTCRB_TM_24HR;
	RTCRB &= ~RTCRB_DM_BINARY;
	RTCRA |= RTCRA_DVR;
	RTCRA &= ~RTCRA_DVR;
	RTCRB &= ~RTCRB_SET;
}
