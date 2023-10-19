/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OMAP2/3 Clock Management (CM) register definitions
 *
 * Copyright (C) 2007-2009 Texas Instruments, Inc.
 * Copyright (C) 2007-2010 Nokia Corporation
 * Paul Walmsley
 *
 * The CM hardware modules on the OMAP2/3 are quite similar to each
 * other.  The CM modules/instances on OMAP4 are quite different, so
 * they are handled in a separate file.
 */
#ifndef __ARCH_ASM_MACH_OMAP2_CM3XXX_H
#define __ARCH_ASM_MACH_OMAP2_CM3XXX_H

#include "prcm-common.h"
#include "cm2xxx_3xxx.h"

#define OMAP34XX_CM_REGADDR(module, reg)				\
			OMAP2_L4_IO_ADDRESS(OMAP3430_CM_BASE + (module) + (reg))


/*
 * OMAP3-specific global CM registers
 * Use cm_{read,write}_reg() with these registers.
 * These registers appear once per CM module.
 */

#define OMAP3430_CM_SYSCONFIG		0x0010
#define OMAP3430_CM_POLCTRL		0x009c

#define OMAP3_CM_CLKOUT_CTRL_OFFSET	0x0070
#define OMAP3430_CM_CLKOUT_CTRL		OMAP_CM_REGADDR(OMAP3430_CCR_MOD, 0x0070)

/*
 * Module specific CM register offsets from CM_BASE + domain offset
 * Use cm_{read,write}_mod_reg() with these registers.
 * These register offsets generally appear in more than one PRCM submodule.
 */

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

#define OMAP34XX_CM_IDLEST_VAL				1


#ifndef __ASSEMBLER__

extern void omap3_cm_save_context(void);
extern void omap3_cm_restore_context(void);
extern void omap3_cm_save_scratchpad_contents(u32 *ptr);

int __init omap3xxx_cm_init(const struct omap_prcm_init_data *data);

#endif

#endif
