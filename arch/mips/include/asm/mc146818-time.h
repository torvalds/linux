/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Machine dependent access functions for RTC registers.
 */
#ifndef __ASM_MC146818_TIME_H
#define __ASM_MC146818_TIME_H

#include <linux/mc146818rtc.h>
#include <linux/time.h>

#ifdef CONFIG_RTC_MC146818_LIB
static inline time64_t mc146818_get_cmos_time(void)
{
	struct rtc_time tm;

	if (mc146818_get_time(&tm, 1000)) {
		pr_err("Unable to read current time from RTC\n");
		return 0;
	}

	return rtc_tm_to_time64(&tm);
}
#endif /* CONFIG_RTC_MC146818_LIB */

#endif /* __ASM_MC146818_TIME_H */
