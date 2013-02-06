/* linux/arch/arm/mach-exynos/include/mach/gpio-midas.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - MIDAS GPIO lib
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_GPIO_P10_H
#define __ASM_ARCH_GPIO_P10_H __FILE__

#if defined(CONFIG_MACH_P10_00_BD)
#include "gpio-rev00-p10.h"
#elif defined(CONFIG_MACH_P10_LTE_00_BD)
#include "gpio-rev00-p10-lte.h"
#elif defined(CONFIG_MACH_P10_WIFI_00_BD)
#include "gpio-rev00-p10-wifi.h"
#elif defined(CONFIG_MACH_P10_LUNGO_01_BD)
#include "gpio-rev01-p10-lungo.h"
#elif defined(CONFIG_MACH_P10_LUNGO_WIFI_01_BD)
#include "gpio-rev01-p10-lungo-wifi.h"
#endif

#endif /* __ASM_ARCH_GPIO_P10_H */
