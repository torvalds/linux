/*
 * OMAP2/3 Clock Management (CM) register definitions
 *
 * Copyright (C) 2007-2009 Texas Instruments, Inc.
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
#ifndef __ARCH_ASM_MACH_OMAP2_CM2XXX_3XXX_H
#define __ARCH_ASM_MACH_OMAP2_CM2XXX_3XXX_H

#include "prcm-common.h"

#define OMAP2420_CM_REGADDR(module, reg)				\
			OMAP2_L4_IO_ADDRESS(OMAP2420_CM_BASE + (module) + (reg))
#define OMAP2430_CM_REGADDR(module, reg)				\
			OMAP2_L4_IO_ADDRESS(OMAP2430_CM_BASE + (module) + (reg))
#define OMAP34XX_CM_REGADDR(module, reg)				\
			OMAP2_L4_IO_ADDRESS(OMAP3430_CM_BASE + (module) + (reg))


/*
 * OMAP3-specific global CM registers
 * Use cm_{read,write}_reg() with these registers.
 * These registers appear once per CM module.
 */

#define OMAP3430_CM_REVISION		OMAP34XX_CM_REGADDR(OCP_MOD, 0x0000)
#define OMAP3430_CM_SYSCONFIG		OMAP34XX_CM_REGADDR(OCP_MOD, 0x0010)
#define OMAP3430_CM_POLCTRL		OMAP34XX_CM_REGADDR(OCP_MOD, 0x009c)

#define OMAP3_CM_CLKOUT_CTRL_OFFSET	0x0070
#define OMAP3430_CM_CLKOUT_CTRL		OMAP_CM_REGADDR(OMAP3430_CCR_MOD, 0x0070)

/*
 * Module specific CM register offsets from CM_BASE + domain offset
 * Use cm_{read,write}_mod_reg() with these registers.
 * These register offsets generally appear in more than one PRCM submodule.
 */

/* Common between OMAP2 and OMAP3 */

#define CM_FCLKEN					0x0000
#define CM_FCLKEN1					CM_FCLKEN
#define CM_CLKEN					CM_FCLKEN
#define CM_ICLKEN					0x0010
#define CM_ICLKEN1					CM_ICLKEN
#define CM_ICLKEN2					0x0014
#define CM_ICLKEN3					0x0018
#define CM_IDLEST					0x0020
#define CM_IDLEST1					CM_IDLEST
#define CM_IDLEST2					0x0024
#define CM_AUTOIDLE					0x0030
#define CM_AUTOIDLE1					CM_AUTOIDLE
#define CM_AUTOIDLE2					0x0034
#define CM_AUTOIDLE3					0x0038
#define CM_CLKSEL					0x0040
#define CM_CLKSEL1					CM_CLKSEL
#define CM_CLKSEL2					0x0044
#define OMAP2_CM_CLKSTCTRL				0x0048

/* OMAP2-specific register offsets */

#define OMAP24XX_CM_FCLKEN2				0x0004
#define OMAP24XX_CM_ICLKEN4				0x001c
#define OMAP24XX_CM_AUTOIDLE4				0x003c
#define OMAP24XX_CM_IDLEST4				0x002c

#define OMAP2430_CM_IDLEST3				0x0028

/* OMAP3-specific register offsets */

#define OMAP3430_CM_CLKEN_PLL				0x0004
#define OMAP3430ES2_CM_CLKEN2				0x0004
#define OMAP3430ES2_CM_FCLKEN3				0x0008
#define OMAP3430_CM_IDLEST_PLL				CM_IDLEST2
#define OMAP3430_CM_AUTOIDLE_PLL			CM_AUTOIDLE2
#define OMAP3430ES2_CM_AUTOIDLE2_PLL			CM_AUTOIDLE2
#define OMAP3430_CM_CLKSEL1				CM_CLKSEL
#define OMAP3430_CM_CLKSEL1_PLL				CM_CLKSEL
#define OMAP3430_CM_CLKSEL2_PLL				CM_CLKSEL2
#define OMAP3430_CM_SLEEPDEP				CM_CLKSEL2
#define OMAP3430_CM_CLKSEL3				OMAP2_CM_CLKSTCTRL
#define OMAP3430_CM_CLKSTST				0x004c
#define OMAP3430ES2_CM_CLKSEL4				0x004c
#define OMAP3430ES2_CM_CLKSEL5				0x0050
#define OMAP3430_CM_CLKSEL2_EMU				0x0050
#define OMAP3430_CM_CLKSEL3_EMU				0x0054


/* CM_IDLEST bit field values to indicate deasserted IdleReq */

#define OMAP24XX_CM_IDLEST_VAL				0
#define OMAP34XX_CM_IDLEST_VAL				1


/* Clock management domain register get/set */

#ifndef __ASSEMBLER__

extern u32 omap2_cm_read_mod_reg(s16 module, u16 idx);
extern void omap2_cm_write_mod_reg(u32 val, s16 module, u16 idx);
extern u32 omap2_cm_rmw_mod_reg_bits(u32 mask, u32 bits, s16 module, s16 idx);

extern int omap2_cm_wait_module_ready(s16 prcm_mod, u8 idlest_id,
				      u8 idlest_shift);
extern u32 omap2_cm_set_mod_reg_bits(u32 bits, s16 module, s16 idx);
extern u32 omap2_cm_clear_mod_reg_bits(u32 bits, s16 module, s16 idx);

extern bool omap2_cm_is_clkdm_in_hwsup(s16 module, u32 mask);
extern void omap2xxx_cm_clkdm_enable_hwsup(s16 module, u32 mask);
extern void omap2xxx_cm_clkdm_disable_hwsup(s16 module, u32 mask);

extern void omap3xxx_cm_clkdm_enable_hwsup(s16 module, u32 mask);
extern void omap3xxx_cm_clkdm_disable_hwsup(s16 module, u32 mask);
extern void omap3xxx_cm_clkdm_force_sleep(s16 module, u32 mask);
extern void omap3xxx_cm_clkdm_force_wakeup(s16 module, u32 mask);

extern void omap2xxx_cm_set_dpll_disable_autoidle(void);
extern void omap2xxx_cm_set_dpll_auto_low_power_stop(void);

extern void omap2xxx_cm_set_apll54_disable_autoidle(void);
extern void omap2xxx_cm_set_apll54_auto_low_power_stop(void);
extern void omap2xxx_cm_set_apll96_disable_autoidle(void);
extern void omap2xxx_cm_set_apll96_auto_low_power_stop(void);

#endif

/* CM register bits shared between 24XX and 3430 */

/* CM_CLKSEL_GFX */
#define OMAP_CLKSEL_GFX_SHIFT				0
#define OMAP_CLKSEL_GFX_MASK				(0x7 << 0)

/* CM_ICLKEN_GFX */
#define OMAP_EN_GFX_SHIFT				0
#define OMAP_EN_GFX_MASK				(1 << 0)

/* CM_IDLEST_GFX */
#define OMAP_ST_GFX_MASK				(1 << 0)


/* Function prototypes */
# ifndef __ASSEMBLER__
extern void omap3_cm_save_context(void);
extern void omap3_cm_restore_context(void);
# endif

#endif
