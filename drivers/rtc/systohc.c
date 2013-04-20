/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */
#include <linux/rtc.h>
#include <linux/time.h>

/**
 * rtc_set_ntp_time - Save NTP synchronized time to the RTC
 * @now: Current time of day
 *
 * Replacement for the NTP platform function update_persistent_clock
 * that stores time for later retrieval by rtc_hctosys.
 *
 * Returns 0 on successful RTC update, -ENODEV if a RTC update is not
 * possible at all, and various other -errno for specific temporary failure
 * cases.
 *
 * If temporary failure is indicated the caller should try again 'soon'
 */
int rtc_set_ntp_time(struct timespec now)
{
	struct rtc_device *rtc;
	struct rtc_time tm;
	int err = -ENODEV;

	if (now.tv_nsec < (NSEC_PER_SEC >> 1))
		rtc_time_to_tm(now.tv_sec, &tm);
	else
		rtc_time_to_tm(now.tv_sec + 1, &tm);

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc) {
		/* rtc_hctosys exclusively uses UTC, so we call set_time here,
		 * not set_mmss. */
		if (rtc->ops && (rtc->ops->set_time || rtc->ops->set_mmss))
			err = rtc_set_time(rtc, &tm);
		rtc_class_close(rtc);
	}

	return err;
}
