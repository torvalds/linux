// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/ratelimit.h>
#include <asm/rtas.h>
#include <asm/time.h>


#define MAX_RTC_WAIT 5000	/* 5 sec */

time64_t __init rtas_get_boot_time(void)
{
	int ret[8];
	int error;
	unsigned int wait_time;
	u64 max_wait_tb;

	max_wait_tb = get_tb() + tb_ticks_per_usec * 1000 * MAX_RTC_WAIT;
	do {
		error = rtas_call(rtas_function_token(RTAS_FN_GET_TIME_OF_DAY), 0, 8, ret);

		wait_time = rtas_busy_delay_time(error);
		if (wait_time) {
			/* This is boot time so we spin. */
			udelay(wait_time*1000);
		}
	} while (wait_time && (get_tb() < max_wait_tb));

	if (error != 0) {
		printk_ratelimited(KERN_WARNING
				   "error: reading the clock failed (%d)\n",
				   error);
		return 0;
	}

	return mktime64(ret[0], ret[1], ret[2], ret[3], ret[4], ret[5]);
}

/* NOTE: get_rtc_time will get an error if executed in interrupt context
 * and if a delay is needed to read the clock.  In this case we just
 * silently return without updating rtc_tm.
 */
void rtas_get_rtc_time(struct rtc_time *rtc_tm)
{
        int ret[8];
	int error;
	unsigned int wait_time;
	u64 max_wait_tb;

	max_wait_tb = get_tb() + tb_ticks_per_usec * 1000 * MAX_RTC_WAIT;
	do {
		error = rtas_call(rtas_function_token(RTAS_FN_GET_TIME_OF_DAY), 0, 8, ret);

		wait_time = rtas_busy_delay_time(error);
		if (wait_time) {
			if (in_interrupt()) {
				memset(rtc_tm, 0, sizeof(struct rtc_time));
				printk_ratelimited(KERN_WARNING
						   "error: reading clock "
						   "would delay interrupt\n");
				return;	/* delay not allowed */
			}
			msleep(wait_time);
		}
	} while (wait_time && (get_tb() < max_wait_tb));

	if (error != 0) {
		printk_ratelimited(KERN_WARNING
				   "error: reading the clock failed (%d)\n",
				   error);
		return;
        }

	rtc_tm->tm_sec = ret[5];
	rtc_tm->tm_min = ret[4];
	rtc_tm->tm_hour = ret[3];
	rtc_tm->tm_mday = ret[2];
	rtc_tm->tm_mon = ret[1] - 1;
	rtc_tm->tm_year = ret[0] - 1900;
}

int rtas_set_rtc_time(struct rtc_time *tm)
{
	int error, wait_time;
	u64 max_wait_tb;

	max_wait_tb = get_tb() + tb_ticks_per_usec * 1000 * MAX_RTC_WAIT;
	do {
		error = rtas_call(rtas_function_token(RTAS_FN_SET_TIME_OF_DAY), 7, 1, NULL,
				  tm->tm_year + 1900, tm->tm_mon + 1,
				  tm->tm_mday, tm->tm_hour, tm->tm_min,
				  tm->tm_sec, 0);

		wait_time = rtas_busy_delay_time(error);
		if (wait_time) {
			if (in_interrupt())
				return 1;	/* probably decrementer */
			msleep(wait_time);
		}
	} while (wait_time && (get_tb() < max_wait_tb));

	if (error != 0)
		printk_ratelimited(KERN_WARNING
				   "error: setting the clock failed (%d)\n",
				   error);

        return 0;
}
