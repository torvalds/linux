/* linux/arch/arm/plat-samsung/include/plat/rtc-core.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Samsung RTC Device core function
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the term of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef __ASM_PLAT_RTC_CORE_H
#define __ASM_PLAT_RTC_CORE_H __FILE__

/* These function are only for use with the core support code, such as
 * the cpu specific initialization code
 */

/* re-define device name depending on support. */
static inline void s3c_rtc_setname(char *name)
{
#ifdef CONFIG_S3C_DEV_RTC
	s3c_device_rtc.name = name;
#endif
}

#endif /* __ASM_PLAT_RTC_CORE_H */
