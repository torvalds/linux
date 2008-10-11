/*
 *  arch/arm/mach-footbridge/include/mach/time.h
 *
 *  Copyright (C) 1998 Russell King.
 *  Copyright (C) 1998 Phil Blundell
 *
 * CATS has a real-time clock, though the evaluation board doesn't.
 *
 * Changelog:
 *  21-Mar-1998	RMK	Created
 *  27-Aug-1998	PJB	CATS support
 *  28-Dec-1998	APH	Made leds optional
 *  20-Jan-1999	RMK	Started merge of EBSA285, CATS and NetWinder
 *  16-Mar-1999	RMK	More support for EBSA285-like machines with RTCs in
 */

#define RTC_PORT(x)		(rtc_base+(x))
#define RTC_ALWAYS_BCD		0

#include <linux/timex.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mc146818rtc.h>
#include <linux/bcd.h>
#include <linux/io.h>

#include <mach/hardware.h>

#include <asm/mach/time.h>
#include "common.h"

static int rtc_base;

static unsigned long __init get_isa_cmos_time(void)
{
	unsigned int year, mon, day, hour, min, sec;

	// check to see if the RTC makes sense.....
	if ((CMOS_READ(RTC_VALID) & RTC_VRT) == 0)
		return mktime(1970, 1, 1, 0, 0, 0);

	do {
		sec  = CMOS_READ(RTC_SECONDS);
		min  = CMOS_READ(RTC_MINUTES);
		hour = CMOS_READ(RTC_HOURS);
		day  = CMOS_READ(RTC_DAY_OF_MONTH);
		mon  = CMOS_READ(RTC_MONTH);
		year = CMOS_READ(RTC_YEAR);
	} while (sec != CMOS_READ(RTC_SECONDS));

	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		sec = bcd2bin(sec);
		min = bcd2bin(min);
		hour = bcd2bin(hour);
		day = bcd2bin(day);
		mon = bcd2bin(mon);
		year = bcd2bin(year);
	}
	if ((year += 1900) < 1970)
		year += 100;
	return mktime(year, mon, day, hour, min, sec);
}

static int set_isa_cmos_time(void)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char save_control, save_freq_select;
	unsigned long nowtime = xtime.tv_sec;

	save_control = CMOS_READ(RTC_CONTROL); /* tell the clock it's being set */
	CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);

	save_freq_select = CMOS_READ(RTC_FREQ_SELECT); /* stop and reset prescaler */
	CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

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
	if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
		real_minutes += 30;		/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
			real_seconds = bin2bcd(real_seconds);
			real_minutes = bin2bcd(real_minutes);
		}
		CMOS_WRITE(real_seconds,RTC_SECONDS);
		CMOS_WRITE(real_minutes,RTC_MINUTES);
	} else
		retval = -1;

	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	CMOS_WRITE(save_control, RTC_CONTROL);
	CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);

	return retval;
}

void __init isa_rtc_init(void)
{
	if (machine_is_personal_server())
		/*
		 * Add-in 21285s shouldn't access the RTC
		 */
		rtc_base = 0;
	else
		rtc_base = 0x70;

	if (rtc_base) {
		int reg_d, reg_b;

		/*
		 * Probe for the RTC.
		 */
		reg_d = CMOS_READ(RTC_REG_D);

		/*
		 * make sure the divider is set
		 */
		CMOS_WRITE(RTC_REF_CLCK_32KHZ, RTC_REG_A);

		/*
		 * Set control reg B
		 *   (24 hour mode, update enabled)
		 */
		reg_b = CMOS_READ(RTC_REG_B) & 0x7f;
		reg_b |= 2;
		CMOS_WRITE(reg_b, RTC_REG_B);

		if ((CMOS_READ(RTC_REG_A) & 0x7f) == RTC_REF_CLCK_32KHZ &&
		    CMOS_READ(RTC_REG_B) == reg_b) {
			struct timespec tv;

			/*
			 * We have a RTC.  Check the battery
			 */
			if ((reg_d & 0x80) == 0)
				printk(KERN_WARNING "RTC: *** warning: CMOS battery bad\n");

			tv.tv_nsec = 0;
			tv.tv_sec = get_isa_cmos_time();
			do_settimeofday(&tv);
			set_rtc = set_isa_cmos_time;
		} else
			rtc_base = 0;
	}
}
