/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Machine dependent access functions for RTC registers.
 */
#ifndef __ASM_MC146818_TIME_H
#define __ASM_MC146818_TIME_H

#include <linux/bcd.h>
#include <linux/mc146818rtc.h>
#include <linux/time.h>

static inline time64_t mc146818_get_cmos_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);

	do {
		sec = CMOS_READ(RTC_SECONDS);
		min = CMOS_READ(RTC_MINUTES);
		hour = CMOS_READ(RTC_HOURS);
		day = CMOS_READ(RTC_DAY_OF_MONTH);
		mon = CMOS_READ(RTC_MONTH);
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
	spin_unlock_irqrestore(&rtc_lock, flags);
	if (year < 70)
		year += 2000;
	else
		year += 1900;

	return mktime64(year, mon, day, hour, min, sec);
}

#endif /* __ASM_MC146818_TIME_H */
