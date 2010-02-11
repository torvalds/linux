/*
 * OMAP34XX powerdomain definitions
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Copyright (C) 2007-2008 Nokia Corporation
 *
 * Written by Paul Walmsley
 * Debugging and integration fixes by Jouni Högander
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARCH_ARM_MACH_OMAP2_POWERDOMAINS34XX
#define ARCH_ARM_MACH_OMAP2_POWERDOMAINS34XX

/*
 * N.B. If powerdomains are added or removed from this file, update
 * the array in mach-omap2/powerdomains.h.
 */

#include <plat/powerdomain.h>

#include "prcm-common.h"
#include "prm.h"
#include "prm-regbits-34xx.h"
#include "cm.h"
#include "cm-regbits-34xx.h"

/*
 * 34XX-specific powerdomains, dependencies
 */

#ifdef CONFIG_ARCH_OMAP34XX

/*
 * 3430: PM_WKDEP_{PER,USBHOST}: CORE, IVA2, MPU, WKUP
 * (USBHOST is ES2 only)
 */
static struct pwrdm_dep per_usbhost_wkdeps[] = {
	{
		.pwrdm_name = "core_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "iva2_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "mpu_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "wkup_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{ NULL },
};

/*
 * 3430 PM_WKDEP_MPU: CORE, IVA2, DSS, PER
 */
static struct pwrdm_dep mpu_34xx_wkdeps[] = {
	{
		.pwrdm_name = "core_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "iva2_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "dss_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "per_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{ NULL },
};

/*
 * 3430 PM_WKDEP_IVA2: CORE, MPU, WKUP, DSS, PER
 */
static struct pwrdm_dep iva2_wkdeps[] = {
	{
		.pwrdm_name = "core_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "mpu_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "wkup_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "dss_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "per_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{ NULL },
};


/* 3430 PM_WKDEP_{CAM,DSS}: IVA2, MPU, WKUP */
static struct pwrdm_dep cam_dss_wkdeps[] = {
	{
		.pwrdm_name = "iva2_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "mpu_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "wkup_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{ NULL },
};

/* 3430: PM_WKDEP_NEON: MPU */
static struct pwrdm_dep neon_wkdeps[] = {
	{
		.pwrdm_name = "mpu_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{ NULL },
};


/* Sleep dependency source arrays for 34xx-specific pwrdms - 34XX only */

/*
 * 3430: CM_SLEEPDEP_{DSS,PER}: MPU, IVA
 * 3430ES2: CM_SLEEPDEP_USBHOST: MPU, IVA
 */
static struct pwrdm_dep dss_per_usbhost_sleepdeps[] = {
	{
		.pwrdm_name = "mpu_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{
		.pwrdm_name = "iva2_pwrdm",
		.omap_chip = OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
	},
	{ NULL },
};


/*
 * Powerdomains
 */

static struct powerdomain iva2_pwrdm = {
	.name		  = "iva2_pwrdm",
	.prcm_offs	  = OMAP3430_IVA2_MOD,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.dep_bit	  = OMAP3430_PM_WKDEP_MPU_EN_IVA2_SHIFT,
	.wkdep_srcs	  = iva2_wkdeps,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	.banks		  = 4,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,
		[1] = PWRSTS_OFF_RET,
		[2] = PWRSTS_OFF_RET,
		[3] = PWRSTS_OFF_RET,
	},
	.pwrsts_mem_on	  = {
		[0] = PWRDM_POWER_ON,
		[1] = PWRDM_POWER_ON,
		[2] = PWRSTS_OFF_ON,
		[3] = PWRDM_POWER_ON,
	},
};

static struct powerdomain mpu_34xx_pwrdm = {
	.name		  = "mpu_pwrdm",
	.prcm_offs	  = MPU_MOD,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.dep_bit	  = OMAP3430_EN_MPU_SHIFT,
	.wkdep_srcs	  = mpu_34xx_wkdeps,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	.flags		  = PWRDM_HAS_MPU_QUIRK,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_OFF_ON,
	},
};

/* No wkdeps or sleepdeps for 34xx core apparently */
static struct powerdomain core_34xx_pre_es3_1_pwrdm = {
	.name		  = "core_pwrdm",
	.prcm_offs	  = CORE_MOD,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP3430ES1 |
					   CHIP_IS_OMAP3430ES2 |
					   CHIP_IS_OMAP3430ES3_0),
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.dep_bit	  = OMAP3430_EN_CORE_SHIFT,
	.banks		  = 2,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,	 /* MEM1RETSTATE */
		[1] = PWRSTS_OFF_RET,	 /* MEM2RETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_OFF_RET_ON, /* MEM1ONSTATE */
		[1] = PWRSTS_OFF_RET_ON, /* MEM2ONSTATE */
	},
};

/* No wkdeps or sleepdeps for 34xx core apparently */
static struct powerdomain core_34xx_es3_1_pwrdm = {
	.name		  = "core_pwrdm",
	.prcm_offs	  = CORE_MOD,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_GE_OMAP3430ES3_1),
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.dep_bit	  = OMAP3430_EN_CORE_SHIFT,
	.flags		  = PWRDM_HAS_HDWR_SAR, /* for USBTLL only */
	.banks		  = 2,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,	 /* MEM1RETSTATE */
		[1] = PWRSTS_OFF_RET,	 /* MEM2RETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_OFF_RET_ON, /* MEM1ONSTATE */
		[1] = PWRSTS_OFF_RET_ON, /* MEM2ONSTATE */
	},
};

/* Another case of bit name collisions between several registers: EN_DSS */
static struct powerdomain dss_pwrdm = {
	.name		  = "dss_pwrdm",
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.prcm_offs	  = OMAP3430_DSS_MOD,
	.dep_bit	  = OMAP3430_PM_WKDEP_MPU_EN_DSS_SHIFT,
	.wkdep_srcs	  = cam_dss_wkdeps,
	.sleepdep_srcs	  = dss_per_usbhost_sleepdeps,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRDM_POWER_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRDM_POWER_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRDM_POWER_ON,  /* MEMONSTATE */
	},
};

/*
 * Although the 34XX TRM Rev K Table 4-371 notes that retention is a
 * possible SGX powerstate, the SGX device itself does not support
 * retention.
 */
static struct powerdomain sgx_pwrdm = {
	.name		  = "sgx_pwrdm",
	.prcm_offs	  = OMAP3430ES2_SGX_MOD,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_GE_OMAP3430ES2),
	.wkdep_srcs	  = gfx_sgx_wkdeps,
	.sleepdep_srcs	  = cam_gfx_sleepdeps,
	/* XXX This is accurate for 3430 SGX, but what about GFX? */
	.pwrsts		  = PWRSTS_OFF_ON,
	.pwrsts_logic_ret = PWRDM_POWER_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRDM_POWER_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRDM_POWER_ON,  /* MEMONSTATE */
	},
};

static struct powerdomain cam_pwrdm = {
	.name		  = "cam_pwrdm",
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.prcm_offs	  = OMAP3430_CAM_MOD,
	.wkdep_srcs	  = cam_dss_wkdeps,
	.sleepdep_srcs	  = cam_gfx_sleepdeps,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRDM_POWER_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRDM_POWER_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRDM_POWER_ON,  /* MEMONSTATE */
	},
};

static struct powerdomain per_pwrdm = {
	.name		  = "per_pwrdm",
	.prcm_offs	  = OMAP3430_PER_MOD,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.dep_bit	  = OMAP3430_EN_PER_SHIFT,
	.wkdep_srcs	  = per_usbhost_wkdeps,
	.sleepdep_srcs	  = dss_per_usbhost_sleepdeps,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRDM_POWER_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRDM_POWER_ON,  /* MEMONSTATE */
	},
};

static struct powerdomain emu_pwrdm = {
	.name		= "emu_pwrdm",
	.prcm_offs	= OMAP3430_EMU_MOD,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct powerdomain neon_pwrdm = {
	.name		  = "neon_pwrdm",
	.prcm_offs	  = OMAP3430_NEON_MOD,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.wkdep_srcs	  = neon_wkdeps,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRDM_POWER_RET,
};

static struct powerdomain usbhost_pwrdm = {
	.name		  = "usbhost_pwrdm",
	.prcm_offs	  = OMAP3430ES2_USBHOST_MOD,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_GE_OMAP3430ES2),
	.wkdep_srcs	  = per_usbhost_wkdeps,
	.sleepdep_srcs	  = dss_per_usbhost_sleepdeps,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRDM_POWER_RET,
	/*
	 * REVISIT: Enabling usb host save and restore mechanism seems to
	 * leave the usb host domain permanently in ACTIVE mode after
	 * changing the usb host power domain state from OFF to active once.
	 * Disabling for now.
	 */
	/*.flags	  = PWRDM_HAS_HDWR_SAR,*/ /* for USBHOST ctrlr only */
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRDM_POWER_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRDM_POWER_ON,  /* MEMONSTATE */
	},
};

static struct powerdomain dpll1_pwrdm = {
	.name		= "dpll1_pwrdm",
	.prcm_offs	= MPU_MOD,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct powerdomain dpll2_pwrdm = {
	.name		= "dpll2_pwrdm",
	.prcm_offs	= OMAP3430_IVA2_MOD,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct powerdomain dpll3_pwrdm = {
	.name		= "dpll3_pwrdm",
	.prcm_offs	= PLL_MOD,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct powerdomain dpll4_pwrdm = {
	.name		= "dpll4_pwrdm",
	.prcm_offs	= PLL_MOD,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct powerdomain dpll5_pwrdm = {
	.name		= "dpll5_pwrdm",
	.prcm_offs	= PLL_MOD,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_GE_OMAP3430ES2),
};


#endif    /* CONFIG_ARCH_OMAP34XX */


#endif
