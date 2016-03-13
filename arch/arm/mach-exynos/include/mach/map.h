/*
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

#include <plat/map-s5p.h>

#define EXYNOS_PA_CHIPID		0x10000000

#define EXYNOS4_PA_CMU			0x10030000

#define EXYNOS4_PA_DMC0			0x10400000
#define EXYNOS4_PA_DMC1			0x10410000

#define EXYNOS4_PA_COREPERI		0x10500000

#define EXYNOS4_PA_SROMC		0x12570000
#define EXYNOS5_PA_SROMC		0x12250000

#endif /* __ASM_ARCH_MAP_H */
