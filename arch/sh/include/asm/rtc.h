/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RTC_H
#define _ASM_RTC_H

#define RTC_CAP_4_DIGIT_YEAR	(1 << 0)

struct sh_rtc_platform_info {
	unsigned long capabilities;
};

#include <cpu/rtc.h>

#endif /* _ASM_RTC_H */
