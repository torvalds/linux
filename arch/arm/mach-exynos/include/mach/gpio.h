/* linux/arch/arm/mach-exynos/include/mach/gpio.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - gpio map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_GPIO_H
#define __ASM_ARCH_GPIO_H __FILE__

#include "gpio-exynos4.h"
#include "gpio-exynos5.h"

extern void (*exynos4_sleep_gpio_table_set)(void);
extern void (*exynos5_sleep_gpio_table_set)(void);

#if defined(CONFIG_ARCH_EXYNOS4)
#define S3C_GPIO_END		EXYNOS4_GPIO_END
#define ARCH_NR_GPIOS		(EXYNOS4XXX_GPIO_END +	\
				CONFIG_SAMSUNG_GPIO_EXTRA)
#elif defined(CONFIG_ARCH_EXYNOS5)
#define S3C_GPIO_END		EXYNOS5_GPIO_END
#define ARCH_NR_GPIOS		(EXYNOS5_GPIO_END +	\
				CONFIG_SAMSUNG_GPIO_EXTRA)
#else
#error "ARCH_EXYNOS* is not defined"
#endif

#define GPIO_LEVEL_LOW		0
#define GPIO_LEVEL_HIGH		1
#define GPIO_LEVEL_NONE		2

#define GPIO_INPUT		0
#define GPIO_OUTPUT		1

#include <asm-generic/gpio.h>

#endif /* __ASM_ARCH_GPIO_H */
