/* linux/arch/arm/mach-exynos/include/mach/regs-pmu-4210.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - 4210 Power management unit definition
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_PMU_4210_H
#define __ASM_ARCH_REGS_PMU_4210_H __FILE__

#define S5P_USBOTG_PHY_CONTROL		S5P_PMUREG(0x0704)
#define S5P_USBHOST_PHY_CONTROL		S5P_PMUREG(0x0708)
#define S5P_USBOTG_PHY_ENABLE		(1 << 0)
#define S5P_USBHOST_PHY_ENABLE		(1 << 0)

#define S5P_PMU_SATA_PHY_CONTROL	S5P_PMUREG(0x0720)

#define S5P_CMU_CLKSTOP_LCD1_SYS	S5P_PMUREG(0x1154)
#define S5P_CMU_RESET_LCD1_SYS		S5P_PMUREG(0x1174)
#define S5P_MODIMIF_MEM_SYS		S5P_PMUREG(0x11C4)
#define S5P_PCIE_MEM_SYS		S5P_PMUREG(0x11E0)
#define S5P_SATA_MEM_SYS		S5P_PMUREG(0x11E4)
#define S5P_LCD1_SYS			S5P_PMUREG(0x1394)

#define S5P_PMU_LCD1_CONF		S5P_PMUREG(0x3CA0)

#endif /* __ASM_ARCH_REGS_PMU_4210_H */
