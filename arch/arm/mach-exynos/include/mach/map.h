/* linux/arch/arm/mach-exynos/include/mach/map.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H __FILE__

#include <plat/map-base.h>

/*
 * EXYNOS4 UART offset is 0x10000 but the older S5P SoCs are 0x400.
 * So need to define it, and here is to avoid redefinition warning.
 */
#define S3C_UART_OFFSET			(0x10000)

#include <plat/map-s5p.h>

#if defined(CONFIG_ARCH_EXYNOS4)
#include "map-exynos4.h"
#elif defined(CONFIG_ARCH_EXYNOS5)
#include "map-exynos5.h"
#else
#error "ARCH_EXYNOS* is not defined"
#endif

#endif /* __ASM_ARCH_MAP_H */
