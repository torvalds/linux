/* linux/arch/arm/mach-exynos/include/mach/gpio-naples.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - MIDAS GPIO lib
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_GPIO_NAPLES_H
#define __ASM_ARCH_GPIO_NAPLES_H __FILE__

#if defined(CONFIG_GPIO_NAPLES_00_BD)
#include "gpio-rev00-naples.h"
#endif

extern int s3c_gpio_slp_cfgpin(unsigned int pin, unsigned int config);
extern int s3c_gpio_slp_setpull_updown(unsigned int pin, unsigned int config);

#endif /* __ASM_ARCH_GPIO_NAPLES_H */
