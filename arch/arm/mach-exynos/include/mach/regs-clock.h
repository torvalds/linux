/* linux/arch/arm/mach-exynos4/include/mach/regs-clock.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - Clock register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_CLOCK_H
#define __ASM_ARCH_REGS_CLOCK_H __FILE__

#include <plat/cpu.h>
#include <mach/map.h>

#define EXYNOS_CLKREG(x)			(S5P_VA_CMU + (x))

#define EXYNOS5_PWR_CTRL1			EXYNOS_CLKREG(0x01020)
#define EXYNOS5_PWR_CTRL2			EXYNOS_CLKREG(0x01024)

#define PWR_CTRL1_CORE2_DOWN_RATIO		(7 << 28)
#define PWR_CTRL1_CORE1_DOWN_RATIO		(7 << 16)
#define PWR_CTRL1_DIV2_DOWN_EN			(1 << 9)
#define PWR_CTRL1_DIV1_DOWN_EN			(1 << 8)
#define PWR_CTRL1_USE_CORE1_WFE			(1 << 5)
#define PWR_CTRL1_USE_CORE0_WFE			(1 << 4)
#define PWR_CTRL1_USE_CORE1_WFI			(1 << 1)
#define PWR_CTRL1_USE_CORE0_WFI			(1 << 0)

#define PWR_CTRL2_DIV2_UP_EN			(1 << 25)
#define PWR_CTRL2_DIV1_UP_EN			(1 << 24)
#define PWR_CTRL2_DUR_STANDBY2_VAL		(1 << 16)
#define PWR_CTRL2_DUR_STANDBY1_VAL		(1 << 8)
#define PWR_CTRL2_CORE2_UP_RATIO		(1 << 4)
#define PWR_CTRL2_CORE1_UP_RATIO		(1 << 0)

#endif /* __ASM_ARCH_REGS_CLOCK_H */
