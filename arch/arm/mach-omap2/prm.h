/*
 * OMAP2/3/4 Power/Reset Management (PRM) bitfield definitions
 *
 * Copyright (C) 2007-2009 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 *
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ARCH_ARM_MACH_OMAP2_PRM_H
#define __ARCH_ARM_MACH_OMAP2_PRM_H

#include "prcm-common.h"

/*
 * 24XX: PM_PWSTST_CORE, PM_PWSTST_GFX, PM_PWSTST_MPU, PM_PWSTST_DSP
 *
 * 2430: PM_PWSTST_MDM
 *
 * 3430: PM_PWSTST_IVA2, PM_PWSTST_MPU, PM_PWSTST_CORE, PM_PWSTST_GFX,
 *	 PM_PWSTST_DSS, PM_PWSTST_CAM, PM_PWSTST_PER, PM_PWSTST_EMU,
 *	 PM_PWSTST_NEON
 */
#define OMAP_INTRANSITION_MASK				(1 << 20)


/*
 * 24XX: PM_PWSTST_GFX, PM_PWSTST_DSP
 *
 * 2430: PM_PWSTST_MDM
 *
 * 3430: PM_PWSTST_IVA2, PM_PWSTST_MPU, PM_PWSTST_CORE, PM_PWSTST_GFX,
 *	 PM_PWSTST_DSS, PM_PWSTST_CAM, PM_PWSTST_PER, PM_PWSTST_EMU,
 *	 PM_PWSTST_NEON
 */
#define OMAP_POWERSTATEST_SHIFT				0
#define OMAP_POWERSTATEST_MASK				(0x3 << 0)

/*
 * 24XX: PM_PWSTCTRL_MPU, PM_PWSTCTRL_CORE, PM_PWSTCTRL_GFX,
 *       PM_PWSTCTRL_DSP, PM_PWSTST_MPU
 *
 * 2430: PM_PWSTCTRL_MDM shared bits
 *
 * 3430: PM_PWSTCTRL_IVA2, PM_PWSTCTRL_MPU, PM_PWSTCTRL_CORE,
 *	 PM_PWSTCTRL_GFX, PM_PWSTCTRL_DSS, PM_PWSTCTRL_CAM, PM_PWSTCTRL_PER,
 *	 PM_PWSTCTRL_NEON shared bits
 */
#define OMAP_POWERSTATE_SHIFT				0
#define OMAP_POWERSTATE_MASK				(0x3 << 0)

/*
 * Standardized OMAP reset source bits
 *
 * To the extent these happen to match the hardware register bit
 * shifts, it's purely coincidental.  Used by omap-wdt.c.
 * OMAP_UNKNOWN_RST_SRC_ID_SHIFT is a special value, used whenever
 * there are any bits remaining in the global PRM_RSTST register that
 * haven't been identified, or when the PRM code for the current SoC
 * doesn't know how to interpret the register.
 */
#define OMAP_GLOBAL_COLD_RST_SRC_ID_SHIFT			0
#define OMAP_GLOBAL_WARM_RST_SRC_ID_SHIFT			1
#define OMAP_SECU_VIOL_RST_SRC_ID_SHIFT				2
#define OMAP_MPU_WD_RST_SRC_ID_SHIFT				3
#define OMAP_SECU_WD_RST_SRC_ID_SHIFT				4
#define OMAP_EXTWARM_RST_SRC_ID_SHIFT				5
#define OMAP_VDD_MPU_VM_RST_SRC_ID_SHIFT			6
#define OMAP_VDD_IVA_VM_RST_SRC_ID_SHIFT			7
#define OMAP_VDD_CORE_VM_RST_SRC_ID_SHIFT			8
#define OMAP_ICEPICK_RST_SRC_ID_SHIFT				9
#define OMAP_ICECRUSHER_RST_SRC_ID_SHIFT			10
#define OMAP_C2C_RST_SRC_ID_SHIFT				11
#define OMAP_UNKNOWN_RST_SRC_ID_SHIFT				12

#ifndef __ASSEMBLER__

/**
 * struct prm_reset_src_map - map register bitshifts to standard bitshifts
 * @reg_shift: bitshift in the PRM reset source register
 * @std_shift: bitshift equivalent in the standard reset source list
 *
 * The fields are signed because -1 is used as a terminator.
 */
struct prm_reset_src_map {
	s8 reg_shift;
	s8 std_shift;
};

/**
 * struct prm_ll_data - fn ptrs to per-SoC PRM function implementations
 * @read_reset_sources: ptr to the Soc PRM-specific get_reset_source impl
 */
struct prm_ll_data {
	u32 (*read_reset_sources)(void);
};

extern int prm_register(struct prm_ll_data *pld);
extern int prm_unregister(struct prm_ll_data *pld);

extern u32 prm_read_reset_sources(void);

#endif


#endif
