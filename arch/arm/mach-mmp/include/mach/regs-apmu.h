/*
 * linux/arch/arm/mach-mmp/include/mach/regs-apmu.h
 *
 *   Application Subsystem Power Management Unit
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_MACH_REGS_APMU_H
#define __ASM_MACH_REGS_APMU_H

#include <mach/addr-map.h>

#define APMU_FNCLK_EN	(1 << 4)
#define APMU_AXICLK_EN	(1 << 3)
#define APMU_FNRST_DIS	(1 << 1)
#define APMU_AXIRST_DIS	(1 << 0)

/* Wake Clear Register */
#define APMU_WAKE_CLR	APMU_REG(0x07c)

#define APMU_PXA168_KP_WAKE_CLR		(1 << 7)
#define APMU_PXA168_CFI_WAKE_CLR	(1 << 6)
#define APMU_PXA168_XD_WAKE_CLR		(1 << 5)
#define APMU_PXA168_MSP_WAKE_CLR	(1 << 4)
#define APMU_PXA168_SD4_WAKE_CLR	(1 << 3)
#define APMU_PXA168_SD3_WAKE_CLR	(1 << 2)
#define APMU_PXA168_SD2_WAKE_CLR	(1 << 1)
#define APMU_PXA168_SD1_WAKE_CLR	(1 << 0)

#endif /* __ASM_MACH_REGS_APMU_H */
