/*
 * OMAP2xxx Power/Reset Management (PRM) register definitions
 *
 * Copyright (C) 2007-2009, 2011-2012 Texas Instruments, Inc.
 * Copyright (C) 2008-2010 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The PRM hardware modules on the OMAP2/3 are quite similar to each
 * other.  The PRM on OMAP4 has a new register layout, and is handled
 * in a separate file.
 */
#ifndef __ARCH_ARM_MACH_OMAP2_PRM2XXX_H
#define __ARCH_ARM_MACH_OMAP2_PRM2XXX_H

#include "prcm-common.h"
#include "prm.h"
#include "prm2xxx_3xxx.h"

#define OMAP2420_PRM_REGADDR(module, reg)				\
		OMAP2_L4_IO_ADDRESS(OMAP2420_PRM_BASE + (module) + (reg))
#define OMAP2430_PRM_REGADDR(module, reg)				\
		OMAP2_L4_IO_ADDRESS(OMAP2430_PRM_BASE + (module) + (reg))

/*
 * OMAP2-specific global PRM registers
 * Use {read,write}l_relaxed() with these registers.
 *
 * With a few exceptions, these are the register names beginning with
 * PRCM_* on 24xx.  (The exceptions are the IRQSTATUS and IRQENABLE
 * bits.)
 *
 */

#define OMAP2_PRCM_REVISION_OFFSET	0x0000
#define OMAP2420_PRCM_REVISION		OMAP2420_PRM_REGADDR(OCP_MOD, 0x0000)
#define OMAP2_PRCM_SYSCONFIG_OFFSET	0x0010
#define OMAP2420_PRCM_SYSCONFIG		OMAP2420_PRM_REGADDR(OCP_MOD, 0x0010)

#define OMAP2_PRCM_IRQSTATUS_MPU_OFFSET	0x0018
#define OMAP2420_PRCM_IRQSTATUS_MPU	OMAP2420_PRM_REGADDR(OCP_MOD, 0x0018)
#define OMAP2_PRCM_IRQENABLE_MPU_OFFSET	0x001c
#define OMAP2420_PRCM_IRQENABLE_MPU	OMAP2420_PRM_REGADDR(OCP_MOD, 0x001c)

#define OMAP2_PRCM_VOLTCTRL_OFFSET	0x0050
#define OMAP2420_PRCM_VOLTCTRL		OMAP2420_PRM_REGADDR(OCP_MOD, 0x0050)
#define OMAP2_PRCM_VOLTST_OFFSET	0x0054
#define OMAP2420_PRCM_VOLTST		OMAP2420_PRM_REGADDR(OCP_MOD, 0x0054)
#define OMAP2_PRCM_CLKSRC_CTRL_OFFSET	0x0060
#define OMAP2420_PRCM_CLKSRC_CTRL	OMAP2420_PRM_REGADDR(OCP_MOD, 0x0060)
#define OMAP2_PRCM_CLKOUT_CTRL_OFFSET	0x0070
#define OMAP2420_PRCM_CLKOUT_CTRL	OMAP2420_PRM_REGADDR(OCP_MOD, 0x0070)
#define OMAP2_PRCM_CLKEMUL_CTRL_OFFSET	0x0078
#define OMAP2420_PRCM_CLKEMUL_CTRL	OMAP2420_PRM_REGADDR(OCP_MOD, 0x0078)
#define OMAP2_PRCM_CLKCFG_CTRL_OFFSET	0x0080
#define OMAP2420_PRCM_CLKCFG_CTRL	OMAP2420_PRM_REGADDR(OCP_MOD, 0x0080)
#define OMAP2_PRCM_CLKCFG_STATUS_OFFSET	0x0084
#define OMAP2420_PRCM_CLKCFG_STATUS	OMAP2420_PRM_REGADDR(OCP_MOD, 0x0084)
#define OMAP2_PRCM_VOLTSETUP_OFFSET	0x0090
#define OMAP2420_PRCM_VOLTSETUP		OMAP2420_PRM_REGADDR(OCP_MOD, 0x0090)
#define OMAP2_PRCM_CLKSSETUP_OFFSET	0x0094
#define OMAP2420_PRCM_CLKSSETUP		OMAP2420_PRM_REGADDR(OCP_MOD, 0x0094)
#define OMAP2_PRCM_POLCTRL_OFFSET	0x0098
#define OMAP2420_PRCM_POLCTRL		OMAP2420_PRM_REGADDR(OCP_MOD, 0x0098)

#define OMAP2430_PRCM_REVISION		OMAP2430_PRM_REGADDR(OCP_MOD, 0x0000)
#define OMAP2430_PRCM_SYSCONFIG		OMAP2430_PRM_REGADDR(OCP_MOD, 0x0010)

#define OMAP2430_PRCM_IRQSTATUS_MPU	OMAP2430_PRM_REGADDR(OCP_MOD, 0x0018)
#define OMAP2430_PRCM_IRQENABLE_MPU	OMAP2430_PRM_REGADDR(OCP_MOD, 0x001c)

#define OMAP2430_PRCM_VOLTCTRL		OMAP2430_PRM_REGADDR(OCP_MOD, 0x0050)
#define OMAP2430_PRCM_VOLTST		OMAP2430_PRM_REGADDR(OCP_MOD, 0x0054)
#define OMAP2430_PRCM_CLKSRC_CTRL	OMAP2430_PRM_REGADDR(OCP_MOD, 0x0060)
#define OMAP2430_PRCM_CLKOUT_CTRL	OMAP2430_PRM_REGADDR(OCP_MOD, 0x0070)
#define OMAP2430_PRCM_CLKEMUL_CTRL	OMAP2430_PRM_REGADDR(OCP_MOD, 0x0078)
#define OMAP2430_PRCM_CLKCFG_CTRL	OMAP2430_PRM_REGADDR(OCP_MOD, 0x0080)
#define OMAP2430_PRCM_CLKCFG_STATUS	OMAP2430_PRM_REGADDR(OCP_MOD, 0x0084)
#define OMAP2430_PRCM_VOLTSETUP		OMAP2430_PRM_REGADDR(OCP_MOD, 0x0090)
#define OMAP2430_PRCM_CLKSSETUP		OMAP2430_PRM_REGADDR(OCP_MOD, 0x0094)
#define OMAP2430_PRCM_POLCTRL		OMAP2430_PRM_REGADDR(OCP_MOD, 0x0098)

/*
 * Module specific PRM register offsets from PRM_BASE + domain offset
 *
 * Use prm_{read,write}_mod_reg() with these registers.
 *
 * With a few exceptions, these are the register names beginning with
 * {PM,RM}_* on both OMAP2/3 SoC families..  (The exceptions are the
 * IRQSTATUS and IRQENABLE bits.)
 */

/* Register offsets appearing on both OMAP2 and OMAP3 */

#define OMAP2_RM_RSTCTRL				0x0050
#define OMAP2_RM_RSTTIME				0x0054
#define OMAP2_RM_RSTST					0x0058
#define OMAP2_PM_PWSTCTRL				0x00e0
#define OMAP2_PM_PWSTST					0x00e4

#define PM_WKEN						0x00a0
#define PM_WKEN1					PM_WKEN
#define PM_WKST						0x00b0
#define PM_WKST1					PM_WKST
#define PM_WKDEP					0x00c8
#define PM_EVGENCTRL					0x00d4
#define PM_EVGENONTIM					0x00d8
#define PM_EVGENOFFTIM					0x00dc

/* OMAP2xxx specific register offsets */
#define OMAP24XX_PM_WKEN2				0x00a4
#define OMAP24XX_PM_WKST2				0x00b4

#define OMAP24XX_PRCM_IRQSTATUS_DSP			0x00f0	/* IVA mod */
#define OMAP24XX_PRCM_IRQENABLE_DSP			0x00f4	/* IVA mod */
#define OMAP24XX_PRCM_IRQSTATUS_IVA			0x00f8
#define OMAP24XX_PRCM_IRQENABLE_IVA			0x00fc

#ifndef __ASSEMBLER__
/* Function prototypes */
extern int omap2xxx_clkdm_sleep(struct clockdomain *clkdm);
extern int omap2xxx_clkdm_wakeup(struct clockdomain *clkdm);

extern void omap2xxx_prm_dpll_reset(void);

extern int __init omap2xxx_prm_init(void);

#endif

#endif
