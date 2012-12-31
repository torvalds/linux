/* linux/arch/arm/mach-exynos/include/mach/regs-pmu-5250.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5 - 5250 Power management unit definition
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_PMU_5250_H
#define __ASM_ARCH_REGS_PMU_5250_H __FILE__

#define EXYNOS5_SATA_PHY_CONTROL				S5P_PMUREG(0x0724)

#define EXYNOS5_ABBG_INT_CONTROL				S5P_PMUREG(0x0780)
#define EXYNOS5_ABBG_ARM_CONTROL				S5P_PMUREG(0x0784)

#define EXYNOS5_USBOTG_MEM_SYS_PWR_REG				S5P_PMUREG(0x11C0)
#define EXYNOS5_USBDRD_MEM_SYS_PWR_REG				S5P_PMUREG(0x11CC)
#define EXYNOS5_SATA_MEM_SYS_PWR_REG				S5P_PMUREG(0x11FC)

#define EXYNOS5_PAD_RETENTION_EFNAND_SYS_PWR_REG		S5P_PMUREG(0x1208)

#define EXYNOS5_SATA_MEM_CONFIGURATION				S5P_PMUREG(0x2FC0)
#define EXYNOS5_SATA_MEM_STATUS					S5P_PMUREG(0x2FC4)
#define EXYNOS5_SATA_MEM_OPTION					S5P_PMUREG(0x2FC8)

#define S5P_MIPI_DPHY_CONTROL(n)		S5P_PMUREG(0x0710 + (n) * 4)
#define S5P_MIPI_DPHY_ENABLE			(1 << 0)
#define S5P_MIPI_DPHY_SRESETN			(1 << 1)
#define S5P_MIPI_DPHY_MRESETN			(1 << 2)

#define S5P_PMU_DEBUG						S5P_PMUREG(0x0A00)

#endif /* __ASM_ARCH_REGS_PMU_5250_H */
