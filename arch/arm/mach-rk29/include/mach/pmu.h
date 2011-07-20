/* arch/arm/mach-rk29/include/mach/pmu.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARCH_ARM_MACH_RK29_PMU_H
#define __ARCH_ARM_MACH_RK29_PMU_H

#include <linux/irqflags.h>
#include <asm/io.h>
#include <mach/rk29_iomap.h>

#define PMU_WAKEUP_EN0	0x00
#define PMU_WAKEUP_EN1	0x04
#define PMU_WAKEUP_EN2	0x08
#define PMU_PD_CON	0x10
#define PMU_MISC_CON	0x14
#define PMU_PLL_CNT	0x18
#define PMU_PD_ST	0x1c
#define PMU_INT_ST	0x20

enum pmu_power_domain {
	PD_ARM_CORE = 0,
	PD_NEON,
	PD_ETM_DBG,
	PD_L2_CACHE,
	PD_VCODEC,
	PD_DISPLAY,
	PD_GPU,
	PD_PERIPHERAL,
	PD_MINI,
};

static inline bool pmu_power_domain_is_on(enum pmu_power_domain pd)
{
	return !(readl(RK29_PMU_BASE + PMU_PD_ST) & (1 << pd));
}

void pmu_set_power_domain(enum pmu_power_domain pd, bool on);

#endif
