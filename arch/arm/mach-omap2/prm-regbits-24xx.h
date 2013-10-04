#ifndef __ARCH_ARM_MACH_OMAP2_PRM_REGBITS_24XX_H
#define __ARCH_ARM_MACH_OMAP2_PRM_REGBITS_24XX_H

/*
 * OMAP24XX Power/Reset Management register bits
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Copyright (C) 2007 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "prm2xxx.h"

#define OMAP24XX_EN_CORE_SHIFT 				0
#define OMAP24XX_FORCESTATE_MASK			(1 << 18)
#define OMAP24XX_AUTOIDLE_MASK				(1 << 0)
#define OMAP24XX_AUTO_EXTVOLT_MASK			(1 << 15)
#define OMAP24XX_SETOFF_LEVEL_SHIFT			12
#define OMAP24XX_MEMRETCTRL_MASK			(1 << 8)
#define OMAP24XX_SETRET_LEVEL_SHIFT			6
#define OMAP24XX_VOLT_LEVEL_SHIFT			0
#define OMAP2420_CLKOUT2_EN_SHIFT			15
#define OMAP2420_CLKOUT2_DIV_SHIFT			11
#define OMAP2420_CLKOUT2_DIV_WIDTH			3
#define OMAP2420_CLKOUT2_SOURCE_MASK			(0x3 << 8)
#define OMAP24XX_CLKOUT_EN_SHIFT			7
#define OMAP24XX_CLKOUT_DIV_SHIFT			3
#define OMAP24XX_CLKOUT_DIV_WIDTH			3
#define OMAP24XX_CLKOUT_SOURCE_MASK			(0x3 << 0)
#define OMAP24XX_EMULATION_EN_SHIFT			0
#define OMAP2430_PM_WKDEP_MPU_EN_MDM_SHIFT		5
#define OMAP24XX_PM_WKDEP_MPU_EN_DSP_SHIFT		2
#define OMAP24XX_EXTWMPU_RST_SHIFT			6
#define OMAP24XX_SECU_WD_RST_SHIFT			5
#define OMAP24XX_MPU_WD_RST_SHIFT			4
#define OMAP24XX_SECU_VIOL_RST_SHIFT			3
#endif
