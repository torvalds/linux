/* linux/arch/arm/plat-samsung/include/plat/rtc-core.h
 *
 * Copyright (c) 2011 Heiko Stuebner <heiko@sntech.de>
 *
 * Samsung RTC Controller core functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_RTC_CORE_H
#define __ASM_PLAT_RTC_CORE_H __FILE__

/* These functions are only for use with the core support code, such as
 * the cpu specific initialisation code
 */

/* re-define device name depending on support. */
static inline void s3c_rtc_setname(char *name)
{
#if defined(CONFIG_SAMSUNG_DEV_RTC) || defined(CONFIG_PLAT_S3C24XX)
	s3c_device_rtc.name = name;
#endif
}

#endif /* __ASM_PLAT_RTC_CORE_H */
