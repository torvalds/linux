/*
 * OMAP2xxx Clock Management (CM) register definitions
 *
 * Copyright (C) 2007-2009, 2012 Texas Instruments, Inc.
 * Copyright (C) 2007-2010 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The CM hardware modules on the OMAP2/3 are quite similar to each
 * other.  The CM modules/instances on OMAP4 are quite different, so
 * they are handled in a separate file.
 */
#ifndef __ARCH_ASM_MACH_OMAP2_CM2XXX_H
#define __ARCH_ASM_MACH_OMAP2_CM2XXX_H

#include "prcm-common.h"
#include "cm2xxx_3xxx.h"

#define OMAP2420_CM_REGADDR(module, reg)				\
			OMAP2_L4_IO_ADDRESS(OMAP2420_CM_BASE + (module) + (reg))
#define OMAP2430_CM_REGADDR(module, reg)				\
			OMAP2_L4_IO_ADDRESS(OMAP2430_CM_BASE + (module) + (reg))

/*
 * Module specific CM register offsets from CM_BASE + domain offset
 * Use cm_{read,write}_mod_reg() with these registers.
 * These register offsets generally appear in more than one PRCM submodule.
 */

/* OMAP2-specific register offsets */

#define OMAP24XX_CM_FCLKEN2				0x0004
#define OMAP24XX_CM_ICLKEN4				0x001c
#define OMAP24XX_CM_AUTOIDLE4				0x003c
#define OMAP24XX_CM_IDLEST4				0x002c

/* CM_IDLEST bit field values to indicate deasserted IdleReq */

#define OMAP24XX_CM_IDLEST_VAL				0


/* Clock management domain register get/set */

#ifndef __ASSEMBLER__

extern void omap2xxx_cm_set_dpll_disable_autoidle(void);
extern void omap2xxx_cm_set_dpll_auto_low_power_stop(void);

extern void omap2xxx_cm_set_apll54_disable_autoidle(void);
extern void omap2xxx_cm_set_apll54_auto_low_power_stop(void);
extern void omap2xxx_cm_set_apll96_disable_autoidle(void);
extern void omap2xxx_cm_set_apll96_auto_low_power_stop(void);

int omap2xxx_cm_wait_module_ready(u8 part, s16 prcm_mod, u16 idlest_id,
				  u8 idlest_shift);
extern int omap2xxx_cm_fclks_active(void);
extern int omap2xxx_cm_mpu_retention_allowed(void);
extern u32 omap2xxx_cm_get_core_clk_src(void);
extern u32 omap2xxx_cm_get_core_pll_config(void);
extern u32 omap2xxx_cm_get_pll_config(void);
extern u32 omap2xxx_cm_get_pll_status(void);
extern void omap2xxx_cm_set_mod_dividers(u32 mpu, u32 dsp, u32 gfx, u32 core,
					 u32 mdm);

extern int __init omap2xxx_cm_init(void);

#endif

#endif
