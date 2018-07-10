/*
 * Common defines/structures/etc... for applets that need to work with the RTC.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

#ifndef BB_RTC_H
#define BB_RTC_H 1

#include "libbb.h"

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

int rtc_adjtime_is_utc(void) FAST_FUNC;
int rtc_xopen(const char **default_rtc, int flags) FAST_FUNC;
void rtc_read_tm(struct tm *ptm, int fd) FAST_FUNC;
time_t rtc_tm2time(struct tm *ptm, int utc) FAST_FUNC;


/*
 * Everything below this point has been copied from linux/rtc.h
 * to eliminate the kernel header dependency
 */

struct linux_rtc_time {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

struct linux_rtc_wkalrm {
	unsigned char enabled;  /* 0 = alarm disabled, 1 = alarm enabled */
	unsigned char pending;  /* 0 = alarm not pending, 1 = alarm pending */
	struct linux_rtc_time time;  /* time the alarm is set to */
};

/*
 * ioctl calls that are permitted to the /dev/rtc interface, if
 * any of the RTC drivers are enabled.
 */
#define RTC_AIE_ON      _IO('p', 0x01)  /* Alarm int. enable on         */
#define RTC_AIE_OFF     _IO('p', 0x02)  /* ... off                      */
#define RTC_UIE_ON      _IO('p', 0x03)  /* Update int. enable on        */
#define RTC_UIE_OFF     _IO('p', 0x04)  /* ... off                      */
#define RTC_PIE_ON      _IO('p', 0x05)  /* Periodic int. enable on      */
#define RTC_PIE_OFF     _IO('p', 0x06)  /* ... off                      */
#define RTC_WIE_ON      _IO('p', 0x0f)  /* Watchdog int. enable on      */
#define RTC_WIE_OFF     _IO('p', 0x10)  /* ... off                      */

#define RTC_ALM_SET     _IOW('p', 0x07, struct linux_rtc_time) /* Set alarm time  */
#define RTC_ALM_READ    _IOR('p', 0x08, struct linux_rtc_time) /* Read alarm time */
#define RTC_RD_TIME     _IOR('p', 0x09, struct linux_rtc_time) /* Read RTC time   */
#define RTC_SET_TIME    _IOW('p', 0x0a, struct linux_rtc_time) /* Set RTC time    */
#define RTC_IRQP_READ   _IOR('p', 0x0b, unsigned long)   /* Read IRQ rate   */
#define RTC_IRQP_SET    _IOW('p', 0x0c, unsigned long)   /* Set IRQ rate    */
#define RTC_EPOCH_READ  _IOR('p', 0x0d, unsigned long)   /* Read epoch      */
#define RTC_EPOCH_SET   _IOW('p', 0x0e, unsigned long)   /* Set epoch       */

#define RTC_WKALM_SET   _IOW('p', 0x0f, struct linux_rtc_wkalrm)/* Set wakeup alarm*/
#define RTC_WKALM_RD    _IOR('p', 0x10, struct linux_rtc_wkalrm)/* Get wakeup alarm*/

/* interrupt flags */
#define RTC_IRQF 0x80 /* any of the following is active */
#define RTC_PF 0x40
#define RTC_AF 0x20
#define RTC_UF 0x10

POP_SAVED_FUNCTION_VISIBILITY

#endif
