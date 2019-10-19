// SPDX-License-Identifier: GPL-2.0
#include <linux/rtc.h>
#include <linux/time.h>

/**
 * rtc_set_ntp_time - Save NTP synchronized time to the RTC
 * @now: Current time of day
 * @target_nsec: pointer for desired now->tv_nsec value
 *
 * Replacement for the NTP platform function update_persistent_clock64
 * that stores time for later retrieval by rtc_hctosys.
 *
 * Returns 0 on successful RTC update, -ENODEV if a RTC update is not
 * possible at all, and various other -errno for specific temporary failure
 * cases.
 *
 * -EPROTO is returned if now.tv_nsec is not close enough to *target_nsec.
 *
 * If temporary failure is indicated the caller should try again 'soon'
 */
int rtc_set_ntp_time(struct timespec64 now, unsigned long *target_nsec)
{
	struct rtc_device *rtc;
	struct rtc_time tm;
	struct timespec64 to_set;
	int err = -ENODEV;
	bool ok;

	rtc = rtc_class_open(CONFIG_RTC_SYSTOHC_DEVICE);
	if (!rtc)
		goto out_err;

	if (!rtc->ops || !rtc->ops->set_time)
		goto out_close;

	/* Compute the value of tv_nsec we require the caller to supply in
	 * now.tv_nsec.  This is the value such that (now +
	 * set_offset_nsec).tv_nsec == 0.
	 */
	set_normalized_timespec64(&to_set, 0, -rtc->set_offset_nsec);
	*target_nsec = to_set.tv_nsec;

	/* The ntp code must call this with the correct value in tv_nsec, if
	 * it does not we update target_nsec and return EPROTO to make the ntp
	 * code try again later.
	 */
	ok = rtc_tv_nsec_ok(rtc->set_offset_nsec, &to_set, &now);
	if (!ok) {
		err = -EPROTO;
		goto out_close;
	}

	rtc_time64_to_tm(to_set.tv_sec, &tm);

	err = rtc_set_time(rtc, &tm);

out_close:
	rtc_class_close(rtc);
out_err:
	return err;
}
