/* linux/arch/arm/mach-s5pv310/include/mach/regs-sysmmu.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5PV310 - System MMU register
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_SYSMMU_H
#define __ASM_ARCH_REGS_SYSMMU_H __FILE__

#define S5P_MMU_CTRL			0x000
#define S5P_MMU_CFG			0x004
#define S5P_MMU_STATUS			0x008
#define S5P_MMU_FLUSH			0x00C
#define S5P_PT_BASE_ADDR		0x014
#define S5P_INT_STATUS			0x018
#define S5P_PAGE_FAULT_ADDR		0x024

#endif /* __ASM_ARCH_REGS_SYSMMU_H */
