/*
 * OMAP2/3 common powerdomain definitions
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Written by Paul Walmsley
 * Debugging and integration fixes by Jouni Högander
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * To Do List
 * -> Move the Sleep/Wakeup dependencies from Power Domain framework to
 *    Clock Domain Framework
 */

#ifndef ARCH_ARM_MACH_OMAP2_POWERDOMAINS
#define ARCH_ARM_MACH_OMAP2_POWERDOMAINS

/*
 * This file contains all of the powerdomains that have some element
 * of software control for the OMAP24xx and OMAP34xx chips.
 *
 * This is not an exhaustive listing of powerdomains on the chips; only
 * powerdomains that can be controlled in software.
 */

/*
 * The names for the DSP/IVA2 powerdomains are confusing.
 *
 * Most OMAP chips have an on-board DSP.
 *
 * On the 2420, this is a 'C55 DSP called, simply, the DSP.  Its
 * powerdomain is called the "DSP power domain."  On the 2430, the
 * on-board DSP is a 'C64 DSP, now called (along with its hardware
 * accelerators) the IVA2 or IVA2.1.  Its powerdomain is still called
 * the "DSP power domain." On the 3430, the DSP is a 'C64 DSP like the
 * 2430, also known as the IVA2; but its powerdomain is now called the
 * "IVA2 power domain."
 *
 * The 2420 also has something called the IVA, which is a separate ARM
 * core, and has nothing to do with the DSP/IVA2.
 *
 * Ideally the DSP/IVA2 could just be the same powerdomain, but the PRCM
 * address offset is different between the C55 and C64 DSPs.
 */

#include <plat/powerdomain.h>

#include "prcm-common.h"
#include "prm.h"
#include "cm.h"
#include "powerdomains24xx.h"
#include "powerdomains34xx.h"
#include "powerdomains44xx.h"

/* OMAP2/3-common powerdomains */

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)

/*
 * The GFX powerdomain is not present on 3430ES2, but currently we do not
 * have a macro to filter it out at compile-time.
 */
static struct powerdomain gfx_omap2_pwrdm = {
	.name		  = "gfx_pwrdm",
	.prcm_offs	  = GFX_MOD,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP24XX |
					   CHIP_IS_OMAP3430ES1),
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

static struct powerdomain wkup_omap2_pwrdm = {
	.name		= "wkup_pwrdm",
	.prcm_offs	= WKUP_MOD,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP24XX | CHIP_IS_OMAP3430),
};

#endif


/* As powerdomains are added or removed above, this list must also be changed */
static struct powerdomain *powerdomains_omap[] __initdata = {

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	&wkup_omap2_pwrdm,
	&gfx_omap2_pwrdm,
#endif

#ifdef CONFIG_ARCH_OMAP2
	&dsp_pwrdm,
	&mpu_24xx_pwrdm,
	&core_24xx_pwrdm,
#endif

#ifdef CONFIG_ARCH_OMAP2430
	&mdm_pwrdm,
#endif

#ifdef CONFIG_ARCH_OMAP3
	&iva2_pwrdm,
	&mpu_3xxx_pwrdm,
	&neon_pwrdm,
	&core_3xxx_pre_es3_1_pwrdm,
	&core_3xxx_es3_1_pwrdm,
	&cam_pwrdm,
	&dss_pwrdm,
	&per_pwrdm,
	&emu_pwrdm,
	&sgx_pwrdm,
	&usbhost_pwrdm,
	&dpll1_pwrdm,
	&dpll2_pwrdm,
	&dpll3_pwrdm,
	&dpll4_pwrdm,
	&dpll5_pwrdm,
#endif

#ifdef CONFIG_ARCH_OMAP4
	&core_44xx_pwrdm,
	&gfx_44xx_pwrdm,
	&abe_44xx_pwrdm,
	&dss_44xx_pwrdm,
	&tesla_44xx_pwrdm,
	&wkup_44xx_pwrdm,
	&cpu0_44xx_pwrdm,
	&cpu1_44xx_pwrdm,
	&emu_44xx_pwrdm,
	&mpu_44xx_pwrdm,
	&ivahd_44xx_pwrdm,
	&cam_44xx_pwrdm,
	&l3init_44xx_pwrdm,
	&l4per_44xx_pwrdm,
	&always_on_core_44xx_pwrdm,
	&cefuse_44xx_pwrdm,
#endif
	NULL
};


#endif
