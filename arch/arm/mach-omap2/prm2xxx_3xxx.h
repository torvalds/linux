/*
 * OMAP2/3 Power/Reset Management (PRM) register definitions
 *
 * Copyright (C) 2007-2009 Texas Instruments, Inc.
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
#ifndef __ARCH_ARM_MACH_OMAP2_PRM2XXX_3XXX_H
#define __ARCH_ARM_MACH_OMAP2_PRM2XXX_3XXX_H

#include "prcm-common.h"
#include "prm.h"

#define OMAP2420_PRM_REGADDR(module, reg)				\
		OMAP2_L4_IO_ADDRESS(OMAP2420_PRM_BASE + (module) + (reg))
#define OMAP2430_PRM_REGADDR(module, reg)				\
		OMAP2_L4_IO_ADDRESS(OMAP2430_PRM_BASE + (module) + (reg))
#define OMAP34XX_PRM_REGADDR(module, reg)				\
		OMAP2_L4_IO_ADDRESS(OMAP3430_PRM_BASE + (module) + (reg))


/*
 * OMAP2-specific global PRM registers
 * Use __raw_{read,write}l() with these registers.
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
 * OMAP3-specific global PRM registers
 * Use __raw_{read,write}l() with these registers.
 *
 * With a few exceptions, these are the register names beginning with
 * PRM_* on 34xx.  (The exceptions are the IRQSTATUS and IRQENABLE
 * bits.)
 */

#define OMAP3_PRM_REVISION_OFFSET	0x0004
#define OMAP3430_PRM_REVISION		OMAP34XX_PRM_REGADDR(OCP_MOD, 0x0004)
#define OMAP3_PRM_SYSCONFIG_OFFSET	0x0014
#define OMAP3430_PRM_SYSCONFIG		OMAP34XX_PRM_REGADDR(OCP_MOD, 0x0014)

#define OMAP3_PRM_IRQSTATUS_MPU_OFFSET	0x0018
#define OMAP3430_PRM_IRQSTATUS_MPU	OMAP34XX_PRM_REGADDR(OCP_MOD, 0x0018)
#define OMAP3_PRM_IRQENABLE_MPU_OFFSET	0x001c
#define OMAP3430_PRM_IRQENABLE_MPU	OMAP34XX_PRM_REGADDR(OCP_MOD, 0x001c)


#define OMAP3_PRM_VC_SMPS_SA_OFFSET	0x0020
#define OMAP3430_PRM_VC_SMPS_SA		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0020)
#define OMAP3_PRM_VC_SMPS_VOL_RA_OFFSET	0x0024
#define OMAP3430_PRM_VC_SMPS_VOL_RA	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0024)
#define OMAP3_PRM_VC_SMPS_CMD_RA_OFFSET	0x0028
#define OMAP3430_PRM_VC_SMPS_CMD_RA	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0028)
#define OMAP3_PRM_VC_CMD_VAL_0_OFFSET	0x002c
#define OMAP3430_PRM_VC_CMD_VAL_0	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x002c)
#define OMAP3_PRM_VC_CMD_VAL_1_OFFSET	0x0030
#define OMAP3430_PRM_VC_CMD_VAL_1	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0030)
#define OMAP3_PRM_VC_CH_CONF_OFFSET	0x0034
#define OMAP3430_PRM_VC_CH_CONF		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0034)
#define OMAP3_PRM_VC_I2C_CFG_OFFSET	0x0038
#define OMAP3430_PRM_VC_I2C_CFG		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0038)
#define OMAP3_PRM_VC_BYPASS_VAL_OFFSET	0x003c
#define OMAP3430_PRM_VC_BYPASS_VAL	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x003c)
#define OMAP3_PRM_RSTCTRL_OFFSET	0x0050
#define OMAP3430_PRM_RSTCTRL		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0050)
#define OMAP3_PRM_RSTTIME_OFFSET	0x0054
#define OMAP3430_PRM_RSTTIME		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0054)
#define OMAP3_PRM_RSTST_OFFSET	0x0058
#define OMAP3430_PRM_RSTST		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0058)
#define OMAP3_PRM_VOLTCTRL_OFFSET	0x0060
#define OMAP3430_PRM_VOLTCTRL		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0060)
#define OMAP3_PRM_SRAM_PCHARGE_OFFSET	0x0064
#define OMAP3430_PRM_SRAM_PCHARGE	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0064)
#define OMAP3_PRM_CLKSRC_CTRL_OFFSET	0x0070
#define OMAP3430_PRM_CLKSRC_CTRL	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0070)
#define OMAP3_PRM_VOLTSETUP1_OFFSET	0x0090
#define OMAP3430_PRM_VOLTSETUP1		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0090)
#define OMAP3_PRM_VOLTOFFSET_OFFSET	0x0094
#define OMAP3430_PRM_VOLTOFFSET		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0094)
#define OMAP3_PRM_CLKSETUP_OFFSET	0x0098
#define OMAP3430_PRM_CLKSETUP		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x0098)
#define OMAP3_PRM_POLCTRL_OFFSET	0x009c
#define OMAP3430_PRM_POLCTRL		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x009c)
#define OMAP3_PRM_VOLTSETUP2_OFFSET	0x00a0
#define OMAP3430_PRM_VOLTSETUP2		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00a0)
#define OMAP3_PRM_VP1_CONFIG_OFFSET	0x00b0
#define OMAP3430_PRM_VP1_CONFIG		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00b0)
#define OMAP3_PRM_VP1_VSTEPMIN_OFFSET	0x00b4
#define OMAP3430_PRM_VP1_VSTEPMIN	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00b4)
#define OMAP3_PRM_VP1_VSTEPMAX_OFFSET	0x00b8
#define OMAP3430_PRM_VP1_VSTEPMAX	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00b8)
#define OMAP3_PRM_VP1_VLIMITTO_OFFSET	0x00bc
#define OMAP3430_PRM_VP1_VLIMITTO	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00bc)
#define OMAP3_PRM_VP1_VOLTAGE_OFFSET	0x00c0
#define OMAP3430_PRM_VP1_VOLTAGE	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00c0)
#define OMAP3_PRM_VP1_STATUS_OFFSET	0x00c4
#define OMAP3430_PRM_VP1_STATUS		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00c4)
#define OMAP3_PRM_VP2_CONFIG_OFFSET	0x00d0
#define OMAP3430_PRM_VP2_CONFIG		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00d0)
#define OMAP3_PRM_VP2_VSTEPMIN_OFFSET	0x00d4
#define OMAP3430_PRM_VP2_VSTEPMIN	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00d4)
#define OMAP3_PRM_VP2_VSTEPMAX_OFFSET	0x00d8
#define OMAP3430_PRM_VP2_VSTEPMAX	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00d8)
#define OMAP3_PRM_VP2_VLIMITTO_OFFSET	0x00dc
#define OMAP3430_PRM_VP2_VLIMITTO	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00dc)
#define OMAP3_PRM_VP2_VOLTAGE_OFFSET	0x00e0
#define OMAP3430_PRM_VP2_VOLTAGE	OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00e0)
#define OMAP3_PRM_VP2_STATUS_OFFSET	0x00e4
#define OMAP3430_PRM_VP2_STATUS		OMAP34XX_PRM_REGADDR(OMAP3430_GR_MOD, 0x00e4)

#define OMAP3_PRM_CLKSEL_OFFSET	0x0040
#define OMAP3430_PRM_CLKSEL		OMAP34XX_PRM_REGADDR(OMAP3430_CCR_MOD, 0x0040)
#define OMAP3_PRM_CLKOUT_CTRL_OFFSET	0x0070
#define OMAP3430_PRM_CLKOUT_CTRL	OMAP34XX_PRM_REGADDR(OMAP3430_CCR_MOD, 0x0070)

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

/* OMAP3 specific register offsets */
#define OMAP3430ES2_PM_WKEN3				0x00f0
#define OMAP3430ES2_PM_WKST3				0x00b8

#define OMAP3430_PM_MPUGRPSEL				0x00a4
#define OMAP3430_PM_MPUGRPSEL1				OMAP3430_PM_MPUGRPSEL
#define OMAP3430ES2_PM_MPUGRPSEL3			0x00f8

#define OMAP3430_PM_IVAGRPSEL				0x00a8
#define OMAP3430_PM_IVAGRPSEL1				OMAP3430_PM_IVAGRPSEL
#define OMAP3430ES2_PM_IVAGRPSEL3			0x00f4

#define OMAP3430_PM_PREPWSTST				0x00e8

#define OMAP3430_PRM_IRQSTATUS_IVA2			0x00f8
#define OMAP3430_PRM_IRQENABLE_IVA2			0x00fc


#ifndef __ASSEMBLER__
/*
 * Stub omap2xxx/omap3xxx functions so that common files
 * continue to build when custom builds are used
 */
#if defined(CONFIG_ARCH_OMAP4) && !(defined(CONFIG_ARCH_OMAP2) ||	\
					defined(CONFIG_ARCH_OMAP3))
static inline u32 omap2_prm_read_mod_reg(s16 module, u16 idx)
{
	WARN(1, "prm: omap2xxx/omap3xxx specific function and "
		"not suppose to be used on omap4\n");
	return 0;
}
static inline void omap2_prm_write_mod_reg(u32 val, s16 module, u16 idx)
{
	WARN(1, "prm: omap2xxx/omap3xxx specific function and "
		"not suppose to be used on omap4\n");
}
static inline u32 omap2_prm_rmw_mod_reg_bits(u32 mask, u32 bits,
		s16 module, s16 idx)
{
	WARN(1, "prm: omap2xxx/omap3xxx specific function and "
		"not suppose to be used on omap4\n");
	return 0;
}
static inline u32 omap2_prm_set_mod_reg_bits(u32 bits, s16 module, s16 idx)
{
	WARN(1, "prm: omap2xxx/omap3xxx specific function and "
		"not suppose to be used on omap4\n");
	return 0;
}
static inline u32 omap2_prm_clear_mod_reg_bits(u32 bits, s16 module, s16 idx)
{
	WARN(1, "prm: omap2xxx/omap3xxx specific function and "
		"not suppose to be used on omap4\n");
	return 0;
}
static inline u32 omap2_prm_read_mod_bits_shift(s16 domain, s16 idx, u32 mask)
{
	WARN(1, "prm: omap2xxx/omap3xxx specific function and "
		"not suppose to be used on omap4\n");
	return 0;
}
static inline int omap2_prm_is_hardreset_asserted(s16 prm_mod, u8 shift)
{
	WARN(1, "prm: omap2xxx/omap3xxx specific function and "
		"not suppose to be used on omap4\n");
	return 0;
}
static inline int omap2_prm_assert_hardreset(s16 prm_mod, u8 shift)
{
	WARN(1, "prm: omap2xxx/omap3xxx specific function and "
		"not suppose to be used on omap4\n");
	return 0;
}
static inline int omap2_prm_deassert_hardreset(s16 prm_mod, u8 shift)
{
	WARN(1, "prm: omap2xxx/omap3xxx specific function and "
		"not suppose to be used on omap4\n");
	return 0;
}
#else
/* Power/reset management domain register get/set */
extern u32 omap2_prm_read_mod_reg(s16 module, u16 idx);
extern void omap2_prm_write_mod_reg(u32 val, s16 module, u16 idx);
extern u32 omap2_prm_rmw_mod_reg_bits(u32 mask, u32 bits, s16 module, s16 idx);
extern u32 omap2_prm_set_mod_reg_bits(u32 bits, s16 module, s16 idx);
extern u32 omap2_prm_clear_mod_reg_bits(u32 bits, s16 module, s16 idx);
extern u32 omap2_prm_read_mod_bits_shift(s16 domain, s16 idx, u32 mask);

/* These omap2_ PRM functions apply to both OMAP2 and 3 */
extern int omap2_prm_is_hardreset_asserted(s16 prm_mod, u8 shift);
extern int omap2_prm_assert_hardreset(s16 prm_mod, u8 shift);
extern int omap2_prm_deassert_hardreset(s16 prm_mod, u8 shift);

#endif	/* CONFIG_ARCH_OMAP4 */
#endif

/*
 * Bits common to specific registers
 *
 * The 3430 register and bit names are generally used,
 * since they tend to make more sense
 */

/* PM_EVGENONTIM_MPU */
/* Named PM_EVEGENONTIM_MPU on the 24XX */
#define OMAP_ONTIMEVAL_SHIFT				0
#define OMAP_ONTIMEVAL_MASK				(0xffffffff << 0)

/* PM_EVGENOFFTIM_MPU */
/* Named PM_EVEGENOFFTIM_MPU on the 24XX */
#define OMAP_OFFTIMEVAL_SHIFT				0
#define OMAP_OFFTIMEVAL_MASK				(0xffffffff << 0)

/* PRM_CLKSETUP and PRCM_VOLTSETUP */
/* Named PRCM_CLKSSETUP on the 24XX */
#define OMAP_SETUP_TIME_SHIFT				0
#define OMAP_SETUP_TIME_MASK				(0xffff << 0)

/* PRM_CLKSRC_CTRL */
/* Named PRCM_CLKSRC_CTRL on the 24XX */
#define OMAP_SYSCLKDIV_SHIFT				6
#define OMAP_SYSCLKDIV_MASK				(0x3 << 6)
#define OMAP_AUTOEXTCLKMODE_SHIFT			3
#define OMAP_AUTOEXTCLKMODE_MASK			(0x3 << 3)
#define OMAP_SYSCLKSEL_SHIFT				0
#define OMAP_SYSCLKSEL_MASK				(0x3 << 0)

/* PM_EVGENCTRL_MPU */
#define OMAP_OFFLOADMODE_SHIFT				3
#define OMAP_OFFLOADMODE_MASK				(0x3 << 3)
#define OMAP_ONLOADMODE_SHIFT				1
#define OMAP_ONLOADMODE_MASK				(0x3 << 1)
#define OMAP_ENABLE_MASK				(1 << 0)

/* PRM_RSTTIME */
/* Named RM_RSTTIME_WKUP on the 24xx */
#define OMAP_RSTTIME2_SHIFT				8
#define OMAP_RSTTIME2_MASK				(0x1f << 8)
#define OMAP_RSTTIME1_SHIFT				0
#define OMAP_RSTTIME1_MASK				(0xff << 0)

/* PRM_RSTCTRL */
/* Named RM_RSTCTRL_WKUP on the 24xx */
/* 2420 calls RST_DPLL3 'RST_DPLL' */
#define OMAP_RST_DPLL3_MASK				(1 << 2)
#define OMAP_RST_GS_MASK				(1 << 1)


/*
 * Bits common to module-shared registers
 *
 * Not all registers of a particular type support all of these bits -
 * check TRM if you are unsure
 */

/*
 * 24XX: RM_RSTST_MPU and RM_RSTST_DSP - on 24XX, 'COREDOMAINWKUP_RST' is
 *	 called 'COREWKUP_RST'
 *
 * 3430: RM_RSTST_IVA2, RM_RSTST_MPU, RM_RSTST_GFX, RM_RSTST_DSS,
 *	 RM_RSTST_CAM, RM_RSTST_PER, RM_RSTST_NEON
 */
#define OMAP_COREDOMAINWKUP_RST_MASK			(1 << 3)

/*
 * 24XX: RM_RSTST_MPU, RM_RSTST_GFX, RM_RSTST_DSP
 *
 * 2430: RM_RSTST_MDM
 *
 * 3430: RM_RSTST_CORE, RM_RSTST_EMU
 */
#define OMAP_DOMAINWKUP_RST_MASK			(1 << 2)

/*
 * 24XX: RM_RSTST_MPU, RM_RSTST_WKUP, RM_RSTST_DSP
 *	 On 24XX, 'GLOBALWARM_RST' is called 'GLOBALWMPU_RST'.
 *
 * 2430: RM_RSTST_MDM
 *
 * 3430: RM_RSTST_CORE, RM_RSTST_EMU
 */
#define OMAP_GLOBALWARM_RST_MASK			(1 << 1)
#define OMAP_GLOBALCOLD_RST_MASK			(1 << 0)

/*
 * 24XX: PM_WKDEP_GFX, PM_WKDEP_MPU, PM_WKDEP_CORE, PM_WKDEP_DSP
 *	 2420 TRM sometimes uses "EN_WAKEUP" instead of "EN_WKUP"
 *
 * 2430: PM_WKDEP_MDM
 *
 * 3430: PM_WKDEP_IVA2, PM_WKDEP_GFX, PM_WKDEP_DSS, PM_WKDEP_CAM,
 *	 PM_WKDEP_PER
 */
#define OMAP_EN_WKUP_SHIFT				4
#define OMAP_EN_WKUP_MASK				(1 << 4)

/*
 * 24XX: PM_PWSTCTRL_MPU, PM_PWSTCTRL_CORE, PM_PWSTCTRL_GFX,
 *	 PM_PWSTCTRL_DSP
 *
 * 2430: PM_PWSTCTRL_MDM
 *
 * 3430: PM_PWSTCTRL_IVA2, PM_PWSTCTRL_CORE, PM_PWSTCTRL_GFX,
 *	 PM_PWSTCTRL_DSS, PM_PWSTCTRL_CAM, PM_PWSTCTRL_PER,
 *	 PM_PWSTCTRL_NEON
 */
#define OMAP_LOGICRETSTATE_MASK				(1 << 2)


/*
 * MAX_MODULE_HARDRESET_WAIT: Maximum microseconds to wait for an OMAP
 * submodule to exit hardreset
 */
#define MAX_MODULE_HARDRESET_WAIT		10000


#endif
