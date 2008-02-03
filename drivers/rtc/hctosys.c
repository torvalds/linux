/*
 * RTC subsystem, initialize system time on startup
 *
 * Copyright (C) 2005 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/rtc.h>

/* IMPORTANT: the RTC only stores whole seconds. It is arbitrary
 * whether it stores the most close value or the value with partial
 * seconds truncated. However, it is important that we use it to store
 * the truncated value. This is because otherwise it is necessary,
 * in an rtc sync function, to read both xtime.tv_sec and
 * xtime.tv_nsec. On some processors (i.e. ARM), an atomic read
 * of >32bits is not possible. So storing the most close value would
 * slow down the sync API. So here we have the truncated value and
 * the best guess is to add 0.5s.
 */

static int __init rtc_hctosys(void)
{
	int err;
	struct rtc_time tm;
	struct rtc_device *rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);

	if (rtc == NULL) {
		printk("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -ENODEV;
	}

	err = rtc_read_time(rtc, &tm);
	if (err == 0) {
		err = rtc_valid_tm(&tm);
		if (err == 0) {
			struct timespec tv;

			tv.tv_nsec = NSEC_PER_SEC >> 1;

			rtc_tm_to_time(&tm, &tv.tv_sec);

			do_settimeofday(&tv);

			dev_info(rtc->dev.parent,
				"setting system clock to "
				"%d-%02d-%02d %02d:%02d:%02d UTC (%u)\n",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec,
				(unsigned int) tv.tv_sec);
		}
		else
			dev_err(rtc->dev.parent,
				"hctosys: invalid date/time\n");
	}
	else
		dev_err(rtc->dev.parent,
			"hctosys: unable to read the hardware clock\n");

	rtc_class_close(rtc);

	return 0;
}

late_initcall(rtc_hctosys);
