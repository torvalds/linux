/* linux/arch/arm/mach-exynos/include/mach/regs-pmu-5210.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5 - 5210 Power management unit definition
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_PMU_5210_H
#define __ASM_ARCH_REGS_PMU_5210_H __FILE__

#define EXYNOS5_OneNANDXL_MEM_SYS_PWR_REG			S5P_PMUREG(0x11C0)
#define EXYNOS5_USBDEV_MEM_SYS_PWR_REG				S5P_PMUREG(0x11CC)

#define EXYNOS5_CMU_CLKSTOP_DISP0_SYS_PWR_REG			S5P_PMUREG(0x1490)
#define EXYNOS5_CMU_SYSCLK_DISP0_SYS_PWR_REG			S5P_PMUREG(0x14D0)
#define EXYNOS5_CMU_RESET_DISP0_SYS_PWR_REG			S5P_PMUREG(0x1590)

#define EXYNOS5_DISP0_CONFIGURATION				S5P_PMUREG(0x4080)
#define EXYNOS5_DISP0_STATUS					S5P_PMUREG(0x4084)
#define EXYNOS5_DISP0_OPTION					S5P_PMUREG(0x4088)

#define EXYNOS5_CMU_CLKSTOP_DISP0_CONFIGURATION			S5P_PMUREG(0x4480)
#define EXYNOS5_CMU_CLKSTOP_DISP0_STATUS			S5P_PMUREG(0x4484)
#define EXYNOS5_CMU_CLKSTOP_DISP0_OPTION			S5P_PMUREG(0x4488)

#define EXYNOS5_CMU_SYSCLK_DISP0_STATUS				S5P_PMUREG(0x4684)
#define EXYNOS5_CMU_SYSCLK_DISP0_OPTION				S5P_PMUREG(0x4688)

#endif /* __ASM_ARCH_REGS_PMU_5210_H */
